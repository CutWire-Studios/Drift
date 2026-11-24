#pragma once

#include "core/base/Time.h"

#include <QString>

namespace drift {

struct Clip;
class Project;

struct ScriptedText
{
    QString text;
    QString error; // empty on success
};

// What a text clip's script (Clip::textScript) says at `timelineUs`. The context is the clip's time,
// the timeline's, the project's frame rate and size and name, the source time of the video under the
// clip, and the wall clock now. A failing script gives empty text and its error, never an exception.
ScriptedText scriptedTextAt(const Project &project, const Clip &clip, TimeUs timelineUs);

} // namespace drift
