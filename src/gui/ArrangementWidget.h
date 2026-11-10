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
#include <QContextMenuEvent>
#include <QPoint>
#include <QWheelEvent>
#include <QKeyEvent>
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
#include <QRadioButton>
#include <QButtonGroup>
#include <QVBoxLayout>
#include <QDialogButtonBox>
#include <QLabel>
#include <QDebug>
#include "../region/MidiRegionModel.h"
#include "../region/MidiRegionRender.h"
#include "../region/RegionSplitPrecision.h"
#include <cstdlib>
#include <functional>
#include <algorithm>
#include <limits>

#include "../midi/MidiFile.h"
#include "../midi/MidiPlayer.h"
#include "../midi/MidiInput.h"
#include "../midi/MidiTrack.h"
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
    std::function<void(int)> positionChanged;
    const xiv::MidiRegionModel &regionModel() const { return _regions; }
    MidiFile *midiFile() const { return _file.data(); }
    int selectedRegionId() const { return _selectedRegionId; }
    bool canEditQuantize() const {
        return _file && _regionsReady && !MidiPlayer::isPlaying() && !MidiInput::recording();
    }
    void applyQuantize(const xiv::RegionQuantize &settings, bool entireTrack = false) {
        if (!canEditQuantize() || !settings.valid()) return;
        const auto *selected = _regions.find(_selectedRegionId);
        const int track = _selected ? _file->tracks()->indexOf(_selected) : -1;
        std::vector<int> targets;
        if (entireTrack) {
            if (track < 0) return;
            for (const auto &region : _regions.regions())
                if (region.trackIndex == track && !(region.quantize == settings))
                    targets.push_back(region.id);
        } else if (selected && !(selected->quantize == settings)) {
            targets.push_back(selected->id);
        }
        if (targets.empty()) return;
        ProtocolEntry *before = _regionHistory.copy();
        for (int id : targets) _regions.setQuantize(id, settings);
        finishRegionEdit(before, entireTrack ? tr("Quantize Track Regions") : tr("Quantize Region"));
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
    // Two rows of large, readable named actions rather than tiny single letters.
    static constexpr int laneHeight = 114;

    void setFile(MidiFile *file) {
        _file = file;
        _selected = nullptr;
        _preview.clear();
        _regions.clear();
        _regionsReady = false;
        _regionTrackCount = 0;
        _selectedRegionId = 0;
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
                    [this]() { rebuildPreview(); });
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
                                                note->midiTime(), note->offEvent()->midiTime()});
            }
        }
        const int count = _file->tracks()->size();
        // Track-list mutations invalidate index-based sidecar layouts. Reset
        // instead of applying a split to the wrong instrument.
        if (!_regionsReady || count != _regionTrackCount) {
            _regions.clear();
            _selectedRegionId = 0;
            for (int i = 0; i < count; ++i) {
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
            _regionTrackCount = count;
            if (!_regionsReady) loadRegionLayout();
            _regionsReady = true;
        }
        _file->setRegionLayout(_regions);
        setMinimumHeight(std::max(130, rulerHeight + count * laneHeight));
        updateGeometry();
        refreshHorizontalRange();
        update();
        if (inspectorChanged) inspectorChanged();
    }

    // MIDI has no region metadata. The native layout is stored separately next
    // to the MIDI: song.mid.xivms-regions.json. Writes are atomic; never edit
    // or overwrite the MIDI events as part of region splitting.
    bool saveRegionLayout(const QString &midiPath = QString()) const {
        if (!_regionsReady || !_file) return false;
        const QString path = midiPath.isEmpty() ? _file->path() : midiPath;
        if (path.trimmed().isEmpty() || !QFileInfo::exists(path)) return false;
        QJsonObject root;
        root.insert(QStringLiteral("format"), QStringLiteral("XIV Midi Studio regions"));
        root.insert(QStringLiteral("version"), 1);
        root.insert(QStringLiteral("tracks"), _file->tracks()->size());
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
            items.append(entry);
        }
        root.insert(QStringLiteral("regions"), items);
        QJsonArray managed;
        for (int track : _regions.managedTracks()) managed.append(track);
        root.insert(QStringLiteral("managedTracks"), managed);
        QSaveFile output(path + QStringLiteral(".xivms-regions.json"));
        if (!output.open(QIODevice::WriteOnly)) return false;
        const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
        if (output.write(bytes) != bytes.size()) { output.cancelWriting(); return false; }
        return output.commit();
    }

