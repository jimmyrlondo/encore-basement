# Changes

## 0.4.0 (not released yet)

- A piano roll: double-click a MIDI clip (or MIDI > Open Piano Roll,
  Ctrl+Shift+E). Click to add a note, drag to move it, drag its end to
  resize, double-click to delete, a velocity strip along the bottom, grid
  snap from 1/4 to 1/32 and triplets, and clicked notes play on the track's
  instrument. Arrow keys transpose and move; Q quantizes.
- MIDI menu: Quantize, Transpose and Velocity, for the piano roll's notes or
  every note in the selected MIDI clips.
- Group tracks: Project > Add Group Track, or Ctrl-click track headers and
  Ctrl+G. A group has one fader, effects and mute for every track in it; pick
  a track's group in its input menu or the mixer's output button. Stems
  export each track on its own and each group with its tracks.
- Templates for real setups: bands on 2, 4 and 8-input interfaces (drums on
  every input, then overdubs), and a full band live, already grouped. Tiles
  say whether a template fits your interface.
- Send to Encore (File > Export): a release package with the master, stems,
  cover art and release.json, then Encore opens to upload it.
- MP3 export: MP3 320 kbps or 192 kbps in the Export box, for the mix or
  stems, with the title, artist, album and genre written into the file.
  Made with LAME (LGPL).
- MP3 files import now (the import box offered them, but they didn't load).
- Give Tracks Their Inputs no longer gives inputs to FX or group tracks.

## 0.3.0 (2026-10-28)

Everything from the first round of testing, plus a lot more.

**Fixed from testing 0.2.0**
- Effects open. Built-in effects get a window of knobs (they had none), and
  your own plugins are found by themselves in the background: Basement used
  to wait forever when it looked for them.
- Recording through your interface's own driver (ASIO) when it has one, for
  low latency and no Windows level slider in the way. Basement switches the
  first time, and goes back to Windows audio (and says so) if another app
  like Cubase has the interface.
- Hear your input: a headphones button on every track.
- The take draws its waveform as you record.
- Space no longer flashes the Transport menu.
- Tracks can be any height: drag a track's bottom edge (Alt for all), or
  Window > Track Height.
- Opens in under a second; the audio interface opens once the window is up.

**New**
- Level meters on every track (the input while armed) and the master, with
  a clip light.
- Editing: snap to the grid, trim clips from either edge, drag fades from
  the top corners, crossfades where clips overlap, drag clips between
  tracks, box-select, nudge, and a right-click menu on every clip.
- Markers for song sections (Insert), with quick names like Verse and Chorus.
- Metronome settings and a big on-screen count-in; punch in and out; loop
  recording keeps every pass as a take.
- A mixer (F3): channel strips, FX tracks you can send to (one shared
  reverb), and a master strip.
- Encore's own plugins built in: Encore Vox, Verb, Equalize and Master.
  Encore Master's styles: Balanced, Heavy, Bright, Warm, Gentle, Wide,
  Arena and Drive.
- Older VST2 plugins (.dll) load and open too. When a plugin is installed
  as both VST2 and VST3, only the VST3 is listed.
- Plugins are checked in a separate helper process, so one that crashes,
  or stops to ask for activation, can't freeze Basement; it's set aside.
- Volume and pan automation drawn on the tracks (A).
- Instrument tracks: play and record your instrument plugins (drums, keys,
  samplers) or the built-in synth from MIDI, and import MIDI files.
- Export: the mix or every track as stems, the whole song or the loop, WAV
  or FLAC, any sample rate, optionally at -14 LUFS for streaming.
- A tuner (T), 30 Hz to 1.5 kHz.
- Autosave every two minutes (written in the background, so a slow drive
  never stalls Basement), and recovery if Basement closes unexpectedly.
- A status bar with your interface, latency and CPU.
- Mac builds (Apple Silicon and Intel).

**Known gaps**
- 32-bit plugins don't load (64-bit only).
- No piano roll for editing MIDI notes yet.

## 0.2.0

The Hub start page, the new-song setup, the full menu bar and the Encore icon.

## 0.1.0

First test build: record, import, arrange, mix and export.
