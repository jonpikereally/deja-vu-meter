import SwiftUI

/// One event: its name, its date, and its running order -- with each entry's
/// own edit points.
struct EventEditorView: View {

    /// A setlist entry opened for editing. Identified by the entry's id, so the
    /// same song twice in one running order edits as two separate things.
    private struct EditTarget: Identifiable {
        let id: UUID
        let track: Track
        let edit: EditPoints
    }

    @State private var draft: Event
    @ObservedObject private var store: LibraryStore
    let onCommit: (Event) -> Void

    @Environment(\.dismiss) private var dismiss
    @State private var isAddingSongs = false
    @State private var editTarget: EditTarget?

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
            TrackPickerView(tracks: store.tracks) { track in
                draft.items.append(store.newItem(for: track))
            }
        }
        .sheet(item: $editTarget) { target in
            EditPointsView(track: target.track, edit: target.edit) { edited in
                guard let index = draft.items.firstIndex(where: { $0.id == target.id }) else { return }
                draft.items[index].edit = edited
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
            ForEach(Array(draft.items.enumerated()), id: \.element.id) { index, item in
                row(index: index, item: item)
                    .listRowBackground(Theme.panel)
            }
            .onDelete { draft.items.remove(atOffsets: $0) }
            .onMove { draft.items.move(fromOffsets: $0, toOffset: $1) }
        }
        .listStyle(.plain)
        .scrollContentBackground(.hidden)
        // Always in edit mode, so the running order can be dragged without
        // hunting for an Edit button first.
        .environment(\.editMode, .constant(.active))
    }

    @ViewBuilder
    private func row(index: Int, item: SetlistItem) -> some View {
        if let track = store.track(id: item.trackID) {
            Button {
                editTarget = EditTarget(id: item.id, track: track, edit: item.edit)
            } label: {
                HStack(spacing: 10) {
                    Text("\(index + 1)")
                        .font(.system(size: 11, weight: .bold, design: .rounded))
                        .monospacedDigit()
                        .foregroundColor(Theme.amber)
                        .frame(width: 22, alignment: .trailing)

                    VStack(alignment: .leading, spacing: 3) {
                        Text(track.title)
                            .font(.system(size: 13, weight: .semibold, design: .rounded))
                            .foregroundColor(Theme.labelStrong)
                            .lineLimit(1)
                        Text(summary(for: item, track: track))
                            .font(.system(size: 10, weight: .medium, design: .rounded))
                            .monospacedDigit()
                            .foregroundColor(Theme.label)
                            .lineLimit(1)
                    }

                    Spacer()

                    Image(systemName: "slider.horizontal.3")
                        .font(.system(size: 12, weight: .semibold))
                        .foregroundColor(
                            item.edit.isTrimmed(of: track.duration) || item.edit.hasFades
                                ? Theme.amber
                                : Theme.label.opacity(0.5)
                        )
                }
                .contentShape(Rectangle())
            }
            .buttonStyle(.plain)
        } else {
            Text("Missing track")
                .font(.system(size: 13, weight: .medium, design: .rounded))
                .foregroundColor(Theme.red)
        }
    }

    private var runningTime: String {
        let total = draft.items.reduce(0) { $0 + $1.edit.playingLength }
        return "\(draft.items.count) songs  -  \(TimeFormat.clock(total))"
    }

    private func summary(for item: SetlistItem, track: Track) -> String {
        var parts = [TimeFormat.clock(item.edit.playingLength)]
        if item.edit.isTrimmed(of: track.duration) {
            parts.append("\(TimeFormat.clock(item.edit.startPoint))-\(TimeFormat.clock(item.edit.endPoint))")
        }
        if item.edit.hasFades {
            parts.append("in \(TimeFormat.seconds(item.edit.fadeIn)) / out \(TimeFormat.seconds(item.edit.fadeOut))")
        }
        return parts.joined(separator: "  -  ")
    }
}
