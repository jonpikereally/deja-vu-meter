import SwiftUI

/// A vertical fader. Reports movement as a delta in fractions of full travel
/// rather than an absolute value, which is what keeps the grab relative -- see
/// the drag gesture below for why that matters on a live output.
struct Fader: View {

    let level: Float
    var accent: Color = Theme.amber
    var isDimmed = false
    var showsTicks = true
    var capHeight: CGFloat = 46
    let onDelta: (Float) -> Void

    /// Y of the previous touch sample. The drag accumulates frame to frame
    /// rather than mapping the finger's absolute position onto the track: a
    /// grab should not slam a live output to wherever the finger happened to
    /// land, and accumulating also lets the fine ratio change mid-drag without
    /// the cap jumping.
    @State private var lastY: CGFloat?

    private let ticks: [CGFloat] = [1.0, 0.75, 0.5, 0.25, 0.0]

    var body: some View {
        GeometryReader { geo in
            let travel = max(1, geo.size.height - capHeight)
            let value = CGFloat(min(max(level, 0), 1))

            ZStack(alignment: .top) {
                RoundedRectangle(cornerRadius: 20, style: .continuous)
                    .fill(Theme.trackWell)
                    .overlay(
                        RoundedRectangle(cornerRadius: 20, style: .continuous)
                            .strokeBorder(Theme.hairline, lineWidth: 1)
                    )

                // Runs from the bottom of the well up to the underside of the
                // cap, so at zero there is no lit fill left showing.
                VStack(spacing: 0) {
                    Spacer(minLength: 0)
                    RoundedRectangle(cornerRadius: 16, style: .continuous)
                        .fill(
                            LinearGradient(
                                colors: [accent.opacity(0.55), accent],
                                startPoint: .bottom,
                                endPoint: .top
                            )
                        )
                        .opacity(isDimmed ? 0.18 : 1)
                        .frame(height: travel * value)
                }
                .padding(6)

                if showsTicks {
                    ForEach(ticks, id: \.self) { frac in
                        tick(at: frac, travel: travel, width: geo.size.width)
                    }
                }

                cap
                    .frame(height: capHeight)
                    .padding(.horizontal, 4)
                    .offset(y: travel * (1 - value))
            }
            .contentShape(Rectangle())
            .gesture(drag(travel: travel))
        }
    }

    // MARK: - Pieces

    private var cap: some View {
        ZStack {
            RoundedRectangle(cornerRadius: 12, style: .continuous)
                .fill(
                    LinearGradient(
                        colors: [Color(white: 0.30), Color(white: 0.16)],
                        startPoint: .top,
                        endPoint: .bottom
                    )
                )
                .shadow(color: .black.opacity(0.55), radius: 8, y: 3)

            RoundedRectangle(cornerRadius: 12, style: .continuous)
                .strokeBorder(Color.white.opacity(0.14), lineWidth: 1)

            // Centre line, the way a real fader cap reads its position.
            Rectangle()
                .fill(isDimmed ? Theme.red : accent)
                .frame(height: 3)
                .padding(.horizontal, 14)
        }
    }

    private func tick(at frac: CGFloat, travel: CGFloat, width: CGFloat) -> some View {
        let y = capHeight / 2 + travel * (1 - frac)
        return HStack(spacing: 5) {
            Rectangle()
                .fill(Theme.hairline)
                .frame(width: 10, height: 1)
            Text("\(Int(frac * 100))")
                .font(.system(size: 9, weight: .medium, design: .rounded))
                .monospacedDigit()
                .foregroundColor(Theme.label.opacity(0.55))
        }
        .position(x: width - 24, y: y)
    }

    // MARK: - Gesture

    private func drag(travel: CGFloat) -> some Gesture {
        DragGesture(minimumDistance: 0)
            .onChanged { value in
                let previous = lastY ?? value.startLocation.y
                let dy = previous - value.location.y   // up the screen is louder
                lastY = value.location.y
                onDelta(Float(dy / travel) * Float(fineness(for: value.translation.width)))
            }
            .onEnded { _ in lastY = nil }
    }

    /// Slide the finger sideways off the fader for finer resolution -- the
    /// usual trick for setting a level precisely on a touchscreen, where the
    /// full throw is only a few hundred points.
    private func fineness(for horizontalOffset: CGFloat) -> CGFloat {
        max(0.08, 1 / (1 + abs(horizontalOffset) / 90))
    }
}
