import SwiftUI

/// Rig settings, as opposed to anything about a set.
struct SettingsView: View {

    @Binding var showMasterFader: Bool
    @ObservedObject var volume: SystemVolume

    @Environment(\.dismiss) private var dismiss

    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(alignment: .leading, spacing: 16) {
                    masterBlock
                    aboutBlock
                }
                .padding(18)
                .frame(maxWidth: .infinity, alignment: .leading)
            }
            .background(Theme.background.ignoresSafeArea())
            .navigationTitle("Settings")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .confirmationAction) {
                    Button("Done") { dismiss() }
                }
            }
        }
        .preferredColorScheme(.dark)
    }

    private var masterBlock: some View {
        VStack(alignment: .leading, spacing: 10) {
            HStack {
                caption("MASTER FADER")
                Spacer()
                Toggle("", isOn: $showMasterFader)
                    .labelsHidden()
                    .tint(Theme.amber)
            }

            Text("Turn this off if you set the output level somewhere else. The MASTER tab and the master strip in the mixer both go away; the decks, the crossfader and the setlists are untouched.")
                .font(.system(size: 11, weight: .medium, design: .rounded))
                .foregroundColor(Theme.label)

            if !showMasterFader {
                // Hidden must not mean unreachable: the system volume is still
                // whatever it was, and a rig left quiet with no visible control
                // is exactly the surprise this note exists to prevent.
                HStack(spacing: 10) {
                    Text("System output is at")
                        .font(.system(size: 11, weight: .medium, design: .rounded))
                        .foregroundColor(Theme.label)
                    Text("\(Int((volume.level * 100).rounded()))%")
                        .font(.system(size: 13, weight: .semibold, design: .rounded))
                        .monospacedDigit()
                        .foregroundColor(Theme.amber)
                    Spacer()
                    Button {
                        volume.set(1)
                    } label: {
                        Text("FULL")
                            .font(.system(size: 10, weight: .bold, design: .rounded))
                            .kerning(0.5)
                            .foregroundColor(Theme.labelStrong)
                            .padding(.horizontal, 11)
                            .padding(.vertical, 7)
                            .background(
                                RoundedRectangle(cornerRadius: 8, style: .continuous)
                                    .fill(Theme.trackWell)
                            )
                    }
                    .buttonStyle(.plain)
                }
                .padding(.top, 2)
            }
        }
        .padding(14)
        .background(
            RoundedRectangle(cornerRadius: 12, style: .continuous)
                .fill(Theme.panel)
        )
    }

    private var aboutBlock: some View {
        VStack(alignment: .leading, spacing: 8) {
            caption("SET FORMAT")
            Text("Sets are written as bigfader.set v1. The identifier is the interop contract with the browser version, so it stays put whatever either app is called.")
                .font(.system(size: 11, weight: .medium, design: .rounded))
                .foregroundColor(Theme.label)
        }
        .padding(14)
        .background(
            RoundedRectangle(cornerRadius: 12, style: .continuous)
                .fill(Theme.panel)
        )
    }

    private func caption(_ text: String) -> some View {
        Text(text)
            .font(.system(size: 9, weight: .bold, design: .rounded))
            .kerning(1)
            .foregroundColor(Theme.label)
    }
}
