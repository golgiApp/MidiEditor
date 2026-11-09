#include "../src/support/GMDrumMap.h"
#include <cassert>
#include <string>
int main() {
    assert(GMDrumMap::nameForNote(35) && std::string(GMDrumMap::nameForNote(35)) == "Acoustic Bass Drum");
    assert(std::string(GMDrumMap::nameForNote(36)) == "Bass Drum 1");
    assert(std::string(GMDrumMap::nameForNote(38)) == "Acoustic Snare");
    assert(std::string(GMDrumMap::nameForNote(42)) == "Closed Hi-Hat");
    assert(std::string(GMDrumMap::nameForNote(46)) == "Open Hi-Hat");
    assert(std::string(GMDrumMap::nameForNote(81)) == "Open Triangle");
    for (int note = 35; note <= 81; ++note) assert(GMDrumMap::nameForNote(note) != nullptr);
    assert(GMDrumMap::nameForNote(34) == nullptr);
    assert(GMDrumMap::nameForNote(82) == nullptr);
    return 0;
}
