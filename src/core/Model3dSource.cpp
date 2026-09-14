#include "Model3dSource.h"

#include "Effect.h"

#include <QCoreApplication>
#include <QJsonArray>

#include <algorithm>

namespace drift {

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
    const QJsonObject keyframesJson = o.value(QStringLiteral("keyframes")).toObject();
    for (auto it = keyframesJson.constBegin(); it != keyframesJson.constEnd(); ++it)
        m.keyframes.insert(it.key(), keyframesFromJson(it.value().toObject()));
    return m;
}

const QStringList &model3dKeyframeProperties()
{
    static const QStringList keys{
        QStringLiteral("scale"),      QStringLiteral("depth"),
        QStringLiteral("rotX"),       QStringLiteral("rotY"),
        QStringLiteral("rotZ"),       QStringLiteral("lightYaw"),
        QStringLiteral("lightPitch"), QStringLiteral("lightIntensity"),
        QStringLiteral("ambient"),
    };
    return keys;
}

bool model3dScalar(const Model3dSource &source, const QString &key, double *out)
{
    double v = 0.0;
    if (key == QStringLiteral("scale"))
        v = source.scale;
    else if (key == QStringLiteral("depth"))
        v = source.depth;
    else if (key == QStringLiteral("rotX"))
        v = source.rotX;
    else if (key == QStringLiteral("rotY"))
        v = source.rotY;
    else if (key == QStringLiteral("rotZ"))
        v = source.rotZ;
    else if (key == QStringLiteral("lightYaw"))
        v = source.lightYaw;
    else if (key == QStringLiteral("lightPitch"))
        v = source.lightPitch;
    else if (key == QStringLiteral("lightIntensity"))
        v = source.lightIntensity;
    else if (key == QStringLiteral("ambient"))
        v = source.ambient;
    else
        return false;
    if (out)
        *out = v;
    return true;
}

bool setModel3dScalar(Model3dSource &source, const QString &key, double value)
{
    if (key == QStringLiteral("scale"))
        source.scale = std::max(0.01, value);
    else if (key == QStringLiteral("depth"))
        source.depth = std::clamp(value, 0.0, 1.0);
    else if (key == QStringLiteral("rotX"))
        source.rotX = value;
    else if (key == QStringLiteral("rotY"))
        source.rotY = value;
    else if (key == QStringLiteral("rotZ"))
        source.rotZ = value;
    else if (key == QStringLiteral("lightYaw"))
        source.lightYaw = value;
    else if (key == QStringLiteral("lightPitch"))
        source.lightPitch = value;
    else if (key == QStringLiteral("lightIntensity"))
        source.lightIntensity = std::max(0.0, value);
    else if (key == QStringLiteral("ambient"))
        source.ambient = std::clamp(value, 0.0, 1.0);
    else
        return false;
    return true;
}

QString model3dKeyframeLabel(const QString &key)
{
    if (key == QStringLiteral("scale"))
        return QCoreApplication::translate("Model3dSource", "Size");
    if (key == QStringLiteral("depth"))
        return QCoreApplication::translate("Model3dSource", "Depth");
    if (key == QStringLiteral("rotX"))
        return QCoreApplication::translate("Model3dSource", "Rotation X");
    if (key == QStringLiteral("rotY"))
        return QCoreApplication::translate("Model3dSource", "Rotation Y");
    if (key == QStringLiteral("rotZ"))
        return QCoreApplication::translate("Model3dSource", "Rotation Z");
    if (key == QStringLiteral("lightYaw"))
        return QCoreApplication::translate("Model3dSource", "Light direction");
    if (key == QStringLiteral("lightPitch"))
        return QCoreApplication::translate("Model3dSource", "Light elevation");
    if (key == QStringLiteral("lightIntensity"))
        return QCoreApplication::translate("Model3dSource", "Light intensity");
    if (key == QStringLiteral("ambient"))
        return QCoreApplication::translate("Model3dSource", "Ambient light");
    return key;
}

} // namespace drift
