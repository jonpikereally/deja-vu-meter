import AVFoundation
import Foundation

/// One player: a loaded file, a transport, and a fader.
///
/// Gain is `level * crossfadeGain`, so the deck fader and the crossfader
/// multiply rather than one overriding the other -- pulling a deck fader down
/// keeps it down wherever the crossfader is.
final class Deck: ObservableObject, Identifiable {

    let id: String
    let player = AVAudioPlayerNode()

    @Published private(set) var title = "Empty"
    @Published private(set) var duration: TimeInterval = 0
    @Published private(set) var isPlaying = false
    @Published private(set) var loadError: String?

    /// Seconds. Written by the transport timer, and by the scrubber while dragged.
    @Published var position: TimeInterval = 0

    /// The deck fader, 0...1.
    @Published var level: Float = 0.85 { didSet { applyGain() } }

    /// Set by the mixer from the crossfader position.
    var crossfadeGain: Float = 1 { didSet { applyGain() } }

    /// True while the scrubber is held, so the transport timer stops fighting
    /// the finger for the position value.
    var isScrubbing = false

    private(set) var file: AVAudioFile?
    private var sampleRate: Double = 44_100
    private var startFrame: AVAudioFramePosition = 0

    /// Files picked from the document browser come with a security scope that
    /// has to be held open for as long as the file is read, and closed exactly
    /// once when it is replaced.
    private var scopedURL: URL?

    init(id: String) {
        self.id = id
    }

    deinit {
        scopedURL?.stopAccessingSecurityScopedResource()
    }

    var isLoaded: Bool { file != nil }

    // MARK: - Loading

    func load(_ url: URL, into engine: AVAudioEngine) {
        let opened = url.startAccessingSecurityScopedResource()

        do {
            let audioFile = try AVAudioFile(forReading: url)

            stop()

            // Release the previous file's scope only once the new one has
            // opened, so a failed load leaves the old track playable.
            scopedURL?.stopAccessingSecurityScopedResource()
            scopedURL = opened ? url : nil

            file = audioFile
            sampleRate = audioFile.processingFormat.sampleRate
            duration = sampleRate > 0 ? Double(audioFile.length) / sampleRate : 0
            title = url.deletingPathExtension().lastPathComponent
            position = 0
            startFrame = 0
            loadError = nil

            // Reconnect at the file's own format. Decks at different sample
            // rates are fine; the main mixer resamples its inputs.
            engine.disconnectNodeOutput(player)
            engine.connect(player, to: engine.mainMixerNode, format: audioFile.processingFormat)
            applyGain()
        } catch {
            if opened { url.stopAccessingSecurityScopedResource() }
            loadError = error.localizedDescription
        }
    }

    /// Re-established after a route change, which tears the engine's
    /// connections down.
    func reconnect(to engine: AVAudioEngine) {
        guard let file else { return }
        engine.disconnectNodeOutput(player)
        engine.connect(player, to: engine.mainMixerNode, format: file.processingFormat)
        applyGain()
    }

    // MARK: - Transport

    func togglePlay() {
        guard isLoaded else { return }
        if isPlaying { pause() } else { play() }
    }

    func play() {
        guard let file, !isPlaying else { return }
        // At the end, a press restarts rather than doing nothing.
        if position >= duration - 0.05 { position = 0 }
        schedule(from: position, file: file)
        player.play()
        isPlaying = true
    }

    func pause() {
        guard isPlaying else { return }
        position = currentTime
        player.pause()
        isPlaying = false
    }

    func stop() {
        player.stop()
        isPlaying = false
        position = 0
        startFrame = 0
    }

    func cueToStart() {
        seek(to: 0)
    }

    func seek(to seconds: TimeInterval) {
        let target = min(max(seconds, 0), duration)
        position = target
        guard let file, isPlaying else { return }
        schedule(from: target, file: file)
        player.play()
    }

    /// Where the node has actually got to, which is the only honest answer
    /// while audio is rendering.
    var currentTime: TimeInterval {
        guard isPlaying,
              let nodeTime = player.lastRenderTime,
              let playerTime = player.playerTime(forNodeTime: nodeTime) else {
            return position
        }
        let frames = startFrame + playerTime.sampleTime
        return min(max(Double(frames) / sampleRate, 0), duration)
    }

    /// Called by the mixer's timer.
    func tick() {
        guard isPlaying, !isScrubbing else { return }
        let now = currentTime
        position = now
        // The scheduled segment has run out. Checking the clock rather than
        // using a completion handler keeps this on the main thread.
        if now >= duration - 0.05 {
            player.stop()
            isPlaying = false
            position = duration
        }
    }

    // MARK: - Internals

    private func schedule(from seconds: TimeInterval, file: AVAudioFile) {
        player.stop()
        let frame = AVAudioFramePosition(seconds * sampleRate)
        let clamped = max(0, min(frame, max(file.length - 1, 0)))
        let count = AVAudioFrameCount(max(file.length - clamped, 0))
        guard count > 0 else { return }
        startFrame = clamped
        player.scheduleSegment(file, startingFrame: clamped, frameCount: count, at: nil)
    }

    private func applyGain() {
        player.volume = min(max(level, 0), 1) * min(max(crossfadeGain, 0), 1)
    }
}
