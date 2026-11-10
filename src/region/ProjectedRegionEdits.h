#ifndef XIV_PROJECTED_REGION_EDITS_H
#define XIV_PROJECTED_REGION_EDITS_H

#include "MidiRegionRender.h"

namespace xiv {

// Selection IDs are one-based indexes in the complete rendered schedule.
// This distinguishes repeated notes and equal note IDs in separate regions.
inline bool materializeProjectedRegionIds(MidiRegionModel &model,
                                          const std::vector<RenderedRegionNote> &rendered,
                                          const std::set<int> &regions, int trackCount) {
    if (regions.empty()) return false;
    for (int id : regions)
        if (!model.find(id)) return false;

    auto records = model.regions();
    for (auto &region : records) {
        if (!regions.count(region.id)) continue;
        std::vector<OwnedRegionNote> notes;
        for (std::size_t i = 0; i < rendered.size(); ++i) {
            const auto &note = rendered[i];
            if (note.regionId != region.id) continue;
            notes.push_back({static_cast<int>(i) + 1, note.startTick - region.startTick,
                note.endTick - region.startTick, note.pitch, note.velocity, note.channel});
        }
        region.sourceEndTick = region.displayDuration();
        region.sourceStartTick = 0;
        region.loopLengthTicks = 0;
        region.quantize = {};
        region.startNoteMode = CrossingNoteMode::Split;
        region.endNoteMode = CrossingNoteMode::Keep;
        region.ownedNotes = std::make_shared<const std::vector<OwnedRegionNote>>(std::move(notes));
    }
    return model.replaceValidated(records, trackCount);
}


inline bool materializeProjectedRegions(MidiRegionModel &model,
                                       const std::vector<RenderedRegionNote> &rendered,
                                       const std::set<int> &selected, int trackCount) {
    std::set<int> regions;
    for (int id : selected) {
        if (id <= 0 || id > static_cast<int>(rendered.size())) return false;
        regions.insert(rendered[id - 1].regionId);
    }
    return materializeProjectedRegionIds(model, rendered, regions, trackCount);
}

} // namespace xiv
#endif
