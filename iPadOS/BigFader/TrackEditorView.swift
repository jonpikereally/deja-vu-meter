import SwiftUI

/// Per-song edit points: where it starts, where it ends, and how long it takes
/// to get in and out.
///
/// These live on the track rather than on a setlist entry, so a song trimmed
/// once is trimmed in every event that uses it.
struct TrackEditorView: View {

    @State private var draft: Track
    let onCommit: (Track) -> Void

    @Environment(\.dismiss) private var dismiss

    init(track: Track, onCommit: @escaping (Track) -> Void) {
        _draft = State(initialValue: track)
        self.onCommit = onCommit
    }

    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(spacing: 18) {
                    summary

                    slider(
                        "START AT",
                        value: $draft.startPoint,
                        range: 0...max(draft.endPoint - 0.5, 0.5),
                        display: TimeFormat.precise(draft.startPoint),
                        step: 0.1
                    )

                    slider(
                        "END AT",
                        value: $draft.endPoint,
                        range: min(draft.startPoint + 0.5, draft.duration)...max(draft.duration, 1),
                        display: TimeFormat.precise(draft.endPoint),
                        step: 0.1
                    )

                    slider(
                        "FADE IN",
                        value: $draft.fadeIn,
                        range: 0...max(min(30, draft.playingLength / 2), 0.5),
                        display: TimeFormat.seconds(draft.fadeIn),
                        step: 0.5
                    )

                    slider(
                        "FADE OUT",
                        value: $draft.fadeOut,
                        range: 0...max(min(30, draft.playingLength / 2), 0.5),
                        display: TimeFormat.seconds(draft.fadeOut),
                        step: 0.5
                    )

                    Button {
                        draft.startPoint = 0
                        draft.endPoint = draft.duration
                        draft.fadeIn = 0
                        draft.fadeOut = 0
                    } label: {
                        Text("RESET TO WHOLE TRACK")
                            .font(.system(size: 11, weight: .bold, design: .rounded))
                            .kerning(0.5)
                            .foregroundColor(Theme.label)
                            .frame(maxWidth: .infinity)
                            .frame(height: 42)
                            .background(
                                RoundedRectangle(cornerRadius: 10, style: .continuous)
                                    .fill(Theme.panel)
                            )
                    }
                    .buttonStyle(.plain)
                }
                .padding(18)
            }
            .background(Theme.background.ignoresSafeArea())
            .navigationTitle(draft.title)
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .confirmationAction) {
                    Button("Done") { dismiss() }
                }
            }
        }
        .preferredColorScheme(.dark)
        // Committing live rather than on Done means a deck already holding this
        // track picks the change up while you are still dragging.
        .onChange(of: draft) { edited in
            // Clamp the draft itself rather than only what is committed, so the
            // sliders can never display a start past its own end. Settles after
            // one extra pass, since clamping is idempotent.
            var clamped = edited
            clamped.clampEditPoints()
            if clamped != edited {
                draft = clamped
            } else {
                onCommit(clamped)
            }
        }
    }

    // MARK: - Pieces

    private var summary: some View {
        HStack {
            label("PLAYS FOR", TimeFormat.clock(draft.playingLength))
            Spacer()
            label("FULL LENGTH", TimeFormat.clock(draft.duration))
        }
        .padding(14)
        .background(
            RoundedRectangle(cornerRadius: 12, style: .continuous)
                .fill(Theme.panel)
        )
    }

    private func label(_ caption: String, _ value: String) -> some View {
        VStack(alignment: .leading, spacing: 3) {
            Text(caption)
                .font(.system(size: 9, weight: .bold, design: .rounded))
                .kerning(1)
                .foregroundColor(Theme.label)
            Text(value)
                .font(.system(size: 17, weight: .semibold, design: .rounded))
                .monospacedDigit()
                .foregroundColor(Theme.labelStrong)
        }
    }

    private func slider(
        _ caption: String,
        value: Binding<TimeInterval>,
        range: ClosedRange<Double>,
        display: String,
        step: Double
    ) -> some View {
        VStack(alignment: .leading, spacing: 6) {
            HStack {
                Text(caption)
                    .font(.system(size: 10, weight: .bold, design: .rounded))
                    .kerning(1)
                    .foregroundColor(Theme.label)
                Spacer()
                Text(display)
                    .font(.system(size: 14, weight: .semibold, design: .rounded))
                    .monospacedDigit()
                    .foregroundColor(Theme.amber)
            }

            HStack(spacing: 10) {
                nudge("minus", by: -step, value: value, range: range)
                Slider(value: value, in: range)
                    .tint(Theme.amber)
                nudge("plus", by: step, value: value, range: range)
            }
        }
        .padding(14)
        .background(
            RoundedRectangle(cornerRadius: 12, style: .continuous)
                .fill(Theme.panel)
        )
    }

    private func nudge(
        _ symbol: String,
        by delta: Double,
        value: Binding<TimeInterval>,
        range: ClosedRange<Double>
    ) -> some View {
        Button {
            value.wrappedValue = min(max(value.wrappedValue + delta, range.lowerBound), range.upperBound)
        } label: {
            Image(systemName: symbol)
                .font(.system(size: 11, weight: .bold))
                .foregroundColor(Theme.labelStrong)
                .frame(width: 34, height: 34)
                .background(
                    RoundedRectangle(cornerRadius: 8, style: .continuous)
                        .fill(Theme.trackWell)
                )
        }
        .buttonStyle(.plain)
    }
}
