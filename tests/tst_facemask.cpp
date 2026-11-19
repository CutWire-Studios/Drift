#include <QtTest>

#include "engine/face/tracking/FaceMask.h"

#include <QtMath>

using namespace drift;

class FaceMaskTest : public QObject
{
    Q_OBJECT

private slots:
    void countsSlotsAcrossFrames();
    void emptyTrackYieldsNoMask();
    void slotWithNoFaceYieldsNoMask();
    void followsTheFaceInMaskCoordinates();
    void paddingScalesTheOval();
    void gapClosesTheMaskAndReopens();
    void longGapKeysOnlyItsEdges();
    void leadingGapStartsClosed();
};

namespace {

// A frame's worth of anchors. faceRx/faceRy are width-normalized half-axes, matching what the
// landmarker writes; faceCenter is uv.
FaceAnchors anchorsAt(double cx, double cy, double rx, double ry, double angle = 0.0)
{
    FaceAnchors a;
    a.valid = true;
    a.faceCenter = QPointF(cx, cy);
    a.faceRx = rx;
    a.faceRy = ry;
    a.angle = angle;
    return a;
}

FaceTrack trackOf(const QList<QList<FaceAnchors>> &frames, int fps = 25)
{
    FaceTrack track;
    track.fps = fps;
    for (const QList<FaceAnchors> &faces : frames) {
        FaceTrackFrame frame;
        frame.faces = faces;
        track.frames.append(frame);
    }
    return track;
}

// Samples one mask time per baked frame, which is what a clip at the track's own rate maps to.
QList<QPair<TimeUs, TimeUs>> samplesFor(const FaceTrack &track)
{
    QList<QPair<TimeUs, TimeUs>> out;
    const TimeUs step = kUsPerSecond / qMax(1, track.fps);
    for (int i = 0; i < track.frames.size(); ++i)
        out.append({TimeUs(i) * step, TimeUs(i) * step});
    return out;
}

} // namespace

void FaceMaskTest::countsSlotsAcrossFrames()
{
    const FaceTrack track = trackOf({{anchorsAt(0.3, 0.3, 0.1, 0.1)},
                                     {anchorsAt(0.3, 0.3, 0.1, 0.1), anchorsAt(0.7, 0.3, 0.1, 0.1)},
                                     {anchorsAt(0.3, 0.3, 0.1, 0.1)}});
    QCOMPARE(faceSlotCount(track), 2);
    QCOMPARE(faceSlotCount(FaceTrack{}), 0);
}

void FaceMaskTest::emptyTrackYieldsNoMask()
{
    const Mask mask = faceMask(FaceTrack{}, 0, {{0, 0}}, {});
    QCOMPARE(mask.shape, MaskShape::None);
    QVERIFY(!mask.contributes());
}

void FaceMaskTest::slotWithNoFaceYieldsNoMask()
{
    // Slot 1 exists in the list but never resolves — an empty gap slot must not become a mask
    // parked on the frame's centre.
    FaceAnchors absent;
    const FaceTrack track = trackOf({{anchorsAt(0.3, 0.4, 0.1, 0.12), absent},
                                     {anchorsAt(0.3, 0.4, 0.1, 0.12), absent}});
    const Mask mask = faceMask(track, 1, samplesFor(track), {});
    QCOMPARE(mask.shape, MaskShape::None);
}

void FaceMaskTest::followsTheFaceInMaskCoordinates()
{
    const FaceTrack track = trackOf({{anchorsAt(0.25, 0.40, 0.10, 0.12, 0.0)},
                                     {anchorsAt(0.75, 0.60, 0.10, 0.12, M_PI / 4.0)}});
    FaceMaskOptions options;
    options.frameAspect = 9.0 / 16.0; // a 16:9 frame
    options.padding = 1.0;
    options.feather = 12.0;

    const Mask mask = faceMask(track, 0, samplesFor(track), options);
    QCOMPARE(mask.shape, MaskShape::Ellipse);
    QCOMPARE(mask.op, MaskOp::Add);
    QCOMPARE(mask.feather, 12.0);

    const TimeUs second = kUsPerSecond / 25;
    QVERIFY(qFuzzyCompare(mask.valueAt(QStringLiteral("x"), 0), 0.25));
    QVERIFY(qFuzzyCompare(mask.valueAt(QStringLiteral("y"), 0), 0.40));
    QVERIFY(qFuzzyCompare(mask.valueAt(QStringLiteral("x"), second), 0.75));
    QVERIFY(qFuzzyCompare(mask.valueAt(QStringLiteral("y"), second), 0.60));

    // w is already width-normalized; h divides the aspect back out so both axes describe the
    // same physical radius once the rasterizer scales them by the frame's own size.
    QVERIFY(qFuzzyCompare(mask.valueAt(QStringLiteral("w"), 0), 0.20));
    QVERIFY(qFuzzyCompare(mask.valueAt(QStringLiteral("h"), 0), 0.12 / (9.0 / 16.0) * 2.0));

    // The rasterizer rotates in degrees, clockwise, about the mask centre.
    QVERIFY(qFuzzyCompare(mask.valueAt(QStringLiteral("rotation"), 0), 0.0));
    QVERIFY(qFuzzyCompare(mask.valueAt(QStringLiteral("rotation"), second), 45.0));

    // The static scalars sit on the first resolved pose, not on the parametric default.
    QVERIFY(qFuzzyCompare(mask.x, 0.25));
    QVERIFY(qFuzzyCompare(mask.w, 0.20));
}

