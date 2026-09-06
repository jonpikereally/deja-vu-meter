import AVFoundation
import Foundation

/// The library: tracks copied into the app, and the events that use them.
///
/// Everything lives in the app's Documents directory -- audio under `Audio/`,
/// the index as one JSON file. Nothing depends on the source the file came
/// from, which is the point of copying on import rather than holding a
/// security-scoped bookmark: a track stays playable with Dropbox uninstalled,
/// the iPad offline, or the original file deleted.
final class LibraryStore: ObservableObject {

    @Published private(set) var tracks: [Track] = []
    @Published private(set) var events: [Event] = []
    @Published var activeEventID: UUID?

    @Published private(set) var isImporting = false
    @Published var lastError: String?

    /// Informational, as opposed to lastError: what an import just did.
    @Published var lastNotice: String?

    /// Peak overviews, keyed by track. Published so a view redraws when one
    /// finishes generating in the background.
    @Published private(set) var waveforms: [UUID: [UInt8]] = [:]
    private var pendingWaveforms: Set<UUID> = []

    private let fileManager = FileManager.default

    /// Writes are coalesced: dragging a slider in the track editor commits on
    /// every frame, and rewriting the index JSON at 60 Hz would be silly.
    private var saveWorkItem: DispatchWorkItem?

    init() {
        load()
    }

    deinit {
        saveWorkItem?.cancel()
    }

    // MARK: - Locations

    private var documents: URL {
        fileManager.urls(for: .documentDirectory, in: .userDomainMask)[0]
    }

    private var audioDirectory: URL {
        documents.appendingPathComponent("Audio", isDirectory: true)
    }

    private var waveformDirectory: URL {
        documents.appendingPathComponent("Waveforms", isDirectory: true)
    }

    private var indexURL: URL {
        documents.appendingPathComponent("library.json")
    }

    private func waveformURL(for track: Track) -> URL {
        waveformDirectory.appendingPathComponent(track.id.uuidString)
    }

    /// Where a track's audio actually is. Built from the filename each time
    /// rather than stored, because the container path changes between installs.
    func url(for track: Track) -> URL {
        audioDirectory.appendingPathComponent(track.filename)
    }

    var storageUsed: Int64 {
        guard let contents = try? fileManager.contentsOfDirectory(
            at: audioDirectory,
            includingPropertiesForKeys: [.fileSizeKey]
        ) else { return 0 }

        return contents.reduce(into: Int64(0)) { total, url in
            let size = (try? url.resourceValues(forKeys: [.fileSizeKey]).fileSize) ?? 0
            total += Int64(size)
        }
    }

    // MARK: - Importing

    /// Copy files in from wherever the document picker found them -- local
    /// storage, iCloud Drive, Dropbox, Google Drive, any other File Provider.
    func importFiles(_ urls: [URL]) {
        guard !urls.isEmpty else { return }
        isImporting = true
        lastError = nil
        lastNotice = nil

        // Snapshot taken here rather than read from the background queue: the
        // track list is main-thread state. Maps every known hash to its track,
        // so a re-import can be recognised as a duplicate and a track waiting
        // for audio can be filled in rather than duplicated.
        let known = tracks.reduce(into: [String: Track]()) { map, track in
            if let hash = track.audioHash { map[hash] = track }
        }

        DispatchQueue.global(qos: .userInitiated).async { [weak self] in
            guard let self else { return }

            var outcomes: [ImportOutcome] = []
            var failures: [String] = []

            for url in urls {
                do {
                    outcomes.append(try self.copyIn(url, known: known))
                } catch {
                    failures.append("\(url.lastPathComponent): \(error.localizedDescription)")
                }
            }

            DispatchQueue.main.async {
                self.apply(outcomes)
                self.isImporting = false
                self.lastError = failures.isEmpty ? nil : failures.joined(separator: "\n")
                self.save()
            }
        }
    }

    /// What importing one file turned out to mean.
    private enum ImportOutcome {
        case created(Track)
        /// The audio for a track that came in from a shared set without it.
        case relinked(id: UUID, filename: String, duration: TimeInterval)
        /// Already in the library, byte for byte.
        case duplicate(title: String)
    }

