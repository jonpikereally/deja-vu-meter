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

        DispatchQueue.global(qos: .userInitiated).async { [weak self] in
            guard let self else { return }

            var imported: [Track] = []
            var failures: [String] = []

            for url in urls {
                do {
                    imported.append(try self.copyIn(url))
                } catch {
                    failures.append("\(url.lastPathComponent): \(error.localizedDescription)")
                }
            }

            DispatchQueue.main.async {
                self.tracks.append(contentsOf: imported)
                self.sortTracks()
                self.isImporting = false
                self.lastError = failures.isEmpty ? nil : failures.joined(separator: "\n")
                self.save()
            }
        }
    }

    private func copyIn(_ source: URL) throws -> Track {
        // Picked URLs arrive security-scoped. The scope only has to last long
        // enough to make the copy.
        let scoped = source.startAccessingSecurityScopedResource()
        defer { if scoped { source.stopAccessingSecurityScopedResource() } }

        try fileManager.createDirectory(at: audioDirectory, withIntermediateDirectories: true)

        let id = UUID()
        let ext = source.pathExtension.isEmpty ? "m4a" : source.pathExtension
        let filename = "\(id.uuidString).\(ext)"
        let destination = audioDirectory.appendingPathComponent(filename)

        // A coordinated read, not a bare copyItem. A file in Dropbox, Google
        // Drive or iCloud may be a placeholder that is not on the device yet;
        // coordinating is what makes the provider materialise it first. Copying
        // a placeholder directly either fails or silently produces a stub.
        var coordinationError: NSError?
        var copyError: Error?

        NSFileCoordinator().coordinate(readingItemAt: source, options: [], error: &coordinationError) { readable in
            do {
                try fileManager.copyItem(at: readable, to: destination)
            } catch {
                copyError = error
            }
        }

        if let coordinationError { throw coordinationError }
        if let copyError { throw copyError }

        // Prove it decodes before it joins the library, and get its length.
        // A file that fails here would otherwise sit in a setlist looking fine
        // until the moment it was needed.
        do {
            let file = try AVAudioFile(forReading: destination)
            let rate = file.processingFormat.sampleRate
            let duration = rate > 0 ? Double(file.length) / rate : 0

            let track = Track(
                id: id,
                title: source.deletingPathExtension().lastPathComponent,
                filename: filename,
                duration: duration
            )

            // Already on a background queue, and the file is warm from the
            // copy, so this is the cheapest moment to draw the overview.
            // A failure here is not fatal -- the view backfills it later.
            try? writeWaveform(for: track, from: destination)

            return track
        } catch {
            try? fileManager.removeItem(at: destination)
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
            tracks.removeAll { !fileManager.fileExists(atPath: url(for: $0).path) }
            repairSetlists()
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
