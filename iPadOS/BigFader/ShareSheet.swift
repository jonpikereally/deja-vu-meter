import SwiftUI
import UIKit

/// A file on its way to the share sheet.
///
/// Identifiable so it can drive `.sheet(item:)`: the export is written once,
/// when the button is pressed, rather than on every pass through a view body.
struct ExportFile: Identifiable {
    let id = UUID()
    let url: URL
}

struct ShareSheet: UIViewControllerRepresentable {

    let url: URL

    func makeUIViewController(context: Context) -> UIActivityViewController {
        UIActivityViewController(activityItems: [url], applicationActivities: nil)
    }

    func updateUIViewController(_ controller: UIActivityViewController, context: Context) {}
}
