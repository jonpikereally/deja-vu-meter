import SwiftUI
import UniformTypeIdentifiers

/// The list of events. One can be made active, which puts its setlist at the
/// top of the deck loader.
struct EventsView: View {

    @ObservedObject var store: LibraryStore
    let onEdit: (Event) -> Void

    @State private var exporting: ExportFile?
    @State private var isImportingSet = false

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
        .fileImporter(
            isPresented: $isImportingSet,
            allowedContentTypes: [.json],
            allowsMultipleSelection: false
        ) { result in
            switch result {
            case .success(let urls): if let url = urls.first { store.importSet(from: url) }
            case .failure(let error): store.lastError = error.localizedDescription
            }
        }
        .sheet(item: $exporting) { file in
            ShareSheet(url: file.url)
        }
    }

    private var header: some View {
        HStack {
            Text("EVENTS")
                .font(.system(size: 11, weight: .bold, design: .rounded))
                .kerning(1)
                .foregroundColor(Theme.label)
            Spacer()

            smallButton("IMPORT", symbol: "square.and.arrow.down") {
                isImportingSet = true
            }

            smallButton("EXPORT", symbol: "square.and.arrow.up") {
                if let url = store.exportedSetURL() {
                    exporting = ExportFile(url: url)
                }
            }
            .disabled(store.events.isEmpty && store.tracks.isEmpty)

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

    private func smallButton(
        _ title: String,
        symbol: String,
        action: @escaping () -> Void
    ) -> some View {
        Button(action: action) {
            Label(title, systemImage: symbol)
                .font(.system(size: 10, weight: .bold, design: .rounded))
                .foregroundColor(Theme.labelStrong)
                .padding(.horizontal, 10)
                .padding(.vertical, 8)
                .background(
                    RoundedRectangle(cornerRadius: 9, style: .continuous)
                        .fill(Theme.panel)
                )
        }
        .buttonStyle(.plain)
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
            Text("An event is a name, a date and a running order picked from the library. IMPORT reads a set shared from another device; the songs relink themselves when you add the audio.")
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
