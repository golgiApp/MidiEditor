// Standalone tests for the non-destructive region model's snapshot restoration.
// UI Protocol integration and Ctrl+Z / Ctrl+Y shortcuts require a Windows Qt build.
#include "../src/region/MidiRegionModel.h"
#include <cassert>
#include <vector>
#include <iostream>

int main() {
    xiv::MidiRegionModel model;
    const int initial = model.addInitial(0, 0, 3840);
    assert(initial != 0);
    const auto before = model.regions();
    const int right = model.split(initial, 960);
    assert(right != 0);
    const auto after = model.regions();
    assert(after.size() == 2);
    assert(model.find(initial)->endTick() == 960);
    assert(model.find(right)->startTick == 960);
    assert(model.find(right)->endTick() == 3840);
    assert(model.replaceValidated(before, 1)); // undo
    assert(model.regions().size() == 1);
    assert(model.find(initial)->endTick() == 3840);
    assert(model.replaceValidated(after, 1)); // redo
    assert(model.regions().size() == 2);
    assert(model.find(right)->startTick == 960);
    const auto secondBefore = model.regions();
    const int tail = model.split(right, 1920);
    assert(tail != 0);
    const auto secondAfter = model.regions();
    assert(secondAfter.size() == 3);
    assert(model.replaceValidated(secondBefore, 1)); // undo second split
    assert(model.regions().size() == 2);
    assert(model.replaceValidated(before, 1)); // undo first split
    assert(model.regions().size() == 1);
    assert(model.replaceValidated(after, 1)); // redo first split
    assert(model.regions().size() == 2);
    assert(model.replaceValidated(secondAfter, 1)); // redo second split
    assert(model.regions().size() == 3);
    xiv::ProjectedNote first, second;
    assert(xiv::projectNote(*model.find(initial), 900, 1020, first));
    assert(xiv::projectNote(*model.find(right), 900, 1020, second));
    assert(first.startTick == 900 && first.endTick == 960);
    assert(second.startTick == 960 && second.endTick == 1020);
    std::cout << "Region snapshot restore, redo and boundary tests passed\n";
}
