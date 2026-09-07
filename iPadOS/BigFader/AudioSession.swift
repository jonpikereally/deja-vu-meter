import AVFoundation

/// One place to configure the audio session, because both the mixer and the
/// master fader depend on it and neither should be fighting the other over it.
enum AudioSession {

    private static var configured = false

    /// `.playback` so the decks are heard through the silent switch, and
    /// `.mixWithOthers` so starting this app does not stop whatever else is
    /// playing -- the master fader is meant to sit alongside Spotify, not
    /// interrupt it. An active session is also what makes
    /// `AVAudioSession.outputVolume` meaningful.
    static func configure() {
        guard !configured else { return }
        configured = true

        let session = AVAudioSession.sharedInstance()
        try? session.setCategory(.playback, mode: .default, options: [.mixWithOthers])
        try? session.setActive(true)
    }
}
