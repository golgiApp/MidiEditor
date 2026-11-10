#ifndef PLAYBACK_CYCLE_H
#define PLAYBACK_CYCLE_H

#include <cstdint>

namespace xiv {

struct PlaybackCycle {
    int start = 0;
    int end = 0;

    bool active() const { return start >= 0 && end > start; }
    bool contains(int ms) const { return active() && ms >= start && ms < end; }

    std::uint64_t packed() const {
        if (!active()) return 0;
        return (static_cast<std::uint64_t>(start) << 32) | static_cast<std::uint32_t>(end);
    }

    static PlaybackCycle unpack(std::uint64_t value) {
        return {static_cast<int>(value >> 32), static_cast<int>(value & 0xffffffff)};
    }

    // When a scheduling interval crosses the exclusive end, preserve the
    // excess elapsed time without replaying several missed passes at once.
    int wrap(int previous, int next) const {
        if (previous >= end) return start;
        return start + (next - end) % (end - start);
    }
};

} // namespace xiv

#endif
