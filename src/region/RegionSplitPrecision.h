/* XIV MIDI Studio v0.3 — explicit grid-aligned region split helpers.
 * GPL-3.0-or-later. Independent of Qt for straightforward unit tests. */
#ifndef XIV_REGION_SPLIT_PRECISION_H
#define XIV_REGION_SPLIT_PRECISION_H
#include <algorithm>
#include <cstdint>
#include <limits>
namespace xiv {
// subdivisionsPerQuarter: 1 = quarter notes, 4 = sixteenth notes.
// Uses nearest gridline rather than silently snapping every split operation.
inline int snapToQuarterSubdivision(int tick, int tpq, int subdivisionsPerQuarter) {
    if (tick <= 0) return 0;
    if (tpq <= 0 || subdivisionsPerQuarter <= 0) return tick;
    const int64_t scaled = static_cast<int64_t>(tick) * subdivisionsPerQuarter;
    const int64_t snapped = ((scaled + tpq / 2) / tpq) * tpq;
    const int64_t result = (snapped + subdivisionsPerQuarter / 2) /
                           subdivisionsPerQuarter;
    return static_cast<int>(std::clamp(result, static_cast<int64_t>(0),
                   static_cast<int64_t>(std::numeric_limits<int>::max())));
}
} // namespace xiv
#endif