    private func apply(_ outcomes: [ImportOutcome]) {
        var added = 0
        var relinked = 0
        var duplicates = 0

        for outcome in outcomes {
            switch outcome {
            case .created(let track):
                tracks.append(track)
                added += 1

            case .relinked(let id, let filename, let duration):
                guard let index = tracks.firstIndex(where: { $0.id == id }) else { break }
                tracks[index].filename = filename
                tracks[index].duration = duration
                relinked += 1

            case .duplicate:
                duplicates += 1
            }
        }

        sortTracks()

        var notes: [String] = []
        if added > 0 { notes.append("\(added) added") }
        if relinked > 0 { notes.append("\(relinked) linked to a shared set") }
        if duplicates > 0 { notes.append("\(duplicates) already in the library") }
        lastNotice = notes.isEmpty ? nil : notes.joined(separator: ", ")
    }

    private func copyIn(_ source: URL, known: [String: Track]) throws -> ImportOutcome {
        // Picked URLs arrive security-scoped. The scope only has to last long
        // enough to make the copy.
        let scoped = source.startAccessingSecurityScopedResource()
        defer { if scoped { source.stopAccessingSecurityScopedResource() } }

        try fileManager.createDirectory(at: audioDirectory, withIntermediateDirectories: true)

        let ext = source.pathExtension.isEmpty ? "m4a" : source.pathExtension

        // Copied to a scratch name first, because the file has to be hashed
        // before it is known which track it belongs to -- a new one, or one
        // already in a shared setlist waiting for its audio.
        let scratch = audioDirectory.appendingPathComponent("incoming-\(UUID().uuidString).\(ext)")

        // A coordinated read, not a bare copyItem. A file in Dropbox, Google
        // Drive or iCloud may be a placeholder that is not on the device yet;
        // coordinating is what makes the provider materialise it first. Copying
        // a placeholder directly either fails or silently produces a stub.
        var coordinationError: NSError?
        var copyError: Error?

        NSFileCoordinator().coordinate(readingItemAt: source, options: [], error: &coordinationError) { readable in
            do {
                try fileManager.copyItem(at: readable, to: scratch)
            } catch {
                copyError = error
            }
        }

        if let coordinationError {
            try? fileManager.removeItem(at: scratch)
            throw coordinationError
        }
        if let copyError { throw copyError }

        do {
            let hash = try AudioHash.sha256(of: scratch)
            let existing = known[hash]

            if let existing, existing.isResolved {
                try? fileManager.removeItem(at: scratch)
                return .duplicate(title: existing.title)
            }

            // Prove it decodes before it joins the library, and get its length.
            // A file that fails here would otherwise sit in a setlist looking
            // fine until the moment it was needed.
            let file = try AVAudioFile(forReading: scratch)
            let rate = file.processingFormat.sampleRate
            let duration = rate > 0 ? Double(file.length) / rate : 0

            // Keep the waiting track's id when relinking, so the setlists
            // already pointing at it stay pointing at it.
            let id = existing?.id ?? UUID()
            let filename = "\(id.uuidString).\(ext)"
            let destination = audioDirectory.appendingPathComponent(filename)
            try? fileManager.removeItem(at: destination)
            try fileManager.moveItem(at: scratch, to: destination)

            let track = Track(
                id: id,
                title: existing?.title ?? source.deletingPathExtension().lastPathComponent,
                filename: filename,
                duration: duration,
                tags: existing?.tags ?? [],
                audioHash: hash
            )

            // Already on a background queue, and the file is warm from the
            // copy, so this is the cheapest moment to draw the overview.
            // A failure here is not fatal -- the view backfills it later.
            try? writeWaveform(for: track, from: destination)

            return existing == nil
                ? .created(track)
                : .relinked(id: id, filename: filename, duration: duration)
        } catch {
            try? fileManager.removeItem(at: scratch)
            throw error
        }
    }

    // MARK: - Waveforms

    /// Make sure a track's overview is loaded, generating it if this is an
    /// import from before waveforms existed or one that failed at import.
    /// Safe to call repeatedly from a view body path.
    func ensureWaveform(for track: Track) {
        guard waveforms[track.id] == nil, !pendingWaveforms.contains(track.id) else { return }

        if let data = try? Data(contentsOf: waveformURL(for: track)), !data.isEmpty {
            waveforms[track.id] = [UInt8](data)
            return
        }

        pendingWaveforms.insert(track.id)
        let audioURL = url(for: track)

        DispatchQueue.global(qos: .utility).async { [weak self] in
            let peaks = (try? WaveformGenerator.generate(from: audioURL)) ?? []
            DispatchQueue.main.async {
                guard let self else { return }
                self.pendingWaveforms.remove(track.id)
                guard !peaks.isEmpty else { return }
                self.waveforms[track.id] = peaks
                try? self.persistWaveform(peaks, for: track)
            }
        }
    }

