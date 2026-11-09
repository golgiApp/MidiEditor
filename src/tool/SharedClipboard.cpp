/*
 * MidiEditor cross-instance clipboard, v2.
 * Copyright (C) 2026. GPL-3.0-or-later, as with MidiEditor.
 */
#include "SharedClipboard.h"
#include "../MidiEvent/MidiEvent.h"
#include "../MidiEvent/NoteOnEvent.h"
#include "../MidiEvent/OffEvent.h"
#include "../midi/MidiFile.h"
#include "../midi/MidiTrack.h"

#include <QApplication>
#include <QClipboard>
#include <QCoreApplication>
#include <QDataStream>
#include <QIODevice>
#include <QMimeData>
#include <QtGlobal>
#include <cmath>
#include <limits>

SharedClipboard *SharedClipboard::_instance = nullptr;
const char *const SharedClipboard::MIME_TYPE = "application/x-midieditor-events-v2";
namespace {
constexpr quint32 kMagic = 0x4D454332; // MEC2
constexpr quint16 kVersion = 2;
constexpr quint8 kNote = 1;
constexpr quint8 kChannelEvent = 2;
constexpr quint32 kMaxEvents = 250000;
constexpr int kMaxPayload = 32 * 1024 * 1024;

struct Header { qint64 process = 0; quint32 tpq = 0; quint32 count = 0; };

bool readHeader(QDataStream &stream, Header &header) {
    quint32 magic = 0;
    quint16 version = 0;
    stream >> magic >> version >> header.process >> header.tpq >> header.count;
    return stream.status() == QDataStream::Ok && magic == kMagic &&
           version == kVersion && header.tpq > 0 && header.tpq <= 96000 &&
           header.count > 0 && header.count <= kMaxEvents;
}

int targetTick(qint32 tick, int fromTpq, int toTpq) {
    const double value = double(tick) * double(toTpq) / double(fromTpq);
    if (value < 0) return 0;
    if (value >= std::numeric_limits<int>::max()) return std::numeric_limits<int>::max();
    return int(std::llround(value));
}
}

SharedClipboard::SharedClipboard(QObject *parent) : QObject(parent) {}
SharedClipboard::~SharedClipboard() = default;
SharedClipboard *SharedClipboard::instance() {
    if (!_instance) _instance = new SharedClipboard();
    return _instance;
}

bool SharedClipboard::initialize() { return QApplication::clipboard() != nullptr; }
void SharedClipboard::cleanup() { /* System clipboard owns its own data and lifetime. */ }

void SharedClipboard::clear() {
    if (initialize()) QApplication::clipboard()->clear();
}

bool SharedClipboard::hasData() {
    if (!initialize()) return false;
    const QMimeData *mime = QApplication::clipboard()->mimeData();
    if (!mime || !mime->hasFormat(MIME_TYPE)) return false;
    QByteArray data = mime->data(MIME_TYPE);
    if (data.size() <= 0 || data.size() > kMaxPayload) return false;
    QDataStream in(&data, QIODevice::ReadOnly);
    in.setVersion(QDataStream::Qt_6_0);
    Header h;
    return readHeader(in, h);
}

bool SharedClipboard::hasDataFromDifferentProcess() {
    if (!initialize()) return false;
    const QMimeData *mime = QApplication::clipboard()->mimeData();
    if (!mime || !mime->hasFormat(MIME_TYPE)) return false;
    QByteArray data = mime->data(MIME_TYPE);
    if (data.size() <= 0 || data.size() > kMaxPayload) return false;
    QDataStream in(&data, QIODevice::ReadOnly);
    in.setVersion(QDataStream::Qt_6_0);
    Header h;
    return readHeader(in, h) && h.process != qint64(QCoreApplication::applicationPid());
}

