import SwiftUI

/// One event: its name, its date, and its running order.
struct EventEditorView: View {

    @State private var draft: Event
    @ObservedObject private var store: LibraryStore
    let onCommit: (Event) -> Void

    @Environment(\.dismiss) private var dismiss
    @State private var isAddingSongs = false

    init(event: Event, store: LibraryStore, onCommit: @escaping (Event) -> Void) {
        _draft = State(initialValue: event)
        _store = ObservedObject(wrappedValue: store)
        self.onCommit = onCommit
    }

    var body: some View {
        NavigationStack {
            VStack(spacing: 0) {
                details
                setlistHeader
                setlist
            }
            .background(Theme.background.ignoresSafeArea())
            .navigationTitle("Event")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .confirmationAction) {
                    Button("Done") { dismiss() }
                }
            }
        }
        .preferredColorScheme(.dark)
        .onChange(of: draft) { edited in onCommit(edited) }
        .sheet(isPresented: $isAddingSongs) {
            TrackPickerView(
                title: "Add songs",
                sections: [("LIBRARY", store.tracks)],
                dismissesOnPick: false
            ) { track in
                draft.trackIDs.append(track.id)
            }
        }
    }

    // MARK: - Pieces

    private var details: some View {
        VStack(spacing: 12) {
            TextField("Event name", text: $draft.name)
                .textFieldStyle(.plain)
                .font(.system(size: 17, weight: .semibold, design: .rounded))
                .foregroundColor(Theme.labelStrong)
                .padding(12)
                .background(
                    RoundedRectangle(cornerRadius: 10, style: .continuous)
                        .fill(Theme.panel)
                )

            DatePicker("Date", selection: $draft.date, displayedComponents: [.date])
                .datePickerStyle(.compact)
                .tint(Theme.amber)
                .foregroundColor(Theme.label)
                .padding(.horizontal, 12)
                .padding(.vertical, 8)
                .background(
                    RoundedRectangle(cornerRadius: 10, style: .continuous)
                        .fill(Theme.panel)
                )

            Button {
                store.setActive(draft)
            } label: {
                Text(store.activeEventID == draft.id ? "ACTIVE - TAP TO CLEAR" : "MAKE ACTIVE")
                    .font(.system(size: 11, weight: .bold, design: .rounded))
                    .kerning(1)
                    .foregroundColor(store.activeEventID == draft.id ? .black : Theme.labelStrong)
                    .frame(maxWidth: .infinity)
                    .frame(height: 42)
                    .background(
                        RoundedRectangle(cornerRadius: 10, style: .continuous)
                            .fill(store.activeEventID == draft.id ? Theme.amber : Theme.panel)
                    )
            }
            .buttonStyle(.plain)
        }
        .padding(16)
    }

    private var setlistHeader: some View {
        HStack {
            Text("SETLIST")
                .font(.system(size: 10, weight: .bold, design: .rounded))
                .kerning(1)
                .foregroundColor(Theme.label)
            Spacer()
            Text(runningTime)
                .font(.system(size: 10, weight: .medium, design: .rounded))
                .monospacedDigit()
                .foregroundColor(Theme.label)
            Button { isAddingSongs = true } label: {
                Image(systemName: "plus")
                    .font(.system(size: 12, weight: .bold))
                    .foregroundColor(.black)
                    .frame(width: 30, height: 30)
                    .background(Circle().fill(Theme.amber))
            }
            .buttonStyle(.plain)
        }
        .padding(.horizontal, 16)
        .padding(.bottom, 8)
    }

    private var setlist: some View {
        List {
            // Indexed rather than keyed by track ID, so the same song can
            // appear twice in a running order without the rows colliding.
            ForEach(Array(draft.trackIDs.enumerated()), id: \.offset) { index, trackID in
                HStack(spacing: 10) {
                    Text("\(index + 1)")
                        .font(.system(size: 11, weight: .bold, design: .rounded))
                        .monospacedDigit()
                        .foregroundColor(Theme.amber)
                        .frame(width: 22, alignment: .trailing)

                    if let track = store.track(id: trackID) {
                        VStack(alignment: .leading, spacing: 2) {
                            Text(track.title)
                                .font(.system(size: 13, weight: .semibold, design: .rounded))
                                .foregroundColor(Theme.labelStrong)
                                .lineLimit(1)
                            Text(summary(for: track))
                                .font(.system(size: 10, weight: .medium, design: .rounded))
                                .monospacedDigit()
                                .foregroundColor(Theme.label)
                        }
                    } else {
                        Text("Missing track")
                            .font(.system(size: 13, weight: .medium, design: .rounded))
                            .foregroundColor(Theme.red)
                    }

                    Spacer()
                }
                .listRowBackground(Theme.panel)
            }
            .onDelete { draft.trackIDs.remove(atOffsets: $0) }
            .onMove { draft.trackIDs.move(fromOffsets: $0, toOffset: $1) }
        }
        .listStyle(.plain)
        .scrollContentBackground(.hidden)
        // Always in edit mode, so the running order can be dragged without
        // hunting for an Edit button first.
        .environment(\.editMode, .constant(.active))
    }

    private var runningTime: String {
        let total = draft.trackIDs
            .compactMap { store.track(id: $0) }
            .reduce(0) { $0 + $1.playingLength }
        return "\(draft.trackIDs.count) songs  -  \(TimeFormat.clock(total))"
    }

    private func summary(for track: Track) -> String {
        var parts = [TimeFormat.clock(track.playingLength)]
        if track.hasFades {
            parts.append("in \(TimeFormat.seconds(track.fadeIn)) / out \(TimeFormat.seconds(track.fadeOut))")
        }
        return parts.joined(separator: "  -  ")
    }
}
