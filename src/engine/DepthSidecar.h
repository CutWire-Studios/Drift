#pragma once

#include "core/Time.h"

#include <QSize>
#include <QString>

#include <memory>
#include <vector>

namespace drift {

// One frame of estimated depth, ready to upload: 0 is the farthest thing in the clip and 65535 the
// nearest. Normalised over the whole clip rather than per frame, so a subject walking towards the
// camera gets nearer in the data too, and a focus distance or light position set on one frame
// still means the same place on the next.
struct DepthFrame
{
    QSize size;
    std::vector<quint16> values; // row-major, size.width() * size.height()
    // Unique per (sidecar file, frame): lets texture caches tell frames apart without hashing.
    quint64 key = 0;
};

// Writes a depth sidecar (.driftdepth): per-frame relative inverse depth at inference resolution,
// losslessly compressed, with an index that makes any frame one seek away.
//
// Not a video. The obvious lossless choice, FFV1, is what ClipReader cannot seek (see
// MatteWriter.h), and an 8-bit codec would band as soon as depth is differentiated into normals.
// Each frame is quantised to 16 bits over its own range, row-delta coded, split into high and low
// byte planes and zstd compressed; the file ends with the index and the clip-wide range the
// reader normalises to.
class DepthSidecarWriter
{
public:
    DepthSidecarWriter();
    ~DepthSidecarWriter();

    // `model` identifies what produced the depth, for diagnostics only.
    bool open(const QString &path, const QSize &size, const QString &model, QString *errorOut);

    // Frames in source-time order. `disparity` holds size.width() * size.height() values, larger
    // meaning nearer; negative values are clamped to 0.
    bool writeFrame(TimeUs ptsUs, const float *disparity, QString *errorOut);

    // Writes the index and moves the temp file into place. Without this the file stays a ".part".
    bool finish(QString *errorOut);

    // Closes and removes the partial file. Safe to call after finish().
    void abort();

    DepthSidecarWriter(const DepthSidecarWriter &) = delete;
    DepthSidecarWriter &operator=(const DepthSidecarWriter &) = delete;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

// Read side. Thread-safe: the compositor reads it from its worker while the UI samples it.
class DepthSidecar
{
public:
    static std::shared_ptr<const DepthSidecar> open(const QString &path, QString *errorOut = nullptr);
    ~DepthSidecar();

    QSize size() const;
    int frameCount() const;
    TimeUs ptsAt(int index) const;

    // The frame whose timestamp is nearest `sourceUs`. Null only if decoding fails.
    std::shared_ptr<const DepthFrame> frameAt(TimeUs sourceUs) const;

    // Normalised depth (0 far, 1 near) at a point given in 0..1 frame coordinates.
    double sample(TimeUs sourceUs, double nx, double ny) const;

    DepthSidecar(const DepthSidecar &) = delete;
    DepthSidecar &operator=(const DepthSidecar &) = delete;

private:
    DepthSidecar();
    struct Impl;
    std::unique_ptr<Impl> d;
};

// Opens through a process-wide cache keyed by path, size and modification time. Null when the file
// is missing or unreadable, which callers treat as "no depth": effects pass through.
std::shared_ptr<const DepthSidecar> loadDepthSidecarCached(const QString &path);

// <CacheLocation>/depth, created on demand. Sidecars there are named by the digest of what
// produced them, so estimating the same clip twice finds the first result.
QString depthCacheDir();

} // namespace drift
