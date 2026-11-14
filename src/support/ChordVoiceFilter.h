#pragma once
// FFXIV chord voice filtering for MidiEditor (C++17+).
// Caller supplies the currently selected MIDI notes; to operate on whole active track,
// caller supplies all its notes. IDs must uniquely identify notes for deletion.
// This file deliberately has no Qt dependencies.
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace FfxivVoice {

enum class Mode { KeepHighest, KeepLowest, KeepInner };
struct Note {
    std::uint64_t id;  // MidiEditor's stable note/event handle mapping
    int track;         // track identity
    int channel;       // 0-based MIDI channel, 9 == General MIDI drums
    int pitch;         // 0..127
    std::int64_t startTick;
    std::string trackName;
    std::int64_t endTick = 0;
};

inline bool isDrum(const Note& n) {
    if (n.channel == 9) return true;
    std::string s = n.trackName;
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s.find("DRUM") != std::string::npos ||
           s.find("PERCUSS") != std::string::npos ||
           s.find("DRUMKIT") != std::string::npos;
}

// Returns IDs of notes to delete; nothing is modified until the application commits
// the deletion as a single Undo action. Drum notes are never deleted.
// All notes in one chord start within toleranceTicks of the first onset in that
// group, and must belong to the same track AND channel.
inline std::set<std::uint64_t> deletionIds(const std::vector<Note>& notes,
                                           Mode mode,
                                           std::int64_t toleranceTicks = 0,
                                           bool includeOverlaps = false) {
    if (toleranceTicks < 0) toleranceTicks = 0;
    std::map<std::pair<int,int>, std::vector<const Note*>> buckets;
    for (const auto& n : notes) {
        if (!isDrum(n)) buckets[{n.track, n.channel}].push_back(&n);
    }
    std::set<std::uint64_t> result;
    for (auto& [key, group] : buckets) {
        std::sort(group.begin(), group.end(), [](auto a, auto b) {
            return std::tie(a->startTick, a->pitch, a->id) <
                   std::tie(b->startTick, b->pitch, b->id);
        });
        if (includeOverlaps && mode == Mode::KeepHighest) {
            const auto lowerVoice = [](const Note *a, const Note *b) {
                if (a->pitch != b->pitch) return a->pitch < b->pitch;
                return a->id < b->id;
            };
            using Voice = const Note *;
            std::set<Voice, decltype(lowerVoice)> active(lowerVoice);
            std::multimap<std::int64_t, Voice> endings;
            for (const auto *note : group) {
                while (!endings.empty() && endings.begin()->first <= note->startTick) {
                    active.erase(endings.begin()->second);
                    endings.erase(endings.begin());
                }
                if (note->endTick <= note->startTick) continue;
                const Voice voice = note;
                active.insert(voice);
                endings.emplace(note->endTick, voice);
                // Each lower voice only needs to be marked once.
                while (active.size() > 1) {
                    result.insert((*active.begin())->id);
                    active.erase(active.begin());
                }
            }
        }
        for (std::size_t i = 0; i < group.size();) {
            const auto anchor = group[i]->startTick;
            std::size_t j = i + 1;
            while (j < group.size() && group[j]->startTick - anchor <= toleranceTicks) ++j;
            // Singletons are always retained. For Inner mode, dyads are retained
            // because they have no inner voice to extract.
            const auto count = j - i;
            if (count > 1 && (mode != Mode::KeepInner || count >= 3)) {
                std::vector<const Note*> chord(group.begin() + static_cast<std::ptrdiff_t>(i),
                                                group.begin() + static_cast<std::ptrdiff_t>(j));
                std::sort(chord.begin(), chord.end(), [](auto a, auto b) {
                    return std::tie(a->pitch, a->id) < std::tie(b->pitch, b->id);
                });
                for (std::size_t k = 0; k < chord.size(); ++k) {
                    bool remove = mode == Mode::KeepHighest ? k + 1 < chord.size()
                                : mode == Mode::KeepLowest ? k > 0
                                : (k == 0 || k + 1 == chord.size());
                    if (remove) result.insert(chord[k]->id);
                }
            }
            i = j;
        }
    }
    return result;
}
} // namespace FfxivVoice
