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
#include <QPolygon>
#include <QPointer>
#include <QToolTip>
#include <QMenu>
#include <QContextMenuEvent>
#include <QApplication>
#include <cstdlib>
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
    std::function<void(MidiTrack *)> soundFontPresetRequested;
    std::function<void(int)> scrollTimeRequested;
    // Commit a clicked time to the actual MIDI transport cursor, not just
    // the arrangement drawing. The owner handles playback/pause state.
    std::function<void(int)> seekTimeRequested;

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
        _trackPresetLabels.clear();
        _solo = nullptr;
        _originMs = 0;
        _dragging = false;
        _timelinePressed = false;
        _playheadMs = file ? file->msOfTick(file->cursorTick()) : -1;
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
    void setTrackPresetLabel(MidiTrack *track, const QString &label) {
        if (track) { _trackPresetLabels.insert(track, label); update(); }
    }

    // Playback and the Piano Roll receive the same MIDI player clock.
    // Scroll only when necessary so the cursor stays visible across all lanes.
    void setPlayheadMs(int ms) {
        if (!_file) return;
        _playheadMs = std::max(0, ms);
        const int timelineWidth = width() - headerWidth;
        if (!_dragging && timelineWidth > 80) {
            const int x = xForMs(_playheadMs);
            const int rightMargin = std::max(24, timelineWidth / 10);
            if (x < headerWidth || x > width() - rightMargin) {
                const int origin = std::max(0, _playheadMs -
                    int(timelineWidth * 0.20 / _scale));
                if (origin != _originMs) {
                    _originMs = origin;
                    if (scrollTimeRequested) scrollTimeRequested(origin);
                }
            }
        }
        update();
    }

    // A transport seek should move the playhead without auto-panning.
    void setCursorMs(int ms) {
        if (!_file) return;
        _playheadMs = std::max(0, ms);
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
                   QStringLiteral("TRACKS   |   Click to seek / Drag to pan"));
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
            const QString currentSound = _trackPresetLabels.value(t, tr("SoundFont…"));
            drawButton(p, soundRect(y), fm.elidedText(currentSound, Qt::ElideRight, 100),
                       _trackPresetLabels.contains(t), QColor("#587D99"));

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

        // Draw the header boundary first, then put the bright playback line on
        // top of every lane's regions/notes (including at time zero).
        p.setPen(QColor("#515966"));
        p.drawLine(headerWidth, 0, headerWidth, height());
        const int ms = _playheadMs >= 0 ? _playheadMs : _file->msOfTick(_file->cursorTick());
        const int x = xForMs(ms);
        if (x >= headerWidth && x < width()) {
            p.save();
            p.setClipRect(QRect(headerWidth, 0, width() - headerWidth, height()));
            // A dark outline keeps the playhead visible on bright track colors.
            p.setPen(QPen(QColor(12, 15, 20, 180), 5));
            p.drawLine(x, rulerHeight, x, height());
            p.setPen(QPen(QColor("#FFDD75"), 2));
            p.drawLine(x, 0, x, height());

            // Playhead tab in the ruler, like the arrangement cursor in a DAW.
            p.setPen(Qt::NoPen);
            p.setBrush(QColor("#FFDD75"));
            const QPolygon triangle{QPoint(x - 7, 4), QPoint(x + 7, 4),
                                    QPoint(x, 16)};
            p.drawPolygon(triangle);

            // One marker per track makes it clear that every lane is aligned
            // to the same playback position, including the selected track.
            for (int i = 0; i < tracks.size(); ++i) {
                const int y = rulerHeight + i * laneHeight;
                if (y >= height()) break;
                p.setBrush(tracks.at(i) == _selected ? QColor("#FFFFFF") :
                           QColor("#FFDD75"));
                p.drawEllipse(QPoint(x, y + laneHeight / 2), 3, 3);
            }
            p.restore();
        }
    }

    void mousePressEvent(QMouseEvent *e) override {
        if (!_file || e->button() != Qt::LeftButton) {
            QWidget::mousePressEvent(e);
            return;
        }
        const QPoint pos = e->position().toPoint();
        if (pos.y() >= rulerHeight && pos.x() < headerWidth) {
            const int row = (pos.y() - rulerHeight) / laneHeight;
            if (row >= 0 && row < _file->tracks()->size()) {
                MidiTrack *track = _file->tracks()->at(row);
                if (soundRect(rulerHeight + row * laneHeight).contains(pos)) {
                    setSelectedTrack(track);
                    if (selectTrackRequested) selectTrackRequested(track);
                    if (soundFontPresetRequested) soundFontPresetRequested(track);
                    update();
                } else if (muteRect(rulerHeight + row * laneHeight).contains(pos)) {
                    _file->protocol()->startNewAction(track->muted() ? tr("Unmute Track") : tr("Mute Track"));
                    track->setMuted(!track->muted());
                    _file->protocol()->endAction();
                    update();
                } else if (soloRect(rulerHeight + row * laneHeight).contains(pos)) {
                    _solo = (_solo == track) ? nullptr : track;
                    _file->setSoloTrack(_solo);
                    update();
                } else {
                    setSelectedTrack(track);
                    if (selectTrackRequested) selectTrackRequested(track);
                }
                e->accept();
                return;
            }
        }

        // Pressing the timeline does not seek yet: wait for release so a
        // click moves the playhead and a drag pans without accidentally seeking.
        if (pos.x() >= headerWidth) {
            _timelinePressed = true;
            _dragging = false;
            _dragX = pos.x();
            _dragStartMs = _originMs;
            setCursor(Qt::OpenHandCursor);
            e->accept();
            return;
        }
        QWidget::mousePressEvent(e);
    }

    void contextMenuEvent(QContextMenuEvent *e) override {
        if (!_file || e->pos().x() >= headerWidth || e->pos().y() < rulerHeight) {
            QWidget::contextMenuEvent(e);
            return;
        }
        const int row = (e->pos().y() - rulerHeight) / laneHeight;
        if (row < 0 || row >= _file->tracks()->size()) return;
        MidiTrack *track = _file->tracks()->at(row);
        QMenu menu(this);
        QAction *soundAction = menu.addAction(tr("Choose SoundFont Preset…"));
        if (menu.exec(e->globalPos()) == soundAction && soundFontPresetRequested) {
            setSelectedTrack(track);
            if (selectTrackRequested) selectTrackRequested(track);
            soundFontPresetRequested(track);
        }
        e->accept();
    }

    void mouseMoveEvent(QMouseEvent *e) override {
        const QPoint pos = e->position().toPoint();
        if (_timelinePressed) {
            const int dx = pos.x() - _dragX;
            if (!_dragging && std::abs(dx) >= QApplication::startDragDistance()) {
                _dragging = true;
                setCursor(Qt::ClosedHandCursor);
            }
            if (_dragging) {
                _originMs = std::max(0, _dragStartMs - int(dx / _scale));
                if (scrollTimeRequested) scrollTimeRequested(_originMs);
                update();
            }
            e->accept();
            return;
        }
        if (pos.x() >= headerWidth) {
            setCursor(pos.y() < rulerHeight ? Qt::PointingHandCursor : Qt::OpenHandCursor);
        } else {
            setCursor(Qt::ArrowCursor);
        }
        QWidget::mouseMoveEvent(e);
    }

    void mouseReleaseEvent(QMouseEvent *e) override {
        if (e->button() == Qt::LeftButton && _timelinePressed) {
            const QPoint pos = e->position().toPoint();
            const bool wasDragging = _dragging ||
                std::abs(pos.x() - _dragX) >= QApplication::startDragDistance();
            _dragging = false;
            _timelinePressed = false;
            setCursor(Qt::ArrowCursor);

            if (!wasDragging && _file && pos.x() >= headerWidth) {
                // Click anywhere in the timeline to seek. The owner converts
                // milliseconds to MIDI ticks using the file's tempo map.
                const int desiredMs = _originMs + int((pos.x() - headerWidth) / _scale);
                const int clampedMs = std::clamp(desiredMs, 0, _file->maxTime());
                if (pos.y() >= rulerHeight) {
                    const int row = (pos.y() - rulerHeight) / laneHeight;
                    if (row >= 0 && row < _file->tracks()->size()) {
                        MidiTrack *track = _file->tracks()->at(row);
                        setSelectedTrack(track);
                        if (selectTrackRequested) selectTrackRequested(track);
                    }
                }
                setCursorMs(clampedMs);
                if (seekTimeRequested) seekTimeRequested(clampedMs);
            }
            e->accept();
            return;
        }
        QWidget::mouseReleaseEvent(e);
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
    QHash<MidiTrack *, QString> _trackPresetLabels;
    int _originMs = 0;
    int _playheadMs = -1;
    double _scale = 0.065; // pixels per millisecond
    bool _dragging = false;
    bool _timelinePressed = false;
    int _dragX = 0;
    int _dragStartMs = 0;

    int xForMs(int ms) const { return headerWidth + int((ms - _originMs) * _scale); }
    QRect soundRect(int y) const { return QRect(12, y + 29, 108, 19); }
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
