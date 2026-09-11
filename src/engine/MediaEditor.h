#pragma once

#include <QString>

#include <functional>

namespace drift {

// Crop is in display space after rotation, each axis 0..1. A full-frame crop (0,0,1,1) leaves
// the picture uncropped. `outSeconds` < 0 means "through the end of the source".
struct MediaEditSpec
{
    QString inputPath;
    QString outputPath;
    QString kind; // "video" | "audio" | "image"
    double inSeconds = 0;
    double outSeconds = -1;
    double cropX = 0;
    double cropY = 0;
    double cropW = 1;
    double cropH = 1;
    // -1 = derive rotation from the source file's own display-matrix tag, as before. Set this to
    // a bin-preview rotation override (0/90/180/270) so the crop rectangle — drawn in the QML
    // preview against that corrected orientation — lands on the same pixels here.
    int rotationOverride = -1;
};

// Rewrites `inputPath` into `outputPath` with the requested trim and crop. Images become PNG,
// audio becomes FLAC, video becomes H.264 MP4 (AAC audio kept when the source has it).
//
// onProgress is called with 0..1 and returns false to cancel. Cancel and failure leave no
// finished file behind — a `.part` is removed.
bool editMedia(const MediaEditSpec &spec, QString *errorOut,
               const std::function<bool(double)> &onProgress);

// Project-owned media, same directory freeze frames use, so a save bundles the result
// and a cache sweep cannot delete it. Extension follows kind (png / flac / mp4).
QString newEditedMediaPath(const QString &projectId, const QString &kind);

} // namespace drift
