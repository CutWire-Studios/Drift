#pragma once

#include "core/Project.h"

#include <QSize>
#include <QString>
#include <QVariantMap>

class PlaybackStats;

// Why the preview is not smooth, answered with measurements instead of guesses.
//
// "It stutters" has at least four unrelated causes in this pipeline — a cadence that beats
// against the display, a frame rate that doubles decode cost, a per-frame trip through system
// memory, and decode landing on the wrong GPU of a hybrid pair. They need completely different
// fixes and the symptom looks identical, so a bug report that only says "it stutters" cannot
// be acted on. This produces something that can.
//
// Shaped like DebugReport: a QVariantMap of rows plus a plain-text rendering for the
// clipboard, so the dialog and the report format stay consistent with the one users already
// know how to send.
namespace PlaybackDiagnostics {

// Environment facts, the live counters, and hints drawn from both. Cheap — no decoding — so
// it is safe to call whenever the dialog opens.
QVariantMap collect(const PlaybackStats &stats, const drift::Project *project,
                    double refreshRate);

// Times one clip through the preview stages so the expensive one can be named. Blocking and
// measured in seconds, not milliseconds: run it off the GUI thread.
//
// `path` empty measures the bundled 1080p60 clip, whose numbers are comparable between
// machines; pass a timeline clip to measure the reporter's own codec and resolution instead.
QVariantMap benchmarkClip(const QString &path, const QSize &canvas);

// The bundled clip, extracted to a temporary file because FFmpeg cannot open a qrc path.
// Empty if it could not be written.
QString bundledBenchmarkClip();

QString formatPlainText(const QVariantMap &info);

} // namespace PlaybackDiagnostics
