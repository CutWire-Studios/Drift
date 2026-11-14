#include "engine/compositor/layers/TransformLayer.h"

#include "core/project/clip/Clip.h"
#include "core/animation/ClipAnimation.h"
#include "core/project/Project.h"
#include "core/edit/TimelineOps.h"
#include "engine/render3d/scene/ClipTransform3d.h"

#include <QVector4D>

namespace drift {

namespace {

double valueAt(const KeyframeTrack<double> &track, TimeUs relative, double fallback)
{
    return track.isEmpty() ? fallback : track.evaluateAt(relative);
}

// Column-vector 3x3 (rows of `m`) as a row-vector QTransform.
QTransform fromColumns(const double m[3][3])
{
    return QTransform(m[0][0], m[1][0], m[2][0], m[0][1], m[1][1], m[2][1], m[0][2], m[1][2],
                      m[2][2]);
}

bool fuzzyIdentity(const QTransform &t)
{
    constexpr double eps = 1e-9;
    return qAbs(t.m11() - 1.0) < eps && qAbs(t.m22() - 1.0) < eps && qAbs(t.m33() - 1.0) < eps
           && qAbs(t.m12()) < eps && qAbs(t.m13()) < eps && qAbs(t.m21()) < eps
           && qAbs(t.m23()) < eps && qAbs(t.m31()) < 1e-6 && qAbs(t.m32()) < 1e-6;
}

} // namespace

QTransform transformLayerMatrix(const Clip &clip, TimeUs timelineUs, const QSize &projectSize,
                                double renderScale, double *opacity)
{
    const TimeUs relative = timelineUs - clip.timelineStart;
    const double rw = projectSize.width() * renderScale;
    const double rh = projectSize.height() * renderScale;
    QRectF box(valueAt(clip.transformX, relative, 0.0) * renderScale,
               valueAt(clip.transformY, relative, 0.0) * renderScale,
               valueAt(clip.transformW, relative, projectSize.width()) * renderScale,
               valueAt(clip.transformH, relative, projectSize.height()) * renderScale);
    double rotation = valueAt(clip.rotation, relative, 0.0);
    double alpha = qBound(0.0, valueAt(clip.opacity, relative, 1.0), 1.0)
                   * clip.fadeMultiplier(timelineUs);

    if (clip.animIn.kind != ClipAnimKind::None || clip.animOut.kind != ClipAnimKind::None) {
        const ClipAnimSample body =
            evaluateClipAnimation(clip.timelineStart, clip.timelineDuration, clip.animIn,
                                  clip.animOut, timelineUs, box.width(), box.height());
        alpha *= body.opacity;
        box.translate(body.dx, body.dy);
        if (!qFuzzyCompare(body.scale, 1.0)) {
            const QPointF centre = box.center();
            box.setSize(box.size() * body.scale);
            box.moveCenter(centre);
        }
        rotation += body.rotationDeg;
    }
    if (opacity)
        *opacity = alpha;
    if (rw <= 0.0 || rh <= 0.0)
        return {};

    const double fx = clip.flipH ? -1.0 : 1.0;
    const double fy = clip.flipV ? -1.0 : 1.0;

    ClipPose3d pose;
    if (clip.layer3d)
        pose = clipPoseAt(clip, relative, renderScale, projectSize.height());
    if (!pose.isActive()) {
        QTransform t;
        t.translate(box.center().x(), box.center().y());
        t.rotate(rotation);
        t.scale(fx * box.width() / rw, fy * box.height() / rh);
        t.translate(-rw / 2.0, -rh / 2.0);
        return t;
    }

    // The card's own local pixels → canvas, reduced to the z = 0 plane it lies in.
    const QMatrix4x4 local = clipLocalToCanvas(box, rotation, pose, QSizeF(rw, rh));
    const int idx[3] = {0, 1, 3};
    double a[3][3];
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c)
            a[r][c] = local(idx[r], idx[c]);
    }
    // Canvas → card-local: flip about the canvas centre, then shrink the canvas onto the box.
    const double sx = box.width() / rw;
    const double sy = box.height() / rh;
    const double toLocal[3][3] = {{fx * sx, 0.0, clip.flipH ? rw * sx : 0.0},
                                  {0.0, fy * sy, clip.flipV ? rh * sy : 0.0},
                                  {0.0, 0.0, 1.0}};
    double m[3][3];
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            m[r][c] = 0.0;
            for (int k = 0; k < 3; ++k)
                m[r][c] += a[r][k] * toLocal[k][c];
        }
    }
    return fromColumns(m);
}

