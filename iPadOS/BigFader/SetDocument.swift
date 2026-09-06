import Foundation

/// The interchange format: what gets exported, imported, and read by anything
/// else that wants to work with these running orders.
///
/// Deliberately not a dump of the app's own storage. It carries only what is
/// meaningful somewhere else -- the songs, their tags, the running orders and
/// their edit points -- and leaves out anything tied to this device: container
/// filenames, waveform caches, which event happens to be active, when a file
/// was added.
///
/// It does not carry audio. Audio is matched by hash on the far side, so the
/// same library imported in two places lines up without moving gigabytes
/// through a JSON file. See SET-FORMAT.md for the spec.
struct SetDocument: Codable {

    static let identifier = "bigfader.set"
    static let currentVersion = 1

    var format: String
    var version: Int
    var exportedAt: Date
    var exportedBy: String
    var tracks: [ExportedTrack]
    var events: [ExportedEvent]

    static func encoder() -> JSONEncoder {
        let encoder = JSONEncoder()
        encoder.outputFormatting = [.prettyPrinted, .sortedKeys, .withoutEscapingSlashes]
        encoder.dateEncodingStrategy = .iso8601
        return encoder
    }

    static func decoder() -> JSONDecoder {
        let decoder = JSONDecoder()
        decoder.dateDecodingStrategy = .iso8601
        return decoder
    }
}

struct ExportedTrack: Codable {
    var id: UUID
    var title: String
    var duration: TimeInterval
    var tags: [String]
    var audio: ExportedAudio?
}

/// Enough to find the file again on another machine, and nothing more.
struct ExportedAudio: Codable {
    var sha256: String?
    var fileExtension: String?
    var bytes: Int?

    enum CodingKeys: String, CodingKey {
        case sha256
        case fileExtension = "extension"
        case bytes
    }
}

struct ExportedEvent: Codable {
    var id: UUID
    var name: String
    var date: Date
    var items: [ExportedItem]
}

/// Edit points are flattened rather than nested, because they are the whole
/// point of the entry and a reader should not have to walk into an object to
/// find them.
struct ExportedItem: Codable {
    var id: UUID
    var trackID: UUID
    var startPoint: TimeInterval
    var endPoint: TimeInterval
    var fadeIn: TimeInterval
    var fadeOut: TimeInterval
}

/// What an import actually did, so it can say so rather than finishing silently.
struct ImportSummary {
    var matchedTracks = 0
    var newTracks = 0
    var unresolvedTracks = 0
    var newEvents = 0
    var updatedEvents = 0

    var sentence: String {
        var parts: [String] = []

        let events = newEvents + updatedEvents
        if events > 0 {
            parts.append("\(events) event\(events == 1 ? "" : "s")")
        }
        if matchedTracks > 0 {
            parts.append("\(matchedTracks) song\(matchedTracks == 1 ? "" : "s") already here")
        }
        if unresolvedTracks > 0 {
            parts.append("\(unresolvedTracks) waiting for audio")
        }
        if newTracks > 0 {
            parts.append("\(newTracks) new")
        }

        return parts.isEmpty ? "Nothing to import" : "Imported " + parts.joined(separator: ", ")
    }
}
