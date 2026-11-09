#include "../src/support/MidiIntervalEdit.h"
#include <cassert>
#include <iostream>
using MidiIntervalEdit::removeTime;
using MidiIntervalEdit::mapPoint;
static void expect(int a, int b, int cutStart, int cutEnd,
                   bool keep, int newStart, int newEnd) {
    auto r = removeTime(a, b, cutStart, cutEnd);
    assert(r.keep == keep);
    if (keep) { assert(r.start == newStart); assert(r.end == newEnd); }
}
int main() {
    // Delete measure 2: [768, 1536) in 4/4 at 192 ticks/quarter.
    constexpr int a=768,b=1536;
    expect(384,768,a,b,true,384,768);       // note ending exactly at M2
    expect(1536,1728,a,b,true,768,960);    // note beginning exactly at M3
    expect(384,900,a,b,true,384,768);      // trim tail overlapping M2
    expect(1400,1728,a,b,true,768,960);   // trim head overlapping M2
    expect(384,1728,a,b,true,384,960);    // long note spans the gap
    expect(800,900,a,b,false,0,0);        // note entirely within M2
    expect(768,1536,a,b,false,0,0);       // note exactly fills M2
    expect(768,1728,a,b,true,768,960);    // starts at M2, tail in M3
    expect(700,800,a,b,true,700,768);     // starts before cut, ends inside
    expect(1400,1536,a,b,false,0,0);      // ends at cut right boundary
    expect(0,192,a,b,true,0,192);         // wholly before cut
    expect(1600,1800,a,b,true,832,1032); // wholly after cut
    int tick=-1;
    assert(mapPoint(768,a,b,tick)==false);
    assert(mapPoint(1535,a,b,tick)==false);
    assert(mapPoint(1536,a,b,tick) && tick == 768);
    assert(mapPoint(767,a,b,tick) && tick == 767);
    std::cout << "All 16 note-boundary checks passed.\n";
}