QList<TransformParent> transformParentsAt(const Project &project, TimeUs timelineUs,
                                          double renderScale)
{
    const QList<Track> &tracks = project.tracks();
    bool anyLayer = false;
    for (const Track &track : tracks)
        anyLayer = anyLayer || track.isTransformLayer();
    if (!anyLayer)
        return {};

    const QSize projectSize(project.width(), project.height());
    QList<TransformParent> parents(tracks.size());
    QList<bool> touched(tracks.size(), false);
    for (int layer = 0; layer < tracks.size(); ++layer) {
        const Track &track = tracks.at(layer);
        if (!track.isTransformLayer() || track.hidden)
            continue;
        const int end = transformSpanEndIndex(tracks, layer);
        if (end < 0)
            continue;

        QTransform matrix;
        double opacity = 1.0;
        bool active = false;
        for (const Clip &clip : track.clips) {
            if (clip.adjustmentKind != AdjustmentKind::Transform || !clip.activeAt(timelineUs))
                continue;
            double clipOpacity = 1.0;
            matrix = matrix * transformLayerMatrix(clip, timelineUs, projectSize, renderScale,
                                                   &clipOpacity);
            opacity *= clipOpacity;
            active = true;
        }
        if (!active)
            continue;

        // Layers are visited outermost first, so each one applies inside those before it.
        for (int i = layer + 1; i < tracks.size(); ++i) {
            if (transformLayersCovering(tracks, i).contains(layer)) {
                parents[i].matrix = matrix * parents[i].matrix;
                parents[i].opacity *= opacity;
                touched[i] = true;
            }
        }
    }
    for (int i = 0; i < parents.size(); ++i)
        parents[i].hasParent = touched.at(i) && !fuzzyIdentity(parents.at(i).matrix);
    return parents;
}

QMatrix4x4 parentedQuadToCanvas(const QTransform &parent, const QMatrix4x4 &quad)
{
    const double a = parent.m11(), b = parent.m21(), c = parent.m31();
    const double d = parent.m12(), e = parent.m22(), f = parent.m32();
    const double g = parent.m13(), h = parent.m23(), i = parent.m33();
    if (parent.isAffine() && qFuzzyCompare(i, 1.0)) {
        const QMatrix4x4 lift(float(a), float(b), 0.f, float(c),
                              float(d), float(e), 0.f, float(f),
                              0.f, 0.f, 1.f, 0.f,
                              0.f, 0.f, 0.f, 1.f);
        return lift * quad;
    }
    // z is rebuilt from the two w's: z = w' - 2n·w + 2n², so GL's -w' <= z <= w' reads as
    // w >= n (the child's own near plane) and w' >= n(w - n) (the card's).
    const double n = kClipNearFraction;
    const QMatrix4x4 lift(float(a), float(b), 0.f, float(c),
                          float(d), float(e), 0.f, float(f),
                          float(g), float(h), 0.f, float(i - 2.0 * n),
                          float(g), float(h), 0.f, float(i));
    QMatrix4x4 out = lift * quad;
    out(2, 3) += float(2.0 * n * n);
    return out;
}

QPolygonF projectedQuad(const QMatrix4x4 &quad)
{
    QPolygonF out;
    for (const QPointF corner : {QPointF(-1, -1), QPointF(1, -1), QPointF(1, 1), QPointF(-1, 1)}) {
        const QVector4D p = quad.map(QVector4D(float(corner.x()), float(corner.y()), 0.f, 1.f));
        if (p.w() <= 1e-6f)
            return {};
        out << QPointF(p.x() / p.w(), p.y() / p.w());
    }
    return out;
}

} // namespace drift
