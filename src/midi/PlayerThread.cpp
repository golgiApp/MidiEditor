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

#include "PlayerThread.h"
#include "../gui/MidiSettingsWidget.h"
#include "../MidiEvent/NoteOnEvent.h"
#include "../MidiEvent/ProgChangeEvent.h"
#include "../MidiEvent/KeySignatureEvent.h"
#include "../MidiEvent/OffEvent.h"
#include "../MidiEvent/TimeSignatureEvent.h"
#include "MidiFile.h"
#include "MidiInput.h"
#include "MidiOutput.h"
#include "MidiPlayer.h"
#include "MidiTrack.h"
#include <QMultiMap>
#include <QElapsedTimer>
#include <QByteArray>
#include <algorithm>
#include <limits>

#define INTERVAL_TIME 15
#define TIMEOUTS_PER_SIGNAL 1

std::atomic<std::uint64_t> PlayerThread::cycleBounds{0};

PlayerThread::PlayerThread()
    : QThread() {
    file = 0;
    timer = 0;
    timeoutSinceLastSignal = 0;
    time = 0;
}

void PlayerThread::setFile(MidiFile *f, int startMs, bool recording) {
    file = f;
    position = startMs;
    recordingPlayback = recording;
    stopped = false;
}

void PlayerThread::setCycleRange(xiv::PlaybackCycle range) {
    cycleBounds.store(range.packed());
}

xiv::PlaybackCycle PlayerThread::cycleRange() {
    return xiv::PlaybackCycle::unpack(cycleBounds.load());
}

void PlayerThread::stop() {
    stopped = true;
}

void PlayerThread::setInterval(int i) {
    interval = i;
}

void PlayerThread::run() {
    activeTrackNotes.clear();
    QTimer playbackTimer;
    timer = &playbackTimer;
    if (time) {
        delete time;
        time = 0;
    }

    events = file->playerData();

    emit playerStarted();
    restorePlaybackState(position.load(), false);

    setInterval(INTERVAL_TIME);

    connect(timer, SIGNAL(timeout()), this, SLOT(timeout()), Qt::DirectConnection);
    timer->start(INTERVAL_TIME);

    QList<TimeSignatureEvent *> *list = 0;

    int tickInMeasure = 0;
    const int tick = file->tick(position.load());
    measure = file->measure(tick, tick, &list, &tickInMeasure);
    emit(measureChanged(measure, tickInMeasure));

    exec();
    timer->stop();
    timer = nullptr;
    delete time;
    time = nullptr;
    emit playerStopped();
}

void PlayerThread::restorePlaybackState(int startMs, bool rewind) {
    if (rewind) {
        MidiPlayer::panic();
        MidiOutput::resetChannelPrograms();
    }
    // Reset all Controllers
    for (int i = 0; i < 16; i++) {
        QByteArray array;
        array.append(0xB0 | i);
        array.append(121);
        array.append(char(0));
        MidiOutput::sendCommand(array);
    }
    MidiOutput::playedNotes.clear();
    activeTrackNotes.clear();

    // Initialize program tracking for track-based compatibility mode
    for (int i = 0; i < 16; i++) {
        currentChannelProgram[i] = -1;
    }

    // Chase channel and meter state without playing notes before the range.
    TimeSignatureEvent *lastMeter = nullptr;
    KeySignatureEvent *lastKey = nullptr;
    QMultiMap<int, MidiEvent *>::iterator it = events->begin();
    while (it != events->end()) {
        if (it.key() >= startMs) {
            break;
        }
        
        ProgChangeEvent *p = dynamic_cast<ProgChangeEvent *>(it.value());
        if (p && p->channel() >= 0 && p->channel() < 16) {
            currentChannelProgram[p->channel()] = p->program();
        }
        
        if (auto *meter = dynamic_cast<TimeSignatureEvent *>(it.value())) {
            lastMeter = meter;
        } else if (auto *key = dynamic_cast<KeySignatureEvent *>(it.value())) {
            lastKey = key;
        } else if (!dynamic_cast<NoteOnEvent *>(it.value()) &&
                   !dynamic_cast<OffEvent *>(it.value()) &&
                   it.value()->channel() >= 0 && it.value()->channel() < 16) {
            MidiOutput::sendCommand(it.value());
        }
        it++;
    }

    if (lastMeter) emit meterChanged(lastMeter->num(), lastMeter->denom());
    if (lastKey) emit tonalityChanged(lastKey->tonality());
}

