/*
 * FFXIV MIDI Studio - SoundFont preset browser.
 * Distributed under the MidiEditor GPL-3.0-or-later license.
 */
#include "SoundFontPresetPicker.h"

#ifdef FLUIDSYNTH_SUPPORT

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

SoundFontPresetPicker::SoundFontPresetPicker(
    const QString &trackName,
    const QVector<FluidSynthEngine::PresetInfo> &presets,
    QWidget *parent)
    : QDialog(parent), _presets(presets) {
    setWindowTitle(tr("Choose SoundFont preset — %1").arg(trackName));
    setMinimumSize(760, 500);

    auto *layout = new QVBoxLayout(this);
    auto *help = new QLabel(tr(
        "Select any preset from an enabled, loaded SoundFont. "
        "This changes the built-in FluidSynth sound for the track's MIDI channel "
        "and automatically renames the selected track to the preset name."), this);
    help->setWordWrap(true);
    layout->addWidget(help);

    _search = new QLineEdit(this);
    _search->setPlaceholderText(tr("Search by preset name, SoundFont, bank, or program…"));
    _search->setClearButtonEnabled(true);
    layout->addWidget(_search);

    _list = new QTreeWidget(this);
    _list->setHeaderLabels({tr("Preset"), tr("SoundFont"), tr("Bank"), tr("Program")});
    _list->setRootIsDecorated(false);
    _list->setSelectionMode(QAbstractItemView::SingleSelection);
    _list->setAlternatingRowColors(true);
    _list->setSortingEnabled(false);
    _list->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    _list->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    _list->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    _list->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);

    for (int i = 0; i < _presets.size(); ++i) {
        const auto &p = _presets.at(i);
        auto *item = new QTreeWidgetItem(_list);
        item->setText(0, p.name);
        item->setText(1, QFileInfo(p.soundFontPath).fileName());
        item->setText(2, QString::number(p.bank));
        item->setText(3, QString::number(p.program));
        item->setData(0, Qt::UserRole, i);
        item->setToolTip(1, p.soundFontPath);
    }
    layout->addWidget(_list, 1);

    _writeMidi = new QCheckBox(tr("Also write bank/program changes into the MIDI track"), this);
    _writeMidi->setChecked(false); // Safe for FFXIV MIDI arranging; audition first.
    _writeMidi->setToolTip(tr(
        "A .mid file can store bank/program numbers, but not which SoundFont supplied them. "
        "Two SoundFonts can contain the same bank and program."));
    layout->addWidget(_writeMidi);

    auto *notice = new QLabel(tr(
        "Preview sound selection lasts until you open another MIDI file or close MidiEditor. "
        "For persistent portable MIDI settings, enable the bank/program checkbox. "
        "The selected track is renamed to the preset name even in preview-only mode; "
        "save the MIDI file to keep the new name. "
        "Separate tracks on the same MIDI channel cannot have independent instrument sounds."), this);
    notice->setWordWrap(true);
    layout->addWidget(notice);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Use Preset"));
    buttons->button(QDialogButtonBox::Ok)->setEnabled(!_presets.isEmpty());
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(_list, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem *, int) { accept(); });
    connect(_list, &QTreeWidget::itemSelectionChanged, this, [this, buttons]() {
        buttons->button(QDialogButtonBox::Ok)->setEnabled(_list->currentItem() != nullptr);
    });
    connect(_search, &QLineEdit::textChanged, this, [this](const QString &text) {
        for (int i = 0; i < _list->topLevelItemCount(); ++i) {
            auto *item = _list->topLevelItem(i);
            const QString searchable = item->text(0) + " " + item->text(1) + " " +
                                       item->text(2) + " " + item->text(3);
            item->setHidden(!searchable.contains(text.trimmed(), Qt::CaseInsensitive));
        }
        if (_list->currentItem() && _list->currentItem()->isHidden()) {
            _list->setCurrentItem(nullptr);
        }
    });
    if (_list->topLevelItemCount()) _list->setCurrentItem(_list->topLevelItem(0));
    _search->setFocus();
}

FluidSynthEngine::PresetInfo SoundFontPresetPicker::selectedPreset() const {
    if (auto *selected = _list->currentItem()) {
        const int index = selected->data(0, Qt::UserRole).toInt();
        if (index >= 0 && index < _presets.size()) return _presets.at(index);
    }
    return {};
}

bool SoundFontPresetPicker::writeMidiAssignment() const {
    return _writeMidi->isChecked();
}

#endif // FLUIDSYNTH_SUPPORT
