#include "../src/support/ChordVoiceFilter.h"
#include <cassert>
#include <iostream>
#include <set>
using namespace FfxivVoice;
int main() {
    std::vector<Note> notes = {
        {1,1,3,60,100,"CLEAN GTR"}, {2,1,3,64,100,"CLEAN GTR"},
        {3,1,3,67,100,"CLEAN GTR"}, {4,1,3,71,100,"CLEAN GTR"},
        {5,1,3,72,140,"CLEAN GTR"}, // later solo note: retained
        {6,2,9,36,100,"DRUMS"}, {7,2,9,42,100,"DRUMS"},
        {8,3,2,60,100,"FR HORN"}, {9,3,2,67,100,"FR HORN"},
        {10,1,3,61,200,"CLEAN GTR"}, {11,1,3,70,202,"CLEAN GTR"}
    };
    assert((deletionIds(notes, Mode::KeepHighest) == std::set<std::uint64_t>{1,2,3,8}));
    assert((deletionIds(notes, Mode::KeepLowest) == std::set<std::uint64_t>{2,3,4,9}));
    assert((deletionIds(notes, Mode::KeepInner) == std::set<std::uint64_t>{1,4}));
    auto t = deletionIds(notes, Mode::KeepHighest, 4);
    assert(t.count(10) && !t.count(11));
    auto d = deletionIds(notes, Mode::KeepHighest, 0);
    assert(!d.count(6) && !d.count(7));
    std::cout << "PASS: highest, lowest, inner; channel/track separation; drum bypass; onset tolerance\n";
}
