import Foundation

/// A track that has been copied into the app. `filename` is relative to the
/// app's audio directory, never an outside URL: once imported, a track no
/// longer depends on Dropbox, Drive, iCloud or the file that came from them.
struct Track: Codable, Identifiable, Hashable {

    let id: UUID
    var title: String
    var filename: String
    var duration: TimeInterval
    var addedAt: Date

    /// Where playback begins and ends, in seconds from the head of the file.
    var startPoint: TimeInterval
    var endPoint: TimeInterval

    /// Fade lengths in seconds, measured inward from the start and end points.
    var fadeIn: TimeInterval
    var fadeOut: TimeInterval

    init(id: UUID = UUID(), title: String, filename: String, duration: TimeInterval, addedAt: Date = Date()) {
        self.id = id
        self.title = title
        self.filename = filename
        self.duration = duration
        self.addedAt = addedAt
        self.startPoint = 0
        self.endPoint = duration
        self.fadeIn = 0
        self.fadeOut = 0
    }

    /// How long the track actually plays for, after trimming.
    var playingLength: TimeInterval { max(endPoint - startPoint, 0) }

    var isTrimmed: Bool { startPoint > 0.01 || endPoint < duration - 0.01 }
    var hasFades: Bool { fadeIn > 0.01 || fadeOut > 0.01 }

    /// Keeps the four edit points from crossing each other. Called after every
    /// edit rather than trusting the sliders, since they are independent.
    mutating func clampEditPoints() {
        endPoint = min(max(endPoint, 0), duration)
        startPoint = min(max(startPoint, 0), max(endPoint - 0.5, 0))

        // Two fades that overlap would never reach full gain, so cap each at
        // half the trimmed length.
        let half = playingLength / 2
        fadeIn = min(max(fadeIn, 0), half)
        fadeOut = min(max(fadeOut, 0), half)
    }
}

/// A gig: a name, a date, and the running order.
///
/// The setlist holds track IDs rather than copies, so editing a song's fades
/// or edit points updates every event that uses it.
struct Event: Codable, Identifiable, Hashable {

    let id: UUID
    var name: String
    var date: Date
    var trackIDs: [UUID]

    init(id: UUID = UUID(), name: String, date: Date = Date(), trackIDs: [UUID] = []) {
        self.id = id
        self.name = name
        self.date = date
        self.trackIDs = trackIDs
    }
}

/// What gets written to disk.
struct LibraryFile: Codable {
    var tracks: [Track] = []
    var events: [Event] = []
    var activeEventID: UUID?
}

enum TimeFormat {

    /// m:ss, for running times.
    static func clock(_ seconds: TimeInterval) -> String {
        guard seconds.isFinite, seconds >= 0 else { return "0:00" }
        let whole = Int(seconds.rounded(.down))
        return String(format: "%d:%02d", whole / 60, whole % 60)
    }

    /// m:ss.t, for edit points where a tenth matters.
    static func precise(_ seconds: TimeInterval) -> String {
        guard seconds.isFinite, seconds >= 0 else { return "0:00.0" }
        let whole = Int(seconds.rounded(.down))
        let tenths = Int((seconds - Double(whole)) * 10)
        return String(format: "%d:%02d.%d", whole / 60, whole % 60, tenths)
    }

    /// Fade lengths read better as plain seconds.
    static func seconds(_ value: TimeInterval) -> String {
        value < 0.05 ? "off" : String(format: "%.1fs", value)
    }
}
