import SwiftUI

/// Picks tracks out of the library to add to a running order. Stays open so a
/// setlist can be built in one pass.
struct TrackPickerView: View {

    let tracks: [Track]
    let allTags: [String]
    let onPick: (Track) -> Void

    @State private var filter: Set<String> = []
    @Environment(\.dismiss) private var dismiss

    /// Filters narrow rather than widen: a track has to carry every selected
    /// tag, so "first dance" plus "slow" is a smaller list, not a bigger one.
    private var visible: [Track] {
        guard !filter.isEmpty else { return tracks }
        return tracks.filter { track in filter.allSatisfy { track.matches(tag: $0) } }
    }

    var body: some View {
        NavigationStack {
            Group {
                if tracks.isEmpty {
                    empty("Nothing in the library yet", "Add files from the LIBRARY tab first.")
                } else {
                    VStack(spacing: 8) {
                        TagFilterBar(tags: allTags, selected: $filter)
                            .padding(.horizontal, 16)

                        if visible.isEmpty {
                            empty("Nothing with those tags", "A track has to carry all of them.")
                        } else {
                            list
                        }
                    }
                }
            }
            .background(Theme.background.ignoresSafeArea())
            .navigationTitle("Add songs")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .confirmationAction) {
                    Button("Done") { dismiss() }
                }
            }
        }
        .preferredColorScheme(.dark)
    }

    private var list: some View {
        List(visible) { track in
            Button { onPick(track) } label: {
                HStack {
                    VStack(alignment: .leading, spacing: 2) {
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
                    Image(systemName: "plus.circle")
                        .foregroundColor(Theme.amber)
                }
                .padding(.vertical, 3)
                .contentShape(Rectangle())
            }
            .buttonStyle(.plain)
            .listRowBackground(Theme.panel)
        }
        .listStyle(.plain)
        .scrollContentBackground(.hidden)
    }

    private func empty(_ title: String, _ detail: String) -> some View {
        VStack(spacing: 8) {
            Text(title)
                .font(.system(size: 14, weight: .semibold, design: .rounded))
                .foregroundColor(Theme.labelStrong)
            Text(detail)
                .font(.system(size: 11, weight: .medium, design: .rounded))
                .foregroundColor(Theme.label)
                .multilineTextAlignment(.center)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }
}