    private func writeWaveform(for track: Track, from audioURL: URL) throws {
        let peaks = try WaveformGenerator.generate(from: audioURL)
        guard !peaks.isEmpty else { return }
        try persistWaveform(peaks, for: track)
    }

    private func persistWaveform(_ peaks: [UInt8], for track: Track) throws {
        try fileManager.createDirectory(at: waveformDirectory, withIntermediateDirectories: true)
        try Data(peaks).write(to: waveformURL(for: track), options: .atomic)
    }

    // MARK: - Tracks

    func track(id: UUID) -> Track? {
        tracks.first { $0.id == id }
    }

    /// Titles come from filenames on import, which is often close but rarely
    /// right, so they can be corrected.
    func rename(_ track: Track, to title: String) {
        guard let index = tracks.firstIndex(where: { $0.id == track.id }) else { return }
        let trimmed = title.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty else { return }
        tracks[index].title = trimmed
        sortTracks()
        save()
    }

    // MARK: - Tags

    /// Every tag in use, for the filter bars and the suggestions when tagging.
    var allTags: [String] {
        var seen: [String: String] = [:]
        for tag in tracks.flatMap(\.tags) {
            // Keyed case-insensitively so "Chill" and "chill" are one tag,
            // displayed however it was first typed.
            seen[tag.lowercased()] = seen[tag.lowercased()] ?? tag
        }
        return seen.values.sorted { $0.localizedStandardCompare($1) == .orderedAscending }
    }

    func addTag(_ raw: String, to track: Track) {
        let tag = raw.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !tag.isEmpty,
              let index = tracks.firstIndex(where: { $0.id == track.id }),
              !tracks[index].matches(tag: tag) else { return }
        tracks[index].tags.append(tag)
        save()
    }

    func removeTag(_ tag: String, from track: Track) {
        guard let index = tracks.firstIndex(where: { $0.id == track.id }) else { return }
        tracks[index].tags.removeAll { $0.caseInsensitiveCompare(tag) == .orderedSame }
        save()
    }

    /// Tracks carrying every one of `tags`, or all of them when nothing is
    /// selected. Filters are additive on purpose: "first dance" plus "slow"
    /// should narrow, not widen.
    func tracks(matching tags: Set<String>) -> [Track] {
        guard !tags.isEmpty else { return tracks }
        return tracks.filter { track in tags.allSatisfy { track.matches(tag: $0) } }
    }

    func deleteTracks(at offsets: IndexSet) {
        let doomed = offsets.map { tracks[$0] }
        for track in doomed {
            try? fileManager.removeItem(at: url(for: track))
            try? fileManager.removeItem(at: waveformURL(for: track))
            waveforms[track.id] = nil
        }
        tracks.remove(atOffsets: offsets)

        // Drop them from every setlist too, so an event cannot reference a
        // track that no longer exists.
        let removedIDs = Set(doomed.map(\.id))
        for index in events.indices {
            events[index].items.removeAll { removedIDs.contains($0.trackID) }
        }
        save()
    }

    private func sortTracks() {
        tracks.sort { $0.title.localizedStandardCompare($1.title) == .orderedAscending }
    }

    // MARK: - Events

    var activeEvent: Event? {
        guard let activeEventID else { return nil }
        return events.first { $0.id == activeEventID }
    }

    @discardableResult
    func addEvent(named name: String = "New event") -> Event {
        let event = Event(name: name)
        events.append(event)
        save()
        return event
    }

    func update(_ event: Event) {
        guard let index = events.firstIndex(where: { $0.id == event.id }) else { return }
        events[index] = event
        save()
    }

    func deleteEvents(at offsets: IndexSet) {
        let doomed = offsets.map { events[$0].id }
        events.remove(atOffsets: offsets)
        if let activeEventID, doomed.contains(activeEventID) { self.activeEventID = nil }
        save()
    }

    func setActive(_ event: Event?) {
        activeEventID = (activeEventID == event?.id) ? nil : event?.id
        save()
    }

    /// An event's running order, each entry paired with its track. Entries
    /// whose track has gone are dropped rather than shown as holes.
    func setlist(for event: Event) -> [SetlistEntry] {
        event.items.compactMap { item in
            guard let track = tracks.first(where: { $0.id == item.trackID }) else { return nil }
            return SetlistEntry(item: item, track: track)
        }
    }

    /// A fresh setlist entry playing the whole track, which is where every
    /// entry starts before it is cut.
    func newItem(for track: Track) -> SetlistItem {
        SetlistItem(trackID: track.id, edit: .whole(track.duration))
    }

    // MARK: - Sharing

