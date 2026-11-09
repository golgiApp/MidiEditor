#include "../src/support/CloneChannelAllocator.h"
#include <array>
#include <cassert>
#include <iostream>

int main() {
    using FFXIVClone::findUnusedMelodicChannel;
    std::array<bool,16> occupied = {};
    occupied[0] = true;
    assert(findUnusedMelodicChannel(occupied,0)==1);
    occupied[1] = true;
    assert(findUnusedMelodicChannel(occupied,0)==2);
    occupied[2] = true;
    assert(findUnusedMelodicChannel(occupied,1)==3);
    occupied.fill(true);
    occupied[9] = false; // never allocate GM drum channel
    assert(findUnusedMelodicChannel(occupied,0)==-1);
    occupied[15] = false;
    assert(findUnusedMelodicChannel(occupied,14)==15);
    occupied[15] = true;
    occupied[0] = false;
    assert(findUnusedMelodicChannel(occupied,15)==0);
    occupied.fill(false);
    occupied[0] = true;
    assert(findUnusedMelodicChannel(occupied,99)==1);
    std::cout << "Clone channel allocator: all 7 checks passed\n";
}
