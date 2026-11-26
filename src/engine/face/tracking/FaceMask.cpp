#include "engine/face/tracking/FaceMask.h"

#include <QtMath>

#include <algorithm>

namespace drift {

namespace {

// One sample resolved into mask coordinates. `valid` false means the slot had no face here.
struct ResolvedSample
{
    TimeUs maskUs = 0;
    bool valid = false;
    double x = 0.5;
    double y = 0.5;
    double w = 0.0;
    double h = 0.0;
    double rotation = 0.0;
};

// The nearest sample either side of `index` that resolved, so a gap key can borrow its position
// instead of parking the collapsed mask at the frame's centre and sliding there from the face.
int nearestResolved(const QList<ResolvedSample> &samples, int index)
{
    for (int distance = 0; distance < samples.size(); ++distance) {
        const int before = index - distance;
        if (before >= 0 && samples.at(before).valid)
            return before;
        const int after = index + distance;
        if (after < samples.size() && samples.at(after).valid)
            return after;
    }
    return -1;
}

} // namespace

int faceSlotCount(const FaceTrack &track)
{
    int slotCount = 0;
    for (const FaceTrackFrame &frame : track.frames)
        slotCount = std::max(slotCount, int(frame.faces.size()));
    return slotCount;
}

Mask faceMask(const FaceTrack &track, int faceIndex, const QList<QPair<TimeUs, TimeUs>> &samples,
              const FaceMaskOptions &options)
{
    Mask mask;
    if (faceIndex < 0 || track.isEmpty() || samples.isEmpty())
        return mask;

    // A zero or negative aspect would divide the height away entirely; treat it as square, which
    // is wrong but bounded, rather than producing a mask of infinite height.
    const double aspect = options.frameAspect > 0.0 ? options.frameAspect : 1.0;
    const double padding = std::max(0.01, options.padding);

    QList<ResolvedSample> resolved;
    resolved.reserve(samples.size());
    bool anyValid = false;
    for (const QPair<TimeUs, TimeUs> &sample : samples) {
        const FaceAnchors anchors = track.sample(sample.second, faceIndex);
        ResolvedSample out;
        out.maskUs = sample.first;
        out.valid = anchors.valid && anchors.faceRx > 0.0 && anchors.faceRy > 0.0;
        if (out.valid) {
            out.x = qBound(0.0, anchors.faceCenter.x(), 1.0);
            out.y = qBound(0.0, anchors.faceCenter.y(), 1.0);
            // faceRx/faceRy are half-axes in width-normalized space. w is already in that space;
            // h is normalized to the frame's own height, so the aspect comes back out of it.
            out.w = qBound(0.0, anchors.faceRx * 2.0 * padding, 2.0);
            out.h = qBound(0.0, anchors.faceRy / aspect * 2.0 * padding, 2.0);
            // Width-normalized space is pixel space scaled uniformly, so the eye-line angle is
            // the same angle the rasterizer's degrees-clockwise rotation wants.
            out.rotation = qRadiansToDegrees(anchors.angle);
            anyValid = true;
        }
        resolved.append(out);
    }
    if (!anyValid)
        return mask;

    KeyframeTrack<double> xs;
    KeyframeTrack<double> ys;
    KeyframeTrack<double> ws;
    KeyframeTrack<double> hs;
    KeyframeTrack<double> rotations;

    for (int i = 0; i < resolved.size(); ++i) {
        const ResolvedSample &sample = resolved.at(i);
        if (!sample.valid) {
            // Only the samples bordering a gap carry a key: zero interpolated with zero stays
            // zero, so keying every frame of a long absence would just bloat the track.
            const bool prevValid = i > 0 && resolved.at(i - 1).valid;
            const bool nextValid = i + 1 < resolved.size() && resolved.at(i + 1).valid;
            if (i != 0 && !prevValid && !nextValid)
                continue;
            const int borrow = nearestResolved(resolved, i);
            const ResolvedSample &from = borrow >= 0 ? resolved.at(borrow) : sample;
            xs.setKeyframe(sample.maskUs, from.x);
            ys.setKeyframe(sample.maskUs, from.y);
            ws.setKeyframe(sample.maskUs, 0.0);
            hs.setKeyframe(sample.maskUs, 0.0);
            rotations.setKeyframe(sample.maskUs, from.rotation);
            continue;
        }
        xs.setKeyframe(sample.maskUs, sample.x);
        ys.setKeyframe(sample.maskUs, sample.y);
        ws.setKeyframe(sample.maskUs, sample.w);
        hs.setKeyframe(sample.maskUs, sample.h);
        rotations.setKeyframe(sample.maskUs, sample.rotation);
    }

    mask.shape = MaskShape::Ellipse;
    mask.op = MaskOp::Add;
    mask.feather = std::max(0.0, options.feather);
    // The scalars hold the first resolved pose, so clearing a track leaves the mask on a face
    // rather than on the parametric default's centred 60% box.
    const int first = nearestResolved(resolved, 0);
    if (first >= 0) {
        const ResolvedSample &from = resolved.at(first);
        mask.x = from.x;
        mask.y = from.y;
        mask.w = from.w;
        mask.h = from.h;
        mask.rotation = from.rotation;
    }
    mask.keyframes.insert(QStringLiteral("x"), xs);
    mask.keyframes.insert(QStringLiteral("y"), ys);
    mask.keyframes.insert(QStringLiteral("w"), ws);
    mask.keyframes.insert(QStringLiteral("h"), hs);
    mask.keyframes.insert(QStringLiteral("rotation"), rotations);
    return mask;
}

} // namespace drift
