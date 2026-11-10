# XIV MIDI Studio

<img width="2544" height="1352" alt="image" src="https://github.com/user-attachments/assets/ee4be632-81df-4fd0-99f2-7656ecacfb74" />


**Arrange like a producer with XIV MIDI Studio, a MIDI-focused digital audio workstation built for Final Fantasy XIV Bard Performance.**

XIV MIDI Studio brings Meowchestra’s MidiEditor quality-of-life features into a redesigned workspace inspired by Logic Pro X, Ableton Live, and Soundtrap. Open a MIDI file, assign SoundFont instruments, arrange phrases across tracks, and edit their notes in the Piano Roll. When your arrangement is ready, export it as a standard MIDI file.

## What's new in v0.3

### Arrangement and workspace

- **A redesigned dark workspace.** The Arrangement sits above a docked, resizable Piano Roll with a traditional black-and-white keyboard. The measure ruler stays visible while you scroll through tracks. The control bar brings transport, undo and redo, Snap, and **Fix XIV Channels** into easy reach, with readouts for position, tempo, meter, and key.
- **Native MIDI regions.** Split at the playhead or a clicked position, trim edges, move regions along the timeline or across tracks, and Ctrl-drag to duplicate. Drag a region’s upper corner to repeat it, with notches marking each repeat. A split inside a repeat preserves the timing of notes after the cut. Snap to Grid is on by default.
- **Overlays and joins.** Overlapping regions stay separate and show both sets of note previews. Shift-click to select multiple regions, then join them across a gap on one track or combine them across tracks. Use **Join Selected Regions Here** from the destination region’s right-click menu. With focus in the Arrangement, **Ctrl+T** splits at the playhead and **Ctrl+J** joins the selection.
- **Region context tools.** Right-click a region to open its quantize settings, transpose it, filter FFXIV chord voices, explode its chords to tracks, or split its notes by MIDI channel.

### Piano Roll and quantization

- **Edit one region at a time.** The Piano Roll follows the selected region. Switch to **All Tracks** to see how parts line up, as in MidiEditor. Click or drag across the Piano Roll keys to select notes by pitch.
- **Shape note lengths.** Use the **Force Legato** button or right-click highlighted notes. Force Legato can extend or shorten selected notes toward the next onset. **Remove Overlaps (8 Ticks)** only shortens selected notes that overlap the next onset. Both leave an 8-tick gap where possible and support Undo. These edits use the selected-region editor with region looping and quantization off.
- **Quantize by region, track, or project.** Adjust grid, strength, and swing for an individual region in the Inspector. Apply those settings to all regions on the track or explicitly copy them across the project in one undoable edit.

### Playback, instruments, and export

- **Playback cycle ribbon.** Drag across the top of the Arrangement ruler to draw an amber playback range. Drag its center to move it, drag an edge to resize it, or click the ribbon to toggle it. Cycle playback repeats a section without changing the arrangement or MIDI export. The Arrangement and Piano Roll scroll with the playhead during playback.
- **Track controls and SoundFonts.** Add tracks from the track panel, choose SoundFont presets from track headers, and audition parts with mute and solo during playback.
- **Bard arranging tools.** Use the FFXIV chord tools to keep the highest voice, lowest voice, or inner voices. Turn on **GM Drum Map** to identify General MIDI percussion by name beside the Piano Roll keys.
- **Named MIDI export.** **File → Export Arranged MIDI** renders region edits into a separate MIDI file and preserves each track’s name. Instrument names are available to Bard tools that read MIDI track names.

### Startup and help

- **Startup chooser.** Open the most recent available project, browse for an existing one, or start an empty project. Opening a MIDI file directly bypasses the chooser.
- **Quick Start.** A short guide appears once after opening a project and is always available under **Help → Quick Start**.

## Windows downloads and development status

Check this repository’s **Releases** tab for published Windows builds. The v0.3 feature list above describes the current source. The latest Windows build is still being tested, including cycle playback and the Piano Roll right-click commands.

The [upstream MidiEditor manual](https://meowchestra.github.io/MidiEditor/manual/) covers basic MIDI editing and setup.

## Credits

XIV MIDI Studio builds on [Meowchestra’s fork](https://github.com/Meowchestra/MidiEditor) of [Markus Schwenk’s MidiEditor](https://github.com/markusschwenk/midieditor). AI tools assisted with development. The software itself contains no AI tools or features.

See [CONTRIBUTORS](CONTRIBUTORS) and [LICENSE](LICENSE).
