#include "../src/gui/PianoRollGrid.h"
#include <cassert>

int main() {
    const auto gridAtQuarterWidth = [](int quarterPixels) {
        return xiv::choosePianoRollGrid(0, 768, 192, 2, [quarterPixels](int tick) {
            return tick * quarterPixels / 192;
        });
    };

    assert(gridAtQuarterWidth(20).division == 0);
    assert(gridAtQuarterWidth(50).division == 4);
    assert(gridAtQuarterWidth(120).division == 8);
    assert(gridAtQuarterWidth(128).division == 16);
    assert(gridAtQuarterWidth(256).division == 16);
    assert(gridAtQuarterWidth(512).division == 16);
    assert(gridAtQuarterWidth(1024).division == 16);
    assert(gridAtQuarterWidth(3200).division == 16);
    const auto fineMeter = xiv::choosePianoRollGrid(0, 96, 192, 6,
        [](int tick) { return tick * 32; });
    assert(fineMeter.division == 16);

    using GridMode = xiv::PianoRollGridMode;
    for (GridMode mode : {GridMode::Quarter, GridMode::Eighth, GridMode::Sixteenth}) {
        const int ticks = 768 / static_cast<int>(mode);
        for (int quarterPixels : {10, 3200}) {
            const auto fixed = xiv::choosePianoRollGrid(100, 676, 192, 3,
                [quarterPixels](int tick) { return tick * quarterPixels / 192; }, mode);
            assert(fixed.division == static_cast<int>(mode));
            assert(fixed.boundary(1) == 100 + ticks);
            assert(fixed.cellAt(100 + ticks) == std::make_pair(100 + ticks, 100 + 2 * ticks));
        }
    }

    const auto odd = xiv::choosePianoRollGrid(100, 500, 101, 2,
        [](int tick) { return tick * 32; });
    assert(odd.division == 16);
    assert(odd.boundary(4) == 201);
    assert(odd.boundary(16) == 500);
    for (int index = 0; odd.boundary(index) < odd.end; ++index) {
        const int left = odd.boundary(index);
        const int right = odd.boundary(index + 1);
        assert(left < right);
        assert(odd.cellAt(left) == std::make_pair(left, right));
        assert(odd.cellAt(right - 1) == std::make_pair(left, right));
    }

    const auto sixEight = xiv::choosePianoRollGrid(0, 576, 192, 3,
        [](int tick) { return tick / 2; });
    assert(sixEight.division == 8);
    assert(sixEight.cellAt(192) == std::make_pair(192, 288));
    const auto shortBar = xiv::choosePianoRollGrid(100, 172, 192, 2,
        [](int tick) { return tick; });
    assert(shortBar.division == 16);
    assert(shortBar.cellAt(171) == std::make_pair(148, 172));
    const auto lowResolution = xiv::choosePianoRollGrid(0, 24, 6, 2,
        [](int tick) { return tick * 64; });
    assert(lowResolution.division == 16);
    assert(lowResolution.boundary(4) == 6);
}
