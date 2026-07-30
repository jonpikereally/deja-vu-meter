# Deja VU

*(working name — see IP notes)*

An Audio Unit (also VST3 / Standalone) metering plugin for Logic Pro.

It measures the incoming level — **VU** (300 ms RMS ballistics) or **Peak**,
switchable — and can **record that level envelope locked to the host timeline**.
Once recorded, it plays the take back as a **"Recorded" ghost meter** next to the
**"Live"** meter, so you get a direct A/B of a previous pass against what's coming
in now, lined up to the same bars.

The recorded envelope is saved inside the plugin state, so it travels with your
Logic project.

## How it works in Logic

1. Insert **Deja VU** on any track or bus (it passes audio through untouched).
2. Click **ARM RECORD**, then play the section. The take is captured at 100
   samples/sec, indexed by the timeline, up to 60 minutes.
3. Un-arm, rewind, and play again — the left **RECORDED** meter replays your
   captured take (amber ghost) while the right **LIVE** meter shows the new input.
4. **CLEAR** wipes the take. **VU / Peak** switches the metering style.

## Building

Requires the Xcode **Command Line Tools**, CMake, and git. (Full Xcode is *not*
required — this builds and passes `auval` under Command Line Tools alone.)

```bash
cmake -B build
cmake --build build --parallel
```

The first configure downloads JUCE automatically. With `COPY_PLUGIN_AFTER_BUILD`
on, the built AU is installed to `~/Library/Audio/Plug-Ins/Components/` and the
VST3 to `~/Library/Audio/Plug-Ins/VST3/`. Restart Logic (or rescan) to see it.

### Validate the AU

```bash
auval -v aufx Djvu Jpke
```

## Notes / roadmap

- Metering is the mono sum of channels (one value per meter). Per-channel stereo
  metering is a natural next step.
- The recorded metric follows the current VU/Peak mode at record time.
- Timeline indexing uses the host's sample position, so the take re-aligns to the
  exact same bars regardless of tempo playback.
