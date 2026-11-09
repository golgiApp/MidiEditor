/*
 * Windows / Qt system clipboard for cross-instance MidiEditor transfers.
 * Copyright (C) 2026. GPL-3.0-or-later, as with MidiEditor.
 *
 * Self-contained payloads: never transfer raw pointers or rely on the
 * lifetime of the originating editor or its MIDI file.
 */
#ifndef SHAREDCLIPBOARD_H_
#define SHAREDCLIPBOARD_H_

#include <QObject>
#include <QList>
#include <QByteArray>

class MidiEvent;
class MidiFile;

class SharedClipboard final : public QObject {
    Q_OBJECT
public:
    static SharedClipboard *instance();
    bool initialize();
    bool copyEvents(const QList<MidiEvent *> &events, MidiFile *sourceFile);
    bool pasteEvents(MidiFile *targetFile, QList<MidiEvent *> &pastedEvents,
                     bool applyTempoConversion = true, int targetCursorTick = 0);
    bool hasData();
    bool hasDataFromDifferentProcess();
    void clear();
    void cleanup();

private:
    explicit SharedClipboard(QObject *parent = nullptr);
    ~SharedClipboard() override;
    SharedClipboard(const SharedClipboard &) = delete;
    SharedClipboard &operator=(const SharedClipboard &) = delete;

    static SharedClipboard *_instance;
    static const char *const MIME_TYPE;
};
#endif
