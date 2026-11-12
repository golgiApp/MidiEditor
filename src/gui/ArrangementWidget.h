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
#include <QPixmap>
#include <QRegularExpression>
#include <tuple>
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
#include <QLineEdit>
#include <QComboBox>
#include <QFormLayout>
#include "TimeSignatureDialog.h"
#include "../MidiEvent/TimeSignatureEvent.h"
#include "../midi/TempoMapEdit.h"
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
    struct SongSection {
        QString name;
        int start = 0;
        int end = 0;
    };

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
    int lastCopiedRegionId() const { return _lastCopiedRegionId; }
    void clearRegionSelection() {
        const bool hadSelection = _selectedRegionId || !_selectedRegionIds.empty();
        _selectedRegionId = 0;
        _selectedRegionIds.clear();
        _trackHeaderSelected = false;
        if (hadSelection && regionViewChanged) regionViewChanged();
        update();
    }


    // The tutorial points at the same geometry that the editor hit-tests.
    QRect instrumentControlRect(int track) const {
        return instrumentRect(rulerHeight() + track * laneHeight);
    }
    QRect muteSoloControlsRect(int track) const {
        const int y = rulerHeight() + track * laneHeight;
        return muteRect(y).united(soloRect(y));
    }
    QRect globalTracksControlRect() const {
        return {0, _rulerTop, 140, measureRulerHeight};
    }
    QRect tempoTrackRect() const {
        if (!_showGlobalTracks) return {};
        return {0, _rulerTop + tempoTop(), width(), tempoLaneHeight};
    }
    QRect cycleControlRect() const {
        return {headerWidth, _rulerTop, width() - headerWidth, cycleStripHeight};
    }
    QRect measureMarkerRect(int tick) const {
        const int x = xForMs(_file->msOfTick(tick));
        return QRect(x - 2, _rulerTop + cycleStripHeight + 1, 20,
                     measureRulerHeight - cycleStripHeight - 2)
            .intersected(QRect(headerWidth, _rulerTop, width() - headerWidth, measureRulerHeight));
    }
    QRect tutorialRegionRect(const xiv::MidiRegion &region) const {
        const int left = xForMs(_file->msOfTick(region.startTick));
        const int right = xForMs(_file->msOfTick(region.endTick()));
        const int top = rulerHeight() + region.trackIndex * laneHeight + 6;
        return QRect(left, top, right - left, laneHeight - 12)
            .intersected(QRect(headerWidth, top, width() - headerWidth, laneHeight - 12));
    }
    QRect tutorialRegionEndRect(const xiv::MidiRegion &region, bool loopHandle) const {
        const int left = xForMs(_file->msOfTick(region.startTick));
        const int right = xForMs(_file->msOfTick(region.endTick()));
        const int top = rulerHeight() + region.trackIndex * laneHeight + 6;
        return loopHandle ? loopHandleRect(left, right, top)
                          : QRect(right - 8, top + 24, 10, laneHeight - 36);
    }

    void revealTutorialTicks(int first, int last) {
        if (!_file) return;
        const int from = _file->msOfTick(first);
        const int to = _file->msOfTick(last);
        const int available = std::max(1, width() - headerWidth - 36);
        if (to > from)
            _scale = std::clamp(std::min(_scale, double(available) / (to - from)), 0.004, 0.7);
        const int padding = int(16 / _scale);
        setOriginMs(std::max(0, from - padding));
        refreshHorizontalRange();
        if (scrollTimeRequested) scrollTimeRequested(_originMs);
        update();
    }
    void setTutorialSnapTick(int tick) { _tutorialSnapTick = tick; }
    int snapTutorialTick(int tick) const {
        if (_file && _tutorialSnapTick >= 0 &&
            std::abs(xForMs(_file->msOfTick(tick)) -
                     xForMs(_file->msOfTick(_tutorialSnapTick))) <= 8)
            return _tutorialSnapTick;
        return tick;
    }

    bool canEditQuantize() const {
        return _file && _regionsReady && !MidiPlayer::isPlaying() && !MidiInput::recording();
    }
    // Called inside the track channel protocol action.
    void reassignRegionChannels(int trackIndex, int channel) {
        auto *before = _regionHistory.copy();
        for (const auto &region : _regions.regions()) {
            if (region.trackIndex != trackIndex || !region.ownedNotes) continue;
            auto notes = *region.ownedNotes;
            for (auto &note : notes) note.channel = channel;
            _regions.replaceOwnedNotes(region.id, std::move(notes));
        }
        _regionTrackEditPendingSave = true;
        _file->protocol()->enterUndoStep(new ProtocolItem(before, &_regionHistory));
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
        int endTick = _file->arrangementEndTick();
        for (const auto &section : _sections) endTick = std::max(endTick, section.end);
        const auto songEnd = static_cast<long long>(
            std::max(_file->maxTime(), _file->msOfTick(endTick))) + 12000;
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
    static constexpr int measureRulerHeight = 34;
    static constexpr int sectionLaneHeight = 24;
    static constexpr int signatureLaneHeight = 24;
    static constexpr int tempoLaneHeight = 94;
    int rulerHeight() const {
        return measureRulerHeight + (_showGlobalTracks
            ? sectionLaneHeight + signatureLaneHeight + tempoLaneHeight : 0);
    }
    std::function<QList<QKeySequence>(const QString &)> commandShortcuts;
    void replaceRegionNotes(int id, std::vector<xiv::OwnedRegionNote> notes,
                            const QString &description) {
        const auto *region = _regions.find(id);
        if (!_file || MidiPlayer::isPlaying() || MidiInput::recording() ||
            id != _selectedRegionId || !region || region->loopLengthTicks) return;
        ProtocolEntry *before = _regionHistory.copy();
        if (!_regions.replaceOwnedNotes(id, std::move(notes))) {
            delete before;
            return;
        }
        finishRegionEdit(before, description);
    }

    void replaceProjectedRegions(xiv::MidiRegionModel updated, const QString &description) {
        if (!canEditQuantize()) return;
        if (!updated.replaceValidated(updated.regions(), _file->numTracks())) return;
        auto *before = _regionHistory.copy();
        _regions = std::move(updated);
        finishRegionEdit(before, description);
    }

    void refreshRegionCreationPreview(bool controlDown) {
        const auto global = QCursor::pos();
        const bool hovering = isVisible() && QApplication::widgetAt(global) == this;
        updateEmptyRegionHover(mapFromGlobal(global),
                               controlDown && hovering ? Qt::ControlModifier : Qt::NoModifier);
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
            _lastCopiedRegionId = 0;
            _tutorialSnapTick = -1;
            _cycle = {};
            _cycleEnabled = false;
            _cycleDrag = CycleDrag::None;
            publishCycle();
        }
        _file = file;
        _selected = nullptr;
        _trackHeaderSelected = false;
        _emptyRegionButton = {};
        _preview.clear();
        _regions.clear();
        _sections.clear();
        _tempoSelected = -1;
        _tempoDrag = TempoDrag::None;
        _sectionPressed = false;
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
                    [this, file]() {
                        if (_file != file) return;
                        const bool refreshSelection = _regionHistoryRefreshPending;
                        _regionHistoryRefreshPending = false;
                        _file->calcMaxTime();
                        rebuildPreview();
                        if (refreshSelection && selectTrackRequested)
                            selectTrackRequested(_selected);
                        if (refreshSelection && regionViewChanged) regionViewChanged();
                    });
            connect(file, &MidiFile::trackChanged, this,
                    [this]() { rebuildPreview(); });
        }
        rebuildPreview();
        _lastSavedRegionChannels = assignedTrackChannels();
        refreshHorizontalRange();
        if (positionChanged) positionChanged(_playheadMs);
        if (regionViewChanged) regionViewChanged();
    }

    void setTrackLayoutEditing(bool editing) { _trackLayoutEditing = editing; }

    void recordTrackLayoutChange(const QList<MidiTrack *> &oldTracks, int source = -1,
                                 int destination = -1, int channel = -1) {
        auto *before = _regionHistory.copy();
        const auto &tracks = *_file->tracks();
        xiv::MidiRegionModel updated;
        std::vector<xiv::MidiRegion> regions;
        int nextId = 1;
        for (const auto &region : _regions.regions()) nextId = std::max(nextId, region.id + 1);
        for (int old : _regions.managedTracks()) {
            const int row = tracks.indexOf(oldTracks.at(old));
            if (row >= 0) updated.markManagedTrack(row);
            if (old == source) updated.markManagedTrack(destination);
        }
        for (auto region : _regions.regions()) {
            const int old = region.trackIndex;
            region.trackIndex = tracks.indexOf(oldTracks.at(old));
            if (region.trackIndex < 0) continue;
            regions.push_back(region);
            if (old != source) continue;
            region.id = nextId++;
            region.trackIndex = destination;
            if (region.ownedNotes) {
                auto notes = *region.ownedNotes;
                for (auto &note : notes) note.channel = channel;
                region.ownedNotes =
                    std::make_shared<const std::vector<xiv::OwnedRegionNote>>(std::move(notes));
            }
            regions.push_back(region);
        }
        if (!updated.replaceValidated(regions, tracks.size())) { delete before; return; }
        _regions = std::move(updated);
        _regionTracks = tracks;
        _regionHistoryRefreshPending = true;
        _regionTrackEditPendingSave = true;
        _file->setRegionLayout(_regions);
        _file->protocol()->enterUndoStep(new ProtocolItem(before, &_regionHistory));
    }

    void setSelectedTrack(MidiTrack *track) {
        if (_selected == track) return;
        _selected = track;
        _trackHeaderSelected = false;
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

    static QString regionDisplayName(const xiv::MidiRegion &region) {
        return region.name.empty() ? tr("Region %1").arg(region.id)
                                   : QString::fromStdString(region.name);
    }

    QString selectedRegionName() const {
        const auto *region = _regions.find(_selectedRegionId);
        return region ? regionDisplayName(*region) : tr("Select a region");
    }

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
        if (_trackLayoutEditing) return;
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
        setMinimumHeight(std::max(130, rulerHeight() + count * laneHeight));
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
        if (_file->isStandaloneMidi()) return true;
        // Track creation/removal must reach the MIDI and its sidecar together.
        if (_regionTrackEditPendingSave && midiPath.isEmpty()) return true;
        const QString path = midiPath.isEmpty() ? _file->path() : midiPath;
        if (path.trimmed().isEmpty() || !QFileInfo::exists(path)) return false;
        QJsonObject root;
        root.insert(QStringLiteral("format"), QStringLiteral("XIV Midi Studio regions"));
        int version = 1;
        root.insert(QStringLiteral("tracks"), std::max(1, _file->numTracks()));
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
            if (!r.name.empty()) entry.insert(QStringLiteral("name"), QString::fromStdString(r.name));
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
        QJsonArray sections;
        for (const auto &section : _sections)
            sections.append(QJsonArray{section.name, section.start, section.end});
        root.insert(QStringLiteral("sections"), sections);
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
        _lastSavedRegionChannels = assignedTrackChannels();
        _regionTrackEditPendingSave = false;
        return true;
    }

