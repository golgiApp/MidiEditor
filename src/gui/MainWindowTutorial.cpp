/*
 * XIV MIDI Studio interactive walkthrough.
 * GPL-3.0-or-later; see LICENSE in the repository root.
 */
#include "MainWindow.h"

#include "ArrangementWidget.h"
#include "MatrixWidget.h"
#include "../midi/MidiChannel.h"

#include <QAction>
#include <QApplication>
#include <QFile>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <array>

namespace {
enum class Step {
    Welcome, Workspace, AddTuba, SeekSplit, Split, Move,
    Loop, Copy, SeekPaste, Paste, Trim, Join, Rewind, Listen,
    SelectDrums, DrumMap, DeleteHiHat, OctaveOne, OctaveTwo, ListenDrums,
    Tempo, PianoViews, PianoGrid, History, Zoom, TrackAudio, Cycle, Done
};

struct Prompt {
    QString title;
    QString instruction;
    QString hint;
};

QRect tutorialCardRect(const QRect &workspace, const QSize &size, const QRect &target,
                       bool preferCenter) {
    constexpr int gap = 12;
    const QRect available = workspace.adjusted(gap, gap, -gap, -gap);
    const auto at = [&](QPoint topLeft) {
        topLeft.setX(std::clamp(topLeft.x(), available.left(),
                               std::max(available.left(), available.right() - size.width() + 1)));
        topLeft.setY(std::clamp(topLeft.y(), available.top(),
                               std::max(available.top(), available.bottom() - size.height() + 1)));
        return QRect(topLeft, size);
    };
    const QRect centered = at(available.center() - QPoint(size.width() / 2, size.height() / 2));
    if (target.isEmpty() || (preferCenter && !centered.intersects(target))) return centered;

    const std::array<QPoint, 4> positions{{
        {target.right() + gap + 1, target.top()},
        {target.left(), target.bottom() + gap + 1},
        {target.left() - size.width() - gap, target.top()},
        {target.left(), target.top() - size.height() - gap}
    }};
    for (const QPoint &pos : positions) {
        const QRect candidate = at(pos);
        if (!candidate.intersects(target)) return candidate;
    }
    if (!centered.intersects(target)) return centered;

    // A large region can fill most of the view. Use the edge with the
    // smallest overlap so as much of the editing area as possible stays usable.
    QRect best = centered;
    for (const QPoint &pos : positions) {
        const QRect candidate = at(pos);
        const QRect overlap = candidate.intersected(target);
        const QRect bestOverlap = best.intersected(target);
        if (overlap.width() * overlap.height() < bestOverlap.width() * bestOverlap.height())
            best = candidate;
    }
    return best;
}
}

