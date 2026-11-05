#include "core/base/CameraEuler.h"
#include "core/project/Project.h"
#include "core/project/clip/Clip.h"
#include "core/edit/TimelineOps.h"
#include "engine/render3d/models/ModelClipTransform.h"
#include "engine/render3d/scene/ClipTransform3d.h"
#include "engine/render3d/scene/SceneCamera3d.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QtTest>

#include <cmath>

namespace {

// The placement maths as it was before format 22 (world: origin at the canvas centre, +z toward
// the viewer), kept here as the reference the migration has to reproduce.
struct LegacyPose
{
    double rotationX = 0.0;
    double rotationY = 0.0;
    double positionZ = 0.0;
    double perspective = 2000.0;
};

QMatrix4x4 legacyQuadToWorld(const QRectF &rect, double rotation, bool flipH, bool flipV, const LegacyPose &pose,
                             const QSizeF &canvas)
{
    const QPointF centre = rect.center();
    QMatrix4x4 m;
    m.translate(float(centre.x() - canvas.width() * 0.5), float(centre.y() - canvas.height() * 0.5),
                float(pose.positionZ));
    m.rotate(float(pose.rotationX), 1.f, 0.f, 0.f);
    m.rotate(float(pose.rotationY), 0.f, 1.f, 0.f);
    m.rotate(float(rotation), 0.f, 0.f, 1.f);
    m.scale(float(rect.width() * 0.5), float(rect.height() * 0.5));
    m.scale(flipH ? -1.f : 1.f, flipV ? -1.f : 1.f);
    return m;
}

QMatrix4x4 legacyModelWorld(const QRectF &rect, double rotation, const LegacyPose &pose, const QVector3D &lo,
                            const QVector3D &hi, const QSizeF &canvas)
{
    const QVector3D extent = hi - lo;
    const double sx = rect.width() / double(extent.x());
    const double sy = rect.height() / double(extent.y());
    const QPointF centre = rect.center();
    QMatrix4x4 m;
    m.translate(float(centre.x() - canvas.width() * 0.5), float(centre.y() - canvas.height() * 0.5),
                float(pose.positionZ));
    m.rotate(float(pose.rotationX), 1.f, 0.f, 0.f);
    m.rotate(float(pose.rotationY), 0.f, 1.f, 0.f);
    m.rotate(float(rotation), 0.f, 0.f, 1.f);
    m.scale(float(sx), float(sy), float(std::sqrt(sx * sy)));
    m.scale(1.f, -1.f, 1.f);
    m.translate(-(lo + hi) * 0.5f);
    return m;
}

// The legacy projection of a point in the legacy world: homogeneous canvas pixels.
QMatrix4x4 legacyEye(double perspective, const QSizeF &canvas)
{
    const double d = std::max(1.0, perspective);
    QMatrix4x4 projection;
    projection.translate(float(canvas.width() * 0.5), float(canvas.height() * 0.5));
    QMatrix4x4 persp;
    persp.setRow(2, QVector4D(0.f, 0.f, float(-1.0 / d), 1.f - 2.f * float(drift::kClipNearFraction)));
    persp.setRow(3, QVector4D(0.f, 0.f, float(-1.0 / d), 1.f));
    return projection * persp;
}

// New world -> legacy world.
QMatrix4x4 toLegacyWorld(const QSizeF &canvas)
{
    QMatrix4x4 m;
    m.translate(float(-canvas.width() * 0.5), float(-canvas.height() * 0.5));
    m.scale(1.f, 1.f, -1.f);
    return m;
}

double maxDifference(const QMatrix4x4 &a, const QMatrix4x4 &b)
{
    double worst = 0.0;
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c)
            worst = std::max(worst, double(std::abs(a(r, c) - b(r, c))));
    }
    return worst;
}

QPointF projected(const QMatrix4x4 &m, const QVector3D &p)
{
    const QVector4D h = m.map(QVector4D(p, 1.f));
    return QPointF(h.x() / h.w(), h.y() / h.w());
}

double uniform(QRandomGenerator &rng, double lo, double hi)
{
    return lo + (hi - lo) * rng.generateDouble();
}

// A track with a few keys carrying tangents and a hold, so migrating the keys has to carry more
// than the values.
drift::KeyframeTrack<double> randomTrack(QRandomGenerator &rng, double lo, double hi)
{
    drift::KeyframeTrack<double> track;
    drift::TimeUs t = 0;
    for (int i = 0; i < 4; ++i) {
        drift::Keyframe<double> key;
        key.value = uniform(rng, lo, hi);
        key.outDx = 100'000;
        key.outDy = uniform(rng, -20, 20);
        key.inDx = -100'000;
        key.inDy = uniform(rng, -20, 20);
        key.hold = i == 2;
        track.setKeyframe(t, key);
        t += 400'000;
    }
    return track;
}

// The camera a format-21 file described with these numbers (pan/dolly P, X-Y-Z angles, lens d), as
// the eye and yaw-first angles the camera has now.
drift::SceneCamera3d cameraFromLegacy(const QSizeF &canvas, const double p[3], const double rot[3], double d)
{
    const QMatrix4x4 world = drift::cameraWorldFromLegacy(p, rot, d, canvas.width(), canvas.height());
    QMatrix4x4 turn = world;
    turn.setColumn(3, QVector4D(0.f, 0.f, 0.f, 1.f));
    const double nearest[3] = {0.0, 0.0, 0.0};
    double angles[3];
    drift::cameraEulerFromMatrix(turn, nearest, angles);
    drift::SceneCamera3d camera;
    camera.positionX = world(0, 3);
    camera.positionY = world(1, 3);
    camera.positionZ = world(2, 3);
    camera.rotationX = angles[0];
    camera.rotationY = angles[1];
    camera.rotationZ = angles[2];
    camera.zoom = d;
    return camera;
}

} // namespace

