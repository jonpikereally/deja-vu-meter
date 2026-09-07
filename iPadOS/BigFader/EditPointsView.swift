import SwiftUI

/// The edit points for one setlist entry: where it starts, where it ends, and
/// how long it takes to get in and out.
///
/// These belong to the entry, not the track, so the same song can be cut one
/// way in one event and differently in another.
struct EditPointsView: View {

    let track: Track
    @State private var draft: EditPoints
    @ObservedObject private var store: LibraryStore
    let onCommit: (EditPoints) -> Void

    @Environment(\.dismiss) private var dismiss

    init(
        track: Track,
        edit: EditPoints,
        store: LibraryStore,
        onCommit: @escaping (EditPoints) -> Void
    ) {
        self.track = track
        _draft = State(initialValue: edit)
        _store = ObservedObject(wrappedValue: store)
        self.onCommit = onCommit
    }

    /// Edit points as fractions of the whole file, which is what the waveform
    /// draws in.
    private var startFraction: Double {
        track.duration > 0 ? draft.startPoint / track.duration : 0
    }

    private var endFraction: Double {
        track.duration > 0 ? min(draft.endPoint / track.duration, 1) : 1
    }

    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(spacing: 18) {
                    waveform
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
                        range: min(draft.startPoint + 0.5, track.duration)...max(track.duration, 1),
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
                        draft = .whole(track.duration)
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

                    Text("These edit points belong to this setlist entry. The same song in another event keeps its own.")
                        .font(.system(size: 10, weight: .medium, design: .rounded))
                        .foregroundColor(Theme.label)
                        .multilineTextAlignment(.center)
                        .padding(.horizontal, 12)
                }
                .padding(18)
            }
            .background(Theme.background.ignoresSafeArea())
            .onAppear { store.ensureWaveform(for: track) }
            .navigationTitle(track.title)
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .confirmationAction) {
                    Button("Done") { dismiss() }
                }
            }
        }
        .preferredColorScheme(.dark)
        .onChange(of: draft) { edited in
            // Clamp the draft itself rather than only what is committed, so the
            // sliders can never display a start past its own end. Settles after
            // one extra pass, since clamping is idempotent.
            var clamped = edited
            clamped.clamp(to: track.duration)
            if clamped != edited {
                draft = clamped
            } else {
                // Committing live rather than on Done means a deck already
                // playing this entry picks the change up mid-drag.
                onCommit(clamped)
            }
        }
    }

    // MARK: - Pieces

    private var waveform: some View {
        VStack(spacing: 6) {
            GeometryReader { geo in
                ZStack(alignment: .topLeading) {
                    WaveformView(
                        peaks: store.waveforms[track.id] ?? [],
                        range: startFraction...max(endFraction, startFraction),
                        fadeIn: track.duration > 0 ? draft.fadeIn / track.duration : 0,
                        fadeOut: track.duration > 0 ? draft.fadeOut / track.duration : 0
                    )

                    handle(at: startFraction, isStart: true, in: geo.size)
                    handle(at: endFraction, isStart: false, in: geo.size)
                }
                .coordinateSpace(name: "waveform")
            }
            .frame(height: 120)

            Text("Drag the handles to trim. The sliders below do the same thing to a tenth of a second.")
                .font(.system(size: 10, weight: .medium, design: .rounded))
                .foregroundColor(Theme.label)
        }
    }

    private func handle(at fraction: Double, isStart: Bool, in size: CGSize) -> some View {
        let x = min(max(fraction, 0), 1) * size.width

        return ZStack {
            Rectangle()
                .fill(Color.white.opacity(0.9))
                .frame(width: 2)
            RoundedRectangle(cornerRadius: 3, style: .continuous)
                .fill(Color.white.opacity(0.9))
                .frame(width: 10, height: 22)
                .offset(y: isStart ? -size.height / 2 + 11 : size.height / 2 - 11)
        }
        // A 2pt bar is not a touch target; the wider frame is.
        .frame(width: 40, height: size.height)
        .contentShape(Rectangle())
        .position(x: x, y: size.height / 2)
        .gesture(
            DragGesture(minimumDistance: 0, coordinateSpace: .named("waveform"))
                .onChanged { value in
                    guard track.duration > 0 else { return }
                    let moved = min(max(value.location.x / size.width, 0), 1) * track.duration
                    if isStart {
                        draft.startPoint = min(moved, draft.endPoint - 0.5)
                    } else {
                        draft.endPoint = max(moved, draft.startPoint + 0.5)
                    }
                }
        )
    }

    private var summary: some View {
        HStack {
            caption("PLAYS FOR", TimeFormat.clock(draft.playingLength))
            Spacer()
            caption("FULL LENGTH", TimeFormat.clock(track.duration))
        }
        .padding(14)
        .background(
            RoundedRectangle(cornerRadius: 12, style: .continuous)
                .fill(Theme.panel)
        )
    }

    private func caption(_ text: String, _ value: String) -> some View {
        VStack(alignment: .leading, spacing: 3) {
            Text(text)
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
