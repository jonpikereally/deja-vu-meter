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

    @Published private(set) var cue: Cue?
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
    var title: String { cue?.track.title ?? "Empty" }
    var duration: TimeInterval { cue?.track.duration ?? 0 }
    var edit: EditPoints? { cue?.edit }

    /// Where the transport is allowed to run, from the setlist entry's edit
    /// points -- or the whole file when a track was loaded straight from the
    /// library without a setlist behind it.
    var startPoint: TimeInterval { cue?.edit.startPoint ?? 0 }
    var endPoint: TimeInterval { cue?.edit.endPoint ?? duration }

    // MARK: - Loading

    /// The URL comes from the library, inside the app's own container, so
    /// there is no security scope to hold open here.
    func load(_ cue: Cue, url: URL, into engine: AVAudioEngine) {
        do {
            let audioFile = try AVAudioFile(forReading: url)

            stop()

            file = audioFile
            sampleRate = audioFile.processingFormat.sampleRate
            self.cue = cue
            position = cue.edit.startPoint
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

    /// Picks up an edit made to the setlist entry this deck is playing, so
    /// changing a fade mid-set takes effect without reloading.
    func refresh(from item: SetlistItem) {
        guard var current = cue, current.setlistItemID == item.id else { return }
        current.edit = item.edit
        cue = current
        if position < current.edit.startPoint || position > current.edit.endPoint {
            seek(to: current.edit.startPoint)
        }
        updateEnvelope(at: position)
    }

    func unload() {
        stop()
        file = nil
        cue = nil
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
        guard let edit = cue?.edit else {
            envelopeGain = 1
            return
        }

        var gain: Double = 1

        if edit.fadeIn > 0.01 {
            let elapsed = time - edit.startPoint
            gain = min(gain, max(elapsed, 0) / edit.fadeIn)
        }

        if edit.fadeOut > 0.01 {
            let remaining = edit.endPoint - time
            gain = min(gain, max(remaining, 0) / edit.fadeOut)
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
