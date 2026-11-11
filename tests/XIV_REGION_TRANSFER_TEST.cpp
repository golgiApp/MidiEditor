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
    auto section = initial;
    assert(!section.moveContainedRegions(10, 100, 350));
    assert(section.find(left)->startTick == 0 && section.find(upper)->startTick == 0);
    assert(!section.moveContainedRegions(0, 100, std::numeric_limits<int>::max()));
    assert(section.moveContainedRegions(0, 100, 351));
    const auto sectionNotes = xiv::renderRegionNotes(source, section);
    assert(section.find(left)->startTick == 351 && section.find(upper)->startTick == 351);
    assert(section.find(left)->trackIndex == 0 && section.find(upper)->trackIndex == 1);
    assert(section.find(right)->startTick == 200);
    assert(sectionNotes.size() == 3 && sectionNotes[0].startTick == 240);
    assert(sectionNotes[1].startTick == 361 && sectionNotes[2].startTick == 366);

    // Splitting across the section edges keeps the outer pieces in place.
    section = initial;
    const int middle = section.split(left, 15, xiv::CrossingNoteMode::Split);
    const int outside = section.split(middle, 18, xiv::CrossingNoteMode::Split);
    assert(middle && outside);
    assert(section.split(upper, 18, xiv::CrossingNoteMode::Split));
    assert(section.split(upper, 15, xiv::CrossingNoteMode::Split));
    assert(section.moveContainedRegions(15, 18, 351));
    assert(section.find(left)->startTick == 0 && section.find(outside)->startTick == 18);
    const auto movedNotes = xiv::renderRegionNotes(source, section);
    assert(std::count_if(movedNotes.begin(), movedNotes.end(), [](const auto &note) {
        return note.startTick == 351 && note.endTick == 354;
    }) == 2);

    auto overlay = model;
    assert(xiv::consolidateRegions(overlay, {left}, left, 1, 0, 3, 3, source));
    assert(overlay.at(1, 15)->id == left && overlay.find(upper));
    assert(xiv::renderRegionNotes(source, overlay).size() == 3);
    assert(xiv::consolidateRegions(overlay, {left, upper}, left, 1, 0, 3, 3, source));
    assert(!overlay.find(upper) && xiv::renderRegionNotes(source, overlay).size() == 3);
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
    // Split in the middle of a partial repeat. Notes after the cut retain
    // their absolute positions, with a retrigger only at the split point.
    xiv::MidiRegionModel loop;
    const int phrase = loop.addInitial(0, 0, 100);
    assert(loop.loopTo(phrase, 240));
    const std::vector<xiv::SourceRegionNote> phraseNotes{
        {1, 0, 10, 35, 60, 90, 2}, {2, 0, 30, 45, 62, 80, 2}
    };
    assert(xiv::consolidateRegions(loop, {phrase}, phrase, 0, 0, -1, 1, phraseNotes));
    const int tail = loop.split(phrase, 225, xiv::CrossingNoteMode::Split);
    assert(tail && loop.find(tail)->startTick == 225 && loop.find(tail)->endTick() == 240);
    notes = xiv::renderRegionNotes(phraseNotes, loop);
    assert(notes.size() == 7);
    assert(notes[4].startTick == 210 && notes[4].endTick == 225);
    assert(notes[5].startTick == 225 && notes[5].endTick == 235);
    assert(notes[6].startTick == 230 && notes[6].endTick == 240);
    std::cout << "Horizontal and vertical joins, routing, overlays, and loop transfer passed\n";
}
