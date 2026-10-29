import SwiftUI

/// Draws a peak overview, with the trimmed region lit, the fade ramps drawn
/// over it, and an optional playhead.
///
/// Rendered per screen column rather than per bin: there are more bins than
/// pixels at these sizes, so each column takes the loudest bin it covers. That
/// keeps a transient visible instead of letting it fall between samples.
struct WaveformView: View {

    let peaks: [UInt8]

    /// The trimmed region, as fractions of the whole file. Everything outside
    /// it is drawn dim.
    var range: ClosedRange<Double> = 0...1

    /// Fade lengths, also as fractions of the whole file.
    var fadeIn: Double = 0
    var fadeOut: Double = 0

    /// Playhead, as a fraction of the whole file.
    var progress: Double? = nil

    var accent: Color = Theme.amber
    var showsEnvelope = true

    var body: some View {
        ZStack {
            RoundedRectangle(cornerRadius: 8, style: .continuous)
                .fill(Theme.trackWell)

            if peaks.isEmpty {
                Text("...")
                    .font(.system(size: 10, weight: .medium, design: .rounded))
                    .foregroundColor(Theme.label.opacity(0.5))
            } else {
                // No padding: the edit handles and the scrub gesture both
                // compute against this view's own width, so any inset here
                // would put the drawing and the touch coordinates out of step.
                Canvas { context, size in
                    draw(in: context, size: size)
                }
            }
        }
        .clipShape(RoundedRectangle(cornerRadius: 8, style: .continuous))
    }

    private func draw(in context: GraphicsContext, size: CGSize) {
        let columns = max(Int(size.width), 1)
        let count = peaks.count
        let mid = size.height / 2

        for column in 0..<columns {
            let fraction = Double(column) / Double(columns)
            let lower = Int(fraction * Double(count))
            let upper = max(lower + 1, Int(Double(column + 1) / Double(columns) * Double(count)))

            var peak: UInt8 = 0
            for index in lower..<min(upper, count) {
                peak = max(peak, peaks[index])
            }

            // A floor of one point, so silence still reads as a centre line
            // rather than a gap in the waveform.
            let height = max(1, Double(peak) / 255 * Double(size.height) * 0.92)
            let bar = CGRect(x: Double(column), y: mid - height / 2, width: 1, height: height)

            let inside = fraction >= range.lowerBound && fraction <= range.upperBound
            context.fill(
                Path(bar),
                with: .color(inside ? accent : Theme.label.opacity(0.22))
            )
        }

        if showsEnvelope {
            drawEnvelope(in: context, size: size)
        }

        if let progress {
            let x = min(max(progress, 0), 1) * size.width
            context.fill(
                Path(CGRect(x: x - 0.5, y: 0, width: 1.5, height: size.height)),
                with: .color(.white.opacity(0.85))
            )
        }
    }

    /// The gain envelope over the trimmed region: up over the fade in, flat,
    /// down over the fade out. Drawn as a line across the top so it reads as a
    /// shape applied to the audio rather than as part of it.
    private func drawEnvelope(in context: GraphicsContext, size: CGSize) {
        guard fadeIn > 0.0001 || fadeOut > 0.0001 else { return }

        let width = size.width
        let top = size.height * 0.06
        let bottom = size.height * 0.94

        let start = range.lowerBound * width
        let end = range.upperBound * width
        let inEnd = min(start + fadeIn * width, end)
        let outStart = max(end - fadeOut * width, inEnd)

        var path = Path()
        path.move(to: CGPoint(x: start, y: fadeIn > 0.0001 ? bottom : top))
        path.addLine(to: CGPoint(x: inEnd, y: top))
        path.addLine(to: CGPoint(x: outStart, y: top))
        path.addLine(to: CGPoint(x: end, y: fadeOut > 0.0001 ? bottom : top))

        context.stroke(path, with: .color(.white.opacity(0.75)), lineWidth: 1.5)
    }
}
