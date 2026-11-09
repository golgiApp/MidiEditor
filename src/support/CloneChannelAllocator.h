// FFXIV MidiEditor: isolated melodic channel assignment for cloned tracks.
// Standard MIDI has 16 channels; channel index 9 (MIDI channel 10) is
// reserved for General MIDI percussion.
#ifndef FFXIV_CLONE_CHANNEL_ALLOCATOR_H
#define FFXIV_CLONE_CHANNEL_ALLOCATOR_H

#include <array>

namespace FFXIVClone {

// Prefer channels immediately following the source channel (with wraparound)
// while avoiding any channel already assigned to a track or containing events.
// Returns -1 when a new melodic MIDI channel is unavailable.
inline int findUnusedMelodicChannel(const std::array<bool, 16> &occupied,
                                    int sourceChannel) {
    if (sourceChannel < 0 || sourceChannel > 15) sourceChannel = 0;
    for (int offset = 1; offset <= 16; ++offset) {
        const int channel = (sourceChannel + offset) % 16;
        if (channel == 9) continue; // GM drums: human-readable channel 10
        if (!occupied[channel]) return channel;
    }
    return -1;
}

} // namespace FFXIVClone
#endif
