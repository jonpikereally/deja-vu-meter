import SwiftUI

/// The list of events. One can be made active, which puts its setlist at the
/// top of the deck loader.
struct EventsView: View {

    @ObservedObject var store: LibraryStore
    let onEdit: (Event) -> Void

    var body: some View {
        VStack(spacing: 10) {
            header

            if store.events.isEmpty {
                emptyState
            } else {
                List {
                    ForEach(store.events) { event in
                        Button { onEdit(event) } label: { row(event) }
                            .buttonStyle(.plain)
                            .listRowBackground(Theme.panel)
                            .listRowSeparatorTint(Theme.hairline)
                    }
                    .onDelete(perform: store.deleteEvents)
                }
                .listStyle(.plain)
                .scrollContentBackground(.hidden)
            }
        }
    }

    private var header: some View {
        HStack {
            Text("EVENTS")
                .font(.system(size: 11, weight: .bold, design: .rounded))
                .kerning(1)
                .foregroundColor(Theme.label)
            Spacer()
            Button { onEdit(store.addEvent()) } label: {
                Label("NEW EVENT", systemImage: "plus")
                    .font(.system(size: 11, weight: .bold, design: .rounded))
                    .foregroundColor(.black)
                    .padding(.horizontal, 12)
                    .padding(.vertical, 8)
                    .background(
                        RoundedRectangle(cornerRadius: 9, style: .continuous)
                            .fill(Theme.amber)
                    )
            }
            .buttonStyle(.plain)
        }
    }

    private var emptyState: some View {
        VStack(spacing: 8) {
            Spacer()
            Image(systemName: "calendar")
                .font(.system(size: 34, weight: .light))
                .foregroundColor(Theme.label.opacity(0.4))
            Text("No events yet")
                .font(.system(size: 14, weight: .semibold, design: .rounded))
                .foregroundColor(Theme.labelStrong)
            Text("An event is a name, a date and a running order picked from the library.")
                .font(.system(size: 11, weight: .medium, design: .rounded))
                .foregroundColor(Theme.label)
                .multilineTextAlignment(.center)
                .padding(.horizontal, 24)
            Spacer()
        }
    }

    private func row(_ event: Event) -> some View {
        HStack(spacing: 10) {
            VStack(alignment: .leading, spacing: 3) {
                Text(event.name)
                    .font(.system(size: 13, weight: .semibold, design: .rounded))
                    .foregroundColor(Theme.labelStrong)
                    .lineLimit(1)
                Text(detail(for: event))
                    .font(.system(size: 10, weight: .medium, design: .rounded))
                    .foregroundColor(Theme.label)
            }

            Spacer()

            if store.activeEventID == event.id {
                Text("ACTIVE")
                    .font(.system(size: 9, weight: .bold, design: .rounded))
                    .kerning(1)
                    .foregroundColor(.black)
                    .padding(.horizontal, 8)
                    .padding(.vertical, 4)
                    .background(Capsule().fill(Theme.amber))
            }
        }
        .padding(.vertical, 4)
        .contentShape(Rectangle())
    }

    private func detail(for event: Event) -> String {
        let songs = store.setlist(for: event)
        // Trimmed lengths, not full ones: the running time of a set is what
        // will actually play.
        let total = songs.reduce(0) { $0 + $1.item.edit.playingLength }
        let formatter = DateFormatter()
        formatter.dateStyle = .medium
        formatter.timeStyle = .none
        return "\(formatter.string(from: event.date))  -  \(songs.count) song\(songs.count == 1 ? "" : "s")  -  \(TimeFormat.clock(total))"
    }
}