    /// Build an interchange document. Pass one event to share just that running
    /// order and the songs it uses; pass nothing for the lot.
    func setDocument(for event: Event? = nil) -> SetDocument {
        let events = event.map { [$0] } ?? self.events

        let wanted: [Track]
        if let event {
            let ids = Set(event.items.map(\.trackID))
            wanted = tracks.filter { ids.contains($0.id) }
        } else {
            wanted = tracks
        }

        return SetDocument(
            format: SetDocument.identifier,
            version: SetDocument.currentVersion,
            exportedAt: Date(),
            exportedBy: "Big Fader iPadOS",
            tracks: wanted.map { track in
                ExportedTrack(
                    id: track.id,
                    title: track.title,
                    duration: track.duration,
                    tags: track.tags,
                    audio: ExportedAudio(
                        sha256: track.audioHash,
                        fileExtension: track.fileExtension,
                        bytes: fileSize(of: track)
                    )
                )
            },
            events: events.map { event in
                ExportedEvent(
                    id: event.id,
                    name: event.name,
                    date: event.date,
                    items: event.items.map { item in
                        ExportedItem(
                            id: item.id,
                            trackID: item.trackID,
                            startPoint: item.edit.startPoint,
                            endPoint: item.edit.endPoint,
                            fadeIn: item.edit.fadeIn,
                            fadeOut: item.edit.fadeOut
                        )
                    }
                )
            }
        )
    }

    /// Writes the document somewhere the share sheet can reach it, and hands
    /// back the URL.
    func exportedSetURL(for event: Event? = nil) -> URL? {
        let name = (event?.name ?? "Big Fader library")
            .components(separatedBy: CharacterSet.alphanumerics.union(.whitespaces).inverted)
            .joined()
            .trimmingCharacters(in: .whitespaces)

        // Named .bigfader.json rather than a custom extension: it really is
        // JSON, every share target and document picker already understands it,
        // and declaring a UTI would buy nothing but Info.plist work.
        let filename = (name.isEmpty ? "Set" : name) + ".bigfader.json"
        let destination = fileManager.temporaryDirectory.appendingPathComponent(filename)

        do {
            let data = try SetDocument.encoder().encode(setDocument(for: event))
            try data.write(to: destination, options: .atomic)
            return destination
        } catch {
            lastError = "Could not export: \(error.localizedDescription)"
            return nil
        }
    }

    /// Merge a shared set in. Nothing is ever deleted: songs already here are
    /// matched and left alone, songs that are not become entries waiting for
    /// their audio, and events are added or replaced by id.
    @discardableResult
    func importSet(from url: URL) -> ImportSummary? {
        let scoped = url.startAccessingSecurityScopedResource()
        defer { if scoped { url.stopAccessingSecurityScopedResource() } }

        let document: SetDocument
        do {
            document = try SetDocument.decoder().decode(SetDocument.self, from: Data(contentsOf: url))
        } catch {
            lastError = "Not a readable set file: \(error.localizedDescription)"
            return nil
        }

        guard document.format == SetDocument.identifier else {
            lastError = "That file is not a Big Fader set."
            return nil
        }
        guard document.version <= SetDocument.currentVersion else {
            lastError = "That set was written by a newer version of the app."
            return nil
        }

        var summary = ImportSummary()

        // Track IDs are per-device, so the document's IDs are remapped onto
        // local ones as the tracks are matched.
        var mapping: [UUID: UUID] = [:]

        for incoming in document.tracks {
            if let local = match(incoming) {
                mapping[incoming.id] = local.id
                summary.matchedTracks += 1
                continue
            }

            // No audio here for it. Keep it anyway: the running order is worth
            // having without the files, and importing the file later relinks it
            // by hash without disturbing the setlist.
            let placeholder = Track(
                id: incoming.id,
                title: incoming.title,
                filename: "",
                duration: incoming.duration,
                tags: incoming.tags,
                audioHash: incoming.audio?.sha256
            )
            tracks.append(placeholder)
            mapping[incoming.id] = placeholder.id
            summary.unresolvedTracks += 1
        }

        for incoming in document.events {
            let event = Event(
                id: incoming.id,
                name: incoming.name,
                date: incoming.date,
                items: incoming.items.compactMap { item in
                    guard let trackID = mapping[item.trackID] else { return nil }
                    return SetlistItem(
                        id: item.id,
                        trackID: trackID,
                        edit: EditPoints(
                            startPoint: item.startPoint,
                            endPoint: item.endPoint,
                            fadeIn: item.fadeIn,
                            fadeOut: item.fadeOut
                        )
                    )
                }
            )

            if let index = events.firstIndex(where: { $0.id == event.id }) {
                events[index] = event
                summary.updatedEvents += 1
            } else {
                events.append(event)
                summary.newEvents += 1
            }
        }

        sortTracks()
        repairSetlists()
        lastNotice = summary.sentence
        save()
        return summary
    }

