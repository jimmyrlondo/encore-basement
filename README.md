# Encore: Basement

A free, open-source DAW for bands and home studios, from [Encore](https://encoremusic.studio).
Record, edit and mix on your own computer, with the sound of Encore's plugins
built for it, and (soon) send finished songs straight to your Encore account.

Basement is built on [Tracktion Engine](https://github.com/Tracktion/tracktion_engine)
and [JUCE](https://juce.com).

## Download

Grab the latest `Encore-Basement-windows.zip` from
[Releases](https://github.com/jimmyrlondo/encore-basement/releases), unzip it
anywhere and run `Encore Basement.exe`. Nothing to install. It's an early
build and not code-signed yet, so Windows SmartScreen will warn the first time:
click **More info**, then **Run anyway**.

## What it does today (0.1)

- **Record** any number of tracks at once from any audio interface. Every input
  is its own input (a mic on 1 and a guitar on 2 go to separate tracks).
- **Import** WAV, AIFF, FLAC, MP3 and OGG files as tracks (drag them in). Stems
  line up at the top of the song.
- **Arrange**: drag clips along their tracks, zoom (Ctrl + wheel), scroll
  (wheel, Shift + wheel), click the ruler or a lane to move the playhead.
- **Mix**: volume, pan, mute and solo per track, plus VST3 effects on any track
  (Encore: Vox, Verb, Equalize and Master included once they're installed).
- **Export** the whole mix to a 24-bit WAV.
- Songs save as one folder each (the song file, its recordings, copies of
  imported audio), so a song can be backed up or moved in one piece.

Keys: Space play/stop, R record, Home back to the start, Ctrl+S save,
Ctrl+N new song, Ctrl+O open, Ctrl+T add track, Ctrl+I import, Ctrl+Z / Ctrl+Y
undo / redo, Delete removes the selected clip or track.

## Coming next

- A proper mixer view with meters, sends and a master bus
- Splitting, trimming and fading clips; loop recording and punch in
- Encore's plugins built in, and mastering on export
- Sign in with Encore and send a song to your catalog as a draft
- MIDI and instruments

## Build (Windows)

Needs Visual Studio 2022 (or its Build Tools) with the C++ workload, and CMake.

```
git clone https://github.com/jimmyrlondo/encore-basement.git
cd encore-basement
git submodule update --init --depth 1 external/JUCE external/tracktion_engine
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

(Don't use `--recurse-submodules`: Tracktion Engine has its own copy of JUCE as
a submodule with an SSH address; Basement uses `external/JUCE`, pinned to the
same commit, instead.)

The app ends up in `build/Basement_artefacts/Release/Encore Basement.exe`.
`"Encore Basement.exe" --selftest <folder>` checks import, mixing, export and
reopening without opening a window (exit code 0 = all good, details in
`<folder>/selftest.log`).
macOS and Linux should build the same way (JUCE and Tracktion support both);
they haven't been tried yet.

## License

Basement is free software under the GNU Affero General Public License v3 (see
`LICENSE`). It uses JUCE under the AGPLv3 and Tracktion Engine under the GPLv3.
The arrange view in `Source/edit/` started from Tracktion Engine's example
code (GPLv3) and is marked where it was changed.

Copyright (C) 2026 Encore Studio. The Encore name and logo belong to Encore Studio; forks are welcome
under the license but should use their own name.
