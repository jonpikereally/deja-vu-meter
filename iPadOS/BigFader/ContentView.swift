import SwiftUI
import UniformTypeIdentifiers

struct ContentView: View {

    private enum Mode: String, CaseIterable {
        case master = "MASTER"
        case mixer = "MIXER"
    }

    @StateObject private var volume = SystemVolume()
    @StateObject private var mixer = AudioMixer()

    @Environment(\.scenePhase) private var scenePhase
    @Environment(\.horizontalSizeClass) private var sizeClass

    /// Nil until the mode is chosen by hand, so the layout follows the window
    /// width -- MASTER in a narrow Split View column, MIXER when there is room
    /// -- without overriding a deliberate choice afterwards.
    @State private var chosenMode: Mode?
    @State private var importingInto: Deck?
    @State private var isImporting = false
    @State private var importError: String?

    private var mode: Mode {
        chosenMode ?? (sizeClass == .compact ? .master : .mixer)
    }

    var body: some View {
        ZStack {
            Theme.background.ignoresSafeArea()

            VStack(spacing: 12) {
                modePicker
                if mode == .master {
                    masterPanel
                } else {
                    mixerPanel
                }
                errorLine
            }
            .padding(.horizontal, 14)
            .padding(.vertical, 14)

            // The MPVolumeView that actually writes the system volume. It has
            // to stay in the hierarchy, on screen and non-hidden to work at
            // all, so it sits in the corner at low alpha rather than hidden.
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
        .fileImporter(
            isPresented: $isImporting,
            allowedContentTypes: [.audio],
            allowsMultipleSelection: false
        ) { result in
            handleImport(result)
        }
    }

    // MARK: - Layouts

    private var modePicker: some View {
        HStack(spacing: 6) {
            ForEach(Mode.allCases, id: \.self) { candidate in
                Button { chosenMode = candidate } label: {
                    Text(candidate.rawValue)
                        .font(.system(size: 11, weight: .bold, design: .rounded))
                        .kerning(1)
                        .foregroundColor(mode == candidate ? .black : Theme.label)
                        .frame(maxWidth: .infinity)
                        .frame(height: 30)
                        .background(
                            RoundedRectangle(cornerRadius: 8, style: .continuous)
                                .fill(mode == candidate ? Theme.amber : Theme.panel)
                        )
                }
                .buttonStyle(.plain)
            }
        }
    }

    private var masterPanel: some View {
        VStack(spacing: 14) {
            masterReadout
            Fader(
                level: volume.level,
                isDimmed: volume.isMuted,
                onDelta: { volume.nudge(by: $0) }
            )
            .frame(maxWidth: 200)
            masterButtons
        }
    }

    private var mixerPanel: some View {
        HStack(spacing: 12) {
            VStack(spacing: 12) {
                HStack(spacing: 12) {
                    DeckView(deck: mixer.deckA) { beginImport(into: mixer.deckA) }
                    DeckView(deck: mixer.deckB) { beginImport(into: mixer.deckB) }
                }
                CrossfaderView(value: $mixer.crossfade) { mixer.centreCrossfade() }
            }

            VStack(spacing: 10) {
                Text("MASTER")
                    .font(.system(size: 10, weight: .bold, design: .rounded))
                    .kerning(1)
                    .foregroundColor(Theme.label)
                masterReadout
                Fader(
                    level: volume.level,
                    isDimmed: volume.isMuted,
                    showsTicks: false,
                    onDelta: { volume.nudge(by: $0) }
                )
                masterButtons
            }
            .frame(width: 132)
        }
    }

    // MARK: - Master strip

    private var masterReadout: some View {
        VStack(spacing: 2) {
            HStack(alignment: .firstTextBaseline, spacing: 2) {
                Text(String(Int((volume.level * 100).rounded())))
                    .font(.system(size: mode == .master ? 46 : 30, weight: .semibold, design: .rounded))
                    .monospacedDigit()
                Text("%")
                    .font(.system(size: mode == .master ? 20 : 14, weight: .medium, design: .rounded))
                    .foregroundColor(Theme.label)
            }
            .foregroundColor(volume.isMuted ? Theme.red : Theme.labelStrong)

            Text(dBText)
                .font(.system(size: 11, weight: .medium, design: .rounded))
                .monospacedDigit()
                .foregroundColor(Theme.label)

            RoutePicker()
                .frame(width: 40, height: 34)
        }
    }

    /// 20*log10 of the system volume scalar. iPadOS exposes no calibrated
    /// output level, so this is a relative figure for judging moves by ear,
    /// not a measurement.
    private var dBText: String {
        guard volume.level > 0.0005 else { return "-INF dB" }
        return String(format: "%.1f dB", 20 * log10(volume.level))
    }

    private var masterButtons: some View {
        HStack(spacing: 8) {
            PadButton(title: "MUTE", isOn: volume.isMuted, onColor: Theme.red, action: volume.toggleMute)
            PadButton(title: "DIM", isOn: volume.isDimmed, onColor: Theme.amber, action: volume.toggleDim)
        }
    }

    @ViewBuilder
    private var errorLine: some View {
        if let message = importError ?? mixer.engineError {
            Text(message)
                .font(.system(size: 10, weight: .medium, design: .rounded))
                .foregroundColor(Theme.red)
                .lineLimit(2)
        }
    }

    // MARK: - Loading

    private func beginImport(into deck: Deck) {
        importError = nil
        importingInto = deck
        isImporting = true
    }

    private func handleImport(_ result: Result<[URL], Error>) {
        defer { importingInto = nil }
        switch result {
        case .success(let urls):
            guard let url = urls.first, let deck = importingInto else { return }
            mixer.load(url, into: deck)
        case .failure(let error):
            importError = error.localizedDescription
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
                .font(.system(size: 12, weight: .bold, design: .rounded))
                .kerning(1)
                .foregroundColor(isOn ? .black : Theme.labelStrong)
                .frame(maxWidth: .infinity)
                .frame(height: 48)
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
