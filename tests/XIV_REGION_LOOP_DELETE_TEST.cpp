#include "../src/region/MidiRegionModel.h"
#include "../src/region/MidiRegionRender.h"
#include <cassert>
#include <iostream>
#include <vector>

static void testGroupMoveAndLoop() {
    xiv::MidiRegionModel model;
    const int melody = model.addInitial(0, 20, 120);
    const int bass = model.addInitial(1, 60, 140);
    const int nextBass = model.addInitial(1, 230, 290);
    const std::set<int> group{melody, bass};
    assert(model.rename(bass, "Bass"));
    assert(model.setQuantize(bass, {24, 75, 50}));

    assert(!model.moveSelected(group, -21));
    assert(model.find(melody)->startTick == 20);
    assert(model.find(bass)->startTick == 60);
    assert(model.moveSelected(group, 40));
    assert(model.find(melody)->startTick == 60);
    assert(model.find(bass)->startTick == 100);
    assert(model.find(bass)->trackIndex == 1);
    assert(model.find(bass)->name == "Bass");
    assert(model.find(bass)->quantize.active());
    assert(model.find(nextBass)->startTick == 230);

    const std::set<int> loopGroup{melody, bass, nextBass};
    assert(model.blockedLoopTracks(loopGroup, 51) == std::set<int>{1});
    assert(model.loopSelected(loopGroup, 51));
    assert(model.find(melody)->endTick() == 211);
    assert(model.find(bass)->endTick() == 180);
    assert(model.find(nextBass)->endTick() == 290);
    assert(!model.loopSelected({bass, nextBass}, 51));
    assert(model.loopSelected({melody}, -51));
    assert(model.loopSelected(group, 50));
    assert(model.find(melody)->endTick() == 210);
    assert(model.find(bass)->endTick() == 230);
    const std::vector<xiv::SourceRegionNote> source = {
        {1, 0, 40, 90, 72, 100, 0}, {2, 1, 80, 130, 48, 100, 1}
    };
    const auto notes = xiv::renderRegionNotes(source, model);
    assert(std::count_if(notes.begin(), notes.end(), [melody](const auto &note) {
        return note.regionId == melody;
    }) == 2);
    assert(std::count_if(notes.begin(), notes.end(), [bass](const auto &note) {
        return note.regionId == bass;
    }) == 2);
    assert(model.loopSelected(group, -50));
    assert(model.find(melody)->loopLengthTicks == 0);
    assert(model.find(bass)->loopLengthTicks == 0);
}

int main() {
    testGroupMoveAndLoop();
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
