#pragma once

#include <cstdint>
#include <string>

namespace drift {

// What a text clip's script can read as `ctx`. Times are seconds. `source` is the source time of the
// media under the clip, NaN when there is none. `now` is the wall clock at render, the one input that
// makes a script differ between two evaluations of the same frame.
struct TextScriptContext
{
    double clipTime = 0.0;
    double timelineTime = 0.0;
    double duration = 0.0;
    double sourceTime = -1.0;
    bool hasSource = false;
    double fps = 30.0;
    int width = 1920;
    int height = 1080;
    std::string projectName;
    std::string clipName;
    struct Now
    {
        int year = 1970, month = 1, day = 1, hour = 0, minute = 0, second = 0, weekday = 1;
    } now;
};

struct TextScriptResult
{
    std::string text;
    std::string error; // empty on success; `text` is then empty too
};

// Runs `script` in a fresh sandbox (the procedural runtime's: no files, clock, os or random) and
// turns what it returns into text. A string or number is used as is, a table of strings is joined with
// newlines, nil is empty. Equal inputs give equal text. Never throws.
TextScriptResult evaluateTextScript(const std::string &script, const TextScriptContext &context);

} // namespace drift
