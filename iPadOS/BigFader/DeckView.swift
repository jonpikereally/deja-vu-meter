import SwiftUI

/// One deck strip: what is loaded, where it is, and its fader.
struct DeckView: View {

    @ObservedObject var deck: Deck
    let onLoad: () -> Void

    var body: some View {
        VStack(spacing: 8) {
            header
            title
            scrubber
            transport
            Fader(
                level: deck.level,
                isDimmed: !deck.isLoaded,
                showsTicks: false,
                capHeight: 38,
                onDelta: { deck.level = min(max(deck.level + $0, 0), 1) }
            )
            .frame(minHeight: 120)
        }
        .padding(10)
        .background(
            RoundedRectangle(cornerRadius: 16, style: .continuous)
                .fill(Theme.panel)
        )
        .overlay(
            RoundedRectangle(cornerRadius: 16, style: .continuous)
                .strokeBorder(Theme.hairline, lineWidth: 1)
        )
    }

    // MARK: - Pieces

    private var header: some View {
        HStack {
            Text("DECK \(deck.id)")
                .font(.system(size: 11, weight: .bold, design: .rounded))
                .kerning(1)
                .foregroundColor(deck.isPlaying ? Theme.amber : Theme.label)
            Spacer()
            Button(action: onLoad) {
                Text("LOAD")
                    .font(.system(size: 10, weight: .bold, design: .rounded))
                    .kerning(0.5)
                    .foregroundColor(Theme.labelStrong)
                    .padding(.horizontal, 10)
                    .padding(.vertical, 5)
                    .background(
                        RoundedRectangle(cornerRadius: 7, style: .continuous)
                            .fill(Theme.trackWell)
                    )
            }
            .buttonStyle(.plain)
        }
    }

    private var title: some View {
        Text(deck.loadError ?? deck.title)
            .font(.system(size: 12, weight: .medium, design: .rounded))
            .foregroundColor(deck.loadError == nil ? Theme.labelStrong : Theme.red)
            .lineLimit(1)
            .truncationMode(.middle)
            .frame(maxWidth: .infinity, alignment: .leading)
    }

    private var scrubber: some View {
        VStack(spacing: 2) {
            Slider(
                value: Binding(
                    get: { deck.position },
                    set: { deck.position = $0 }
                ),
                in: 0...max(deck.duration, 0.01),
                onEditingChanged: { editing in
                    deck.isScrubbing = editing
                    if !editing { deck.seek(to: deck.position) }
                }
            )
            .tint(Theme.amber)
            .disabled(!deck.isLoaded)

            HStack {
                Text(Self.clock(deck.position))
                Spacer()
                Text("-" + Self.clock(max(deck.duration - deck.position, 0)))
            }
            .font(.system(size: 10, weight: .medium, design: .rounded))
            .monospacedDigit()
            .foregroundColor(Theme.label)
        }
    }

    private var transport: some View {
        HStack(spacing: 8) {
            transportButton(symbol: "backward.end.fill", action: deck.cueToStart)
            transportButton(
                symbol: deck.isPlaying ? "pause.fill" : "play.fill",
                action: deck.togglePlay,
                isPrimary: true
            )
        }
    }

    private func transportButton(
        symbol: String,
        action: @escaping () -> Void,
        isPrimary: Bool = false
    ) -> some View {
        Button(action: action) {
            Image(systemName: symbol)
                .font(.system(size: isPrimary ? 17 : 13, weight: .semibold))
                .foregroundColor(isPrimary && deck.isPlaying ? .black : Theme.labelStrong)
                .frame(maxWidth: .infinity)
                .frame(height: 38)
                .background(
                    RoundedRectangle(cornerRadius: 9, style: .continuous)
                        .fill(isPrimary && deck.isPlaying ? Theme.amber : Theme.trackWell)
                )
        }
        .buttonStyle(.plain)
        .disabled(!deck.isLoaded)
        .opacity(deck.isLoaded ? 1 : 0.4)
    }

    private static func clock(_ seconds: TimeInterval) -> String {
        guard seconds.isFinite, seconds >= 0 else { return "0:00" }
        let whole = Int(seconds.rounded(.down))
        return String(format: "%d:%02d", whole / 60, whole % 60)
    }
}
