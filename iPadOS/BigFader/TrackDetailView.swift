import SwiftUI

/// A track's name and its tags.
///
/// Titles are taken from filenames on import, which is usually close and
/// sometimes wrong, so they can be corrected here too.
struct TrackDetailView: View {

    let track: Track
    @ObservedObject private var store: LibraryStore

    @State private var title: String
    @State private var newTag = ""
    @Environment(\.dismiss) private var dismiss

    init(track: Track, store: LibraryStore) {
        self.track = track
        _store = ObservedObject(wrappedValue: store)
        _title = State(initialValue: track.title)
    }

    /// Read back out of the store rather than held in state, so a tag added
    /// here appears immediately and survives the sheet being reopened.
    private var current: Track {
        store.track(id: track.id) ?? track
    }

    private var suggestions: [String] {
        store.allTags.filter { !current.matches(tag: $0) }
    }

    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(alignment: .leading, spacing: 18) {
                    field
                    tags
                    if !suggestions.isEmpty { suggested }
                }
                .padding(18)
                .frame(maxWidth: .infinity, alignment: .leading)
            }
            .background(Theme.background.ignoresSafeArea())
            .navigationTitle("Track")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .confirmationAction) {
                    Button("Done") {
                        store.rename(track, to: title)
                        dismiss()
                    }
                }
            }
        }
        .preferredColorScheme(.dark)
    }

    // MARK: - Pieces

    private var field: some View {
        VStack(alignment: .leading, spacing: 6) {
            caption("TITLE")
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
        }
    }

    private var tags: some View {
        VStack(alignment: .leading, spacing: 8) {
            caption("TAGS")

            if current.tags.isEmpty {
                Text("No tags yet.")
                    .font(.system(size: 11, weight: .medium, design: .rounded))
                    .foregroundColor(Theme.label)
            } else {
                WrappedTags(tags: current.tags) { tag in
                    TagChip(text: tag, isSelected: true, showsRemove: true) {
                        store.removeTag(tag, from: current)
                    }
                }
            }

            HStack(spacing: 8) {
                TextField("Add a tag", text: $newTag)
                    .textFieldStyle(.plain)
                    .font(.system(size: 13, weight: .medium, design: .rounded))
                    .foregroundColor(Theme.labelStrong)
                    .padding(10)
                    .background(
                        RoundedRectangle(cornerRadius: 9, style: .continuous)
                            .fill(Theme.panel)
                    )
                    .onSubmit(commitTag)

                Button(action: commitTag) {
                    Image(systemName: "plus")
                        .font(.system(size: 12, weight: .bold))
                        .foregroundColor(.black)
                        .frame(width: 38, height: 38)
                        .background(
                            RoundedRectangle(cornerRadius: 9, style: .continuous)
                                .fill(Theme.amber)
                        )
                }
                .buttonStyle(.plain)
                .disabled(newTag.trimmingCharacters(in: .whitespaces).isEmpty)
            }
        }
    }

    private var suggested: some View {
        VStack(alignment: .leading, spacing: 8) {
            caption("ALREADY IN USE")
            WrappedTags(tags: suggestions) { tag in
                TagChip(text: tag) { store.addTag(tag, to: current) }
            }
        }
    }

    private func caption(_ text: String) -> some View {
        Text(text)
            .font(.system(size: 9, weight: .bold, design: .rounded))
            .kerning(1)
            .foregroundColor(Theme.label)
    }

    private func commitTag() {
        store.addTag(newTag, to: current)
        newTag = ""
    }
}

/// Tags wrap onto as many rows as they need. SwiftUI has no flow layout below
/// iOS 16's Layout protocol, and this stays on the iOS 16 floor, so the rows
/// are measured here instead.
struct WrappedTags<Content: View>: View {

    let tags: [String]
    @ViewBuilder let content: (String) -> Content

    /// Rough width per tag, used only to decide where to break. Being a little
    /// out just means a row breaks a tag early, which is invisible.
    private func estimatedWidth(_ tag: String) -> CGFloat {
        CGFloat(tag.count) * 7 + 34
    }

    private func rows(fitting width: CGFloat) -> [[String]] {
        var rows: [[String]] = [[]]
        var used: CGFloat = 0
        for tag in tags {
            let needed = estimatedWidth(tag) + 6
            if used + needed > width, !rows[rows.count - 1].isEmpty {
                rows.append([])
                used = 0
            }
            rows[rows.count - 1].append(tag)
            used += needed
        }
        return rows
    }

    var body: some View {
        GeometryReader { geo in
            VStack(alignment: .leading, spacing: 6) {
                ForEach(Array(rows(fitting: geo.size.width).enumerated()), id: \.offset) { _, row in
                    HStack(spacing: 6) {
                        ForEach(row, id: \.self) { tag in content(tag) }
                        Spacer(minLength: 0)
                    }
                }
            }
        }
        .frame(height: CGFloat(max(1, (tags.count + 2) / 3)) * 34)
    }
}
