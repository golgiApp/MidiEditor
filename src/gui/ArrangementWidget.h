/*
 * FFXIV Workspace -- Logic-inspired, per-instrument arrangement overview.
 * Copyright (C) 2026. Distributed under the MidiEditor GPL-3.0 license.
 *
 * This first iteration shows one derived, read-only MIDI region per track.
 * The notes remain editable in the selected-track Piano Roll below it.
 */
#ifndef FFXIV_ARRANGEMENTWIDGET_H
#define FFXIV_ARRANGEMENTWIDGET_H

#include <QWidget>
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QHash>
#include <QVector>
#include <QScrollArea>
#include <QFontMetrics>
#include <QCursor>
#include <QPointer>
#include <QToolTip>
#include <functional>
#include <algorithm>

#include "../midi/MidiFile.h"
#include "../midi/MidiTrack.h"
#include "../MidiEvent/NoteOnEvent.h"
#include "../MidiEvent/OffEvent.h"
#include "../protocol/Protocol.h"

class ArrangementWidget final : public QWidget {
public:
    explicit ArrangementWidget(QWidget *parent = nullptr) : QWidget(parent) {
        setMinimumHeight(130);
        setMouseTracking(true);
        setFocusPolicy(Qt::ClickFocus);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    }

    std::function<void(MidiTrack *)> selectTrackRequested;
    std::function<void(int)> scrollTimeRequested;

    // View origin is shared with the Piano Roll's horizontal scrollbar.
    void setOriginMs(int ms) {
        ms = std::max(0, ms);
        if (ms != _originMs) { _originMs = ms; update(); }
    }
    static constexpr int headerWidth = 190;
    static constexpr int rulerHeight = 32;
    static constexpr int laneHeight = 55;

    void setFile(MidiFile *file) {
        _file = file;
        _selected = nullptr;
        _preview.clear();
        _solo = nullptr;
        _originMs = 0;
        _playheadMs = -1;
        if (file) {
            file->setSoloTrack(nullptr);
            connect(file->protocol(), &Protocol::actionFinished, this,
                    [this]() { rebuildPreview(); });
            connect(file, &MidiFile::trackChanged, this,
                    [this]() { rebuildPreview(); });
        }
        rebuildPreview();
    }

    void setSelectedTrack(MidiTrack *track) {
        if (_selected == track) return;
        _selected = track;
        update();
    }
    MidiTrack *selectedTrack() const { return _selected; }

    void setPlayheadMs(int ms) {
        _playheadMs = ms;
        update();
    }

    void rebuildPreview() {
        _preview.clear();
        if (!_file) {
            updateGeometry();
            update();
            return;
        }
        // Removing a track must also drop any stale solo/focus pointer.
        if (_solo && !_file->tracks()->contains(_solo)) {
            _solo = nullptr;
            _file->setSoloTrack(nullptr);
        }
        if (_selected && !_file->tracks()->contains(_selected)) {
            _selected = nullptr;
        }
        for (int ch = 0; ch < 16; ++ch) {
            auto *events = _file->channelEvents(ch);
            for (auto it = events->constBegin(); it != events->constEnd(); ++it) {
                auto *note = dynamic_cast<NoteOnEvent *>(it.value());
                if (!note || !note->track() || !note->offEvent() || note->velocity() == 0) continue;
                const int begin = _file->msOfTick(note->midiTime());
                const int end = _file->msOfTick(note->offEvent()->midiTime());
                _preview[note->track()].append({begin, std::max(end, begin + 1), note->note()});
            }
        }
        const int count = _file->tracks()->size();
        setMinimumHeight(std::max(130, rulerHeight + count * laneHeight));
        updateGeometry();
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, false);
        p.fillRect(rect(), QColor("#202329"));
        p.fillRect(0, 0, width(), rulerHeight, QColor("#292D34"));
        p.fillRect(0, 0, headerWidth, height(), QColor("#292D34"));

        p.setFont(QFont(QStringLiteral("Segoe UI"), 9));
        p.setPen(QColor("#CBD2DF"));
        p.drawText(QRect(12, 0, headerWidth - 15, rulerHeight), Qt::AlignVCenter,
                   QStringLiteral("TRACKS   |   Drag timeline to pan"));
        if (!_file) {
            p.drawText(QRect(headerWidth + 12, 0, width() - headerWidth - 12, 60),
                       Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("Open a MIDI file"));
            return;
        }

