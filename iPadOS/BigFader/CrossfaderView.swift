import SwiftUI

/// The horizontal crossfader. Relative drag like the vertical faders, plus a
/// double tap to snap back to centre.
struct CrossfaderView: View {

    @Binding var value: Float          // 0 = A, 1 = B
    let onCentre: () -> Void

    @State private var lastX: CGFloat?

    private let capWidth: CGFloat = 54

    var body: some View {
        VStack(spacing: 6) {
            HStack {
                Text("A").foregroundColor(value < 0.5 ? Theme.amber : Theme.label)
                Spacer()
                Text("CROSSFADE").foregroundColor(Theme.label.opacity(0.7))
                Spacer()
                Text("B").foregroundColor(value > 0.5 ? Theme.amber : Theme.label)
            }
            .font(.system(size: 10, weight: .bold, design: .rounded))
            .kerning(1)

            GeometryReader { geo in
                let travel = max(1, geo.size.width - capWidth)
                let position = CGFloat(min(max(value, 0), 1))

                ZStack(alignment: .leading) {
                    RoundedRectangle(cornerRadius: 16, style: .continuous)
                        .fill(Theme.trackWell)
                        .overlay(
                            RoundedRectangle(cornerRadius: 16, style: .continuous)
                                .strokeBorder(Theme.hairline, lineWidth: 1)
                        )

                    // Centre detent marker.
                    Rectangle()
                        .fill(Theme.hairline)
                        .frame(width: 1)
                        .padding(.vertical, 8)
                        .offset(x: geo.size.width / 2)

                    cap
                        .frame(width: capWidth)
                        .padding(.vertical, 4)
                        .offset(x: travel * position)
                }
                .contentShape(Rectangle())
                .gesture(
                    DragGesture(minimumDistance: 0)
                        .onChanged { drag in
                            let previous = lastX ?? drag.startLocation.x
                            let dx = drag.location.x - previous
                            lastX = drag.location.x
                            value = min(max(value + Float(dx / travel), 0), 1)
                        }
                        .onEnded { _ in lastX = nil }
                )
                .onTapGesture(count: 2) { onCentre() }
            }
            .frame(height: 54)
        }
    }

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

            Rectangle()
                .fill(Theme.amber)
                .frame(width: 3)
                .padding(.vertical, 12)
        }
    }
}
