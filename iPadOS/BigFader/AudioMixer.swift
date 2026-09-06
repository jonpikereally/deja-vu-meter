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

    func load(_ track: Track, url: URL, into deck: Deck) {
        deck.load(track, url: url, into: engine)
        start()
    }

    /// Push a track edit out to whichever deck is holding it, so changing a
    /// fade or an edit point takes effect without reloading.
    func refresh(from track: Track) {
        decks.forEach { $0.refresh(from: track) }
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
