// Standalone regression tests: g++ -std=c++17 -Wall -Wextra -pedantic -o ... thisfile.cpp
#include "../src/region/MidiRegionModel.h"
#include <cassert>
#include <iostream>
using xiv::MidiRegionModel;
using xiv::ProjectedNote;
using xiv::projectNote;
int main() {
    MidiRegionModel model;
    const int a = model.addInitial(0, 100, 500);
    assert(a > 0);
    assert(model.split(a, 100) == 0);
    assert(model.split(a, 500) == 0);
    const int b = model.split(a, 300);
    assert(b > 0 && model.regions().size() == 2);
    assert(model.find(a)->sourceStartTick == 100 && model.find(a)->sourceEndTick == 300);
    assert(model.find(b)->sourceStartTick == 300 && model.find(b)->sourceEndTick == 500);
    assert(model.find(a)->endTick() == model.find(b)->startTick);
    ProjectedNote p;
    // Boundary-touching notes belong to exactly one half.
    assert(projectNote(*model.find(a), 200, 300, p) && p.startTick == 200 && p.endTick == 300);
    assert(!projectNote(*model.find(b), 200, 300, p));
    assert(!projectNote(*model.find(a), 300, 350, p));
    assert(projectNote(*model.find(b), 300, 350, p) && p.startTick == 300);
    // A sustained note across a split gets two correct nonzero fragments.
    assert(projectNote(*model.find(a), 250, 350, p) && p.startTick == 250 && p.endTick == 300);
    assert(projectNote(*model.find(b), 250, 350, p) && p.startTick == 300 && p.endTick == 350);
    // No zero-length or inverted notes.
    assert(!projectNote(*model.find(a), 100, 100, p));
    assert(!projectNote(*model.find(b), 400, 350, p));
    // Splitting the right fragment preserves the source/timeline mapping.
    const int c = model.split(b, 420);
    assert(c > b && model.find(b)->endTick() == 420);
    assert(model.find(c)->startTick == 420 && model.find(c)->sourceEndTick == 500);
    // Invalid sidecar (bad track, duplicates, zero-length) cannot mutate state.
    const auto valid = model.regions();
    auto wrongTrack = valid;
    wrongTrack.front().trackIndex = 10;
    assert(!model.replaceValidated(wrongTrack, 1) && model.regions().size() == valid.size());
    auto duplicate = valid;
    duplicate.back().id = duplicate.front().id;
    assert(!model.replaceValidated(duplicate, 1));
    auto zero = valid;
    zero.back().sourceEndTick = zero.back().sourceStartTick;
    assert(!model.replaceValidated(zero, 1));
    auto overlap = valid;
    overlap.back().startTick = 210;
    assert(model.replaceValidated(overlap, 1));
    assert(model.replaceValidated(valid, 1));
    const auto id = model.addInitial(2, 500, 800);
    assert(id > c);
    assert(model.at(2, 600)->id == id && !model.at(2, 800));
    std::cout << "XIV MIDI region model: 24 checks passed\n";
}