void PlayerThread::silenceMutedTracks() {
    MidiTrack *solo = file ? file->soloTrack() : nullptr;
    for (auto it = activeTrackNotes.begin(); it != activeTrackNotes.end(); ++it) {
        MidiTrack *track = it.key();
        if (!track || (!track->muted() && (!solo || track == solo))) continue;
        auto &notes = it.value();
        for (auto note = notes.constBegin(); note != notes.constEnd(); ++note) {
            const int channel = (note.key() >> 8) & 0x0f;
            const int pitch = note.key() & 0x7f;
            for (int n = 0; n < note.value(); ++n) {
                QByteArray msg;
                msg.append(char(0x80 | channel));
                msg.append(char(pitch));
                msg.append(char(0));
                MidiOutput::sendCommand(msg);
            }
        }
        notes.clear();
    }
}

void PlayerThread::sendTrackEvent(MidiEvent *event) {
    if (!event) return;
    MidiTrack *track = event->track();
    MidiTrack *solo = file ? file->soloTrack() : nullptr;
    const bool audible = track && !track->muted() && (!solo || solo == track);
    if (auto *on = dynamic_cast<NoteOnEvent *>(event)) {
        const int key = (on->channel() << 8) | on->note();
        if (on->velocity() > 0) {
            if (audible) {
                ++activeTrackNotes[track][key];
                MidiOutput::sendCommand(event);
            }
        } else { // zero-velocity note on is a note off
            auto &notes = activeTrackNotes[track];
            if (notes.value(key) > 0) {
                notes[key]--;
                MidiOutput::sendCommand(event);
            }
        }
        return;
    }
    if (auto *off = dynamic_cast<OffEvent *>(event)) {
        auto *on = dynamic_cast<NoteOnEvent *>(off->onEvent());
        if (on) {
            const int key = (on->channel() << 8) | on->note();
            auto &notes = activeTrackNotes[track];
            if (notes.value(key) > 0) {
                notes[key]--;
                MidiOutput::sendCommand(event);
            }
        }
        return;
    }
    // Allow tempo/meta processing elsewhere; send channel messages only
    // for the audible tracks, except track-independent global events.
    if (audible || !track) MidiOutput::sendCommand(event);
}

