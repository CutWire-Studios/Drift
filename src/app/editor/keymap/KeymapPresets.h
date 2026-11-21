#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

namespace drift::keymap {

struct Preset {
    QString id;
    const char *name;
    // Complete for what the preset binds. An id listed with an empty list is deliberately unbound.
    QHash<QString, QStringList> chords;
};

inline constexpr char kDefaultPresetId[] = "drift";

// Presets in picker order.
const QList<Preset> &presets();
const Preset *findPreset(const QString &id);
QString translatedName(const Preset &preset);

// What `preset` binds to `actionId`. An action the preset does not list is unbound, except a
// panel-scoped action, which takes the Drift binding: panel keys have no counterpart in other
// editors' tables, and leaving them unbound would silently disable the panels.
QStringList presetChords(const Preset &preset, const QString &actionId, const QString &scope);

// Chords use Qt's Mac-neutral names, so a preset's Meta (physical Control on a Mac) has no
// counterpart elsewhere. Off macOS it becomes Ctrl+Alt; the caller drops the result if that
// collides with a chord that was already there.
QString translateForPlatform(const QString &chord, bool mac);

// Splits "Ctrl+Shift+Left" into modifiers and key. The key may itself be "+" ("Ctrl++", "+"),
// so a plain split on '+' is wrong. Modifier names are Meta, Ctrl, Alt, Shift and Num.
void splitChord(const QString &chord, QStringList *modifiers, QString *key);

} // namespace drift::keymap
