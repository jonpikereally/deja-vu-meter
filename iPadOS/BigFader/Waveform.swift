import Accelerate
import AVFoundation
import Foundation

/// Builds the peak overview drawn behind the edit points.
///
/// One byte per bin, normalised to the track's own loudest moment: this is a
/// picture of the *shape* of a song, for finding where the intro ends and the
/// outro starts, not a level meter. A quiet recording should still fill the
/// frame.
enum WaveformGenerator {

    /// Enough detail to see a bar line on a four-minute track, small enough
    /// that the whole overview is about a kilobyte on disk.
    static let binCount = 1_200

    static func generate(from url: URL, bins: Int = binCount) throws -> [UInt8] {
        let file = try AVAudioFile(forReading: url)
        let format = file.processingFormat
        let total = file.length
        guard total > 0, bins > 0 else { return [] }

        let framesPerBin = max(1, Int((Double(total) / Double(bins)).rounded(.up)))

        // One buffer, reused for every bin: the whole point is not to hold a
        // five-minute file in memory to draw a thumbnail of it.
        guard let buffer = AVAudioPCMBuffer(
            pcmFormat: format,
            frameCapacity: AVAudioFrameCount(framesPerBin)
        ) else {
            return []
        }

        var peaks: [Float] = []
        peaks.reserveCapacity(bins)
        file.framePosition = 0

        while peaks.count < bins {
            try file.read(into: buffer, frameCount: AVAudioFrameCount(framesPerBin))
            let frames = Int(buffer.frameLength)
            if frames == 0 { break }

            var peak: Float = 0
            if let channels = buffer.floatChannelData {
                for channel in 0..<Int(buffer.format.channelCount) {
                    var channelPeak: Float = 0
                    // Max magnitude of the whole chunk in one call. Doing this
                    // in a Swift loop is tens of millions of iterations per
                    // track and shows up as a visible stall on import.
                    vDSP_maxmgv(channels[channel], 1, &channelPeak, vDSP_Length(frames))
                    peak = max(peak, channelPeak)
                }
            }
            peaks.append(peak)
        }

        guard let loudest = peaks.max(), loudest > 0.0001 else {
            return [UInt8](repeating: 0, count: peaks.count)
        }

        var overview = peaks.map { UInt8(min($0 / loudest, 1) * 255) }

        // A file shorter than the bin count leaves the tail unread; pad it flat
        // so the drawing code can always assume a full-width overview.
        if overview.count < bins {
            overview.append(contentsOf: [UInt8](repeating: 0, count: bins - overview.count))
        }
        return overview
    }
}
