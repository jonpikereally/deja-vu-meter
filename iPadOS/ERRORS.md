# Big Fader error codes

Every error Big Fader shows ends with a code in brackets, like
`Could not save the library: ... (BF-LIB-02)`. This page explains each one. It is
written so it can be handed to a language model along with the error.

Codes are `BF-AREA-NN`, and they live in
[`BigFader/ErrorCode.swift`](BigFader/ErrorCode.swift):

| Area | What it covers |
|---|---|
| `IMPORT` | Adding audio files to the library |
| `SET` | Importing and exporting sets |
| `LIB` | The library index on disk |
| `AUDIO` | The audio engine |
| `DECK` | Putting a song on a deck |

Library, set and engine errors appear in red under the master fader. A deck
error replaces that deck's title.

---

## BF-IMPORT-01 - Could not import a file

One file from the document picker could not be copied in. The message names the
file and gives iPadOS's reason. Files that worked are still added, and each
failure gets its own line.

- **Usual cause:** a cloud file (Dropbox, Google Drive, iCloud) that could not be
  downloaded, perhaps because of no network or the provider app being signed
  out. Or a file that is not decodable audio: files are checked on import.
- **Try:** open the file once in the Files app so the provider downloads it,
  then add it again. If it is not audio this iPad can decode, convert it to MP3,
  AAC/M4A or WAV.

## BF-IMPORT-02 - Could not open those files

The document picker itself returned an error before any file was read.

- **Try:** add the files again. If a provider is missing from the picker, turn it
  on in **Files > Browse > Edit**.

## BF-SET-01 - Could not open that file

The picker returned an error while choosing a set file.

- **Try:** choose the file again, or open it from the Files app.

## BF-SET-02 - Not a readable set file

The file is not valid JSON, or not shaped like a set. The message includes the
decoder's reason.

- **Usual cause:** a file that is not a set, or one an LLM or a person edited
  into invalid JSON.
- **Try:** check the file against [`SET-FORMAT.md`](SET-FORMAT.md).

## BF-SET-03 - Not a Big Fader set

The JSON is valid but its `format` field is not a Big Fader set's.

- **Try:** check the file has the format identifier given in
  [`SET-FORMAT.md`](SET-FORMAT.md). A MobileDJ backup is not a set.

## BF-SET-04 - Set from a newer version

The set's version is newer than this build reads.

- **Try:** update the app, then import again.

## BF-SET-05 - Could not export

Writing the set file to the temporary folder failed. The message gives
iPadOS's reason.

- **Usual cause:** the iPad is out of storage.
- **Try:** free some space and export again.

## BF-LIB-01 - Could not read the library

The library index could not be read when the app started, so the library may
appear empty. The audio files themselves are not touched.

- **Usual cause:** a damaged index file, for example after the app was killed
  mid-save.
- **Try:** restart the app. Until it reads the library again, avoid adding or
  changing anything: the next save writes the library as it now stands,
  empty, over the index that could not be read. If it repeats, the index
  needs restoring from a backup of the app's Documents folder.

## BF-LIB-02 - Could not save the library

Writing the library index failed, so recent changes may not survive a restart.

- **Usual cause:** the iPad is out of storage.
- **Try:** free some space. The next change saves again.

## BF-AUDIO-01 - Audio would not start

`AVAudioEngine` refused to start. The message gives the system's reason.

- **Usual cause:** another app holding the audio session exclusively, or an
  output route that changed or disappeared, such as Bluetooth or AirPlay
  dropping.
- **Try:** stop audio in the other app, pick the output again, and press play.
  Restarting the app resets the session.

## BF-DECK-01 - Could not open the file

The deck could not open the song's audio file. The error replaces the deck's
title.

- **Usual cause:** the file in the library was removed or damaged.
- **Try:** add the song's file again from the library tab.
