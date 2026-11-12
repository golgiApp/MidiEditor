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
#include <memory>
#include <utility>
#include <set>
#include <string>
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

struct OwnedRegionNote {
    int id;
    // Use the region's source coordinate space so trimming is reversible.
    int startTick;
    int endTick;
    int pitch;
    int velocity;
    int channel;

    bool valid() const {
        return id > 0 && startTick >= 0 && endTick > startTick &&
               pitch >= 0 && pitch < 128 && velocity > 0 && velocity < 128 &&
               channel >= 0 && channel < 16;
    }
};

inline bool validOwnedNotes(const std::vector<OwnedRegionNote> &notes) {
    std::set<int> ids;
    return std::all_of(notes.begin(), notes.end(), [&ids](const auto &note) {
        return note.valid() && ids.insert(note.id).second;
    });
}

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
    // Null uses track source notes. An allocated empty list means silence.
    // Replacing the immutable list makes editing a duplicate independent.
    std::shared_ptr<const std::vector<OwnedRegionNote>> ownedNotes{};
    std::string name;

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
    bool rename(int id, std::string name) {
        for (auto &region : _regions) {
            if (region.id != id || region.name == name) continue;
            region.name = std::move(name);
            return true;
        }
        return false;
    }
    bool replaceOwnedNotes(int id, std::vector<OwnedRegionNote> notes) {
        if (!validOwnedNotes(notes)) return false;
        for (auto &region : _regions) {
            if (region.id != id) continue;
            region.ownedNotes = std::make_shared<const std::vector<OwnedRegionNote>>(std::move(notes));
            return true;
        }
        return false;
    }
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
    int groupLoopEnd(const MidiRegion &region, int delta) const {
        const int currentEnd = region.endTick();
        if (delta == 0) return currentEnd;
        const int baseEnd = region.startTick + region.duration();
        int64_t limit = std::min<int64_t>(std::numeric_limits<int>::max(),
            region.startTick + int64_t{10001} * region.duration() - 1);
        for (const auto &other : _regions) {
            if (other.id == region.id || other.trackIndex != region.trackIndex) continue;
            if (other.startTick >= region.startTick) {
                limit = std::min<int64_t>(limit, other.startTick);
            } else if (other.endTick() > region.startTick) {
                return currentEnd;
            }
        }
        const auto requestedEnd = std::max<int64_t>(baseEnd,
            static_cast<int64_t>(currentEnd) + delta);
        if (delta > 0) return static_cast<int>(std::max<int64_t>(
            currentEnd, std::min(requestedEnd, limit)));
        return requestedEnd <= limit ? static_cast<int>(requestedEnd) : currentEnd;
    }
    bool loopSelected(const std::set<int> &ids, int delta) {
        if (delta == 0) return false;
        std::vector<std::pair<MidiRegion *, int>> edits;
        for (auto &region : _regions) {
            if (!ids.count(region.id)) continue;
            const int end = groupLoopEnd(region, delta);
            if (end != region.endTick()) edits.emplace_back(&region, end);
        }
        for (const auto &[region, end] : edits) {
            const int length = end - region->startTick;
            region->loopLengthTicks = length == region->duration() ? 0 : length;
        }
        return !edits.empty();
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
        for (auto it = _regions.crbegin(); it != _regions.crend(); ++it)
            if (it->trackIndex == trackIndex && it->contains(tick)) return &*it;
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
                                oldEnd, behavior, previousEndMode, 0, r.quantize, r.ownedNotes, r.name});
            return newId;
        }
        return 0;
    }
    // The list order is the paint order. A move places its region on top
    // without changing the notes or other regions underneath.
    bool canMove(int id, int newStartTick) const {
        const auto *region = find(id);
        return region && newStartTick >= 0 &&
               static_cast<int64_t>(newStartTick) + region->displayDuration() <=
                   std::numeric_limits<int>::max();
    }
    bool move(int id, int newStartTick) {
        if (!canMove(id, newStartTick)) return false;
        const auto it = std::find_if(_regions.begin(), _regions.end(),
            [id](const auto &region) { return region.id == id; });
        it->startTick = newStartTick;
        std::rotate(it, it + 1, _regions.end());
        return true;
    }
    bool canMoveSelected(const std::set<int> &ids, int delta) const {
        if (ids.empty()) return false;
        for (int id : ids) {
            const auto *region = find(id);
            if (!region) return false;
            const auto start = static_cast<int64_t>(region->startTick) + delta;
            if (start < 0 || start + region->displayDuration() >
                std::numeric_limits<int>::max()) return false;
        }
        return true;
    }
    bool moveSelected(const std::set<int> &ids, int delta) {
        if (delta == 0 || !canMoveSelected(ids, delta)) return false;
        for (auto &region : _regions) {
            if (ids.count(region.id)) region.startTick += delta;
        }
        std::stable_partition(_regions.begin(), _regions.end(),
            [&ids](const auto &region) { return !ids.count(region.id); });
        return true;
    }
    // Move a time range across every track while preserving region order.
    bool moveContainedRegions(int start, int end, int destination) {
        if (start < 0 || end <= start || destination < 0 ||
            static_cast<int64_t>(destination) + (end - start) >
                std::numeric_limits<int>::max()) return false;
        const int delta = destination - start;
        for (const auto &region : _regions) {
            if (region.startTick >= end || region.endTick() <= start) continue;
            if (region.startTick < start || region.endTick() > end) return false;
        }
        for (auto &region : _regions) {
            if (region.startTick >= start && region.endTick() <= end)
                region.startTick += delta;
        }
        return true;
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
                            source.loopLengthTicks, source.quantize, source.ownedNotes, source.name});
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
        if (!r || newEndTick <= r->startTick) return false;
        // Owned regions can grow into silence, including a newly created clip.
        if (r->ownedNotes) maxSourceTick = std::numeric_limits<int>::max();
        if (maxSourceTick < r->sourceEndTick) return false;
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
                if (r.ownedNotes || r.startTick != r.sourceStartTick) return false;
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
        std::set<const std::vector<OwnedRegionNote> *> checkedNoteLists;
        for (const auto &r : incoming) {
            if (!r.valid() || r.trackIndex >= trackCount || r.id > 100000000) return false;
            if (r.ownedNotes && checkedNoteLists.insert(r.ownedNotes.get()).second &&
                !validOwnedNotes(*r.ownedNotes)) return false;
            ids.push_back(r.id);
            highestId = std::max(highestId, r.id);
        }
        std::sort(ids.begin(), ids.end());
        if (std::adjacent_find(ids.begin(), ids.end()) != ids.end()) return false;
        // Preserve paint order and allow independent regions to overlap.
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
