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

    private var indexURL: URL {
        documents.appendingPathComponent("library.json")
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

            return Track(
                id: id,
                title: source.deletingPathExtension().lastPathComponent,
                filename: filename,
                duration: duration
            )
        } catch {
            try? fileManager.removeItem(at: destination)
            throw error
        }
    }

    // MARK: - Tracks

    func track(id: UUID) -> Track? {
        tracks.first { $0.id == id }
    }

    func update(_ track: Track) {
        guard let index = tracks.firstIndex(where: { $0.id == track.id }) else { return }
        var edited = track
        edited.clampEditPoints()
        tracks[index] = edited
        save()
    }

    func deleteTracks(at offsets: IndexSet) {
        let doomed = offsets.map { tracks[$0] }
        for track in doomed {
            try? fileManager.removeItem(at: url(for: track))
        }
        tracks.remove(atOffsets: offsets)

        // Drop them from every setlist too, so an event cannot reference a
        // track that no longer exists.
        let removedIDs = Set(doomed.map(\.id))
        for index in events.indices {
            events[index].trackIDs.removeAll { removedIDs.contains($0) }
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

    /// The tracks of an event, in running order, skipping any that have gone.
    func setlist(for event: Event) -> [Track] {
        event.trackIDs.compactMap { id in tracks.first { $0.id == id } }
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
        } catch {
            lastError = "Could not read the library: \(error.localizedDescription)"
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
