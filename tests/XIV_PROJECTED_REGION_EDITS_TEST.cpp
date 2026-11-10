#include "../src/region/ProjectedRegionEdits.h"
#include <cassert>
#include <tuple>

static auto schedule(const std::vector<xiv::RenderedRegionNote> &notes) {
    std::vector<std::tuple<int, int, int, int, int, int>> result;
    for (const auto &note : notes)
        result.emplace_back(note.regionId, note.trackIndex, note.startTick,
                            note.endTick, note.pitch, note.velocity);
    return result;
}

int main() {
    xiv::MidiRegionModel model;
    const int first = model.addInitial(0, 0, 100);
    const int loop = model.addInitial(1, 0, 100);
    const int other = model.addInitial(2, 0, 100);
    assert(model.replaceOwnedNotes(first, {{1, 10, 50, 60, 90, 0}}));
    assert(model.replaceOwnedNotes(loop, {{1, 20, 40, 64, 80, 1}}));
    assert(model.replaceOwnedNotes(other, {{1, 5, 30, 67, 75, 2}}));
    assert(model.move(loop, 200));
    assert(model.loopTo(loop, 450));
    assert(model.setQuantize(loop, {25, 100, 50}));
    const auto original = model;
    const auto rendered = xiv::renderRegionNotes({}, model);
    assert(rendered.size() == 5);

    // Choose one note from each of two tracks. Both owned-note IDs are 1.
    std::set<int> selected;
    for (std::size_t i = 0; i < rendered.size(); ++i) {
        const auto &note = rendered[i];
        if (note.regionId == first || (note.regionId == loop && note.startTick >= 300 && note.startTick < 400))
            selected.insert(static_cast<int>(i) + 1);
    }
    assert(selected.size() == 2);
    const auto otherNotes = model.find(other)->ownedNotes;
    assert(xiv::materializeProjectedRegions(model, rendered, selected, 3));
    assert(schedule(xiv::renderRegionNotes({}, model)) == schedule(rendered));
    assert(model.find(other)->ownedNotes == otherNotes);
    assert(model.find(loop)->loopLengthTicks == 0 && !model.find(loop)->quantize.active());
    assert(original.find(loop)->loopLengthTicks == 250);

    // Editing one repeated instance must leave the other repeats at their positions.
    auto notes = *model.find(loop)->ownedNotes;
    int modified = 0;
    for (auto &note : notes) {
        if (selected.count(note.id)) {
            note.pitch += 12;
            ++modified;
        }
    }
    assert(modified == 1);
    assert(model.replaceOwnedNotes(loop, notes));
    const auto edited = xiv::renderRegionNotes({}, model);
    int transposed = 0;
    for (const auto &note : edited) {
        if (note.regionId == loop && note.pitch == 76) ++transposed;
    }
    assert(transposed == 1);
    assert(edited.size() == rendered.size());
    // Velocity edits use the same instance IDs and keep the other repetitions.
    for (auto &note : notes)
        if (selected.count(note.id)) note.velocity = 111;
    assert(model.replaceOwnedNotes(loop, notes));
    int velocityEdits = 0;
    for (const auto &note : xiv::renderRegionNotes({}, model))
        if (note.regionId == loop && note.velocity == 111) ++velocityEdits;
    assert(velocityEdits == 1);

    const auto current = xiv::renderRegionNotes({}, model);
    const auto beforeInvalid = schedule(current);
    assert(!xiv::materializeProjectedRegions(model, current, {999}, 3));
    assert(schedule(xiv::renderRegionNotes({}, model)) == beforeInvalid);

    // A full model snapshot provides atomic undo/redo across all edited tracks.
    model = original;
    assert(schedule(xiv::renderRegionNotes({}, model)) == schedule(rendered));
}