bool SharedClipboard::copyEvents(const QList<MidiEvent *> &events, MidiFile *sourceFile) {
    if (!initialize() || !sourceFile || events.isEmpty()) return false;
    QByteArray data;
    QDataStream out(&data, QIODevice::WriteOnly);
    out.setVersion(QDataStream::Qt_6_0);
    // Event count is patched after filtering off-events and non-channel metadata.
    out << kMagic << kVersion << qint64(QCoreApplication::applicationPid())
        << quint32(sourceFile->ticksPerQuarter()) << quint32(0);
    quint32 count = 0;
    for (MidiEvent *ev : events) {
        if (!ev || dynamic_cast<OffEvent *>(ev)) continue;
        const int ch = ev->channel();
        if (ch < 0 || ch > 15) continue; // Only channel MIDI is transferred in v2.
        QByteArray bytes = ev->save();
        if (bytes.isEmpty() || bytes.size() > 4096) continue;
        quint8 kind = kChannelEvent;
        qint32 end = -1;
        if (auto *on = dynamic_cast<NoteOnEvent *>(ev)) {
            if (!on->offEvent() || on->velocity() <= 0) continue;
            kind = kNote;
            end = qint32(on->offEvent()->midiTime());
        }
        out << kind << qint32(ev->midiTime()) << end << qint32(ch) << bytes;
        if (++count > kMaxEvents || data.size() > kMaxPayload) return false;
    }
    if (count == 0 || out.status() != QDataStream::Ok) return false;
    // Rebuild the header with the actual number of events.
    QByteArray payload;
    QDataStream write(&payload, QIODevice::WriteOnly);
    write.setVersion(QDataStream::Qt_6_0);
    write << kMagic << kVersion << qint64(QCoreApplication::applicationPid())
          << quint32(sourceFile->ticksPerQuarter()) << count;
    payload.append(data.mid(sizeof(quint32) + sizeof(quint16) + sizeof(qint64)
                            + sizeof(quint32) + sizeof(quint32)));
    if (payload.size() > kMaxPayload) return false;
    auto *mime = new QMimeData();
    mime->setData(MIME_TYPE, payload);
    QApplication::clipboard()->setMimeData(mime); // Ownership transferred.
    return true;
}

bool SharedClipboard::pasteEvents(MidiFile *targetFile, QList<MidiEvent *> &pastedEvents,
                                  bool applyTempoConversion, int targetCursorTick) {
    Q_UNUSED(applyTempoConversion);
    Q_UNUSED(targetCursorTick);
    pastedEvents.clear();
    if (!targetFile || !hasData() || !targetFile->track(0)) return false;
    QByteArray bytes = QApplication::clipboard()->mimeData()->data(MIME_TYPE);
    QDataStream in(&bytes, QIODevice::ReadOnly);
    in.setVersion(QDataStream::Qt_6_0);
    Header header;
    if (!readHeader(in, header)) return false;

    MidiTrack *track = targetFile->track(0); // Reassigned to the destination track on paste.
    auto fail = [&pastedEvents]() {
        for (MidiEvent *ev : pastedEvents) delete ev;
        pastedEvents.clear();
        return false;
    };
    for (quint32 index = 0; index < header.count; ++index) {
        quint8 kind = 0;
        qint32 tick = 0, endTick = 0, channel = 0;
        QByteArray eventBytes;
        in >> kind >> tick >> endTick >> channel >> eventBytes;
        if (in.status() != QDataStream::Ok || (kind != kNote && kind != kChannelEvent) ||
            channel < 0 || channel > 15 || eventBytes.isEmpty() || eventBytes.size() > 4096)
            return fail();
        const int start = targetTick(tick, int(header.tpq), targetFile->ticksPerQuarter());
        if (kind == kNote) {
            if (eventBytes.size() != 3 || (quint8(eventBytes.at(0)) & 0xF0) != 0x90 ||
                quint8(eventBytes.at(1)) > 127 || quint8(eventBytes.at(2)) == 0 ||
                endTick < tick) return fail();
            auto *on = new NoteOnEvent(quint8(eventBytes.at(1)), quint8(eventBytes.at(2)), channel, track);
            // Construct off with a sentinel line to avoid matching unrelated pending notes.
            auto *off = new OffEvent(channel, -1, track);
            off->setOnEvent(on);
            OffEvent::removeOnEvent(on);
            on->setFile(targetFile);
            off->setFile(targetFile);
            on->setMidiTime(start, false);
            off->setMidiTime(std::max(start, targetTick(endTick, int(header.tpq), targetFile->ticksPerQuarter())), false);
            pastedEvents.append(on);
            pastedEvents.append(off);
        } else {
            QDataStream eventStream(&eventBytes, QIODevice::ReadOnly);
            bool ok = false, end = false;
            MidiEvent *event = MidiEvent::loadMidiEvent(&eventStream, &ok, &end, track);
            if (!ok || !event || end || dynamic_cast<OffEvent *>(event) ||
                dynamic_cast<NoteOnEvent *>(event)) {
                delete event;
                return fail();
            }
            event->setFile(targetFile);
            event->setMidiTime(start, false);
            pastedEvents.append(event);
        }
    }
    if (in.status() != QDataStream::Ok || !in.atEnd() || pastedEvents.isEmpty())
        return fail();
    return true;
}
