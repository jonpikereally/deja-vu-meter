import SwiftUI

struct ContentView: View {

    private enum Mode: String, CaseIterable {
        case master = "MASTER"
        case mixer = "MIXER"
        case library = "LIBRARY"
        case events = "EVENTS"
    }

    @StateObject private var volume = SystemVolume()
    @StateObject private var mixer = AudioMixer()
    @StateObject private var store = LibraryStore()

    @Environment(\.scenePhase) private var scenePhase
    @Environment(\.horizontalSizeClass) private var sizeClass

    /// Nil until a mode is chosen by hand, so the layout follows the window
    /// width -- MASTER in a narrow Split View column, MIXER when there is room
    /// -- without overriding a deliberate choice afterwards.
    @State private var chosenMode: Mode?

    @State private var loadingDeck: Deck?
    @State private var editingEvent: Event?

    private var mode: Mode {
        chosenMode ?? (sizeClass == .compact ? .master : .mixer)
    }

    var body: some View {
        ZStack {
            Theme.background.ignoresSafeArea()

            VStack(spacing: 12) {
                modePicker

                switch mode {
                case .master: masterPanel
                case .mixer: mixerPanel
                case .library: LibraryView(store: store)
                case .events: EventsView(store: store) { editingEvent = $0 }
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
        .onAppear(perform: connectLiveMode)
        .onChange(of: scenePhase) { phase in
            if phase == .active {
                volume.refresh()
            } else {
                store.flush()
            }
        }
        .sheet(item: $loadingDeck) { deck in
            DeckLoaderView(
                deckID: deck.id,
                setlistName: store.activeEvent?.name.uppercased(),
                setlist: activeSetlist,
                tracks: store.tracks
            ) { cue in
                mixer.load(cue, url: store.url(for: cue.track), into: deck)
            }
        }
        .sheet(item: $editingEvent) { event in
            EventEditorView(event: event, store: store) { edited in
                store.update(edited)
                // Keep a deck already playing one of these entries in step with
                // the edit, so a fade changed mid-set takes effect at once.
                edited.items.forEach { mixer.refresh(from: $0) }
            }
        }
    }

    // MARK: - Live mode

    /// Teaches the mixer how to find the next song without giving it any
    /// knowledge of events or the file system.
    ///
    /// "Next" means: start after the furthest point in the running order either
    /// deck has reached, then take the first entry that has not been consumed
    /// and whose track is not on the other deck. It does not wrap -- the end of
    /// the setlist is the end of the setlist.
    private func connectLiveMode() {
        let library = store
        mixer.nextCue = { consumed, busyTracks in
            guard let event = library.activeEvent else { return nil }
            let entries = library.setlist(for: event)
            guard !entries.isEmpty else { return nil }

            let furthest = entries.lastIndex { consumed.contains($0.item.id) }
            let start = furthest.map { $0 + 1 } ?? 0
            guard start < entries.count else { return nil }

            guard let entry = entries[start...].first(where: {
                !consumed.contains($0.item.id) && !busyTracks.contains($0.track.id)
            }) else { return nil }

            return (
                Cue(track: entry.track, edit: entry.item.edit, setlistItemID: entry.item.id),
                library.url(for: entry.track)
            )
        }
    }

    // MARK: - Deck loading

    /// The active event's running order, offered above the plain library so
    /// the next song of the night -- cut the way that event wants it -- is at
    /// the top rather than buried alphabetically.
    private var activeSetlist: [SetlistEntry] {
        guard let event = store.activeEvent else { return [] }
        return store.setlist(for: event)
    }

    // MARK: - Layouts

    private var modePicker: some View {
        HStack(spacing: 5) {
            ForEach(Mode.allCases, id: \.self) { candidate in
                Button { chosenMode = candidate } label: {
                    Text(candidate.rawValue)
                        .font(.system(size: 10, weight: .bold, design: .rounded))
                        .kerning(0.5)
                        .lineLimit(1)
                        .minimumScaleFactor(0.7)
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
                    DeckView(
                        deck: mixer.deckA,
                        store: store,
                        onLoad: { loadingDeck = mixer.deckA },
                        onEject: { mixer.eject(mixer.deckA) }
                    )
                    DeckView(
                        deck: mixer.deckB,
                        store: store,
                        onLoad: { loadingDeck = mixer.deckB },
                        onEject: { mixer.eject(mixer.deckB) }
                    )
                }
                liveBar
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

    /// Live mode, and whatever it last did.
    private var liveBar: some View {
        HStack(spacing: 10) {
            Button { mixer.isLiveMode.toggle() } label: {
                HStack(spacing: 6) {
                    Circle()
                        .fill(mixer.isLiveMode ? Color.black : Theme.label.opacity(0.4))
                        .frame(width: 7, height: 7)
                    Text("LIVE")
                        .font(.system(size: 11, weight: .bold, design: .rounded))
                        .kerning(1)
                }
                .foregroundColor(mixer.isLiveMode ? .black : Theme.labelStrong)
                .padding(.horizontal, 14)
                .frame(height: 36)
                .background(
                    RoundedRectangle(cornerRadius: 9, style: .continuous)
                        .fill(mixer.isLiveMode ? Theme.amber : Theme.panel)
                )
            }
            .buttonStyle(.plain)

            Text(liveStatus)
                .font(.system(size: 10, weight: .medium, design: .rounded))
                .foregroundColor(mixer.isLiveMode ? Theme.label : Theme.label.opacity(0.6))
                .lineLimit(1)
                .truncationMode(.middle)

            Spacer()
        }
    }

    private var liveStatus: String {
        guard mixer.isLiveMode else {
            return "Auto-loads the next song in the running order"
        }
        if let event = store.activeEvent {
            return mixer.liveNote ?? "Following \(event.name)"
        }
        return "No active event - mark one in EVENTS"
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
        if let message = store.lastError ?? mixer.engineError {
            Text(message)
                .font(.system(size: 10, weight: .medium, design: .rounded))
                .foregroundColor(Theme.red)
                .lineLimit(2)
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
