#include "../src/midi/PlaybackCycle.h"
#include <cassert>
#include <limits>

int main() {
    const xiv::PlaybackCycle cycle{100, 500};
    const auto decoded = xiv::PlaybackCycle::unpack(cycle.packed());
    assert(decoded.start == 100 && decoded.end == 500);
    assert(!decoded.contains(99));
    assert(decoded.contains(100) && decoded.contains(499));
    assert(!decoded.contains(500));
    assert(decoded.wrap(490, 500) == 100);
    assert(decoded.wrap(490, 507) == 107);
    assert(decoded.wrap(490, 907) == 107);
    assert(decoded.wrap(700, 715) == 100);
    const xiv::PlaybackCycle invalid{200, 200};
    assert(!xiv::PlaybackCycle::unpack(invalid.packed()).active());
    const xiv::PlaybackCycle wide{0, std::numeric_limits<int>::max()};
    assert(xiv::PlaybackCycle::unpack(wide.packed()).end == wide.end);
}
