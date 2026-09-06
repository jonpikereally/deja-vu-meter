import CryptoKit
import Foundation

/// A track's identity across devices.
///
/// Track IDs are generated locally, so the same song imported on an iPad and in
/// a browser gets two different UUIDs and a shared setlist would point at
/// nothing. The hash of the audio bytes is the same on both, which is what lets
/// an exported running order find the files already sitting on the other
/// machine.
enum AudioHash {

    /// Streamed in 1 MB chunks rather than read whole: a library import is
    /// several files in a row and a hundred-megabyte read per track would show.
    static func sha256(of url: URL) throws -> String {
        let handle = try FileHandle(forReadingFrom: url)
        defer { try? handle.close() }

        var hasher = SHA256()
        while let chunk = try handle.read(upToCount: 1 << 20), !chunk.isEmpty {
            hasher.update(data: chunk)
        }

        return hasher.finalize().map { String(format: "%02x", $0) }.joined()
    }
}
