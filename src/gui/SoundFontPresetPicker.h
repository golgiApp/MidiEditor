/*
 * FFXIV MIDI Studio - SoundFont preset browser.
 * Distributed under the MidiEditor GPL-3.0-or-later license.
 */
#ifndef FFXIV_SOUNDFONT_PRESET_PICKER_H
#define FFXIV_SOUNDFONT_PRESET_PICKER_H

#ifdef FLUIDSYNTH_SUPPORT

#include <QDialog>
#include <QVector>
#include "../midi/FluidSynthEngine.h"

class QLineEdit;
class QTreeWidget;
class QCheckBox;

class SoundFontPresetPicker final : public QDialog {
public:
    explicit SoundFontPresetPicker(const QString &trackName,
                                   const QVector<FluidSynthEngine::PresetInfo> &presets,
                                   QWidget *parent = nullptr);
    FluidSynthEngine::PresetInfo selectedPreset() const;
    bool writeMidiAssignment() const;

private:
    QVector<FluidSynthEngine::PresetInfo> _presets;
    QLineEdit *_search = nullptr;
    QTreeWidget *_list = nullptr;
    QCheckBox *_writeMidi = nullptr;
};

#endif // FLUIDSYNTH_SUPPORT
#endif // FFXIV_SOUNDFONT_PRESET_PICKER_H
