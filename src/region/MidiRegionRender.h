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
#include <unordered_map>
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
    int regionId = 0;
    int ownedNoteId = 0;
};

// Quantize relative to each instance's start. Keep the attack inside the
// region, and preserve the note length except at explicit cut boundaries.
inline void quantizeRegionNote(const MidiRegion &region, int64_t &start, int64_t &end) {
    const auto &q = region.quantize;
    if (!q.active()) return;

    const int64_t relative = start - region.startTick;
    const int64_t step = q.gridTicks;
    const int64_t pairStart = relative / (2 * step) * (2 * step);
    const int64_t swung = pairStart + (2 * step * q.swing + 50) / 100;
    int64_t target = pairStart;
    for (const int64_t candidate : {swung, pairStart + 2 * step}) {
        const auto distance = candidate > relative ? candidate - relative : relative - candidate;
        const auto best = target > relative ? target - relative : relative - target;
        if (distance < best) target = candidate;
    }
    const int64_t delta = (target - relative) * q.strength;
    const int64_t adjusted = std::clamp<int64_t>(
        relative + (delta + (delta < 0 ? -50 : 50)) / 100,
        0, region.duration() - 1);
    const bool endsAtCut = region.endNoteMode != CrossingNoteMode::Keep &&
                           end == region.endTick();
    end += adjusted - relative;
    start = region.startTick + adjusted;
    if (endsAtCut) end = region.endTick();
    if (region.endNoteMode != CrossingNoteMode::Keep)
        end = std::min<int64_t>(end, region.endTick());
    end = std::clamp<int64_t>(end, start + 1, std::numeric_limits<int>::max());
}

// Use the same model for arrangement previews and, once integrated, playback
// and export. If a track has no regions, its notes remain unchanged. If it has
// any regions, only source passages covered by those regions are included.
inline std::vector<RenderedRegionNote> renderUnloopedRegionNotes(
    const std::vector<SourceRegionNote> &source,
    const MidiRegionModel &model, int trackFilter = -1) {
    std::vector<RenderedRegionNote> result;
    const auto appendNote = [&result, &model](const SourceRegionNote &note,
                                               const MidiRegion &region, int ownedId) {
        const int sourceStart = std::max(note.startTick, region.sourceStartTick);
        const int sourceEnd = std::min(note.endTick, region.sourceEndTick);
        if (sourceEnd <= sourceStart) return;

        // A source note already sounding when this right-hand region begins
        // is not retriggered for Keep or Shorten. With Split, its remaining
        // source portion becomes a separate note at the new region start.
        if (note.startTick < region.sourceStartTick &&
            region.startNoteMode != CrossingNoteMode::Split) return;

        int renderedEndSource = sourceEnd;
        if (note.endTick > region.sourceEndTick &&
            region.endNoteMode == CrossingNoteMode::Keep) {
            // Keep preserves the note's natural release, even if another
            // region is subsequently moved. A later explicit Split or
            // Shorten boundary can still cut the held note downstream.
            renderedEndSource = note.endTick;
            for (const auto &later : model.regions()) {
                if (ownedId || later.trackIndex != note.trackIndex ||
                    later.sourceStartTick <= region.sourceEndTick ||
                    later.sourceStartTick >= renderedEndSource ||
                    later.startNoteMode == CrossingNoteMode::Keep) continue;
                // An ordinary clone of earlier material must not truncate
                // the continuous note merely because it has a source start.
                if (later.sourceStartTick > region.sourceEndTick)
                    renderedEndSource = later.sourceStartTick;
            }
        }
        int64_t outputStart = static_cast<int64_t>(region.startTick) +
                                    sourceStart - region.sourceStartTick;
        int64_t outputEnd = static_cast<int64_t>(region.startTick) +
                                  renderedEndSource - region.sourceStartTick;
        if (outputEnd <= outputStart || outputStart < 0 ||
            outputEnd > std::numeric_limits<int>::max()) return;
        quantizeRegionNote(region, outputStart, outputEnd);
        result.push_back({note.id, note.trackIndex,
                          static_cast<int>(outputStart),
                          static_cast<int>(outputEnd),
                          note.pitch, note.velocity, note.channel,
                          sourceStart, renderedEndSource, region.id, ownedId});
    };
    for (const auto &note : source) {
        if ((trackFilter >= 0 && note.trackIndex != trackFilter) || note.startTick < 0 || note.endTick <= note.startTick ||
            note.trackIndex < 0 || note.id <= 0) continue;
        bool hasRegions = false;
        for (const auto &region : model.regions()) {
            if (region.trackIndex != note.trackIndex) continue;
            hasRegions = true;
            if (!region.ownedNotes) appendNote(note, region, 0);
        }
        if (!hasRegions && !model.isManagedTrack(note.trackIndex)) {
            result.push_back({note.id, note.trackIndex, note.startTick,
                              note.endTick, note.pitch, note.velocity, note.channel,
                              note.startTick, note.endTick});
        }
    }
    for (const auto &region : model.regions()) {
        if (!region.ownedNotes || (trackFilter >= 0 && region.trackIndex != trackFilter)) continue;
        for (const auto &note : *region.ownedNotes) {
            appendNote({0, region.trackIndex, note.startTick, note.endTick,
                        note.pitch, note.velocity, note.channel}, region, note.id);
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

// Expand a loop into short-lived virtual regions. Every iteration uses the
// SAME source notes; the final iteration may cover only part of the phrase.
// Loop boundaries always retrigger notes instead of sustaining across cycles.
inline std::vector<RenderedRegionNote> renderRegionNotes(
    const std::vector<SourceRegionNote> &source,
    const MidiRegionModel &model, int trackFilter = -1) {
    bool hasLoops = false;
    for (const auto &r : model.regions()) if (r.loopLengthTicks > 0) {
        hasLoops = true; break;
    }
    if (!hasLoops) return renderUnloopedRegionNotes(source, model, trackFilter);
    std::vector<MidiRegion> expanded;
    std::unordered_map<int, int> realRegionId;
    int nextId = 1;
    int maxTrack = 0;
    for (const auto &r : model.regions()) {
        maxTrack = std::max(maxTrack, r.trackIndex);
        if (r.loopLengthTicks == 0) {
            MidiRegion instance = r;
            instance.id = nextId++;
            realRegionId[instance.id] = r.id;
            expanded.push_back(instance);
            continue;
        }
        const int baseLength = r.duration();
        const int loops = 1 + (r.loopLengthTicks - 1) / baseLength;
        for (int k = 0; k < loops; ++k) {
            MidiRegion instance = r;
            instance.id = nextId++;
            realRegionId[instance.id] = r.id;
            instance.startTick = r.startTick + k * baseLength;
            instance.sourceEndTick = r.sourceStartTick +
                std::min(baseLength, r.loopLengthTicks - k * baseLength);
            instance.loopLengthTicks = 0;
            if (k > 0) instance.startNoteMode = CrossingNoteMode::Split;
            instance.endNoteMode = CrossingNoteMode::Shorten;
            expanded.push_back(instance);
        }
    }
    MidiRegionModel expandedModel = model;
    if (!expandedModel.replaceValidated(expanded, maxTrack + 1)) return {};
    auto result = renderUnloopedRegionNotes(source, expandedModel, trackFilter);
    for (auto &note : result) {
        const auto it = realRegionId.find(note.regionId);
        if (it != realRegionId.end()) note.regionId = it->second;
    }
    return result;
}

} // namespace xiv
#endif
