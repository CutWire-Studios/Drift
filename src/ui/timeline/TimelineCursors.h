#pragma once

#include <QCursor>
#include <QString>

enum class TimelineCursorKind {
    Move,
    MoveActive,
    Blade,
    TrimStartTool,
    TrimEndTool,
    TrimIn,
    TrimOut,
    FadeIn,
    FadeOut,
    TransitionResize,
    TransitionMove,
    PlayheadScrub,
    TrackReorder,
    TrackResizeH,
    TrackResizeV,
    Pan,
};

// QML-facing names: "move", "moveActive", "blade", "trimStartTool", "trimEndTool", "trimIn",
// "trimOut", "fadeIn", "fadeOut", "transitionResize", "transitionMove", "playheadScrub",
// "trackReorder", "trackResizeH", "trackResizeV", "pan".
bool timelineCursorKindFromName(const QString &name, TimelineCursorKind *kind);

// heightPx only matters for TrimIn/TrimOut, which span the clip. Cached per (kind, height, dpr).
QCursor timelineCursor(TimelineCursorKind kind, int heightPx, qreal dpr);

// Cached QCursors hold platform resources; drop them while QGuiApplication is still alive.
void clearTimelineCursorCache();
