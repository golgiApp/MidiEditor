/*
 * XIV MIDI Studio - initial non-destructive MIDI region model.
 * Copyright (C) 2026 XIV MIDI Studio contributors.
 * GPL-3.0-or-later; see LICENSE in the repository root.
 *
 * Region positions and note clipping use integer MIDI ticks (not milliseconds).
 * Sidecar persistence and UI live in ArrangementWidget; this file intentionally
 * has no Qt dependencies so boundary calculations can be tested independently.
 */
#ifndef XIV_MIDI_REGION_MODEL_H
#define XIV_MIDI_REGION_MODEL_H

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace xiv {

struct MidiRegion {
    int id = 0;
    int trackIndex = 0;
    int startTick = 0;         // Position of the region in the arrangement
    int sourceStartTick = 0;   // First audible tick from the original track
    int sourceEndTick = 0;     // Exclusive: notes at this tick belong to the next region

    int duration() const { return sourceEndTick - sourceStartTick; }
    int endTick() const { return startTick + duration(); }
    bool valid() const {
        return id > 0 && trackIndex >= 0 && startTick >= 0 && sourceStartTick >= 0 &&
               sourceEndTick > sourceStartTick &&
               static_cast<int64_t>(startTick) + sourceEndTick - sourceStartTick <=
                   std::numeric_limits<int>::max();
    }
    bool contains(int timelineTick) const {
        return timelineTick >= startTick && timelineTick < endTick();
    }
};

struct ProjectedNote {
    int startTick = 0;
    int endTick = 0;
};

// Map a source note into a region, preserving half-open MIDI note boundaries.
// The entire source note is retained elsewhere; this operation never mutates it.
inline bool projectNote(const MidiRegion &region, int noteStartTick,
                        int noteEndTick, ProjectedNote &out) {
    if (!region.valid() || noteStartTick >= noteEndTick) return false;
    const int start = std::max(region.sourceStartTick, noteStartTick);
    const int end = std::min(region.sourceEndTick, noteEndTick);
    if (end <= start) return false;
    out.startTick = region.startTick + (start - region.sourceStartTick);
    out.endTick = region.startTick + (end - region.sourceStartTick);
    return out.endTick > out.startTick;
}

class MidiRegionModel {
public:
    const std::vector<MidiRegion> &regions() const { return _regions; }
    void clear() { _regions.clear(); _nextId = 1; }
    int addInitial(int trackIndex, int sourceStartTick, int sourceEndTick) {
        if (trackIndex < 0 || sourceStartTick < 0 || sourceEndTick <= sourceStartTick) return 0;
        const int id = _nextId++;
        _regions.push_back({id, trackIndex, sourceStartTick, sourceStartTick, sourceEndTick});
        return id;
    }
    const MidiRegion *find(int id) const {
        for (const auto &r : _regions) if (r.id == id) return &r;
        return nullptr;
    }
    const MidiRegion *at(int trackIndex, int tick) const {
        for (const auto &r : _regions)
            if (r.trackIndex == trackIndex && r.contains(tick)) return &r;
        return nullptr;
    }
    // Splitting changes region boundaries only; no MIDI event is destroyed.
    // The right half retains the original region's source end. Adjacent split
    // fragments therefore cover exactly the same source interval as before.
    int split(int id, int timelineTick) {
        for (auto &r : _regions) {
            if (r.id != id || timelineTick <= r.startTick || timelineTick >= r.endTick()) continue;
            const int sourceSplit = r.sourceStartTick + (timelineTick - r.startTick);
            const int oldEnd = r.sourceEndTick;
            r.sourceEndTick = sourceSplit;
            const int newId = _nextId++;
            _regions.push_back({newId, r.trackIndex, timelineTick, sourceSplit, oldEnd});
            return newId;
        }
        return 0;
    }
    // Non-destructive arrangement movement. Only the destination timeline
    // changes: sourceStartTick/sourceEndTick retain the original musical data.
    // Reject overlapping regions and integer overflow before mutating state.
    // Cross-track movement is intentionally deferred until instrument/channel
    // routing and MIDI export can honor it correctly.
    bool canMove(int id, int newStartTick) const {
        const MidiRegion *target = find(id);
        if (!target || newStartTick < 0 ||
            static_cast<int64_t>(newStartTick) + target->duration() >
                std::numeric_limits<int>::max()) return false;
        const int64_t newEndTick = static_cast<int64_t>(newStartTick) + target->duration();
        for (const auto &r : _regions) {
            if (r.id == id || r.trackIndex != target->trackIndex) continue;
            if (newStartTick < r.endTick() && r.startTick < newEndTick) return false;
        }
        return true;
    }
    bool move(int id, int newStartTick) {
        if (!canMove(id, newStartTick)) return false;
        for (auto &r : _regions) {
            if (r.id == id) {
                r.startTick = newStartTick;
                return true;
            }
        }
        return false;
    }
    // Place a new independent instance of the same source interval, provided
    // it will not overlap an existing region on this track.
    int duplicate(int id, int newStartTick) {
        const MidiRegion *target = find(id);
        if (!target || newStartTick < 0) return 0;
        const int64_t newEnd = static_cast<int64_t>(newStartTick) + target->duration();
        if (newEnd > std::numeric_limits<int>::max()) return 0;
        for (const auto &other : _regions) {
            if (other.trackIndex == target->trackIndex &&
                newStartTick < other.endTick() && other.startTick < newEnd) return 0;
        }
        const MidiRegion source = *target; // snapshot before vector growth
        const int newId = _nextId++;
        _regions.push_back({newId, source.trackIndex, newStartTick,
                            source.sourceStartTick, source.sourceEndTick});
        return newId;
    }
    // Validate sidecar records before replacing the active model. A stale or
    // malformed layout must never partially install into the live UI.
    bool replaceValidated(const std::vector<MidiRegion> &incoming, int trackCount) {
        if (trackCount < 0 || incoming.size() > 100000) return false;
        std::vector<int> ids;
        ids.reserve(incoming.size());
        int highestId = 0;
        for (const auto &r : incoming) {
            if (!r.valid() || r.trackIndex >= trackCount || r.id > 100000000) return false;
            ids.push_back(r.id);
            highestId = std::max(highestId, r.id);
        }
        std::sort(ids.begin(), ids.end());
        if (std::adjacent_find(ids.begin(), ids.end()) != ids.end()) return false;
        // In this first implementation regions on one track may touch, but
        // they may not overlap. Future region overlays need explicit layers.
        auto sorted = incoming;
        std::sort(sorted.begin(), sorted.end(), [](const MidiRegion &a, const MidiRegion &b) {
            if (a.trackIndex != b.trackIndex) return a.trackIndex < b.trackIndex;
            return a.startTick < b.startTick;
        });
        for (std::size_t i = 1; i < sorted.size(); ++i) {
            if (sorted[i - 1].trackIndex == sorted[i].trackIndex &&
                sorted[i - 1].endTick() > sorted[i].startTick) return false;
        }
        _regions = incoming;
        _nextId = highestId + 1;
        return true;
    }
private:
    std::vector<MidiRegion> _regions;
    int _nextId = 1;
};
} // namespace xiv
#endif
