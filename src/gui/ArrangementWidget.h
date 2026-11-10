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
#include <QPainterPath>
#include <QMouseEvent>
#include <QContextMenuEvent>
#include <QPoint>
#include <QPair>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QKeySequence>
#include <QResizeEvent>
#include <cmath>
#include <QHash>
#include <QVector>
#include <QScrollArea>
#include <QFontMetrics>
#include <QCursor>
#include <QPolygon>
#include <QPointer>
#include <QToolTip>
#include <QApplication>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMenu>
#include <QDialog>
#include <QInputDialog>
#include <QRadioButton>
#include <QButtonGroup>
#include <QVBoxLayout>
#include <QDialogButtonBox>
#include <QLabel>
#include <QDebug>
#include "../region/MidiRegionModel.h"
#include "../region/MidiRegionTransfer.h"
#include "../region/MidiRegionNoteGroups.h"
#include "../support/ChordVoiceFilter.h"
#include "../region/RegionSplitPrecision.h"
#include <cstdlib>
#include <functional>
#include <algorithm>
#include <limits>

#include "../midi/MidiFile.h"
#include "../midi/MidiPlayer.h"
#include "../midi/PlayerThread.h"
#include "../midi/MidiInput.h"
#include "../midi/MidiTrack.h"
#include "../midi/MidiChannel.h"
#include "../MidiEvent/NoteOnEvent.h"
#include "../MidiEvent/OffEvent.h"
#include "../protocol/Protocol.h"
#include "../protocol/ProtocolEntry.h"
#include "../protocol/ProtocolItem.h"

class ArrangementWidget final : public QWidget {
    enum class RegionDragMode { Move, Duplicate, TrimLeft, TrimRight, Loop };
public:
    explicit ArrangementWidget(QWidget *parent = nullptr) : QWidget(parent) {
        setMinimumHeight(130);
        setMouseTracking(true);
        setFocusPolicy(Qt::ClickFocus);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    }

    std::function<void(MidiTrack *)> selectTrackRequested;
    std::function<void(MidiTrack *, const QPoint &)> instrumentMenuRequested;
    std::function<void(MidiTrack *, const QPoint &)> trackMenuRequested;
    std::function<void(MidiTrack *)> renameTrackRequested;
    std::function<void(int)> scrollTimeRequested;
    // Range and page step (in milliseconds) for the arrangement-only scrollbar.
    std::function<void(int, int)> scrollRangeChanged;
    // Commit a clicked time to the actual MIDI transport cursor, not just
    // the arrangement drawing. The owner handles playback/pause state.
    std::function<void(int)> seekTimeRequested;
    // Selection and region edits update the lower Piano Roll's projection.
    std::function<void()> regionViewChanged;
    std::function<void()> inspectorChanged;
    std::function<void()> regionQuantizeRequested;
    std::function<int()> chordVoiceTolerance;
    std::function<void(int)> positionChanged;
    const xiv::MidiRegionModel &regionModel() const { return _regions; }
    MidiFile *midiFile() const { return _file.data(); }
    int selectedRegionId() const { return _selectedRegionId; }
    bool canEditQuantize() const {
        return _file && _regionsReady && !MidiPlayer::isPlaying() && !MidiInput::recording();
    }
    enum class QuantizeScope { Region, Track, Project };
    void applyQuantize(const xiv::RegionQuantize &settings,
                       QuantizeScope scope = QuantizeScope::Region) {
        if (!canEditQuantize() || !settings.valid()) return;
        const int track = _selected ? _file->tracks()->indexOf(_selected) : -1;
        std::vector<int> targets;
        for (const auto &region : _regions.regions()) {
            const bool inScope = scope == QuantizeScope::Project ||
                (scope == QuantizeScope::Track && region.trackIndex == track) ||
                (scope == QuantizeScope::Region && region.id == _selectedRegionId);
            if (inScope && !(region.quantize == settings)) targets.push_back(region.id);
        }
        if (targets.empty()) return;

        QString description;
        switch (scope) {
        case QuantizeScope::Region: description = tr("Quantize Region"); break;
        case QuantizeScope::Track: description = tr("Quantize Track Regions"); break;
        case QuantizeScope::Project: description = tr("Quantize Project Regions"); break;
        }
        ProtocolEntry *before = _regionHistory.copy();
        for (int id : targets) _regions.setQuantize(id, settings);
        finishRegionEdit(before, description);
    }
    void setSnapToGrid(bool on) { _snapToGrid = on; update(); }

    // Arrangement and Piano Roll use independent horizontal scroll positions.
    void setOriginMs(int ms) {
        ms = std::clamp(ms, 0, maxOriginMs());
        if (ms != _originMs) { _originMs = ms; update(); }
    }
    int maxOriginMs() const {
        if (!_file) return 0;
        const int visibleMs = visibleTimeMs();
        // Include an extra second of visual room after the last MIDI event.
        const auto songEnd = static_cast<long long>(std::max(
            _file->maxTime(), _file->msOfTick(_file->arrangementEndTick()))) + 12000;
        return static_cast<int>(std::clamp(songEnd - visibleMs,
                     0LL, static_cast<long long>(std::numeric_limits<int>::max())));
    }
    void refreshHorizontalRange() {
        const int maxMs = maxOriginMs();
        if (_originMs > maxMs) {
            _originMs = maxMs;
            if (scrollTimeRequested) scrollTimeRequested(_originMs);
        }
        if (scrollRangeChanged) scrollRangeChanged(maxMs, visibleTimeMs());
    }
    static constexpr int headerWidth = 326;
    static constexpr int rulerHeight = 34;
    void replaceRegionNotes(int id, std::vector<xiv::OwnedRegionNote> notes,
                            const QString &description) {
        const auto *region = _regions.find(id);
        if (!_file || MidiPlayer::isPlaying() || MidiInput::recording() ||
            id != _selectedRegionId || !region || region->loopLengthTicks ||
            region->quantize.active()) return;
        ProtocolEntry *before = _regionHistory.copy();
        if (!_regions.replaceOwnedNotes(id, std::move(notes))) {
            delete before;
            return;
        }
        finishRegionEdit(before, description);
    }

    void zoomTimeline(bool zoomIn) {
        if (!_file) return;
        zoomTimelineAt(zoomIn, headerWidth + std::max(0, width() - headerWidth) / 2);
    }

    void setRulerTop(int top) {
        if (top == _rulerTop) return;
        _rulerTop = top;
        update();
    }

    static constexpr int laneHeight = 84;

    void setFile(MidiFile *file) {
        if (_file != file) {
            _cycle = {};
            _cycleEnabled = false;
            _cycleDrag = CycleDrag::None;
            publishCycle();
        }
        _file = file;
        _selected = nullptr;
        _preview.clear();
        _regions.clear();
        _regionsReady = false;
        _regionTracks.clear();
        _lastSavedRegionTracks = file ? *file->tracks() : QList<MidiTrack *>{};
        _regionTrackEditPendingSave = false;
        _regionHistoryRefreshPending = false;
        _selectedRegionId = 0;
        _selectedRegionIds.clear();
        _solo = nullptr;
        _originMs = 0;
        if (scrollTimeRequested) scrollTimeRequested(0);
        _hoverTick = -1;
        _hoverRow = -1;
        _dragging = false;
        _timelinePressed = false;
        _regionPressed = false;
        _regionDragging = false;
        _moveRegionId = 0;
        _dragMode = RegionDragMode::Move;
        _playheadMs = file ? file->msOfTick(file->cursorTick()) : -1;
        if (file) {
            file->setSoloTrack(nullptr);
            connect(file->protocol(), &Protocol::actionFinished, this,
                    [this]() {
                        const bool refreshSelection = _regionHistoryRefreshPending;
                        _regionHistoryRefreshPending = false;
                        rebuildPreview();
                        if (refreshSelection && regionViewChanged) regionViewChanged();
                    });
            connect(file, &MidiFile::trackChanged, this,
                    [this]() { rebuildPreview(); });
        }
        rebuildPreview();
        refreshHorizontalRange();
        if (positionChanged) positionChanged(_playheadMs);
        if (regionViewChanged) regionViewChanged();
    }

    void setSelectedTrack(MidiTrack *track) {
        if (_selected == track) return;
        _selected = track;
        const auto *region = _regions.find(_selectedRegionId);
        if (region && (!_file || !_file->tracks()->contains(track) ||
                       _file->tracks()->at(region->trackIndex) != track)) {
            _selectedRegionId = 0;
            _selectedRegionIds.clear();
            if (regionViewChanged) regionViewChanged();
        }
        if (inspectorChanged) inspectorChanged();
        update();
    }
    MidiTrack *selectedTrack() const { return _selected; }

    // Playback and the Piano Roll receive the same MIDI player clock.
    // Scroll only when necessary so the cursor stays visible across all lanes.
    void setPlayheadMs(int ms) {
        if (!_file) return;
        _playheadMs = std::max(0, ms);
        if (positionChanged) positionChanged(_playheadMs);
        const int timelineWidth = width() - headerWidth;
        if (!_dragging && timelineWidth > 80) {
            const int x = xForMs(_playheadMs);
            const int rightMargin = std::max(24, timelineWidth / 10);
            if (x < headerWidth || x > width() - rightMargin) {
                const int origin = std::max(0, _playheadMs -
                    int(timelineWidth * 0.20 / _scale));
                const int scrollOrigin = std::min(origin, maxOriginMs());
                if (scrollOrigin != _originMs) {
                    _originMs = scrollOrigin;
                    if (scrollTimeRequested) scrollTimeRequested(scrollOrigin);
                }
            }
        }
        update();
    }

    // A transport seek should move the playhead without auto-panning.
    void setCursorMs(int ms) {
        if (!_file) return;
        _playheadMs = std::max(0, ms);
        if (positionChanged) positionChanged(_playheadMs);
        update();
    }

