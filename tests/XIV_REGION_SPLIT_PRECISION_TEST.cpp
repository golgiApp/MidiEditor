#include "../src/region/RegionSplitPrecision.h"
#include <cassert>
#include <iostream>
int main() {
    assert(xiv::snapToQuarterSubdivision(0,480,4)==0);
    assert(xiv::snapToQuarterSubdivision(60,480,4)==120);
    assert(xiv::snapToQuarterSubdivision(59,480,4)==0);
    assert(xiv::snapToQuarterSubdivision(500,480,1)==480);
    assert(xiv::snapToQuarterSubdivision(720,480,1)==960);
    assert(xiv::snapToQuarterSubdivision(300,480,4)==360);
    assert(xiv::snapToQuarterSubdivision(959,480,4)==960);
    assert(xiv::snapToQuarterSubdivision(1079,480,4)==1080);
    assert(xiv::snapToQuarterSubdivision(80,0,4)==80);
    assert(xiv::snapToQuarterSubdivision(80,480,0)==80);
    std::cout << "10 split-grid assertions passed\n";
}
