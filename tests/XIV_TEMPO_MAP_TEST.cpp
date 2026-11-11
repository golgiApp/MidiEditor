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

    xiv::insertTempoPoint(points, 96);
    assert(std::abs(xiv::tempoAt(points, 192).bpm() - 150) < 0.001);

    points.erase(points.find(96));
    points[0].rampToNext = false;
    assert(xiv::tempoAt(points, 383).bpm() == 120);

    points[0].rampToNext = true;
    points[0].curve = {0.2, 0.8};
    assert(xiv::tempoAt(points, 192).bpm() > 150);
    auto late = points;
    late[0].curve = {0.8, 0.2};
    assert(xiv::tempoAt(late, 192).bpm() < 150);
    assert(xiv::tempoAt(points, 0).microseconds == start.microseconds);
    assert(xiv::tempoAt(points, 384).microseconds == finish.microseconds);

    auto subdivided = points;
    xiv::insertTempoPoint(subdivided, 123);
    for (int tick = 0; tick <= 384; ++tick)
        assert(std::abs(xiv::tempoAt(subdivided, tick).bpm() - xiv::tempoAt(points, tick).bpm()) < 0.001);

    xiv::TempoCurve decoded;
    assert(xiv::decodeTempoRamp(xiv::encodeTempoRamp(384, points[0].curve), decoded) == 384);
    assert(decoded.time == 0.2 && decoded.progress == 0.8);
    assert(xiv::decodeTempoRamp("XIVMS:TempoRamp:384", decoded) == 384);
    assert(decoded.time == 0.5 && decoded.progress == 0.5);
    assert(xiv::decodeTempoRamp("XIVMS:TempoRamp:384,2,0.5", decoded) < 0);

    finish.setBpm(123.45);
    assert(std::abs(finish.bpm() - 123.45) < 0.001);
    std::cout << "Tempo steps, linear and shaped ramps, point insertion and fractional BPM passed\n";
}
