#ifndef MIDI_REGION_TRANSFER_H
#define MIDI_REGION_TRANSFER_H

#include "MidiRegionRender.h"

namespace xiv {

inline bool validRegionDestination(int track, int start, int64_t end, int trackCount) {
    return track >= 0 && track < trackCount && start >= 0 && end > start &&
           end <= std::numeric_limits<int>::max();
}

inline int emptyRegionGapDuration(const MidiRegionModel &model, int track, int start, int limit) {
    if (start < 0 || limit <= start || model.at(track, start)) return 0;
    for (const auto &region : model.regions()) {
        if (region.trackIndex == track && region.startTick > start)
            limit = std::min(limit, region.startTick);
    }
    return limit - start;
}

struct RegionClipboard {
    int duration = 0;
    std::vector<OwnedRegionNote> notes;
    std::string name;
};

inline int insertOwnedRegion(MidiRegionModel &model, int track, int start, int trackCount,
                             const RegionClipboard &clip, int channel) {
    const int64_t end = static_cast<int64_t>(start) + clip.duration;
    if (clip.duration <= 0 || !validRegionDestination(track, start, end, trackCount) ||
        channel < 0 || channel >= 16) return 0;
    auto notes = clip.notes;
    for (auto &note : notes) {
        if (static_cast<int64_t>(start) + note.endTick > std::numeric_limits<int>::max())
            return 0;
        note.channel = channel;
    }
    if (!validOwnedNotes(notes)) return 0;

    const int id = model.addInitial(track, 0, clip.duration);
    model.move(id, start);
    model.replaceOwnedNotes(id, std::move(notes));
    model.rename(id, clip.name);
    return id;
}

// Capture the audible note positions before removing anything. The resulting
// region owns its notes independently of the original tracks and transforms.
inline bool consolidateRegions(MidiRegionModel &model, const std::set<int> &ids,
                               int primaryId, int destinationTrack, int destinationStart,
                               int destinationChannel, int trackCount,
                               const std::vector<SourceRegionNote> &source) {
    const auto *primary = model.find(primaryId);
    if (!primary || !ids.count(primaryId)) return false;
    int first = std::numeric_limits<int>::max();
    int last = 0;
    bool changesTrack = false;
    for (int id : ids) {
        const auto *region = model.find(id);
        if (!region) return false;
        first = std::min(first, region->startTick);
        last = std::max(last, region->endTick());
        changesTrack |= region->trackIndex != destinationTrack;
    }
    if (changesTrack && (destinationChannel < 0 || destinationChannel > 15)) return false;
    const int64_t end = static_cast<int64_t>(destinationStart) + (last - first);
    if (!validRegionDestination(destinationTrack, destinationStart, end, trackCount))
        return false;

    std::vector<OwnedRegionNote> notes;
    for (const auto &note : renderRegionNotes(source, model)) {
        if (!ids.count(note.regionId)) continue;
        const int64_t noteEnd = static_cast<int64_t>(destinationStart) + note.endTick - first;
        if (noteEnd > std::numeric_limits<int>::max()) return false;
        notes.push_back({static_cast<int>(notes.size()) + 1, note.startTick - first,
                         note.endTick - first, note.pitch, note.velocity,
                         note.trackIndex == destinationTrack ? note.channel : destinationChannel});
    }

    MidiRegion result{primaryId, destinationTrack, destinationStart, 0, last - first};
    result.name = primary->name;
    result.ownedNotes = std::make_shared<const std::vector<OwnedRegionNote>>(std::move(notes));
    std::vector<MidiRegion> regions;
    for (const auto &region : model.regions())
        if (!ids.count(region.id)) regions.push_back(region);
    regions.push_back(std::move(result));
    return model.replaceValidated(regions, trackCount);
}

} // namespace xiv

#endif
