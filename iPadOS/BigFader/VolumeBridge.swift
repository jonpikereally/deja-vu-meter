import MediaPlayer
import SwiftUI
import UIKit

/// The one channel iPadOS gives a third-party app for *writing* the system
/// output volume.
///
/// There is no public "set the volume" call. `AVAudioSession.outputVolume` is
/// read-only, and no widget or App Intent can touch it either. The only
/// sanctioned control is the `UISlider` that `MPVolumeView` builds inside
/// itself, so the whole app is really a large custom fader driving that slider.
///
/// The slider only works while its `MPVolumeView` is in a real window at a real
/// on-screen position -- an `isHidden` or off-screen view is inert, which is why
/// the bridge view below is parked in the layout at low alpha rather than being
/// hidden outright.
final class VolumeSliderBox {

    weak var slider: UISlider?

    func write(_ value: Float) {
        guard let slider else { return }
        slider.value = min(max(value, 0), 1)
        // MPVolumeView listens for the control event, not for the property
        // write, so setting `value` alone moves the knob without moving audio.
        slider.sendActions(for: .valueChanged)
    }
}

/// Hosts the `MPVolumeView` whose slider the fader drives.
///
/// Keep this in the view hierarchy, on screen, non-hidden and non-zero-sized.
/// Everything about it is deliberate; shrinking it to nothing or hiding it is
/// exactly the change that silently breaks volume writes.
struct VolumeBridgeView: UIViewRepresentable {

    let box: VolumeSliderBox

    func makeUIView(context: Context) -> MPVolumeView {
        let view = MPVolumeView()
        view.alpha = 0.02
        view.isUserInteractionEnabled = false
        // The slider subview is built during layout, so it is not there yet.
        DispatchQueue.main.async { self.capture(from: view) }
        return view
    }

    func updateUIView(_ view: MPVolumeView, context: Context) {
        if box.slider == nil { capture(from: view) }
    }

    private func capture(from view: MPVolumeView) {
        // Search the whole subtree rather than the immediate subviews: the
        // slider has always been a direct child so far, but nothing documents
        // that, and a nil slider fails silently as "the fader does nothing".
        box.slider = Self.findSlider(in: view)
    }

    private static func findSlider(in view: UIView) -> UISlider? {
        if let slider = view as? UISlider { return slider }
        for subview in view.subviews {
            if let slider = findSlider(in: subview) { return slider }
        }
        return nil
    }
}
