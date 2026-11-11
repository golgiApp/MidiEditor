/*
 * MidiEditor
 * Copyright (C) 2010  Markus Schwenk
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "TempoChangeEvent.h"
#include "../midi/MidiFile.h"
#include <algorithm>
#include <cmath>

TempoChangeEvent::TempoChangeEvent(int channel, int value, MidiTrack *track)
    : MidiEvent(channel, track) {
    _microseconds = std::clamp(value, 1, 0xFFFFFF);
}

TempoChangeEvent::TempoChangeEvent(TempoChangeEvent &other)
    : MidiEvent(other) {
    _microseconds = other._microseconds;
}

int TempoChangeEvent::beatsPerQuarter() {
    return static_cast<int>(std::lround(60000000.0 / _microseconds));
}

double TempoChangeEvent::msPerTick() {
    return _microseconds / (1000.0 * std::max(1, file()->ticksPerQuarter()));
}

ProtocolEntry *TempoChangeEvent::copy() {
    return new TempoChangeEvent(*this);
}

void TempoChangeEvent::reloadState(ProtocolEntry *entry) {
    TempoChangeEvent *other = dynamic_cast<TempoChangeEvent *>(entry);
    if (!other) {
        return;
    }
    MidiEvent::reloadState(entry);
    _microseconds = other->_microseconds;
    if (file()) file()->calcMaxTime();
}

int TempoChangeEvent::line() {
    return MidiEvent::TEMPO_CHANGE_EVENT_LINE;
}

QByteArray TempoChangeEvent::save() {
    QByteArray array = QByteArray();

    array.append(char(0xFF));
    array.append(char(0x51));
    array.append(char(0x03));
    const int value = _microseconds;
    for (int i = 2; i >= 0; i--) {
        array.append((value & (0xFF << 8 * i)) >> 8 * i);
    }

    return array;
}

void TempoChangeEvent::setBeats(int beats) {
    ProtocolEntry *toCopy = copy();
    _microseconds = 60000000 / std::clamp(beats, 4, 999);
    file()->calcMaxTime();
    protocol(toCopy, this);
}

QString TempoChangeEvent::typeString() {
    return "Tempo Change Event";
}
