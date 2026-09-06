# Big Fader

A two-deck player and output fader for iPad, sized to sit in Split View next to
Spotify.

- **MASTER** — one big fader on the iPad's **system output volume**, the same
  level the hardware buttons and Control Center move.
- **MIXER** — two decks with a crossfader, each with its own volume fader.
- **LIBRARY** — the tracks imported into the app, with tags.
- **EVENTS** — running orders, with per-song fades and edit points.

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

## Tags

Tap a library row to name a track and tag it — "first dance", "chill", "closer",
whatever you sort by. The filter bar above the library and above the setlist
picker narrows by tag, which is where tags earn their keep: building a running
order out of a few hundred tracks.

Filters are **additive**: a track has to carry every selected tag, so "first
dance" plus "slow" is a smaller list, not a bigger one. Tags are compared
case-insensitively but stored as typed, so "Chill" and "chill" are one tag.

## Events and setlists

An event is a name, a date and an ordered list of songs, each with its own edit
points. Drag to reorder, swipe to remove, tap a row to cut it. The header totals
the running time from the entries' *trimmed* lengths rather than their full ones.

Marking an event **ACTIVE** puts its running order at the top of the deck
loader, so the next song of the night is one tap away instead of buried in an
alphabetical library.

## Per-song edit points

**Fades and edit points belong to the setlist entry, not to the track.** The
same song can open a ceremony trimmed to its first verse and close a dance floor
running full length, in two different events, with no conflict — and twice in
one running order, cut differently each time.

Tap any row in an event's setlist to open it against its waveform. Drag the
handles to trim; the sliders below do the same thing to a tenth of a second.
Four things to set:

| | |
|---|---|
| **START AT** | where playback begins |
| **END AT** | where it stops — the deck counts down to this, not to the end of the file |
| **FADE IN** | ramp up, measured inward from the start point |
| **FADE OUT** | ramp down, measured inward from the end point |

The four are clamped against each other, so start can never pass end and two
fades can never overlap (each is capped at half the trimmed length). Edits
commit live: a deck already playing that entry picks them up while you drag.

The waveform shows the trimmed region lit and the rest dimmed, with the gain
envelope drawn across the top so a fade reads as a shape applied to the audio
rather than as part of it.

A track loaded straight from the library rather than from a setlist plays whole.
Cutting it means putting it in an event first.

## Live mode

The **LIVE** toggle sits between the decks and the crossfader. With it on, a
deck that finishes — or that you eject — reloads itself with the next song in
the active event's running order.

"Next" means: start after the furthest point in the running order either deck
has reached, then take the first entry that has not been played and whose track
is not on the other deck. Two decks working down one list, without ever both
holding the same song. It does not wrap; the end of the setlist is the end.

It **loads and cues, it does not start playing.** Bringing the next song in is
the crossfader's job, and a deck that started itself mid-set would be worse than
useless. Turning live mode on with empty decks fills both, so the top of the
running order is under your hands straight away.

Live mode needs an active event. Without one the toggle says so rather than
doing nothing quietly.

## Sharing sets

**EXPORT** in the EVENTS tab writes a `.bigfader.json` file — every song, its
tags, every running order and every edit point. The share icon inside an event
exports just that event and the songs it uses. **IMPORT** reads one back.

The file carries **no audio**. A four-hour set is gigabytes. Instead every track
records the SHA-256 of its audio, which is the same on every device, so a set
opened somewhere that already holds the files matches them up. Somewhere that
does not still gets the whole running order: the songs come in marked *add the
audio*, and importing the file later relinks it by hash without disturbing the
setlist already pointing at it.

An import never deletes anything. Songs already here are matched and left alone;
events are replaced by id or appended.

This is the shared half of a hybrid: a browser app on a laptop is a better place
to build a running order and set edit points against a big waveform, and the
iPad is a better place to play it. [SET-FORMAT.md](SET-FORMAT.md) is the full
spec, written so something other than this app can be built against it.

## Waveforms

Overviews are generated on import, in the same background pass as the copy,
while the file is still warm. One byte per bin, 1200 bins, so a whole overview
is about a kilobyte on disk next to the audio. Anything imported before
waveforms existed is backfilled the first time it is shown.

They are normalised to each track's own loudest moment: this is a picture of the
*shape* of a song, for finding where the intro ends and the outro starts, not a
level meter. A quiet recording still fills the frame.

Drawing is per screen column rather than per bin -- at these sizes there are
more bins than pixels, so each column takes the loudest bin it covers, which
keeps a transient visible instead of letting it fall between samples.

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

## Getting it onto the iPad

There is no way around a Mac: an iPad cannot build an iPad app. What you need is
Xcode, a cable, and about fifteen minutes the first time.