protected:
    void keyPressEvent(QKeyEvent *e) override {
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
        p.fillRect(0, 0, width(), rulerHeight, QColor("#292D34"));
        p.fillRect(0, 0, headerWidth, height(), QColor("#292D34"));

        p.setFont(QFont(QStringLiteral("Segoe UI"), 9, QFont::DemiBold));
        p.setPen(QColor("#E8EFF8"));
        p.drawText(QRect(14, 0, headerWidth - 18, rulerHeight), Qt::AlignVCenter,
                   QStringLiteral("TRACKS  /  RIGHT-CLICK FOR ACTIONS"));
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
        // Cache the grid positions once, then draw them after EACH lane's
        // background. Previously the grid was painted here and the opaque
        // lane backgrounds immediately covered it.
        QVector<int> beatGridXs;
        QVector<int> subBeatGridXs;
        QVector<int> measureGridXs;
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
            if (x >= headerWidth && x <= width()) {
                measureGridXs.append(x);
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
            for (int x : measureGridXs) p.drawLine(x, y + 1, x, y + laneHeight - 2);
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

            drawButton(p, instrumentRect(y), QStringLiteral("Instrument  \u25be"), false, QColor("#45647F"));

            // Keep the common track controls beside its instrument selector.
            drawButton(p, visibilityRect(y), QStringLiteral("Show"), !t->hidden(), QColor("#1767A4"));
            drawButton(p, muteRect(y), QStringLiteral("Mute"), t->muted(), QColor("#A83C4B"));
            drawButton(p, soloRect(y), QStringLiteral("Solo"), t == _solo, QColor("#9B761D"));
            drawButton(p, actionsRect(y), QStringLiteral("More..."), false, QColor("#55748E"));

            const auto &notes = _preview[t];
            if (notes.isEmpty()) continue;
            // Draw region backgrounds first; note previews are rendered later
            // from the same region schedule as playback and arranged export.
            for (const auto &region : _regions.regions()) {
                if (region.trackIndex != i) continue;
                const int x0 = std::max(headerWidth + 3,
                    xForMs(_file->msOfTick(region.startTick)));
                const int x1 = std::min(width() - 2,
                    xForMs(_file->msOfTick(region.endTick())));
                if (x1 <= x0) continue;
                const bool selectedRegion = region.id == _selectedRegionId;
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
                p.fillRect(regionRect, regionFill);
                p.setBrush(Qt::NoBrush);
                p.setPen(QPen(selectedRegion ? QColor("#FFD18B") : accent,
                              selectedRegion ? 2 : 1));
                p.drawRect(regionRect.adjusted(0, 0, -1, -1));
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
                lowestPitch = std::min(lowestPitch, n.pitch);
                highestPitch = std::max(highestPitch, n.pitch);
            }
            if (!sourceNotes.empty()) {
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
                for (const auto &note : xiv::renderRegionNotes(sourceNotes, _regions)) {
                    const int sx = std::max(headerWidth + 4,
                        xForMs(_file->msOfTick(note.startTick)));
                    const int ex = std::min(width() - 3,
                        xForMs(_file->msOfTick(note.endTick)));
                    if (ex <= sx) continue;
                    const int ny = y + 96 - (note.pitch - lowestPitch) * 62 / pitchRange;
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
                case RegionDragMode::Move:
                    previewStart = target;
                    previewEnd = target + moving->displayDuration();
                    valid = _regions.canMove(moving->id, target);
                    description = tr("Move region");
                    break;
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
                const int top = rulerHeight + moving->trackIndex * laneHeight;
                p.save();
                p.setClipRect(QRect(headerWidth, rulerHeight,
                                    width()-headerWidth, height()-rulerHeight));
                p.setBrush(QColor(valid ? 47 : 170, valid ? 172 : 57,
                                  valid ? 109 : 65, 62));
                p.setPen(QPen(QColor(valid ? "#70E4A0" : "#ED6977"),
                              2, Qt::DashLine));
                p.drawRect(QRect(left, top + 7, std::max(1, right - left),
                                 laneHeight - 14));
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

    void contextMenuEvent(QContextMenuEvent *e) override {
        if (_file && e->pos().x() >= headerWidth && e->pos().y() >= rulerHeight) {
            const QPoint pos = e->pos();
            const int row = (pos.y() - rulerHeight) / laneHeight;
            const int tick = snapTick(timelineTickAt(pos.x()));
            const auto *region = hitRegion(row, pos.x());
            if (region && row >= 0 && row < _file->tracks()->size()) {
                const int id = region->id;
                _selectedRegionId = id;
                setFocus(Qt::MouseFocusReason);
                MidiTrack *track = _file->tracks()->at(row);
                setSelectedTrack(track);
                if (selectTrackRequested) selectTrackRequested(track);
                if (regionViewChanged) regionViewChanged();
                QMenu menu(this);
                QAction *splitHere = menu.addAction(tr("Split Here"));
                splitHere->setEnabled(region->loopLengthTicks == 0 &&
                    tick > region->startTick && tick < region->endTick());
                const int cursorTick = _file->cursorTick();
                QAction *splitAtCursor = menu.addAction(tr("Split at Play Head"));
                splitAtCursor->setEnabled(region->loopLengthTicks == 0 &&
                    cursorTick > region->startTick &&
                    cursorTick < region->endTick());
                QAction *duplicateAction = menu.addAction(tr("Duplicate Region After Original"));
                const int nextTick = region->endTick();
                duplicateAction->setEnabled(!MidiPlayer::isPlaying() &&
                                            _regions.canDuplicate(id, nextTick));
                menu.addSeparator();
                QAction *deleteRegionAction = menu.addAction(tr("Delete Region (Backspace)"));
                deleteRegionAction->setEnabled(!MidiPlayer::isPlaying());
                QAction *chosen = menu.exec(e->globalPos());
                if (chosen == splitHere) splitRegion(id, tick);
                else if (chosen == splitAtCursor) splitRegion(id, cursorTick);
                else if (chosen == duplicateAction) duplicateRegion(id, nextTick);
                else if (chosen == deleteRegionAction) deleteSelectedRegion();
                update();
                e->accept();
                return;
            }
        }
        if (_file && e->pos().x() < headerWidth && e->pos().y() >= rulerHeight) {
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
            e->position().y() >= rulerHeight &&
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
        if (pos.y() >= rulerHeight && pos.x() < headerWidth) {
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
        if (pos.x() >= headerWidth && pos.y() >= rulerHeight) {
            const int row = (pos.y() - rulerHeight) / laneHeight;
            const xiv::MidiRegion *r = hitRegion(row, pos.x());
            if (r && row >= 0 && row < _file->tracks()->size()) {
                _selectedRegionId = r->id;
                setFocus(Qt::MouseFocusReason);
                if (!MidiPlayer::isPlaying()) {
                    _regionPressed = true;
                    _regionDragging = false;
                    _moveRegionId = r->id;
                    _moveRow = row;
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
                MidiTrack *track = _file->tracks()->at(row);
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
        int hoverTick = -1, hoverRow = -1;
        if (_file && pos.x() >= headerWidth && pos.y() >= rulerHeight) {
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
            if (!_regionDragging &&
                std::abs(pos.x() - _movePressedX) >= QApplication::startDragDistance()) {
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
        if (_file && pos.x() < headerWidth && pos.y() >= rulerHeight) {
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
        if (_file && pos.x() >= headerWidth && pos.y() >= rulerHeight) {
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
        if (e->button() == Qt::LeftButton && _regionPressed) {
            const bool drag = _regionDragging;
            const int id = _moveRegionId;
            const int targetTick = _moveProposedTick;
            const int releaseRow = (int(e->position().y()) - rulerHeight) / laneHeight;
            _regionPressed = false;
            _regionDragging = false;
            _moveRegionId = 0;
            unsetCursor();
            // Region operations stay within the original track; editing is
            // disabled during playback to avoid changing an active scheduler.
            if (drag && releaseRow == _moveRow) {
                switch (_dragMode) {
                case RegionDragMode::Move: moveRegion(id, targetTick); break;
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
            // Keep the time beneath the mouse fixed while zooming.
            const int x = std::clamp(int(e->position().x()), headerWidth, width());
            const double anchorMs = _originMs + (x - headerWidth) / _scale;
            _scale = std::clamp(_scale * factor, 0.004, 0.7);
            _originMs = std::clamp(int(std::round(anchorMs - (x - headerWidth) / _scale)),
                                   0, maxOriginMs());
            refreshHorizontalRange();
            if (scrollTimeRequested) scrollTimeRequested(_originMs);
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
                _selectedAtCapture = widget->_selectedRegionId;
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
            const int trackCount = w->_file->tracks()->size();
            for (int track : old->_regionsAtCapture.managedTracks())
                if (track >= trackCount) return;
            w->_regions = old->_regionsAtCapture;
            w->_selectedRegionId = old->_selectedAtCapture;
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
            if (w->regionViewChanged) w->regionViewChanged();
        }
    private:
        ArrangementWidget *owner() const {
            return static_cast<ArrangementWidget *>(_owner.data());
        }
        QPointer<QWidget> _owner;
        QPointer<MidiFile> _fileAtCapture;
        xiv::MidiRegionModel _regionsAtCapture;
        int _selectedAtCapture = 0;
    };

    struct PreviewNote {
        int beginMs, endMs, pitch, beginTick, endTick;
    };
    xiv::MidiRegionModel _regions;
    bool _regionsReady = false;
    int _regionTrackCount = 0;
    int _selectedRegionId = 0;

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
    void splitRegion(int id, int tick) {
        if (!_file) return;
        const xiv::MidiRegion *region = _regions.find(id);
        if (!region || tick <= region->startTick || tick >= region->endTick()) return;
        const int sourceCut = region->sourceStartTick + tick - region->startTick;
        xiv::CrossingNoteMode choice = xiv::CrossingNoteMode::Keep;
        bool hasCrossingNotes = false;
        if (region->trackIndex >= 0 && region->trackIndex < _file->tracks()->size()) {
            MidiTrack *track = _file->tracks()->at(region->trackIndex);
            const auto &notes = _preview.value(track);
            for (const auto &note : notes) {
                if (note.beginTick < sourceCut && sourceCut < note.endTick &&
                    note.endTick > region->sourceStartTick) {
                    hasCrossingNotes = true;
                    break;
                }
            }
        }
        if (hasCrossingNotes) {
            QDialog dialog(this);
            dialog.setWindowTitle(tr("Notes Crossing Split Point"));
            auto *layout = new QVBoxLayout(&dialog);
            auto *message = new QLabel(tr("Do you want to keep, shorten, or split "
                                          "the notes that cross the split point?"), &dialog);
            message->setWordWrap(true);
            layout->addWidget(message);
            auto *keep = new QRadioButton(tr("Keep - let notes sustain past the split"), &dialog);
            auto *shorten = new QRadioButton(tr("Shorten - end notes at the split"), &dialog);
            auto *split = new QRadioButton(tr("Split - retrigger notes in the next region"), &dialog);
            keep->setChecked(true);
            layout->addWidget(keep);
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
            if (shorten->isChecked()) choice = xiv::CrossingNoteMode::Shorten;
            else if (split->isChecked()) choice = xiv::CrossingNoteMode::Split;
        }
        // Snapshot BEFORE modifying the region model. Register the operation
        // in the same MIDI Protocol used by Ctrl+Z / Ctrl+Y elsewhere.
        ProtocolEntry *before = _regionHistory.copy();
        const int newId = _regions.split(id, tick, choice);
        if (!newId) { delete before; return; }
        _selectedRegionId = newId;
        _file->setRegionLayout(_regions);
        _file->protocol()->startNewAction(tr("Split Region"));
        _file->protocol()->enterUndoStep(new ProtocolItem(before, &_regionHistory));
        _file->protocol()->endAction();
        // Persist only the region metadata; note contents remain unmodified.
        if (!_file->path().isEmpty() && QFileInfo::exists(_file->path()) &&
            !saveRegionLayout()) {
            qWarning() << "XIV Midi Studio: could not save region sidecar";
            QToolTip::showText(mapToGlobal(QPoint(headerWidth + 12, rulerHeight + 8)),
                tr("The region split is in memory, but its sidecar could not be saved."), this);
        }
        update();
        if (regionViewChanged) regionViewChanged();
    }
    void moveRegion(int id, int newStartTick) {
        if (!_file || MidiPlayer::isPlaying()) return;
        const auto *region = _regions.find(id);
        if (!region || region->startTick == newStartTick ||
            !_regions.canMove(id, newStartTick)) return;
        ProtocolEntry *before = _regionHistory.copy();
        if (!_regions.move(id, newStartTick)) { delete before; return; }
        _file->setRegionLayout(_regions);
        _file->protocol()->startNewAction(tr("Move Region"));
        _file->protocol()->enterUndoStep(new ProtocolItem(before, &_regionHistory));
        _file->protocol()->endAction();
        if (!_file->path().isEmpty() && QFileInfo::exists(_file->path()) &&
            !saveRegionLayout())
            qWarning() << "XIV Midi Studio: moved region could not be persisted";
        refreshHorizontalRange();
        update();
        if (regionViewChanged) regionViewChanged();
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
        if (root.value(QStringLiteral("format")).toString() !=
                QStringLiteral("XIV Midi Studio regions") ||
            root.value(QStringLiteral("version")).toInt() != 1 ||
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
        for (const auto &r : _regions.regions()) {
            if (r.trackIndex != row) continue;
            const int left = xForMs(_file->msOfTick(r.startTick));
            const int right = xForMs(_file->msOfTick(r.endTick()));
            if ((left >= headerWidth && std::abs(x-left) <= 5) ||
                (right <= width() && std::abs(x-right) <= 5)) return _regions.find(r.id);
        }
        return nullptr;
    }
    int xForMs(int ms) const { return headerWidth + int((ms - _originMs) * _scale); }
    // Hit targets match the painted buttons. Do not overlap the track name.
    QRect instrumentRect(int y) const { return QRect(13, y + 49, 299, 27); }
    QRect visibilityRect(int y) const { return QRect(13, y + 80, 67, 27); }
    QRect muteRect(int y) const { return QRect(87, y + 80, 66, 27); }
    QRect soloRect(int y) const { return QRect(160, y + 80, 67, 27); }
    QRect actionsRect(int y) const { return QRect(236, y + 80, 76, 27); }
    static void drawButton(QPainter &p, const QRect &r, const QString &label,
                           bool active, const QColor &accent) {
        p.save();
        p.setRenderHint(QPainter::Antialiasing, true);
        // Off means available but inactive, NOT disabled. A stronger border,
        // contrast and text make the buttons readable on dark Windows themes.
        p.setPen(QPen(active ? accent.lighter(145) : QColor("#62748A"), 1));
        p.setBrush(active ? accent : QColor("#344252"));
        p.drawRoundedRect(r, 6, 6);
        p.setFont(QFont(QStringLiteral("Segoe UI"), 9, QFont::DemiBold));
        p.setPen(QColor("#F5F8FC"));
        p.drawText(r, Qt::AlignCenter, label);
        p.restore();
    }
};

#endif // FFXIV_ARRANGEMENTWIDGET_H
