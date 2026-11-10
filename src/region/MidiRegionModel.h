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
#include <set>
#include <vector>

namespace xiv {

// Behavior of a sustained source note that straddles a region split.
// Keep: the first note continues across the split; no new note in the right
// region. Shorten: end the first note at the cut; no new note on the right.
// Split: end the first and retrigger a new note in the right region.
enum class CrossingNoteMode : int { Keep = 0, Shorten = 1, Split = 2 };
inline bool validCrossingNoteMode(CrossingNoteMode mode) {
    return mode == CrossingNoteMode::Keep || mode == CrossingNoteMode::Shorten ||
           mode == CrossingNoteMode::Split;
}

struct RegionQuantize {
    int gridTicks = 0;
    int strength = 100;
    int swing = 50;

    bool valid() const {
        return gridTicks >= 0 && strength >= 0 && strength <= 100 &&
               swing >= 50 && swing <= 75;
    }
    bool active() const { return gridTicks > 0 && strength > 0; }
    bool operator==(const RegionQuantize &other) const {
        return gridTicks == other.gridTicks && strength == other.strength &&
               swing == other.swing;
    }
};

struct MidiRegion {
    int id = 0;
    int trackIndex = 0;
    int startTick = 0;         // Position of the region in the arrangement
    int sourceStartTick = 0;   // First audible tick from the original track
    int sourceEndTick = 0;     // Exclusive: notes at this tick belong to the next region
    // Boundary policies survive moves, duplications, Undo/Redo and sidecar saves.
    // Default keeps existing sidecars' original legato splitting behavior.
    CrossingNoteMode startNoteMode = CrossingNoteMode::Split;
    CrossingNoteMode endNoteMode = CrossingNoteMode::Keep;
    // Zero means one ordinary playthrough. Positive values >= duration()
    // extend the region with full or partial repeats of its original source.
    int loopLengthTicks = 0;
    RegionQuantize quantize{};