void PlayerThread::timeout() {
    if (!time) {
        time = new QElapsedTimer();
        time->start();
    }

    disconnect(timer, SIGNAL(timeout()), this, SLOT(timeout()));
    if (stopped) {

        // AllNotesOff // All SoundsOff
        for (int i = 0; i < 16; i++) {
            // value (third number) should be 0, but doesnt work
            QByteArray array;
            array.append(0xB0 | i);
            array.append(char(123));
            array.append(char(127));
            MidiOutput::sendCommand(array);
        }
        if (MidiOutput::isAlternativePlayer) {
            foreach(int channel, MidiOutput::playedNotes.keys()) {
                foreach(int note, MidiOutput::playedNotes.value(channel)) {
                    QByteArray array;
                    array.append(0x80 | channel);
                    array.append(char(note));
                    array.append(char(0));
                    MidiOutput::sendCommand(array);
                }
            }
        }
        quit();
        return;
    } else {
        // A mute/solo click is honored on the next 15 ms audio scheduling tick.
        silenceMutedTracks();
        const int previous = position.load();
        const double elapsed = time->restart() * MidiPlayer::speedScale();
        const int newPos = static_cast<int>(std::clamp(previous + elapsed,
            0.0, static_cast<double>(std::numeric_limits<int>::max())));
        const auto cycle = recordingPlayback ? xiv::PlaybackCycle{} : cycleRange();
        int next = newPos;
        const bool wrapped = cycle.active() && newPos >= cycle.end;
        bool atEnd;
        if (wrapped) {
            if (previous < cycle.end) sendEventsBetween(previous, cycle.end);
            restorePlaybackState(cycle.start, true);
            next = cycle.wrap(previous, newPos);
            atEnd = sendEventsBetween(cycle.start, next);
        } else {
            atEnd = sendEventsBetween(previous, next);
        }
        if (atEnd && !cycle.active() && !MidiInput::recording()) stop();

        const int tick = file->tick(next);
        QList<TimeSignatureEvent *> *list = nullptr;
        int tickInMeasure = 0;
        const int nextMeasure = file->measure(tick, tick, &list, &tickInMeasure);
        if (wrapped || nextMeasure != measure) {
            measure = nextMeasure;
            emit measureChanged(measure, tickInMeasure);
        }
        position = next;
        MidiInput::setTime(next);
        if (++timeoutSinceLastSignal == TIMEOUTS_PER_SIGNAL) {
            emit timeMsChanged(next);
            emit measureUpdate(measure, tickInMeasure);
            timeoutSinceLastSignal = 0;
        }
    }
    connect(timer, SIGNAL(timeout()), this, SLOT(timeout()), Qt::DirectConnection);
}

bool PlayerThread::sendEventsBetween(int startMs, int endMs) {
    QMultiMap<int, MidiEvent *>::iterator it = events->lowerBound(startMs);

    while (it != events->end() && it.key() < endMs) {
        // save events for the given tick
        QList<MidiEvent *> onEv, offEv;
        int sendPosition = it.key();

        do {
            if (it.value()->isOnEvent()) {
                onEv.append(it.value());
            } else {
                offEv.append(it.value());
            }
            it++;
        } while (it != events->end() && it.key() == sendPosition);

        foreach(MidiEvent* ev, offEv) {
            sendTrackEvent(ev);
        }
        foreach(MidiEvent* ev, onEv) {
            if (ev->line() == MidiEvent::KEY_SIGNATURE_EVENT_LINE) {
                KeySignatureEvent *keySig = dynamic_cast<KeySignatureEvent *>(ev);
                if (keySig) {
                    emit tonalityChanged(keySig->tonality());
                }
            }
            if (ev->line() == MidiEvent::TIME_SIGNATURE_EVENT_LINE) {
                TimeSignatureEvent *timeSig = dynamic_cast<TimeSignatureEvent *>(ev);
                if (timeSig) {
                    emit meterChanged(timeSig->num(), timeSig->denom());
                }
            }
            
            ProgChangeEvent *p = dynamic_cast<ProgChangeEvent *>(ev);
            if (p && p->channel() >= 0 && p->channel() < 16) {
                currentChannelProgram[p->channel()] = p->program();
            }

            if (AdditionalMidiSettingsWidget::trackBasedProgramChanges()) {
                NoteOnEvent *n = dynamic_cast<NoteOnEvent *>(ev);
                if (n && n->track() && !n->track()->muted() &&
                    (!file->soloTrack() || file->soloTrack() == n->track()) &&
                    n->channel() >= 0 && n->channel() < 16) {
                    int prog = n->track()->progAtTick(n->midiTime());
                    if (currentChannelProgram[n->channel()] != prog) {
                        MidiOutput::sendProgram(n->channel(), prog);
                        currentChannelProgram[n->channel()] = prog;
                    }
                }
            }
            
            sendTrackEvent(ev);
        }

    }
    return it == events->end();
}

int PlayerThread::timeMs() {
    return position.load();
}