    /// Hash first, because it is the only identifier that means the same thing
    /// on two devices. Then id, for a set exported and reimported here. Then
    /// title and length, which is a guess but a good one and beats making a
    /// duplicate of a song already sitting in the library.
    private func match(_ incoming: ExportedTrack) -> Track? {
        if let hash = incoming.audio?.sha256,
           let found = tracks.first(where: { $0.audioHash == hash }) {
            return found
        }
        if let found = tracks.first(where: { $0.id == incoming.id }) {
            return found
        }
        return tracks.first {
            $0.title.caseInsensitiveCompare(incoming.title) == .orderedSame
                && abs($0.duration - incoming.duration) < 1
        }
    }

    private func fileSize(of track: Track) -> Int? {
        guard track.isResolved else { return nil }
        return (try? url(for: track).resourceValues(forKeys: [.fileSizeKey]).fileSize)
    }

    /// Tracks imported before hashing existed have no identity to share. Fill
    /// them in quietly so an old library can still take part in a shared set.
    private func backfillHashes() {
        let missing = tracks.filter { $0.isResolved && $0.audioHash == nil }
        guard !missing.isEmpty else { return }

        let pairs = missing.map { ($0.id, url(for: $0)) }
        DispatchQueue.global(qos: .utility).async { [weak self] in
            let hashes = pairs.compactMap { id, url -> (UUID, String)? in
                guard let hash = try? AudioHash.sha256(of: url) else { return nil }
                return (id, hash)
            }
            DispatchQueue.main.async {
                guard let self, !hashes.isEmpty else { return }
                for (id, hash) in hashes {
                    guard let index = self.tracks.firstIndex(where: { $0.id == id }) else { continue }
                    self.tracks[index].audioHash = hash
                }
                self.save()
            }
        }
    }

    // MARK: - Persistence

    private func load() {
        guard let data = try? Data(contentsOf: indexURL) else { return }
        do {
            let file = try JSONDecoder().decode(LibraryFile.self, from: data)
            tracks = file.tracks
            events = file.events
            activeEventID = file.activeEventID
            sortTracks()

            // Drop anything whose audio has gone missing -- a restore from
            // backup, or a half-finished delete.
            // A track with no filename is one from a shared set still waiting
            // for its audio, which is not the same as one whose file has gone.
            tracks.removeAll { $0.isResolved && !fileManager.fileExists(atPath: url(for: $0).path) }
            repairSetlists()
            backfillHashes()
        } catch {
            lastError = "Could not read the library: \(error.localizedDescription)"
        }
    }

    /// Fill in edit points that cannot be right: entries carried over from the
    /// older format, where the points lived on the track and the running order
    /// stored bare IDs, plus anything whose end has drifted past its file.
    private func repairSetlists() {
        for eventIndex in events.indices {
            for itemIndex in events[eventIndex].items.indices {
                let item = events[eventIndex].items[itemIndex]
                guard let track = tracks.first(where: { $0.id == item.trackID }) else { continue }

                if item.edit.endPoint <= item.edit.startPoint + 0.01 || item.edit.endPoint > track.duration {
                    events[eventIndex].items[itemIndex].edit = .whole(track.duration)
                } else {
                    events[eventIndex].items[itemIndex].edit.clamp(to: track.duration)
                }
            }
        }
    }

    /// Force any pending write out now. Called when the app leaves the
    /// foreground, where a coalesced save would otherwise be lost if iPadOS
    /// reclaimed the app before the timer fired.
    func flush() {
        guard saveWorkItem != nil else { return }
        saveWorkItem?.cancel()
        writeNow()
    }

    /// The normal path: batch up rapid edits and write once they settle.
    private func save() {
        saveWorkItem?.cancel()
        let work = DispatchWorkItem { [weak self] in self?.writeNow() }
        saveWorkItem = work
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.4, execute: work)
    }

    private func writeNow() {
        saveWorkItem = nil
        let file = LibraryFile(tracks: tracks, events: events, activeEventID: activeEventID)
        do {
            let encoder = JSONEncoder()
            encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
            try encoder.encode(file).write(to: indexURL, options: .atomic)
        } catch {
            lastError = "Could not save the library: \(error.localizedDescription)"
        }
    }
}
