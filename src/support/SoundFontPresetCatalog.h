/* XIV MIDI Studio - SoundFont 2 preset catalog, GPL-3.0-or-later.
 * Reads the standard RIFF pdta/phdr records, including their real program and bank.
 * Does not invent missing GM presets. Ignores required terminal EOP record.
 */
#ifndef XIV_SOUNDFONT_PRESET_CATALOG_H
#define XIV_SOUNDFONT_PRESET_CATALOG_H
#include <QFile>
#include <QByteArray>
#include <QVector>
#include <QString>
#include <QtEndian>
struct XIVSoundFontPreset { QString name; int program; int bank; };
inline QString xivSoundFontPresetDisplayName(const QString &name, int program, int bank) {
    if (bank != 0) return name;
    // The full in-game labels exceed the SoundFont's 20-byte name field.
    // Match the known names and program so unrelated SoundFonts keep their labels.
    struct GuitarPreset { const char *legacy; const char *compact; const char *variant; };
    static constexpr GuitarPreset guitars[] = {
        {"Clean Guitar", "E.Guitar: Clean", "Clean"},
        {"Muted Guitar", "E.Guitar: Muted", "Muted"},
        {"Overdriven Guitar", "E.Guitar: Overdrive", "Overdriven"},
        {"Power Chords Guitar", "E.Guitar: Power Chd", "Power Chords"},
        {"Special Guitar", "E.Guitar: Special", "Special"},
    };
    if (program >= 27 && program <= 31) {
        const auto &preset = guitars[program - 27];
        if (name == QLatin1String(preset.legacy) || name == QLatin1String(preset.compact))
            return QStringLiteral("Electric Guitar: %1").arg(QLatin1String(preset.variant));
    }
    return name;
}

inline QVector<XIVSoundFontPreset> xivReadSoundFontPresets(const QString &path) {
    QVector<XIVSoundFontPreset> result;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return result;
    const QByteArray data = f.readAll();
    if (data.size() < 12 || data.left(4) != "RIFF" || data.mid(8, 4) != "sfbk") return result;
    auto u32 = [&](qsizetype i) { return qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(data.constData() + i)); };
    auto u16 = [&](qsizetype i) { return qFromLittleEndian<quint16>(reinterpret_cast<const uchar *>(data.constData() + i)); };
    // A well-formed SoundFont must carry a pdta LIST with a phdr subchunk.
    qsizetype index = 12;
    while (index + 12 <= data.size()) {
        const quint32 len = u32(index + 4);
        const qsizetype next = index + 8 + len + (len & 1u);
        if (next > data.size() || next <= index) break;
        if (data.mid(index, 4) == "LIST" && data.mid(index + 8, 4) == "pdta") {
            qsizetype sub = index + 12;
            const qsizetype end = index + 8 + len;
            while (sub + 8 <= end) {
                const quint32 bytes = u32(sub + 4);
                const qsizetype begin = sub + 8;
                if (begin + bytes > end || begin + bytes < begin) break;
                if (data.mid(sub, 4) == "phdr" && bytes % 38u == 0) {
                    const qsizetype count = bytes / 38u;
                    for (qsizetype i = 0; i + 1 < count; ++i) {
                        const qsizetype at = begin + i * 38;
                        const QByteArray raw = data.mid(at, 20);
                        const QString name = QString::fromLatin1(raw.constData(), raw.indexOf('\0') < 0 ? 20 : raw.indexOf('\0')).trimmed();
                        const int program = u16(at + 20), bank = u16(at + 22);
                        if (!name.isEmpty() && program <= 127 && bank <= 16383)
                            result.append({xivSoundFontPresetDisplayName(name, program, bank),
                                           program, bank});
                    }
                    return result;
                }
                sub = begin + bytes + (bytes & 1u);
            }
        }
        index = next;
    }
    return result;
}
#endif
