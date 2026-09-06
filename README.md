# Deja VU Meter

**Audio metering plugins that remember.**

Five free, open-source metering plugins for macOS (Audio Unit, VST3, and
Standalone). They do something ordinary meters don't: they **record what the
meter did, locked to your DAW's timeline**, then play that back as an amber
"ghost" against the live signal — so you can A/B a previous take against what
you're hearing now, lined up to the exact same bars.

| Plugin | What it meters |
|---|---|
| **Deja VU Meter** | Stereo VU (300 ms RMS) or Peak, with bar and analog-VU faces |
| **Deja VU LUFS** | Loudness — Momentary / Short-term / Integrated — plus True Peak |
| **Deja VU Spectrum** | FFT spectrum (31 log bands), curve + band bars, delta view |
| **Deja VU Width** | Stereo width, phase correlation, balance, goniometer |
| **Deja VU Suite** | Meter + LUFS + Spectrum + Width in one, with selectable panels |

Every plugin passes audio through untouched — they only listen.

## The idea

1. Insert a Deja VU plugin on a track or bus.
2. Hit **ARM** (or **AUTO** to capture every playback pass) and play a section.
3. Rewind and play again — the **amber ghost** replays the recorded take against
   the live signal, bar-aligned.

Takes are named, kept 5 deep, selectable, renameable, and saved inside the
project, so they travel with your session.

Other things they do: tag peak moments with their bar/beat position, monitor
peaks continuously, show live-minus-recorded deltas, and (in the Suite) show any
combination of meters at once.

## Build it yourself

This is source code, not a download — you build it on your own Mac. It takes one
command and a few minutes.

### What you need

- **macOS 11 or newer**
- **Xcode Command Line Tools** — full Xcode is *not* required:
  ```bash
  xcode-select --install
  ```
- **CMake** and **git**:
  ```bash
  brew install cmake git
  ```
  (If you don't have Homebrew, get it at https://brew.sh)

### Build

```bash
git clone https://github.com/jonpikereally/deja-vu-meter.git
cd deja-vu-meter
cmake -B build
cmake --build build --parallel
```

The first configure downloads JUCE automatically (a few minutes). When it
finishes, the plugins are **installed for you** into:

- `~/Library/Audio/Plug-Ins/Components/` (Audio Units)
- `~/Library/Audio/Plug-Ins/VST3/` (VST3)

Quit and reopen Logic (or your DAW) so it rescans. They appear under
**Audio FX → Jon Pike**.

### Building with Claude Code

If you'd rather not touch a terminal: open this folder with
[Claude Code](https://claude.com/claude-code) and say **"build and install these
plugins."** The repo includes a `CLAUDE.md` that tells Claude how the project is
laid out, how to build it, and how to verify the result.

### Optional: check the Audio Units are valid

```bash
auval -v aufx Djvu Jpke   # Deja VU Meter
auval -v aufx Dvlu Jpke   # Deja VU LUFS
auval -v aufx Dvsp Jpke   # Deja VU Spectrum
auval -v aufx Dvwi Jpke   # Deja VU Width
auval -v aufx Dvsu Jpke   # Deja VU Suite
```

### Universal (Intel + Apple Silicon) build

The default build targets your own Mac. For a binary that runs on both:

```bash
cmake -B build-dist -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build-dist --parallel
```

## Why you build it instead of downloading it

Two reasons, and they're both honest ones:

1. **Licensing.** These plugins use [JUCE](https://juce.com), whose modules are
   dual-licensed AGPLv3 / commercial. Releasing this freely under the AGPLv3
   keeps everything above board — and the AGPLv3 is why the source is here for
   you to read, change, and share.
2. **Code signing.** Distributing ready-made macOS plugins without warnings
   requires a paid Apple Developer ID and notarization. Building on your own
   machine sidesteps that entirely — no "unidentified developer" prompts,
   because you compiled it.

## License

**GNU Affero General Public License v3.0** — see [LICENSE](LICENSE).

You are free to use, study, modify, and share this. If you distribute a modified
version, it must also be AGPLv3 and you must make your source available.

This project uses the JUCE framework under its AGPLv3 option. **If you want to
sell a plugin built from this code, you need a commercial JUCE licence** — the
AGPLv3 option does not cover closed-source commercial distribution.

## Also in this repo

[`iPadOS/`](iPadOS/) holds **Big Fader**, an iPad app you can park in Split View
next to Spotify: two decks of local audio with a crossfader, a track library
imported from Files/iCloud/Dropbox/Drive, event setlists whose entries each carry
their own fades and edit points, and a large fader on the system output volume. Unrelated to the plugins: SwiftUI, no JUCE, built in
Xcode rather than by the CMake build above. See
[iPadOS/README.md](iPadOS/README.md).

## Notes and caveats

- **Deja VU LUFS** uses RBJ shelf/high-pass filters to approximate BS.1770
  K-weighting. Very close, but not a certified-exact loudness meter — don't
  deliver a broadcast master on it without checking against a certified tool.
- Takes are capped at 5 per plugin, peak lists at 25 entries.
- Recorded takes are stored in the plugin state, so very long takes make for
  larger project files.

---

Built by Jon Pike with [Claude Code](https://claude.com/claude-code).
