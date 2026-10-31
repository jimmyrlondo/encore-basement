# Changes

## 0.4.0 (not released yet)

**Encore: Basement is now Encore: Basement Studio.** Your settings and songs carry
over by themselves. New songs are saved as .encore files, and .basement songs
still open as they are.

- Tabs under the toolbar for the parts you jump between: Tracks, Mixer, Piano
  Roll, Drum Grid and Tuner (Alt+1 to Alt+5). Add Track, Import and Export
  moved into the File menu.
- Keyboard shortcuts you can change: Preferences > Keyboard Shortcuts lists
  every action (docs/SHORTCUTS.md has the defaults).
- Preferences is a normal settings window: General, Audio, Recording,
  Plug-ins and Keyboard Shortcuts, with OK, Cancel and Apply.
- When the audio interface is unplugged, a notice says so (and what Studio
  is using instead), and Studio switches back when it's plugged in again.
- A track's input menu names the interface and its inputs, ticks the ones the
  track records from (click again to untick), lists MIDI keyboards apart, and
  says plainly where the track's sound goes.
- Tracks: the selected track takes a dark shade of its own colour, Shift-click
  selects a run of tracks, drag a track's header up or down to move it, and
  right-click a track, its lane or its plug-ins for what you can do there.
- The Effects column is now Plug-ins, with names you can read and a bypass dot.
- Selected text shows up (it was the same colour as the field).
- The tuner: 87 instruments and 229 tunings (guitar and bass to mandolin,
  banjo, bouzouki, oud, lute, viols, koto, timpani, kalimba and transposing
  horns, which get their band and orchestra tuning notes and what to adjust),
  which string you're playing, a needle or strobe, any reference pitch and
  historical temperaments. Paired strings (12-string, mandolin, lute, bouzouki, charango,
  tres, tiple...) show every string, octave strings included. The same tuner
  is on the web beside Master and Live.
- Metronome sounds (8) and how beat 1 sounds: higher, louder, a cowbell or the
  same. Right-click the Click button for its settings.
- New Song: the song's details first, then where it starts from, with its
  tracks right under it. Blank really is blank; the band sizes and Live
  multitrack starting points are gone (Full band stays).
- Your profile: several genres from Apple Music's list (what Encore uses),
  hometown, members, website, who owns the recordings, and the songwriter's
  PRO and IPI, which go into what's sent to Encore.
- Plugins that open a window while being checked (an activation prompt) no
  longer flash it on screen.
- Who you're making music as: the first screen asks for an artist or band, or
  a record label and its artists (a profile on this computer until Encore
  sign-in arrives). Pick the artist on the start page or in the Account menu,
  and new songs get their name and genre filled in.
- A folder for each artist, in a music library you choose: Songs, Releases,
  Masters and Live. Every finished export of a whole song is also kept in
  Masters, and when it's mastered again the earlier one moves to "Earlier
  masters", so nothing is lost. Send to Encore packages go in Releases, and
  live shows go in Live.
- An installer for Windows (Start menu and desktop shortcuts, opens .encore
  songs on double-click, and an uninstaller that leaves your music alone) and
  a disk image for the Mac.
- 32-bit plugins work (Windows): each one runs in a small 32-bit helper
  beside Studio, so old VST and VST3 plugins show up in the menus marked
  "(32-bit)". Their own window opens in front of Studio, and if one crashes,
  only that plugin goes quiet.
- Tempo and time signature changes: right-click the bar numbers (or Project >
  Tempo or Time Signature Change at the Playhead). A tempo can jump or ramp.
  Bars, the grid and snapping follow.
- Audio that follows the song's tempo: right-click a clip > Follow the Song's
  Tempo. A loop with its tempo in the name ("Beat 92bpm.wav") does it when
  you import it. The pitch stays the same.
- Four drum kits, all made by Studio itself: Studio, 808, Rock and Lo-fi (the
  Kit box in the drum grid).
- Recording a drum track from pads or a keyboard puts the hits into the beat
  that's there, snapped to 16ths (MIDI > Snap Recorded Drum Hits to the Grid).
- The title bar is dark like the rest of the app.

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
- Master it on export: pick a style (Balanced, Heavy, Bright, Warm), a
  loudness (Streaming -14, Balanced -11, Loud -9) and how hard, and the mix
  goes out through Encore Master. It learns the song on its loudest 30
  seconds first, so the whole song is mastered evenly. Your song doesn't
  change. Listen plays 30 seconds mastered; flip to the original, or match
  their loudness for a fair comparison.
- Drum tracks: Project > Add Drum Track (Ctrl+Alt+T) gives you the Studio
  Kit (ten drum sounds Studio makes itself, saved in the song folder) and a
  drum grid. Click steps for hits, drag to paint, Shift+click for accents,
  right-click to fill or clear a row. The piano roll and drum grid switch
  with one button.
- Song versions: Save New Version (Ctrl+Alt+S) asks what's changing, and
  File > Versions (Ctrl+Alt+V) lists every version with its note, opens any
  of them, and compares one with the open song by ear (the same 30 seconds of
  each, switching between them).
- Import a live recording (File > Import > Live Recording): an Encore Live
  session or any multichannel WAV becomes a track per input, grouped by name
  (drums, vocals, guitars), with a marker at each song, or a Studio song for
  each song in the show. Inputs with nothing on them are left out.
- Help > Check for Updates looks for a newer Studio (and once a day by
  itself, which you can switch off), and Help > Report a Problem makes a zip
  of the log and your setup on the Desktop and opens a GitHub issue to attach
  it to. If Studio didn't close properly, it says so next time.
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
