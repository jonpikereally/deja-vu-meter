# Big Fader

A large vertical output fader for iPad, sized to sit in Split View next to
Spotify. It moves the iPad's **system output volume** — the same level the
hardware buttons and Control Center move.

```
+---------------+---------------+
|               |          75   |
|    Spotify    |         %     |
|               |    -2.5 dB    |
|               |     [ | ]     |   <- fader
|               |     | | |     |
|               |  MUTE   DIM   |
+---------------+---------------+
```

## Why this is an app and not a widget

It cannot be a widget. There is no public API anywhere on iPadOS that sets the
system volume from outside a foreground app:

- **Home Screen widgets** have been interactive since iOS 17, but only through
  `Button` and `Toggle` — there is no slider control, and no App Intent that
  sets volume.
- **Control Center controls** (`ControlWidget`, iOS 18) are likewise button or
  toggle only. Apple's own Control Center volume slider uses private API.
- `AVAudioSession.outputVolume` is **read-only**.

The one sanctioned control is the `UISlider` that `MPVolumeView` builds inside
itself. So the app holds an `MPVolumeView` in its view hierarchy and drives that
slider from a large custom fader, reading the level back through KVO so the cap
still tracks the hardware buttons. That is what `VolumeBridge.swift` is doing,
and it is the reason that view must stay on screen and non-hidden — hiding it or
sizing it to zero makes it inert and volume writes silently stop working.

If you only need a few fixed levels rather than a fader, the Shortcuts app has a
**Set Volume** action, and a Shortcuts widget will run it straight from the Home
Screen with no code at all. That gets you preset steps, not continuous control.

## Building it

Requires a Mac and Xcode 16 or newer. Targets **iOS 16.0**, which covers every
iPad in the inventory including the 9.7 (that one caps at iPadOS 16).

1. Open `iPadOS/BigFader.xcodeproj`.
2. Select the **BigFader** target, then **Signing & Capabilities**, and set
   **Team** to your Apple ID. The bundle identifier ships as
   `com.jonpike.BigFader`; change it if Xcode reports it as taken.
3. Plug in the iPad, pick it as the run destination, and press Run.

A free Apple ID signs the app for **7 days**, after which it stops launching and
has to be re-run from Xcode. A paid Apple Developer account signs it for a year.

If the project will not open, it can be regenerated instead — the sources are
the real deliverable and the project file is disposable:

```bash
brew install xcodegen
cd iPadOS && xcodegen generate
```

Or build it by hand in about a minute: **File > New > Project > iOS > App**,
SwiftUI interface, then delete the generated `ContentView.swift` and
`*App.swift` and drag the `iPadOS/BigFader` folder in.

## Using it

Open Spotify, swipe up the Dock, and drag Big Fader out to the side to make a
Split View pair. iPadOS remembers the pairing, so afterwards it comes back
together from the App Switcher.

- **Drag anywhere on the fader.** The grab is relative, not absolute — touching
  the track does not jump the output to wherever your finger landed.
- **Slide sideways while dragging** for fine resolution. At roughly 90 pt out
  the fader moves at half speed, tapering to about 1/12 speed further out.
- **MUTE** latches to silence and restores the previous level.
- **DIM** drops roughly 12 dB for talking over the music and restores on the
  second press. Either mode clears as soon as you move the fader or the hardware
  buttons.
- The **AirPlay button** under the readout switches output device.

The dB figure is `20*log10` of the system volume scalar. iPadOS does not expose
a calibrated output level, so read it as a relative number for judging moves by
ear, not as a measurement.

## Known limits

- **Spotify Connect.** If Spotify is streaming to another device — a speaker, a
  Chromecast, the desktop app — the audio is not coming out of the iPad and the
  system volume has nothing to do with it. Controlling that would need the
  Spotify Web API and an OAuth login, which is a different app.
- **The system volume HUD still appears** when you use the hardware buttons.
  Suppressing it needs private API.
- **Foreground only.** It cannot change volume from the background, and no
  background mode grants that.
- **No haptics.** iPads have no Taptic Engine, so the fader has no detent feel.

## Licence

Covered by the repository `LICENSE` (AGPLv3). This app links only Apple
frameworks — no JUCE — so the JUCE commercial-licence question does not apply
to it.
