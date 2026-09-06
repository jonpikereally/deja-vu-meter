import SwiftUI
import UniformTypeIdentifiers

/// Everything that has been copied into the app.
struct LibraryView: View {

    @ObservedObject var store: LibraryStore

    @State private var isImporting = false
    @State private var renaming: Track?

    var body: some View {
        VStack(spacing: 10) {
            header

            if store.tracks.isEmpty {
                emptyState
            } else {
                List {
                    ForEach(store.tracks) { track in
                        Button { renaming = track } label: { row(track) }
                            .buttonStyle(.plain)
                            .listRowBackground(Theme.panel)
                            .listRowSeparatorTint(Theme.hairline)
                    }
                    .onDelete(perform: store.deleteTracks)
                }
                .listStyle(.plain)
                .scrollContentBackground(.hidden)
            }
        }
        .fileImporter(
            isPresented: $isImporting,
            allowedContentTypes: [.audio],
            allowsMultipleSelection: true
        ) { result in
            switch result {
            case .success(let urls): store.importFiles(urls)
            case .failure(let error): store.lastError = error.localizedDescription
            }
        }
        .sheet(item: $renaming) { track in
            RenameTrackView(track: track) { store.rename(track, to: $0) }
        }
    }

    // MARK: - Pieces

    private var header: some View {
        HStack {
            VStack(alignment: .leading, spacing: 2) {
                Text("LIBRARY")
                    .font(.system(size: 11, weight: .bold, design: .rounded))
                    .kerning(1)
                    .foregroundColor(Theme.label)
                Text(subtitle)
                    .font(.system(size: 10, weight: .medium, design: .rounded))
                    .foregroundColor(Theme.label.opacity(0.7))
            }

            Spacer()

            if store.isImporting {
                ProgressView().tint(Theme.amber)
            }

            Button { isImporting = true } label: {
                Label("ADD FILES", systemImage: "plus")
                    .font(.system(size: 11, weight: .bold, design: .rounded))
                    .foregroundColor(.black)
                    .padding(.horizontal, 12)
                    .padding(.vertical, 8)
                    .background(
                        RoundedRectangle(cornerRadius: 9, style: .continuous)
                            .fill(Theme.amber)
                    )
            }
            .buttonStyle(.plain)
            .disabled(store.isImporting)
        }
    }

    private var subtitle: String {
        let count = store.tracks.count
        let megabytes = Double(store.storageUsed) / 1_000_000
        return "\(count) track\(count == 1 ? "" : "s") - \(String(format: "%.0f", megabytes)) MB in the app"
    }

    private var emptyState: some View {
        VStack(spacing: 10) {
            Spacer()
            Image(systemName: "music.note.list")
                .font(.system(size: 34, weight: .light))
                .foregroundColor(Theme.label.opacity(0.4))
            Text("No tracks yet")
                .font(.system(size: 14, weight: .semibold, design: .rounded))
                .foregroundColor(Theme.labelStrong)
            Text("ADD FILES reaches local storage, iCloud Drive, Dropbox and Google Drive - anything showing in the Files app. Tracks are copied into the app, so they keep working offline. Fades and edit points are set per setlist entry, over in EVENTS.")
                .font(.system(size: 11, weight: .medium, design: .rounded))
                .foregroundColor(Theme.label)
                .multilineTextAlignment(.center)
                .padding(.horizontal, 24)
            Spacer()
        }
    }

    private func row(_ track: Track) -> some View {
        HStack(spacing: 10) {
            VStack(alignment: .leading, spacing: 3) {
                Text(track.title)
                    .font(.system(size: 13, weight: .semibold, design: .rounded))
                    .foregroundColor(Theme.labelStrong)
                    .lineLimit(1)
                Text(TimeFormat.clock(track.duration))
                    .font(.system(size: 10, weight: .medium, design: .rounded))
                    .monospacedDigit()
                    .foregroundColor(Theme.label)
            }
            Spacer()
            Image(systemName: "pencil")
                .font(.system(size: 11, weight: .semibold))
                .foregroundColor(Theme.label.opacity(0.5))
        }
        .padding(.vertical, 4)
        .contentShape(Rectangle())
    }
}

/// Titles are taken from filenames on import, which is usually close and
/// sometimes wrong, so they can be corrected in place.
struct RenameTrackView: View {

    let track: Track
    let onCommit: (String) -> Void

    @State private var title: String
    @Environment(\.dismiss) private var dismiss

    init(track: Track, onCommit: @escaping (String) -> Void) {
        self.track = track
        self.onCommit = onCommit
        _title = State(initialValue: track.title)
    }

    var body: some View {
        NavigationStack {
            VStack(spacing: 14) {
                TextField("Title", text: $title)
                    .textFieldStyle(.plain)
                    .font(.system(size: 17, weight: .semibold, design: .rounded))
                    .foregroundColor(Theme.labelStrong)
                    .padding(12)
                    .background(
                        RoundedRectangle(cornerRadius: 10, style: .continuous)
                            .fill(Theme.panel)
                    )

                Text("Titles come from the filename on import, not from the file's tags.")
                    .font(.system(size: 10, weight: .medium, design: .rounded))
                    .foregroundColor(Theme.label)
                    .multilineTextAlignment(.center)

                Spacer()
            }
            .padding(18)
            .background(Theme.background.ignoresSafeArea())
            .navigationTitle("Rename")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .confirmationAction) {
                    Button("Done") {
                        onCommit(title)
                        dismiss()
                    }
                }
            }
        }
        .preferredColorScheme(.dark)
    }
}
