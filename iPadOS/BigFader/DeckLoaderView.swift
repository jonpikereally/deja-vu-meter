import SwiftUI

/// What a deck can be loaded with: a setlist entry, cut the way that event
/// wants it, or a bare library track played whole.
struct DeckLoaderView: View {

    let deckID: String
    let setlistName: String?
    let setlist: [SetlistEntry]
    let tracks: [Track]
    let onPick: (Cue) -> Void

    @Environment(\.dismiss) private var dismiss

    var body: some View {
        NavigationStack {
            Group {
                if setlist.isEmpty && tracks.isEmpty {
                    empty
                } else {
                    List {
                        if let setlistName, !setlist.isEmpty {
                            Section {
                                ForEach(Array(setlist.enumerated()), id: \.element.id) { index, entry in
                                    pick(
                                        title: entry.track.title,
                                        detail: detail(for: entry),
                                        number: index + 1,
                                        isPlayable: entry.track.isResolved,
                                        cue: Cue(
                                            track: entry.track,
                                            edit: entry.item.edit,
                                            setlistItemID: entry.item.id
                                        )
                                    )
                                }
                            } header: {
                                header(setlistName)
                            }
                        }

                        Section {
                            // Only the ones with audio: an entry from a shared
                            // set that has none is not something to offer here.
                            ForEach(tracks.filter(\.isResolved)) { track in
                                pick(
                                    title: track.title,
                                    detail: TimeFormat.clock(track.duration) + "  -  whole track",
                                    number: nil,
                                    isPlayable: true,
                                    cue: Cue(track: track, edit: .whole(track.duration), setlistItemID: nil)
                                )
                            }
                        } header: {
                            header("ALL TRACKS")
                        }
                    }
                    .listStyle(.plain)
                    .scrollContentBackground(.hidden)
                }
            }
            .background(Theme.background.ignoresSafeArea())
            .navigationTitle("Load deck \(deckID)")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .confirmationAction) {
                    Button("Cancel") { dismiss() }
                }
            }
        }
        .preferredColorScheme(.dark)
    }

    // MARK: - Pieces

    private func header(_ text: String) -> some View {
        Text(text)
            .font(.system(size: 10, weight: .bold, design: .rounded))
            .kerning(1)
            .foregroundColor(Theme.label)
    }

    private func pick(
        title: String,
        detail: String,
        number: Int?,
        isPlayable: Bool,
        cue: Cue
    ) -> some View {
        Button {
            onPick(cue)
            dismiss()
        } label: {
            HStack(spacing: 10) {
                if let number {
                    Text("\(number)")
                        .font(.system(size: 11, weight: .bold, design: .rounded))
                        .monospacedDigit()
                        .foregroundColor(Theme.amber)
                        .frame(width: 20, alignment: .trailing)
                }
                VStack(alignment: .leading, spacing: 2) {
                    Text(title)
                        .font(.system(size: 13, weight: .semibold, design: .rounded))
                        .foregroundColor(Theme.labelStrong)
                        .lineLimit(1)
                    Text(isPlayable ? detail : "audio missing - add the file in LIBRARY")
                        .font(.system(size: 10, weight: .medium, design: .rounded))
                        .monospacedDigit()
                        .foregroundColor(isPlayable ? Theme.label : Theme.red)
                        .lineLimit(1)
                }
                Spacer()
            }
            .padding(.vertical, 3)
            .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .disabled(!isPlayable)
        .opacity(isPlayable ? 1 : 0.6)
        .listRowBackground(Theme.panel)
    }

    private func detail(for entry: SetlistEntry) -> String {
        var parts = [TimeFormat.clock(entry.item.edit.playingLength)]
        if entry.item.edit.isTrimmed(of: entry.track.duration) {
            parts.append("trimmed")
        }
        if entry.item.edit.hasFades {
            parts.append("in \(TimeFormat.seconds(entry.item.edit.fadeIn)) / out \(TimeFormat.seconds(entry.item.edit.fadeOut))")
        }
        return parts.joined(separator: "  -  ")
    }

    private var empty: some View {
        VStack(spacing: 8) {
            Text("Nothing in the library yet")
                .font(.system(size: 14, weight: .semibold, design: .rounded))
                .foregroundColor(Theme.labelStrong)
            Text("Add files from the LIBRARY tab first.")
                .font(.system(size: 11, weight: .medium, design: .rounded))
                .foregroundColor(Theme.label)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }
}
