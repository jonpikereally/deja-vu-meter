import SwiftUI
import UniformTypeIdentifiers

/// Everything that has been copied into the app.
struct LibraryView: View {

    @ObservedObject var store: LibraryStore
    let onEdit: (Track) -> Void

    @State private var isImporting = false

    var body: some View {
        VStack(spacing: 10) {
            header

            if store.tracks.isEmpty {
                emptyState
            } else {
                List {
                    ForEach(store.tracks) { track in
                        Button { onEdit(track) } label: { row(track) }
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
            Text("ADD FILES reaches local storage, iCloud Drive, Dropbox and Google Drive - anything showing in the Files app. Tracks are copied into the app, so they keep working offline.")
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
                Text(detail(for: track))
                    .font(.system(size: 10, weight: .medium, design: .rounded))
                    .monospacedDigit()
                    .foregroundColor(Theme.label)
                    .lineLimit(1)
            }
            Spacer()
            Image(systemName: "slider.horizontal.3")
                .font(.system(size: 12, weight: .semibold))
                .foregroundColor(track.isTrimmed || track.hasFades ? Theme.amber : Theme.label.opacity(0.5))
        }
        .padding(.vertical, 4)
        .contentShape(Rectangle())
    }

    private func detail(for track: Track) -> String {
        var parts = [TimeFormat.clock(track.duration)]
        if track.isTrimmed {
            parts.append("trim \(TimeFormat.clock(track.startPoint))-\(TimeFormat.clock(track.endPoint))")
        }
        if track.hasFades {
            parts.append("in \(TimeFormat.seconds(track.fadeIn)) / out \(TimeFormat.seconds(track.fadeOut))")
        }
        return parts.joined(separator: "  -  ")
    }
}
