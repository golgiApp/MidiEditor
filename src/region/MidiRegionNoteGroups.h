#ifndef MIDI_REGION_NOTE_GROUPS_H
#define MIDI_REGION_NOTE_GROUPS_H

#include "MidiRegionModel.h"
#include <map>
#include <tuple>

namespace xiv {

using RegionNoteGroup = std::vector<OwnedRegionNote>;

inline std::map<int, RegionNoteGroup> groupRegionNotesByChannel(const RegionNoteGroup &notes) {
    std::map<int, RegionNoteGroup> groups;
    for (const auto &note : notes) groups[note.channel].push_back(note);
    return groups;
}

struct RegionChordVoices {
    RegionNoteGroup remaining;
    // Keyed by channel and pitch rank, with rank zero the highest note.
    std::map<std::pair<int, std::size_t>, RegionNoteGroup> voices;
};

inline RegionChordVoices explodeRegionChords(const RegionNoteGroup &notes) {
    std::map<std::pair<int, int>, RegionNoteGroup> onsets;
    for (const auto &note : notes) onsets[{note.channel, note.startTick}].push_back(note);
    RegionChordVoices result;
    for (auto &[onset, chord] : onsets) {
        if (chord.size() < 2 || onset.first == 9) {
            result.remaining.insert(result.remaining.end(), chord.begin(), chord.end());
            continue;
        }
        std::sort(chord.begin(), chord.end(), [](const auto &a, const auto &b) {
            return std::tie(a.pitch, a.id) > std::tie(b.pitch, b.id);
        });
        for (std::size_t rank = 0; rank < chord.size(); ++rank)
            result.voices[{onset.first, rank}].push_back(chord[rank]);
    }
    return result;
}

} // namespace xiv
#endif
