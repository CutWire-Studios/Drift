#pragma once

#include <QString>

#include <functional>

namespace drift {

// Writes a low-res, decode-friendly copy of sourcePath's video to outPath for live preview:
// H.264, short side at most shortSide (never upscaled), short GOP, no B-frames, no audio.
//
// Every frame keeps its source timestamp (rebased to the stream's start, as ClipReader reads it),
// so a clip resolves to the same source time on the proxy as on the original, variable frame
// rate included, and preview can swap files without any time mapping.
//
// onProgress is called with 0..1 and returns false to cancel; on cancel or failure no file is
// left behind.
bool renderPreviewProxy(const QString &sourcePath, int shortSide, const QString &outPath,
                        QString *errorOut, const std::function<bool(double)> &onProgress);

} // namespace drift
