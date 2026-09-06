import SwiftUI

/// One deck strip: what is loaded, where it is, and its fader.
struct DeckView: View {

    @ObservedObject var deck: Deck
    @ObservedObject var store: LibraryStore
    let onLoad: () -> Void
    let onEject: () -> Void

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
        .onAppear { ensureWaveform() }
        .onChange(of: deck.cue) { _ in ensureWaveform() }
    }

    private func ensureWaveform() {
        guard let track = deck.cue?.track else { return }
        store.ensureWaveform(for: track)
    }

    /// The trimmed region as fractions of the whole file, for the waveform.
    private var trimRange: ClosedRange<Double> {
        guard deck.duration > 0, let edit = deck.edit else { return 0...1 }
        let lower = min(max(edit.startPoint / deck.duration, 0), 1)
        let upper = min(max(edit.endPoint / deck.duration, lower), 1)
        return lower...upper
    }

    private func fraction(of seconds: TimeInterval) -> Double {
        deck.duration > 0 ? seconds / deck.duration : 0
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

    /// Drag the waveform to scrub. Absolute here, unlike the faders: you are
    /// pointing at a place in the song, not nudging a live level.
    private var scrubber: some View {
        VStack(spacing: 4) {
            GeometryReader { geo in
                WaveformView(
                    peaks: deck.cue.map { store.waveforms[$0.track.id] ?? [] } ?? [],
                    range: trimRange,
                    fadeIn: fraction(of: deck.edit?.fadeIn ?? 0),
                    fadeOut: fraction(of: deck.edit?.fadeOut ?? 0),
                    progress: deck.duration > 0 ? deck.position / deck.duration : nil
                )
                .contentShape(Rectangle())
                .gesture(
                    DragGesture(minimumDistance: 0)
                        .onChanged { value in
                            guard deck.isLoaded, deck.duration > 0 else { return }
                            deck.isScrubbing = true
                            let hit = min(max(value.location.x / geo.size.width, 0), 1)
                            deck.position = hit * deck.duration
                        }
                        .onEnded { _ in
                            guard deck.isLoaded else { return }
                            deck.isScrubbing = false
                            deck.seek(to: deck.position)
                        }
                )
            }
            .frame(height: 46)
            .opacity(deck.isLoaded ? 1 : 0.5)

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
            transportButton(symbol: "eject.fill", action: onEject)
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