void MainWindow::startTutorial() {
    bool loaded = false;
    const QString resource = QStringLiteral(":/tutorial/XIV MIDI Studio Tutorial.mid");
    if (!QFile::exists(resource)) {
        QMessageBox::warning(this, tr("Tutorial"), tr("The bundled tutorial MIDI could not be found."));
        return;
    }

    auto *demo = new MidiFile(resource, &loaded);
    if (!loaded || demo->numTracks() != 2) {
        delete demo;
        QMessageBox::warning(this, tr("Tutorial"), tr("The bundled tutorial MIDI could not be opened."));
        return;
    }
    // Keep the resource immutable and route Save through the normal Save As flow.
    demo->setPath(QString());
    MidiTrack *flute = demo->track(0);
    MidiTrack *drums = demo->track(1);
    drums->setMuted(true);
    setFile(demo);
    setWindowTitle(QApplication::applicationName() + tr(" - Tutorial: Mambo De Chocobo[*]"));
    if (auto *pianoRoll = findChild<QAction *>(QStringLiteral("xivPianoRollVisibility")))
        pianoRoll->setChecked(true);

    auto *central = centralWidget();
    auto *panel = new QWidget(central);
    panel->setObjectName(QStringLiteral("xivTutorial"));
    panel->setAttribute(Qt::WA_StyledBackground);
    panel->setStyleSheet(QStringLiteral(
        "QWidget#xivTutorial { background:#222b37; border:1px solid #62788f; border-radius:8px; }"
        "QWidget#xivTutorial QLabel { color:#eef5ff; background:transparent; }"
        "QWidget#xivTutorial QLabel#xivTutorialProgress { color:#a9bfd5; }"));
    _tutorialPanel = panel;
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(16, 12, 16, 12);
    layout->setSpacing(7);
    auto *progress = new QLabel(panel);
    progress->setObjectName(QStringLiteral("xivTutorialProgress"));
    auto *heading = new QLabel(panel);
    QFont font = heading->font();
    font.setBold(true);
    heading->setFont(font);
    heading->setWordWrap(true);
    auto *instruction = new QLabel(panel);
    instruction->setWordWrap(true);
    layout->addWidget(progress);
    layout->addWidget(heading);
    layout->addWidget(instruction);
    auto *actions = new QHBoxLayout;
    auto *hint = new QPushButton(tr("Hint"), panel);
    hint->setCheckable(true);
    auto *next = new QPushButton(panel);
    auto *exit = new QPushButton(tr("Exit Tutorial"), panel);
    for (auto *button : {hint, next, exit}) button->setFocusPolicy(Qt::TabFocus);
    actions->addWidget(exit);
    actions->addStretch();
    actions->addWidget(hint);
    actions->addWidget(next);
    layout->addLayout(actions);

    auto *outline = new QWidget(central);
    outline->setAttribute(Qt::WA_TransparentForMouseEvents);
    outline->setObjectName(QStringLiteral("xivTutorialOutline"));
    outline->setStyleSheet(QStringLiteral(
        "QWidget#xivTutorialOutline { background:transparent; border:2px solid #73c5ff; border-radius:4px; }"));
    _tutorialOutline = outline;
    outline->hide();

    const std::array<Prompt, static_cast<int>(Step::Done)> prompts{{
        {tr("Welcome to the tutorial"), tr("This project starts as a duet for Flute and Bass Drum. You’ll add a Tuba part to turn it into a trio, then prepare the drum part for XIV Bard Performance."), {}},
        {tr("Find your way around"), tr("The Arrangement above holds tracks and regions. The Piano Roll below shows the notes in a region when you select it."), {}},
        {tr("Add a Tuba track"), tr("Click + Add Track. In the Add Track dialog, click the preset button beside Track Name, choose Tuba, then click Accept."),
         tr("Look under Brass or the loaded SoundFont's name. Choose the preset so the track gets the Tuba sound.")},
        {tr("Move the playhead"), tr("Click measure 5 on the Arrangement timeline ruler."),
         tr("Click the measure number below the cycle strip.")},
        {tr("Split the Flute region"), tr("Right-click the Flute region and choose Split at Playhead. If prompted, choose Split - retrigger notes in the next region, then click OK."),
         tr("The opening and the melody from measure 5 should now be separate regions. Use Undo to retry a split.")},
        {tr("Move the opening to Tuba"), tr("Drag the opening Flute region straight down onto the Tuba track. Keep its start at measure 1."),
         tr("Grab the middle of the region. Tuba is below the Bass Drum track.")},
        {tr("Loop the Tuba pattern"), tr("Drag the upper-right loop handle to measure 13 for two full repeats after the original."),
         tr("Grab the loop icon near the top-right corner. The finished region has three passes.")},
        {tr("Copy the Tuba region"), tr("Right-click the looped Tuba region and choose Copy Region."),
         tr("The whole looped passage will be available to paste.")},
        {tr("Go to measure 21"), tr("Click measure 21 on the Arrangement timeline ruler."),
         tr("The copied Tuba passage will start at the playhead.")},
        {tr("Paste onto Tuba"), tr("Right-click the Tuba lane and choose Paste Region at Playhead, or press Ctrl+V with Tuba selected."),
         tr("Check that the new Tuba region starts at measure 21.")},
        {tr("Trim the Tuba ending"), tr("Drag the right edge of the pasted region left to the start of measure 29."),
         tr("Grab the right edge below the loop handle. The Flute carries the melody beyond measure 29.")},
        {tr("Join the Tuba regions"), tr("Hold Shift and click both Tuba regions. Right-click either selected region and choose Join Selected Regions Here."),
         tr("Both regions should be highlighted before you open the menu. The silence between them stays in the joined region.")},
        {tr("Return to measure 1"), tr("Click Back to Beginning in the transport controls."),
         tr("When playback is stopped, the button immediately to the left of Play goes to the beginning.")},
        {tr("Preview the Flute and Tuba"), tr("Click Play. Listen for Flute at measure 5 and Tuba returning at measure 21. Click Continue when ready."),
         tr("The Bass Drum is muted for this preview.")},
        {tr("Open the Bass Drum region"), tr("Click the Bass Drum region in the Arrangement to show its notes in the Piano Roll."),
         tr("The Bass Drum track is between Flute and Tuba.")},
        {tr("Show the drum map"), tr("Turn on GM Drum Map in the Piano Roll header."),
         tr("The names beside the piano keys identify the drum sounds by pitch.")},
        {tr("Remove the open hi-hat"), tr("Click the Bb2 / Open Hi-Hat key to select the whole row, then press Backspace."),
         tr("Click the key on the left of the Piano Roll. The 109 notes on the other two rows should remain.")},
        {tr("Transpose up one octave"), tr("The Bass Drum part includes notes below C3. XIV Bard Performance plays C3 through C6, so those low hits are silent in-game. Select both remaining pitch rows, then right-click a selected note and choose Transpose Selected Notes > Octave Up."),
         tr("Drag from the highest remaining key to the lowest to select both rows across the entire region.")},
        {tr("Transpose up again"), tr("Right-click a selected note and choose Transpose Selected Notes > Octave Up again."),
         tr("The notes stay selected after transposing. The second octave shift brings both pitches into XIV’s playable range.")},
        {tr("Hear the Bass Drum"), tr("The Bass Drum is unmuted. Click Play to hear the full trio, then click Continue."),
         tr("The GM pitches were too low for the XIV part. Transposing the remaining notes puts them in range.")},
        {tr("Arrangement and Tempo"), tr("Global Tracks above the track headers opens the Tempo lane, where you can see changes in speed."), {}},
        {tr("Piano Roll views"), tr("All Tracks shows notes from every track together, like most versions of MidiEditor. Selected Region shows only the region you clicked, so you can focus on one instrument."), {}},
        {tr("Try the Piano Roll grid"), tr("Click 1/4, 1/8, and 1/16 to see quarter-note, eighth-note, and sixteenth-note spacing. Then switch back to Auto. Hold Ctrl and scroll the mouse wheel over the Piano Roll to zoom horizontally. In Auto, zooming in shows smaller subdivisions of the beat, and zooming out shows larger ones."), {}},
        {tr("Undo and Redo"), tr("Use these two buttons in the top bar to reverse or reapply an edit."), {}},
        {tr("Arrangement zoom"), tr("Use the zoom buttons on the right of the top bar to see more detail or more of the song."), {}},
        {tr("Mute and Solo"), tr("In the buttons at the lower right of each track header, click the speaker to mute or unmute the track. Click the headphones to solo it."), {}},
        {tr("Cycle playback"), tr("Drag across the thin cycle strip above the measure numbers to repeat a time range during playback."), {}}
    }};

    const auto showPrompt = [progress, heading, instruction, hint, prompts](Step step) {
        const int index = static_cast<int>(step);
        const auto &prompt = prompts[index];
        progress->setText(tr("Step %1 of %2").arg(index + 1).arg(prompts.size()));
        heading->setText(prompt.title);
        instruction->setText(hint->isChecked() && !prompt.hint.isEmpty() ? prompt.hint : prompt.instruction);
        hint->setVisible(!prompt.hint.isEmpty());
    };
    showPrompt(Step::Welcome);
    next->hide();

    // This state lives with the coaching panel and is discarded when the project changes.
    auto *timer = new QTimer(panel);
    timer->setInterval(75);
    auto *arrangement = _arrangementWidget;
    const auto bar = [demo](int measure) { return demo->startTickOfMeasure(measure); };
    const int splitTick = bar(5);
    // Some versions of the starter melody have a note that straddles bar 5.
    const auto *fluteEvents = demo->channelEvents(flute->assignedChannel());
    const bool crossesSplit = std::any_of(fluteEvents->cbegin(), fluteEvents->cend(),
        [flute, splitTick](MidiEvent *event) {
            auto *note = dynamic_cast<NoteOnEvent *>(event);
            return note && note->track() == flute && note->velocity() > 0 && note->offEvent() &&
                   note->midiTime() < splitTick && note->offEvent()->midiTime() > splitTick;
        });
    const int loopEnd = bar(13);
    const int pasteTick = bar(21);
    const int trimTick = bar(29);
    const int pastedEnd = bar(33);
    auto *area = findChild<QScrollArea *>(QStringLiteral("ffxivArrangementScrollArea"));

    connect(exit, &QPushButton::clicked, this, &MainWindow::finishTutorial);
    if (auto *play = _actionMap.value(QStringLiteral("play")))
        connect(play, &QAction::triggered, panel, [panel]() {
            panel->setProperty("tutorialPlayRequested", true);
        });
    connect(next, &QPushButton::clicked, panel, [next]() {
        next->setProperty("advanceTutorial", true);
    });

    connect(timer, &QTimer::timeout, panel,
        [this, panel, layout, central, outline, arrangement, area, demo, flute, drums,
         showPrompt, hint, next, splitTick, crossesSplit, loopEnd, pasteTick, trimTick, pastedEnd,
         step = Step::Welcome, shown = Step::Done, tuba = QPointer<MidiTrack>(),
         heard = false]() mutable {
        if (_tutorialPanel != panel || file != demo) return;
        if (QApplication::activeModalWidget() || QApplication::activePopupWidget()) return;

        const auto &regions = arrangement->regionModel().regions();
        const auto indexOf = [demo](MidiTrack *track) { return int(demo->tracks()->indexOf(track)); };
        const int fluteIndex = indexOf(flute);
        const int drumIndex = indexOf(drums);
        const auto programOf = [demo](MidiTrack *track) {
            const int channel = track->assignedChannel();
            return channel >= 0 && channel < 16 ? demo->channel(channel)->progAtTick(0) : -1;
        };
        if (step == Step::AddTuba) {
            tuba = nullptr;
            for (auto *track : *demo->tracks()) {
                if (track != flute && track != drums && programOf(track) == 58) {
                    tuba = track;
                    break;
                }
            }
        }
        const int tubaIndex = indexOf(tuba.data());
        const auto regionAt = [&regions](int track, int tick) -> const xiv::MidiRegion * {
            const auto found = std::find_if(regions.begin(), regions.end(), [track, tick](const auto &r) {
                return r.trackIndex == track && r.contains(tick);
            });
            return found == regions.end() ? nullptr : &*found;
        };
        const auto *opening = regionAt(fluteIndex, 0);
        const auto *second = regionAt(fluteIndex, splitTick);
        const auto *tubaStart = regionAt(tubaIndex, 0);
        const auto *pasted = regionAt(tubaIndex, pasteTick);
        const auto *drumRegion = regionAt(drumIndex, 0);
        const bool drumsSelected = drumRegion && arrangement->selectedRegionId() == drumRegion->id &&
                                  !_showAllTracksInPianoRoll;

        const bool requested = next->property("advanceTutorial").toBool();
        if (requested) next->setProperty("advanceTutorial", false);
        const bool listening = step == Step::Listen || step == Step::ListenDrums;
        if (listening && MidiPlayer::isPlaying()) heard = true;
        bool playRequested = panel->property("tutorialPlayRequested").toBool();
        const bool tour = step >= Step::Tempo;
        const auto noteCounts = [this, drumRegion]() {
            std::array<int, 128> counts{};
            for (const auto &note : mw_matrixWidget->allTracksNotes())
                if (note.regionId == drumRegion->id && note.pitch >= 0 && note.pitch < 128)
                    ++counts[note.pitch];
            return counts;
        };
        const auto matchingDrums = [&](int offset) {
            if (!drumsSelected) return false;
            const auto counts = noteCounts();
            int total = 0;
            for (int count : counts) total += count;
            return total == 109 && counts[36 + offset] == 73 && counts[56 + offset] == 36;
        };

        bool complete = false;
        switch (step) {
        case Step::Welcome: case Step::Workspace: complete = requested; break;
        case Step::Listen: case Step::ListenDrums:
            complete = requested && (heard || playRequested);
            if (complete && MidiPlayer::isPlaying()) stop();
            break;
        case Step::AddTuba: complete = tubaIndex >= 0; break;
        case Step::SeekSplit: complete = demo->cursorTick() == splitTick; break;
        case Step::Split: complete = opening && opening->endTick() == splitTick &&
            second && second->startTick == splitTick &&
            (!crossesSplit || opening->endNoteMode == xiv::CrossingNoteMode::Split); break;
        case Step::Move: complete = !opening && tubaStart && tubaStart->endTick() == splitTick; break;
        case Step::Loop: complete = tubaStart && tubaStart->loopLengthTicks == loopEnd; break;
        case Step::Copy: complete = tubaStart && arrangement->lastCopiedRegionId() == tubaStart->id; break;
        case Step::SeekPaste: complete = demo->cursorTick() == pasteTick; break;
        case Step::Paste: complete = pasted && pasted->startTick == pasteTick && pasted->endTick() == pastedEnd; break;
        case Step::Trim: complete = pasted && pasted->startTick == pasteTick && pasted->endTick() == trimTick; break;
        case Step::Join: complete = tubaStart && tubaStart == pasted && tubaStart->endTick() == trimTick; break;
        case Step::Rewind: complete = demo->cursorTick() == 0 && !MidiPlayer::isPlaying(); break;
        case Step::SelectDrums: complete = drumsSelected; break;
        case Step::DrumMap: complete = drumsSelected && _pianoRollGMDrumsAction->isChecked(); break;
        case Step::DeleteHiHat: complete = matchingDrums(0); break;
        case Step::OctaveOne: complete = matchingDrums(12); break;
        case Step::OctaveTwo: complete = matchingDrums(24); break;
        default: complete = tour && requested; break;
        }
        if (complete) {
            step = static_cast<Step>(static_cast<int>(step) + 1);
            heard = false;
            playRequested = false;
            panel->setProperty("tutorialPlayRequested", false);
        }
        if (step == Step::Done) {
            finishTutorial();
            return;
        }

        const auto showTrack = [arrangement, area](int track) {
            if (!area || track < 0) return;
            const int bottom = arrangement->rulerHeight() + (track + 1) * ArrangementWidget::laneHeight;
            auto *scroll = area->verticalScrollBar();
            const int minimum = std::max(0, bottom - area->viewport()->height());
            if (scroll->value() < minimum)
                scroll->setValue(minimum);
            else if (arrangement->rulerHeight() + track * ArrangementWidget::laneHeight <
                     scroll->value() + arrangement->rulerHeight())
                scroll->setValue(track * ArrangementWidget::laneHeight);
        };
        const auto showPitches = [this](int low, int high) {
            const int lines = high - low + 4;
            for (int i = 0; i < 32 && mw_matrixWidget->lineHeight() > 0; ++i) {
                const double before = mw_matrixWidget->lineHeight();
                if (mw_matrixWidget->height() - mw_matrixWidget->yPosOfLine(vert->value()) >= lines * before)
                    break;
                mw_matrixWidget->zoomVerOut();
                if (mw_matrixWidget->lineHeight() == before) break;
            }
            vert->setValue(127 - high - 1);
        };
        if (step != shown) {
            shown = step;
            hint->setChecked(false);
            arrangement->setTutorialSnapTick(-1);
            switch (step) {
            case Step::Welcome: showTrack(fluteIndex); break;
            case Step::SeekSplit:
                showTrack(fluteIndex);
                arrangement->revealTutorialTicks(0, splitTick);
                arrangement->setTutorialSnapTick(splitTick);
                break;
            case Step::Split: arrangement->setTutorialSnapTick(splitTick); break;
            case Step::Move: showTrack(tubaIndex); break;
            case Step::Loop:
                arrangement->revealTutorialTicks(0, loopEnd);
                arrangement->setTutorialSnapTick(loopEnd);
                break;
            case Step::SeekPaste: case Step::Paste:
                arrangement->revealTutorialTicks(pasteTick, pastedEnd);
                arrangement->setTutorialSnapTick(pasteTick);
                break;
            case Step::Trim:
                arrangement->revealTutorialTicks(trimTick, pastedEnd);
                arrangement->setTutorialSnapTick(trimTick);
                break;
            case Step::Join:
                arrangement->clearRegionSelection();
                arrangement->revealTutorialTicks(0, trimTick);
                break;
            case Step::SelectDrums:
                if (_showAllTracksInPianoRollAction->isChecked()) _showAllTracksInPianoRollAction->setChecked(false);
                arrangement->revealTutorialTicks(0, splitTick);
                showTrack(drumIndex);
                break;
            case Step::DeleteHiHat: case Step::OctaveOne: showPitches(36, 56); break;
            case Step::OctaveTwo: showPitches(48, 68); break;
            case Step::ListenDrums:
                if (drumIndex >= 0 && drums->muted()) {
                    demo->protocol()->startNewAction(tr("Unmute Bass Drum"));
                    drums->setMuted(false);
                    demo->protocol()->endAction();
                }
                backToBegin();
                break;
            case Step::Tempo:
                if (area) area->verticalScrollBar()->setValue(0);
                break;
            case Step::TrackAudio: showTrack(fluteIndex); break;
            default: break;
            }
        }

        const auto findControl = [central](const char *id) {
            return central->findChild<QWidget *>(QString::fromLatin1(id));
        };
        const auto globalRect = [](QWidget *widget, QRect bounds) {
            if (!widget || !widget->isVisible() || bounds.isEmpty()) return QRect();
            return QRect(widget->mapToGlobal(bounds.topLeft()), bounds.size());
        };
        const auto controlRect = [&findControl, &globalRect](const char *id) {
            auto *widget = findControl(id);
            return widget ? globalRect(widget, widget->rect()) : QRect();
        };
        const auto arrangeRect = [arrangement, area, &globalRect](QRect bounds) {
            QRect target = globalRect(arrangement, bounds);
            if (area) target = target.intersected(globalRect(area->viewport(), area->viewport()->rect()));
            return target;
        };
        const auto pianoKeys = [this, &globalRect](int low, int high) {
            const int top = mw_matrixWidget->yPosOfLine(127 - high);
            const int bottom = mw_matrixWidget->yPosOfLine(128 - low);
            const int clippedTop = std::max(top, mw_matrixWidget->yPosOfLine(vert->value()));
            return globalRect(mw_matrixWidget, QRect(0, clippedTop, mw_matrixWidget->pianoGutterWidth(),
                bottom - clippedTop).intersected(mw_matrixWidget->rect()));
        };
        QRect target;
        switch (step) {
        case Step::Workspace: target = controlRect("ffxivPianoHeader"); break;
        case Step::Listen: case Step::ListenDrums:
            target = controlRect("xivPlay"); break;
        case Step::AddTuba: target = controlRect("xivAddTrack"); break;
        case Step::SeekSplit: target = arrangeRect(arrangement->measureMarkerRect(splitTick)); break;
        case Step::Split: case Step::Move:
            if (opening) target = arrangeRect(arrangement->tutorialRegionRect(*opening));
            break;
        case Step::Loop:
            if (tubaStart) target = arrangeRect(arrangement->tutorialRegionEndRect(*tubaStart, true));
            break;
        case Step::Copy:
            if (tubaStart) target = arrangeRect(arrangement->tutorialRegionRect(*tubaStart));
            break;
        case Step::SeekPaste:
            target = arrangeRect(arrangement->measureMarkerRect(pasteTick)); break;
        case Step::Paste:
            if (tubaIndex >= 0) {
                const int top = arrangement->rulerHeight() + tubaIndex * ArrangementWidget::laneHeight;
                const int x = arrangement->measureMarkerRect(pasteTick).center().x();
                target = arrangeRect(QRect(x - 8, top + 6, 24, ArrangementWidget::laneHeight - 12));
            }
            break;
        case Step::Trim:
            if (pasted) target = arrangeRect(arrangement->tutorialRegionEndRect(*pasted, false));
            break;
        case Step::Join:
            if (tubaStart && pasted)
                target = arrangeRect(arrangement->tutorialRegionRect(*tubaStart)
                    .united(arrangement->tutorialRegionRect(*pasted)));
            break;
        case Step::Rewind: target = controlRect("xivStopOrBegin"); break;
        case Step::SelectDrums:
            if (drumRegion) target = arrangeRect(arrangement->tutorialRegionRect(*drumRegion));
            break;
        case Step::DrumMap: target = globalRect(_pianoRollGMDrumsButton, _pianoRollGMDrumsButton->rect()); break;
        case Step::DeleteHiHat: target = pianoKeys(46, 46); break;
        case Step::OctaveOne: target = pianoKeys(36, 56); break;
        case Step::OctaveTwo: target = pianoKeys(48, 68); break;
        case Step::Tempo:
            target = arrangeRect(arrangement->globalTracksControlRect());
            if (!arrangement->tempoTrackRect().isEmpty())
                target = target.united(arrangeRect(arrangement->tempoTrackRect()));
            break;
        case Step::PianoViews:
            target = globalRect(_pianoRollShowSelectedButton, _pianoRollShowSelectedButton->rect())
                .united(globalRect(_pianoRollShowAllButton, _pianoRollShowAllButton->rect()));
            break;
        case Step::PianoGrid: target = controlRect("xivPianoGridControls"); break;
        case Step::History: target = controlRect("xivUndo").united(controlRect("xivRedo")); break;
        case Step::Zoom: target = controlRect("xivArrangeZoomOut").united(controlRect("xivArrangeZoomIn")); break;
        case Step::TrackAudio:
            if (fluteIndex >= 0) target = arrangeRect(arrangement->muteSoloControlsRect(fluteIndex));
            break;
        case Step::Cycle: target = arrangeRect(arrangement->cycleControlRect()); break;
        default: break;
        }
        if (!target.isEmpty()) {
            target.moveTopLeft(central->mapFromGlobal(target.topLeft()));
            target = target.intersected(central->rect());
        }
        if (!target.isEmpty()) {
            outline->setGeometry(target.adjusted(-2, -2, 2, 2).intersected(central->rect()));
            outline->show();
            outline->raise();
        } else {
            outline->hide();
        }

        showPrompt(step);
        const bool nowListening = step == Step::Listen || step == Step::ListenDrums;
        const bool explanation = step <= Step::Workspace || step >= Step::Tempo;
        next->setVisible(nowListening || explanation);
        next->setEnabled(explanation || heard || playRequested);
        next->setText(step == Step::Cycle ? tr("Finish Tutorial")
                       : nowListening ? tr("Continue") : tr("Next"));

        QRect editingArea = target;
        if (step == Step::Move && tubaIndex >= 0 && opening) {
            QRect destination = arrangeRect(arrangement->tutorialRegionRect(*opening).translated(
                0, (tubaIndex - fluteIndex) * ArrangementWidget::laneHeight));
            if (!destination.isEmpty()) {
                destination.moveTopLeft(central->mapFromGlobal(destination.topLeft()));
                editingArea = editingArea.united(destination.intersected(central->rect()));
            }
        } else if (step == Step::OctaveOne || step == Step::OctaveTwo) {
            // The user selects pitches on the keys and opens the note menu
            // in the grid, so keep the card away from both.
            editingArea = QRect(_matrixWidgetContainer->mapTo(central, QPoint()),
                                _matrixWidgetContainer->size()).intersected(central->rect());
        }
        const int cardWidth = std::min(420, std::max(1, central->width() - 24));
        const QSize cardSize(cardWidth, layout->totalHeightForWidth(cardWidth));
        panel->setGeometry(tutorialCardRect(central->rect(), cardSize, editingArea, nowListening));
        panel->show();
        panel->raise();
    });
    timer->start();
}

void MainWindow::finishTutorial() {
    if (!_tutorialPanel) return;
    _arrangementWidget->setTutorialSnapTick(-1);
    _tutorialPanel->hide();
    _tutorialPanel->deleteLater();
    _tutorialPanel = nullptr;
    _tutorialOutline->hide();
    _tutorialOutline->deleteLater();
    _tutorialOutline = nullptr;
}
