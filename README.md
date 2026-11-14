# XIV MIDI Studio

![XIV MIDI Studio showing two selected Arrangement regions in the Piano Roll](docs/images/xiv-midi-studio.png)


**Arrange like a producer with XIV MIDI Studio, a MIDI-focused digital audio workstation built for Final Fantasy XIV Bard Performance.**

XIV MIDI Studio brings Meowchestra’s MidiEditor features into a workspace inspired by Logic Pro X, Ableton Live, and Soundtrap. Open a MIDI file, assign SoundFont instruments, edit your arrangement and notes, and save your work as a standard MIDI file.

## Interactive tutorial

In the **Startup** window, select **Start the interactive tutorial** to open the bundled tutorial project. Floating instructions point to the controls you need and offer hints along the way.

- Turn a Flute and Bass Drum duet into a trio by adding a Tuba part.
- Use the Piano Roll and **GM Drum Map** to select notes by pitch, remove the open hi-hat hits, and transpose the Bass Drum notes.
- Try Global Tracks, the Piano Roll views and grid options, and playback controls.

## What's new in v0.3.6.2

- **View multiple regions in the Piano Roll.** Select regions in the Arrangement, including across tracks, to see their notes together at their song positions and in their track colors. The Piano Roll button reads **Selected Region** for one region and **Selected Regions** for multiple.
- **Maximized startup.** XIV MIDI Studio opens maximized when launched from the app shortcut or by opening a MIDI file directly.

## What's new in v0.3.6.1

- **A clearer interactive tutorial.** Get oriented before editing, learn why the drum notes need to move into XIV’s playable range, and try the Piano Roll views and grid options.
- **Easier Arrangement editing.** The taller measure ruler makes it easier to grab the playhead. Loop and trim handles respond to smaller drags and use the position where you release.
- **A clearer Piano Roll grid.** Beat and measure lines stand out from subdivisions. Notes stay selected after transposing so you can adjust them again.

## What's new in v0.3.6

- **Bundled FFXIV SoundFont.** The Windows installer includes **FFXIV Standard** and loads it by default on a fresh setup.
- **Instrument picker in the track header.** Click the instrument icon on the left to choose a preset. Track controls are grouped on the right.

## What's new in v0.3.5

- **Track instruments.** Add Track applies the chosen SoundFont preset immediately. The instrument menu marks the track’s current preset with a checkmark.
- **Region creation.** Hover over empty track space to preview a region, then **Shift+click** to add it. The region extends up to four measures and stops at the next region. A plain click clears the region selection.
- **Group region edits.** Shift-click to select multiple regions, then drag any selected region to move them along their tracks together. Drag the upper-right loop handle of any selected region to extend their loops. Each loop stops at the next region on its track, while loops with more room continue to extend. Each drag is one undo step.
- **Piano Roll editing.** Preview note moves and resizes as you drag. The **Grid** starts on **Auto (recommended)**. Use **Ctrl+mouse wheel** to zoom horizontally and reveal finer subdivisions, down to sixteenth notes. Choose **1/4**, **1/8**, or **1/16** for a fixed grid at any zoom level. A click adds a note one grid cell long, and dragging sets a different length. Right-click selected notes for editing commands, or press **Backspace** to delete them while keeping the region.

## What's new in v0.3.4

- **Playhead time.** The transport displays elapsed time in minutes, seconds, and hundredths alongside the bar and beat.

- **Global Tracks.** The lanes start collapsed. Use the disclosure above the track headers to show Arrangement, Time Signature, and Tempo. Use **+** to add a named section, a signature at the start of the current bar, or a tempo point at the exact playhead tick.
- **Song sections.** Name sections of your song in the Arrangement lane.
- **Edit the tempo line.** Click to add a point, drag it to change BPM or timing, double-click it to type a BPM, or select it and press Backspace to delete it. Drag the corner of a step to create a ramp, then drag the ramp handle to shape the transition. Double-click the handle to restore the step. The original meter and tempo tools remain available.
- **Track editing.** Click a track header to select its regions. Backspace removes the regions and a second press removes the empty track. **Copy Track** duplicates the track and its regions on an unused MIDI channel. MIDI files retain their original track order and empty tracks.
- **Shortcuts and dialog contrast.** Menus and tooltips show active bindings, and the shared dark theme improves text input and selection contrast.

## What's new in v0.3.3

