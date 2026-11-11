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
struct TempoPoint {
    int microseconds = 500000;
    bool rampToNext = false;
    MidiTrack *track = nullptr;

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

inline TempoMap readTempoMap(MidiFile *file) {
    TempoMap result;
    for (auto *event : *file->channel(17)->eventMap()) {
        if (auto *tempo = dynamic_cast<TempoChangeEvent *>(event); tempo && !result.contains(tempo->midiTime()))
            result.insert(tempo->midiTime(), {tempo->microsecondsPerQuarter(), false, tempo->track()});
    }
    for (auto *event : *file->channel(16)->eventMap()) {
        const auto marker = tempoRampMarker(event);
        if (!marker) continue;
        bool ok = false;
        const int end = marker->data().mid(QByteArray(rampTag).size()).toInt(&ok);
        const int start = marker->midiTime();
        if (!ok || end <= start || !result.contains(start) || !result.contains(end)) continue;
        result[start].rampToNext = true;
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
        tempo.setBpm(tempo.bpm() + (next.value().bpm() - tempo.bpm()) * fraction);
    }
    return tempo;
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
        auto *marker = new UnknownEvent(16, 0x7f, QByteArray(rampTag) + QByteArray::number(next.key()), track);
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
