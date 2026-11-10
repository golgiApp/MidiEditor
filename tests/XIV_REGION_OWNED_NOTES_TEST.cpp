#include "../src/region/MidiRegionRender.h"
#include <cassert>
#include <iostream>

int main() {
    xiv::MidiRegionModel model;
    const int original = model.addInitial(0, 100, 200);
    const int duplicate = model.duplicate(original, 220);
    const std::vector<xiv::SourceRegionNote> source{{1, 0, 120, 160, 60, 80, 2}};
    assert(model.replaceOwnedNotes(duplicate, {{7, 130, 170, 66, 81, 2}}));
    assert(!model.trackIsIdentity(0, 200));
    auto notes = xiv::renderRegionNotes(source, model);
    assert(notes.size() == 2);
    assert(notes[0].startTick == 120 && notes[0].sourceId == 1);
    assert(notes[1].startTick == 250 && notes[1].endTick == 290);
    assert(notes[1].regionId == duplicate && notes[1].ownedNoteId == 7);
    assert(notes[1].sourceId == 0 && notes[1].pitch == 66 && notes[1].velocity == 81);

    const auto snapshot = model;
    const int copy = model.duplicate(duplicate, 350);
    assert(model.replaceOwnedNotes(copy, {}));
    assert(xiv::renderRegionNotes(source, model).size() == 2);
    assert(!model.replaceOwnedNotes(duplicate, {{1, 120, 130, 60, 80, 16}}));
    assert(!model.replaceOwnedNotes(duplicate, {{1, 120, 130, 60, 80, 2}, {1, 130, 140, 61, 80, 2}}));

    model = snapshot;
    assert(model.move(duplicate, 300));
    assert(model.loopTo(duplicate, 550));
    notes = xiv::renderRegionNotes(source, model);
    assert(notes.size() == 4);
    assert(notes.back().regionId == duplicate && notes.back().startTick == 530);
    assert(notes.back().endTick == 550 && notes.back().ownedNoteId == 7);
    assert(xiv::renderRegionNotes(source, model, 1).empty());

    model = snapshot;
    const int right = model.split(duplicate, 270, xiv::CrossingNoteMode::Split);
    notes = xiv::renderRegionNotes(source, model);
    assert(notes.size() == 3);
    assert(notes[1].endTick == 270 && notes[2].startTick == 270 && notes[2].regionId == right);
    assert(model.replaceOwnedNotes(right, {}));
    assert(xiv::renderRegionNotes(source, model).size() == 2);

    xiv::MidiRegionModel emptyTrack;
    const int empty = emptyTrack.addInitial(0, 0, 100);
    assert(emptyTrack.replaceOwnedNotes(empty, {}));
    assert(!emptyTrack.trackIsIdentity(0, 100));
    assert(xiv::renderRegionNotes({{1, 0, 10, 20, 60, 80, 2}}, emptyTrack).empty());
    std::cout << "Owned note isolation, empty regions, moves, splits, and loops passed\n";
}
