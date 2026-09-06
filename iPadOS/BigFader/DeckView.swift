import SwiftUI

/// One deck strip: what is loaded, where it is, and its fader.
struct DeckView: View {

    @ObservedObject var deck: Deck
    let onLoad: () -> Void

    var body: some View {
        VStack(spacing: 8) {
            header
            title
            badges
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

    /// Shows at a glance that a loaded song is trimmed or faded, so an edit
    /// made in the setlist is visible from the mixer.
    @ViewBuilder
    private var badges: some View {
        if let edit = deck.edit, edit.isTrimmed(of: deck.duration) || edit.hasFades {
            HStack(spacing: 5) {
                if edit.isTrimmed(of: deck.duration) {
                    badge("TRIM " + TimeFormat.clock(edit.startPoint) + "-" + TimeFormat.clock(edit.endPoint))
                }
                if edit.hasFades {
                    badge("IN " + TimeFormat.seconds(edit.fadeIn) + " OUT " + TimeFormat.seconds(edit.fadeOut))
                }
                Spacer()
            }
        }
    }

    private func badge(_ text: String) -> some View {
        Text(text)
            .font(.system(size: 8, weight: .bold, design: .rounded))
            .monospacedDigit()
            .foregroundColor(Theme.amber)
            .padding(.horizontal, 5)
            .padding(.vertical, 2)
            .background(
                RoundedRectangle(cornerRadius: 4, style: .continuous)
                    .fill(Theme.amber.opacity(0.14))
            )
            .lineLimit(1)
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
                Text(TimeFormat.clock(deck.position))
                Spacer()
                // Counts down to the track's end point, not the end of the
                // file, so a trimmed song reads as the length it will play.
                Text("-" + TimeFormat.clock(max(deck.endPoint - deck.position, 0)))
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
}
