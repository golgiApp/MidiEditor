#ifndef XIV_TEMPO_MAP_EDIT_H
#define XIV_TEMPO_MAP_EDIT_H

#include "MidiFile.h"
#include "MidiChannel.h"
#include "../MidiEvent/TempoChangeEvent.h"
#include "../MidiEvent/UnknownEvent.h"
#include "../protocol/Protocol.h"
#include "../protocol/ProtocolItem.h"
#include <algorithm>
#include <cmath>
#include <iterator>

namespace xiv {

// Ramp endpoints are standard tempo events. A sequencer-specific MIDI
// meta-event identifies the generated intermediate events on reopen.
struct TempoCurve {
    double time = 0.5;
    double progress = 0.5;

    // Invert the quadratic time axis before evaluating its BPM axis.
    double parameterAt(double fraction) const {
        if (fraction <= 0.0 || fraction >= 1.0) return std::clamp(fraction, 0.0, 1.0);
        const double root = std::sqrt(std::max(0.0, time * time + (1.0 - 2.0 * time) * fraction));
        return fraction / (time + root);
    }

    double progressAt(double fraction) const {
        const double t = parameterAt(fraction);
        return t * (2.0 * (1.0 - t) * progress + t);
    }
};

struct TempoPoint {
    int microseconds = 500000;
    bool rampToNext = false;
    MidiTrack *track = nullptr;
    TempoCurve curve{};

