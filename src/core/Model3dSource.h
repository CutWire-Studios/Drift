#pragma once

#include "Keyframe.h"
#include "Time.h"
#include "VectorSource.h"

#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector3D>

namespace drift {

// A glTF binary (.glb) placed on a Graphic track (ClipType::Model3d). The file lives in
// Clip::path; this struct carries what was probed from it plus the light knobs. The model is a
// 3D layer like any other: the clip's transform rect is its bounding box's front face (x/y/z,
// width/height), its rotationX/Y and rotation turn it, and its perspective is the eye it is seen
// through when there is no scene camera.

struct Model3dAnimationRef
{
    QString name;
    TimeUs durationUs = 0;
};

struct Model3dSource
{
    QString path; // kept equal to Clip::path (remapProjectPaths re-points both)

    // Probed when the file was attached so the inspector and the overlay never parse on the GUI
    // thread. Rest-pose bounds are in the renderer's normalised model space (largest axis == 1).
    QList<Model3dAnimationRef> animations;
    QVector3D aabbMin;
    QVector3D aabbMax;

    int animation = 0; // index into animations
    VectorLoop loop = VectorLoop::Loop;
    TimeUs startOffsetUs = 0;

    // Keyframeable statics. Keys in keyframes are exactly these member names.
    double lightYaw = 30.0;
    double lightPitch = 20.0;
    double lightIntensity = 1.0;
    double ambient = 0.35;
    QMap<QString, KeyframeTrack<double>> keyframes; // clip-relative times

    // The pose a project from before format 13 kept here, read so Project can move it onto the
    // clip's transform. Never written back; empty for anything current.
    struct LegacyPose
    {
        bool present = false;
        double scale = 0.5; // fraction of project height the largest rest extent spanned
        double depth = 0.5; // 0 = orthographic .. 1 = strong perspective
        double rotX = 0.0;  // degrees, glTF axes (+y up), X then Y then Z, intrinsic
        double rotY = 0.0;
        double rotZ = 0.0;
        QMap<QString, KeyframeTrack<double>> keyframes; // "scale", "depth", "rotX", "rotY", "rotZ"
    };
    LegacyPose legacyPose;

    bool isEmpty() const { return path.isEmpty(); }
    bool hasAnimations() const { return !animations.isEmpty(); }
    bool hasAabb() const { return aabbMax != aabbMin; }
    TimeUs animationDurationUs() const;
    bool isAnimated() const;
    Model3dSource resolvedAt(TimeUs clipTimeUs) const;

    QJsonObject toJson() const;
    static Model3dSource fromJson(const QJsonObject &o);
};

// Fixed order: lightYaw, lightPitch, lightIntensity, ambient.
const QStringList &model3dKeyframeProperties();
bool model3dScalar(const Model3dSource &source, const QString &key, double *out);
// Clamps into the value's valid range. False for an unknown key.
bool setModel3dScalar(Model3dSource &source, const QString &key, double value);
QString model3dKeyframeLabel(const QString &key);

} // namespace drift