    void rebuildPreview() {
        if (_regionHistoryRefreshPending && _file && *_file->tracks() != _regionTracks)
            return;
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
                _preview[note->track()].append({begin, std::max(end, begin + 1), note->note(),
                                                note->midiTime(), note->offEvent()->midiTime(),
                                                note->velocity(), note->channel()});
            }
        }
        const auto &tracks = *_file->tracks();
        const int count = tracks.size();
        if (!_regionsReady || tracks != _regionTracks) {
            const bool appendOnly = _regionsReady && count >= _regionTracks.size() &&
                std::equal(_regionTracks.cbegin(), _regionTracks.cend(), tracks.cbegin());
            const int firstNewTrack = appendOnly ? _regionTracks.size() : 0;
            if (!appendOnly) {
                // An index-based layout cannot safely follow reordered or removed tracks.
                _regions.clear();
                _selectedRegionId = 0;
                _selectedRegionIds.clear();
            }
            for (int i = firstNewTrack; i < count; ++i) {
                MidiTrack *track = _file->tracks()->at(i);
                const auto &notes = _preview[track];
                if (notes.isEmpty()) continue;
                int lastTick = notes.first().endTick;
                for (const auto &note : notes) {
                    lastTick = std::max(lastTick, note.endTick);
                }
                // Start at the beginning of the arrangement, not the first
                // audible note. Otherwise a click in a track's initial silence
                // cannot split the region at the requested time.
                _regions.addInitial(i, 0, std::max(1, lastTick));
            }
            _regionTracks = tracks;
            if (!_regionsReady) loadRegionLayout();
            _regionsReady = true;
        }
        _file->setRegionLayout(_regions);
        publishCycle();
        setMinimumHeight(std::max(130, rulerHeight + count * laneHeight));
        updateGeometry();
        refreshHorizontalRange();
        update();
        if (inspectorChanged) inspectorChanged();
    }

    // MIDI has no region metadata. The native layout is stored separately next
    // to the MIDI: song.mid.xivms-regions.json. Writes are atomic; never edit
    // or overwrite the MIDI events as part of region splitting.
    bool saveRegionLayout(const QString &midiPath = QString()) {
        if (!_regionsReady || !_file) return false;
        // Track creation/removal must reach the MIDI and its sidecar together.
        if (_regionTrackEditPendingSave && midiPath.isEmpty()) return true;
        const QString path = midiPath.isEmpty() ? _file->path() : midiPath;
        if (path.trimmed().isEmpty() || !QFileInfo::exists(path)) return false;
        QJsonObject root;
        root.insert(QStringLiteral("format"), QStringLiteral("XIV Midi Studio regions"));
        int version = 1;
        root.insert(QStringLiteral("tracks"), _file->numTracks());
        QJsonArray items;
        for (const xiv::MidiRegion &r : _regions.regions()) {
            QJsonObject entry;
            entry.insert(QStringLiteral("id"), r.id);
            entry.insert(QStringLiteral("track"), r.trackIndex);
            entry.insert(QStringLiteral("startTick"), r.startTick);
            entry.insert(QStringLiteral("sourceStartTick"), r.sourceStartTick);
            entry.insert(QStringLiteral("sourceEndTick"), r.sourceEndTick);
            entry.insert(QStringLiteral("startNoteMode"), static_cast<int>(r.startNoteMode));
            entry.insert(QStringLiteral("endNoteMode"), static_cast<int>(r.endNoteMode));
            entry.insert(QStringLiteral("loopLengthTicks"), r.loopLengthTicks);
            entry.insert(QStringLiteral("quantizeGridTicks"), r.quantize.gridTicks);
            entry.insert(QStringLiteral("quantizeStrength"), r.quantize.strength);
            entry.insert(QStringLiteral("quantizeSwing"), r.quantize.swing);
            if (r.ownedNotes) {
                version = 2;
                QJsonArray notes;
                for (const auto &note : *r.ownedNotes)
                    notes.append(QJsonArray{note.id, note.startTick, note.endTick,
                                            note.pitch, note.velocity, note.channel});
                entry.insert(QStringLiteral("ownedNotes"), notes);
            }
            items.append(entry);
        }
        root.insert(QStringLiteral("version"), version);
        root.insert(QStringLiteral("regions"), items);
        QJsonArray managed;
        for (int track : _regions.managedTracks()) managed.append(track);
        root.insert(QStringLiteral("managedTracks"), managed);
        QJsonObject assignedChannels;
        for (const auto &region : _regions.regions()) {
            if (!region.ownedNotes) continue;
            const int channel = _file->tracks()->at(region.trackIndex)->assignedChannel();
            if (channel >= 0 && channel < 16)
                assignedChannels.insert(QString::number(region.trackIndex), channel);
        }
        if (!assignedChannels.isEmpty())
            root.insert(QStringLiteral("assignedChannels"), assignedChannels);
        QSaveFile output(path + QStringLiteral(".xivms-regions.json"));
        if (!output.open(QIODevice::WriteOnly)) return false;
        const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
        if (output.write(bytes) != bytes.size()) { output.cancelWriting(); return false; }
        if (!output.commit()) return false;
        _lastSavedRegionTracks = *_file->tracks();
        _regionTrackEditPendingSave = false;
        return true;
    }

