#ifndef XIV_PIANO_ROLL_GRID_H
#define XIV_PIANO_ROLL_GRID_H

#include <algorithm>
#include <cstdint>
#include <utility>

namespace xiv {

enum class PianoRollGridMode { Auto = 0, Quarter = 4, Eighth = 8, Sixteenth = 16 };

struct PianoRollGrid {
    int start;
    int end;
    int wholeNoteTicks;
    int division = 0;

    int boundary(int index) const {
        if (index == 0) return start;
        if (division == 0) return end;
        return static_cast<int>(std::min<int64_t>(end,
            start + static_cast<int64_t>(index) * wholeNoteTicks / division));
    }

    std::pair<int, int> cellAt(int tick) const {
        if (tick < start || tick >= end) return {tick, tick};
        if (division == 0) return {start, end};
        // Invert the rounded-down boundaries so odd PPQ resolutions stay aligned.
        const auto offset = static_cast<int64_t>(tick) - start;
        const int index = static_cast<int>(((offset + 1) * division - 1) / wholeNoteTicks);
        return {boundary(index), boundary(index + 1)};
    }
};

template<class TickToX>
PianoRollGrid choosePianoRollGrid(int start, int end, int ppq, int meterDenominatorPower,
                                 TickToX tickToX, PianoRollGridMode mode = PianoRollGridMode::Auto) {
    constexpr int minimumCellPixels = 32;
    constexpr int finestDivision = static_cast<int>(PianoRollGridMode::Sixteenth);
    PianoRollGrid grid{start, end, 4 * ppq};
    if (end <= start || ppq <= 0) return grid;
    if (mode != PianoRollGridMode::Auto) {
        grid.division = std::min(grid.wholeNoteTicks, static_cast<int>(mode));
        return grid;
    }
    const int beatDivision = std::min(finestDivision, 1 << std::clamp(meterDenominatorPower, 0, 15));
    const int left = tickToX(start);
    const int lastDivision = std::min(grid.wholeNoteTicks, finestDivision);
    for (int division = beatDivision; division <= lastDivision; division *= 2) {
        const int cellTicks = grid.wholeNoteTicks / division;
        if (cellTicks >= end - start) continue;
        if (static_cast<int64_t>(tickToX(start + cellTicks)) - left < minimumCellPixels) break;
        grid.division = division;
    }
    return grid;
}

} // namespace xiv

#endif
