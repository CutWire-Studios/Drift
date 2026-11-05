#pragma once

#include "core/animation/Keyframe.h"
#include "core/base/Time.h"
#include "core/project/sources/VectorSource.h"

#include <QColor>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector3D>

#include <array>

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

// One of a model clip's lights, placed the way 3D Relight places its own: x and y across the
// clip's rect (0..1 spans it), z in clip heights from the clip's plane (positive is farther from
// the viewer, like the world's z; negative is nearer), and its reach in clip heights. The frame follows the clip's position and size but not
// its rotation, so turning the model moves the light across it. A spot (cone under 180 degrees)
// points at (aimX, aimY) on the plane through the model's centre. See modelLightFrame.
struct ModelLight
{
    bool enabled = false;
    double x = 0.5;
    double y = 0.5;
    double z = -1.5;
    QColor color = QColor(Qt::white);
    double intensity = 1.0;
    double radius = 3.0;
    double cone = 180.0;
    double aimX = 0.5;
    double aimY = 0.5;
};

inline constexpr int kModelLightCount = 4;
std::array<ModelLight, kModelLightCount> defaultModelLights();

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

    // Keyframeable statics, keyed in `keyframes` by model3dKeyframeProperties(): "ambient" and
    // "light<n>_<x|y|z|intensity|radius|cone|aimX|aimY>". A light's on switch and colour are
    // plain values (setModel3dLightOption).
    std::array<ModelLight, kModelLightCount> lights = defaultModelLights();
    double ambient = 0.35;
    // Keyed as "modelScale" and "modelScaleZ". Uniform size about the clip's anchor, and a further
    // multiplier on the depth alone (the depth is otherwise the geometric mean of the width and
    // height scales). 1 and 1 draws the model as fitted to the clip's rect.
    double modelScale = 1.0;
    double modelScaleZ = 1.0;
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

// Fixed order: ambient, modelScale, modelScaleZ, then each light's x, y, z, intensity, radius, cone, aimX, aimY.
const QStringList &model3dKeyframeProperties();
bool model3dScalar(const Model3dSource &source, const QString &key, double *out);
// Clamps into the value's valid range. False for an unknown key.
bool setModel3dScalar(Model3dSource &source, const QString &key, double value);
QString model3dKeyframeLabel(const QString &key);
// "light<n>_enabled" (bool) and "light<n>_color" (a colour name). False for any other key or an
// invalid colour.
bool setModel3dLightOption(Model3dSource &source, const QString &key, const QVariant &value);

} // namespace drift
