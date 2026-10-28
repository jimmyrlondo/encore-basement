# Encore: Basement

A free, open-source DAW for bands and home studios, from [Encore](https://encoremusic.studio).
Record, edit and mix on your own computer, with Encore's own plugins built in,
and (soon) send finished songs straight to your Encore account.

Basement is built on [Tracktion Engine](https://github.com/Tracktion/tracktion_engine)
and [JUCE](https://juce.com).

## Download

From [Releases](https://github.com/jimmyrlondo/encore-basement/releases):

- **Windows**: `Encore-Basement-windows.zip`. Unzip it anywhere and run
  `Encore Basement.exe`; nothing to install. It isn't code-signed yet, so
  SmartScreen warns the first time: click **More info**, then **Run anyway**.
- **Mac** (Apple Silicon and Intel): `Encore-Basement-macos.zip`. Unzip it and
  move `Encore Basement.app` to Applications. It isn't notarized yet, so the
  first time right-click it, choose **Open**, then **Open** again.

## What it does

- **Hub**: Basement opens on a start page. Pick up a recent song or set up a
  new one: start from a template (Blank, Singer-songwriter, Full band, Live
  multitrack, Podcast, Mix stems, or your own), fill in title, artist, album,
  genre, tempo, time signature, key and notes, choose the interface you record
  from, its sample rate and bit depth, how many tracks to start with and which
  input feeds each one.
- **Record** any number of tracks at once. Every input on your interface is
  its own input. Level meters on every track show the input while it's armed
  (with a clip light), a headphones button lets you hear yourself, the take's
  waveform draws as you play, and a metronome with a count-in you can see.
  Loop a section and record over it, or punch in on just that part.
- **Your interface's own driver**: on Windows, Basement uses the interface's
  ASIO driver when it has one (low latency, no Windows audio settings in the
  way) and switches to it by itself the first time.
- **Edit**: snap to the grid (bar, beat, 1/8, 1/16 or off; hold Shift to
  ignore it), trim clips from either edge, drag fades from their top corners
  (overlapping clips crossfade), drag clips between tracks, box-select,
  Shift-click, nudge, cut, copy, paste, duplicate, split, clip gain,
  normalize, reverse, mute, takes, and right-click any clip for all of it.
  Markers along the ruler (Intro, Verse, Chorus...) to jump around the song.
- **Mix**: volume, pan, mute, solo and a meter per track, a master meter,
  and effects on any track:
  - **Encore's plugins, built in**: Encore Vox (a whole vocal chain with tune
    and harmony), Verb, Equalize and Master. Nothing to install.
  - **Your plugins**: Basement finds your VST3 plugins by itself (in the
    background, each checked in a separate helper process so a broken plugin
    can't crash it) and lists them by maker. Add more plugin folders in
    Preferences.
  - A few simple built-in effects (EQ, compressor, reverb, delay and more).
- **Export** the whole mix to a 24-bit WAV, or zip the whole song folder.
- **Safe**: songs save as one folder each, Basement keeps an automatic copy
  every two minutes, and offers it back if it ever closes unexpectedly.
- Menus like a real DAW (File, Edit, Project, Audio, MIDI, Score, Media,
  Transport, Studio, Window, Help), resizable tracks, and a status bar with
  your interface, latency and CPU.

Keys: Space play/stop, R record, L loop, C click, Home back to the start,
Ctrl+S save, Ctrl+N new song, Ctrl+O open, Ctrl+T add track, Ctrl+I import,
Ctrl+E export, Alt+X split, Ctrl+Left/Right nudge, Insert adds a marker,
Shift+Left/Right jumps between markers, Ctrl+Z / Ctrl+Y undo / redo.
Help > Keyboard Shortcuts has the full list.

## Coming next

- A mixer view with sends, FX tracks and a master bus with its own plugins
- Volume and pan automation lanes
- Exporting stems, MP3 and FLAC, and mastering on export
- Sign in with Encore and send a song to your catalog as a draft
- MIDI and instruments

## Build

Needs CMake and a C++20 compiler: Visual Studio 2022 (or its Build Tools) with
the C++ workload on Windows, Xcode on a Mac.

```
git clone https://github.com/jimmyrlondo/encore-basement.git
cd encore-basement
git submodule update --init --depth 1 external/JUCE external/tracktion_engine
cmake -B build                       # Windows: add -A x64; Mac: add -G Xcode
cmake --build build --config Release
```

(Don't use `--recurse-submodules`: Tracktion Engine has its own copy of JUCE as
a submodule with an SSH address; Basement uses `external/JUCE`, pinned to the
same commit, instead.)

The app ends up in `build/Basement_artefacts/Release/`. Checks you can run:

- `--selftest <folder>`: makes songs, imports, mixes, exports, reopens, and
  runs each built-in Encore plugin, without a window (exit code 0 = all good,
  details in `<folder>/selftest.log`). GitHub runs it on every build.
- `--plugintest <report file> [folder]`: loads every VST3 in a folder the way
  the Effects menu does and reports what happened with each one.
- `--profile <name>`: keeps settings separately, for testing without touching
  your own.

The Encore plugins' code is in `encore/` (copied from the plugin repo with
`tools/sync-encore-plugins.ps1`).

## License

Basement is free software under the GNU Affero General Public License v3 (see
`LICENSE`). It uses JUCE under the AGPLv3 and Tracktion Engine under the GPLv3.
The arrange view in `Source/edit/` started from Tracktion Engine's example
code (GPLv3) and is marked where it was changed. The Encore plugins in
`encore/` are part of Basement under the same license.

Copyright (C) 2026 Encore Studio. The Encore name and logo (including
`Assets/encore-icon.*`) belong to Encore Studio; forks are welcome under the
license but should use their own name.
