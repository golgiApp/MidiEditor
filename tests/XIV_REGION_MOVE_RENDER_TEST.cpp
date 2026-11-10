#include "../src/region/MidiRegionRender.h"
#include <cassert>
#include <climits>
#include <vector>
#include <iostream>

using namespace xiv;
int main() {
    MidiRegionModel m;
    const int a = m.addInitial(0, 0, 960);
    assert(a);
    const int b = m.split(a, 480);
    assert(b);
    assert(m.regions().size() == 2);
    assert(m.find(a)->startTick == 0 && m.find(a)->endTick() == 480);
    assert(m.find(b)->startTick == 480 && m.find(b)->endTick() == 960);
    const std::vector<SourceRegionNote> notes = {
        {1, 0, 100, 700, 60, 90, 0}, // crosses the split
        {2, 0, 500, 600, 64, 80, 0}, // entirely in right half
        {3, 1, 200, 400, 36, 100, 9} // independent unregioned drum track
    };
    auto initial = renderRegionNotes(notes, m);
    assert(initial.size() == 3);
    assert(initial[0].sourceId == 1 && initial[0].startTick == 100 && initial[0].endTick == 700);
    assert(initial[1].sourceId == 3 && initial[1].startTick == 200 && initial[1].endTick == 400);
    assert(initial[2].sourceId == 2 && initial[2].startTick == 500 && initial[2].endTick == 600);
    assert(!m.canMove(a, 490));  // overlap right half
    assert(!m.move(a, -1));
    assert(!m.move(a, INT_MAX));
    assert(!m.move(a, 720));    // would overlap right half
    assert(m.move(b, 960));     // leaves silence after left half
    assert(m.find(b)->startTick == 960 && m.find(b)->sourceStartTick == 480);
    auto moved = renderRegionNotes(notes, m);
    assert(moved.size() == 3); // Keep: cross-boundary note stays continuous
    assert(moved[0].sourceId == 1 && moved[0].startTick == 100 && moved[0].endTick == 700);
    assert(moved[1].sourceId == 3 && moved[1].startTick == 200 && moved[1].endTick == 400);
    assert(moved[2].sourceId == 2 && moved[2].startTick == 980 && moved[2].endTick == 1080);
    assert(m.move(b, 480));
    const auto back = renderRegionNotes(notes, m);
    assert(back.size() == 3 && back[0].endTick == 700);
    assert(!m.duplicate(a, 240)); // overlaps first region
    const int c = m.duplicate(a, 960);
    assert(c && c != a && c != b);
    assert(m.find(c)->startTick == 960 && m.find(c)->sourceStartTick == 0);
    assert(m.regions().size() == 3);
    assert(!m.duplicate(b, 1000)); // overlaps clone
    auto duplicated = renderRegionNotes(notes, m);
    assert(duplicated.size() == 4); // adds only the first fragment of source note 1
    assert(duplicated.back().sourceId == 1 && duplicated.back().startTick == 1060 && duplicated.back().endTick == 1660);
    assert(m.replaceValidated(m.regions(), 2));
    assert(!m.move(a, 970));
    assert(m.move(c, 1440));
    assert(m.find(c)->startTick == 1440);
    assert(m.find(c)->endTick() == 1920);
    assert(!m.move(c, INT_MAX - 1));
    const std::vector<SourceRegionNote> invalid = {{0,0,0,10,10,90,0},{7,0,40,40,10,90,0}};
    assert(renderRegionNotes(invalid, m).empty());
    std::cout << "Region move/duplicate/render tests passed\n";
}