protected:
    bool event(QEvent *e) override {
        // Keep region shortcuts local to the Arrangement. Ctrl+T still
        // handles transposition elsewhere.
        if (e->type() == QEvent::ShortcutOverride) {
            const auto *key = static_cast<QKeyEvent *>(e);
            if ((_selectedRegionId && isSplitShortcut(key)) || isJoinShortcut(key) ||
                regionClipboardShortcut(key) ||
                (_selectedRegionId && regionOctaveShift(key) != 0) ||
                (matchesCommand(key, QStringLiteral("xiv_arrangement_delete")) &&
                 (_tempoSelected >= 0 || _selectedRegionId || (_trackHeaderSelected && _selected)))) {
                e->accept();
                return true;
            }
        }
        return QWidget::event(e);
    }

    void keyPressEvent(QKeyEvent *e) override {
        if (matchesCommand(e, QStringLiteral("xiv_arrangement_delete")) &&
            _tempoSelected >= 0 && _showGlobalTracks && canEditQuantize()) {
            if (!e->isAutoRepeat()) deleteSelectedTempoPoint();
            e->accept();
            return;
        }
        if (regionClipboardShortcut(e)) {
            if (!e->isAutoRepeat()) {
                if (matchesCommand(e, QStringLiteral("copy"))) copyRegion(_selectedRegionId);
                else if (_file && _selected)
                    pasteRegionAtPlayhead(_file->tracks()->indexOf(_selected));
            }
            e->accept();
            return;
        }
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
        if (const int semitones = regionOctaveShift(e); _selectedRegionId && semitones != 0) {
            if (!e->isAutoRepeat()) transposeRegion(_selectedRegionId, semitones);
            e->accept();
            return;
        }
        if (matchesCommand(e, QStringLiteral("xiv_arrangement_delete")) &&
            (_selectedRegionId || (_trackHeaderSelected && _selected)) && canEditQuantize()) {
            if (!e->isAutoRepeat()) {
                if (_selectedRegionId) deleteSelectedRegion();
                else deleteEmptySelectedTrack();
            }
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
            const int y = rulerHeight() + i * laneHeight;
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
            const int channel = t->assignedChannel();
            p.drawText(14, y + 44, tr("Track %1   |   Channel %2")
                       .arg(i + 1).arg(channel >= 0 && channel < 16
                            ? QString::number(channel + 1) : tr("Unassigned")));

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
                // Dedicated upper-right looping handle, distinct from
                // draggable left/right trim edges.
                const QRect loopIcon = loopHandleRect(
                    xForMs(_file->msOfTick(region.startTick)),
                    xForMs(_file->msOfTick(region.endTick())), y + 6);
                QRect title = regionRect.adjusted(5, 3, -4, -3);
                if (!loopIcon.isEmpty()) title.setRight(loopIcon.left() - 3);
                if (title.width() > 0)
                    p.drawText(title, Qt::AlignLeft | Qt::AlignTop,
                               p.fontMetrics().elidedText(regionDisplayName(region),
                                                         Qt::ElideRight, title.width()));
                if (!loopIcon.isEmpty()) {
                    p.fillRect(loopIcon, QColor(25, 34, 43, 170));
                    QFont loopFont = p.font();
                    loopFont.setPixelSize(17);
                    loopFont.setBold(true);
                    p.setFont(loopFont);
                    p.setPen(QColor("#E8B66D"));
                    p.drawText(loopIcon, Qt::AlignCenter, QString(QChar(ushort(0x21BB))));
                }
                if (regionRect.width() >= 47 && region.loopLengthTicks > 0) {
                    p.setPen(QColor("#FFE0A4"));
                    p.drawText(regionRect.adjusted(6, 20, -5, -3),
                               Qt::AlignLeft | Qt::AlignTop,
                               tr("Loop"));
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

        if (_regionDragging && groupRegionDrag()) {
            paintGroupRegionDrag(p);
        } else if (_regionDragging && _moveRegionId > 0) {
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
                const int top = rulerHeight() + previewRow * laneHeight;
                p.save();
                p.setClipRect(QRect(headerWidth, rulerHeight(),
                                    width()-headerWidth, height()-rulerHeight()));
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
            const int guideY = rulerHeight() + _hoverRow * laneHeight;
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
            p.drawLine(x, rulerHeight(), x, height());
            p.setPen(QPen(QColor("#FFDD75"), 2));
            p.drawLine(x, 0, x, height());

            p.setPen(Qt::NoPen);
            // One marker per track makes it clear that every lane is aligned
            // to the same playback position, including the selected track.
            for (int i = 0; i < tracks.size(); ++i) {
                const int y = rulerHeight() + i * laneHeight;
                if (y >= height()) break;
                p.setBrush(tracks.at(i) == _selected ? QColor("#FFFFFF") :
                           QColor("#FFDD75"));
                p.drawEllipse(QPoint(x, y + laneHeight / 2), 3, 3);
            }
            p.restore();
        }
        if (!_emptyRegionButton.isEmpty() && canEditQuantize()) {
            p.save();
            p.setPen(QColor("#9BCDF1"));
            p.setBrush(QColor(72, 129, 167, 105));
            p.drawRoundedRect(_emptyRegionButton, 4, 4);
            const QPoint center = _emptyRegionButton.center();
            p.drawLine(center + QPoint(-4, 0), center + QPoint(4, 0));
            p.drawLine(center + QPoint(0, -4), center + QPoint(0, 4));
            p.restore();
        }
        drawRuler(p, measureMarks, x);
    }

    void contextMenuEvent(QContextMenuEvent *e) override {
        if (inTempoLane(e->pos()) && e->pos().x() >= headerWidth && canEditQuantize()) {
            auto points = xiv::readTempoMap(_file);
            const auto [kind, tick] = tempoHit(points, e->pos());
            if (kind == TempoDrag::Point) {
                _tempoSelected = tick;
                setFocus(Qt::MouseFocusReason);
                update();
                QMenu menu(this);
                auto *edit = menu.addAction(tr("Edit BPM…"));
                auto *remove = menu.addAction(withShortcut(
                    tr("Delete Tempo Point"), QStringLiteral("xiv_arrangement_delete")));
                remove->setEnabled(tick > 0);
                const auto *chosen = menu.exec(e->globalPos());
                if (chosen == edit) editTempoHit(e->pos());
                else if (chosen == remove) deleteSelectedTempoPoint();
            }
            e->accept();
            return;
        }
        _tempoSelected = -1;
        if (inSectionLane(e->pos()) && e->pos().x() >= headerWidth) {
            showSectionMenu(e);
            e->accept();
            return;
        }
        if (_file && e->pos().x() >= headerWidth && isLaneY(e->pos().y())) {
            const QPoint pos = e->pos();
            const int row = (pos.y() - rulerHeight()) / laneHeight;
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
                auto *copy = menu.addAction(withShortcut(tr("Copy Region"), QStringLiteral("copy")));
                auto *paste = menu.addAction(withShortcut(tr("Paste Region at Playhead"), QStringLiteral("paste")));
                paste->setEnabled(_regionClipboard.duration > 0 && canEditQuantize());
                menu.addSeparator();
                QAction *join = menu.addAction(withShortcut(
                    tr("Join Selected Regions Here"), QStringLiteral("xiv_join_regions")));
                join->setEnabled(_selectedRegionIds.size() > 1 && canEditQuantize());
                menu.addSeparator();
                QAction *splitHere = menu.addAction(tr("Split Here"));
                splitHere->setEnabled(canSplitAt(*region, tick));
                const int cursorTick = _file->cursorTick();
                QAction *splitAtCursor = menu.addAction(withShortcut(
                    tr("Split at Playhead"), QStringLiteral("xiv_split_regions")));
                splitAtCursor->setEnabled(std::any_of(_selectedRegionIds.begin(),
                    _selectedRegionIds.end(), [&](int selected) {
                        const auto *target = _regions.find(selected);
                        return target && canSplitAt(*target, cursorTick);
                    }));
                menu.addSeparator();
                auto *rename = menu.addMenu(tr("Rename"));
                rename->setEnabled(canEditQuantize());
                connect(rename->addAction(tr("Rename Region…")), &QAction::triggered,
                        this, [this, id]() { renameRegions(id, false); });
                connect(rename->addAction(tr("Use Track Name")), &QAction::triggered,
                        this, [this, id]() { renameRegions(id, true); });
                addRegionNoteActions(menu, id);
                menu.addSeparator();
                QAction *deleteRegionAction = menu.addAction(withShortcut(
                    tr("Delete Selected Regions"), QStringLiteral("xiv_arrangement_delete")));
                deleteRegionAction->setEnabled(canEditQuantize());
                QAction *chosen = menu.exec(e->globalPos());
                if (chosen == copy) copyRegion(id);
                else if (chosen == paste) pasteRegionAtPlayhead(row);
                else if (chosen == join) joinSelectedRegions(id);
                else if (chosen == splitHere) splitRegion(id, tick);
                else if (chosen == splitAtCursor) splitSelectedAtPlayhead();
                else if (chosen == deleteRegionAction) deleteSelectedRegion();
                update();
                e->accept();
                return;
            }
        }
        if (_file && e->pos().x() >= headerWidth && isLaneY(e->pos().y())) {
            const int row = (e->pos().y() - rulerHeight()) / laneHeight;
            if (row >= 0 && row < _file->numTracks()) {
                QMenu menu(this);
                const int createTick = emptyRegionStartTick(e->pos().x());
                auto *create = menu.addAction(tr("Add Empty Region Here"));
                create->setEnabled(canEditQuantize() && emptyRegionDuration(row, createTick) > 0);
                auto *paste = menu.addAction(withShortcut(tr("Paste Region at Playhead"), QStringLiteral("paste")));
                paste->setEnabled(_regionClipboard.duration > 0 && canEditQuantize());
                QAction *chosen = menu.exec(e->globalPos());
                if (chosen == create) addEmptyRegion(row, createTick);
                else if (chosen == paste) pasteRegionAtPlayhead(row);
                e->accept();
                return;
            }
        }
        if (_file && e->pos().x() < headerWidth && isLaneY(e->pos().y())) {
            const int row = (e->pos().y() - rulerHeight()) / laneHeight;
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
        const auto pos = e->position().toPoint();
        if (e->button() == Qt::LeftButton && canEditQuantize() && pos.x() >= headerWidth) {
            if (inTempoLane(pos)) {
                _tempoDrag = TempoDrag::None;
                editTempoHit(pos);
                e->accept();
                return;
            }
            if (inSignatureLane(pos)) {
                auto *events = _file->timeSignatureEvents();
                for (auto it = events->cbegin(); it != events->cend(); ++it) {
                    const int x = xForMs(_file->msOfTick(it.key()));
                    if (pos.x() >= x - 3 && pos.x() <= x + 35) {
                        editMeter(it.key());
                        break;
                    }
                }
                e->accept();
                return;
            }
        }
        if (_file && e->button() == Qt::LeftButton && e->position().x() < headerWidth &&
            isLaneY(int(e->position().y())) &&
            (int(e->position().y()) - rulerHeight()) % laneHeight < 47) {
            const int row = (int(e->position().y()) - rulerHeight()) / laneHeight;
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
        const int headerY = pos.y() - _rulerTop;
        if (pos.x() < headerWidth && headerY >= 0 && headerY < measureRulerHeight) {
            if (pos.x() < 140) {
                _showGlobalTracks = !_showGlobalTracks;
                _tempoSelected = -1;
                setMinimumHeight(std::max(130, rulerHeight() + _file->numTracks() * laneHeight));
                update();
            }
            e->accept();
            return;
        }
        if (inTempoLane(pos) || inSignatureLane(pos) || inSectionLane(pos)) {
            setFocus(Qt::MouseFocusReason);
            if (inTempoLane(pos)) {
                if (canEditQuantize()) {
                    if (pos.x() < headerWidth) {
                        if (globalAddRect(tempoTop()).contains(QPoint(pos.x(), headerY)))
                            addTempoPoint(_file->cursorTick());
                    } else beginTempoDrag(pos);
                }
                e->accept();
                return;
            }
            _tempoSelected = -1;
            if (inSignatureLane(pos)) {
                if (canEditQuantize() && globalAddRect(signatureTop()).contains(QPoint(pos.x(), headerY)))
                    editMeter(_file->cursorTick());
                e->accept();
                return;
            }
            if (pos.x() < headerWidth) {
                if (canEditQuantize() && globalAddRect(measureRulerHeight).contains(QPoint(pos.x(), headerY)))
                    addSectionAtPlayhead();
                e->accept();
                return;
            }
        } else {
            _tempoSelected = -1;
        }
        if (e->modifiers().testFlag(Qt::ControlModifier) && canEditQuantize() &&
            pos.x() >= headerWidth && isLaneY(pos.y())) {
            const int row = (pos.y() - rulerHeight()) / laneHeight;
            if (row >= 0 && row < _file->numTracks() && !hitRegion(row, pos.x())) {
                const int tick = emptyRegionStartTick(pos.x());
                if (emptyRegionDuration(row, tick) > 0) {
                    _emptyRegionButton = {};
                    setFocus(Qt::MouseFocusReason);
                    addEmptyRegion(row, tick);
                    e->accept();
                    return;
                }
            }
        }
        if (inCycleStrip(pos)) {
            beginCycleDrag(pos.x());
            e->accept();
            return;
        }
        if (isLaneY(pos.y()) && pos.x() < headerWidth) {
            setFocus(Qt::MouseFocusReason);
            const int row = (pos.y() - rulerHeight()) / laneHeight;
            if (row >= 0 && row < _file->tracks()->size()) {
                MidiTrack *track = _file->tracks()->at(row);
                if (instrumentRect(rulerHeight() + row * laneHeight).contains(pos)) {
                    setSelectedTrack(track);
                    if (selectTrackRequested) selectTrackRequested(track);
                    if (instrumentMenuRequested) instrumentMenuRequested(track, mapToGlobal(pos));
                } else if (visibilityRect(rulerHeight() + row * laneHeight).contains(pos)) {
                    _file->protocol()->startNewAction(track->hidden() ? tr("Show Track") : tr("Hide Track"));
                    track->setHidden(!track->hidden());
                    _file->protocol()->endAction();
                    update();
                } else if (actionsRect(rulerHeight() + row * laneHeight).contains(pos)) {
                    setSelectedTrack(track);
                    if (selectTrackRequested) selectTrackRequested(track);
                    if (trackMenuRequested) trackMenuRequested(track, mapToGlobal(pos));
                } else if (muteRect(rulerHeight() + row * laneHeight).contains(pos)) {
                    _file->protocol()->startNewAction(track->muted() ? tr("Unmute Track") : tr("Mute Track"));
                    track->setMuted(!track->muted());
                    _file->protocol()->endAction();
                    update();
                } else if (soloRect(rulerHeight() + row * laneHeight).contains(pos)) {
                    _solo = (_solo == track) ? nullptr : track;
                    _file->setSoloTrack(_solo);
                    update();
                } else {
                    setSelectedTrack(track);
                    if (selectTrackRequested) selectTrackRequested(track);
                    _trackHeaderSelected = true;
                    _selectedRegionIds.clear();
                    for (const auto &region : _regions.regions())
                        if (region.trackIndex == row) _selectedRegionIds.insert(region.id);
                    _selectedRegionId = _selectedRegionIds.empty() ? 0 : *_selectedRegionIds.begin();
                    if (regionViewChanged) regionViewChanged();
                    update();
                }
                e->accept();
                return;
            }
        }

        // Region clicks select material. Empty lanes deselect, and dragging
        // empty space still pans the timeline.
        if (pos.x() >= headerWidth && isLaneY(pos.y())) {
            const int row = (pos.y() - rulerHeight()) / laneHeight;
            const xiv::MidiRegion *r = hitRegion(row, pos.x());
            if (r && row >= 0 && row < _file->tracks()->size()) {
                _trackHeaderSelected = false;
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
                const bool wasSelected = r->id == _selectedRegionId || _selectedRegionIds.count(r->id);
                const bool canDrag = !MidiPlayer::isPlaying() && !MidiInput::recording();
                const int left = xForMs(_file->msOfTick(r->startTick));
                const int right = xForMs(_file->msOfTick(r->endTick()));
                const bool leftHandle = left >= headerWidth && std::abs(pos.x() - left) <= 8;
                const bool rightHandle = right < width() && std::abs(pos.x() - right) <= 8;
                const int regionTop = rulerHeight() + row * laneHeight + 6;
                const bool loopHandle = loopHandleRect(left, right, regionTop).contains(pos);
                _dragMode = loopHandle ? RegionDragMode::Loop :
                    add ? RegionDragMode::Move :
                    e->modifiers().testFlag(Qt::ControlModifier) ? RegionDragMode::Duplicate :
                    leftHandle ? RegionDragMode::TrimLeft :
                    rightHandle ? RegionDragMode::TrimRight : RegionDragMode::Move;
                const bool groupGesture = _dragMode == RegionDragMode::Move ||
                                          _dragMode == RegionDragMode::Loop;
                _deselectRegionOnClick = add && wasSelected && canDrag;
                if (_deselectRegionOnClick) {
                    _selectedRegionIds.insert(r->id);
                    _selectedRegionId = r->id;
                } else if (add) {
                    if (_selectedRegionId) _selectedRegionIds.insert(_selectedRegionId);
                    if (!_selectedRegionIds.erase(r->id)) _selectedRegionIds.insert(r->id);
                    _selectedRegionId = _selectedRegionIds.count(r->id) ? r->id :
                        (_selectedRegionIds.empty() ? 0 : *_selectedRegionIds.begin());
                } else {
                    if (!wasSelected || !canDrag || !groupGesture)
                        _selectedRegionIds = {r->id};
                    _selectedRegionId = r->id;
                }
                if (canDrag) {
                    _regionPressed = true;
                    _regionDragging = false;
                    _shiftRegionPress = add;
                    _moveRegionId = r->id;
                    _moveRow = row;
                    _movePreviewRow = row;
                    _movePressedY = pos.y();
                    _movePressedX = pos.x();
                    _moveOffsetTick = timelineTickAt(pos.x()) - r->startTick;
                    _moveProposedTick = (_dragMode == RegionDragMode::TrimRight ||
                                         _dragMode == RegionDragMode::Loop) ?
                        r->endTick() : r->startTick;
                }
                refreshClickedRegionSelection(row);
                e->accept();
                return;
            }
        }
        // Defer ruler seeks and lane deselection until release to distinguish panning.
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
        if (_tempoDrag != TempoDrag::None) {
            updateTempoDraft(pos);
            e->accept();
            return;
        }
        updateEmptyRegionHover(pos, e->modifiers());
        if (_sectionPressed) {
            updateSectionDraft(pos.x());
            e->accept();
            return;
        }
        if (inTempoLane(pos) && pos.x() >= headerWidth && !_timelinePressed &&
            !_regionPressed && _cycleDrag == CycleDrag::None) {
            const auto points = xiv::readTempoMap(_file);
            const auto [kind, tick] = tempoHit(points, pos);
            setCursor(kind == TempoDrag::None ? Qt::CrossCursor : Qt::SizeAllCursor);
            if (kind == TempoDrag::Corner) {
                QToolTip::showText(e->globalPosition().toPoint(),
                    points.value(tick).rampToNext
                        ? tr("Drag to shape the tempo ramp. Double-click to restore the step.")
                        : tr("Drag to turn the tempo step into a ramp."), this);
            } else {
                QToolTip::hideText();
            }
            e->accept();
            return;
        }
        if (inSectionLane(pos) && !_timelinePressed && !_regionPressed &&
            _cycleDrag == CycleDrag::None) {
            setCursor(sectionAt(pos.x()) >= 0 ? Qt::OpenHandCursor : Qt::CrossCursor);
            QToolTip::showText(e->globalPosition().toPoint(),
                tr("Drag empty space to name a section. Drag a section to move its regions. Right-click to rename or delete."),
                this);
            e->accept();
            return;
        }
        if (!_emptyRegionButton.isEmpty()) {
            setCursor(Qt::PointingHandCursor);
            QToolTip::hideText();
            e->accept();
            return;
        }
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
            const int row = (pos.y() - rulerHeight()) / laneHeight;
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
                if (_dragMode == RegionDragMode::Loop) setCursor(loopCursor());
                else setCursor((_dragMode == RegionDragMode::TrimLeft ||
                                _dragMode == RegionDragMode::TrimRight) ?
                               Qt::SizeHorCursor : Qt::SizeAllCursor);
            }
            if (_regionDragging) {
                updateRegionDragTarget(pos);
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
            const int row = (pos.y() - rulerHeight()) / laneHeight;
            if (row >= 0 && row < _file->tracks()->size()) {
                const int top = rulerHeight() + row * laneHeight;
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
            const int row = (pos.y() - rulerHeight()) / laneHeight;
            if (const auto *r = hitRegion(row, pos.x())) {
                const int left = xForMs(_file->msOfTick(r->startTick));
                const int right = xForMs(_file->msOfTick(r->endTick()));
                const int regionTop = rulerHeight() + row * laneHeight + 6;
                const bool loopHandle = loopHandleRect(left, right, regionTop).contains(pos);
                const bool edge = (left >= headerWidth && std::abs(pos.x()-left) <= 8) ||
                                  (right < width() && std::abs(pos.x()-right) <= 8);
                if (loopHandle) setCursor(loopCursor());
                else setCursor(edge ? Qt::SizeHorCursor : Qt::SizeAllCursor);
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
        if (e->button() == Qt::LeftButton && _tempoDrag != TempoDrag::None) {
            finishTempoDrag(e->position().toPoint());
            e->accept();
            return;
        }
        if (e->button() == Qt::LeftButton && _sectionPressed) {
            const int x = int(e->position().x());
            updateSectionDraft(x);
            _sectionPressed = false;
            unsetCursor();
            if (std::abs(x - _sectionAnchorX) >= QApplication::startDragDistance())
                commitSectionDraft(x);
            update();
            e->accept();
            return;
        }
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
            const bool group = groupRegionDrag();
            if (drag && (_dragMode == RegionDragMode::Move || group))
                updateRegionDragTarget(e->position().toPoint());
            const int targetTick = _moveProposedTick;
            const int releaseRow = (int(e->position().y()) - rulerHeight()) / laneHeight;
            _regionPressed = false;
            _regionDragging = false;
            _moveRegionId = 0;
            unsetCursor();
            if (!drag && _deselectRegionOnClick) {
                _selectedRegionIds.erase(id);
                _selectedRegionId = _selectedRegionIds.empty() ? 0 : *_selectedRegionIds.begin();
                refreshClickedRegionSelection(_moveRow);
            } else if (!drag && group && !_shiftRegionPress) {
                _selectedRegionIds = {id};
                refreshClickedRegionSelection(_moveRow);
            } else if (drag && group) {
                if (isLaneY(int(e->position().y()))) commitGroupRegionDrag(id, targetTick);
            } else if (drag && isLaneY(int(e->position().y())) &&
                (_dragMode == RegionDragMode::Move || releaseRow == _moveRow)) {
                switch (_dragMode) {
                case RegionDragMode::Move: moveRegion(id, targetTick, releaseRow); break;
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
                    setFocus(Qt::MouseFocusReason);
                    clearRegionSelection();
                    e->accept();
                    return;
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
    // Anchor the painted handle and its hit target to the actual region end.
    // A region that continues beyond the viewport has no visible end handle.
    static const QCursor &loopCursor() {
        static const QCursor cursor = [] {
            QPixmap icon(24, 24);
            icon.fill(Qt::transparent);
            QPainter painter(&icon);
            painter.setRenderHint(QPainter::Antialiasing);
            const QRectF ring(4, 4, 15, 15);
            const QPolygonF tip{QPointF(19, 8), QPointF(15, 13), QPointF(22, 13)};
            painter.setPen(QPen(Qt::black, 4, Qt::SolidLine, Qt::RoundCap));
            painter.drawArc(ring, 50 * 16, 310 * 16);
            painter.setPen(QPen(Qt::white, 2, Qt::SolidLine, Qt::RoundCap));
            painter.drawArc(ring, 50 * 16, 310 * 16);
            painter.setPen(QPen(Qt::black, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.setBrush(Qt::white);
            painter.drawPolygon(tip);
            painter.end();
            return QCursor(icon, 12, 12);
        }();
        return cursor;
    }

    QRect loopHandleRect(int left, int right, int top) const {
        if (right - left < 47 || right > width() - 2) return QRect();
        const QRect handle(right - 33, top + 3, 22, 22);
        if (handle.left() < headerWidth + 3) return QRect();
        return handle;
    }

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
                _sectionsAtCapture = widget->_sections;
                _tracksAtCapture = widget->_regionTracks;
                _channelsAtCapture = widget->assignedTrackChannels();
                _selectedTrackAtCapture = widget->_selected;
                _trackHeaderAtCapture = widget->_trackHeaderSelected;
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
            w->_regionTracks = old->_tracksAtCapture;
            if (tracksChanging || tracks != w->_lastSavedRegionTracks ||
                old->_channelsAtCapture != w->_lastSavedRegionChannels ||
                w->assignedTrackChannels() != w->_lastSavedRegionChannels)
                w->_regionTrackEditPendingSave = true;
            w->_regionHistoryRefreshPending = true;
            w->_regions = old->_regionsAtCapture;
            w->_sections = old->_sectionsAtCapture;
            w->_sectionPressed = false;
            w->_selectedRegionId = old->_selectedAtCapture;
            w->_selectedRegionIds = old->_selectionAtCapture;
            w->_selected = old->_selectedTrackAtCapture;
            w->_trackHeaderSelected = old->_trackHeaderAtCapture;
            w->_file->setRegionLayout(w->_regions);
            w->refreshHorizontalRange();
            // Undo and Redo also persist the restored layout, so a reopened
            // document matches the visible arrangement after the shortcut.
            if (!w->_file->path().isEmpty() && QFileInfo::exists(w->_file->path()))
                w->saveRegionLayout();
            w->update();
        }
    private:
        ArrangementWidget *owner() const {
            return static_cast<ArrangementWidget *>(_owner.data());
        }
        QPointer<QWidget> _owner;
        QPointer<MidiFile> _fileAtCapture;
        xiv::MidiRegionModel _regionsAtCapture;
        QVector<SongSection> _sectionsAtCapture;
        QList<MidiTrack *> _tracksAtCapture;
        QList<int> _channelsAtCapture;
        MidiTrack *_selectedTrackAtCapture = nullptr;
        bool _trackHeaderAtCapture = false;
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
    QList<int> _lastSavedRegionChannels;
    QList<int> assignedTrackChannels() const {
        QList<int> result;
        if (_file) {
            for (auto *track : *_file->tracks()) result.append(track->assignedChannel());
        }
        return result;
    }
    bool _trackHeaderSelected = false;
    bool _trackLayoutEditing = false;
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

    enum class TempoDrag { None, Point, Corner };
    TempoDrag _tempoDrag = TempoDrag::None;
    xiv::TempoMap _tempoDraft;
    int _tempoSelected = -1;
    int _tempoOriginalTick = -1;
    QPoint _tempoPress;
    double _tempoLow = 40;
    double _tempoHigh = 200;
    bool _tempoMoved = false;

    int signatureTop() const { return measureRulerHeight + sectionLaneHeight; }
    int tempoTop() const { return signatureTop() + signatureLaneHeight; }
    bool inSignatureLane(const QPoint &pos) const {
        const int y = pos.y() - _rulerTop;
        return _file && _showGlobalTracks && y >= signatureTop() && y < tempoTop();
    }
    bool inTempoLane(const QPoint &pos) const {
        const int y = pos.y() - _rulerTop;
        return _file && _showGlobalTracks && y >= tempoTop() && y < rulerHeight();
    }
    static QRect globalAddRect(int top) { return {headerWidth - 32, top + 2, 24, 20}; }
    void drawGlobalHeader(QPainter &p, int top, int height, const QString &name) const {
        p.setPen(QColor("#E8EFF8"));
        p.drawText(QRect(14, top, headerWidth - 55, 24), Qt::AlignVCenter, name);
        p.fillRect(globalAddRect(top), QColor("#394553"));
        p.drawText(globalAddRect(top), Qt::AlignCenter, QStringLiteral("+"));
        p.setPen(QColor("#515966"));
        p.drawLine(0, top + height - 1, width(), top + height - 1);
    }
    void createNamedSection(SongSection section) {
        if (!canPlaceSection(_sections, section)) return;
        const auto document = _file;
        QDialog dialog(this);
        dialog.setWindowTitle(tr("Add Arrangement Section"));
        QFormLayout form(&dialog);
        auto *names = new QComboBox(&dialog);
        names->addItems({tr("Intro"), tr("Verse"), tr("Chorus"), tr("Bridge"),
                         tr("Solo"), tr("Outro"), QStringLiteral("A"), QStringLiteral("B"),
                         QStringLiteral("C"), QStringLiteral("D"), QStringLiteral("E"), tr("Custom…")});
        auto *custom = new QLineEdit(&dialog);
        custom->hide();
        form.addRow(tr("Name"), names);
        form.addRow(custom);
        connect(names, &QComboBox::currentIndexChanged, &dialog, [names, custom](int index) {
            const bool isCustom = index == names->count() - 1;
            custom->setVisible(isCustom);
            if (isCustom) custom->setFocus();
        });
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        form.addRow(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
            section.name = (custom->isVisible() ? custom->text() : names->currentText()).trimmed();
            if (section.name.isEmpty()) custom->setFocus();
            else dialog.accept();
        });
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() != QDialog::Accepted || document != _file ||
            !canEditQuantize() || !canPlaceSection(_sections, section)) return;
        auto *before = _regionHistory.copy();
        _sections.append(section);
        finishRegionEdit(before, tr("Add Arrangement Section"));
    }
    void addSectionAtPlayhead() {
        const int start = _file->cursorTick();
        int end = start;
        for (int i = 0; i < 4; ++i) {
            int barStart = 0, barEnd = 0;
            _file->measure(end, &barStart, &barEnd);
            if (barEnd <= end) break;
            end = barEnd;
        }
        for (const auto &section : _sections) {
            if (section.start <= start && start < section.end) return;
            if (section.start > start) end = std::min(end, section.start);
        }
        createNamedSection({QString(), start, end});
    }
    void editMeter(int tick) {
        if (!_file->numTracks()) return;
        int start = 0, end = 0;
        const int bar = _file->measure(tick, &start, &end);
        TimeSignatureDialog dialog(_file, bar, start, this);
        dialog.exec();
    }
    std::pair<double, double> tempoScale(const xiv::TempoMap &points) const {
        double low = 40, high = 200;
        for (const auto &point : points) {
            low = std::min(low, point.bpm() - 10);
            high = std::max(high, point.bpm() + 10);
        }
        return {low, high};
    }
    int tempoY(double bpm, double low, double high) const {
        return tempoTop() + tempoLaneHeight - 10 -
            qRound((bpm - low) * (tempoLaneHeight - 20) / (high - low));
    }
    void drawMeterAndTempo(QPainter &p) const {
        const auto *meters = _file->timeSignatureEvents();
        p.setPen(QColor("#CDE2F4"));
        for (auto it = meters->cbegin(); it != meters->cend(); ++it) {
            auto *meter = dynamic_cast<TimeSignatureEvent *>(it.value());
            if (!meter) continue;
            const int x = xForMs(_file->msOfTick(it.key()));
            if (x < headerWidth || x >= width()) continue;
            p.drawText(QRect(x + 3, signatureTop(), 44, signatureLaneHeight), Qt::AlignVCenter,
                       QStringLiteral("%1/%2").arg(meter->num()).arg(1 << std::clamp(meter->denom(), 0, 15)));
        }

        const auto points = _tempoDrag == TempoDrag::None ? xiv::readTempoMap(_file) : _tempoDraft;
        const auto [low, high] = _tempoDrag == TempoDrag::None ? tempoScale(points)
            : std::make_pair(_tempoLow, _tempoHigh);
        p.save();
        p.setClipRect(QRect(headerWidth, tempoTop(), std::max(0, width() - headerWidth),
                            tempoLaneHeight), Qt::IntersectClip);
        p.fillRect(QRect(headerWidth, tempoTop(), width() - headerWidth, tempoLaneHeight), QColor("#202931"));
        for (auto point = points.cbegin(); point != points.cend(); ++point) {
            const int x = xForMs(_file->msOfTick(point.key()));
            const int y = tempoY(point.value().bpm(), low, high);
            const auto next = std::next(point);
            const int right = next == points.cend() ? width() : xForMs(_file->msOfTick(next.key()));
            const int nextY = next == points.cend() ? y : tempoY(next.value().bpm(), low, high);
            p.setPen(QPen(QColor("#72C5C0"), 2));
            if (point.value().rampToNext && next != points.cend()) {
                // Sample the same tempo curve used to generate playback events.
                QPoint last(x, y);
                for (int i = 1; i <= 32; ++i) {
                    const double fraction = i / 32.0;
                    const int tick = point.key() + qRound((next.key() - point.key()) * fraction);
                    const double bpm = xiv::tempoCurveBpm(point.value(), next.value(), fraction);
                    const QPoint sample(xForMs(_file->msOfTick(tick)), tempoY(bpm, low, high));
                    p.drawLine(last, sample);
                    last = sample;
                }
            } else {
                p.drawLine(x, y, right, y);
                if (next != points.cend()) p.drawLine(right, y, right, nextY);
            }
            if (next != points.cend() && (point.value().rampToNext || y != nextY)) {
                const auto corner = tempoCorner(point, next, low, high);
                if (point.value().rampToNext) {
                    const double bpm = xiv::tempoCurveBpm(point.value(), next.value(), point.value().curve.time);
                    p.setPen(QPen(QColor("#A4BC88"), 1, Qt::DashLine));
                    p.drawLine(corner, QPoint(corner.x(), tempoY(bpm, low, high)));
                }
                p.fillRect(QRect(corner - QPoint(3, 3), QSize(7, 7)), QColor("#A4BC88"));
            }
            p.fillRect(QRect(x - 4, y - 4, 9, 9), point.key() == _tempoSelected
                       ? QColor("#FFD386") : QColor("#72C5C0"));
            p.setPen(QColor("#CDE2F4"));
            p.drawText(x + 6, y - 4, QString::number(point.value().bpm(), 'f', 1));
        }
        p.restore();
    }
    QPoint tempoCorner(xiv::TempoMap::const_iterator point, xiv::TempoMap::const_iterator next,
                       double low, double high) const {
        if (!point.value().rampToNext)
            return {xForMs(_file->msOfTick(next.key())), tempoY(point.value().bpm(), low, high)};
        const auto curve = point.value().curve;
        const int tick = point.key() + qRound((next.key() - point.key()) * curve.time);
        const double bpm = std::lerp(point.value().bpm(), next.value().bpm(), curve.progress);
        return {xForMs(_file->msOfTick(tick)), tempoY(bpm, low, high)};
    }
    std::pair<TempoDrag, int> tempoHit(const xiv::TempoMap &points, const QPoint &pos) const {
        const auto [low, high] = tempoScale(points);
        const QPoint local(pos.x(), pos.y() - _rulerTop);
        std::pair<TempoDrag, int> hit{TempoDrag::None, -1};
        int closest = 10;
        for (auto point = points.cbegin(); point != points.cend(); ++point) {
            const QPoint dot(xForMs(_file->msOfTick(point.key())), tempoY(point.value().bpm(), low, high));
            const int distance = (dot - local).manhattanLength();
            if (distance < closest) {
                closest = distance;
                hit = {TempoDrag::Point, point.key()};
            }
        }
        for (auto point = points.cbegin(); point != points.cend(); ++point) {
            const auto next = std::next(point);
            if (next == points.cend()) break;
            if (point.value().rampToNext || point.value().microseconds != next.value().microseconds) {
                const int distance = (tempoCorner(point, next, low, high) - local).manhattanLength();
                if (distance < closest) {
                    closest = distance;
                    hit = {TempoDrag::Corner, point.key()};
                }
            }
        }
        return hit;
    }
    void deleteSelectedTempoPoint() {
        if (!canEditQuantize() || _tempoSelected <= 0) return;
        auto points = xiv::readTempoMap(_file);
        auto point = points.find(_tempoSelected);
        if (point == points.end()) return;
        std::prev(point).value().rampToNext = false;
        points.erase(point);
        _tempoSelected = -1;
        xiv::writeTempoMap(_file, points, tr("Delete Tempo Point"));
    }
    void addTempoPoint(int tick) {
        auto points = xiv::readTempoMap(_file);
        if (!points.contains(tick)) {
            xiv::insertTempoPoint(points, tick);
            xiv::writeTempoMap(_file, points, tr("Add Tempo Point"));
        }
        _tempoSelected = tick;
        update();
    }
    void beginTempoDrag(const QPoint &pos) {
        const auto points = xiv::readTempoMap(_file);
        const auto [kind, tick] = tempoHit(points, pos);
        if (kind == TempoDrag::None) {
            const int at = timelineTickAt(pos.x());
            const auto [low, high] = tempoScale(points);
            if (std::abs(pos.y() - _rulerTop - tempoY(xiv::tempoAt(points, at).bpm(), low, high)) <= 9)
                addTempoPoint(at);
            else _tempoSelected = -1;
            update();
            return;
        }
        _tempoDrag = kind;
        _tempoOriginalTick = tick;
        _tempoSelected = kind == TempoDrag::Point ? tick : -1;
        _tempoPress = pos;
        _tempoMoved = false;
        _tempoDraft = points;
        const auto [low, high] = tempoScale(points);
        _tempoLow = low;
        _tempoHigh = high;
        update();
    }
    void updateTempoDraft(const QPoint &pos) {
        const QPoint delta = pos - _tempoPress;
        if (!_tempoMoved && delta.manhattanLength() < QApplication::startDragDistance()) return;
        _tempoMoved = true;
        _tempoDraft = xiv::readTempoMap(_file);
        auto point = _tempoDraft.find(_tempoOriginalTick);
        if (point == _tempoDraft.end()) return;
        if (_tempoDrag == TempoDrag::Corner) {
            auto &value = point.value();
            const auto next = std::next(point);
            if (next == _tempoDraft.end()) return;
            if (value.rampToNext) {
                const int deltaTicks = timelineTickAt(pos.x()) - timelineTickAt(_tempoPress.x());
                const double bpmSpan = next.value().bpm() - value.bpm();
                if (delta.x() != 0)
                    value.curve.time = std::clamp(
                        value.curve.time + double(deltaTicks) / (next.key() - point.key()), 0.0, 1.0);
                if (delta.y() != 0 && bpmSpan != 0.0)
                    value.curve.progress = std::clamp(value.curve.progress -
                        delta.y() * (_tempoHigh - _tempoLow) / ((tempoLaneHeight - 20) * bpmSpan), 0.0, 1.0);
            } else {
                value.rampToNext = true;
                value.curve = {};
            }
        } else {
            auto value = point.value();
            if (delta.y() != 0)
                value.setBpm(value.bpm() - delta.y() * (_tempoHigh - _tempoLow) / (tempoLaneHeight - 20));
            int tick = _tempoOriginalTick;
            if (tick > 0 && delta.x() != 0) {
                const auto next = std::next(point);
                const int maximum = next == _tempoDraft.end() ? std::numeric_limits<int>::max() : next.key() - 1;
                tick = std::clamp(timelineTickAt(pos.x()), std::prev(point).key() + 1, maximum);
            }
            _tempoDraft.erase(point);
            _tempoDraft.insert(tick, value);
            _tempoSelected = tick;
        }
        update();
    }
    void finishTempoDrag(const QPoint &pos) {
        updateTempoDraft(pos);
        const auto gesture = _tempoDrag;
        _tempoDrag = TempoDrag::None;
        if (_tempoMoved && canEditQuantize())
            xiv::writeTempoMap(_file, _tempoDraft, gesture == TempoDrag::Corner
                              ? tr("Shape Tempo Ramp") : tr("Edit Tempo Point"));
        _tempoDraft.clear();
        update();
    }
    void editTempoHit(const QPoint &pos) {
        auto points = xiv::readTempoMap(_file);
        const auto [kind, tick] = tempoHit(points, pos);
        if (kind == TempoDrag::None) return;
        if (kind == TempoDrag::Corner) {
            if (!points[tick].rampToNext) return;
            points[tick].rampToNext = false;
            points[tick].curve = {};
        } else {
            const auto document = _file;
            bool ok = false;
            const double bpm = QInputDialog::getDouble(this, tr("Edit Tempo Point"), tr("BPM"),
                points[tick].bpm(), 4.0, 999.0, 2, &ok);
            if (!ok || _file != document || !canEditQuantize()) return;
            points[tick].setBpm(bpm);
            _tempoSelected = tick;
        }
        xiv::writeTempoMap(_file, points, tr("Edit Tempo Point"));
    }

    bool _showGlobalTracks = false;
    QVector<SongSection> _sections;
    SongSection _sectionDraft;
    bool _sectionPressed = false;
    int _sectionIndex = -1;
    int _sectionAnchorTick = 0;
    int _sectionAnchorX = 0;

    bool inSectionLane(const QPoint &pos) const {
        return _file && _showGlobalTracks &&
            pos.y() >= _rulerTop + measureRulerHeight && pos.y() < _rulerTop + signatureTop();
    }

    int sectionAt(int x) const {
        for (int i = 0; i < _sections.size(); ++i) {
            const auto &section = _sections.at(i);
            if (x >= xForMs(_file->msOfTick(section.start)) &&
                x < xForMs(_file->msOfTick(section.end))) return i;
        }
        return -1;
    }

    static bool canPlaceSection(const QVector<SongSection> &sections,
                                const SongSection &candidate, int ignored = -1) {
        if (candidate.start < 0 || candidate.end <= candidate.start) return false;
        for (int i = 0; i < sections.size(); ++i) {
            const auto &other = sections.at(i);
            if (i != ignored && candidate.start < other.end && other.start < candidate.end)
                return false;
        }
        return true;
    }

    void updateSectionDraft(int x) {
        const int tick = timelineTickAt(std::clamp(x, headerWidth, std::max(headerWidth, width())));
        if (_sectionIndex < 0) {
            const int first = snapTick(_sectionAnchorTick);
            const int last = snapTick(tick);
            _sectionDraft = {QString(), std::min(first, last), std::max(first, last)};
        } else {
            const auto &section = _sections.at(_sectionIndex);
            const int duration = section.end - section.start;
            const auto raw = std::clamp<int64_t>(
                static_cast<int64_t>(section.start) + tick - _sectionAnchorTick,
                0, std::numeric_limits<int>::max() - duration);
            const int start = std::min(snapTick(static_cast<int>(raw)),
                                       std::numeric_limits<int>::max() - duration);
            _sectionDraft = {section.name, start, start + duration};
        }
        update();
    }

    void commitSectionDraft(int x) {
        if (!canEditQuantize()) return;
        if (_sectionIndex < 0) {
            createNamedSection(_sectionDraft);
            return;
        }
        const auto section = _sections.at(_sectionIndex);
        const int target = sectionAt(x);
        int destination = _sectionDraft.start;
        int shiftStart = 0, shiftEnd = 0, shift = 0;
        if (target >= 0 && target != _sectionIndex) {
            const auto &other = _sections.at(target);
            const int length = section.end - section.start;
            if (other.start > section.start) {
                destination = other.end - length;
                shiftStart = section.end;
                shiftEnd = other.end;
                shift = -length;
            } else {
                destination = other.start;
                shiftStart = other.start;
                shiftEnd = section.start;
                shift = length;
            }
        } else if (!canPlaceSection(_sections, _sectionDraft, _sectionIndex)) {
            return;
        }
        if (destination == section.start) return;
        auto updated = _regions;
        std::set<int> boundaries{section.start, section.end};
        if (shift) { boundaries.insert(shiftStart); boundaries.insert(shiftEnd); }
        for (int boundary : boundaries) {
            std::vector<int> crossing;
            for (const auto &region : updated.regions())
                if (canSplitAt(region, boundary)) crossing.push_back(region.id);
            for (int id : crossing) {
                const auto *region = updated.find(id);
                if ((region->loopLengthTicks > 0 || region->quantize.active()) &&
                    !captureRegion(updated, id)) return;
                if (!updated.split(id, boundary, xiv::CrossingNoteMode::Split)) return;
            }
        }
        if (shift) {
            auto reordered = updated.regions();
            for (auto &region : reordered) {
                if (region.startTick >= section.start && region.endTick() <= section.end)
                    region.startTick += destination - section.start;
                else if (region.startTick >= shiftStart && region.endTick() <= shiftEnd)
                    region.startTick += shift;
            }
            if (!updated.replaceValidated(reordered, _file->numTracks())) return;
        } else if (!updated.moveContainedRegions(section.start, section.end, destination)) {
            return;
        }
        auto *before = _regionHistory.copy();
        _regions = std::move(updated);
        for (int i = 0; i < _sections.size(); ++i) {
            auto &item = _sections[i];
            const int delta = i == _sectionIndex ? destination - section.start :
                (shift && item.start >= shiftStart && item.end <= shiftEnd ? shift : 0);
            item.start += delta;
            item.end += delta;
        }
        finishRegionEdit(before, tr("Move Arrangement Section"));
    }

    void showSectionMenu(QContextMenuEvent *event) {
        const int index = sectionAt(event->pos().x());
        if (index < 0 || !canEditQuantize()) return;
        const auto document = _file;
        QMenu menu(this);
        auto *rename = menu.addAction(tr("Rename Section…"));
        auto *remove = menu.addAction(tr("Delete Section"));
        const auto *chosen = menu.exec(event->globalPos());
        if (!chosen || document != _file || !canEditQuantize() || index >= _sections.size())
            return;
        if (chosen == rename) {
            bool accepted = false;
            const QString oldName = _sections.at(index).name;
            const QString name = QInputDialog::getText(this, tr("Rename Arrangement Section"),
                tr("Section name"), QLineEdit::Normal, oldName, &accepted).trimmed();
            if (!accepted || name.isEmpty() || name == oldName || document != _file ||
                !canEditQuantize() || index >= _sections.size()) return;
            auto *before = _regionHistory.copy();
            _sections[index].name = name;
            finishRegionEdit(before, tr("Rename Arrangement Section"));
        } else if (chosen == remove) {
            auto *before = _regionHistory.copy();
            _sections.removeAt(index);
            finishRegionEdit(before, tr("Delete Arrangement Section"));
        }
    }

    void drawSection(QPainter &p, const SongSection &section, bool draft) const {
        const int left = std::max(headerWidth, xForMs(_file->msOfTick(section.start)));
        const int right = std::min(width(), xForMs(_file->msOfTick(section.end)));
        if (right <= left) return;
        const QRect block(left, measureRulerHeight + 2, right - left, sectionLaneHeight - 4);
        const bool placeable = !draft || canPlaceSection(_sections, section, _sectionIndex);
        p.fillRect(block, !placeable ? QColor("#653746")
                                    : draft ? QColor("#346C85") : QColor("#254E64"));
        p.setPen(QColor("#B9E4F9"));
        const QRect label = block.adjusted(5, 0, -4, 0);
        if (label.width() > 0)
            p.drawText(label, Qt::AlignVCenter,
                       p.fontMetrics().elidedText(section.name, Qt::ElideRight, label.width()));
    }

    bool isLaneY(int y) const { return y >= _rulerTop + rulerHeight(); }

    void drawRuler(QPainter &p, const QVector<QPair<int, int>> &marks, int playheadX) const {
        p.save();
        p.translate(0, _rulerTop);
        p.setClipRect(QRect(0, 0, width(), rulerHeight()));
        p.fillRect(0, 0, width(), rulerHeight(), QColor("#292D34"));
        p.setFont(QFont(QStringLiteral("Segoe UI"), 9, QFont::DemiBold));
        p.setPen(QColor("#E8EFF8"));
        p.drawText(QRect(14, 0, 125, measureRulerHeight), Qt::AlignVCenter,
                   _showGlobalTracks ? tr("▾ Global Tracks") : tr("▸ Global Tracks"));
        if (_showGlobalTracks) {
            drawGlobalHeader(p, measureRulerHeight, sectionLaneHeight, tr("Arrangement"));
            drawGlobalHeader(p, signatureTop(), signatureLaneHeight, tr("Time Signature"));
            drawGlobalHeader(p, tempoTop(), tempoLaneHeight, tr("Tempo"));
        }
        p.setPen(QColor("#515966"));
        p.drawLine(0, measureRulerHeight, width(), measureRulerHeight);
        p.drawLine(0, rulerHeight() - 1, width(), rulerHeight() - 1);
        p.drawLine(headerWidth, 0, headerWidth, rulerHeight());
        p.setClipRect(QRect(headerWidth, 0, std::max(0, width() - headerWidth), rulerHeight()));
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
            p.drawText(QRect(headerWidth + 12, 0, width() - headerWidth - 12, rulerHeight()),
                       Qt::AlignVCenter, tr("Open a MIDI file"));
        for (const auto &mark : marks)
            p.drawText(mark.first + 5, 27, QString::number(mark.second));
        if (_file && _showGlobalTracks) {
            drawMeterAndTempo(p);
            for (int i = 0; i < _sections.size(); ++i)
                if (!_sectionPressed || i != _sectionIndex) drawSection(p, _sections.at(i), false);
            if (_sectionPressed) drawSection(p, _sectionDraft, true);
        }
        if (playheadX >= headerWidth && playheadX < width()) {
            p.setPen(QPen(QColor("#FFDD75"), 2));
            p.drawLine(playheadX, cycleStripHeight + 8, playheadX, rulerHeight());
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
        const int tutorialTick = snapTutorialTick(tick);
        if (tutorialTick != tick || (_tutorialSnapTick >= 0 && tick == _tutorialSnapTick))
            return tutorialTick;
        if (!_snapToGrid || !_file) return std::max(0, tick);
        const int beatTicks = std::max(1, _file->ticksPerQuarter());
        const int64_t aligned = (static_cast<int64_t>(std::max(0, tick)) +
                                 beatTicks / 2) / beatTicks * beatTicks;
        return static_cast<int>(std::min<int64_t>(aligned,
                                                  std::numeric_limits<int>::max()));
    }
    bool matchesCommand(const QKeyEvent *event, const QString &id) const {
        return commandShortcuts && commandShortcuts(id).contains(QKeySequence(event->keyCombination()));
    }

    int regionOctaveShift(const QKeyEvent *event) const {
        if (matchesCommand(event, QStringLiteral("transpose_up"))) return 12;
        if (matchesCommand(event, QStringLiteral("transpose_down"))) return -12;
        return 0;
    }

    QString withShortcut(const QString &label, const QString &id) const {
        const auto bindings = commandShortcuts ? commandShortcuts(id) : QList<QKeySequence>{};
        QStringList display;
        for (const auto &binding : bindings)
            if (!binding.isEmpty()) display.append(binding.toString(QKeySequence::NativeText));
        return display.isEmpty() ? label : label + QLatin1Char('\t') + display.join(QStringLiteral(" / "));
    }

    bool isJoinShortcut(const QKeyEvent *e) const {
        return _selectedRegionId && _selectedRegionIds.size() > 1 &&
               matchesCommand(e, QStringLiteral("xiv_join_regions"));
    }

    bool isSplitShortcut(const QKeyEvent *e) const {
        return matchesCommand(e, QStringLiteral("xiv_split_regions"));
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

    void refreshClickedRegionSelection(int row) {
        const auto *primary = _regions.find(_selectedRegionId);
        MidiTrack *track = _file->tracks()->at(primary ? primary->trackIndex : row);
        setSelectedTrack(track);
        if (selectTrackRequested) selectTrackRequested(track);
        if (regionViewChanged) regionViewChanged();
        update();
    }

    bool groupRegionDrag() const {
        return _moveRegionId > 0 && _selectedRegionIds.size() > 1 &&
            _selectedRegionIds.count(_moveRegionId) &&
            (_dragMode == RegionDragMode::Move || _dragMode == RegionDragMode::Loop);
    }

    void updateRegionDragTarget(const QPoint &pos) {
        const int mouseTick = timelineTickAt(pos.x());
        if (_dragMode != RegionDragMode::Move && _dragMode != RegionDragMode::Duplicate) {
            _moveProposedTick = snapTick(mouseTick);
            return;
        }
        _moveProposedTick = snapTick(mouseTick - _moveOffsetTick);
        if (_dragMode != RegionDragMode::Move) return;
        _movePreviewRow = isLaneY(pos.y()) ? (pos.y() - rulerHeight()) / laneHeight : -1;
        const auto *anchor = _regions.find(_moveRegionId);
        if (!anchor) return;
        if (std::abs(pos.x() - _movePressedX) < QApplication::startDragDistance())
            _moveProposedTick = anchor->startTick;
        if (groupRegionDrag()) {
            int first = anchor->startTick;
            for (int id : _selectedRegionIds)
                if (const auto *region = _regions.find(id)) first = std::min(first, region->startTick);
            _moveProposedTick = std::max(_moveProposedTick, anchor->startTick - first);
        }
    }

    void paintGroupRegionDrag(QPainter &p) const {
        const auto *anchor = _regions.find(_moveRegionId);
        if (!anchor) return;
        const bool loop = _dragMode == RegionDragMode::Loop;
        const int delta = _moveProposedTick - (loop ? anchor->endTick() : anchor->startTick);
        const auto blockedTracks = loop ? _regions.blockedLoopTracks(_selectedRegionIds, delta)
                                        : std::set<int>{};
        const bool moveValid = loop || _regions.canMoveSelected(_selectedRegionIds, delta);
        p.save();
        p.setClipRect(QRect(headerWidth, rulerHeight(), width() - headerWidth, height() - rulerHeight()));
        for (int id : _selectedRegionIds) {
            const auto *region = _regions.find(id);
            if (!region) continue;
            const bool loopBlocked = blockedTracks.count(region->trackIndex);
            const bool valid = moveValid && !loopBlocked;
            p.setBrush(valid ? QColor(47, 172, 109, 62) : QColor(170, 57, 65, 62));
            p.setPen(QPen(QColor(valid ? "#70E4A0" : "#ED6977"), 2, Qt::DashLine));
            const auto proposedStart = static_cast<int64_t>(region->startTick) + (loop ? 0 : delta);
            const int start = static_cast<int>(std::clamp<int64_t>(
                proposedStart, 0, std::numeric_limits<int>::max()));
            const auto end = static_cast<int64_t>(region->endTick()) + (loopBlocked ? 0 : delta);
            const int left = std::max(headerWidth, xForMs(_file->msOfTick(start)));
            const int right = std::min(width() - 1, xForMs(_file->msOfTick(
                static_cast<int>(std::clamp<int64_t>(end, start, std::numeric_limits<int>::max())))));
            if (right <= left) continue;
            const int top = rulerHeight() + region->trackIndex * laneHeight;
            const QRect rect(QPoint(left, top + 7), QPoint(right, top + laneHeight - 7));
            if (loop && valid) p.drawPath(loopOutline(rect, start, static_cast<int>(end), region->duration()));
            else p.drawRect(rect);
        }
        p.restore();
    }

    void commitGroupRegionDrag(int id, int tick) {
        if (!canEditQuantize()) return;
        const auto *anchor = _regions.find(id);
        if (!anchor) return;
        const bool loop = _dragMode == RegionDragMode::Loop;
        const int delta = tick - (loop ? anchor->endTick() : anchor->startTick);
        auto updated = _regions;
        const bool changed = loop ? updated.loopSelected(_selectedRegionIds, delta)
                                  : updated.moveSelected(_selectedRegionIds, delta);
        if (changed) installRegionNoteEdit(std::move(updated), loop ? tr("Loop Regions") : tr("Move Regions"));
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

    void renameRegions(int id, bool useTrackName) {
        if (!canEditQuantize() || !_regions.find(id)) return;
        const auto document = _file;
        auto ids = _selectedRegionIds.count(id) ? _selectedRegionIds : std::set<int>{id};
        std::vector<xiv::MidiRegion> targets;
        for (int selected : ids)
            if (const auto *region = _regions.find(selected)) targets.push_back(*region);
        std::sort(targets.begin(), targets.end(), [](const auto &left, const auto &right) {
            return std::tie(left.startTick, left.trackIndex, left.id) <
                   std::tie(right.startTick, right.trackIndex, right.id);
        });

        QString name;
        if (!useTrackName) {
            bool accepted = false;
            const QString initial = targets.size() == 1 ? regionDisplayName(targets.front()) : QString();
            name = QInputDialog::getText(this, tr("Rename Region"), tr("Region name"),
                                        QLineEdit::Normal, initial, &accepted).trimmed();
            if (!accepted || name.isEmpty() || document != _file || !canEditQuantize()) return;
        }

        static const QRegularExpression trailingNumber(QStringLiteral("([0-9]+)$"));
        const auto suffix = trailingNumber.match(name);
        bool numbered = false;
        const auto number = suffix.captured(1).toULongLong(&numbered);
        const bool increment = numbered && targets.size() > 1 &&
            number <= std::numeric_limits<qulonglong>::max() - (targets.size() - 1);
        const QString prefix = increment ? name.left(suffix.capturedStart(1)) : name;
        auto updated = _regions;
        bool changed = false;
        for (std::size_t i = 0; i < targets.size(); ++i) {
            const auto *region = updated.find(targets[i].id);
            if (!region) continue;
            QString title = name;
            if (useTrackName) title = _file->track(region->trackIndex)->name();
            else if (increment)
                title = prefix + QStringLiteral("%1").arg(number + i, suffix.capturedLength(1), 10, QLatin1Char('0'));
            changed |= updated.rename(region->id, title.toStdString());
        }
        if (changed) installRegionNoteEdit(std::move(updated), tr("Rename Regions"));
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
        add(menu, withShortcut(tr("Transpose Region Up an Octave"), QStringLiteral("transpose_up")),
            [this, id]() { transposeRegion(id, 12); });
        add(menu, withShortcut(tr("Transpose Region Down an Octave"), QStringLiteral("transpose_down")),
            [this, id]() { transposeRegion(id, -12); });
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
        if (accepted && document == _file) transposeRegion(id, semitones);
    }

    void transposeRegion(int id, int semitones) {
        if (!semitones || !canEditQuantize()) return;
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

    xiv::RegionClipboard _regionClipboard;
    int _lastCopiedRegionId = 0;
    int _tutorialSnapTick = -1;

    bool regionClipboardShortcut(const QKeyEvent *event) const {
        return (_selectedRegionId && matchesCommand(event, QStringLiteral("copy"))) ||
               (_selected && _regionClipboard.duration > 0 && matchesCommand(event, QStringLiteral("paste")));
    }

    void copyRegion(int id) {
        auto snapshot = _regions;
        if (!captureRegion(snapshot, id)) return;
        const auto *region = snapshot.find(id);
        _regionClipboard = {region->duration(), *region->ownedNotes, region->name};
        _lastCopiedRegionId = id;
    }

    QRect _emptyRegionButton;

    void leaveEvent(QEvent *event) override {
        _emptyRegionButton = {};
        update();
        QWidget::leaveEvent(event);
    }

    void updateEmptyRegionHover(const QPoint &pos, Qt::KeyboardModifiers modifiers) {
        QRect preview;
        if (modifiers.testFlag(Qt::ControlModifier) && canEditQuantize() &&
            !_timelinePressed && !_regionPressed && _cycleDrag == CycleDrag::None &&
            rect().contains(pos) && pos.x() >= headerWidth && isLaneY(pos.y())) {
            const int row = (pos.y() - rulerHeight()) / laneHeight;
            if (row < _file->numTracks() && !hitRegion(row, pos.x())) {
                const int tick = emptyRegionStartTick(pos.x());
                const int duration = emptyRegionDuration(row, tick);
                if (duration > 0) {
                    const int left = std::max(headerWidth, xForMs(_file->msOfTick(tick)));
                    const int right = std::min(width(), xForMs(_file->msOfTick(tick + duration)));
                    preview = QRect(left, rulerHeight() + row * laneHeight + 6,
                                    std::max(1, right - left), laneHeight - 12);
                }
            }
        }
        if (_emptyRegionButton != preview) {
            _emptyRegionButton = preview;
            update();
        }
    }

    int emptyRegionStartTick(int x) const {
        int start = 0, end = 0;
        _file->measure(timelineTickAt(x), &start, &end);
        return start;
    }

    int emptyRegionDuration(int track, int tick) const {
        int boundary = tick;
        for (int i = 0; i < 4; ++i) {
            int measureStart = 0, measureEnd = 0;
            _file->measure(boundary, &measureStart, &measureEnd);
            if (measureEnd <= boundary || measureEnd <= measureStart) break;
            boundary = measureEnd;
        }
        return xiv::emptyRegionGapDuration(_regions, track, tick, boundary);
    }

    void addEmptyRegion(int track, int tick) {
        if (!canEditQuantize() || track < 0 || track >= _file->numTracks()) return;
        const int duration = emptyRegionDuration(track, tick);
        if (duration > 0)
            insertRegion(track, tick, {duration, {}}, tr("Add Empty Region"));
    }

    void pasteRegionAtPlayhead(int track) {
        if (!_file) return;
        insertRegion(track, _file->cursorTick(), _regionClipboard, tr("Paste Region"));
    }

    void insertRegion(int trackIndex, int tick, const xiv::RegionClipboard &clip,
                      const QString &description) {
        if (!canEditQuantize() || trackIndex < 0 || trackIndex >= _file->numTracks()) return;
        MidiTrack *track = _file->tracks()->at(trackIndex);
        int channel = track->assignedChannel();
        // A note-free region can also be created on an unassigned track.
        if (clip.notes.empty() && (channel < 0 || channel >= 16)) channel = 0;
        auto updated = _regions;
        const int id = xiv::insertOwnedRegion(updated, trackIndex, tick,
                                               _file->numTracks(), clip, channel);
        if (!id) {
            QToolTip::showText(QCursor::pos(), tr("Choose a track with a MIDI channel and a valid timeline position."), this);
            return;
        }

        auto *before = _regionHistory.copy();
        _regions = std::move(updated);
        _selectedRegionId = id;
        _selectedRegionIds = {id};
        setSelectedTrack(track);
        if (selectTrackRequested) selectTrackRequested(track);
        finishRegionEdit(before, description);
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
    void deleteEmptySelectedTrack() {
        const int index = _file->tracks()->indexOf(_selected);
        for (const auto &region : _regions.regions())
            if (region.trackIndex == index) return;
        const auto oldTracks = *_file->tracks();
        if (index < 0) return;
        _file->protocol()->startNewAction(tr("Delete Track"));
        setTrackLayoutEditing(true);
        if (_file->removeTrack(_selected)) recordTrackLayoutChange(oldTracks);
        setTrackLayoutEditing(false);
        _file->protocol()->endAction();
    }

    void deleteSelectedRegion() {
        if (!canEditQuantize() || !_selectedRegionId) return;
        ProtocolEntry *before = _regionHistory.copy();
        if (_selectedRegionIds.empty()) _regions.remove(_selectedRegionId);
        else for (int id : _selectedRegionIds) _regions.remove(id);
        _selectedRegionId = 0;
        _selectedRegionIds.clear();
        finishRegionEdit(before, tr("Delete Region"));
    }
    void loadRegionLayout() {
        if (!_file || _file->isStandaloneMidi() || _file->path().trimmed().isEmpty()) return;
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
            incoming.back().name = o.value(QStringLiteral("name")).toString().toStdString();
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
        if (!loaded.replaceValidated(incoming, trackCount)) {
            qWarning() << "XIV Midi Studio: invalid region sidecar ignored";
            return;
        }
        _regions = std::move(loaded);
        for (const auto &value : root.value(QStringLiteral("sections")).toArray()) {
            const auto row = value.toArray();
            if (row.size() != 3) continue;
            const SongSection section{row.at(0).toString().trimmed(),
                                      row.at(1).toInt(-1), row.at(2).toInt(-1)};
            if (!section.name.isEmpty() && canPlaceSection(_sections, section))
                _sections.append(section);
        }
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
    bool _shiftRegionPress = false;
    bool _deselectRegionOnClick = false;
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
