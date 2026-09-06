import AVFoundation
import Foundation

/// One player: a loaded track, a transport, a fader, and the fade envelope.
///
/// Gain is `level * crossfadeGain * envelopeGain`, so the deck fader, the
/// crossfader and the track's own fades all multiply. Pulling a deck fader
/// down keeps it down wherever the crossfader is and whatever the fade is doing.
final class Deck: ObservableObject, Identifiable {

    let id: String
    let player = AVAudioPlayerNode()

    @Published private(set) var track: Track?
    @Published private(set) var isPlaying = false
    @Published private(set) var loadError: String?

    /// Seconds from the head of the file. Written by the transport timer, and
    /// by the scrubber while it is dragged.
    @Published var position: TimeInterval = 0

    /// The deck fader, 0...1.
    @Published var level: Float = 0.85 { didSet { applyGain() } }

    /// Set by the mixer from the crossfader position.
    var crossfadeGain: Float = 1 { didSet { applyGain() } }

    /// True while the scrubber is held, so the transport timer stops fighting
    /// the finger for the position value.
    var isScrubbing = false

    private var file: AVAudioFile?
    private var sampleRate: Double = 44_100
    private var startFrame: AVAudioFramePosition = 0
    private var envelopeGain: Float = 1 { didSet { applyGain() } }

    init(id: String) {
        self.id = id
    }

    var isLoaded: Bool { file != nil }
    var title: String { track?.title ?? "Empty" }
    var duration: TimeInterval { track?.duration ?? 0 }

    /// Where the transport is allowed to run, from the track's edit points.
    var startPoint: TimeInterval { track?.startPoint ?? 0 }
    var endPoint: TimeInterval { track?.endPoint ?? duration }

    // MARK: - Loading

    /// The URL comes from the library, inside the app's own container, so
    /// there is no security scope to hold open here.
    func load(_ track: Track, url: URL, into engine: AVAudioEngine) {
        do {
            let audioFile = try AVAudioFile(forReading: url)

            stop()

            file = audioFile
            sampleRate = audioFile.processingFormat.sampleRate
            self.track = track
            position = track.startPoint
            startFrame = 0
            loadError = nil

            // Reconnect at the file's own format. Decks at different sample
            // rates are fine; the main mixer resamples its inputs.
            engine.disconnectNodeOutput(player)
            engine.connect(player, to: engine.mainMixerNode, format: audioFile.processingFormat)
            updateEnvelope(at: position)
        } catch {
            loadError = error.localizedDescription
        }
    }

    /// Picks up edits made in the track editor while this deck holds the track.
    func refresh(from track: Track) {
        guard self.track?.id == track.id else { return }
        self.track = track
        if position < track.startPoint || position > track.endPoint {
            seek(to: track.startPoint)
        }
        updateEnvelope(at: position)
    }

    func unload() {
        stop()
        file = nil
        track = nil
        position = 0
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
        // Outside the trimmed region -- including sitting at the end after a
        // previous play -- a press starts from the top of the trim.
        if position < startPoint || position >= endPoint - 0.05 {
            position = startPoint
        }
        updateEnvelope(at: position)
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
        position = startPoint
        startFrame = 0
        envelopeGain = 1
    }

    func cueToStart() {
        seek(to: startPoint)
    }

    func seek(to seconds: TimeInterval) {
        let target = min(max(seconds, 0), duration)
        position = target
        updateEnvelope(at: target)
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

    /// Called by the mixer's timer, fast enough that the fade ramp is smooth.
    func tick() {
        guard isPlaying, !isScrubbing else { return }
        let now = currentTime
        position = now
        updateEnvelope(at: now)

        // Stop at the track's end point rather than the end of the file.
        // Checking the clock rather than using a scheduleSegment completion
        // handler keeps this state change on the main thread.
        if now >= endPoint - 0.02 {
            player.stop()
            isPlaying = false
            position = endPoint
            envelopeGain = 1
        }
    }

    // MARK: - Internals

    /// The fade envelope at a given time, ramped linearly in and out from the
    /// track's edit points.
    ///
    /// AVAudioPlayerNode has no gain-ramp API, so the fade is applied by
    /// writing `volume` from the transport timer. At 30 Hz a one-second fade
    /// moves in 3% steps, which is short of a true sample-accurate ramp but
    /// well below what is audible as zipper noise on a fade of DJ length.
    private func updateEnvelope(at time: TimeInterval) {
        guard let track else {
            envelopeGain = 1
            return
        }

        var gain: Double = 1

        if track.fadeIn > 0.01 {
            let elapsed = time - track.startPoint
            gain = min(gain, max(elapsed, 0) / track.fadeIn)
        }

        if track.fadeOut > 0.01 {
            let remaining = track.endPoint - time
            gain = min(gain, max(remaining, 0) / track.fadeOut)
        }

        envelopeGain = Float(min(max(gain, 0), 1))
    }

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
        let fader = min(max(level, 0), 1)
        let crossfade = min(max(crossfadeGain, 0), 1)
        player.volume = fader * crossfade * min(max(envelopeGain, 0), 1)
    }
}
