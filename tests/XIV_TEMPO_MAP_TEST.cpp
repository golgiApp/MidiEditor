#include "../src/midi/TempoMapEdit.h"
#include <cassert>
#include <cmath>
#include <iostream>

int main() {
    xiv::TempoMap points;
    xiv::TempoPoint start;
    xiv::TempoPoint finish;
    start.setBpm(120);
    finish.setBpm(180);
    points.insert(0, start);
    points.insert(384, finish);
    assert(xiv::tempoAt(points, 192).bpm() == 120);
    points[0].rampToNext = true;
    assert(std::abs(xiv::tempoAt(points, 192).bpm() - 150) < 0.001);
    assert(std::abs(xiv::tempoAt(points, 384).bpm() - 180) < 0.001);

    const auto inserted = xiv::tempoAt(points, 96);
    points.insert(96, inserted);
    assert(std::abs(xiv::tempoAt(points, 192).bpm() - 150) < 0.001);

    points.erase(points.find(96));
    points[0].rampToNext = false;
    assert(xiv::tempoAt(points, 383).bpm() == 120);

    finish.setBpm(123.45);
    assert(std::abs(finish.bpm() - 123.45) < 0.001);
    std::cout << "Tempo steps, linear ramps, point insertion and fractional BPM passed\n";
}
