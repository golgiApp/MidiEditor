# XIV MIDI Studio v0.3 — DEVELOPMENT SNAPSHOT (NOT RELEASE-READY)

This is **not a completed or tested v0.3 release**. Do not replace a successful stable build or publish an installer as final v0.3.

## Implemented / staged in this source snapshot
- v0.2.11 features from the previous source snapshot.
- Rebranded Windows executable / version / Help About label and installer display name.
- Traditional black/white piano keyboard even in dark mode.
- Click or drag across piano keyboard to select all notes at each selected pitch (across the currently focused track). Ctrl adds a range; Alt-click auditions.
- The bundled user-supplied `FFXIV Standard.sf2` is copied to the installer payload and adopted on first run if no user SoundFont collection exists.
- The arrangement's **Instrument** dropdown reads real SoundFont 2 presets (not a fictional GM-128 list), writes a track-owned MIDI program-change event at time 0, immediately sends a program change, and renames the track to the chosen preset.
- Other SoundFonts already enabled in settings are also presented.

## Not yet implemented / cannot be called complete
- True independently movable/splittable/trimmable/loopable MIDI region model, and project persistence for region metadata. The current top-lane region previews are still read-only.
- Per-region nondestructive quantization and project-wide explicit command.
- Cycle ribbon with repeated playback, seamless transport seeking during playback, and follow-playhead override.
- Force legato / note-end commands and fully tested cross-region editing.
- Interactive first-run tutorial.
- Integrated tests on the Windows Qt/FluidSynth toolchain.

## Important verification
The source here has not been built under Windows Qt. GitHub Actions must compile it and a human should exercise playback, instrument assignment, and note selection. The SF2 originates from user-provided content. Before distributing the SF2 in a public GitHub release, confirm licensing and rights to redistribute its samples.

Review `v0.3-ROADMAP.md` before deciding the final release scope.
