/*
 * FFXIV MidiEditor: exact-tick half-open interval editing helpers.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Given a deleted interval [cutStart, cutEnd), retain the portions of a note
 * strictly outside that interval and ripple-close the gap.  The surviving
 * portions of a sustained note are concatenated (single note, no retrigger).
 */
#ifndef MIDI_INTERVAL_EDIT_H
#define MIDI_INTERVAL_EDIT_H

#include <algorithm>

namespace MidiIntervalEdit {
struct Result {
    bool keep;
    int start;
    int end;
};

inline Result removeTime(int start, int end, int cutStart, int cutEnd) {
    if (cutEnd <= cutStart) return {true, start, end};
    const int removed = cutEnd - cutStart;

    // MIDI notes are modelled as [start,end): merely touching a boundary
    // does not mean that the note overlaps the deleted interval.
    if (end <= cutStart) return {true, start, end};
    if (start >= cutEnd) return {true, start - removed, end - removed};

    // Preserve any note material before the cut and after it.
    const int left = start < cutStart
        ? std::max(0, std::min(end, cutStart) - start) : 0;
    const int right = end > cutEnd
        ? std::max(0, end - std::max(start, cutEnd)) : 0;
    if (left + right <= 0) return {false, 0, 0};

    const int newStart = left > 0 ? start : cutStart;
    return {true, newStart, newStart + left + right};
}

// Other events are instantaneous rather than sustained. Events exactly on
// the next measure's boundary survive and shift to the start of the cut.
inline bool mapPoint(int tick, int cutStart, int cutEnd, int &mapped) {
    if (tick < cutStart) { mapped = tick; return true; }
    if (tick >= cutEnd) { mapped = tick - (cutEnd - cutStart); return true; }
    return false;
}
} // namespace MidiIntervalEdit
#endif
