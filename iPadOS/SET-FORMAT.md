# The Big Fader set format

`*.bigfader.json` — the interchange file shared between the iPad app and
anything else that wants to work with these running orders. This document is
the contract; the Swift types are in `BigFader/SetDocument.swift`.

## What it is for

The iPad app plays; a browser app on a laptop is a better place to build a
running order, tag a library, and set edit points against a big waveform on a
real screen. Both need to describe the same set. This is that description.

**It carries no audio.** A four-hour wedding set is gigabytes; a JSON file that
tried to hold it would be unusable. Audio is matched on the far side by hash —
see *Matching* below — so a set exported from a laptop and opened on an iPad
that already holds the files just works, and one opened on an iPad that does not
still gets the whole running order and waits for the audio.

## Shape

```json
{
  "format": "bigfader.set",
  "version": 1,
  "exportedAt": "2026-09-06T14:22:31Z",
  "exportedBy": "Big Fader iPadOS",
  "tracks": [
    {
      "id": "9C1F...-...",
      "title": "Artist - Title",
      "duration": 231.44,
      "tags": ["first dance", "slow"],
      "audio": {
        "sha256": "e3b0c44298fc1c149afbf4c8996fb924...",
        "extension": "mp3",
        "bytes": 8123456
      }
    }
  ],
  "events": [
    {
      "id": "4B77...-...",
      "name": "Julienne and Paul",
      "date": "2026-09-20T00:00:00Z",
      "items": [
        {
          "id": "1A0E...-...",
          "trackID": "9C1F...-...",
          "startPoint": 0,
          "endPoint": 210.5,
          "fadeIn": 0,
          "fadeOut": 6
        }
      ]
    }
  ]
}
```

### Rules

- **Times are seconds**, as decimals. Not milliseconds, not frames.
- **Dates are ISO 8601** with a timezone.
- **IDs are UUID strings.** They are *device-local* — see *Matching*.
- `format` must be exactly `"bigfader.set"`. Anything else is not this format
  and should be refused rather than guessed at.
- `version` is a single integer. A reader must refuse a `version` higher than it
  knows. Within a version, **unknown keys must be ignored, not rejected** — that
  is what makes adding a field later a non-breaking change.
- `audio` may be absent or partially filled. A track with no `sha256` can still
  be matched by title and duration, less reliably.
- `items` is the running order, and **its array order is the running order**.
  Nothing else encodes position.
- An `item` has its own `id` distinct from `trackID`, because the same song can
  appear twice in one running order **cut differently each time**. Never key a
  setlist entry by its track.
- `startPoint <= endPoint`, both within `0...duration`. `fadeIn` and `fadeOut`
  are lengths in seconds measured inward from those points, and neither may
  exceed half of `endPoint - startPoint`, or two fades would overlap and the
  song would never reach full gain.

### Deliberately not included

Anything that means something only on the device that wrote it: container
filenames, waveform caches, which event is currently active, when a file was
added, deck state, fader positions. A reader that finds itself wanting one of
these should keep it in its own local storage, not here.

## Matching

Track IDs are generated locally, so the same song imported in two places has two
different UUIDs. A reader resolves an incoming track against its own library in
this order:

1. **`audio.sha256`** — the SHA-256 of the audio file's bytes. The same file
   hashes the same everywhere, so this is the only identifier that genuinely
   means the same thing on two devices. Always prefer it.
2. **`id`** — catches a set exported and reimported on the same device.
3. **`title` (case-insensitive) and `duration` within one second** — a guess,
   but a good one, and better than silently making a second copy of a song
   already in the library.

Nothing matched? Keep the track anyway, as an entry with no audio. The running
order is worth having without the files. When the audio is imported later its
hash matches and it links itself up, without disturbing the setlist that was
already pointing at it. The iPad app shows these as *add the audio* in the
library and refuses to load them onto a deck.

## Merging

An import **never deletes anything**.

- Tracks that match are left alone — local tags and titles win, on the grounds
  that whoever is holding the device has the more recent opinion.
- Tracks that do not match are added, with audio if it can be found and without
  if it cannot.
- Events are matched **by `id`**: same id replaces, new id appends.

## Writing a reader

The whole format is four object types and no cleverness. In a browser:

```js
const doc = JSON.parse(text)
if (doc.format !== 'bigfader.set') throw new Error('not a set file')
if (doc.version > 1) throw new Error('written by a newer version')
```

Hashing to match against files the browser already holds:

```js
const bytes = await file.arrayBuffer()
const digest = await crypto.subtle.digest('SHA-256', bytes)
const sha256 = [...new Uint8Array(digest)]
  .map(b => b.toString(16).padStart(2, '0')).join('')
```

Note that `crypto.subtle.digest` wants the whole file in memory, where the iPad
app streams it in 1 MB chunks. For a library of large files a browser reader
should hash incrementally instead, or it will be evicted for memory before it
finishes.

## Versioning

Additive changes — a new optional key — do not bump `version`. Anything that
would make an older reader misread an existing key does. If `version` ever goes
to 2, a version 1 file must still load: the format is meant to outlive both
apps that currently read it.