void FaceMaskTest::paddingScalesTheOval()
{
    const FaceTrack track = trackOf({{anchorsAt(0.5, 0.5, 0.10, 0.10)}});
    FaceMaskOptions options;
    options.padding = 1.5;
    const Mask mask = faceMask(track, 0, samplesFor(track), options);
    QVERIFY(qFuzzyCompare(mask.valueAt(QStringLiteral("w"), 0), 0.30));
    // The centre does not move when the oval grows.
    QVERIFY(qFuzzyCompare(mask.valueAt(QStringLiteral("x"), 0), 0.5));
}

void FaceMaskTest::gapClosesTheMaskAndReopens()
{
    FaceAnchors absent;
    const FaceTrack track = trackOf({{anchorsAt(0.3, 0.4, 0.10, 0.10)},
                                     {absent},
                                     {anchorsAt(0.3, 0.4, 0.10, 0.10)}});
    FaceMaskOptions options;
    options.padding = 1.0;
    const Mask mask = faceMask(track, 0, samplesFor(track), options);

    const TimeUs step = kUsPerSecond / 25;
    QVERIFY(qFuzzyCompare(mask.valueAt(QStringLiteral("w"), 0), 0.20));
    // Collapsed where the face is gone, so a "requires": "mask" effect covers nothing there.
    QCOMPARE(mask.valueAt(QStringLiteral("w"), step), 0.0);
    QCOMPARE(mask.valueAt(QStringLiteral("h"), step), 0.0);
    // Borrowed position: the collapsed mask stays on the face rather than sliding to the centre.
    QVERIFY(qFuzzyCompare(mask.valueAt(QStringLiteral("x"), step), 0.3));
    QVERIFY(qFuzzyCompare(mask.valueAt(QStringLiteral("w"), 2 * step), 0.20));
}

void FaceMaskTest::longGapKeysOnlyItsEdges()
{
    FaceAnchors absent;
    QList<QList<FaceAnchors>> frames{{anchorsAt(0.3, 0.4, 0.10, 0.10)}};
    for (int i = 0; i < 20; ++i)
        frames.append({absent});
    frames.append({anchorsAt(0.3, 0.4, 0.10, 0.10)});

    const FaceTrack track = trackOf(frames);
    const Mask mask = faceMask(track, 0, samplesFor(track), {});

    // Two resolved frames plus the two bordering the gap — the other 19 absent frames would key
    // zero against zero and are left out.
    QCOMPARE(mask.keyframes.value(QStringLiteral("w")).keyframes().size(), 4);

    const TimeUs step = kUsPerSecond / 25;
    // Still closed in the middle of the gap, where there is no key at all.
    QCOMPARE(mask.valueAt(QStringLiteral("w"), 10 * step), 0.0);
}

void FaceMaskTest::leadingGapStartsClosed()
{
    FaceAnchors absent;
    const FaceTrack track = trackOf({{absent}, {absent}, {anchorsAt(0.6, 0.5, 0.10, 0.10)}});
    const Mask mask = faceMask(track, 0, samplesFor(track), {});

    const TimeUs step = kUsPerSecond / 25;
    QCOMPARE(mask.valueAt(QStringLiteral("w"), 0), 0.0);
    QCOMPARE(mask.valueAt(QStringLiteral("w"), step), 0.0);
    QVERIFY(mask.valueAt(QStringLiteral("w"), 2 * step) > 0.0);
    // The mask was never anywhere but on the face it is waiting for.
    QVERIFY(qFuzzyCompare(mask.valueAt(QStringLiteral("x"), 0), 0.6));
}

QTEST_MAIN(FaceMaskTest)
#include "tst_facemask.moc"
