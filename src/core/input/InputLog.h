#pragma once

#include "core/base/Time.h"

#include <QJsonObject>
#include <QList>
#include <QPointF>
#include <QString>

namespace drift {

// What the mouse and keyboard did while the screen was being recorded, stored beside the video.
//
// Recorded as data rather than drawn into the frames, for the same reason a face track is: once it
// is on the timeline the user wants to move the click rings, retime them, drop the ones over a
// password field, or zoom to a click they did not think to zoom to while recording. Burnt-in
// decoration cannot be any of that.
enum class InputEventKind { Press, Release, Key };

QString inputEventKindToString(InputEventKind kind);
InputEventKind inputEventKindFromString(const QString &text);

struct InputEvent
{
    InputEventKind kind = InputEventKind::Press;
    // Time from the start of the recording, matching the video's own timestamps so the two line
    // up without knowing which clock either came from.
    TimeUs timeUs = 0;
    // Normalized to the recorded frame (0..1, top-left origin), so the log survives the clip being
    // scaled, cropped or reframed. Meaningless for Key events.
    QPointF position;
    // Press/Release: 1 left, 2 right, 3 middle. Key: nothing.
    int button = 0;
    // Key only: what to show, already in the user's own layout — "Ctrl+S", "Esc", "a". Never a
    // keycode: the overlay draws this verbatim and a scancode would mean nothing to a viewer.
    QString label;
};

// One recording's events, in time order.
struct InputLog
{
    QList<InputEvent> events;

    bool isEmpty() const { return events.isEmpty(); }
    // Only the presses, which is what a zoom follows: a release is where the mouse stopped being
    // held, not where attention went.
    QList<InputEvent> presses() const;
};

QJsonObject inputLogToJson(const InputLog &log);
InputLog inputLogFromJson(const QJsonObject &object);

// Writes via a ".part" file and renames, so a cancelled or crashed recording never leaves a
// sidecar that looks complete. Mirrors writeFaceTrack.
bool writeInputLog(const QString &path, const InputLog &log, QString *errorOut);
bool readInputLog(const QString &path, InputLog *out, QString *errorOut);

// The sidecar that belongs to a recording: the video's path with ".input.json" in place of its
// suffix. Derived rather than stored so a recording and its log cannot drift apart.
QString inputLogPathFor(const QString &videoPath);

} // namespace drift
