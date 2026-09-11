#pragma once

#include "core/Time.h"

#include <QImage>
#include <QSize>
#include <QString>

#include <memory>

namespace drift {

// Writes a matte sidecar video: one frame per source frame, consumed later as an ordinary video by
// ClipReaderPool.
//
// H.264 in MP4. FFV1 in MKV is the more natural choice for a lossless matte and was tried first,
// but ClipReader cannot seek it reliably — past roughly the seventh frame it either repeats a frame
// or returns nothing, and an FFV1 file produced by ffmpeg itself behaves the same way. Do not
// "restore" FFV1 here without fixing that first.
//
// Two modes, because the two sidecars a cutout produces want different things:
//
//   Coverage  the alpha map, in the luma plane with flat chroma, lossless at qp=0 so the mask edge
//             survives intact. Full-range: see the note on open().
//   Colour    RVM's decontaminated foreground, ordinary picture content at crf 16. Lossless here
//             would cost hundreds of megabytes a clip for nothing anyone can see.
class MatteWriter
{
public:
    MatteWriter();
    ~MatteWriter();

    enum class Mode {
        Coverage, // Grayscale8 alpha into luma, lossless
        Colour,   // RGB888 picture, visually lossless
    };

    // fps must match the source clip's frame rate: the matte is indexed by source time, so a
    // mismatch would slide the mask off the subject.
    bool open(const QString &path, const QSize &size, int fpsNum, int fpsDen, QString *errorOut,
              Mode mode = Mode::Coverage);

    // Frames must be appended in order. Input is converted to the mode's format if it is not
    // already in it.
    bool writeFrame(const QImage &image, QString *errorOut);

    // Flushes and moves the temp file into place. Without this the file stays a ".part".
    bool finish(QString *errorOut);

    // Closes and removes the partial file. Safe to call after finish().
    void abort();

    MatteWriter(const MatteWriter &) = delete;
    MatteWriter &operator=(const MatteWriter &) = delete;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

// <AppDataLocation>/mattes, created on demand.
QString matteCacheDir();

// A fresh, unused absolute path inside matteCacheDir().
QString newMattePath();

} // namespace drift