- **Arrangement sections.** Drag across empty space in the **Arrangement** lane below the measure ruler and give the section a name, such as Intro, Verse, or Chorus. Drag the section to a free spot in that lane to move its regions together across all tracks. Regions crossing the section edges split automatically, with sustained notes retriggered at the cut. Right-click to rename or remove the section label. Section edits support Undo and Redo and are saved with the region layout.
- **Paste regions at the playhead.** Copy a region, select a destination track, and press **Ctrl+V** in the Arrangement. You can also right-click the destination track lane and choose **Paste Region at Playhead**. The pasted region starts at the exact playhead tick, including when Snap to Grid is on.
- **Loop handle placement.** The loop icon and its drag target stay at the region's actual right edge as the timeline scrolls.

## What's new in v0.3

### Arrangement and workspace

- **A redesigned dark workspace.** The Arrangement sits above a docked, resizable Piano Roll with a traditional black-and-white keyboard. The measure ruler stays visible while you scroll through tracks. The control bar brings transport, undo and redo, Snap, and **Fix XIV Channels** into easy reach, with readouts for position, tempo, meter, and key.
- **Native MIDI regions.** Split at the playhead or a clicked position, trim edges, move regions along the timeline or across tracks, and Ctrl-drag to duplicate. Drag a region’s upper corner to repeat it, with notches marking each repeat. A split inside a repeat preserves the timing of notes after the cut. Snap to Grid is on by default.
- **Overlays and joins.** Overlapping regions stay separate and show both sets of note previews. Shift-click to select multiple regions, then join them across a gap on one track or combine them across tracks. Use **Join Selected Regions Here** from the destination region’s right-click menu. With focus in the Arrangement, **Ctrl+T** splits at the playhead and **Ctrl+J** joins the selection.
- **Region context tools.** Right-click a region to rename it, open its quantize settings, transpose it by an octave, filter FFXIV chord voices, explode its chords to tracks, or split its notes by MIDI channel.

### Piano Roll and quantization

- **Edit one region at a time.** The Piano Roll follows the selected region. Click or drag across the Piano Roll keys to select notes by pitch, including with region quantization active. Switch to **All Tracks** to see how parts line up.
- **Shape note lengths.** Use the **Force Legato** button or right-click highlighted notes. Force Legato can extend or shorten selected notes toward the next onset. **Remove Overlaps (8 Ticks)** only shortens selected notes that overlap the next onset. Both leave an 8-tick gap where possible and support Undo.
- **Quantize by region, track, or project.** Adjust grid, strength, and swing for an individual region in the Inspector. Apply those settings to all regions on the track or explicitly copy them across the project in one undoable edit.

### Playback, instruments, and export

- **Playback cycle ribbon.** Drag across the top of the Arrangement ruler to draw an amber playback range. Drag its center to move it, drag an edge to resize it, or click the ribbon to toggle it. Cycle playback repeats a section without changing the arrangement or MIDI export. The Arrangement and Piano Roll scroll with the playhead during playback.
- **Track controls and SoundFonts.** Add tracks from the track panel, choose SoundFont presets from track headers, and audition parts with mute and solo during playback.
- **Bard arranging tools.** Use the FFXIV chord tools to keep the highest voice, lowest voice, or inner voices. Turn on **GM Drum Map** to identify General MIDI percussion by name beside the Piano Roll keys.
- **Named MIDI export.** **File → Export Arranged MIDI** renders region edits into a separate MIDI file and preserves each track’s name. Instrument names are available to Bard tools that read MIDI track names.

### Startup and help

- **Startup chooser.** Open the most recent available project, browse for an existing one, start an empty project, or launch the interactive tutorial. Opening a MIDI file directly bypasses the chooser.
- **Quick Start.** A short guide appears once after opening a project and is always available under **Help → Quick Start**.

## Windows downloads

Download published Windows builds from [GitHub Releases](https://github.com/golgiApp/XIVMIDIStudio/releases).

The [upstream MidiEditor manual](https://meowchestra.github.io/MidiEditor/manual/) covers basic MIDI editing and setup.

## Credits

XIV MIDI Studio builds on [Meowchestra’s fork](https://github.com/Meowchestra/MidiEditor) of [Markus Schwenk’s MidiEditor](https://github.com/markusschwenk/midieditor). AI tools assisted with development. The software itself contains no AI tools or features.

See [CONTRIBUTORS](CONTRIBUTORS) and [LICENSE](LICENSE).
