#pragma once

#include <QList>
#include <QString>

namespace drift {

// Ready-made Lua scripts for a text clip (Clip::textScript). The script reads `ctx` (time, timeline,
// duration, progress, frame, fps, source, now, ...) and the helpers timecode, lerp, ease, pad and
// commas, and returns the text for that frame.
struct TextScriptPreset
{
    QString id;
    QString label;
    QString script;
};

const QList<TextScriptPreset> &textScriptPresets();
const TextScriptPreset *textScriptPreset(const QString &id);

} // namespace drift