protected:
    bool event(QEvent *e) override {
        // Keep region shortcuts local to the Arrangement. Ctrl+T still
        // handles transposition elsewhere.
        if (e->type() == QEvent::ShortcutOverride) {
            const auto *key = static_cast<QKeyEvent *>(e);
            if ((_selectedRegionId && isSplitShortcut(key)) || isJoinShortcut(key)) {
                e->accept();
                return true;
            }
        }
        return QWidget::event(e);
    }

    void keyPressEvent(QKeyEvent *e) override {
        if (isJoinShortcut(e)) {
            if (!e->isAutoRepeat()) joinSelectedRegions(_selectedRegionId);
            e->accept();
            return;
        }
        if (_selectedRegionId && isSplitShortcut(e)) {
            if (_file && !e->isAutoRepeat()) splitSelectedAtPlayhead();
            e->accept();
            return;
        }
        if (e->key() == Qt::Key_Backspace && e->modifiers() == Qt::NoModifier &&
            _selectedRegionId && !MidiPlayer::isPlaying()) {
            deleteSelectedRegion();
            e->accept();
            return;
        }
        QWidget::keyPressEvent(e);
    }
    void resizeEvent(QResizeEvent *event) override {
        QWidget::resizeEvent(event);
        refreshHorizontalRange();
    }

    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, false);
        p.fillRect(rect(), QColor("#202329"));
        p.fillRect(0, 0, headerWidth, height(), QColor("#292D34"));
        if (!_file) {
            drawRuler(p, {}, -1);
            return;
        }
        // Tempo-aware quarter grid plus actual bar boundaries, including
        // time-signature changes (no hardcoded 4/4 assumption).
        const int tpq = std::max(1, _file->ticksPerQuarter());
        const int endMs = _originMs + int(std::max(0, width() - headerWidth) / _scale);
        const int endTick = std::max(0, _file->tick(endMs));
        const int firstQuarter = std::max(0, _file->tick(_originMs) / tpq - 2);
        const int lastQuarter = std::max(firstQuarter, endTick / tpq + 2);
        // Cache the grid positions once, then draw them after EACH lane's
        // background. Previously the grid was painted here and the opaque
        // lane backgrounds immediately covered it.
        QVector<int> beatGridXs;
        QVector<int> subBeatGridXs;
        QVector<QPair<int, int>> measureMarks;
        const int step16 = std::max(1, tpq / 4);
        for (int q = firstQuarter; q <= lastQuarter && q - firstQuarter < 2000; ++q) {
            const int quarterTick = q * tpq;
            const int x = xForMs(_file->msOfTick(quarterTick));
            if (x >= headerWidth && x <= width()) beatGridXs.append(x);
            // Show sixteenth guides only when zoomed in sufficiently.
            if (xForMs(_file->msOfTick(quarterTick + step16)) - x >= 12) {
                for (int sub = 1; sub < 4; ++sub) {
                    const int subX = xForMs(_file->msOfTick(quarterTick + sub * step16));
                    if (subX >= headerWidth && subX <= width()) subBeatGridXs.append(subX);
                }
            }
        }
        int barStart = 0, barEnd = 0;
        int bar = _file->measure(std::max(0, _file->tick(_originMs)), &barStart, &barEnd);
        for (int step = 0; step < 1000 && barStart <= endTick; ++step) {
            const int x = xForMs(_file->msOfTick(barStart));
            if (x >= headerWidth && x <= width())
                measureMarks.append(qMakePair(x, bar));
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
            // Three-level musical grid, behind the region color and MIDI
            // notes. Absolute tick positioning keeps it aligned during
            // independent scrolling, zooming and meter/tempo changes.
            p.save();
            p.setClipRect(QRect(headerWidth, y, std::max(0, width() - headerWidth),
                                laneHeight - 1));
            p.setPen(QColor("#353A45"));
            for (int x : subBeatGridXs) p.drawLine(x, y + 1, x, y + laneHeight - 2);
            p.setPen(QColor("#4A5360"));
            for (int x : beatGridXs) p.drawLine(x, y + 1, x, y + laneHeight - 2);
            p.setPen(QPen(QColor("#748092"), 1));
            for (const auto &mark : measureMarks)
                p.drawLine(mark.first, y + 1, mark.first, y + laneHeight - 2);
            p.restore();
            p.setPen(QColor("#414753"));
            p.drawLine(0, y + laneHeight - 1, width(), y + laneHeight - 1);
            p.fillRect(0, y + 2, 4, laneHeight - 4, c);
            const QString name = t->name().trimmed().isEmpty() ?
                                 QStringLiteral("Track %1").arg(i + 1) : t->name();
            p.setFont(QFont(QStringLiteral("Segoe UI"), 11, QFont::DemiBold));
            p.setPen(QColor("#F4F7FD"));
            QFontMetrics fm(p.font());
            p.drawText(14, y + 24, fm.elidedText(name, Qt::ElideRight, headerWidth - 28));
            p.setFont(QFont(QStringLiteral("Segoe UI"), 9));
            p.setPen(QColor("#ACBACD"));
            // ASCII delimiters avoid mojibake on MSVC installations with a
            // legacy source character set (previously rendered as 'A' + junk).
            p.drawText(14, y + 44, QStringLiteral("Track %1   |   Channel %2")
                       .arg(i + 1).arg(t->assignedChannel() + 1));

            drawButton(p, instrumentRect(y), TrackControl::Instrument, false, QColor("#45647F"));
            drawButton(p, visibilityRect(y), TrackControl::Visibility, !t->hidden(), QColor("#1767A4"));
            drawButton(p, muteRect(y), TrackControl::Mute, t->muted(), QColor("#A83C4B"));
            drawButton(p, soloRect(y), TrackControl::Solo, t == _solo, QColor("#9B761D"));
            drawButton(p, actionsRect(y), TrackControl::More, false, QColor("#55748E"));

            const auto &notes = _preview[t];
            // Draw region backgrounds first; note previews are rendered later
            // from the same region schedule as playback and arranged export.
            for (const auto &region : _regions.regions()) {
                if (region.trackIndex != i) continue;
                const int x0 = std::max(headerWidth + 3,
                    xForMs(_file->msOfTick(region.startTick)));
                const int x1 = std::min(width() - 2,
                    xForMs(_file->msOfTick(region.endTick())));
                if (x1 <= x0) continue;
                const bool selectedRegion = region.id == _selectedRegionId || _selectedRegionIds.count(region.id);
                QColor accent = c.isValid() ? c : QColor("#7EA4D4");
                int hue = 0, saturation = 0, lightness = 0, alpha = 255;
                accent.getHsl(&hue, &saturation, &lightness, &alpha);
                if (hue >= 0) {
                    accent.setHsl(hue, std::clamp(saturation, 65, 135),
                                  std::clamp(lightness, 100, 160));
                }
                QColor regionFill = accent;
                regionFill.setAlpha(selectedRegion ? 83 : (chosen ? 66 : 44));
                const QRect regionRect(x0, y + 6, x1 - x0, laneHeight - 12);
                p.setBrush(regionFill);
                p.setPen(QPen(selectedRegion ? QColor("#FFD18B") : accent,
                              selectedRegion ? 2 : 1));
                const QRect outline = regionRect.adjusted(0, 0, -1, -1);
                if (region.loopLengthTicks > region.duration()) {
                    p.save();
                    p.setRenderHint(QPainter::Antialiasing);
                    p.drawPath(loopOutline(outline, region.startTick, region.endTick(), region.duration()));
                    p.restore();
                } else {
                    p.fillRect(regionRect, regionFill);
                    p.setBrush(Qt::NoBrush);
                    p.drawRect(outline);
                }
                // A small title identifies each selectable region.
                p.setFont(QFont(QStringLiteral("Segoe UI"), 8, QFont::DemiBold));
                p.setPen(selectedRegion ? QColor("#FFE0AC") : QColor("#AFBBCB"));
                p.drawText(regionRect.adjusted(5, 3, -4, -3),
                           Qt::AlignLeft | Qt::AlignTop,
                           QStringLiteral("Region %1").arg(region.id));
                // Dedicated upper-right looping handle, distinct from
                // draggable left/right trim edges.
                if (regionRect.width() >= 47) {
                    QRect loopIcon(regionRect.right() - 32, regionRect.top() + 3, 22, 22);
                    p.fillRect(loopIcon, QColor(25, 34, 43, 170));
                    QFont loopFont = p.font();
                    loopFont.setPixelSize(17);
                    loopFont.setBold(true);
                    p.setFont(loopFont);
                    p.setPen(QColor("#E8B66D"));
                    p.drawText(loopIcon, Qt::AlignCenter, QString(QChar(ushort(0x21BB))));
                    if (region.loopLengthTicks > 0) {
                        p.setPen(QColor("#FFE0A4"));
                        p.drawText(regionRect.adjusted(6, 20, -5, -3),
                                   Qt::AlignLeft | Qt::AlignTop,
                                   tr("Loop"));
                    }
                }
            }

            // Draw all note previews AFTER all region backgrounds/borders.
            // Per-region clipped projections made Keep and Shorten appear
            // identical, even though playback/export used different schedules.
            // The same region renderer used by audio/export now drives the
            // arrangement preview.  Keep notes can visibly sustain past a
            // region boundary; Shorten notes stop; Split notes retrigger.
            std::vector<xiv::SourceRegionNote> sourceNotes;
            sourceNotes.reserve(static_cast<std::size_t>(notes.size()));
            int lowestPitch = 127, highestPitch = 0;
            int previewId = 1;
            for (const auto &n : notes) {
                if (n.endTick <= n.beginTick) continue;
                sourceNotes.push_back({previewId++, i, n.beginTick, n.endTick,
                                       n.pitch, 100, 0});
            }
            const auto renderedNotes = xiv::renderRegionNotes(sourceNotes, _regions, i);
            if (!renderedNotes.empty()) {
                for (const auto &note : renderedNotes) {
                    lowestPitch = std::min(lowestPitch, note.pitch);
                    highestPitch = std::max(highestPitch, note.pitch);
                }
                const int pitchRange = std::max(1, highestPitch - lowestPitch);
                QColor previewAccent = c.isValid() ? c : QColor("#7EA4D4");
                int h = 0, s = 0, l = 0, a = 255;
                previewAccent.getHsl(&h, &s, &l, &a);
                if (h >= 0)
                    previewAccent.setHsl(h, std::clamp(s, 65, 135),
                                         std::clamp(l, 100, 160));
                p.save();
                p.setClipRect(QRect(headerWidth + 3, y + 2,
                                    std::max(0, width() - headerWidth - 6),
                                    laneHeight - 4));
                p.setPen(Qt::NoPen);
                p.setBrush(previewAccent.lighter(145));
                for (const auto &note : renderedNotes) {
                    const int sx = std::max(headerWidth + 4,
                        xForMs(_file->msOfTick(note.startTick)));
                    const int ex = std::min(width() - 3,
                        xForMs(_file->msOfTick(note.endTick)));
                    if (ex <= sx) continue;
                    const int ny = y + laneHeight - 17 - (note.pitch - lowestPitch) * (laneHeight - 52) / pitchRange;
                    p.drawRect(QRect(sx, ny, std::max(1, ex - sx), 4));
                }
                p.restore();
            }
        }

        if (_regionDragging && _moveRegionId > 0) {
            const xiv::MidiRegion *moving = _regions.find(_moveRegionId);
            if (moving) {
                const int target = _moveProposedTick;
                int previewStart = moving->startTick;
                int previewEnd = moving->endTick();
                bool valid = false;
                QString description;
                switch (_dragMode) {
                case RegionDragMode::Move: {
                    previewStart = target;
                    const auto end = static_cast<int64_t>(target) + moving->displayDuration();
                    previewEnd = static_cast<int>(std::min<int64_t>(end, std::numeric_limits<int>::max()));
                    valid = canPlaceMove(*moving, target, _movePreviewRow);
                    description = tr("Move region");
                    break;
                }
                case RegionDragMode::Duplicate:
                    previewStart = target;
                    previewEnd = target + moving->displayDuration();
                    valid = _regions.canDuplicate(moving->id, target);
                    description = tr("Duplicate region");
                    break;
                case RegionDragMode::TrimLeft:
                    previewStart = target;
                    valid = _regions.canTrimLeft(moving->id, target);
                    description = tr("Trim left edge");
                    break;
                case RegionDragMode::TrimRight:
                    previewEnd = target;
                    valid = _regions.canTrimRight(moving->id, target,
                                                  sourceEndTick(moving->trackIndex));
                    description = tr("Trim right edge");
                    break;
                case RegionDragMode::Loop:
                    previewEnd = target;
                    valid = _regions.canLoop(moving->id, target);
                    description = tr("Loop region (full or partial repeat)");
                    break;
                }
                if (previewEnd <= previewStart) valid = false;
                const int left = xForMs(_file->msOfTick(std::max(0, previewStart)));
                const int right = xForMs(_file->msOfTick(std::max(0, previewEnd)));
                const int previewRow = _dragMode == RegionDragMode::Move && _movePreviewRow >= 0 &&
                    _movePreviewRow < tracks.size() ? _movePreviewRow : moving->trackIndex;
                const int top = rulerHeight + previewRow * laneHeight;
                p.save();
                p.setClipRect(QRect(headerWidth, rulerHeight,
                                    width()-headerWidth, height()-rulerHeight));
                p.setBrush(QColor(valid ? 47 : 170, valid ? 172 : 57,
                                  valid ? 109 : 65, 62));
                p.setPen(QPen(QColor(valid ? "#70E4A0" : "#ED6977"),
                              2, Qt::DashLine));
                if (valid && _dragMode == RegionDragMode::Loop) {
                    const int visibleLeft = std::max(left, headerWidth);
                    const int visibleRight = std::min(right, width() - 1);
                    if (visibleLeft < visibleRight) {
                        p.setRenderHint(QPainter::Antialiasing);
                        p.drawPath(loopOutline(QRect(QPoint(visibleLeft, top + 7),
                                                    QPoint(visibleRight, top + laneHeight - 7)),
                                               previewStart, previewEnd, moving->duration()));
                    }
                } else {
                    p.drawRect(QRect(left, top + 7, std::max(1, right - left),
                                     laneHeight - 14));
                }
                p.setPen(QColor("#FFFFFF"));
                p.drawText(QRect(left+5, top+16, std::max(40, right-left-8), 20),
                           Qt::AlignLeft | Qt::AlignVCenter,
                           valid ? description : tr("Placement blocked"));
                p.restore();
            }
        }

        // An accurate hover guide previews the exact split tick represented
        // by the cursor; this is not a snap-to-grid marker.
        if (_hoverTick >= 0 && _hoverRow >= 0 && _hoverRow < tracks.size()) {
            const int guideX = xForMs(_file->msOfTick(_hoverTick));
            const int guideY = rulerHeight + _hoverRow * laneHeight;
            if (guideX >= headerWidth && guideX < width()) {
                p.save();
                p.setPen(QPen(QColor("#FFCB75"), 1, Qt::DashLine));
                p.drawLine(guideX, guideY, guideX, guideY + laneHeight - 2);
                p.restore();
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

            p.setPen(Qt::NoPen);
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
        drawRuler(p, measureMarks, x);
    }

    void contextMenuEvent(QContextMenuEvent *e) override {
        if (_file && e->pos().x() >= headerWidth && isLaneY(e->pos().y())) {
            const QPoint pos = e->pos();
            const int row = (pos.y() - rulerHeight) / laneHeight;
            const int tick = snapTick(timelineTickAt(pos.x()));
            const auto *region = hitRegion(row, pos.x());
            if (region && row >= 0 && row < _file->tracks()->size()) {
                const int id = region->id;
                if (!_selectedRegionIds.count(id)) _selectedRegionIds = {id};
                _selectedRegionId = id;
                setFocus(Qt::MouseFocusReason);
                MidiTrack *track = _file->tracks()->at(row);
                setSelectedTrack(track);
                if (selectTrackRequested) selectTrackRequested(track);
                if (regionViewChanged) regionViewChanged();
                QMenu menu(this);
                QAction *join = menu.addAction(tr("Join Selected Regions Here") + QLatin1Char('\t') +
                    QKeySequence(QKeyCombination(Qt::ControlModifier, Qt::Key_J)).toString(QKeySequence::NativeText));
                join->setEnabled(_selectedRegionIds.size() > 1 && canEditQuantize());
                menu.addSeparator();
                QAction *splitHere = menu.addAction(tr("Split Here"));
                splitHere->setEnabled(canSplitAt(*region, tick));
                const int cursorTick = _file->cursorTick();
                QAction *splitAtCursor = menu.addAction(
                    tr("Split at Playhead") + QLatin1Char('\t') +
                    QKeySequence(QKeyCombination(Qt::ControlModifier, Qt::Key_T)).toString(QKeySequence::NativeText));
                splitAtCursor->setEnabled(std::any_of(_selectedRegionIds.begin(),
                    _selectedRegionIds.end(), [&](int selected) {
                        const auto *target = _regions.find(selected);
                        return target && canSplitAt(*target, cursorTick);
                    }));
                menu.addSeparator();
                addRegionNoteActions(menu, id);
                menu.addSeparator();
                QAction *deleteRegionAction = menu.addAction(tr("Delete Region (Backspace)"));
                deleteRegionAction->setEnabled(!MidiPlayer::isPlaying());
                QAction *chosen = menu.exec(e->globalPos());
                if (chosen == join) joinSelectedRegions(id);
                else if (chosen == splitHere) splitRegion(id, tick);
                else if (chosen == splitAtCursor) splitSelectedAtPlayhead();
                else if (chosen == deleteRegionAction) deleteSelectedRegion();
                update();
                e->accept();
                return;
            }
        }
        if (_file && e->pos().x() < headerWidth && isLaneY(e->pos().y())) {
            const int row = (e->pos().y() - rulerHeight) / laneHeight;
            if (row >= 0 && row < _file->tracks()->size()) {
                MidiTrack *track = _file->tracks()->at(row);
                setSelectedTrack(track);
                if (selectTrackRequested) selectTrackRequested(track);
                if (trackMenuRequested) trackMenuRequested(track, e->globalPos());
                e->accept();
                return;
            }
        }
        QWidget::contextMenuEvent(e);
    }

    void mouseDoubleClickEvent(QMouseEvent *e) override {
        if (_file && e->button() == Qt::LeftButton && e->position().x() < headerWidth &&
            isLaneY(int(e->position().y())) &&
            (int(e->position().y()) - rulerHeight) % laneHeight < 47) {
            const int row = (int(e->position().y()) - rulerHeight) / laneHeight;
            if (row >= 0 && row < _file->tracks()->size()) {
                MidiTrack *track = _file->tracks()->at(row);
                if (renameTrackRequested) renameTrackRequested(track);
                e->accept();
                return;
            }
        }
        QWidget::mouseDoubleClickEvent(e);
    }

    void mousePressEvent(QMouseEvent *e) override {
        if (!_file || e->button() != Qt::LeftButton) {
            QWidget::mousePressEvent(e);
            return;
        }
        const QPoint pos = e->position().toPoint();
        if (inCycleStrip(pos)) {
            beginCycleDrag(pos.x());
            e->accept();
            return;
        }
        if (isLaneY(pos.y()) && pos.x() < headerWidth) {
            const int row = (pos.y() - rulerHeight) / laneHeight;
            if (row >= 0 && row < _file->tracks()->size()) {
                MidiTrack *track = _file->tracks()->at(row);
                if (instrumentRect(rulerHeight + row * laneHeight).contains(pos)) {
                    setSelectedTrack(track);
                    if (selectTrackRequested) selectTrackRequested(track);
                    if (instrumentMenuRequested) instrumentMenuRequested(track, mapToGlobal(pos));
                } else if (visibilityRect(rulerHeight + row * laneHeight).contains(pos)) {
                    _file->protocol()->startNewAction(track->hidden() ? tr("Show Track") : tr("Hide Track"));
                    track->setHidden(!track->hidden());
                    _file->protocol()->endAction();
                    update();
                } else if (actionsRect(rulerHeight + row * laneHeight).contains(pos)) {
                    setSelectedTrack(track);
                    if (selectTrackRequested) selectTrackRequested(track);
                    if (trackMenuRequested) trackMenuRequested(track, mapToGlobal(pos));
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

        // A click on a real MIDI region selects it; a blank timeline click
        // still seeks and a drag across empty space still pans the view.
        if (pos.x() >= headerWidth && isLaneY(pos.y())) {
            const int row = (pos.y() - rulerHeight) / laneHeight;
            const xiv::MidiRegion *r = hitRegion(row, pos.x());
            if (r && row >= 0 && row < _file->tracks()->size()) {
                setFocus(Qt::MouseFocusReason);
                const bool add = e->modifiers().testFlag(Qt::ShiftModifier);
                if (add && (r->id == _selectedRegionId || _selectedRegionIds.count(r->id))) {
                    const int tick = timelineTickAt(pos.x());
                    const auto &regions = _regions.regions();
                    const auto next = std::find_if(regions.crbegin(), regions.crend(), [&](const auto &candidate) {
                        return candidate.id != _selectedRegionId && !_selectedRegionIds.count(candidate.id) &&
                               candidate.trackIndex == row && candidate.contains(tick);
                    });
                    if (next != regions.crend()) r = &*next;
                }
                if (add && _selectedRegionId) _selectedRegionIds.insert(_selectedRegionId);
                if (add) {
                    if (!_selectedRegionIds.erase(r->id)) _selectedRegionIds.insert(r->id);
                    _selectedRegionId = _selectedRegionIds.count(r->id) ? r->id :
                        (_selectedRegionIds.empty() ? 0 : *_selectedRegionIds.begin());
                } else {
                    _selectedRegionIds = {r->id};
                    _selectedRegionId = r->id;
                }
                if (!add && !MidiPlayer::isPlaying() && !MidiInput::recording()) {
                    _regionPressed = true;
                    _regionDragging = false;
                    _moveRegionId = r->id;
                    _moveRow = row;
                    _movePreviewRow = row;
                    _movePressedY = pos.y();
                    _movePressedX = pos.x();
                    _movePressedTick = timelineTickAt(pos.x());
                    _moveOffsetTick = _movePressedTick - r->startTick;
                    // Ctrl+drag copies the source material. Without Ctrl,
                    // the first/last 8px are trim handles, not a region move.
                    const int left = xForMs(_file->msOfTick(r->startTick));
                    const int right = xForMs(_file->msOfTick(r->endTick()));
                    const bool leftHandle = left >= headerWidth &&
                        std::abs(pos.x() - left) <= 8;
                    const bool rightHandle = right < width() &&
                        std::abs(pos.x() - right) <= 8;
                    const int regionTop = rulerHeight + row * laneHeight + 6;
                    const bool loopHandle = right - left >= 47 &&
                        pos.x() >= right - 34 && pos.x() < right - 8 &&
                        pos.y() >= regionTop && pos.y() < regionTop + 29;
                    _dragMode = loopHandle ? RegionDragMode::Loop :
                        (e->modifiers() & Qt::ControlModifier) ? RegionDragMode::Duplicate :
                        leftHandle ? RegionDragMode::TrimLeft :
                        rightHandle ? RegionDragMode::TrimRight :
                        RegionDragMode::Move;
                    _moveProposedTick = (_dragMode == RegionDragMode::TrimRight ||
                                         _dragMode == RegionDragMode::Loop) ?
                        r->endTick() : r->startTick;
                }
                const auto *primary = _regions.find(_selectedRegionId);
                MidiTrack *track = _file->tracks()->at(primary ? primary->trackIndex : row);
                setSelectedTrack(track);
                if (selectTrackRequested) selectTrackRequested(track);
                if (regionViewChanged) regionViewChanged();
                update();
                e->accept();
                return;
            }
        }
        // Pressing the blank timeline does not seek yet: wait for release
        // so a click seeks and a drag pans without accidentally seeking.
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

    void mouseMoveEvent(QMouseEvent *e) override {
        const QPoint pos = e->position().toPoint();
        if (_cycleDrag != CycleDrag::None) {
            updateCycleDrag(pos.x());
            e->accept();
            return;
        }
        if (inCycleStrip(pos) && !_timelinePressed && !_regionPressed) {
            const auto hit = cycleHit(pos.x());
            setCursor(hit == CycleDrag::Left || hit == CycleDrag::Right
                ? Qt::SizeHorCursor : hit == CycleDrag::Move ? Qt::OpenHandCursor : Qt::CrossCursor);
            QToolTip::showText(e->globalPosition().toPoint(),
                tr("Cycle playback: drag to draw or move the range, drag an edge to resize, click the ribbon to toggle."),
                this);
            e->accept();
            return;
        }
        int hoverTick = -1, hoverRow = -1;
        if (_file && pos.x() >= headerWidth && isLaneY(pos.y())) {
            const int row = (pos.y() - rulerHeight) / laneHeight;
            const int tick = snapTick(timelineTickAt(pos.x()));
            if (_regions.at(row, tick)) { hoverTick = tick; hoverRow = row; }
        }
        if (_hoverTick != hoverTick || _hoverRow != hoverRow) {
            _hoverTick = hoverTick;
            _hoverRow = hoverRow;
            update();
        }
        if (_regionPressed && _moveRegionId > 0) {
            const int horizontalDistance = std::abs(pos.x() - _movePressedX);
            const int verticalDistance = _dragMode == RegionDragMode::Move
                ? std::abs(pos.y() - _movePressedY) : 0;
            if (!_regionDragging &&
                std::max(horizontalDistance, verticalDistance) >= QApplication::startDragDistance()) {
                _regionDragging = true;
                setCursor((_dragMode == RegionDragMode::TrimLeft ||
                           _dragMode == RegionDragMode::TrimRight) ?
                          Qt::SizeHorCursor : Qt::SizeAllCursor);
            }
            if (_regionDragging) {
                const int mouseTick = timelineTickAt(pos.x());
                // Moving and copying preserve the original mouse offset;
                // trimming instead follows the dragged region edge exactly.
                const int gridTick = snapTick(mouseTick);
                _moveProposedTick =
                    (_dragMode == RegionDragMode::Move ||
                     _dragMode == RegionDragMode::Duplicate) ?
                    std::max(0, snapTick(mouseTick - _moveOffsetTick)) : gridTick;
                if (_dragMode == RegionDragMode::Move) {
                    _movePreviewRow = isLaneY(pos.y()) ? (pos.y() - rulerHeight) / laneHeight : -1;
                    // A straight vertical drag preserves the exact starting tick.
                    if (horizontalDistance < QApplication::startDragDistance()) {
                        if (const auto *region = _regions.find(_moveRegionId))
                            _moveProposedTick = region->startTick;
                    }
                }
                update();
            }
            e->accept();
            return;
        }
        if (_timelinePressed) {
            const int dx = pos.x() - _dragX;
            if (!_dragging && std::abs(dx) >= QApplication::startDragDistance()) {
                _dragging = true;
                setCursor(Qt::ClosedHandCursor);
            }
            if (_dragging) {
                _originMs = std::clamp(_dragStartMs - int(dx / _scale), 0, maxOriginMs());
                if (scrollTimeRequested) scrollTimeRequested(_originMs);
                update();
            }
            e->accept();
            return;
        }
        if (_file && pos.x() < headerWidth && isLaneY(pos.y())) {
            const int row = (pos.y() - rulerHeight) / laneHeight;
            if (row >= 0 && row < _file->tracks()->size()) {
                const int top = rulerHeight + row * laneHeight;
                QString tooltip;
                if (instrumentRect(top).contains(pos)) tooltip = tr("Choose a loaded SoundFont instrument and automatically rename this track");
                else if (visibilityRect(top).contains(pos)) tooltip = tr("Show / hide track notes in Piano Roll");
                else if (muteRect(top).contains(pos)) tooltip = tr("Mute / unmute track during playback");
                else if (soloRect(top).contains(pos)) tooltip = tr("Solo track during playback");
                else if (actionsRect(top).contains(pos)) tooltip = tr("All track actions: merge, move, quantize, transpose, FFXIV chord filter, split, select and more");
                if (!tooltip.isEmpty()) {
                    setCursor(Qt::PointingHandCursor);
                    QToolTip::showText(e->globalPosition().toPoint(), tooltip, this);
                } else {
                    QToolTip::hideText();
                    setCursor(Qt::ArrowCursor);
                }
                QWidget::mouseMoveEvent(e);
                return;
            }
        }
        QToolTip::hideText();
        if (_file && pos.x() >= headerWidth && isLaneY(pos.y())) {
            const int row = (pos.y() - rulerHeight) / laneHeight;
            if (const auto *r = hitRegion(row, pos.x())) {
                const int left = xForMs(_file->msOfTick(r->startTick));
                const int right = xForMs(_file->msOfTick(r->endTick()));
                const int regionTop = rulerHeight + row * laneHeight + 6;
                const bool loopHandle = right - left >= 47 &&
                    pos.x() >= right - 34 && pos.x() < right - 8 &&
                    pos.y() >= regionTop && pos.y() < regionTop + 29;
                const bool edge = (left >= headerWidth && std::abs(pos.x()-left) <= 8) ||
                                  (right < width() && std::abs(pos.x()-right) <= 8);
                setCursor(loopHandle || edge ? Qt::SizeHorCursor : Qt::SizeAllCursor);
                if (loopHandle) QToolTip::showText(e->globalPosition().toPoint(),
                              tr("Drag to loop this region; release for a partial repeat"), this);
                else if (edge) QToolTip::showText(e->globalPosition().toPoint(),
                              tr("Drag edge to trim; Ctrl+drag inside to duplicate"), this);
                else QToolTip::showText(e->globalPosition().toPoint(),
                              tr("Drag to move; Ctrl+drag to duplicate"), this);
            } else setCursor(Qt::OpenHandCursor);
        } else if (pos.x() >= headerWidth) {
            setCursor(Qt::PointingHandCursor);
        } else {
            setCursor(Qt::ArrowCursor);
        }
        QWidget::mouseMoveEvent(e);
    }

    void mouseReleaseEvent(QMouseEvent *e) override {
        if (e->button() == Qt::LeftButton && _cycleDrag != CycleDrag::None) {
            updateCycleDrag(int(e->position().x()));
            if (_cycleMoved && _cycleDraft.active() &&
                _file->msOfTick(_cycleDraft.end) > _file->msOfTick(_cycleDraft.start)) {
                _cycle = _cycleDraft;
                if (_cycleDrag == CycleDrag::Draw) _cycleEnabled = true;
            } else if (!_cycleMoved && _cycleDrag != CycleDrag::Draw) {
                _cycleEnabled = !_cycleEnabled;
            }
            _cycleDrag = CycleDrag::None;
            publishCycle();
            unsetCursor();
            update();
            e->accept();
            return;
        }
        if (e->button() == Qt::LeftButton && _regionPressed) {
            const bool drag = _regionDragging;
            const int id = _moveRegionId;
            const int targetTick = _moveProposedTick;
            const int releaseRow = (int(e->position().y()) - rulerHeight) / laneHeight;
            _regionPressed = false;
            _regionDragging = false;
            _moveRegionId = 0;
            unsetCursor();
            if (drag && isLaneY(int(e->position().y())) &&
                (_dragMode == RegionDragMode::Move || releaseRow == _moveRow)) {
                switch (_dragMode) {
                case RegionDragMode::Move: {
                    const int x = int(e->position().x());
                    if (const auto *region = _regions.find(id)) {
                        const int start = std::abs(x - _movePressedX) < QApplication::startDragDistance()
                            ? region->startTick : std::max(0, snapTick(timelineTickAt(x) - _moveOffsetTick));
                        moveRegion(id, start, releaseRow);
                    }
                    break;
                }
                case RegionDragMode::Duplicate: duplicateRegion(id, targetTick); break;
                case RegionDragMode::TrimLeft: trimRegionLeft(id, targetTick); break;
                case RegionDragMode::TrimRight: trimRegionRight(id, targetTick); break;
                case RegionDragMode::Loop: loopRegion(id, targetTick); break;
                }
            }
            update();
            e->accept();
            return;
        }
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
                const int endMs = std::max(_file->maxTime(),
                    _file->msOfTick(_file->arrangementEndTick()));
                const int clampedMs = std::clamp(desiredMs, 0, endMs);
                if (isLaneY(pos.y())) {
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
            zoomTimelineAt(e->angleDelta().y() > 0, int(e->position().x()));
        } else if (e->modifiers() & Qt::ShiftModifier) {
            _originMs = std::clamp(_originMs - int(e->angleDelta().y() * 4 / _scale),
                                   0, maxOriginMs());
        } else {
            QWidget::wheelEvent(e); // parent scroll area handles vertical wheel
            return;
        }
        if (scrollTimeRequested && (e->modifiers() & Qt::ShiftModifier)) scrollTimeRequested(_originMs);
        update();
        e->accept();
    }

private:
    // Region edits share MidiEditor's existing Protocol undo/redo stack with
    // ordinary note edits. A snapshot stores only independent region metadata.
    // The live target remains owned by this ArrangementWidget; ProtocolItem
    // snapshots are heap allocated and released by the regular Protocol stack.
    class RegionHistoryEntry final : public ProtocolEntry {
    public:
        explicit RegionHistoryEntry(ArrangementWidget *widget)
            : _owner(widget), _fileAtCapture(widget ? widget->_file.data() : nullptr) {
            if (widget) {
                _regionsAtCapture = widget->_regions;
                _tracksAtCapture = widget->_regionTracks;
                _selectedAtCapture = widget->_selectedRegionId;
                _selectionAtCapture = widget->_selectedRegionIds;
            }
        }
        MidiFile *file() override {
            // The persistent target is created before a MIDI file is opened;
            // look up the current document instead of retaining that nullptr.
            auto *w = owner();
            return w ? w->_file.data() : nullptr;
        }
        ProtocolEntry *copy() override {
            auto *w = owner();
            return w ? new RegionHistoryEntry(w) : nullptr;
        }
        void reloadState(ProtocolEntry *entry) override {
            auto *old = dynamic_cast<RegionHistoryEntry *>(entry);
            auto *w = owner();
            if (!w || !old || !w->_file || !w->_regionsReady ||
                old->_fileAtCapture.data() != w->_file.data()) return;
            const auto &tracks = *w->_file->tracks();
            const bool tracksChanging = tracks != old->_tracksAtCapture;
            // A track-producing action stores the layout last, so Undo
            // restores it just before the appended tracks are removed.
            if (tracksChanging && (old->_tracksAtCapture.size() > tracks.size() ||
                !std::equal(old->_tracksAtCapture.cbegin(), old->_tracksAtCapture.cend(),
                            tracks.cbegin()))) return;
            w->_regionTracks = old->_tracksAtCapture;
            if (tracksChanging || tracks != w->_lastSavedRegionTracks)
                w->_regionTrackEditPendingSave = true;
            w->_regionHistoryRefreshPending = tracksChanging;
            w->_regions = old->_regionsAtCapture;
            w->_selectedRegionId = old->_selectedAtCapture;
            w->_selectedRegionIds = old->_selectionAtCapture;
            if (const auto *region = w->_regions.find(w->_selectedRegionId)) {
                MidiTrack *track = w->_file->tracks()->at(region->trackIndex);
                w->setSelectedTrack(track);
                if (w->selectTrackRequested) w->selectTrackRequested(track);
            }
            w->_file->setRegionLayout(w->_regions);
            w->refreshHorizontalRange();
            // Undo and Redo also persist the restored layout, so a reopened
            // document matches the visible arrangement after the shortcut.
            if (!w->_file->path().isEmpty() && QFileInfo::exists(w->_file->path()))
                w->saveRegionLayout();
            w->update();
            if (!tracksChanging && w->regionViewChanged) w->regionViewChanged();
        }
    private:
        ArrangementWidget *owner() const {
            return static_cast<ArrangementWidget *>(_owner.data());
        }
        QPointer<QWidget> _owner;
        QPointer<MidiFile> _fileAtCapture;
        xiv::MidiRegionModel _regionsAtCapture;
        QList<MidiTrack *> _tracksAtCapture;
        int _selectedAtCapture = 0;
        std::set<int> _selectionAtCapture;
    };

    struct PreviewNote {
        int beginMs, endMs, pitch, beginTick, endTick, velocity, channel;
    };
    xiv::MidiRegionModel _regions;
    bool _regionsReady = false;
    QList<MidiTrack *> _regionTracks;
    QList<MidiTrack *> _lastSavedRegionTracks;
    bool _regionTrackEditPendingSave = false;
    bool _regionHistoryRefreshPending = false;
    int _selectedRegionId = 0;
    std::set<int> _selectedRegionIds;

    int _rulerTop = 0;
    void zoomTimelineAt(bool zoomIn, int x) {
        x = std::clamp(x, headerWidth, std::max(headerWidth, width()));
        const double anchorMs = _originMs + (x - headerWidth) / _scale;
        _scale = std::clamp(_scale * (zoomIn ? 1.2 : 1.0 / 1.2), 0.004, 0.7);
        _originMs = std::clamp(int(std::round(anchorMs - (x - headerWidth) / _scale)),
                               0, maxOriginMs());
        refreshHorizontalRange();
        if (scrollTimeRequested) scrollTimeRequested(_originMs);
        update();
    }


    enum class CycleDrag { None, Draw, Move, Left, Right };
    static constexpr int cycleStripHeight = 12;
    xiv::PlaybackCycle _cycle, _cycleDraft;
    CycleDrag _cycleDrag = CycleDrag::None;
    bool _cycleEnabled = false;
    bool _cycleMoved = false;
    int _cycleAnchorX = 0;
    int _cycleAnchorTick = 0;

    bool inCycleStrip(const QPoint &pos) const {
        return _file && pos.x() >= headerWidth &&
               pos.y() >= _rulerTop && pos.y() < _rulerTop + cycleStripHeight;
    }

    void publishCycle() const {
        if (_file && _cycleEnabled) {
            PlayerThread::setCycleRange({_file->msOfTick(_cycle.start),
                                         _file->msOfTick(_cycle.end)});
        } else {
            PlayerThread::setCycleRange({});
        }
    }

    CycleDrag cycleHit(int x) const {
        if (!_cycle.active()) return CycleDrag::Draw;
        const int left = xForMs(_file->msOfTick(_cycle.start));
        const int right = xForMs(_file->msOfTick(_cycle.end));
        if (std::abs(x - left) <= 4 && std::abs(x - left) <= std::abs(x - right))
            return CycleDrag::Left;
        if (std::abs(x - right) <= 4) return CycleDrag::Right;
        return x >= left && x < right ? CycleDrag::Move : CycleDrag::Draw;
    }

    void beginCycleDrag(int x) {
        _cycleDrag = cycleHit(x);
        _cycleAnchorX = x;
        _cycleAnchorTick = snapTick(timelineTickAt(x));
        _cycleDraft = _cycle;
        _cycleMoved = false;
        QToolTip::hideText();
    }

    void updateCycleDrag(int x) {
        if (!_cycleMoved && std::abs(x - _cycleAnchorX) < QApplication::startDragDistance())
            return;
        _cycleMoved = true;
        const int tick = snapTick(timelineTickAt(std::clamp(x, headerWidth, width())));
        switch (_cycleDrag) {
        case CycleDrag::Draw:
            _cycleDraft = {std::min(tick, _cycleAnchorTick), std::max(tick, _cycleAnchorTick)};
            break;
        case CycleDrag::Move: {
            const int duration = _cycle.end - _cycle.start;
            const auto start = std::clamp<int64_t>(
                static_cast<int64_t>(_cycle.start) + tick - _cycleAnchorTick,
                0, std::numeric_limits<int>::max() - duration);
            _cycleDraft = {static_cast<int>(start), static_cast<int>(start) + duration};
            break;
        }
        case CycleDrag::Left: _cycleDraft.start = std::min(tick, _cycle.end - 1); break;
        case CycleDrag::Right: _cycleDraft.end = std::max(tick, _cycle.start + 1); break;
        case CycleDrag::None: break;
        }
        update();
    }

    bool isLaneY(int y) const { return y >= _rulerTop + rulerHeight; }

    void drawRuler(QPainter &p, const QVector<QPair<int, int>> &marks, int playheadX) const {
        p.save();
        p.translate(0, _rulerTop);
        p.setClipRect(QRect(0, 0, width(), rulerHeight));
        p.fillRect(0, 0, width(), rulerHeight, QColor("#292D34"));
        p.setFont(QFont(QStringLiteral("Segoe UI"), 9, QFont::DemiBold));
        p.setPen(QColor("#E8EFF8"));
        p.drawText(QRect(14, 0, headerWidth - 18, rulerHeight), Qt::AlignVCenter, tr("Tracks"));
        p.setPen(QColor("#515966"));
        p.drawLine(0, rulerHeight - 1, width(), rulerHeight - 1);
        p.drawLine(headerWidth, 0, headerWidth, rulerHeight);
        p.setClipRect(QRect(headerWidth, 0, std::max(0, width() - headerWidth), rulerHeight));
        if (_file) {
            p.fillRect(headerWidth, 0, width() - headerWidth, cycleStripHeight, QColor("#1E2229"));
            const auto range = _cycleMoved && _cycleDrag != CycleDrag::None ? _cycleDraft : _cycle;
            if (range.active()) {
                const int left = xForMs(_file->msOfTick(range.start));
                const int right = xForMs(_file->msOfTick(range.end));
                const QRect ribbon(left, 1, std::max(2, right - left), cycleStripHeight - 2);
                const bool enabled = _cycleEnabled || (_cycleMoved && _cycleDrag == CycleDrag::Draw);
                p.fillRect(ribbon, enabled ? QColor("#E7A536") : QColor("#59472A"));
                p.setPen(enabled ? QColor("#FFD386") : QColor("#9B7B42"));
                p.drawRect(ribbon.adjusted(0, 0, -1, -1));
                p.drawLine(ribbon.left() + 2, 3, ribbon.left() + 2, cycleStripHeight - 4);
                p.drawLine(ribbon.right() - 2, 3, ribbon.right() - 2, cycleStripHeight - 4);
            } else {
                p.setFont(QFont(QStringLiteral("Segoe UI"), 7));
                p.setPen(QColor("#C4A36B"));
                p.drawText(QRect(headerWidth + 6, 0, width() - headerWidth - 6, cycleStripHeight),
                           Qt::AlignVCenter, tr("Drag to set Cycle"));
                p.setFont(QFont(QStringLiteral("Segoe UI"), 9, QFont::DemiBold));
            }
        }
        p.setPen(QColor("#C4CCD9"));
        if (!_file)
            p.drawText(QRect(headerWidth + 12, 0, width() - headerWidth - 12, rulerHeight),
                       Qt::AlignVCenter, tr("Open a MIDI file"));
        for (const auto &mark : marks)
            p.drawText(mark.first + 5, 27, QString::number(mark.second));
        if (playheadX >= headerWidth && playheadX < width()) {
            p.setPen(QPen(QColor("#FFDD75"), 2));
            p.drawLine(playheadX, cycleStripHeight + 8, playheadX, rulerHeight);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor("#FFDD75"));
            p.drawPolygon(QPolygon{QPoint(playheadX - 5, cycleStripHeight),
                                   QPoint(playheadX + 5, cycleStripHeight),
                                   QPoint(playheadX, cycleStripHeight + 9)});
        }
        p.restore();
    }

    int visibleTimeMs() const {
        return static_cast<int>(std::ceil(std::max(1, width() - headerWidth) / _scale));
    }
    int timelineTickAt(int x) const {
        if (!_file) return 0;
        // Use fractional milliseconds instead of truncating the mouse x
        // position to a full millisecond before converting it to a tick.
        // The MIDI tick() API accepts integers, so interpolate between
        // consecutive milliseconds using the actual tempo map.
        const double target = std::max(0.0, _originMs +
                                             (x - headerWidth) / _scale);
        const int loMs = static_cast<int>(std::floor(target));
        const int hiMs = loMs + 1;
        const int lowTick = _file->tick(loMs);
        const int highTick = _file->tick(hiMs);
        return std::max(0, static_cast<int>(std::lround(
            lowTick + (target - loMs) * (highTick - lowTick))));
    }
    int snapTick(int tick) const {
        if (!_snapToGrid || !_file) return std::max(0, tick);
        const int beatTicks = std::max(1, _file->ticksPerQuarter());
        const int64_t aligned = (static_cast<int64_t>(std::max(0, tick)) +
                                 beatTicks / 2) / beatTicks * beatTicks;
        return static_cast<int>(std::min<int64_t>(aligned,
                                                  std::numeric_limits<int>::max()));
    }
    bool isJoinShortcut(const QKeyEvent *e) const {
        return _selectedRegionId && _selectedRegionIds.size() > 1 &&
               e->key() == Qt::Key_J && e->modifiers() == Qt::ControlModifier;
    }

    static bool isSplitShortcut(const QKeyEvent *e) {
        return e->key() == Qt::Key_T && e->modifiers() == Qt::ControlModifier;
    }

    static bool canSplitAt(const xiv::MidiRegion &region, int tick) {
        return tick > region.startTick && tick < region.endTick();
    }

    void splitSelectedAtPlayhead() {
        if (!_file || !_selectedRegionId) return;
        splitRegions(_selectedRegionIds.empty() ? std::set<int>{_selectedRegionId} :
                     _selectedRegionIds, _file->cursorTick());
    }

    void splitRegion(int id, int tick) {
        splitRegions({id}, tick);
    }

    void splitRegions(const std::set<int> &ids, int tick) {
        if (!_file || MidiPlayer::isPlaying() || MidiInput::recording()) return;
        const QPointer<MidiFile> document = _file;
        auto updated = _regions;
        std::vector<int> targets;
        bool hasCrossingNotes = false;
        for (int id : ids) {
            const auto *region = updated.find(id);
            if (!region || !canSplitAt(*region, tick)) continue;
            targets.push_back(id);
            if (region->loopLengthTicks > 0) {
                // A mid-repeat cut uses the actual arranged note positions,
                // including any partial repeat. Neither half restarts the loop.
                if (!xiv::consolidateRegions(updated, {id}, id, region->trackIndex,
                    region->startTick, -1, _file->tracks()->size(), sourceNotesFor({id}))) return;
                region = updated.find(id);
            }
            const int sourceCut = region->sourceStartTick + (tick - region->startTick);
            if (region->ownedNotes) {
                hasCrossingNotes |= std::any_of(region->ownedNotes->begin(), region->ownedNotes->end(),
                    [sourceCut](const auto &note) {
                        return note.startTick < sourceCut && sourceCut < note.endTick;
                    });
            } else {
                const auto &notes = _preview.value(_file->tracks()->at(region->trackIndex));
                hasCrossingNotes |= std::any_of(notes.begin(), notes.end(), [sourceCut](const auto &note) {
                    return note.beginTick < sourceCut && sourceCut < note.endTick;
                });
            }
        }
        if (targets.empty()) return;
        auto choice = xiv::CrossingNoteMode::Shorten;
        if (hasCrossingNotes) {
            QDialog dialog(this);
            dialog.setWindowTitle(tr("Notes Crossing Split Point"));
            auto *layout = new QVBoxLayout(&dialog);
            auto *message = new QLabel(tr("Do you want to shorten or split "
                                          "the notes that cross the split point?"), &dialog);
            message->setWordWrap(true);
            layout->addWidget(message);
            auto *shorten = new QRadioButton(tr("Shorten - end notes at the split"), &dialog);
            auto *split = new QRadioButton(tr("Split - retrigger notes in the next region"), &dialog);
            shorten->setChecked(true);
            layout->addWidget(shorten);
            layout->addWidget(split);
            auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok |
                                                 QDialogButtonBox::Cancel, &dialog);
            QObject::connect(buttons, &QDialogButtonBox::accepted,
                             &dialog, &QDialog::accept);
            QObject::connect(buttons, &QDialogButtonBox::rejected,
                             &dialog, &QDialog::reject);
            layout->addWidget(buttons);
            if (dialog.exec() != QDialog::Accepted) return;
            if (split->isChecked()) choice = xiv::CrossingNoteMode::Split;
        }
        if (!document || _file.data() != document.data() || MidiPlayer::isPlaying() || MidiInput::recording()) return;
        std::set<int> rightHalves;
        int primary = 0;
        for (int id : targets) {
            const int right = updated.split(id, tick, choice);
            if (!right) return;
            if (!primary || id == _selectedRegionId) primary = right;
            rightHalves.insert(right);
        }
        ProtocolEntry *before = _regionHistory.copy();
        _regions = std::move(updated);
        _selectedRegionIds = std::move(rightHalves);
        _selectedRegionId = primary;
        auto *track = _file->tracks()->at(_regions.find(primary)->trackIndex);
        setSelectedTrack(track);
        if (selectTrackRequested) selectTrackRequested(track);
        finishRegionEdit(before, targets.size() == 1 ? tr("Split Region") : tr("Split Regions"));
    }

    bool canPlaceMove(const xiv::MidiRegion &region, int start, int destination) const {
        const int trackCount = _file->tracks()->size();
        if (destination < 0 || destination >= trackCount) return false;
        if (destination == region.trackIndex) return _regions.canMove(region.id, start);
        const int channel = _file->tracks()->at(destination)->assignedChannel();
        return channel >= 0 && channel < 16 &&
            xiv::validRegionDestination(destination, start,
                static_cast<int64_t>(start) + region.displayDuration(), trackCount);
    }

    std::vector<xiv::SourceRegionNote> sourceNotesFor(const std::set<int> &ids) const {
        std::set<int> tracks;
        for (int id : ids)
            if (const auto *region = _regions.find(id)) tracks.insert(region->trackIndex);
        std::vector<xiv::SourceRegionNote> source;
        int id = 1;
        for (int track : tracks) {
            const auto it = _preview.constFind(_file->tracks()->at(track));
            if (it == _preview.constEnd()) continue;
            for (const auto &note : it.value())
                source.push_back({id++, track, note.beginTick, note.endTick,
                                  note.pitch, note.velocity, note.channel});
        }
        return source;
    }

    void addRegionNoteActions(QMenu &menu, int id) {
        const bool editable = canEditQuantize();
        auto add = [&](QMenu &parent, const QString &name, auto handler) {
            QAction *action = parent.addAction(name);
            action->setEnabled(editable);
            connect(action, &QAction::triggered, this, handler);
        };
        add(menu, tr("Quantize Region..."), [this]() {
            if (regionQuantizeRequested) regionQuantizeRequested();
        });
        add(menu, tr("Transpose Region..."), [this, id]() { transposeRegion(id); });
        add(menu, tr("Explode Region Chords to Tracks"), [this, id]() {
            distributeRegionNotes(id, false);
        });
        QMenu *filter = menu.addMenu(tr("Chord Voice Filter (FFXIV)"));
        filter->setEnabled(editable);
        add(*filter, tr("Keep Highest Voice"), [this, id]() {
            filterRegionChords(id, FfxivVoice::Mode::KeepHighest);
        });
        add(*filter, tr("Keep Lowest Voice"), [this, id]() {
            filterRegionChords(id, FfxivVoice::Mode::KeepLowest);
        });
        add(*filter, tr("Keep Inner Voices"), [this, id]() {
            filterRegionChords(id, FfxivVoice::Mode::KeepInner);
        });
        add(menu, tr("Split Region Channels to Tracks"), [this, id]() {
            distributeRegionNotes(id, true);
        });
    }

    bool captureRegion(xiv::MidiRegionModel &updated, int id) const {
        const auto *region = updated.find(id);
        return region && xiv::consolidateRegions(updated, {id}, id, region->trackIndex,
            region->startTick, -1, _file->tracks()->size(), sourceNotesFor({id}));
    }

    void installRegionNoteEdit(xiv::MidiRegionModel updated, const QString &description) {
        ProtocolEntry *before = _regionHistory.copy();
        _regions = std::move(updated);
        finishRegionEdit(before, description);
    }

    void transposeRegion(int id) {
        if (!canEditQuantize() || !_regions.find(id)) return;
        const QPointer<MidiFile> document = _file;
        bool accepted = false;
        const int semitones = QInputDialog::getInt(this, tr("Transpose Region"),
            tr("Semitones"), 0, -127, 127, 1, &accepted);
        if (!accepted || !semitones || document != _file || !canEditQuantize()) return;

        auto updated = _regions;
        const auto *region = updated.find(id);
        if (!region) return;
        const auto original = *region;
        updated.setQuantize(id, {});
        updated.clearLoop(id);
        if (!captureRegion(updated, id)) return;
        auto notes = *updated.find(id)->ownedNotes;
        if (notes.empty()) return;
        for (auto &note : notes) {
            note.pitch += semitones;
            if (note.pitch < 0 || note.pitch > 127) {
                QToolTip::showText(QCursor::pos(),
                    tr("Transposition would put a note outside the MIDI pitch range."), this);
                return;
            }
        }
        if (!updated.replaceOwnedNotes(id, std::move(notes))) return;
        auto regions = updated.regions();
        for (auto &edited : regions) {
            if (edited.id != id) continue;
            edited.loopLengthTicks = original.loopLengthTicks;
            edited.endNoteMode = original.endNoteMode;
            edited.quantize = original.quantize;
        }
        if (!updated.replaceValidated(regions, _file->numTracks())) return;
        installRegionNoteEdit(std::move(updated), tr("Transpose Region"));
    }

    void filterRegionChords(int id, FfxivVoice::Mode mode) {
        if (!canEditQuantize()) return;
        auto updated = _regions;
        if (!captureRegion(updated, id)) return;
        const auto *region = updated.find(id);
        const auto name = _file->tracks()->at(region->trackIndex)->name().toStdString();
        auto notes = *region->ownedNotes;
        std::vector<FfxivVoice::Note> voices;
        for (const auto &note : notes)
            voices.push_back({static_cast<std::uint64_t>(note.id), region->trackIndex,
                              note.channel, note.pitch, note.startTick, name});
        const auto removed = FfxivVoice::deletionIds(voices, mode,
            chordVoiceTolerance ? chordVoiceTolerance() : 0);
        if (removed.empty()) return;
        notes.erase(std::remove_if(notes.begin(), notes.end(), [&](const auto &note) {
            return removed.count(static_cast<std::uint64_t>(note.id)) != 0;
        }), notes.end());
        if (!updated.replaceOwnedNotes(id, std::move(notes))) return;
        installRegionNoteEdit(std::move(updated), tr("Filter Region Chord Voices"));
    }

    void distributeRegionNotes(int id, bool byChannel) {
        if (!canEditQuantize()) return;
        auto updated = _regions;
        if (!captureRegion(updated, id)) return;
        const auto *region = updated.find(id);
        const int start = region->startTick;
        const int duration = region->displayDuration();
        const auto sourceName = _file->tracks()->at(region->trackIndex)->name();

        std::vector<xiv::RegionNoteGroup> groups;
        xiv::RegionNoteGroup remaining;
        if (byChannel) {
            auto channels = xiv::groupRegionNotesByChannel(*region->ownedNotes);
            if (channels.size() < 2) {
                QToolTip::showText(QCursor::pos(), tr("This region uses fewer than two MIDI channels."), this);
                return;
            }
            for (auto &channel : channels) groups.push_back(std::move(channel.second));
        } else {
            auto chords = xiv::explodeRegionChords(*region->ownedNotes);
            if (chords.voices.empty()) {
                QToolTip::showText(QCursor::pos(), tr("No simultaneous pitched chord notes in this region."), this);
                return;
            }
            for (auto &voice : chords.voices) groups.push_back(std::move(voice.second));
            remaining = std::move(chords.remaining);
        }

        const int firstTrack = _file->tracks()->size();
        if (firstTrack + groups.size() > 65535) {
            QToolTip::showText(QCursor::pos(), tr("This would exceed the MIDI track limit."), this);
            return;
        }
        const bool keepSource = !remaining.empty();
        if (keepSource) {
            if (!updated.replaceOwnedNotes(id, std::move(remaining))) return;
        } else {
            updated.remove(id);
        }

        std::vector<int> channels;
        std::vector<QString> names;
        int firstNewRegion = 0;
        for (auto &notes : groups) {
            const int channel = notes.front().channel;
            const int trackIndex = firstTrack + static_cast<int>(channels.size());
            const int newId = updated.addInitial(trackIndex, 0, duration);
            if (!newId || (start && !updated.move(newId, start)) ||
                !updated.replaceOwnedNotes(newId, std::move(notes))) return;
            if (!firstNewRegion) firstNewRegion = newId;
            channels.push_back(channel);
            names.push_back(byChannel
                ? (channel == 9 ? tr("Drums") : MidiFile::instrumentName(
                    _file->channel(channel)->progAtTick(start)))
                : sourceName);
        }

        ProtocolEntry *before = _regionHistory.copy();
        auto *protocol = _file->protocol();
        protocol->startNewAction(byChannel ? tr("Split Region Channels to Tracks")
                                           : tr("Explode Region Chords to Tracks"));
        for (std::size_t i = 0; i < channels.size(); ++i) {
            _file->addTrack();
            auto *track = _file->tracks()->last();
            track->assignChannel(channels[i]);
            track->setName(names[i]);
        }
        _regions = std::move(updated);
        _regionTracks = *_file->tracks();
        _regionTrackEditPendingSave = true;
        _selectedRegionId = keepSource ? id : firstNewRegion;
        _selectedRegionIds = {_selectedRegionId};
        _file->setRegionLayout(_regions);
        protocol->enterUndoStep(new ProtocolItem(before, &_regionHistory));
        protocol->endAction();

        auto *track = _file->tracks()->at(_regions.find(_selectedRegionId)->trackIndex);
        setSelectedTrack(track);
        if (selectTrackRequested) selectTrackRequested(track);
        if (regionViewChanged) regionViewChanged();
    }

    void transferRegions(const std::set<int> &ids, int primary, int track, int start,
                         const QString &description) {
        MidiTrack *destination = _file->tracks()->at(track);
        ProtocolEntry *before = _regionHistory.copy();
        if (!xiv::consolidateRegions(_regions, ids, primary, track, start,
                                     destination->assignedChannel(), _file->tracks()->size(),
                                     sourceNotesFor(ids))) {
            delete before;
            QToolTip::showText(QCursor::pos(),
                tr("The destination needs a valid timeline position and a MIDI channel for notes moved from other tracks."), this);
            return;
        }
        _selectedRegionId = primary;
        _selectedRegionIds = {primary};
        setSelectedTrack(destination);
        if (selectTrackRequested) selectTrackRequested(destination);
        finishRegionEdit(before, description);
    }

    void joinSelectedRegions(int primary) {
        if (!_file || MidiPlayer::isPlaying() || MidiInput::recording() ||
            _selectedRegionIds.size() < 2) return;
        const auto *destination = _regions.find(primary);
        if (!destination) return;
        int start = destination->startTick;
        for (int id : _selectedRegionIds) {
            const auto *region = _regions.find(id);
            if (!region) return;
            start = std::min(start, region->startTick);
        }
        transferRegions(_selectedRegionIds, primary, destination->trackIndex, start, tr("Join Regions"));
    }

    void moveRegion(int id, int newStartTick, int destination) {
        if (!_file || MidiPlayer::isPlaying() || MidiInput::recording()) return;
        const auto *region = _regions.find(id);
        if (!region || !canPlaceMove(*region, newStartTick, destination)) return;
        if (destination != region->trackIndex) {
            transferRegions({id}, id, destination, newStartTick, tr("Move Region to Track"));
            return;
        }
        if (region->startTick == newStartTick) return;
        ProtocolEntry *before = _regionHistory.copy();
        if (!_regions.move(id, newStartTick)) { delete before; return; }
        finishRegionEdit(before, tr("Move Region"));
    }

    // Every operation creates one undo step and updates the sidecar. Notes are
    // projected during playback/export; neither trim nor duplication edits
    // source MIDI note-on/off pairs.
    void finishRegionEdit(ProtocolEntry *before, const QString &description) {
        _file->setRegionLayout(_regions);
        _file->protocol()->startNewAction(description);
        _file->protocol()->enterUndoStep(new ProtocolItem(before, &_regionHistory));
        _file->protocol()->endAction();
        if (!_file->path().isEmpty() && QFileInfo::exists(_file->path()) &&
            !saveRegionLayout())
            qWarning() << "XIV MIDI Studio: edited region could not be persisted";
        refreshHorizontalRange();
        update();
        if (regionViewChanged) regionViewChanged();
    }
    void duplicateRegion(int id, int newStartTick) {
        if (!_file || MidiPlayer::isPlaying() || !_regions.canDuplicate(id, newStartTick)) return;
        ProtocolEntry *before = _regionHistory.copy();
        const int newId = _regions.duplicate(id, newStartTick);
        if (!newId) { delete before; return; }
        _selectedRegionId = newId;
        _selectedRegionIds = {newId};
        finishRegionEdit(before, tr("Duplicate Region"));
    }
    void trimRegionLeft(int id, int newStartTick) {
        if (!_file || MidiPlayer::isPlaying()) return;
        const auto *r = _regions.find(id);
        if (!r || r->startTick == newStartTick ||
            !_regions.canTrimLeft(id, newStartTick)) return;
        ProtocolEntry *before = _regionHistory.copy();
        if (!_regions.trimLeft(id, newStartTick)) { delete before; return; }
        finishRegionEdit(before, tr("Trim Region Start"));
    }
    void trimRegionRight(int id, int newEndTick) {
        if (!_file || MidiPlayer::isPlaying()) return;
        const auto *r = _regions.find(id);
        const int sourceEnd = r ? sourceEndTick(r->trackIndex) : 0;
        if (!r || r->endTick() == newEndTick ||
            !_regions.canTrimRight(id, newEndTick, sourceEnd)) return;
        ProtocolEntry *before = _regionHistory.copy();
        if (!_regions.trimRight(id, newEndTick, sourceEnd)) { delete before; return; }
        finishRegionEdit(before, tr("Trim Region End"));
    }

    void loopRegion(int id, int newEndTick) {
        if (!_file || MidiPlayer::isPlaying() ||
            !_regions.canLoop(id, newEndTick)) return;
        ProtocolEntry *before = _regionHistory.copy();
        if (!_regions.loopTo(id, newEndTick)) { delete before; return; }
        finishRegionEdit(before, tr("Loop Region"));
    }
    void deleteSelectedRegion() {
        if (!_file || !_selectedRegionId || MidiPlayer::isPlaying()) return;
        ProtocolEntry *before = _regionHistory.copy();
        if (!_regions.remove(_selectedRegionId)) { delete before; return; }
        _selectedRegionId = 0;
        _selectedRegionIds.clear();
        finishRegionEdit(before, tr("Delete Region"));
    }
    void loadRegionLayout() {
        if (!_file || _file->path().trimmed().isEmpty()) return;
        QFile input(_file->path() + QStringLiteral(".xivms-regions.json"));
        if (!input.exists() || !input.open(QIODevice::ReadOnly)) return;
        QJsonParseError error;
        const QJsonDocument doc = QJsonDocument::fromJson(input.readAll(), &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject()) return;
        const QJsonObject root = doc.object();
        const int version = root.value(QStringLiteral("version")).toInt();
        if (root.value(QStringLiteral("format")).toString() !=
                QStringLiteral("XIV Midi Studio regions") ||
            (version != 1 && version != 2) ||
            root.value(QStringLiteral("tracks")).toInt(-1) != _file->tracks()->size()) return;
        const QJsonValue jsonRegions = root.value(QStringLiteral("regions"));
        if (!jsonRegions.isArray()) return;
        std::vector<xiv::MidiRegion> incoming;
        for (const QJsonValue &value : jsonRegions.toArray()) {
            if (!value.isObject()) return;
            const QJsonObject o = value.toObject();
            if (!o.value(QStringLiteral("id")).isDouble() ||
                !o.value(QStringLiteral("track")).isDouble() ||
                !o.value(QStringLiteral("startTick")).isDouble() ||
                !o.value(QStringLiteral("sourceStartTick")).isDouble() ||
                !o.value(QStringLiteral("sourceEndTick")).isDouble()) return;
            // Missing fields preserve compatibility with existing sidecars.
            // Old v0.3 sidecars predate the crossing-note dialog. A region
            // starting after source tick zero came from a split; those splits
            // behaved as Keep before explicit boundary choices existed.
            const int legacyEntryMode = o.value(QStringLiteral("sourceStartTick")).toInt() > 0 ? 0 : 2;
            const int entryMode = o.value(QStringLiteral("startNoteMode")).toInt(legacyEntryMode);
            const int exitMode = o.value(QStringLiteral("endNoteMode")).toInt(0);
            if (entryMode < 0 || entryMode > 2 || exitMode < 0 || exitMode > 2)
                return;
            incoming.push_back({o.value(QStringLiteral("id")).toInt(),
                                o.value(QStringLiteral("track")).toInt(-1),
                                o.value(QStringLiteral("startTick")).toInt(-1),
                                o.value(QStringLiteral("sourceStartTick")).toInt(-1),
                                o.value(QStringLiteral("sourceEndTick")).toInt(-1),
                                static_cast<xiv::CrossingNoteMode>(entryMode),
                                static_cast<xiv::CrossingNoteMode>(exitMode),
                                o.value(QStringLiteral("loopLengthTicks")).toInt(0),
                                {o.value(QStringLiteral("quantizeGridTicks")).toInt(0),
                                 o.value(QStringLiteral("quantizeStrength")).toInt(100),
                                 o.value(QStringLiteral("quantizeSwing")).toInt(50)}});
            const auto storedNotes = o.value(QStringLiteral("ownedNotes"));
            if (storedNotes.isUndefined()) continue;
            if (version != 2 || !storedNotes.isArray()) return;
            std::vector<xiv::OwnedRegionNote> notes;
            const QJsonArray rows = storedNotes.toArray();
            notes.reserve(static_cast<std::size_t>(rows.size()));
            for (const auto &row : rows) {
                const QJsonArray values = row.toArray();
                if (values.size() != 6) return;
                notes.push_back({values.at(0).toInt(-1), values.at(1).toInt(-1),
                                 values.at(2).toInt(-1), values.at(3).toInt(-1),
                                 values.at(4).toInt(-1), values.at(5).toInt(-1)});
            }
            incoming.back().ownedNotes =
                std::make_shared<const std::vector<xiv::OwnedRegionNote>>(std::move(notes));
        }
        const int trackCount = _file->tracks()->size();
        const auto storedManaged = root.value(QStringLiteral("managedTracks"));
        xiv::MidiRegionModel loaded = storedManaged.isArray() ? xiv::MidiRegionModel{} : _regions;
        if (storedManaged.isArray()) {
            for (const auto &value : storedManaged.toArray()) {
                const int track = value.toInt(-1);
                if (track < 0 || track >= trackCount) return;
                loaded.markManagedTrack(track);
            }
        }
        if (loaded.replaceValidated(incoming, trackCount))
            _regions = std::move(loaded);
        else
            qWarning() << "XIV Midi Studio: invalid region sidecar ignored";
    }

    QPointer<MidiFile> _file;
    MidiTrack *_selected = nullptr;
    MidiTrack *_solo = nullptr;
    QHash<MidiTrack *, QVector<PreviewNote>> _preview;
    int _originMs = 0;
    int _playheadMs = -1;
    double _scale = 0.065; // pixels per millisecond
    bool _snapToGrid = false;
    bool _dragging = false;
    bool _timelinePressed = false;
    bool _regionPressed = false;
    bool _regionDragging = false;
    RegionDragMode _dragMode = RegionDragMode::Move;
    int _moveRegionId = 0;
    int _moveRow = -1;
    int _movePreviewRow = -1;
    int _movePressedY = 0;
    int _movePressedX = 0;
    int _movePressedTick = 0;
    int _moveOffsetTick = 0;
    int _moveProposedTick = 0;
    int _dragX = 0;
    int _dragStartMs = 0;
    int _hoverTick = -1;
    int _hoverRow = -1;
    RegionHistoryEntry _regionHistory{this};

    // Returns the actual source ending tick for a region's track, including
    // material hidden by a previous trim. The original note map is untouched.
    int sourceEndTick(int trackIndex) const {
        if (!_file || trackIndex < 0 || trackIndex >= _file->tracks()->size()) return 0;
        int end = 0;
        const auto &notes = _preview.value(_file->tracks()->at(trackIndex));
        for (const auto &note : notes) end = std::max(end, note.endTick);
        return end;
    }
    const xiv::MidiRegion *hitRegion(int row, int x) const {
        if (!_file || row < 0 || row >= _file->tracks()->size()) return nullptr;
        if (const auto *r = _regions.at(row, timelineTickAt(x))) return r;
        // A one-pixel rounding discrepancy at a border must not make an edge
        // handle impossible to grab, especially at a low zoom setting.
        const auto &regions = _regions.regions();
        for (auto it = regions.crbegin(); it != regions.crend(); ++it) {
            const auto &r = *it;
            if (r.trackIndex != row) continue;
            const int left = xForMs(_file->msOfTick(r.startTick));
            const int right = xForMs(_file->msOfTick(r.endTick()));
            if ((left >= headerWidth && std::abs(x-left) <= 5) ||
                (right <= width() && std::abs(x-right) <= 5)) return &r;
        }
        return nullptr;
    }
    QPainterPath loopOutline(const QRect &bounds, int startTick, int endTick, int period) const {
        constexpr int halfWidth = 8;
        constexpr int depth = 11;
        QVector<int> seams;
        // Start near the viewport, retaining one earlier boundary to absorb
        // rounding when converting between ticks and whole milliseconds.
        const int64_t offset = static_cast<int64_t>(_file->tick(_originMs)) - startTick;
        const int64_t firstRepeat = std::max<int64_t>(1, offset / period - 1);
        int previous = bounds.left();
        for (int64_t tick = startTick + firstRepeat * period; tick < endTick; tick += period) {
            const int x = xForMs(_file->msOfTick(static_cast<int>(tick)));
            if (x >= bounds.right() - halfWidth) break;
            if (x - previous <= 2 * halfWidth) continue;
            seams.append(x);
            previous = x;
        }

        QPainterPath path;
        path.moveTo(bounds.topLeft());
        const auto notch = [&path](int x, int y, int direction, int inward) {
            path.lineTo(x - direction * halfWidth, y);
            path.quadTo(x - direction * 2, y, x, y + inward * depth);
            path.quadTo(x + direction * 2, y, x + direction * halfWidth, y);
        };
        for (int x : seams)
            notch(x, bounds.top(), 1, 1);
        path.lineTo(bounds.topRight());
        path.lineTo(bounds.bottomRight());
        for (auto it = seams.crbegin(); it != seams.crend(); ++it)
            notch(*it, bounds.bottom(), -1, -1);
        path.lineTo(bounds.bottomLeft());
        path.closeSubpath();
        return path;
    }

    int xForMs(int ms) const { return headerWidth + int((ms - _originMs) * _scale); }
    enum class TrackControl { Instrument, Visibility, Mute, Solo, More };
    // The same rectangles are used for painting, hit testing, and tooltips.
    QRect instrumentRect(int y) const { return QRect(13, y + 49, 43, 24); }
    QRect visibilityRect(int y) const { return QRect(61, y + 49, 29, 24); }
    QRect muteRect(int y) const { return QRect(95, y + 49, 29, 24); }
    QRect soloRect(int y) const { return QRect(129, y + 49, 29, 24); }
    QRect actionsRect(int y) const { return QRect(163, y + 49, 66, 24); }
    static void drawButton(QPainter &p, const QRect &r, TrackControl control,
                           bool active, const QColor &accent) {
        p.save();
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(active ? accent.lighter(145) : QColor("#62748A"), 1));
        p.setBrush(active ? accent : QColor("#344252"));
        p.drawRoundedRect(r, 5, 5);
        p.setPen(QPen(QColor("#F5F8FC"), 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        p.translate(r.center());
        switch (control) {
        case TrackControl::Instrument:
            p.drawRect(QRectF(-13, -6, 15, 12));
            p.drawLine(QPointF(-8, -6), QPointF(-8, 6));
            p.drawLine(QPointF(-3, -6), QPointF(-3, 6));
            p.drawLine(QPointF(7, -1), QPointF(10, 2));
            p.drawLine(QPointF(10, 2), QPointF(13, -1));
            break;
        case TrackControl::Visibility: {
            QPainterPath eye;
            eye.moveTo(-8, 0);
            eye.quadTo(0, -10, 8, 0);
            eye.quadTo(0, 10, -8, 0);
            p.drawPath(eye);
            p.drawEllipse(QPointF(0, 0), 2.4, 2.4);
            if (!active) p.drawLine(QPointF(-7, 6), QPointF(7, -6));
            break;
        }
        case TrackControl::Mute:
            p.drawPolygon(QPolygonF{QPointF(-8, -3), QPointF(-5, -3), QPointF(0, -7),
                                    QPointF(0, 7), QPointF(-5, 3), QPointF(-8, 3)});
            p.drawLine(QPointF(3, -3), QPointF(8, 3));
            p.drawLine(QPointF(3, 3), QPointF(8, -3));
            break;
        case TrackControl::Solo:
            p.drawArc(QRectF(-7, -7, 14, 14), 0, 180 * 16);
            p.drawRoundedRect(QRectF(-8, 0, 4, 6), 1, 1);
            p.drawRoundedRect(QRectF(4, 0, 4, 6), 1, 1);
            break;
        case TrackControl::More:
            p.setBrush(p.pen().color());
            p.setPen(Qt::NoPen);
            p.drawEllipse(QPointF(-6, 0), 1.5, 1.5);
            p.drawEllipse(QPointF(0, 0), 1.5, 1.5);
            p.drawEllipse(QPointF(6, 0), 1.5, 1.5);
            break;
        }
        p.restore();
    }
};

#endif // FFXIV_ARRANGEMENTWIDGET_H
