#include "../src/region/MidiRegionModel.h"
#include "../src/region/MidiRegionRender.h"
#include <algorithm>
#include <cassert>
#include <iostream>
#include <map>
#include <vector>

int main() {
    xiv::MidiRegionModel model;
    const int id = model.addInitial(0, 0, 768);  // Four beats, 192 TPQ.
    assert(id > 0);
    const int second = model.split(id, 384);
    assert(second > 0);
    std::vector<xiv::SourceRegionNote> notes{
        {1, 0, 0, 96, 60, 100, 0},
        {2, 0, 300, 470, 64, 90, 0}, // crosses the boundary
        {3, 0, 400, 450, 67, 90, 0},
        {4, 1, 100, 250, 42, 100, 9}, // percussion track unchanged
    };
    auto untouched = xiv::renderRegionNotes(notes, model);
    assert(untouched.size() == notes.size());
    auto findNote = [](const auto &events, int sourceId) {
        return std::find_if(events.begin(), events.end(), [sourceId](const auto &event) {
            return event.sourceId == sourceId;
        });
    };
    assert(findNote(untouched, 2)->startTick == 300);
    assert(findNote(untouched, 2)->endTick == 470);
    assert(model.canMove(second, 768));
    assert(model.move(second, 768));
    auto moved = xiv::renderRegionNotes(notes, model);
    assert(moved.size() == 4); // Keep: sustained note is not retriggered after move
    auto first = std::find_if(moved.begin(), moved.end(), [](const auto &n) {
        return n.sourceId == 2 && n.startTick == 300;
    });
    auto later = std::find_if(moved.begin(), moved.end(), [](const auto &n) {
        return n.sourceId == 2 && n.startTick == 768;
    });
    assert(first != moved.end() && first->endTick == 470);
    assert(later == moved.end());
    assert(findNote(moved, 3)->startTick == 784);
    assert(findNote(moved, 4)->startTick == 100);
    // The source interval stays at 384..768 while playback shifts to 768..1152.
    assert(model.find(second)->sourceStartTick == 384);
    assert(model.find(second)->startTick == 768);
    assert(model.canMove(second, 200)); // may overlap the first region
    const int duplicate = model.duplicate(second, 1152);
    assert(duplicate > 0);
    auto duplicated = xiv::renderRegionNotes(notes, model);
    assert(duplicated.size() == 5);
    assert(duplicated.back().endTick >= 100);
    std::cout << "Phase 4B rendered-note contract tests passed\n";
}
