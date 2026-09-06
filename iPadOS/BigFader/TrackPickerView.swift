import SwiftUI

/// Picks tracks out of the library to add to a running order. Stays open so a
/// setlist can be built in one pass.
struct TrackPickerView: View {

    let tracks: [Track]
    let onPick: (Track) -> Void

    @Environment(\.dismiss) private var dismiss

    var body: some View {
        NavigationStack {
            Group {
                if tracks.isEmpty {
                    VStack(spacing: 8) {
                        Text("Nothing in the library yet")
                            .font(.system(size: 14, weight: .semibold, design: .rounded))
                            .foregroundColor(Theme.labelStrong)
                        Text("Add files from the LIBRARY tab first.")
                            .font(.system(size: 11, weight: .medium, design: .rounded))
                            .foregroundColor(Theme.label)
                    }
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
                } else {
                    List(tracks) { track in
                        Button { onPick(track) } label: {
                            HStack {
                                VStack(alignment: .leading, spacing: 2) {
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
}
