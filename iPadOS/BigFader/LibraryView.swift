import SwiftUI
import UniformTypeIdentifiers

/// Everything that has been copied into the app.
struct LibraryView: View {

    @ObservedObject var store: LibraryStore

    @State private var isImporting = false
    @State private var editing: Track?
    @State private var filter: Set<String> = []

    private var visible: [Track] { store.tracks(matching: filter) }

    var body: some View {
        VStack(spacing: 10) {
            header
            TagFilterBar(tags: store.allTags, selected: $filter)

            if store.tracks.isEmpty {
                emptyLibrary
            } else if visible.isEmpty {
                emptyFilter
            } else {
                List {
                    ForEach(visible) { track in
                        Button { editing = track } label: { row(track) }
                            .buttonStyle(.plain)
                            .listRowBackground(Theme.panel)
                            .listRowSeparatorTint(Theme.hairline)
                    }
                    .onDelete(perform: delete)
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
        .sheet(item: $editing) { track in
            TrackDetailView(track: track, store: store)
        }
    }

    /// The list may be filtered, so a swipe deletes what the row shows rather
    /// than whatever sits at that index in the full library.
    private func delete(at offsets: IndexSet) {
        let doomed = Set(offsets.map { visible[$0].id })
        let realOffsets = IndexSet(
            store.tracks.enumerated()
                .filter { doomed.contains($0.element.id) }
                .map(\.offset)
        )
        store.deleteTracks(at: realOffsets)
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
        let count = filter.isEmpty ? store.tracks.count : visible.count
        let megabytes = Double(store.storageUsed) / 1_000_000
        let scope = filter.isEmpty ? "" : " of \(store.tracks.count)"
        return "\(count)\(scope) track\(count == 1 ? "" : "s") - \(String(format: "%.0f", megabytes)) MB in the app"
    }

    private var emptyLibrary: some View {
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

    private var emptyFilter: some View {
        VStack(spacing: 8) {
            Spacer()
            Text("Nothing with those tags")
                .font(.system(size: 14, weight: .semibold, design: .rounded))
                .foregroundColor(Theme.labelStrong)
            Text("Tag filters narrow rather than widen - a track has to carry all of them.")
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
                HStack(spacing: 6) {
                    Text(TimeFormat.clock(track.duration))
                        .monospacedDigit()
                    if !track.tags.isEmpty {
                        Text(track.tags.joined(separator: ", "))
                            .foregroundColor(Theme.amber.opacity(0.9))
                            .lineLimit(1)
                    }
                }
                .font(.system(size: 10, weight: .medium, design: .rounded))
                .foregroundColor(Theme.label)
            }
            Spacer()
            Image(systemName: "tag")
                .font(.system(size: 11, weight: .semibold))
                .foregroundColor(track.tags.isEmpty ? Theme.label.opacity(0.4) : Theme.amber)
        }
        .padding(.vertical, 4)
        .contentShape(Rectangle())
    }
}
