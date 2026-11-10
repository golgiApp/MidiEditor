/*
 * XIV MIDI Studio — MIDI note projection for future region-aware transport.
 * Source MIDI events are never edited here. All timing is in integer MIDI ticks.
 * This pure C++ layer can be tested independently of Qt/FluidSynth.
 * Copyright (C) 2026 XIV MIDI Studio contributors; GPL-3.0-or-later.
 */
#ifndef XIV_MIDI_REGION_RENDER_H
#define XIV_MIDI_REGION_RENDER_H

#include "MidiRegionModel.h"
#include <algorithm>
#include <limits>
#include <vector>

namespace xiv {

// Each note must have a stable ID so a note sustained through a split can be
// rejoined without retriggering on the split boundary.
struct SourceRegionNote {
    int id = 0;
    int trackIndex = 0;
    int startTick = 0;
    int endTick = 0;
    int pitch = 60;
    int velocity = 100;
    int channel = 0;
};
struct RenderedRegionNote {
    int sourceId = 0;
    int trackIndex = 0;
    int startTick = 0;
    int endTick = 0;
    int pitch = 60;
    int velocity = 100;
    int channel = 0;
    // A note can be joined across two adjacent split fragments ONLY when the
    // source slices are also consecutive. Independent region copies must
    // retrigger even when their timeline endpoints happen to touch.
    int sourceSliceStartTick = 0;
    int sourceSliceEndTick = 0;
};

// Use the same model for arrangement previews and, once integrated, playback
// and export. If a track has no regions, its notes remain unchanged. If it has
// any regions, only source passages covered by those regions are included.
inline std::vector<RenderedRegionNote> renderRegionNotes(
    const std::vector<SourceRegionNote> &source,
    const MidiRegionModel &model) {
    std::vector<RenderedRegionNote> result;
    for (const auto &note : source) {
        if (note.startTick < 0 || note.endTick <= note.startTick ||
            note.trackIndex < 0 || note.id <= 0) continue;
        bool hasRegions = false;
        for (const auto &region : model.regions()) {
            if (region.trackIndex != note.trackIndex) continue;
            hasRegions = true;
            const int sourceStart = std::max(note.startTick, region.sourceStartTick);
            const int sourceEnd = std::min(note.endTick, region.sourceEndTick);
            if (sourceEnd <= sourceStart) continue;

            // A source note already sounding when this right-hand region begins
            // is not retriggered for Keep or Shorten. With Split, its remaining
            // source portion becomes a separate note at the new region start.
            if (note.startTick < region.sourceStartTick &&
                region.startNoteMode != CrossingNoteMode::Split) continue;

            int renderedEndSource = sourceEnd;
            if (note.endTick > region.sourceEndTick &&
                region.endNoteMode == CrossingNoteMode::Keep) {
                // Keep preserves the note's natural release, even if another
                // region is subsequently moved. A later explicit Split or
                // Shorten boundary can still cut the held note downstream.
                renderedEndSource = note.endTick;
                for (const auto &later : model.regions()) {
                    if (later.trackIndex != note.trackIndex ||
                        later.sourceStartTick <= region.sourceEndTick ||
                        later.sourceStartTick >= renderedEndSource ||
                        later.startNoteMode == CrossingNoteMode::Keep) continue;
                    // An ordinary clone of earlier material must not truncate
                    // the continuous note merely because it has a source start.
                    if (later.sourceStartTick > region.sourceEndTick)
                        renderedEndSource = later.sourceStartTick;
                }
            }
            const int64_t outputStart = static_cast<int64_t>(region.startTick) +
                                        sourceStart - region.sourceStartTick;
            const int64_t outputEnd = static_cast<int64_t>(region.startTick) +
                                      renderedEndSource - region.sourceStartTick;
            if (outputEnd <= outputStart || outputStart < 0 ||
                outputEnd > std::numeric_limits<int>::max()) continue;
            result.push_back({note.id, note.trackIndex,
                              static_cast<int>(outputStart),
                              static_cast<int>(outputEnd),
                              note.pitch, note.velocity, note.channel,
                              sourceStart, renderedEndSource});
        }
        if (!hasRegions) {
            result.push_back({note.id, note.trackIndex, note.startTick,
                              note.endTick, note.pitch, note.velocity, note.channel,
                              note.startTick, note.endTick});
        }
    }
    // Adjacent split fragments are deliberately NOT merged here: a note
    // intentionally split by the user must retrigger at the split tick.
    std::sort(result.begin(), result.end(), [](const auto &a, const auto &b) {
        if (a.startTick != b.startTick) return a.startTick < b.startTick;
        if (a.trackIndex != b.trackIndex) return a.trackIndex < b.trackIndex;
        return a.pitch < b.pitch;
    });
    return result;
}

} // namespace xiv
#endif
