#ifndef XIV_REGION_FORCE_LEGATO_H
#define XIV_REGION_FORCE_LEGATO_H

#include "MidiRegionRender.h"
#include <map>

namespace xiv {

enum class RegionNoteEndMode { Legato, RemoveOverlap };

inline bool adjustRegionNoteEnds(std::vector<OwnedRegionNote> &notes,
                             const std::vector<RenderedRegionNote> &visible,
                             const std::set<int> &selected,
                             int sourceRegionEnd, RegionNoteEndMode mode) {
    constexpr int gapTicks = 8;
    std::set<int> onsets;
    for (const auto &note : visible)
        onsets.insert(note.sourceSliceStartTick);

    std::map<int, int> editedEnds;
    for (const auto &note : visible) {
        const int id = note.ownedNoteId ? note.ownedNoteId : note.sourceId;
        if (!selected.count(id)) continue;
        // Treat simultaneous notes as a chord and use the next distinct onset.
        const auto next = onsets.upper_bound(note.sourceSliceStartTick);
        if (mode == RegionNoteEndMode::RemoveOverlap &&
            (next == onsets.end() || note.sourceSliceEndTick <= *next)) continue;
        editedEnds[id] = next == onsets.end() ? sourceRegionEnd
            : std::max(note.sourceSliceStartTick + 1, *next - gapTicks);
    }

    bool changed = false;
    for (auto &note : notes) {
        const auto end = editedEnds.find(note.id);
        if (end == editedEnds.end() || end->second <= note.startTick ||
            note.endTick == end->second) continue;
        note.endTick = end->second;
        changed = true;
    }
    return changed;
}

} // namespace xiv
#endif