**Before you start.** Xcode 16 needs macOS Sequoia 15.x or newer, so check what
the Mac is on before downloading 8 GB. Targets **iOS 16.0**, so any of the iPads
will run it.

1. **Install Xcode** from the Mac App Store. It is a large download and the
   first launch installs more components on top.
2. **Open `iPadOS/BigFader.xcodeproj`.**
3. **Signing.** Select the project in the sidebar, then the **BigFader** target,
   then **Signing & Capabilities**. Tick *Automatically manage signing* and set
   **Team** to your Apple ID — add it under Xcode > Settings > Accounts if it is
   not listed. If Xcode says the bundle identifier is taken, change
   `com.jonpike.BigFader` to anything unique.
4. **Optional, same tab:** *+ Capability > Background Modes > Audio*, if you
   want the decks to keep playing when you switch away from the app. Off by
   default, since in Split View both apps are foreground anyway.
5. **Plug the iPad in** with a cable and unlock it. Tap **Trust** on the "Trust
   this computer?" prompt.
6. **Turn on Developer Mode on the iPad.** This is the step everyone misses:
   *Settings > Privacy & Security > Developer Mode*, switch on, restart the iPad
   when it asks. Required on iPadOS 16 and later; the entry only appears once a
   Mac running Xcode has been connected at least once.
7. **Pick the iPad** from the run-destination menu at the top of the Xcode
   window, and press **Run** (the play button, or Cmd-R).
8. **Trust the certificate.** The first run installs the app but refuses to
   launch it. On the iPad: *Settings > General > VPN & Device Management*, tap
   your Apple ID under *Developer App*, then **Trust**. Press Run again.

The app can then be launched from the Home Screen with the cable unplugged. To
put it beside Spotify: open Spotify, swipe up the Dock, and drag Big Fader out
to the side.

**A free Apple ID signs the app for 7 days**, after which it stops launching and
has to be re-run from Xcode. A paid Apple Developer Program membership signs it
for a year.

### If the first build fails

Expect this. None of this code has ever been compiled — there is no Mac in the
environment it was written in — so the first build is where any mistake in it
shows up. Xcode lists the errors in the Issue navigator (Cmd-5); the file, line
and message from the first few are enough to fix them.

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

Drag the waveform to scrub -- absolute, unlike the faders: you are pointing at a
place in the song, not nudging a live level. The playhead is the white line, the
trimmed region is lit, and the countdown runs to the entry's end point rather
than the end of the file.

**LOAD** opens the library for that deck; **eject** clears it, or in live mode
swaps in the next song. The active event's setlist comes
first, each entry cut the way that event wants it; below it, every track in the
library, playable whole. Transport is cue-to-start and
play/pause; drag the scrub bar to move within a track.

Deck gain is `deck fader x crossfader`, so the two multiply rather than one
overriding the other — pulling a deck fader down keeps it down wherever the
crossfader sits. The crossfade curve is equal-power (`cos`/`sin`), so the pair
stays at roughly constant loudness across the throw instead of dipping about
3 dB in the middle the way a linear blend does.

### Master

The master fader can be **turned off entirely** in Settings, for when the level
is set somewhere else -- another volume app, an interface, the desk. The MASTER
tab and the master strip in the mixer both go away; the decks, the crossfader
and the setlists are untouched. Settings still shows what the system output is
sitting at, so hiding the fader can never leave the rig quiet with no visible
way to find out why.

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
- **A shared set does not bring its audio.** That is the design, not an
  omission, but it means moving a library to a new iPad is still a file copy.
- **Track titles come from filenames**, not from tags. Files named
  "Artist - Title" read correctly; ones named "track07" do not. Tap a library
  row to rename.
- **The system volume HUD still appears** when you use the hardware buttons.
  Suppressing it needs private API.
- **No haptics.** iPads have no Taptic Engine, so the faders have no detent feel.
- **No beat detection, sync, EQ or key lock.** It is two decks, a crossfader
  and per-song edit points, not a DJ controller.

## The browser half

`MobileDJ` at `pikemusicschool.com/mobiledj` is the same app in a browser --
same decks, crossfader, waveforms, tags, setlists and edit points -- and reads
and writes the same set files. A laptop is a better place to build a running
order; the iPad is a better place to play it.

Two differences worth knowing:

- Its master fader rides **its own mix**, not the system output. No web API can
  set the device volume, which is the one thing only this app can do.
- Its fades are **sample-accurate**, scheduled as Web Audio gain ramps, where
  this app writes volume from a 30 Hz timer because `AVAudioPlayerNode` has no
  ramp API.

The set format identifier stays `bigfader.set` in both, whatever either app is
called: it is the interop contract, not a product name.

## Licence

Covered by the repository `LICENSE` (AGPLv3). This app links only Apple
frameworks — no JUCE — so the JUCE commercial-licence question does not apply
to it.
