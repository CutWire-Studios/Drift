#include "core/text/TextScriptPresets.h"

#include <QCoreApplication>

namespace drift {

namespace {

TextScriptPreset make(const char *id, const char *label, const char *script)
{
    return {QString::fromLatin1(id), QCoreApplication::translate("TextScriptPresets", label), QString::fromUtf8(script)};
}

QList<TextScriptPreset> build()
{
    return {
        make("timecode", QT_TRANSLATE_NOOP("TextScriptPresets", "Timecode burn-in (timeline)"),
             "-- HH:MM:SS:FF on the timeline\nreturn timecode(ctx.timeline, ctx.fps)\n"),
        make("source-timecode", QT_TRANSLATE_NOOP("TextScriptPresets", "Timecode burn-in (source)"),
             "-- The source time of the video under this clip; the timeline time when there is none.\n"
             "return timecode(ctx.source or ctx.timeline, ctx.fps)\n"),
        make("frame-counter", QT_TRANSLATE_NOOP("TextScriptPresets", "Frame counter"),
             "-- Frames since the start of this clip\nreturn string.format(\"%d\", ctx.frame)\n"),
        make("ticker", QT_TRANSLATE_NOOP("TextScriptPresets", "Number ticker"),
             "-- Counts from `from` to `to` over the clip, with easing.\n"
             "local from, to = 0, 12500\n"
             "local easing = \"outCubic\" -- linear, inQuad, outQuad, inOutQuad, inCubic, outCubic, inOutCubic, outExpo, inOutSine\n"
             "return commas(lerp(from, to, ease(easing, ctx.progress)))\n"),
        make("stopwatch", QT_TRANSLATE_NOOP("TextScriptPresets", "Stopwatch"),
             "local t = ctx.time\n"
             "return string.format(\"%02d:%02d.%02d\", t // 60, t % 60 // 1, t * 100 % 100 // 1)\n"),
        make("clock", QT_TRANSLATE_NOOP("TextScriptPresets", "Date and clock (at render)"),
             "-- The wall clock when the frame is drawn, so a render is stamped with the time it ran.\n"
             "local n = ctx.now\n"
             "return string.format(\"%04d-%02d-%02d  %02d:%02d:%02d\", n.year, n.month, n.day, n.hour, n.min, n.sec)\n"),
        make("loader", QT_TRANSLATE_NOOP("TextScriptPresets", "Percentage loader"),
             "local pct = math.floor(ctx.progress * 100 + 0.5)\n"
             "local filled = pct // 10\n"
             "return string.rep(\"#\", filled) .. string.rep(\"-\", 10 - filled) .. \"  \" .. pct .. \"%\"\n"),
    };
}

} // namespace

const QList<TextScriptPreset> &textScriptPresets()
{
    static const QList<TextScriptPreset> presets = build();
    return presets;
}

const TextScriptPreset *textScriptPreset(const QString &id)
{
    for (const TextScriptPreset &p : textScriptPresets()) {
        if (p.id == id)
            return &p;
    }
    return nullptr;
}

} // namespace drift
