import SwiftUI

/// A list of tracks to choose from. Used both for loading a deck and for
/// adding songs to a setlist, which differ only in whether picking dismisses.
struct TrackPickerView: View {

    let title: String
    let sections: [(String, [Track])]
    var dismissesOnPick = true
    let onPick: (Track) -> Void

    @Environment(\.dismiss) private var dismiss

    var body: some View {
        NavigationStack {
            Group {
                if sections.allSatisfy({ $0.1.isEmpty }) {
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
                    List {
                        ForEach(sections.indices, id: \.self) { index in
                            let section = sections[index]
                            if !section.1.isEmpty {
                                Section {
                                    ForEach(section.1) { track in
                                        Button {
                                            onPick(track)
                                            if dismissesOnPick { dismiss() }
                                        } label: {
                                            row(track)
                                        }
                                        .buttonStyle(.plain)
                                        .listRowBackground(Theme.panel)
                                    }
                                } header: {
                                    Text(section.0)
                                        .font(.system(size: 10, weight: .bold, design: .rounded))
                                        .kerning(1)
                                        .foregroundColor(Theme.label)
                                }
                            }
                        }
                    }
                    .listStyle(.plain)
                    .scrollContentBackground(.hidden)
                }
            }
            .background(Theme.background.ignoresSafeArea())
            .navigationTitle(title)
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .confirmationAction) {
                    Button("Done") { dismiss() }
                }
            }
        }
        .preferredColorScheme(.dark)
    }

    private func row(_ track: Track) -> some View {
        HStack {
            VStack(alignment: .leading, spacing: 2) {
                Text(track.title)
                    .font(.system(size: 13, weight: .semibold, design: .rounded))
                    .foregroundColor(Theme.labelStrong)
                    .lineLimit(1)
                Text(TimeFormat.clock(track.playingLength))
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
}
