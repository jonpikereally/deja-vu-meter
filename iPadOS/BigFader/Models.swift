import Foundation

/// Where a song starts, where it ends, and how long it takes to get in and out.
///
/// These belong to a setlist entry, not to the track: the same song can be cut
/// one way for a ceremony and another way for a dance floor.
struct EditPoints: Codable, Hashable {

    var startPoint: TimeInterval
    var endPoint: TimeInterval
    var fadeIn: TimeInterval
    var fadeOut: TimeInterval

    /// The whole file, untouched.
    static func whole(_ duration: TimeInterval) -> EditPoints {
        EditPoints(startPoint: 0, endPoint: duration, fadeIn: 0, fadeOut: 0)
    }

    var playingLength: TimeInterval { max(endPoint - startPoint, 0) }

    func isTrimmed(of duration: TimeInterval) -> Bool {
        startPoint > 0.01 || endPoint < duration - 0.01
    }

    var hasFades: Bool { fadeIn > 0.01 || fadeOut > 0.01 }

    /// Keeps the four from crossing each other. Called after every edit rather
    /// than trusting the sliders, since they move independently.
    mutating func clamp(to duration: TimeInterval) {
        endPoint = min(max(endPoint, 0), duration)
        startPoint = min(max(startPoint, 0), max(endPoint - 0.5, 0))

        // Two fades that overlapped would never reach full gain, so cap each at
        // half the trimmed length.
        let half = playingLength / 2
        fadeIn = min(max(fadeIn, 0), half)
        fadeOut = min(max(fadeOut, 0), half)
    }
}

/// One song in one running order, with its own edit points.
///
/// It carries its own id rather than being keyed by track, so the same song can
/// appear twice in a setlist and be cut differently each time.
struct SetlistItem: Codable, Identifiable, Hashable {
    let id: UUID
    var trackID: UUID
    var edit: EditPoints

    init(id: UUID = UUID(), trackID: UUID, edit: EditPoints) {
        self.id = id
        self.trackID = trackID
        self.edit = edit
    }
}

/// A track that has been copied into the app. `filename` is relative to the
/// app's audio directory, never an outside URL: once imported, a track no
/// longer depends on Dropbox, Drive, iCloud or the file that came from them.
struct Track: Codable, Identifiable, Hashable {

    let id: UUID
    var title: String
    var filename: String
    var duration: TimeInterval
    var addedAt: Date

    /// Free-form labels for finding a song again -- "first dance", "chill",
    /// "closer". Compared case-insensitively but stored as typed.
    var tags: [String]

    init(
        id: UUID = UUID(),
        title: String,
        filename: String,
        duration: TimeInterval,
        addedAt: Date = Date(),
        tags: [String] = []
    ) {
        self.id = id
        self.title = title
        self.filename = filename
        self.duration = duration
        self.addedAt = addedAt
        self.tags = tags
    }

    enum CodingKeys: String, CodingKey {
        case id, title, filename, duration, addedAt, tags
    }

    /// Written by hand only so that a library saved before tags existed still
    /// decodes. The synthesised decoder treats a missing key as an error, which
    /// would take the whole library down with it.
    init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        id = try container.decode(UUID.self, forKey: .id)
        title = try container.decode(String.self, forKey: .title)
        filename = try container.decode(String.self, forKey: .filename)
        duration = try container.decode(TimeInterval.self, forKey: .duration)
        addedAt = try container.decodeIfPresent(Date.self, forKey: .addedAt) ?? Date()
        tags = try container.decodeIfPresent([String].self, forKey: .tags) ?? []
    }

    func matches(tag: String) -> Bool {
        tags.contains { $0.caseInsensitiveCompare(tag) == .orderedSame }
    }
}

/// A gig: a name, a date, and the running order.
struct Event: Codable, Identifiable, Hashable {

    let id: UUID
    var name: String
    var date: Date
    var items: [SetlistItem]

    init(id: UUID = UUID(), name: String, date: Date = Date(), items: [SetlistItem] = []) {
        self.id = id
        self.name = name
        self.date = date
        self.items = items
    }

    enum CodingKeys: String, CodingKey {
        case id, name, date, items
        /// Setlists used to be bare track IDs, with the edit points on the
        /// track. Read here so an existing library keeps its running orders.
        case trackIDs
    }

    init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        id = try container.decode(UUID.self, forKey: .id)
        name = try container.decode(String.self, forKey: .name)
        date = try container.decode(Date.self, forKey: .date)

        if let stored = try container.decodeIfPresent([SetlistItem].self, forKey: .items) {
            items = stored
        } else if let legacy = try container.decodeIfPresent([UUID].self, forKey: .trackIDs) {
            // Zero-length edit points here; the store repairs them to the whole
            // track once durations are known.
            items = legacy.map { SetlistItem(trackID: $0, edit: .whole(0)) }
        } else {
            items = []
        }
    }

    /// Written by hand because the CodingKeys carry a legacy case with no
    /// matching property, which is enough to defeat the synthesised encoder.
    func encode(to encoder: Encoder) throws {
        var container = encoder.container(keyedBy: CodingKeys.self)
        try container.encode(id, forKey: .id)
        try container.encode(name, forKey: .name)
        try container.encode(date, forKey: .date)
        try container.encode(items, forKey: .items)
    }
}

/// A setlist entry paired with the track it points at, for display.
struct SetlistEntry: Identifiable, Hashable {
    let item: SetlistItem
    let track: Track
    var id: UUID { item.id }
}

/// What a deck is asked to play: a file, and how to cut it.
struct Cue: Hashable {
    var track: Track
    var edit: EditPoints

    /// Set when the cue came from a setlist, so an edit to that entry can be
    /// pushed to a deck already holding it.
    var setlistItemID: UUID?
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
