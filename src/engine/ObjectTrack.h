#pragma once

#include "core/Clip.h"
#include "core/Time.h"
#include "core/Track.h"

#include <QImage>
#include <QList>
#include <QString>

#include <memory>

namespace drift {

// One baked frame of a patch track. x/y are the patch centre in source-frame units (0..1),
// radius is a fraction of the frame width — the circle the user placed, not a size the
// tracker estimated. `valid` is false on frames where the patch could not be found; x/y
// then hold the last place it was seen so a gap does not fling the camera.
struct ObjectSample
{
    float x = 0.5f;
    float y = 0.5f;
    float radius = 0.12f;
    float confidence = 0.f;
    bool valid = false;
};

// A clip's object track, baked once and replayed. Indexed the same way as a face track:
// `sample`'s argument is source time minus Clip::objectTrackSrcOffsetUs.
struct ObjectTrack
{
    int fps = 0;
    TimeUs startSrcUs = 0;
    QList<ObjectSample> frames;

    bool isEmpty() const { return frames.isEmpty(); }

    // Interpolates between valid neighbours. A gap holds the last valid sample rather than
    // reporting nothing, so a sticker pinned to the track does not snap back to its rest
    // position for a frame or two where the match dropped out.
    ObjectSample sample(TimeUs relativeUs) const;
};

// A short average over a contiguous run of valid frames. Tracking a flat patch jitters by a
// pixel even when the object is still, and a zoom locked to that point visibly boils.
void smoothObjectTrack(ObjectTrack *track, int radius = 2);

// Follows one patch through `frames`, starting at `seedIndex` and walking both directions so
// the circle can be placed on any frame of the clip, not only the first. Coordinates are
// normalised to the frame; `radius` is a fraction of width.
QList<ObjectSample> trackObjectFrames(const QList<QImage> &frames, int seedIndex, double x,
                                      double y, double radius);

// The same tracker, one decoded frame at a time. The scan job uses this so a long clip is
// not held in memory as images. `resetToSeed()` arms a second walk in the other direction
// without inheriting the drift of the first.
class ObjectPatchTracker
{
public:
    ObjectPatchTracker();
    ~ObjectPatchTracker();

    ObjectSample start(const QImage &frame, double x, double y, double radius);
    ObjectSample next(const QImage &frame);
    void resetToSeed();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

QString objectTrackCacheDir();
QString newObjectTrackPath();

bool writeObjectTrack(const QString &path, const ObjectTrack &track, QString *errorOut);
bool readObjectTrack(const QString &path, ObjectTrack *out, QString *errorOut);

// Parsed once and kept. The compositor samples this every frame, so the steady state must
// not touch the disk. Null when the file is missing or malformed. Thread-safe.
std::shared_ptr<const ObjectTrack> loadObjectTrackCached(const QString &path);

// The circle has moved, or was placed on a different frame, since the sidecar was baked.
bool objectTrackIsStale(const Clip &clip);
// Zoom, hold point, or the sidecar itself no longer match the keyframes last written.
bool objectLockIsStale(const Clip &clip);

// Where a zoomed clip must sit so source point (cx, cy) lands on the hold point, clamped
// so the canvas stays covered. Zoom is a scale of the layout: 2 shows half the frame.
struct ObjectLockOptions
{
    double zoom = 2.0;
    double holdX = 0.5;
    double holdY = 0.5;
    double canvasW = 1920.0;
    double canvasH = 1080.0;
    double epsilonPx = 1.5;
};

struct ObjectLockPose
{
    double x = 0;
    double y = 0;
    double w = 0;
    double h = 0;
};

ObjectLockPose objectLockPose(double cx, double cy, const ObjectLockOptions &options);

struct ObjectLockKey
{
    int frame = 0;
    double x = 0;
    double y = 0;
    double w = 0;
    double h = 0;
};

// One pose per kept frame. Straight motion collapses to the endpoints; a turn keeps the
// frame that actually bends, so a pan does not become a key on every sample.
QList<ObjectLockKey> planObjectLock(const ObjectTrack &track, const ObjectLockOptions &options);

// Top-left of `follower`, in project pixels, so its centre sits on the host's tracked
// point at `timelineUs`. False when the follower is not pinned, or the host has no track
// to read — the caller then keeps the follower's own position keys.
bool objectFollowPosition(const QList<Track> &tracks, const Clip &follower, TimeUs timelineUs,
                          double projectW, double projectH, double *xOut, double *yOut);

} // namespace drift
