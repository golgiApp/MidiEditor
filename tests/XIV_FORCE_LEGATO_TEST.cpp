#include "../src/region/RegionForceLegato.h"
#include <cassert>

int main() {
    std::vector<xiv::OwnedRegionNote> notes{
        {1, 100, 103, 60, 90, 0}, {2, 100, 160, 64, 80, 0},
        {3, 140, 150, 67, 70, 0}, {4, 160, 170, 69, 90, 0},
        {5, 164, 170, 71, 95, 0}
    };
    std::vector<xiv::RenderedRegionNote> visible;
    for (const auto &note : notes) {
        xiv::RenderedRegionNote rendered;
        rendered.ownedNoteId = note.id;
        rendered.sourceSliceStartTick = note.startTick;
        rendered.sourceSliceEndTick = note.endTick;
        visible.push_back(rendered);
    }
    auto overlap = notes;
    assert(xiv::adjustRegionNoteEnds(overlap, visible, {1, 2, 3, 5}, 190,
                                     xiv::RegionNoteEndMode::RemoveOverlap));
    assert(overlap[0].endTick == 103 && overlap[1].endTick == 132);
    assert(overlap[2].endTick == 150 && overlap[3].endTick == 170 && overlap[4].endTick == 170);
    assert(xiv::adjustRegionNoteEnds(notes, visible, {1, 2, 4, 5}, 190, xiv::RegionNoteEndMode::Legato));
    assert(notes[0].endTick == 132 && notes[1].endTick == 132);
    assert(notes[2].endTick == 150);
    assert(notes[3].endTick == 161);
    assert(notes[4].endTick == 190);
    assert(notes[0].startTick == 100 && notes[0].velocity == 90);
    assert(!xiv::adjustRegionNoteEnds(notes, visible, {1, 2, 4, 5}, 190, xiv::RegionNoteEndMode::Legato));
    assert(!xiv::adjustRegionNoteEnds(notes, visible, {}, 190, xiv::RegionNoteEndMode::Legato));
}
