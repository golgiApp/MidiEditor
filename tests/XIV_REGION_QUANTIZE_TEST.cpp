#include "../src/region/MidiRegionRender.h"

#include <cassert>
#include <iostream>

int main() {
    xiv::MidiRegionModel model;
    const int region = model.addInitial(0, 0, 480);
    assert(model.move(region, 13));
    const std::vector<xiv::SourceRegionNote> source = {
        {1, 0, 85, 105, 60, 100, 0},
        {2, 0, 360, 440, 64, 100, 0}
    };

    const auto original = xiv::renderRegionNotes(source, model);
    assert(original[0].startTick == 98 && original[0].endTick == 118);
    assert(model.setQuantize(region, {120, 50, 50}));
    const auto halfway = xiv::renderRegionNotes(source, model);
    assert(halfway[0].startTick == 116 && halfway[0].endTick == 136);
    assert(!model.trackIsIdentity(0, 480));

    assert(model.setQuantize(region, {120, 100, 60}));
    const auto swung = xiv::renderRegionNotes(source, model);
    assert(swung[0].startTick == 157 && swung[0].endTick == 177);
    assert(swung[1].startTick == 397 && swung[1].endTick == 477);
    const auto snapshot = model;
    assert(model.setQuantize(region, {}));
    const auto reset = xiv::renderRegionNotes(source, model);
    assert(reset[0].startTick == original[0].startTick);
    assert(reset[0].endTick == original[0].endTick);
    model = snapshot;

    assert(model.loopTo(region, 593));
    const auto looped = xiv::renderRegionNotes(source, model);
    assert(looped.size() == 3);
    assert(looped[2].regionId == region);
    assert(looped[2].startTick == 592 && looped[2].endTick == 593);
    assert(model.remove(region));
    assert(xiv::renderRegionNotes(source, model).empty());

    model = snapshot;
    const int split = model.split(region, 253, xiv::CrossingNoteMode::Shorten);
    assert(split && model.find(split)->quantize == snapshot.find(region)->quantize);
    const int copy = model.duplicate(split, 500);
    assert(copy && model.find(copy)->quantize == snapshot.find(region)->quantize);

    xiv::MidiRegionModel identity;
    const int aligned = identity.addInitial(0, 0, 480);
    assert(identity.trackIsIdentity(0, 480));
    assert(identity.setQuantize(aligned, {120, 100, 50}));
    assert(!identity.trackIsIdentity(0, 480));
    assert(identity.setQuantize(aligned, {120, 0, 50}));
    assert(identity.trackIsIdentity(0, 480));
    assert(!identity.setQuantize(aligned, {-1, 100, 50}));

    xiv::MidiRegionModel restored;
    restored.markManagedTrack(0);
    assert(restored.replaceValidated({}, 1));
    assert(!restored.trackIsIdentity(0, 480));
    assert(xiv::renderRegionNotes(source, restored).empty());

    std::cout << "Region quantization, split/copy/loop, identity, and managed-track tests passed\n";
}