        // Tempo-aware quarter grid plus actual bar boundaries, including
        // time-signature changes (no hardcoded 4/4 assumption).
        const int tpq = std::max(1, _file->ticksPerQuarter());
        const int endMs = _originMs + int(std::max(0, width() - headerWidth) / _scale);
        const int endTick = std::max(0, _file->tick(endMs));
        const int firstQuarter = std::max(0, _file->tick(_originMs) / tpq - 2);
        const int lastQuarter = std::max(firstQuarter, endTick / tpq + 2);
        for (int q = firstQuarter; q <= lastQuarter && q - firstQuarter < 2000; ++q) {
            const int x = xForMs(_file->msOfTick(q * tpq));
            if (x < headerWidth || x > width()) continue;
            p.setPen(QColor("#323941"));
            p.drawLine(x, rulerHeight, x, height());
        }
        int barStart = 0, barEnd = 0;
        int bar = _file->measure(std::max(0, _file->tick(_originMs)), &barStart, &barEnd);
        for (int step = 0; step < 1000 && barStart <= endTick; ++step) {
            const int x = xForMs(_file->msOfTick(barStart));
            if (x >= headerWidth && x <= width()) {
                p.setPen(QColor("#5C6775"));
                p.drawLine(x, rulerHeight, x, height());
                p.setPen(QColor("#C4CCD9"));
                p.drawText(x + 5, 20, QString::number(bar));
            }
            if (barEnd <= barStart) break;
            bar = _file->measure(barEnd, &barStart, &barEnd);
        }

        const auto &tracks = *_file->tracks();
        for (int i = 0; i < tracks.size(); ++i) {
            MidiTrack *t = tracks.at(i);
            if (!t) continue;
            const int y = rulerHeight + i * laneHeight;
            if (y > height()) break;
            const bool chosen = t == _selected;
            const QColor c = t->color() ? *t->color() : QColor("#7EA4D4");
            p.fillRect(0, y, width(), laneHeight, chosen ? QColor("#363D48") :
                       (i % 2 ? QColor("#252930") : QColor("#292D34")));
            p.setPen(QColor("#414753"));
            p.drawLine(0, y + laneHeight - 1, width(), y + laneHeight - 1);
            p.fillRect(0, y + 2, 4, laneHeight - 4, c);
            p.setPen(QColor("#EEF1F6"));
            const QString name = t->name().trimmed().isEmpty() ?
                                 QStringLiteral("Track %1").arg(i) : t->name();
            QFontMetrics fm(p.font());
            p.drawText(13, y + 22, fm.elidedText(name, Qt::ElideRight, 112));
            p.setPen(QColor("#929BAA"));
            p.drawText(13, y + 42, QStringLiteral("Track %1").arg(i + 1));

            drawButton(p, muteRect(y), QStringLiteral("M"), t->muted(), QColor("#B75B61"));
            drawButton(p, soloRect(y), QStringLiteral("S"), t == _solo, QColor("#C4A45A"));

            const auto &notes = _preview[t];
            if (notes.isEmpty()) continue;
            int begin = notes.first().beginMs;
            int end = notes.first().endMs;
            int low = 127, high = 0;
            for (const auto &n : notes) {
                begin = std::min(begin, n.beginMs);
                end = std::max(end, n.endMs);
                low = std::min(low, n.pitch);
                high = std::max(high, n.pitch);
            }
            const int x0 = std::max(headerWidth + 3, xForMs(begin));
            const int x1 = std::min(width() - 2, xForMs(end));
            if (x1 <= x0) continue;
            QColor region = c;
            region.setAlpha(56);
            p.fillRect(QRect(x0, y + 6, x1 - x0, laneHeight - 12), region);
            p.setPen(c);
            p.drawRect(QRect(x0, y + 6, x1 - x0, laneHeight - 12));
            p.setPen(Qt::NoPen);
            p.setBrush(c);
            const int range = std::max(1, high - low);
            for (const auto &n : notes) {
                const int sx = std::max(x0 + 1, xForMs(n.beginMs));
                const int ex = std::min(x1 - 1, xForMs(n.endMs));
                if (ex < x0 || sx >= x1 || ex <= sx) continue;
                const int ny = y + 42 - ((n.pitch - low) * 31 / range);
                p.drawRect(QRect(sx, ny, std::max(1, ex - sx), 3));
            }
        }