    double bpm() const { return 60000000.0 / microseconds; }
    void setBpm(double value) {
        microseconds = static_cast<int>(std::lround(60000000.0 / std::clamp(value, 4.0, 999.0)));
    }
};
using TempoMap = QMap<int, TempoPoint>;
inline constexpr auto rampTag = "XIVMS:TempoRamp:";

inline UnknownEvent *tempoRampMarker(MidiEvent *event) {
    auto *marker = dynamic_cast<UnknownEvent *>(event);
    return marker && marker->type() == 0x7f && marker->data().startsWith(rampTag) ? marker : nullptr;
}

inline QByteArray encodeTempoRamp(int end, const TempoCurve &curve) {
    auto data = QByteArray(rampTag) + QByteArray::number(end);
    if (curve.time != 0.5 || curve.progress != 0.5) {
        data += ',' + QByteArray::number(curve.time, 'g', 17);
        data += ',' + QByteArray::number(curve.progress, 'g', 17);
    }
    return data;
}

inline int decodeTempoRamp(const QByteArray &data, TempoCurve &curve) {
    if (!data.startsWith(rampTag)) return -1;
    const auto fields = data.mid(QByteArray(rampTag).size()).split(',');
    bool validEnd = false;
    const int end = fields.first().toInt(&validEnd);
    if (!validEnd || end <= 0) return -1;
    if (fields.size() == 1) {
        curve = {};
        return end;
    }
    if (fields.size() != 3) return -1;
    bool validTime = false, validProgress = false;
    const double time = fields.at(1).toDouble(&validTime);
    const double progress = fields.at(2).toDouble(&validProgress);
    if (!validTime || !validProgress || !std::isfinite(time) || !std::isfinite(progress) ||
        time < 0.0 || time > 1.0 || progress < 0.0 || progress > 1.0) return -1;
    curve = {time, progress};
    return end;
}

inline double tempoCurveBpm(const TempoPoint &first, const TempoPoint &last, double fraction) {
    return std::lerp(first.bpm(), last.bpm(), first.curve.progressAt(fraction));
}

inline TempoMap readTempoMap(MidiFile *file) {
    TempoMap result;
    for (auto *event : *file->channel(17)->eventMap()) {
        if (auto *tempo = dynamic_cast<TempoChangeEvent *>(event); tempo && !result.contains(tempo->midiTime()))
            result.insert(tempo->midiTime(), {tempo->microsecondsPerQuarter(), false, tempo->track()});
    }
    for (auto *event : *file->channel(16)->eventMap()) {
        const auto marker = tempoRampMarker(event);
        if (!marker) continue;
        TempoCurve curve;
        const int end = decodeTempoRamp(marker->data(), curve);
        const int start = marker->midiTime();
        if (end <= start || !result.contains(start) || !result.contains(end)) continue;
        result[start].rampToNext = true;
        result[start].curve = curve;
        auto point = result.upperBound(start);
        while (point != result.end() && point.key() < end) point = result.erase(point);
    }
    return result;
}

inline TempoPoint tempoAt(const TempoMap &points, int tick) {
    auto next = points.upperBound(tick);
    if (next == points.cbegin()) return {};
    const auto previous = std::prev(next);
    auto tempo = previous.value();
    if (tempo.rampToNext && next != points.cend()) {
        const double fraction = double(tick - previous.key()) / (next.key() - previous.key());
        tempo.setBpm(tempoCurveBpm(previous.value(), next.value(), fraction));
    }
    return tempo;
}

inline void insertTempoPoint(TempoMap &points, int tick) {
    if (points.contains(tick)) return;
    auto inserted = tempoAt(points, tick);
    auto next = points.upperBound(tick);
    if (next != points.begin() && next != points.end() && inserted.rampToNext) {
        auto previous = std::prev(next);
        const auto curve = previous.value().curve;
        const double fraction = double(tick - previous.key()) / (next.key() - previous.key());
        const double t = curve.parameterAt(fraction);
        // Subdivide the quadratic so adding a point preserves the ramp shape.
        const auto splitAxis = [t](double control) {
            const double remaining = 1.0 - t;
            return std::pair(std::clamp(control / (2.0 * remaining * control + t), 0.0, 1.0),
                std::clamp(std::lerp(control, 1.0 - control, t) /
                           (2.0 * (1.0 - control) * t + remaining), 0.0, 1.0));
        };
        const auto [leftTime, rightTime] = splitAxis(curve.time);
        const auto [leftProgress, rightProgress] = splitAxis(curve.progress);
        previous.value().curve = {leftTime, leftProgress};
        inserted.curve = {rightTime, rightProgress};
    } else {
        inserted.rampToNext = false;
        inserted.curve = {};
    }
    points.insert(tick, inserted);
}

inline void writeTempoMap(MidiFile *file, const TempoMap &points, const QString &label) {
    if (!file->numTracks() || !points.contains(0)) return;
    auto *tempoChannel = file->channel(17);
    auto *metaChannel = file->channel(16);
    file->protocol()->startNewAction(label);
    auto *beforeTempo = tempoChannel->copy();
    auto *beforeMeta = metaChannel->copy();
    tempoChannel->eventMap()->clear();
    const auto events = metaChannel->eventMap()->values();
    for (auto *event : events)
        if (tempoRampMarker(event)) metaChannel->removeEvent(event, false);

    for (auto point = points.cbegin(); point != points.cend(); ++point) {
        const int tick = point.key();
        auto *track = point.value().track;
        if (!file->tracks()->contains(track)) track = file->track(0);
        tempoChannel->insertEvent(new TempoChangeEvent(17, point.value().microseconds, track), tick, false);
        const auto next = std::next(point);
        if (!point.value().rampToNext || next == points.cend()) continue;
        const int length = next.key() - tick;
        const int spacing = std::max({1, file->ticksPerQuarter() / 4, length / 4096});
        auto *marker = new UnknownEvent(16, 0x7f, encodeTempoRamp(next.key(), point.value().curve), track);
        metaChannel->insertEvent(marker, tick, false);
        int previous = point.value().microseconds;
        for (int offset = spacing; offset < length; ) {
            const int us = tempoAt(points, tick + offset).microseconds;
            if (us != previous)
                tempoChannel->insertEvent(new TempoChangeEvent(17, us, track), tick + offset, false);
            previous = us;
            if (length - offset <= spacing) break;
            offset += spacing;
        }
    }
    file->protocol()->enterUndoStep(new ProtocolItem(beforeTempo, tempoChannel));
    file->protocol()->enterUndoStep(new ProtocolItem(beforeMeta, metaChannel));
    file->calcMaxTime();
    file->protocol()->endAction();
}
}

#endif
