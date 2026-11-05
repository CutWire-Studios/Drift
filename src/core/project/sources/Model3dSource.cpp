#include "core/project/sources/Model3dSource.h"

#include "core/effects/Effect.h"

#include <QCoreApplication>
#include <QJsonArray>

#include <algorithm>
#include <cmath>

namespace drift {

namespace {

// A light's keyframeable scalars, in the order model3dKeyframeProperties lists them.
const QStringList &lightScalarNames()
{
    static const QStringList names{QStringLiteral("x"),      QStringLiteral("y"),
                                   QStringLiteral("z"),      QStringLiteral("intensity"),
                                   QStringLiteral("radius"), QStringLiteral("cone"),
                                   QStringLiteral("aimX"),   QStringLiteral("aimY")};
    return names;
}

// "light<n>_<name>" -> the light's index and the name. False for anything else.
bool parseLightKey(const QString &key, int *index, QString *name)
{
    if (!key.startsWith(QLatin1String("light")) || key.size() < 8 || key.at(6) != QLatin1Char('_'))
        return false;
    const int n = key.at(5).digitValue();
    if (n < 1 || n > kModelLightCount)
        return false;
    *index = n - 1;
    *name = key.mid(7);
    return true;
}

double *lightScalar(ModelLight &light, const QString &name)
{
    if (name == QLatin1String("x"))
        return &light.x;
    if (name == QLatin1String("y"))
        return &light.y;
    if (name == QLatin1String("z"))
        return &light.z;
    if (name == QLatin1String("intensity"))
        return &light.intensity;
    if (name == QLatin1String("radius"))
        return &light.radius;
    if (name == QLatin1String("cone"))
        return &light.cone;
    if (name == QLatin1String("aimX"))
        return &light.aimX;
    if (name == QLatin1String("aimY"))
        return &light.aimY;
    return nullptr;
}

double clampLightScalar(const QString &name, double value)
{
    if (name == QLatin1String("x") || name == QLatin1String("y"))
        return std::clamp(value, -1.0, 2.0);
    if (name == QLatin1String("z"))
        return std::clamp(value, -3.0, 3.0);
    if (name == QLatin1String("intensity"))
        return std::clamp(value, 0.0, 4.0);
    if (name == QLatin1String("radius"))
        return std::clamp(value, 0.05, 5.0);
    if (name == QLatin1String("cone"))
        return std::clamp(value, 10.0, 180.0);
    return std::clamp(value, 0.0, 1.0); // aimX, aimY
}

// How far out, in clip heights, an old directional light is stood, and the reach it is given.
// Brightness is raised by the falloff at that distance, so the model's centre is lit as before.
constexpr double kSunDistance = 2.5;
constexpr double kSunReach = 5.0;
constexpr double kSunGain = 1.0 + (kSunDistance * kSunDistance) / (kSunReach * kSunReach);

// The point kSunDistance out along an old key light's direction, as light x, y, z. That light was
// screen-space (+x right, +y up, +z toward the viewer); this frame runs y down and z away.
QVector3D sunPosition(double yawDeg, double pitchDeg, double aspect)
{
    const double yaw = yawDeg * M_PI / 180.0;
    const double pitch = pitchDeg * M_PI / 180.0;
    const double dx = std::sin(yaw) * std::cos(pitch);
    const double dy = std::sin(pitch);
    const double dz = std::cos(yaw) * std::cos(pitch);
    return QVector3D(float(0.5 + kSunDistance * dx / std::max(1e-3, aspect)), float(0.5 - kSunDistance * dy),
                     float(-kSunDistance * dz));
}

// Before format 15 a model had one directional key light (lightYaw, lightPitch, lightIntensity).
// It becomes light 1, stood out along that direction; keys on any of the three are resampled onto
// the light's position and brightness at the same times.
void migrateKeyLight(Model3dSource &m, const QJsonObject &o, double aspect)
{
    const double yaw = o.value(QStringLiteral("lightYaw")).toDouble(30.0);
    const double pitch = o.value(QStringLiteral("lightPitch")).toDouble(20.0);
    const double intensity = std::max(0.0, o.value(QStringLiteral("lightIntensity")).toDouble(1.0));
    const auto take = [&m](const char *key) {
        KeyframeTrack<double> track = m.keyframes.take(QLatin1String(key));
        return !track.isEmpty() && track.enabled() ? track : KeyframeTrack<double>();
    };
    const KeyframeTrack<double> yawTrack = take("lightYaw");
    const KeyframeTrack<double> pitchTrack = take("lightPitch");
    const KeyframeTrack<double> intensityTrack = take("lightIntensity");

    m.lights = defaultModelLights();
    ModelLight &light = m.lights[0];
    light.enabled = true;
    light.color = QColor(Qt::white);
    light.radius = kSunReach;
    const QVector3D at = sunPosition(yaw, pitch, aspect);
    setModel3dScalar(m, QStringLiteral("light1_x"), at.x());
    setModel3dScalar(m, QStringLiteral("light1_y"), at.y());
    setModel3dScalar(m, QStringLiteral("light1_z"), at.z());
    setModel3dScalar(m, QStringLiteral("light1_intensity"), intensity * kSunGain);
    for (int n = 1; n < kModelLightCount; ++n)
        m.lights[n].enabled = false;

    QList<TimeUs> times = yawTrack.keyframes().keys();
    times.append(pitchTrack.keyframes().keys());
    std::sort(times.begin(), times.end());
    times.erase(std::unique(times.begin(), times.end()), times.end());
    if (!times.isEmpty()) {
        KeyframeTrack<double> x;
        KeyframeTrack<double> y;
        KeyframeTrack<double> z;
        for (const TimeUs t : times) {
            const QVector3D p = sunPosition(yawTrack.isEmpty() ? yaw : yawTrack.evaluateAt(t),
                                            pitchTrack.isEmpty() ? pitch : pitchTrack.evaluateAt(t), aspect);
            x.setKeyframe(t, clampLightScalar(QStringLiteral("x"), p.x()));
            y.setKeyframe(t, clampLightScalar(QStringLiteral("y"), p.y()));
            z.setKeyframe(t, clampLightScalar(QStringLiteral("z"), p.z()));
        }
        m.keyframes.insert(QStringLiteral("light1_x"), x);
        m.keyframes.insert(QStringLiteral("light1_y"), y);
        m.keyframes.insert(QStringLiteral("light1_z"), z);
    }
    if (!intensityTrack.isEmpty()) {
        KeyframeTrack<double> gained;
        for (auto it = intensityTrack.keyframes().cbegin(); it != intensityTrack.keyframes().cend(); ++it)
            gained.setKeyframe(it.key(), clampLightScalar(QStringLiteral("intensity"),
                                                          std::max(0.0, intensityTrack.evaluateAt(it.key())) * kSunGain));
        m.keyframes.insert(QStringLiteral("light1_intensity"), gained);
    }
}

} // namespace

std::array<ModelLight, kModelLightCount> defaultModelLights()
{
    std::array<ModelLight, kModelLightCount> lights;
    // A warm key up and to the right, standing where the old default key light pointed from.
    lights[0].enabled = true;
    lights[0].x = 1.6;
    lights[0].y = -0.35;
    lights[0].z = -2.0;
    lights[0].color = QColor(QStringLiteral("#fff1e0"));
    lights[0].intensity = 1.25;
    lights[0].radius = 5.0;
    // A cool fill from the left, a top light and a rim behind: off until wanted.
    lights[1].x = -0.6;
    lights[1].y = 0.3;
    lights[1].z = -1.5;
    lights[1].color = QColor(QStringLiteral("#9fc4ff"));
    lights[1].intensity = 0.6;
    lights[2].x = 0.5;
    lights[2].y = -0.8;
    lights[2].z = -0.5;
    lights[3].x = 0.5;
    lights[3].y = 0.2;
    lights[3].z = 2.0;
    lights[3].color = QColor(QStringLiteral("#ff6fb0"));
    return lights;
}

TimeUs Model3dSource::animationDurationUs() const
{
    if (animation < 0 || animation >= animations.size())
        return 0;
    return animations.at(animation).durationUs;
}

bool Model3dSource::isAnimated() const
{
    for (auto it = keyframes.constBegin(); it != keyframes.constEnd(); ++it) {
        if (!it->isEmpty() && it->enabled())
            return true;
    }
    return false;
}

Model3dSource Model3dSource::resolvedAt(TimeUs clipTimeUs) const
{
    Model3dSource out = *this;
    for (auto it = keyframes.constBegin(); it != keyframes.constEnd(); ++it) {
        if (!it->isEmpty() && it->enabled())
            setModel3dScalar(out, it.key(), it->evaluateAt(clipTimeUs));
    }
    out.keyframes.clear();
    return out;
}

QJsonObject Model3dSource::toJson() const
{
    QJsonArray animsJson;
    for (const Model3dAnimationRef &a : animations) {
        animsJson.append(QJsonObject{
            {QStringLiteral("name"), a.name},
            {QStringLiteral("durationUs"), static_cast<double>(a.durationUs)},
        });
    }
    QJsonObject o{
        {QStringLiteral("path"), path},
        {QStringLiteral("animations"), animsJson},
        {QStringLiteral("aabbMin"),
         QJsonArray{double(aabbMin.x()), double(aabbMin.y()), double(aabbMin.z())}},
        {QStringLiteral("aabbMax"),
         QJsonArray{double(aabbMax.x()), double(aabbMax.y()), double(aabbMax.z())}},
        {QStringLiteral("animation"), animation},
        {QStringLiteral("loop"), vectorLoopToString(loop)},
        {QStringLiteral("startOffsetUs"), static_cast<double>(startOffsetUs)},
    };
    for (const QString &key : model3dKeyframeProperties()) {
        double v = 0.0;
        model3dScalar(*this, key, &v);
        o.insert(key, v);
    }
    for (int n = 0; n < kModelLightCount; ++n) {
        const QString prefix = QStringLiteral("light%1_").arg(n + 1);
        o.insert(prefix + QLatin1String("enabled"), lights[n].enabled);
        o.insert(prefix + QLatin1String("color"), lights[n].color.name());
    }
    QJsonObject keyframesJson;
    for (auto it = keyframes.cbegin(); it != keyframes.cend(); ++it) {
        if (!it->isEmpty())
            keyframesJson.insert(it.key(), keyframesToJson(it.value()));
    }
    if (!keyframesJson.isEmpty())
        o.insert(QStringLiteral("keyframes"), keyframesJson);
    return o;
}

namespace {

QVector3D vec3FromJson(const QJsonValue &v)
{
    const QJsonArray a = v.toArray();
    if (a.size() != 3)
        return {};
    return QVector3D(float(a.at(0).toDouble()), float(a.at(1).toDouble()),
                     float(a.at(2).toDouble()));
}

} // namespace

Model3dSource Model3dSource::fromJson(const QJsonObject &o)
{
    Model3dSource m;
    if (o.isEmpty())
        return m;
    m.path = o.value(QStringLiteral("path")).toString();
    const QJsonArray animsJson = o.value(QStringLiteral("animations")).toArray();
    for (const QJsonValue &v : animsJson) {
        const QJsonObject a = v.toObject();
        Model3dAnimationRef ref;
        ref.name = a.value(QStringLiteral("name")).toString();
        ref.durationUs = static_cast<TimeUs>(a.value(QStringLiteral("durationUs")).toDouble());
        m.animations.append(ref);
    }
    m.aabbMin = vec3FromJson(o.value(QStringLiteral("aabbMin")));
    m.aabbMax = vec3FromJson(o.value(QStringLiteral("aabbMax")));
    m.animation = o.value(QStringLiteral("animation")).toInt();
    m.loop = vectorLoopFromString(o.value(QStringLiteral("loop")).toString());
    m.startOffsetUs = static_cast<TimeUs>(o.value(QStringLiteral("startOffsetUs")).toDouble());
    for (const QString &key : model3dKeyframeProperties()) {
        if (o.contains(key))
            setModel3dScalar(m, key, o.value(key).toDouble());
    }
    for (int n = 1; n <= kModelLightCount; ++n) {
        for (const char *option : {"enabled", "color"}) {
            const QString key = QStringLiteral("light%1_%2").arg(n).arg(QLatin1String(option));
            if (o.contains(key))
                setModel3dLightOption(m, key, o.value(key).toVariant());
        }
    }
    static const QStringList legacyKeys{QStringLiteral("scale"), QStringLiteral("depth"),
                                        QStringLiteral("rotX"), QStringLiteral("rotY"),
                                        QStringLiteral("rotZ")};
    LegacyPose &legacy = m.legacyPose;
    for (const QString &key : legacyKeys) {
        if (!o.contains(key))
            continue;
        legacy.present = true;
        const double v = o.value(key).toDouble();
        if (key == legacyKeys.at(0))
            legacy.scale = std::max(0.01, v);
        else if (key == legacyKeys.at(1))
            legacy.depth = std::clamp(v, 0.0, 1.0);
        else if (key == legacyKeys.at(2))
            legacy.rotX = v;
        else if (key == legacyKeys.at(3))
            legacy.rotY = v;
        else
            legacy.rotZ = v;
    }
    const QJsonObject keyframesJson = o.value(QStringLiteral("keyframes")).toObject();
    for (auto it = keyframesJson.constBegin(); it != keyframesJson.constEnd(); ++it) {
        if (legacyKeys.contains(it.key())) {
            legacy.present = true;
            legacy.keyframes.insert(it.key(), keyframesFromJson(it.value().toObject()));
        } else {
            m.keyframes.insert(it.key(), keyframesFromJson(it.value().toObject()));
        }
    }
    if (o.contains(QStringLiteral("lightYaw")) && !o.contains(QStringLiteral("light1_enabled"))) {
        // The clip's rect is the box's front face, so the box gives the light frame's aspect.
        const QVector3D extent = m.aabbMax - m.aabbMin;
        const double aspect = extent.x() > 1e-6f && extent.y() > 1e-6f ? double(extent.x()) / extent.y() : 1.0;
        migrateKeyLight(m, o, aspect);
    }
    return m;
}

const QStringList &model3dKeyframeProperties()
{
    static const QStringList keys = [] {
        QStringList out{QStringLiteral("ambient"), QStringLiteral("modelScale"), QStringLiteral("modelScaleZ")};
        for (int n = 1; n <= kModelLightCount; ++n) {
            for (const QString &name : lightScalarNames())
                out.append(QStringLiteral("light%1_%2").arg(n).arg(name));
        }
        return out;
    }();
    return keys;
}

bool model3dScalar(const Model3dSource &source, const QString &key, double *out)
{
    double v = 0.0;
    int index = 0;
    QString name;
    if (key == QLatin1String("ambient")) {
        v = source.ambient;
    } else if (key == QLatin1String("modelScale")) {
        v = source.modelScale;
    } else if (key == QLatin1String("modelScaleZ")) {
        v = source.modelScaleZ;
    } else if (parseLightKey(key, &index, &name)) {
        ModelLight light = source.lights[size_t(index)];
        const double *field = lightScalar(light, name);
        if (!field)
            return false;
        v = *field;
    } else {
        return false;
    }
    if (out)
        *out = v;
    return true;
}

bool setModel3dScalar(Model3dSource &source, const QString &key, double value)
{
    int index = 0;
    QString name;
    if (key == QLatin1String("ambient")) {
        source.ambient = std::clamp(value, 0.0, 1.0);
        return true;
    }
    if (key == QLatin1String("modelScale")) {
        source.modelScale = std::clamp(value, 0.01, 100.0);
        return true;
    }
    if (key == QLatin1String("modelScaleZ")) {
        source.modelScaleZ = std::clamp(value, 0.01, 100.0);
        return true;
    }
    if (!parseLightKey(key, &index, &name))
        return false;
    double *field = lightScalar(source.lights[size_t(index)], name);
    if (!field)
        return false;
    *field = clampLightScalar(name, value);
    return true;
}

bool setModel3dLightOption(Model3dSource &source, const QString &key, const QVariant &value)
{
    int index = 0;
    QString name;
    if (!parseLightKey(key, &index, &name))
        return false;
    ModelLight &light = source.lights[size_t(index)];
    if (name == QLatin1String("enabled")) {
        light.enabled = value.toBool();
        return true;
    }
    if (name == QLatin1String("color")) {
        const QColor c = value.typeId() == QMetaType::QColor ? value.value<QColor>() : QColor(value.toString());
        if (!c.isValid())
            return false;
        light.color = c;
        return true;
    }
    return false;
}

QString model3dKeyframeLabel(const QString &key)
{
    if (key == QLatin1String("ambient"))
        return QCoreApplication::translate("Model3dSource", "Ambient light");
    if (key == QLatin1String("modelScale"))
        return QCoreApplication::translate("Model3dSource", "Model scale");
    if (key == QLatin1String("modelScaleZ"))
        return QCoreApplication::translate("Model3dSource", "Model depth scale");
    int index = 0;
    QString name;
    if (!parseLightKey(key, &index, &name))
        return key;
    QString what;
    if (name == QLatin1String("x"))
        what = QCoreApplication::translate("Model3dSource", "left / right");
    else if (name == QLatin1String("y"))
        what = QCoreApplication::translate("Model3dSource", "up / down");
    else if (name == QLatin1String("z"))
        what = QCoreApplication::translate("Model3dSource", "near / far");
    else if (name == QLatin1String("intensity"))
        what = QCoreApplication::translate("Model3dSource", "brightness");
    else if (name == QLatin1String("radius"))
        what = QCoreApplication::translate("Model3dSource", "reach");
    else if (name == QLatin1String("cone"))
        what = QCoreApplication::translate("Model3dSource", "spot angle");
    else if (name == QLatin1String("aimX"))
        what = QCoreApplication::translate("Model3dSource", "aim left / right");
    else if (name == QLatin1String("aimY"))
        what = QCoreApplication::translate("Model3dSource", "aim up / down");
    else
        return key;
    return QCoreApplication::translate("Model3dSource", "Light %1 %2").arg(index + 1).arg(what);
}

} // namespace drift
