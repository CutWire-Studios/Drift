#include "engine/compositor/ScriptedText.h"

#include "core/project/Project.h"
#include "engine/procedural/TextScript.h"

#include <QDate>
#include <QDateTime>
#include <QTime>

namespace drift {

ScriptedText scriptedTextAt(const Project &project, const Clip &clip, TimeUs timelineUs)
{
    TextScriptContext ctx;
    ctx.clipTime = usToSeconds(qMax<TimeUs>(0, timelineUs - clip.timelineStart));
    ctx.timelineTime = usToSeconds(qMax<TimeUs>(0, timelineUs));
    ctx.duration = usToSeconds(clip.timelineDuration);
    ctx.fps = project.fps() > 0 ? project.fps() : 30;
    ctx.width = project.width();
    ctx.height = project.height();
    ctx.projectName = project.name().toStdString();
    ctx.clipName = clip.name.toStdString();
    for (const Track &track : project.tracks()) {
        for (const Clip &other : track.clips) {
            if (other.type != ClipType::Video || !other.enabled || other.id == clip.id)
                continue;
            if (timelineUs < other.timelineStart || timelineUs >= other.timelineStart + other.timelineDuration)
                continue;
            ctx.hasSource = true;
            ctx.sourceTime = usToSeconds(other.timelineToSourceUs(timelineUs));
            break;
        }
        if (ctx.hasSource)
            break;
    }
    const QDateTime now = QDateTime::currentDateTime();
    ctx.now = {now.date().year(), now.date().month(), now.date().day(), now.time().hour(),
               now.time().minute(), now.time().second(), now.date().dayOfWeek()};

    const TextScriptResult result = evaluateTextScript(clip.textScript.toStdString(), ctx);
    return {QString::fromStdString(result.text), QString::fromStdString(result.error)};
}

} // namespace drift
