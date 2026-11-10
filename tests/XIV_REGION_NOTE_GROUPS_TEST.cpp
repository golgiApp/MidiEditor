#include "../src/region/MidiRegionNoteGroups.h"
#include <cassert>
#include <iostream>

int main() {
    const xiv::RegionNoteGroup notes{
        {1, 0, 40, 60, 70, 2},
        {2, 0, 20, 72, 90, 2},
        {3, 48, 80, 65, 80, 2},
        {4, 0, 12, 40, 100, 9},
        {5, 0, 16, 42, 100, 9},
        {6, 0, 16, 55, 60, 3}
    };

    const auto channels = xiv::groupRegionNotesByChannel(notes);
    assert(channels.size() == 3);
    assert(channels.at(2).size() == 3);
    assert(channels.at(3).front().id == 6);
    assert(channels.at(9).size() == 2);

    const auto split = xiv::explodeRegionChords(notes);
    assert(split.voices.size() == 2);
    const auto &high = split.voices.at({2, 0}).front();
    const auto &low = split.voices.at({2, 1}).front();
    assert(high.id == 2 && high.endTick == 20 && high.velocity == 90);
    assert(low.id == 1 && low.endTick == 40 && low.velocity == 70);
    assert(split.remaining.size() == 4);
    for (const auto &note : split.remaining) assert(note.id >= 3);
    std::cout << "Region channel and chord grouping passed\n";
}
