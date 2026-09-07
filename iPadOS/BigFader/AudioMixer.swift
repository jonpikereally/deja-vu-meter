import AVFoundation
import Combine
import Foundation

/// Two decks into one engine, with an equal-power crossfader between them.
final class AudioMixer: ObservableObject {

    let engine = AVAudioEngine()
    let deckA = Deck(id: "A")
    let deckB = Deck(id: "B")

    /// 0 = all A, 1 = all B.
    @Published var crossfade: Float = 0.5 { didSet { applyCrossfade() } }
    @Published private(set) var engineError: String?

    /// When on, a deck that finishes or is ejected reloads itself with the next
    /// song in the running order. It loads and cues; it does not start playing,
    /// because bringing the next song in is the crossfader's job.
    @Published var isLiveMode = false {
        didSet {
            guard isLiveMode else {
                liveNote = nil
                return
            }
            fillEmptyDecks()
        }
    }

    /// What live mode last did, or why it could not.
    @Published private(set) var liveNote: String?

    /// Supplied by the view layer, which owns the library. Given the setlist
    /// entries already consumed and the tracks currently on a deck, it returns
    /// the next thing to play. The mixer deliberately knows nothing about
    /// events or files.
    var nextCue: ((_ consumedItemIDs: Set<UUID>, _ busyTrackIDs: Set<UUID>) -> (cue: Cue, url: URL)?)?

    private var timer: Timer?
    private var cancellables: Set<AnyCancellable> = []

    init() {
        AudioSession.configure()

        engine.attach(deckA.player)
        engine.attach(deckB.player)

        // Touching mainMixerNode is what wires it to the output. Connect at the
        // mixer's own format for now; each load reconnects at the file's.
        let format = engine.mainMixerNode.outputFormat(forBus: 0)
        engine.connect(deckA.player, to: engine.mainMixerNode, format: format)
        engine.connect(deckB.player, to: engine.mainMixerNode, format: format)

        applyCrossfade()
        start()

        deckA.onFinished = { [weak self] deck in self?.finished(deck) }
        deckB.onFinished = { [weak self] deck in self?.finished(deck) }

        // The engine stops and loses its connections whenever the route changes
        // -- headphones in, AirPlay out. Without this the decks go silent and
        // look like they are still playing.
        NotificationCenter.default
            .publisher(for: .AVAudioEngineConfigurationChange, object: engine)
            .receive(on: DispatchQueue.main)
            .sink { [weak self] _ in self?.handleConfigurationChange() }
            .store(in: &cancellables)

        // 30 Hz: fast enough that the fade ramps are smooth, since
        // AVAudioPlayerNode has no gain-ramp API and the fades are applied by
        // writing volume from here.
        timer = Timer.scheduledTimer(withTimeInterval: 1.0 / 30.0, repeats: true) { [weak self] _ in
            self?.decks.forEach { $0.tick() }
        }
    }

    deinit {
        timer?.invalidate()
        engine.stop()
    }

    var decks: [Deck] { [deckA, deckB] }

    func load(_ cue: Cue, url: URL, into deck: Deck) {
        deck.load(cue, url: url, into: engine)
        start()
    }

    /// Push a setlist edit out to whichever deck is playing that entry, so
    /// changing a fade or an edit point takes effect without reloading.
    func refresh(from item: SetlistItem) {
        decks.forEach { $0.refresh(from: item) }
    }

    // MARK: - Live mode

    /// Take the current song off a deck. In live mode the next one takes its
    /// place; otherwise the deck is just left empty.
    func eject(_ deck: Deck) {
        if isLiveMode {
            advance(deck)
        } else {
            deck.unload()
        }
    }

    private func finished(_ deck: Deck) {
        guard isLiveMode else { return }
        advance(deck)
    }

    /// Replace what is on `deck` with the next song in the running order that
    /// is not already on a deck.
    private func advance(_ deck: Deck) {
        let other = (deck === deckA) ? deckB : deckA

        // Captured before unloading: what this deck just played still counts as
        // consumed, or live mode would hand it straight back.
        var consumed: Set<UUID> = []
        var busyTracks: Set<UUID> = []

        if let id = deck.cue?.setlistItemID { consumed.insert(id) }
        if let cue = other.cue {
            if let id = cue.setlistItemID { consumed.insert(id) }
            // Excluded by track as well as by entry, so a song listed twice
            // cannot end up playing on both decks at once.
            busyTracks.insert(cue.track.id)
        }

        deck.unload()

        guard let provider = nextCue else { return }
        guard let next = provider(consumed, busyTracks) else {
            liveNote = "Nothing left to load"
            return
        }

        deck.load(next.cue, url: next.url, into: engine)
        liveNote = "Deck \(deck.id): \(next.cue.track.title)"
        start()
    }

    /// Turning live mode on with empty decks should put the top of the running
    /// order under your hands, not wait for something to finish first.
    private func fillEmptyDecks() {
        for deck in decks where !deck.isLoaded {
            advance(deck)
        }
    }

    /// Slam the crossfader to one deck, for a hard cut.
    func cut(to deck: Deck) {
        crossfade = deck === deckA ? 0 : 1
    }

    func centreCrossfade() {
        crossfade = 0.5
    }

    // MARK: - Internals

    private func start() {
        guard !engine.isRunning else { return }
        do {
            try engine.start()
            engineError = nil
        } catch {
            engineError = error.localizedDescription
        }
    }

    private func handleConfigurationChange() {
        let wasPlaying = decks.map(\.isPlaying)
        let positions = decks.map(\.position)

        decks.forEach { $0.pause() }
        decks.forEach { $0.reconnect(to: engine) }
        start()

        for (index, deck) in decks.enumerated() where wasPlaying[index] {
            deck.seek(to: positions[index])
            deck.play()
        }
    }

    /// Equal power, so the sum stays roughly constant across the throw instead
    /// of dipping ~3 dB in the middle the way a linear blend does.
    private func applyCrossfade() {
        let x = min(max(crossfade, 0), 1)
        deckA.crossfadeGain = cos(x * Float.pi / 2)
        deckB.crossfadeGain = sin(x * Float.pi / 2)
    }
}
