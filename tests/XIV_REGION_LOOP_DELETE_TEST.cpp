#include "../src/region/MidiRegionModel.h"
#include "../src/region/MidiRegionRender.h"
#include <cassert>
#include <iostream>
#include <vector>
int main() {
    xiv::MidiRegionModel model;
    const int first = model.addInitial(0, 0, 100);
    assert(first != 0 && model.isManagedTrack(0));
    assert(model.canLoop(first, 235));
    assert(model.loopTo(first, 235));
    assert(model.find(first)->loopLengthTicks == 235);
    assert(model.find(first)->endTick() == 235);
    std::vector<xiv::SourceRegionNote> source = {{1,0,10,40,42,100,9}, {2,0,75,95,46,100,9}};
    auto notes = xiv::renderRegionNotes(source, model);
    // Base notes repeat at 0,100,200; last repetition is only 35 ticks.
    assert(notes.size() == 5);
    bool firstCycle = false, secondCycle = false, partialCycle = false;
    for (const auto &n : notes) {
        assert(n.regionId == first);
        if (n.startTick == 10 && n.endTick == 40) firstCycle = true;
        if (n.startTick == 110 && n.endTick == 140) secondCycle = true;
        if (n.startTick == 210 && n.endTick == 235) partialCycle = true;
    }
    assert(firstCycle && secondCycle && partialCycle);
    assert(!model.trackIsIdentity(0,100));
    assert(model.canMove(first, 300));
    assert(model.move(first,300));
    notes = xiv::renderRegionNotes(source,model);
    assert(!notes.empty() && notes.front().startTick >= 300);
    assert(model.remove(first));
    // Deleting the last region must render SILENCE, not restore source notes.
    assert(model.isManagedTrack(0) && !model.trackIsIdentity(0,100));
    assert(xiv::renderRegionNotes(source,model).empty());
    auto restored = model;
    assert(restored.replaceValidated({{1,0,0,0,100}},1));
    assert(restored.isManagedTrack(0));
    assert(xiv::renderRegionNotes(source,restored).size()==2);
    std::cout << "Loop/full/partial, delete-last, move, and restore tests passed\n";
}
