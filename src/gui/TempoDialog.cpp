#include "TempoDialog.h"
#include "Appearance.h"
#include "../midi/TempoMapEdit.h"

#include <QGridLayout>
#include <QLabel>
#include <QIcon>
#include <QPushButton>
#include <QMap>

#include "../MidiEvent/MidiEvent.h"
#include "../MidiEvent/TempoChangeEvent.h"
#include "../protocol/Protocol.h"
#include "../midi/MidiChannel.h"

TempoDialog::TempoDialog(MidiFile *file, int startTick, int endTick, QWidget *parent) : QDialog(parent) {
    _startTick = startTick;
    _endTick = endTick;
    _file = file;

    setMinimumWidth(550);
    setMaximumHeight(450);
    setWindowTitle(tr("Edit Tempo"));
    // Note: setWindowIcon doesn't use QAction, so we keep the direct approach
    setWindowIcon(Appearance::adjustIconForDarkMode(":/run_environment/graphics/icon.png"));
    QGridLayout *layout = new QGridLayout(this);

    QLabel *icon = new QLabel();
    QPixmap iconPixmap = Appearance::adjustIconForDarkMode(QPixmap(":/run_environment/graphics/midieditor.png"), "midieditor");
    icon->setPixmap(iconPixmap.scaledToWidth(80, Qt::SmoothTransformation));
    icon->setFixedSize(80, 80);
    layout->addWidget(icon, 0, 0, 3, 1);

    QLabel *title = new QLabel("<h3>Edit Tempo</h3>", this);
    layout->addWidget(title, 0, 1, 1, 2);
    if (Appearance::shouldUseDarkMode()) {
        title->setStyleSheet("color: white");
    } else {
        title->setStyleSheet("color: black");
    }

    // Use smooth transition
    _smoothTransition = new QCheckBox("Smooth Transition");
    layout->addWidget(_smoothTransition, 1, 1, 1, 2);

    QLabel *tip = new QLabel("<html>"
        "<body>"
        "<p>"
        "<b>Tip</b>: Select time range to enter smooth transitions"
        "</p>"
        "</body>"
        "</html>");
    layout->addWidget(tip, 2, 1, 1, 2);
    if (Appearance::shouldUseDarkMode()) {
        tip->setStyleSheet("color: white; background-color: #404040; padding: 5px");
    } else {
        tip->setStyleSheet("color: black; background-color: white; padding: 5px");
    }

    // identify tempo at start tick
    QMap<int, MidiEvent *> *events = file->tempoEvents();
    QMap<int, MidiEvent *>::iterator it = events->begin();
    int tick = -1;
    MidiEvent *ev = 0;
    while (it != events->end()) {
        if (it.key() <= _startTick && (tick < 0 || tick < it.key())) {
            tick = it.key();
            ev = it.value();
        }
        it++;
    }

    int beats = 120;
    if (ev != 0 && dynamic_cast<TempoChangeEvent *>(ev) != 0) {
        beats = dynamic_cast<TempoChangeEvent *>(ev)->beatsPerQuarter();
    }
    // Beats
    QLabel *beatsLabel = new QLabel("Beats (Quarters) per Minute:");
    layout->addWidget(beatsLabel, 3, 1, 1, 2);
    QLabel *beginLabel = new QLabel("Starting From:");
    layout->addWidget(beginLabel, 4, 1, 1, 1);
    _startBeats = new QSpinBox();
    _startBeats->setMinimum(4);
    _startBeats->setMaximum(999);
    _startBeats->setValue(beats);
    layout->addWidget(_startBeats, 4, 2, 1, 1);

    QLabel *endLabel = new QLabel("New Tempo:");
    layout->addWidget(endLabel, 5, 1, 1, 1);
    _endBeats = new QSpinBox();
    _endBeats->setMinimum(4);
    _endBeats->setMaximum(999);
    _endBeats->setValue(beats);
    layout->addWidget(_endBeats, 5, 2, 1, 1);


    if (_endTick > -1) {
        _smoothTransition->setCheckable(true);
        _smoothTransition->setChecked(true);
        connect(_smoothTransition, SIGNAL(toggled(bool)),
                _startBeats, SLOT(setEnabled(bool)));
        connect(_smoothTransition, SIGNAL(toggled(bool)),
                beginLabel, SLOT(setEnabled(bool)));
    } else {
        _smoothTransition->setChecked(false);
        _smoothTransition->setCheckable(false);
        _smoothTransition->setEnabled(false);
        _startBeats->setEnabled(false);
        beginLabel->setEnabled(false);
    }

    QFrame *f = new QFrame(this);
    f->setFrameStyle(QFrame::HLine | QFrame::Sunken);
    layout->addWidget(f, 9, 0, 1, 3);

    // Accept / Break button
    QPushButton *breakButton = new QPushButton("Cancel");
    connect(breakButton, SIGNAL(clicked()), this, SLOT(hide()));
    QPushButton *acceptButton = new QPushButton("Accept");
    connect(acceptButton, SIGNAL(clicked()), this, SLOT(accept()));

    layout->addWidget(acceptButton, 10, 1, 1, 1);
    layout->addWidget(breakButton, 10, 2, 1, 1);
}

void TempoDialog::accept() {
    auto points = xiv::readTempoMap(_file);
    const int end = _smoothTransition->isChecked() ? _endTick : _startTick;
    if (end < _startTick) return;
    auto point = points.lowerBound(_startTick);
    if (point != points.begin()) std::prev(point).value().rampToNext = false;
    while (point != points.end() && point.key() <= end) point = points.erase(point);
    xiv::TempoPoint tempo;
    tempo.setBpm(_startBeats->value());
    if (end > _startTick) {
        tempo.rampToNext = true;
        points.insert(_startTick, tempo);
    }
    tempo.rampToNext = false;
    tempo.setBpm(_endBeats->value());
    points.insert(end, tempo);
    xiv::writeTempoMap(_file, points, tr("Edit Tempo"));
    QDialog::accept();
}
