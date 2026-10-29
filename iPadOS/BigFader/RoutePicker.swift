import AVKit
import SwiftUI
import UIKit

/// The AirPlay / output picker, so the fader can be pointed at the speakers,
/// headphones or an AirPlay target without leaving the app.
///
/// `AVRoutePickerView` rather than `MPVolumeView.showsRouteButton`, which is
/// long deprecated.
struct RoutePicker: UIViewRepresentable {

    func makeUIView(context: Context) -> AVRoutePickerView {
        let view = AVRoutePickerView()
        view.tintColor = UIColor(Theme.label)
        view.activeTintColor = UIColor(Theme.amber)
        view.prioritizesVideoDevices = false
        return view
    }

    func updateUIView(_ view: AVRoutePickerView, context: Context) {}
}
