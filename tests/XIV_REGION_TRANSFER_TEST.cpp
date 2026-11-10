#include "../src/region/MidiRegionTransfer.h"
#include <cassert>
#include <iostream>

int main() {
    xiv::MidiRegionModel model;
    const int left = model.addInitial(0, 0, 100);
    const int right = model.addInitial(0, 200, 300);
    const int upper = model.addInitial(1, 0, 100);
    const std::vector<xiv::SourceRegionNote> source{
        {1, 0, 10, 20, 60, 80, 2},
        {2, 0, 240, 270, 62, 90, 2},
        {3, 1, 15, 25, 64, 100, 3}
    };
    const auto initial = model;
    assert(xiv::consolidateRegions(model, {left, right}, left, 0, 0, -1, 3, source));
    assert(model.regions().size() == 2);
    assert(model.find(left)->endTick() == 300);
    auto notes = xiv::renderRegionNotes(source, model);
    assert(notes.size() == 3);
    assert(notes[0].startTick == 10 && notes[2].startTick == 240);
    assert(notes[2].regionId == left && notes[2].channel == 2 && notes[2].velocity == 90);

    model = initial;
    assert(!xiv::consolidateRegions(model, {left}, left, 1, 0, 3, 3, source));
    assert(!xiv::consolidateRegions(model, {left}, left, 2, 0, -1, 3, source));
    assert(model.regions().size() == 3);
    assert(xiv::consolidateRegions(model, {left, upper}, upper, 1, 0, 3, 3, source));
    notes = xiv::renderRegionNotes(source, model);
    assert(notes.size() == 3);
    assert(notes[0].trackIndex == 1 && notes[0].channel == 3 && notes[0].startTick == 10);
    assert(notes[1].trackIndex == 1 && notes[1].channel == 3 && notes[1].startTick == 15);
    assert(notes[2].trackIndex == 0 && notes[2].startTick == 240);

    model = initial;
    assert(model.loopTo(left, 170));
    assert(xiv::consolidateRegions(model, {left}, left, 2, 310, 6, 3, source));
    notes = xiv::renderRegionNotes(source, model);
    assert(notes.size() == 4);
    assert(notes[2].trackIndex == 2 && notes[2].startTick == 320 && notes[2].channel == 6);
    assert(notes[3].trackIndex == 2 && notes[3].startTick == 420);
    assert(model.find(left)->endTick() == 480);
    assert(model.isManagedTrack(0) && model.isManagedTrack(2));
    std::cout << "Horizontal and vertical joins, routing, overlap checks, and loop transfer passed\n";
}
