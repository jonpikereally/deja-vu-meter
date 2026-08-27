# CLAUDE.md

Guidance for Claude Code working in this repo. `README.md` is the user-facing
reference; this file is what's worth knowing before touching anything.

## What this is

**Deja VU** — six macOS audio metering plugins (AU / VST3 / Standalone) built
with [JUCE](https://juce.com) and CMake. They pass audio through untouched and
record what the meter did, locked to the host timeline, so a previous take can be
replayed as an amber "ghost" against the live signal.

## Building

```bash
cmake -B build            # first run downloads JUCE (a few minutes)
cmake --build build --parallel
```

- **Full Xcode is not required** — this builds and passes `auval` with only the
  Xcode Command Line Tools. Don't tell users to install Xcode.
- `COPY_PLUGIN_AFTER_BUILD` installs to `~/Library/Audio/Plug-Ins/` on build.
- Build one target: `cmake --build build --target DejaVULUFS_All`
- Universal build goes in a **separate** directory, never `build/`:
  `cmake -B build-dist -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"`

## Verifying

There is no unit-test suite. Verification is: **it compiles with zero warnings,
and every Audio Unit passes `auval`.** Always check both.

```bash
auval -v aufx Djvu Jpke   # Meter
auval -v aufx Dvlu Jpke   # LUFS
auval -v aufx Dvsp Jpke   # Spectrum
auval -v aufx Dvwi Jpke   # Width
auval -v aufx Dvpi Jpke   # Pitch
auval -v aufx Dvsu Jpke   # Suite
```

`auval` validates the **installed** component, not the one you just built — if a
build fails, `auval` will happily re-validate the previous binary and look like a
pass. Confirm the installed binary actually changed before trusting it.

## Layout

| Target | Sources | AU code | Product |
|---|---|---|---|
| `TimelineVU` | `Source/*.cpp` | `Djvu` | Deja VU Meter |
| `DejaVUSpectrum` | `Source/Spectrum/` | `Dvsp` | Deja VU Spectrum |
| `DejaVULUFS` | `Source/LUFS/` | `Dvlu` | Deja VU LUFS |
| `DejaVUPitch` | `Source/Pitch/` | `Dvpi` | Deja VU Pitch |
| `DejaVUWidth` | `Source/Width/` | `Dvwi` | Deja VU Width |
| `DejaVUSuite` | `Source/Suite/` | `Dvsu` | Deja VU Suite |

The `TimelineVU` target name is historical — the product is "Deja VU Meter".
Renaming the target would break nothing but churns the build; leave it.

## Hard-won rules

These each cost a debugging session.

1. **No multibyte characters in user-facing string literals.** `juce::String`
   decodes `const char*` as Latin-1, not UTF-8, so an em-dash, `…`, `−∞`, or a
   `▾` glyph renders as mojibake ("â ¾"). Use plain ASCII. Where an icon is
   wanted, *draw* it — see `Source/MenuButton.h`, which paints a real disclosure
   triangle rather than using a character.

2. **Deja VU Suite duplicates the four DSP engines on purpose.** The standalone
   plugins can't be linked into it because each defines its own
   `createPluginFilter()`. When you change a metering engine, change it in *both*
   places.

3. **A feature added to a standalone plugin must also be added to its Suite
   panel, and vice versa.** This is the project owner's standing rule. The Suite
   is meant to behave like the individual plugins, not lag behind them.

4. **Bumping a plugin's state format breaks saved takes.** `setStateInformation`
   only accepts an exact version match and silently drops older blobs — the
   parameters survive, the recorded takes don't. That's an acceptable trade
   during development, but say so plainly rather than letting it surprise anyone.

5. **The audio thread must not allocate.** Envelope and frame buffers are sized
   once in the constructor / `prepareToPlay` and only written in place. Peaks
   reach the GUI through a lock-free `AbstractFifo`. Keep it that way.

6. **`isBusesLayoutSupported` must keep input and output matched** (mono or
   stereo). `auval` tests odd layouts and will fail the plugin if this is loose.

## Architecture

Every plugin follows the same shape:

- The processor meters the signal and writes one value (or one frame) per
  **timeline slot**, indexed by the host's *sample position* — not elapsed time —
  so takes re-align to the same bars regardless of how playback started.
- A finished take moves into a 5-deep history; the active take is copied into a
  playback buffer that the audio thread reads to produce the "ghost" value.
- The editor polls atomics on a 30 Hz timer. Nothing blocks the audio thread.
- Everything — parameters, takes, peaks — is serialised in
  `getStateInformation` so it saves with the host project.

Shared UI: `Source/MenuButton.h` (the DEL button's drawn disclosure triangle).

## Style

- Modern C++17, JUCE idioms, 4-space indent, `juce::` qualified.
- Comments explain **why**, not what — match the existing ones, which record the
  reasoning or the bug behind a decision.
- Keep the audio thread allocation-free and lock-free.

## Commits

Subject lines are plain imperative sentences — no `feat:`/`fix:` prefixes. The
body explains what was wrong, why the fix is shaped this way, and what was
verified. Look at `git log` and match it. Milestones get a `vX.Y.Z` tag and a
matching `project(... VERSION ...)` bump in `CMakeLists.txt`.

## Licensing

**AGPLv3** (see `LICENSE`), because JUCE's modules are dual-licensed
AGPLv3/commercial and this project takes the AGPLv3 option. Anything derived from
this must stay AGPLv3 with source available. Selling a plugin built from this
code requires a **commercial JUCE licence** — don't advise otherwise.
