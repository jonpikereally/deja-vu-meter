import SwiftUI

struct ContentView: View {

    @StateObject private var volume = SystemVolume()
    @Environment(\.scenePhase) private var scenePhase

    var body: some View {
        ZStack {
            Theme.background.ignoresSafeArea()

            VStack(spacing: 14) {
                readout
                FaderView(volume: volume)
                    .frame(maxWidth: 200)
                buttons
            }
            .padding(.horizontal, 16)
            .padding(.vertical, 18)
            .frame(maxWidth: .infinity)

            // The MPVolumeView that actually writes the volume. It has to stay
            // in the hierarchy, on screen and non-hidden to work at all, so it
            // sits in the corner at low alpha rather than being hidden.
            VolumeBridgeView(box: volume.box)
                .frame(width: 40, height: 40)
                .allowsHitTesting(false)
                .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .bottomTrailing)
                .padding(4)
        }
        .preferredColorScheme(.dark)
        .onChange(of: scenePhase) { phase in
            if phase == .active { volume.refresh() }
        }
    }

    // MARK: - Readout

    private var readout: some View {
        VStack(spacing: 2) {
            HStack(alignment: .firstTextBaseline, spacing: 2) {
                Text(percentText)
                    .font(.system(size: 46, weight: .semibold, design: .rounded))
                    .monospacedDigit()
                Text("%")
                    .font(.system(size: 20, weight: .medium, design: .rounded))
                    .foregroundColor(Theme.label)
            }
            .foregroundColor(volume.isMuted ? Theme.red : Theme.labelStrong)

            Text(dBText)
                .font(.system(size: 12, weight: .medium, design: .rounded))
                .monospacedDigit()
                .foregroundColor(Theme.label)

            RoutePicker()
                .frame(width: 40, height: 34)
                .padding(.top, 2)
        }
    }

    private var percentText: String {
        String(Int((volume.level * 100).rounded()))
    }

    /// 20*log10 of the volume scalar. iPadOS does not expose a calibrated
    /// output level, so this is a relative figure for judging moves by ear,
    /// not a measurement.
    private var dBText: String {
        guard volume.level > 0.0005 else { return "-INF dB" }
        return String(format: "%.1f dB", 20 * log10(volume.level))
    }

    // MARK: - Buttons

    private var buttons: some View {
        HStack(spacing: 10) {
            PadButton(
                title: "MUTE",
                isOn: volume.isMuted,
                onColor: Theme.red,
                action: volume.toggleMute
            )
            PadButton(
                title: "DIM",
                isOn: volume.isDimmed,
                onColor: Theme.amber,
                action: volume.toggleDim
            )
        }
    }
}

/// A latching pad, sized for a thumb rather than a cursor.
struct PadButton: View {

    let title: String
    let isOn: Bool
    let onColor: Color
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            Text(title)
                .font(.system(size: 13, weight: .bold, design: .rounded))
                .kerning(1)
                .foregroundColor(isOn ? .black : Theme.labelStrong)
                .frame(maxWidth: .infinity)
                .frame(height: 52)
                .background(
                    RoundedRectangle(cornerRadius: 12, style: .continuous)
                        .fill(isOn ? onColor : Theme.panel)
                )
                .overlay(
                    RoundedRectangle(cornerRadius: 12, style: .continuous)
                        .strokeBorder(Theme.hairline, lineWidth: 1)
                )
        }
        .buttonStyle(.plain)
    }
}