        const int ms = _playheadMs >= 0 ? _playheadMs : _file->msOfTick(_file->cursorTick());
        const int x = xForMs(ms);
        if (x >= headerWidth && x < width()) {
            p.setPen(QPen(QColor("#F1D378"), 2));
            p.drawLine(x, 0, x, height());
        }
        p.setPen(QColor("#515966"));
        p.drawLine(headerWidth, 0, headerWidth, height());
    }

    void mousePressEvent(QMouseEvent *e) override {
        if (!_file || e->button() != Qt::LeftButton) return;
        const QPoint pos = e->position().toPoint();
        if (pos.y() >= rulerHeight) {
            const int row = (pos.y() - rulerHeight) / laneHeight;
            if (row >= 0 && row < _file->tracks()->size()) {
                MidiTrack *t = _file->tracks()->at(row);
                if (muteRect(rulerHeight + row * laneHeight).contains(pos)) {
                    _file->protocol()->startNewAction(t->muted() ? tr("Unmute Track") : tr("Mute Track"));
                    t->setMuted(!t->muted());
                    _file->protocol()->endAction();
                    update();
                    return;
                }
                if (soloRect(rulerHeight + row * laneHeight).contains(pos)) {
                    _solo = (_solo == t) ? nullptr : t;
                    _file->setSoloTrack(_solo);
                    update();
                    return;
                }
                setSelectedTrack(t);
                if (selectTrackRequested) selectTrackRequested(t);
            }
        }
        if (pos.x() >= headerWidth) {
            _dragging = true;
            _dragX = pos.x();
            _dragStartMs = _originMs;
            setCursor(Qt::ClosedHandCursor);
        }
    }

    void mouseMoveEvent(QMouseEvent *e) override {
        if (_dragging) {
            const int dx = e->position().toPoint().x() - _dragX;
            _originMs = std::max(0, _dragStartMs - int(dx / _scale));
            if (scrollTimeRequested) scrollTimeRequested(_originMs);
            update();
        } else {
            setCursor(e->position().x() >= headerWidth ? Qt::OpenHandCursor : Qt::ArrowCursor);
        }
    }
    void mouseReleaseEvent(QMouseEvent *e) override {
        if (e->button() == Qt::LeftButton) {
            _dragging = false;
            setCursor(Qt::ArrowCursor);
        }
    }
    void wheelEvent(QWheelEvent *e) override {
        if (e->modifiers() & Qt::ControlModifier) {
            const double factor = e->angleDelta().y() > 0 ? 1.2 : 1.0 / 1.2;
            _scale = std::clamp(_scale * factor, 0.004, 0.7);
        } else if (e->modifiers() & Qt::ShiftModifier) {
            _originMs = std::max(0, _originMs - int(e->angleDelta().y() * 4 / _scale));
        } else {
            QWidget::wheelEvent(e); // parent scroll area handles vertical wheel
            return;
        }
        if (scrollTimeRequested && (e->modifiers() & Qt::ShiftModifier)) scrollTimeRequested(_originMs);
        update();
        e->accept();
    }

private:
    struct PreviewNote { int beginMs; int endMs; int pitch; };
    QPointer<MidiFile> _file;
    MidiTrack *_selected = nullptr;
    MidiTrack *_solo = nullptr;
    QHash<MidiTrack *, QVector<PreviewNote>> _preview;
    int _originMs = 0;
    int _playheadMs = -1;
    double _scale = 0.065; // pixels per millisecond
    bool _dragging = false;
    int _dragX = 0;
    int _dragStartMs = 0;

    int xForMs(int ms) const { return headerWidth + int((ms - _originMs) * _scale); }
    QRect muteRect(int y) const { return QRect(130, y + 16, 25, 24); }
    QRect soloRect(int y) const { return QRect(160, y + 16, 25, 24); }
    static void drawButton(QPainter &p, const QRect &r, const QString &label,
                           bool active, const QColor &accent) {
        p.setBrush(active ? accent : QColor("#3A404B"));
        p.setPen(active ? accent : QColor("#5B6573"));
        p.drawRoundedRect(r, 4, 4);
        p.setPen(QColor("#F3F6FA"));
        p.drawText(r, Qt::AlignCenter, label);
    }
};

#endif // FFXIV_ARRANGEMENTWIDGET_H
