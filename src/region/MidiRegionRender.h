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
            ProjectedNote projection;
            if (!projectNote(region, note.startTick, note.endTick, projection)) continue;
            result.push_back({note.id, note.trackIndex,
                              projection.startTick, projection.endTick,
                              note.pitch, note.velocity, note.channel});
        }
        if (!hasRegions) {
            result.push_back({note.id, note.trackIndex, note.startTick,
                              note.endTick, note.pitch, note.velocity, note.channel});
        }
    }
    // A sustained note passing through an unmoved split must still play as one
    // continuous MIDI note, not a pair of retriggered segments.
    std::sort(result.begin(), result.end(), [](const auto &a, const auto &b) {
        if (a.trackIndex != b.trackIndex) return a.trackIndex < b.trackIndex;
        if (a.sourceId != b.sourceId) return a.sourceId < b.sourceId;
        if (a.startTick != b.startTick) return a.startTick < b.startTick;
        return a.endTick < b.endTick;
    });
    std::vector<RenderedRegionNote> merged;
    for (const auto &note : result) {
        if (!merged.empty()) {
            auto &previous = merged.back();
            if (previous.trackIndex == note.trackIndex &&
                previous.sourceId == note.sourceId &&
                previous.channel == note.channel &&
                previous.pitch == note.pitch &&
                previous.velocity == note.velocity &&
                previous.endTick == note.startTick) {
                previous.endTick = note.endTick;
                continue;
            }
        }
        merged.push_back(note);
    }
    std::sort(merged.begin(), merged.end(), [](const auto &a, const auto &b) {
        if (a.startTick != b.startTick) return a.startTick < b.startTick;
        if (a.trackIndex != b.trackIndex) return a.trackIndex < b.trackIndex;
        return a.pitch < b.pitch;
    });
    return merged;
}

} // namespace xiv
#endif