    int duration() const { return sourceEndTick - sourceStartTick; }
    int displayDuration() const { return loopLengthTicks > 0 ? loopLengthTicks : duration(); }
    int endTick() const { return startTick + displayDuration(); }
    bool valid() const {
        return quantize.valid() && id > 0 && trackIndex >= 0 && startTick >= 0 && sourceStartTick >= 0 &&
               validCrossingNoteMode(startNoteMode) && validCrossingNoteMode(endNoteMode) &&
               sourceEndTick > sourceStartTick && loopLengthTicks >= 0 &&
               (loopLengthTicks == 0 || loopLengthTicks >= duration()) &&
               static_cast<int64_t>(startTick) + displayDuration() <=
                   std::numeric_limits<int>::max() &&
               (loopLengthTicks == 0 || loopLengthTicks / duration() <= 10000);
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
    void clear() { _regions.clear(); _nextId = 1; _managedTracks.clear(); }
    // Even when its last region is deleted, the track remains managed and
    // renders silence rather than reverting to the original MIDI notes.
    bool isManagedTrack(int trackIndex) const {
        return _managedTracks.count(trackIndex) != 0;
    }
    const std::set<int> &managedTracks() const { return _managedTracks; }
    bool setQuantize(int id, const RegionQuantize &settings) {
        if (!settings.valid()) return false;
        for (auto &region : _regions) {
            if (region.id != id || region.quantize == settings) continue;
            region.quantize = settings;
            return true;
        }
        return false;
    }
    void markManagedTrack(int trackIndex) {
        if (trackIndex >= 0) _managedTracks.insert(trackIndex);
    }
    bool remove(int id) {
        auto it = std::find_if(_regions.begin(), _regions.end(),
                               [id](const MidiRegion &r) { return r.id == id; });
        if (it == _regions.end()) return false;
        _managedTracks.insert(it->trackIndex);
        _regions.erase(it);
        return true;
    }
    bool canLoop(int id, int newEndTick) const {
        const auto *r = find(id);
        if (!r || newEndTick < r->startTick + r->duration() ||
            newEndTick <= 0 ||
            (static_cast<int64_t>(newEndTick) - r->startTick) / r->duration() > 10000)
            return false;
        for (const auto &other : _regions)
            if (other.id != id && other.trackIndex == r->trackIndex &&
                r->startTick < other.endTick() && other.startTick < newEndTick)
                return false;
        return true;
    }
    bool loopTo(int id, int newEndTick) {
        if (!canLoop(id, newEndTick)) return false;
        for (auto &r : _regions) if (r.id == id) {
            r.loopLengthTicks = newEndTick - r.startTick == r.duration() ? 0 :
                                newEndTick - r.startTick;
            return true;
        }
        return false;
    }
    bool clearLoop(int id) {
        for (auto &r : _regions) if (r.id == id && r.loopLengthTicks != 0) {
            r.loopLengthTicks = 0; return true;
        }
        return false;
    }
    int addInitial(int trackIndex, int sourceStartTick, int sourceEndTick) {
        if (trackIndex < 0 || sourceStartTick < 0 || sourceEndTick <= sourceStartTick) return 0;
        const int id = _nextId++;
        _regions.push_back({id, trackIndex, sourceStartTick, sourceStartTick, sourceEndTick});
        _managedTracks.insert(trackIndex);
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
    int split(int id, int timelineTick,
              CrossingNoteMode behavior = CrossingNoteMode::Keep) {
        if (!validCrossingNoteMode(behavior)) return 0;
        for (auto &r : _regions) {
            if (r.id != id || r.loopLengthTicks > 0 ||
                timelineTick <= r.startTick || timelineTick >= r.endTick()) continue;
            const int sourceSplit = r.sourceStartTick + (timelineTick - r.startTick);
            const int oldEnd = r.sourceEndTick;
            const CrossingNoteMode previousEndMode = r.endNoteMode;
            r.sourceEndTick = sourceSplit;
            r.endNoteMode = behavior;
            const int newId = _nextId++;
            _regions.push_back({newId, r.trackIndex, timelineTick, sourceSplit,
                                oldEnd, behavior, previousEndMode, 0, r.quantize});
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
            static_cast<int64_t>(newStartTick) + target->displayDuration() >
                std::numeric_limits<int>::max()) return false;
        const int64_t newEndTick = static_cast<int64_t>(newStartTick) + target->displayDuration();
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
    // An independent copy uses the same source interval but its own region ID.
    bool canDuplicate(int id, int newStartTick) const {
        const MidiRegion *target = find(id);
        if (!target || newStartTick < 0) return false;
        const int64_t newEnd = static_cast<int64_t>(newStartTick) + target->displayDuration();
        if (newEnd > std::numeric_limits<int>::max()) return false;
        for (const auto &other : _regions) {
            if (other.trackIndex == target->trackIndex &&
                newStartTick < other.endTick() && other.startTick < newEnd) return false;
        }
        return true;
    }
    int duplicate(int id, int newStartTick) {
        if (!canDuplicate(id, newStartTick)) return 0;
        const MidiRegion source = *find(id); // snapshot before vector growth
        const int newId = _nextId++;
        _regions.push_back({newId, source.trackIndex, newStartTick,
                            source.sourceStartTick, source.sourceEndTick,
                            source.startNoteMode, source.endNoteMode,
                            source.loopLengthTicks, source.quantize});
        return newId;
    }
    // Trim the left boundary while preserving the source/timeline offset.
    // This cuts or reveals source material; it does NOT time-stretch notes.
    bool canTrimLeft(int id, int newStartTick) const {
        const MidiRegion *r = find(id);
        if (!r || newStartTick < 0 || newStartTick >= r->endTick()) return false;
        const int64_t newSourceStart = static_cast<int64_t>(r->sourceStartTick) +
                                       newStartTick - r->startTick;
        if (newSourceStart < 0 || newSourceStart >= r->sourceEndTick) return false;
        for (const auto &other : _regions) {
            if (other.id == id || other.trackIndex != r->trackIndex) continue;
            if (newStartTick < other.endTick() && other.startTick < r->endTick()) return false;
        }
        return true;
    }
    bool trimLeft(int id, int newStartTick) {
        if (!canTrimLeft(id, newStartTick)) return false;
        for (auto &r : _regions) {
            if (r.id == id) {
                const int delta = newStartTick - r.startTick;
                r.sourceStartTick += delta;
                r.startTick = newStartTick;
                // Trimming the left edge intentionally starts a new note where
                // a sustained source note enters the shortened region.
                r.startNoteMode = CrossingNoteMode::Split;
                r.loopLengthTicks = 0;
                return true;
            }
        }
        return false;
    }
    // Trim the right boundary without changing the first note's placement.
    // The source limit is the last available source MIDI tick on this track.
    // Passing a source limit prevents extending into unbounded empty material.
    bool canTrimRight(int id, int newEndTick, int maxSourceTick) const {
        const MidiRegion *r = find(id);
        if (!r || newEndTick <= r->startTick || maxSourceTick < r->sourceEndTick)
            return false;
        const int64_t newSourceEnd = static_cast<int64_t>(r->sourceEndTick) +
                                     newEndTick - r->endTick();
        if (newSourceEnd <= r->sourceStartTick || newSourceEnd > maxSourceTick)
            return false;
        for (const auto &other : _regions) {
            if (other.id == id || other.trackIndex != r->trackIndex) continue;
            if (r->startTick < other.endTick() && other.startTick < newEndTick) return false;
        }
        return true;
    }
    bool trimRight(int id, int newEndTick, int maxSourceTick) {
        if (!canTrimRight(id, newEndTick, maxSourceTick)) return false;
        for (auto &r : _regions) {
            if (r.id == id) {
                r.sourceEndTick += newEndTick - r.endTick();
                // The trimmed region stops notes at its new right edge.
                r.endNoteMode = CrossingNoteMode::Shorten;
                r.loopLengthTicks = 0;
                return true;
            }
        }
        return false;
    }
    // Identity means no clip, move, or duplicate changes the source track's
    // note schedule. Adjacent unchanged splits still count as identity.
    // A track with zero regions is passed through unchanged by the renderer.
    bool trackIsIdentity(int trackIndex, int sourceEndTick) const {
        std::vector<MidiRegion> track;
        for (const auto &r : _regions) {
            if (r.trackIndex == trackIndex) {
                if (r.startTick != r.sourceStartTick) return false;
                track.push_back(r);
            }
        }
        if (track.empty()) return !isManagedTrack(trackIndex);
        std::sort(track.begin(), track.end(), [](const auto &a, const auto &b) {
            return a.sourceStartTick < b.sourceStartTick;
        });
        int sourceCursor = 0;
        for (const auto &r : track) {
            // Split and Shorten change the rendered MIDI note schedule even
            // if the region positions still match their original source.
            if (r.endNoteMode != CrossingNoteMode::Keep || r.loopLengthTicks != 0 ||
                r.quantize.active())
                return false;
            if (r.sourceStartTick != sourceCursor) return false;
            sourceCursor = r.sourceEndTick;
        }
        return sourceCursor >= sourceEndTick;
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
        for (const auto &r : incoming) _managedTracks.insert(r.trackIndex);
        _nextId = highestId + 1;
        return true;
    }
private:
    std::vector<MidiRegion> _regions;
    std::set<int> _managedTracks;
    int _nextId = 1;
};
} // namespace xiv
#endif
