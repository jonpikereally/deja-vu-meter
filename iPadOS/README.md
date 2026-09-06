# Big Fader

A two-deck player and output fader for iPad, sized to sit in Split View next to
Spotify.

- **MASTER** — one big fader on the iPad's **system output volume**, the same
  level the hardware buttons and Control Center move.
- **MIXER** — two decks with a crossfader, each with its own volume fader.
- **LIBRARY** — tracks imported into the app, with per-song edit points.
- **EVENTS** — a name, a date and a running order picked from the library.

The app opens in whichever mode fits the window (MASTER in a narrow Split View
column, MIXER when there is room) until you pick one by hand.

## Where tracks come from

**ADD FILES** in the LIBRARY tab opens the system document picker, which reaches
**local storage, iCloud Drive, Dropbox and Google Drive** — anything that shows
up in the Files app. Dropbox and Google Drive need no login here and no SDK:
both ship a File Provider extension, so having their apps installed is enough.
If either is missing from the picker, turn it on in **Files > Browse > Edit**.

A cloud file may not actually be on the iPad yet, so the import does a
*coordinated* read rather than a plain copy — that is what makes the provider
download it first. Copying a placeholder directly either fails or silently
produces a stub.

**Imported tracks are copied into the app.** Once a file is in the library it no
longer depends on where it came from: it keeps playing with Dropbox uninstalled,
the iPad offline, or the original deleted. The cost is disk — the library counts
its own size under the LIBRARY heading. Files are verified as decodable on
import, so a bad file is rejected then rather than at the moment you need it.

## Events and setlists

An event is a name, a date and an ordered list of songs. Drag to reorder, swipe
to remove, and the header totals the running time from the songs' *trimmed*
lengths rather than their full ones.

Marking an event **ACTIVE** puts its running order at the top of the deck
loader, so the next song of the night is one tap away instead of buried in an
alphabetical library.

Setlists hold references, not copies. The same song can appear twice in one
running order, and editing its fades updates every event that uses it.

## Per-song edit points

Tap any track in the library to set four things:

| | |
|---|---|
| **START AT** | where playback begins |
| **END AT** | where it stops — the deck counts down to this, not to the end of the file |
| **FADE IN** | ramp up, measured inward from the start point |
| **FADE OUT** | ramp down, measured inward from the end point |

The four are clamped against each other, so start can never pass end and two
fades can never overlap (each is capped at half the trimmed length). Edits
commit live: a deck already holding that track picks them up while you drag.

These live on the **track**, not on a setlist entry — trim a song once and it is
trimmed everywhere. Per-event overrides of the same song would be a small
follow-up if you ever want them.

`AVAudioPlayerNode` has no gain-ramp API, so fades are applied by writing volume
from the transport timer at 30 Hz. A one-second fade moves in 3% steps — short
of a sample-accurate ramp, but well below audible zipper noise at DJ fade
lengths.

## What it cannot play

It **cannot** load Spotify or Apple Music tracks. Those are DRM-protected and no
third-party app can decode them; there is no workaround, only licensing. The
Spotify pairing this app is built for is the *master fader* riding Spotify's
output, not the decks playing Spotify's catalogue.

## Why the master fader is an app and not a widget

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
3. Optional, in the same tab: **+ Capability > Background Modes > Audio**, if
   you want the decks to keep playing when you switch away from the app. Left
   off by default, since in Split View both apps are foreground anyway.
4. Plug in the iPad, pick it as the run destination, and press Run.

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

### Faders

- **Drag anywhere on a fader.** The grab is relative, not absolute — touching
  the track does not jump the level to wherever your finger landed.
- **Slide sideways while dragging** for fine resolution. At roughly 90 pt out
  the fader moves at half speed, tapering to about 1/12 speed further out.
- **Double-tap the crossfader** to snap it back to centre.

### Decks

**LOAD** opens the library for that deck, active event's setlist first. Transport is cue-to-start and
play/pause; drag the scrub bar to move within a track.

Deck gain is `deck fader x crossfader`, so the two multiply rather than one
overriding the other — pulling a deck fader down keeps it down wherever the
crossfader sits. The crossfade curve is equal-power (`cos`/`sin`), so the pair
stays at roughly constant loudness across the throw instead of dipping about
3 dB in the middle the way a linear blend does.

### Master

**MUTE** latches to silence and restores the previous level. **DIM** drops
roughly 12 dB for talking over the music and restores on the second press.
Either mode clears as soon as you move the fader or the hardware buttons. The
**AirPlay button** under the readout switches output device.

The dB figure is `20*log10` of the system volume scalar. iPadOS does not expose
a calibrated output level, so read it as a relative number for judging moves by
ear, not as a measurement.

## Known limits

- **Spotify Connect.** If Spotify is streaming to another device — a speaker, a
  Chromecast, the desktop app — the audio is not coming out of the iPad and the
  system volume has nothing to do with it. Controlling that would need the
  Spotify Web API and an OAuth login, which is a different app.
- **Track titles come from filenames**, not from tags. Files named
  "Artist - Title" read correctly; ones named "track07" do not.
- **No waveform display.** Edit points are set on sliders against a clock, not
  against a picture of the audio.
- **The system volume HUD still appears** when you use the hardware buttons.
  Suppressing it needs private API.
- **No haptics.** iPads have no Taptic Engine, so the faders have no detent feel.
- **No beat detection, sync, EQ or key lock.** It is two decks, a crossfader
  and per-song edit points, not a DJ controller.

## Licence

Covered by the repository `LICENSE` (AGPLv3). This app links only Apple
frameworks — no JUCE — so the JUCE commercial-licence question does not apply
to it.
