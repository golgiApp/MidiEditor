# XIV MIDI Studio interactive tutorial

This source patch is based on `main` at `5c9bd609332e61cece8da775f308d49598af9623`. It includes the tutorial MIDI as a Qt resource. The existing Windows xmake workflow builds the new source and resource.

From the root of a clean repository checkout, run the following commands after extracting this archive outside the checkout. Replace the path with the actual path to the extracted patch.

```powershell
git apply --check C:/path/to/interactive-tutorial.patch
git apply --index C:/path/to/interactive-tutorial.patch
git commit -m "Add interactive startup tutorial" -m "Guide users through arranging a Flute and Tuba duet and cleaning up the included Bass Drum part. Highlight the editor controls and finish with a tour of the workspace."
```

Push the commit through the normal Windows build workflow.

## Verify in the Windows build

1. Launch the app and choose **Start the interactive tutorial** in **Startup**. Confirm that the demo opens with the Flute melody and a muted Bass Drum track.
2. Complete the instrument and arrangement prompts. Confirm that the Tuba opening starts at measure 1, repeats twice, returns at measure 21, and ends at measure 29. The bundled melody has no note spanning the start of measure 5, so the editor splits it directly. The prompt also covers the crossing-note dialog if it appears.
3. In the drum lesson, delete the Open Hi-Hat row and transpose the other 109 notes up twice. Verify that the Bass Drum is audible on playback with the XIV SoundFont.
4. Finish the controls tour. Confirm that the edited arrangement is still open. Relaunch the tutorial and confirm that it starts from the original demo MIDI again.

The MIDI, region edit sequence, and patch application were checked locally. Run the Windows build and the UI walkthrough before release.
