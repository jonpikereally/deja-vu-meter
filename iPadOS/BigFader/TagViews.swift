import SwiftUI

/// One tag, as a pill. Selected pills read as lit rather than merely outlined,
/// so an active filter is obvious across a dim room.
struct TagChip: View {

    let text: String
    var isSelected = false
    var showsRemove = false
    var action: (() -> Void)?

    var body: some View {
        let content = HStack(spacing: 4) {
            Text(text)
                .font(.system(size: 11, weight: .semibold, design: .rounded))
                .lineLimit(1)
            if showsRemove {
                Image(systemName: "xmark")
                    .font(.system(size: 8, weight: .bold))
            }
        }
        .foregroundColor(isSelected ? .black : Theme.labelStrong)
        .padding(.horizontal, 10)
        .padding(.vertical, 6)
        .background(
            Capsule().fill(isSelected ? Theme.amber : Theme.trackWell)
        )

        if let action {
            Button(action: action) { content }
                .buttonStyle(.plain)
        } else {
            content
        }
    }
}

/// The filter row above a track list. Hidden entirely when nothing is tagged,
/// rather than sitting there as an empty strip.
struct TagFilterBar: View {

    let tags: [String]
    @Binding var selected: Set<String>

    var body: some View {
        if !tags.isEmpty {
            ScrollView(.horizontal, showsIndicators: false) {
                HStack(spacing: 6) {
                    if !selected.isEmpty {
                        TagChip(text: "Clear", isSelected: false) { selected.removeAll() }
                    }
                    ForEach(tags, id: \.self) { tag in
                        TagChip(text: tag, isSelected: selected.contains(tag)) {
                            if selected.contains(tag) {
                                selected.remove(tag)
                            } else {
                                selected.insert(tag)
                            }
                        }
                    }
                }
                .padding(.horizontal, 1)
            }
        }
    }
}
