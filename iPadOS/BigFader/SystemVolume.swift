import AVFoundation
import Combine
import Foundation

/// The system output volume as a published, two-way value.
///
/// Reads come from `AVAudioSession.outputVolume` via KVO, so the fader still
/// tracks the hardware buttons and Control Center. Writes go out through
/// `VolumeSliderBox`. See VolumeBridge.swift for why writing has to work that way.
final class SystemVolume: ObservableObject {

    /// 0...1, matching what `AVAudioSession` reports.
    @Published private(set) var level: Float = 0
    @Published private(set) var isMuted = false
    @Published private(set) var isDimmed = false

    let box = VolumeSliderBox()

    private let session = AVAudioSession.sharedInstance()
    private var observation: NSKeyValueObservation?

    /// Restored on unmute. Seeded so that unmuting from a cold start at zero
    /// still does something audible instead of appearing broken.
    private var levelBeforeMute: Float = 0.25
    private var levelBeforeDim: Float = 0.25

    /// Anything at or below this counts as silence. `outputVolume` does not
    /// always land on exactly 0.
    private static let silence: Float = 0.0005

    /// DIM depth. The volume scalar is not calibrated amplitude, so this is
    /// "about 12 dB down" rather than a measured -12 dB.
    private static let dimFactor: Float = 0.25

    init() {
        // The session is configured in one place, since the mixer depends on
        // it too. It has to be active for outputVolume to be meaningful.
        AudioSession.configure()

        level = session.outputVolume
        isMuted = level <= Self.silence
        if level > Self.silence {
            levelBeforeMute = level
            levelBeforeDim = level
        }

        observation = session.observe(\.outputVolume, options: [.new]) { [weak self] _, change in
            guard let value = change.newValue else { return }
            // KVO arrives on an arbitrary queue.
            DispatchQueue.main.async { self?.observed(value) }
        }
    }

    deinit {
        observation?.invalidate()
    }

    // MARK: - Writing

    /// Move the output volume to `value`, clamped to 0...1.
    func set(_ value: Float) {
        let target = min(max(value, 0), 1)
        guard abs(target - level) > 0.0001 else { return }
        apply(target)
        // Any deliberate move is the user overriding the mute and dim states.
        isDimmed = false
        isMuted = target <= Self.silence
        if target > Self.silence {
            levelBeforeMute = target
            levelBeforeDim = target
        }
    }

    /// Relative move, which is what the fader drag produces.
    func nudge(by delta: Float) {
        set(level + delta)
    }

    func toggleMute() {
        if isMuted || level <= Self.silence {
            let restored = max(levelBeforeMute, 0.05)
            apply(restored)
            isMuted = false
            isDimmed = false
        } else {
            levelBeforeMute = level
            apply(0)
            isMuted = true
        }
    }

    /// Duck for talking over the music, and back again.
    func toggleDim() {
        if isDimmed {
            apply(levelBeforeDim)
            isDimmed = false
            isMuted = levelBeforeDim <= Self.silence
        } else {
            levelBeforeDim = level
            apply(level * Self.dimFactor)
            isDimmed = true
        }
    }

    /// Re-read the system volume. KVO can miss changes made while the app was
    /// backgrounded, so the scene calls this on the way back to active.
    func refresh() {
        observed(session.outputVolume)
    }

    // MARK: - Internals

    /// Push a value out to the system and adopt it locally. Adopting it
    /// immediately, rather than waiting for the KVO echo, keeps the fader cap
    /// under the finger instead of a frame behind it.
    private func apply(_ value: Float) {
        level = value
        box.write(value)
    }

    /// A change we did not make: hardware buttons, Control Center, a route
    /// switch. Clear the modes, since the volume is no longer where we put it.
    private func observed(_ value: Float) {
        guard abs(value - level) > 0.001 else { return }
        level = value
        isDimmed = false
        isMuted = value <= Self.silence
        if value > Self.silence { levelBeforeMute = value }
    }
}