class Transform3dTest : public QObject
{
    Q_OBJECT

private slots:
    void migrationKeepsClipWorldMatrices();
    void migrationKeepsModelsAndLights();
    void migrationKeepsPerspectiveAndConvertsCameras();
    void defaultPerspectiveFollowsTheProjectHeight();
    void anchorTracksRoundTripAndKeyframe();
    void anchorIsTheRotationPivot();
    void modelScaleAndAnchor();
    void worldIsRightHandedWithYDown();
    void migrationKeepsTheCameraViewAtEveryKey();
    void cameraEulerRoundTripsIncludingGimbal();
    void pitchAndYawKeepTheHorizonLevel();
    void keysDifferingOnlyInRotationKeepTheEye();
    void aimAtLooksAtTheTargetWithoutRoll();
    void zoomFocalLengthAndFovAreOneValue();
    void pointOfInterestAimsTheCamera();
};

void Transform3dTest::migrationKeepsClipWorldMatrices()
{
    QRandomGenerator rng(20241);
    const QSizeF canvas(1920, 1080);

    drift::Project old;
    old.setResolution(1920, 1080);
    drift::Clip clip;
    clip.id = QStringLiteral("c");
    clip.type = drift::ClipType::Shape;
    clip.timelineDuration = drift::secondsToUs(5.0);
    clip.layer3d = true;
    clip.rotationX = randomTrack(rng, -80, 80);
    clip.rotationY = randomTrack(rng, -80, 80);
    clip.rotation = randomTrack(rng, -180, 180);
    clip.positionZ = randomTrack(rng, -900, 900);
    clip.perspective = randomTrack(rng, 600, 4000);
    clip.transformX = randomTrack(rng, 0, 1200);
    clip.transformY = randomTrack(rng, 0, 600);
    clip.transformW = randomTrack(rng, 100, 900);
    clip.transformH = randomTrack(rng, 100, 700);
    old.tracks().clear();
    old.tracks().append(drift::Track{.type = drift::TrackType::Shape});
    old.tracks()[0].clips.append(clip);

    // The file as format 21 wrote it: same JSON, the old version number.
    QJsonObject json = old.toJson();
    json.insert(QStringLiteral("version"), 21);
    QString error;
    const drift::Project migrated = drift::Project::fromJson(json, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    const drift::Clip &now = migrated.tracks().at(0).clips.at(0);
    QVERIFY(now.layer3d);

    for (drift::TimeUs t = 0; t <= drift::secondsToUs(1.6); t += 53'000) {
        // The old pose at t, from the unmigrated tracks.
        LegacyPose before;
        before.rotationX = clip.rotationX.evaluateAt(t);
        before.rotationY = clip.rotationY.evaluateAt(t);
        before.positionZ = clip.positionZ.evaluateAt(t);
        before.perspective = clip.perspective.evaluateAt(t);
        const QRectF rect(clip.transformX.evaluateAt(t), clip.transformY.evaluateAt(t), clip.transformW.evaluateAt(t),
                          clip.transformH.evaluateAt(t));
        const double spin = clip.rotation.evaluateAt(t);
        const bool flipH = (t / 53'000) % 2 == 0;

        const drift::ClipPose3d after = drift::clipPoseAt(now, t, 1.0, 1080.0);
        QVERIFY(std::abs(after.rotationX + before.rotationX) < 1e-9);
        QVERIFY(std::abs(after.rotationY + before.rotationY) < 1e-9);
        QVERIFY(std::abs(after.positionZ + before.positionZ) < 1e-9);
        QVERIFY(std::abs(now.rotation.evaluateAt(t) - spin) < 1e-9);
        QVERIFY(std::abs(after.perspective - before.perspective) < 1e-9);

        // The world matrices differ by exactly the change of world and the half turn of the quad's
        // own z axis: A * M_old = M_new * diag(1, 1, -1).
        const QMatrix4x4 oldWorld = legacyQuadToWorld(rect, spin, flipH, false, before, canvas);
        const QMatrix4x4 newWorld = drift::clipQuadToWorld(rect, spin, flipH, false, after, canvas);
        QMatrix4x4 flipZ;
        flipZ.scale(1.f, 1.f, -1.f);
        QMatrix4x4 fromLegacy = toLegacyWorld(canvas).inverted();
        QVERIFY2(maxDifference(fromLegacy * oldWorld, newWorld * flipZ) < 2e-3, qPrintable(QString::number(t)));

        // And so the picture is the same: the canvas corners it lands on through its own eye.
        const QMatrix4x4 oldCanvas = legacyEye(before.perspective, canvas) * oldWorld;
        const QMatrix4x4 newCanvas = drift::clipQuadToCanvas(rect, spin, flipH, false, after, canvas);
        for (const QVector3D corner : {QVector3D(-1, -1, 0), QVector3D(1, -1, 0), QVector3D(1, 1, 0), QVector3D(-1, 1, 0),
                                       QVector3D(0.3f, -0.2f, 0)}) {
            const QPointF a = projected(oldCanvas, corner);
            const QPointF b = projected(newCanvas, corner);
            QVERIFY2(std::hypot(a.x() - b.x(), a.y() - b.y()) < 0.01, qPrintable(QString::number(t)));
        }

        // Through a scene camera, given in the old numbers and converted: identical picture too.
        const double oldP[3] = {uniform(rng, -300, 300), 0.0, uniform(rng, -400, 600)};
        const double oldRot[3] = {uniform(rng, -20, 20), uniform(rng, -30, 30), 0.0};
        const double oldD = uniform(rng, 800, 3000);
        const drift::SceneCamera3d camera = cameraFromLegacy(canvas, oldP, oldRot, oldD);
        // Legacy camera: the same code as before, in the legacy world.
        QMatrix4x4 legacyView = legacyEye(oldD, canvas);
        legacyView.translate(float(-oldP[0]), float(-oldP[1]), float(-oldP[2]));
        legacyView.rotate(float(-oldRot[2]), 0.f, 0.f, 1.f);
        legacyView.rotate(float(-oldRot[1]), 0.f, 1.f, 0.f);
        legacyView.rotate(float(-oldRot[0]), 1.f, 0.f, 0.f);
        const QMatrix4x4 viaOld = legacyView * oldWorld;
        const QMatrix4x4 viaNew = drift::cameraViewProjection(camera, canvas) * newWorld;
        for (const QVector3D corner : {QVector3D(-1, -1, 0), QVector3D(1, 1, 0), QVector3D(0.7f, -0.4f, 0)}) {
            const QVector4D a = viaOld.map(QVector4D(corner, 1.f));
            const QVector4D b = viaNew.map(QVector4D(corner, 1.f));
            if (a.w() <= 0.f || b.w() <= 0.f)
                continue;
            const QPointF pa(a.x() / a.w(), a.y() / a.w());
            const QPointF pb(b.x() / b.w(), b.y() / b.w());
            QVERIFY2(std::hypot(pa.x() - pb.x(), pa.y() - pb.y()) < 0.02, qPrintable(QString::number(t)));
        }
    }
}

void Transform3dTest::migrationKeepsModelsAndLights()
{
    QRandomGenerator rng(77);
    const QSizeF canvas(1280, 720);
    const QVector3D lo(-1.f, -0.4f, -0.3f);
    const QVector3D hi(1.f, 0.9f, 0.5f);
    for (int i = 0; i < 40; ++i) {
        LegacyPose before;
        before.rotationX = uniform(rng, -90, 90);
        before.rotationY = uniform(rng, -90, 90);
        before.positionZ = uniform(rng, -500, 500);
        const QRectF rect(uniform(rng, 0, 800), uniform(rng, 0, 400), uniform(rng, 50, 500), uniform(rng, 50, 300));
        const double spin = uniform(rng, -180, 180);

        // What the migration writes for those numbers.
        drift::ClipPose3d after;
        after.rotationX = -before.rotationX;
        after.rotationY = -before.rotationY;
        after.positionZ = -before.positionZ;

        // Models: the rest box lands on the same place, defaults for scale and anchor included.
        QMatrix4x4 fromLegacy = toLegacyWorld(canvas).inverted();
        const QMatrix4x4 oldModel = legacyModelWorld(rect, spin, before, lo, hi, canvas);
        const QMatrix4x4 newModel = drift::modelClipWorld(rect, spin, after, lo, hi, canvas);
        QVERIFY2(maxDifference(fromLegacy * oldModel, newModel) < 2e-3, qPrintable(QString::number(i)));
        for (int corner = 0; corner < 8; ++corner) {
            const QVector3D p((corner & 1) ? hi.x() : lo.x(), (corner & 2) ? hi.y() : lo.y(),
                              (corner & 4) ? hi.z() : lo.z());
            QVERIFY((fromLegacy.map(oldModel.map(p)) - newModel.map(p)).length() < 0.05f);
        }

        // Lights: z is clip heights away from the viewer in both, and the frame is the same place.
        const double lx = uniform(rng, -0.5, 1.5);
        const double ly = uniform(rng, -0.5, 1.5);
        const double lz = uniform(rng, -3, 3);
        const QVector3D oldOrigin(float(rect.x() - canvas.width() * 0.5), float(rect.y() - canvas.height() * 0.5),
                                  float(before.positionZ));
        const QVector3D oldLight = oldOrigin + QVector3D(float(lx * rect.width()), float(ly * rect.height()),
                                                         float(-lz * rect.height()));
        const drift::ModelLightFrame frame = drift::modelLightFrame(rect, after, canvas);
        const QVector3D newLight = frame.toWorld(QVector3D(float(lx), float(ly), float(lz)));
        QVERIFY((fromLegacy.map(oldLight) - newLight).length() < 0.05f);
        QVERIFY((frame.fromWorld(newLight) - QVector3D(float(lx), float(ly), float(lz))).length() < 1e-4f);
    }
}

void Transform3dTest::migrationKeepsPerspectiveAndConvertsCameras()
{
    drift::Project old;
    old.tracks().clear();
    old.tracks().append(drift::Track{.type = drift::TrackType::Shape});

    drift::Clip lensless;
    lensless.id = QStringLiteral("lensless");
    lensless.type = drift::ClipType::Shape;
    lensless.timelineDuration = drift::secondsToUs(2.0);
    lensless.layer3d = true;
    lensless.positionZ.setKeyframe(0, 100.0);
    old.tracks()[0].clips.append(lensless);

    drift::Clip flat;
    flat.id = QStringLiteral("flat");
    flat.type = drift::ClipType::Shape;
    flat.timelineDuration = drift::secondsToUs(2.0);
    old.tracks()[0].clips.append(flat);

    const int cameraTrack = drift::insertCameraTrack(old.tracks());
    drift::Clip camera = drift::makeCameraClip(0, drift::secondsToUs(2.0));
    camera.positionZ.setKeyframe(0, 321.0);
    camera.rotationX.setKeyframe(0, 12.0);
    camera.rotationY.setKeyframe(0, -7.0);
    old.tracks()[cameraTrack].clips.append(camera);

    QJsonObject json = old.toJson();
    json.insert(QStringLiteral("version"), 21);
    const drift::Project migrated = drift::Project::fromJson(json);

    const auto byId = [&migrated](const QString &id) -> const drift::Clip * {
        for (const drift::Track &track : migrated.tracks()) {
            for (const drift::Clip &c : track.clips) {
                if (c.id == id)
                    return &c;
            }
        }
        return nullptr;
    };
    // An empty lens used to mean 2000 px; it is written down so the clip keeps it.
    const drift::Clip *kept = byId(QStringLiteral("lensless"));
    QVERIFY(kept);
    QCOMPARE(kept->perspective.evaluateAt(0), 2000.0);
    QCOMPARE(kept->positionZ.evaluateAt(0), -100.0);
    // A flat clip is not made a 3D layer.
    QVERIFY(byId(QStringLiteral("flat"))->perspective.isEmpty());
    QVERIFY(!byId(QStringLiteral("flat"))->layer3d);
    // The camera is rewritten as an eye and angles that see the same picture; its lens is the zoom.
    const drift::Clip *camNow = drift::cameraClipAt(migrated.tracks(), 0);
    QVERIFY(camNow);
    const double oldP[3] = {0.0, 0.0, 321.0};
    const double oldRot[3] = {12.0, -7.0, 0.0};
    const QSizeF canvas(migrated.width(), migrated.height());
    const drift::SceneCamera3d expected = cameraFromLegacy(canvas, oldP, oldRot, 2000.0);
    const drift::SceneCamera3d now = drift::sceneCameraFromClip(*camNow, 0, 1.0, canvas);
    QVERIFY((drift::sceneCameraEye(now) - drift::sceneCameraEye(expected)).length() < 1e-2f);
    QCOMPARE(now.zoom, 2000.0);
    QVERIFY(!now.aimAtTarget);

    // Reading and writing the current format changes nothing further.
    const drift::Project again = drift::Project::fromJson(migrated.toJson());
    for (const drift::Track &track : again.tracks()) {
        for (const drift::Clip &c : track.clips) {
            if (c.id == QStringLiteral("lensless"))
                QCOMPARE(c.positionZ.evaluateAt(0), -100.0);
        }
    }
}

void Transform3dTest::defaultPerspectiveFollowsTheProjectHeight()
{
    // 50 mm lens on a 36 x 24 film back: tan(half the vertical field of view) = 12 / 50.
    const double fov = 2.0 * std::atan(12.0 / 50.0) * 180.0 / M_PI;
    QVERIFY(std::abs(fov - 26.99) < 0.01);
    QVERIFY(std::abs(drift::defaultClipPerspective(1080.0) - 540.0 / std::tan(fov * M_PI / 360.0)) < 1e-6);
    QVERIFY(std::abs(drift::defaultClipPerspective(1080.0) - 2250.0) < 1e-6);

    drift::Clip clip;
    clip.layer3d = true;
    QCOMPARE(drift::clipPoseAt(clip, 0, 1.0, 1080.0).perspective, drift::defaultClipPerspective(1080.0));
    QCOMPARE(drift::clipPoseAt(clip, 0, 0.5, 1080.0).perspective, drift::defaultClipPerspective(1080.0) * 0.5);
    clip.perspective.setKeyframe(0, 1234.0);
    QCOMPARE(drift::clipPoseAt(clip, 0, 1.0, 1080.0).perspective, 1234.0);
}

void Transform3dTest::anchorTracksRoundTripAndKeyframe()
{
    drift::Project project;
    drift::Clip clip;
    clip.id = QStringLiteral("a");
    clip.timelineDuration = drift::secondsToUs(2.0);
    clip.anchorX.setKeyframe(0, 10.0);
    clip.anchorX.setKeyframe(drift::secondsToUs(1.0), 110.0);
    clip.anchorZ.setKeyframe(0, -30.0);
    project.tracks()[0].clips.append(clip);
    const drift::Project back = drift::Project::fromJson(project.toJson());
    const drift::Clip &loaded = back.tracks()[0].clips[0];
    // Only the pose tracks that carry data make a layer 3D, the anchor included.
    QVERIFY(loaded.layer3d);
    QCOMPARE(loaded.anchorX.evaluateAt(drift::secondsToUs(0.5)), 60.0);
    QVERIFY(loaded.anchorY.isEmpty());
    QCOMPARE(loaded.anchorZ.evaluateAt(0), -30.0);

    const drift::ClipPose3d pose = drift::clipPoseAt(loaded, drift::secondsToUs(0.5), 2.0, 1080.0);
    QVERIFY(pose.hasAnchorX && !pose.hasAnchorY);
    QCOMPARE(pose.anchorX, 120.0); // lengths follow the render scale
    QCOMPARE(pose.anchorZ, -60.0);
    // The unset axis stays on the centre, so it follows a changing size.
    QCOMPARE(pose.anchorIn(QRectF(0, 0, 200, 100)).y(), 50.f);
    QCOMPARE(pose.anchorIn(QRectF(0, 0, 200, 300)).y(), 150.f);
    QCOMPARE(pose.anchorIn(QRectF(0, 0, 200, 300)).x(), 120.f);
}

void Transform3dTest::anchorIsTheRotationPivot()
{
    const QSizeF canvas(1000, 800);
    const QRectF rect(200, 100, 400, 300);

    // Unset: the pivot is the rect centre, so the layer turns about it.
    drift::ClipPose3d centred;
    centred.rotationX = 30.0;
    centred.rotationY = -20.0;
    centred.positionZ = 40.0;
    QVERIFY((centred.pivot(rect) - QVector3D(400.f, 250.f, 40.f)).length() < 1e-4f);
    const QMatrix4x4 centredWorld = drift::clipQuadToWorld(rect, 15.0, false, false, centred, canvas);
    QVERIFY((centredWorld.map(QVector3D()) - QVector3D(400.f, 250.f, 40.f)).length() < 1e-3f);

    // An anchor in a corner: that corner stays where it is however the layer is turned, and the
    // rest swings around it.
    drift::ClipPose3d anchored = centred;
    anchored.hasAnchorX = true;
    anchored.hasAnchorY = true;
    anchored.anchorX = 0.0;
    anchored.anchorY = 0.0;
    const QMatrix4x4 world = drift::clipQuadToWorld(rect, 15.0, false, false, anchored, canvas);
    QVERIFY((world.map(QVector3D(-1.f, -1.f, 0.f)) - QVector3D(200.f, 100.f, 40.f)).length() < 1e-3f);
    QVERIFY((world.map(QVector3D(1.f, 1.f, 0.f)) - centredWorld.map(QVector3D(1.f, 1.f, 0.f))).length() > 10.f);
    // The layer's own size is untouched: the opposite corner is still a diagonal away.
    QVERIFY(std::abs((world.map(QVector3D(1.f, 1.f, 0.f)) - world.map(QVector3D(-1.f, -1.f, 0.f))).length() - 500.f) < 1e-2f);

    // Unturned, the anchor changes nothing at all: transformX/Y still name the top-left corner.
    drift::ClipPose3d still = anchored;
    still.rotationX = still.rotationY = 0.0;
    const QMatrix4x4 flat = drift::clipQuadToWorld(rect, 0.0, false, false, still, canvas);
    QVERIFY((flat.map(QVector3D(-1.f, -1.f, 0.f)) - QVector3D(200.f, 100.f, 40.f)).length() < 1e-3f);
    QVERIFY((flat.map(QVector3D(1.f, 1.f, 0.f)) - QVector3D(600.f, 400.f, 40.f)).length() < 1e-3f);

    // The anchor's depth offset: the pivot sits anchorZ out of the layer's plane.
    drift::ClipPose3d deep = anchored;
    deep.rotationX = deep.rotationY = 0.0;
    deep.anchorZ = 100.0;
    const QMatrix4x4 deepWorld = drift::clipQuadToWorld(rect, 0.0, false, false, deep, canvas);
    QVERIFY((deepWorld.map(QVector3D(-1.f, -1.f, 0.f)) - QVector3D(200.f, 100.f, 40.f)).length() < 1e-3f);
    deep.rotationY = 90.0;
    const QMatrix4x4 swung = drift::clipQuadToWorld(rect, 0.0, false, false, deep, canvas);
    // The anchor is at z = 40 + 100; the layer's corner is 100 in front of it and is swung about
    // the vertical through the anchor, to its left.
    QVERIFY((swung.map(QVector3D(-1.f, -1.f, 0.f)) - QVector3D(100.f, 100.f, 140.f)).length() < 1e-2f);

    // The local-item placement agrees: the item's anchor px lands on the pivot.
    const QMatrix4x4 local = drift::clipLocalToWorld(rect, 15.0, anchored, canvas);
    QVERIFY((local.map(QVector3D(0.f, 0.f, 0.f)) - QVector3D(200.f, 100.f, 40.f)).length() < 1e-3f);
    // And the projection (per-clip eye) leaves a point on the view axis alone.
    drift::ClipPose3d axis;
    axis.hasAnchorX = axis.hasAnchorY = true;
    axis.anchorX = 150.0;
    axis.anchorY = 100.0;
    axis.rotationY = 25.0;
    axis.perspective = 1500.0;
    const QRectF on(350, 300, 300, 200);
    const QMatrix4x4 view = drift::clipQuadToCanvas(on, 0.0, false, false, axis, QSizeF(1000, 800));
    const float u = float((150.0 - 150.0) / 150.0);
    const float v = float((100.0 - 100.0) / 100.0);
    const QPointF pivotOnScreen = projected(view, QVector3D(u, v, 0.f));
    QVERIFY(std::hypot(pivotOnScreen.x() - 500.0, pivotOnScreen.y() - 400.0) < 1e-2);
}

void Transform3dTest::modelScaleAndAnchor()
{
    const QSizeF canvas(1000, 800);
    const QRectF rect(300, 200, 400, 200);
    const QVector3D lo(-1.f, -0.5f, -0.5f);
    const QVector3D hi(1.f, 0.5f, 0.5f);
    drift::ClipPose3d pose;
    pose.rotationY = 35.0;
    pose.positionZ = 25.0;

    const QMatrix4x4 plain = drift::modelClipWorld(rect, 0.0, pose, lo, hi, canvas);
    // Depth is the geometric mean of the face scales: 200 px per unit across, 200 up, so 200 deep.
    const QVector3D front = plain.map(QVector3D(0.f, 0.f, 0.5f));
    const QVector3D back = plain.map(QVector3D(0.f, 0.f, -0.5f));
    QVERIFY(std::abs((front - back).length() - 200.f) < 1e-2f);
    // The model's front (glTF +z) is nearer the viewer, so it has the smaller world z.
    QVERIFY(front.z() < back.z() || pose.rotationY != 0.0);

    // A scale of 2 about the centre anchor doubles every distance from the pivot and keeps it put.
    const QMatrix4x4 doubled = drift::modelClipWorld(rect, 0.0, pose, lo, hi, canvas, 2.0, 1.0);
    const QVector3D pivot = pose.pivot(rect);
    for (const QVector3D p : {QVector3D(1.f, 0.5f, 0.5f), QVector3D(-1.f, 0.f, 0.3f), QVector3D(0.2f, -0.5f, -0.5f)}) {
        const QVector3D a = plain.map(p) - pivot;
        const QVector3D b = doubled.map(p) - pivot;
        QVERIFY((b - a * 2.f).length() < 1e-2f);
    }
    // scaleZ stretches the depth alone.
    const QMatrix4x4 deeper = drift::modelClipWorld(rect, 0.0, pose, lo, hi, canvas, 1.0, 3.0);
    const QVector3D a = plain.map(QVector3D(0.7f, 0.4f, 0.5f)) - pivot;
    const QVector3D b = deeper.map(QVector3D(0.7f, 0.4f, 0.5f)) - pivot;
    QMatrix4x4 inverseRotation;
    inverseRotation.rotate(float(-pose.rotationY), 0.f, 1.f, 0.f);
    const QVector3D la = inverseRotation.map(a);
    const QVector3D lb = inverseRotation.map(b);
    QVERIFY(std::abs(la.x() - lb.x()) < 1e-2f && std::abs(la.y() - lb.y()) < 1e-2f);
    QVERIFY(std::abs(lb.z() - 3.f * la.z()) < 1e-2f);

    // An off-centre anchor pivots the rotation there, and the model's front face stays on the rect
    // when unturned.
    drift::ClipPose3d anchored = pose;
    anchored.rotationY = 0.0;
    anchored.hasAnchorX = anchored.hasAnchorY = true;
    anchored.anchorX = 0.0;
    anchored.anchorY = 0.0;
    const QMatrix4x4 still = drift::modelClipWorld(rect, 0.0, anchored, lo, hi, canvas);
    QVERIFY((still.map(QVector3D(-1.f, 0.5f, 0.5f)) - QVector3D(300.f, 200.f, 25.f - 100.f)).length() < 1e-2f);
    anchored.rotationY = 60.0;
    const QMatrix4x4 turned = drift::modelClipWorld(rect, 0.0, anchored, lo, hi, canvas);
    // The point of the box at the layer's plane over the anchor stays on the pivot, turned or not.
    const QVector3D onPlane(-1.f, 0.5f, 0.f);
    QVERIFY((still.map(onPlane) - QVector3D(300.f, 200.f, 25.f)).length() < 1e-2f);
    QVERIFY((turned.map(onPlane) - QVector3D(300.f, 200.f, 25.f)).length() < 1e-2f);
    // While the rest of the box swings around it.
    QVERIFY((turned.map(QVector3D(1.f, 0.5f, 0.f)) - still.map(QVector3D(1.f, 0.5f, 0.f))).length() > 50.f);
}

void Transform3dTest::migrationKeepsTheCameraViewAtEveryKey()
{
    QRandomGenerator rng(4242);
    const QSizeF canvas(1920, 1080);
    drift::Project old;
    old.setResolution(1920, 1080);
    old.tracks().clear();
    const int cameraTrack = drift::insertCameraTrack(old.tracks());
    const drift::TimeUs length = drift::secondsToUs(6.0);

    // Tracks keyed at different times, so the migration has to take the union of them.
    const auto keyedAt = [&](std::initializer_list<double> seconds, double lo, double hi) {
        drift::KeyframeTrack<double> track;
        for (const double s : seconds)
            track.setKeyframe(drift::secondsToUs(s), uniform(rng, lo, hi));
        return track;
    };
    drift::Clip camera = drift::makeCameraClip(0, length);
    camera.id = QStringLiteral("cam");
    camera.transformX = keyedAt({0.0, 1.0, 2.5}, -400, 400);
    camera.transformY = keyedAt({0.5, 2.5}, -300, 300);
    camera.positionZ = keyedAt({0.0, 3.0}, -300, 600);
    camera.rotationX = keyedAt({0.0, 1.0, 2.0, 4.0}, -85, 85);
    camera.rotationY = keyedAt({1.5, 4.0, 5.0}, -170, 170);
    camera.rotation = keyedAt({0.0, 5.0}, -60, 60);
    camera.perspective = keyedAt({0.0, 2.0}, 800, 3500);
    old.tracks()[cameraTrack].clips.append(camera);
    // A camera nobody touched has empty tracks and the old lens of 2000 px.
    drift::Clip untouched = drift::makeCameraClip(length, drift::secondsToUs(1.0));
    untouched.id = QStringLiteral("untouched");
    old.tracks()[cameraTrack].clips.append(untouched);

    QJsonObject json = old.toJson();
    json.insert(QStringLiteral("version"), 21);
    QString error;
    const drift::Project migrated = drift::Project::fromJson(json, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    const auto find = [&migrated](const QString &id) -> const drift::Clip * {
        for (const drift::Track &track : migrated.tracks()) {
            for (const drift::Clip &c : track.clips) {
                if (c.id == id)
                    return &c;
            }
        }
        return nullptr;
    };
    const drift::Clip *now = find(QStringLiteral("cam"));
    const drift::Clip *rest = find(QStringLiteral("untouched"));
    QVERIFY(now && rest);

    const auto value = [](const drift::KeyframeTrack<double> &t, drift::TimeUs time, double fallback) {
        return t.isEmpty() ? fallback : t.evaluateAt(time);
    };
    const auto legacyView = [&](const drift::Clip &c, drift::TimeUs time) {
        const double d = std::max(1.0, value(c.perspective, time, 2000.0));
        QMatrix4x4 view = legacyEye(d, canvas);
        view.translate(float(-value(c.transformX, time, 0)), float(-value(c.transformY, time, 0)),
                       float(-value(c.positionZ, time, 0)));
        view.rotate(float(-value(c.rotation, time, 0)), 0.f, 0.f, 1.f);
        view.rotate(float(-value(c.rotationY, time, 0)), 0.f, 1.f, 0.f);
        view.rotate(float(-value(c.rotationX, time, 0)), 1.f, 0.f, 0.f);
        return view * toLegacyWorld(canvas);
    };
    const auto compare = [&](const drift::Clip &before, const drift::Clip &after, drift::TimeUs time) {
        const QMatrix4x4 a = legacyView(before, time);
        const QMatrix4x4 b = drift::cameraViewProjection(drift::sceneCameraFromClip(after, time, 1.0, canvas), canvas);
        int compared = 0;
        for (const QVector3D point : {QVector3D(0, 0, 0), QVector3D(1920, 0, 0), QVector3D(1920, 1080, 0),
                                      QVector3D(0, 1080, 0), QVector3D(960, 540, 0), QVector3D(700, 300, -150)}) {
            const QVector4D pa = a.map(QVector4D(point, 1.f));
            const QVector4D pb = b.map(QVector4D(point, 1.f));
            QCOMPARE(pa.w() > 0.f, pb.w() > 0.f);
            if (pa.w() <= 0.f)
                continue;
            ++compared;
            const QPointF sa(pa.x() / pa.w(), pa.y() / pa.w());
            const QPointF sb(pb.x() / pb.w(), pb.y() / pb.w());
            QVERIFY2(std::hypot(sa.x() - sb.x(), sa.y() - sb.y()) < 0.05,
                     qPrintable(QStringLiteral("t=%1 point %2").arg(time).arg(compared)));
        }
        QVERIFY(compared > 0);
    };

    const drift::Clip &before = old.tracks()[cameraTrack].clips[0];
    QList<drift::TimeUs> times{0};
    for (const drift::KeyframeTrack<double> *t : {&before.transformX, &before.transformY, &before.positionZ,
                                                  &before.rotationX, &before.rotationY, &before.rotation,
                                                  &before.perspective}) {
        times.append(t->keyframes().keys());
    }
    for (const drift::TimeUs time : times)
        compare(before, *now, time);
    compare(old.tracks()[cameraTrack].clips[1], *rest, 0);

    // The angles are unwrapped from key to key: no key is a half turn away from its neighbour.
    drift::TimeUs previous = -1;
    for (const drift::TimeUs time : now->rotationY.keyframes().keys()) {
        if (previous >= 0)
            QVERIFY(std::abs(now->rotationY.evaluateAt(time) - now->rotationY.evaluateAt(previous)) < 270.0);
        previous = time;
    }
    QVERIFY(!now->aimAtTarget);
}

void Transform3dTest::cameraEulerRoundTripsIncludingGimbal()
{
    QRandomGenerator rng(9);
    const auto matrix = [](double x, double y, double z) {
        QMatrix4x4 m;
        m.rotate(float(y), 0.f, 1.f, 0.f);
        m.rotate(float(x), 1.f, 0.f, 0.f);
        m.rotate(float(z), 0.f, 0.f, 1.f);
        return m;
    };
    const auto close = [](const QMatrix4x4 &a, const QMatrix4x4 &b) {
        return maxDifference(a, b) < 2e-4;
    };
    for (int i = 0; i < 200; ++i) {
        const double x = i % 10 == 0 ? (i % 20 == 0 ? 90.0 : -90.0) : uniform(rng, -89, 89);
        const double y = uniform(rng, -179, 179);
        const double z = uniform(rng, -179, 179);
        const QMatrix4x4 m = matrix(x, y, z);
        const double nearest[3] = {uniform(rng, -180, 180), uniform(rng, -180, 180), uniform(rng, -180, 180)};
        double out[3];
        drift::cameraEulerFromMatrix(m, nearest, out);
        QVERIFY2(close(matrix(out[0], out[1], out[2]), m), qPrintable(QString::number(i)));
    }
    // A small step from a known pose stays on the same branch.
    const double near[3] = {20.0, 170.0, -30.0};
    double out[3];
    drift::cameraEulerFromMatrix(matrix(21.0, -179.0, -29.0), near, out);
    QVERIFY(std::abs(out[1] - 181.0) < 1e-2);
}

void Transform3dTest::pitchAndYawKeepTheHorizonLevel()
{
    QRandomGenerator rng(31);
    const QSizeF canvas(1920, 1080);
    for (int i = 0; i < 100; ++i) {
        drift::SceneCamera3d camera = drift::defaultSceneCamera(canvas);
        camera.rotationX = uniform(rng, -89, 89);
        camera.rotationY = uniform(rng, -180, 180);
        const QMatrix4x4 world = drift::sceneCameraWorld(camera);
        const QVector3D right = world.mapVector(QVector3D(1, 0, 0));
        QVERIFY2(std::abs(right.y()) < 1e-5f, qPrintable(QString::number(i)));
        // On screen the world's horizontal stays horizontal: two points at the same height keep one row.
        const QMatrix4x4 view = drift::cameraViewProjection(camera, canvas);
        const QVector3D forward = world.mapVector(QVector3D(0, 0, 1));
        const QVector3D centre = drift::sceneCameraEye(camera) + forward * 1500.f;
        const QVector3D beside = centre + right * 200.f;
        const QVector4D a = view.map(QVector4D(centre, 1.f));
        const QVector4D b = view.map(QVector4D(beside, 1.f));
        QVERIFY(std::abs(a.y() / a.w() - b.y() / b.w()) < 1e-2);
    }
}

void Transform3dTest::keysDifferingOnlyInRotationKeepTheEye()
{
    const QSizeF canvas(1920, 1080);
    drift::Clip camera = drift::makeCameraClip(0, drift::secondsToUs(4.0));
    for (const drift::TimeUs time : {drift::TimeUs(0), drift::secondsToUs(2.0)}) {
        camera.transformX.setKeyframe(time, 1200.0);
        camera.transformY.setKeyframe(time, 300.0);
        camera.positionZ.setKeyframe(time, -1500.0);
    }
    camera.rotationY.setKeyframe(0, 0.0);
    camera.rotationY.setKeyframe(drift::secondsToUs(2.0), 90.0);
    camera.rotationX.setKeyframe(0, -20.0);
    camera.rotationX.setKeyframe(drift::secondsToUs(2.0), 15.0);
    for (const double seconds : {0.0, 0.5, 1.0, 1.7, 2.0}) {
        const drift::SceneCamera3d c = drift::sceneCameraFromClip(camera, drift::secondsToUs(seconds), 1.0, canvas);
        QVERIFY((drift::sceneCameraEye(c) - QVector3D(1200, 300, -1500)).length() < 1e-3f);
    }
    const drift::SceneCamera3d mid = drift::sceneCameraFromClip(camera, drift::secondsToUs(1.0), 1.0, canvas);
    QVERIFY(std::abs(mid.rotationY - 45.0) < 1e-6);
    // The render scale scales the eye and leaves the angles alone.
    const drift::SceneCamera3d half = drift::sceneCameraFromClip(camera, drift::secondsToUs(1.0), 0.5, canvas);
    QVERIFY((drift::sceneCameraEye(half) - QVector3D(600, 150, -750)).length() < 1e-3f);
    QCOMPARE(half.rotationY, mid.rotationY);
}

void Transform3dTest::aimAtLooksAtTheTargetWithoutRoll()
{
    QRandomGenerator rng(77);
    const QSizeF canvas(1920, 1080);
    for (int i = 0; i < 100; ++i) {
        drift::SceneCamera3d camera = drift::defaultSceneCamera(canvas);
        camera.rotationZ = uniform(rng, -90, 90);
        camera.zoom = uniform(rng, 800, 3000);
        const QVector3D eye(float(uniform(rng, -500, 2500)), float(uniform(rng, -500, 1500)), float(uniform(rng, -3000, -500)));
        const QVector3D target(float(uniform(rng, 0, 1920)), float(uniform(rng, 0, 1080)), float(uniform(rng, -200, 800)));
        const drift::SceneCamera3d aimed = drift::aimAt(camera, eye, target);
        QCOMPARE(aimed.rotationZ, 0.0);
        QCOMPARE(aimed.zoom, camera.zoom);
        QVERIFY((drift::sceneCameraEye(aimed) - eye).length() < 1e-3f);
        const QMatrix4x4 world = drift::sceneCameraWorld(aimed);
        QVERIFY(std::abs(world.mapVector(QVector3D(1, 0, 0)).y()) < 1e-5f);
        const QVector3D want = (target - eye).normalized();
        QVERIFY((world.mapVector(QVector3D(0, 0, 1)) - want).length() < 1e-4f);
        const QVector4D p = drift::cameraViewProjection(aimed, canvas).map(QVector4D(target, 1.f));
        QVERIFY(std::abs(p.x() / p.w() - 960.0) < 0.05 && std::abs(p.y() / p.w() - 540.0) < 0.05);
    }
}

void Transform3dTest::zoomFocalLengthAndFovAreOneValue()
{
    const double height = 1080.0;
    const double zoom = drift::defaultClipPerspective(height);
    QVERIFY(std::abs(drift::focalLengthFromZoom(zoom, height) - 50.0) < 1e-9);
    QVERIFY(std::abs(drift::fovFromZoom(zoom, height) - 26.99) < 0.01);
    QVERIFY(std::abs(drift::zoomFromFocalLength(50.0, height) - zoom) < 1e-9);
    QRandomGenerator rng(5);
    for (int i = 0; i < 50; ++i) {
        const double z = uniform(rng, 200, 8000);
        QVERIFY(std::abs(drift::zoomFromFocalLength(drift::focalLengthFromZoom(z, height), height) - z) < 1e-6);
        QVERIFY(std::abs(drift::zoomFromFov(drift::fovFromZoom(z, height), height) - z) < 1e-6);
    }
    // A longer lens is a narrower view and a bigger zoom.
    QVERIFY(drift::zoomFromFocalLength(100.0, height) > drift::zoomFromFocalLength(35.0, height));
    QVERIFY(drift::fovFromZoom(4000.0, height) < drift::fovFromZoom(1000.0, height));

    // Zooming leaves the eye where it was; only the picture's scale changes.
    drift::SceneCamera3d camera = drift::defaultSceneCamera(QSizeF(1920, 1080));
    drift::SceneCamera3d zoomed = camera;
    zoomed.zoom = camera.zoom * 2.0;
    QCOMPARE(drift::sceneCameraEye(zoomed), drift::sceneCameraEye(camera));
    const auto spread = [](const drift::SceneCamera3d &c) {
        const QMatrix4x4 view = drift::cameraViewProjection(c, QSizeF(1920, 1080));
        const QVector4D p = view.map(QVector4D(1160.f, 540.f, 0.f, 1.f));
        return p.x() / p.w() - 960.0;
    };
    QVERIFY(std::abs(spread(camera) - 200.0) < 1e-2);
    QVERIFY(std::abs(spread(zoomed) - 400.0) < 1e-2);
}

void Transform3dTest::pointOfInterestAimsTheCamera()
{
    const QSizeF canvas(1920, 1080);
    drift::Clip clip = drift::makeCameraClip(0, drift::secondsToUs(4.0));
    clip.transformX.setKeyframe(0, 300.0);
    clip.transformY.setKeyframe(0, 900.0);
    clip.positionZ.setKeyframe(0, -1800.0);
    clip.anchorX.setKeyframe(0, 1500.0);
    clip.anchorY.setKeyframe(0, 200.0);
    clip.anchorZ.setKeyframe(0, 400.0);

    // Without the toggle the point of interest is stored but does not steer.
    drift::SceneCamera3d camera = drift::sceneCameraFromClip(clip, 0, 1.0, canvas);
    QVERIFY(!camera.aimAtTarget);
    QCOMPARE(camera.targetX, 1500.0);
    QCOMPARE(camera.targetZ, 400.0);
    const QVector3D poi(1500, 200, 400);
    const auto onScreen = [&](const drift::SceneCamera3d &c) {
        const QVector4D p = drift::cameraViewProjection(c, canvas).map(QVector4D(poi, 1.f));
        return QPointF(p.x() / p.w(), p.y() / p.w());
    };
    QVERIFY(std::abs(onScreen(camera).x() - 960.0) > 10.0);

    clip.aimAtTarget = true;
    camera = drift::sceneCameraFromClip(clip, 0, 1.0, canvas);
    QVERIFY(camera.aimAtTarget);
    QVERIFY(std::hypot(onScreen(camera).x() - 960.0, onScreen(camera).y() - 540.0) < 0.05);
    QVERIFY(std::abs(drift::sceneCameraWorld(camera).mapVector(QVector3D(1, 0, 0)).y()) < 1e-5f);

    // A roll offset spins the picture about the aim: the point of interest stays dead centre.
    camera.rotationZ = 25.0;
    QVERIFY(std::hypot(onScreen(camera).x() - 960.0, onScreen(camera).y() - 540.0) < 0.05);
    // A yaw offset turns away from it, in the camera's own frame.
    camera.rotationZ = 0.0;
    camera.rotationY = 10.0;
    QVERIFY(std::hypot(onScreen(camera).x() - 960.0, onScreen(camera).y() - 540.0) > 50.0);
    // The eye does not move for any of it.
    QVERIFY((drift::sceneCameraEye(camera) - QVector3D(300, 900, -1800)).length() < 1e-4f);
}

void Transform3dTest::worldIsRightHandedWithYDown()
{
    const QSizeF canvas(1920, 1080);
    const QRectF rect(0, 0, 1920, 1080);
    drift::ClipPose3d pose;
    // + rotationX tips the top edge toward the viewer (smaller z), + rotationY swings the right
    // edge toward the viewer, + rotation is clockwise on screen.
    pose.rotationX = 30.0;
    QMatrix4x4 m = drift::clipQuadToWorld(rect, 0.0, false, false, pose, canvas);
    QVERIFY(m.map(QVector3D(0.f, -1.f, 0.f)).z() < 0.f);
    QVERIFY(m.map(QVector3D(0.f, 1.f, 0.f)).z() > 0.f);
    pose.rotationX = 0.0;
    pose.rotationY = 30.0;
    m = drift::clipQuadToWorld(rect, 0.0, false, false, pose, canvas);
    QVERIFY(m.map(QVector3D(1.f, 0.f, 0.f)).z() < 0.f);
    pose.rotationY = 0.0;
    m = drift::clipQuadToWorld(rect, 90.0, false, false, pose, canvas);
    const QVector3D right = m.map(QVector3D(1.f, 0.f, 0.f)) - m.map(QVector3D());
    QVERIFY(right.y() > 0.f && std::abs(right.x()) < 1e-3f); // the right edge goes down: clockwise
    // x cross y points away from the viewer (+z): right-handed.
    QVERIFY(QVector3D::crossProduct(QVector3D(1, 0, 0), QVector3D(0, 1, 0)).z() > 0.f);
    // More depth is farther: smaller on screen.
    drift::ClipPose3d far;
    far.positionZ = 500.0;
    drift::ClipPose3d near;
    near.positionZ = -500.0;
    const QPolygonF farQuad = drift::projectedClipQuad(QRectF(660, 340, 600, 400), 0.0, far, canvas);
    const QPolygonF nearQuad = drift::projectedClipQuad(QRectF(660, 340, 600, 400), 0.0, near, canvas);
    QVERIFY(farQuad.boundingRect().width() < nearQuad.boundingRect().width());
}

QTEST_MAIN(Transform3dTest)
#include "tst_transform3d.moc"
