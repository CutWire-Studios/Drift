#include "VectorSource.h"

#include <QCryptographicHash>
#include <QJsonArray>

namespace drift {

QString vectorKindToString(VectorKind kind)
{
    switch (kind) {
    case VectorKind::Lottie:
        return QStringLiteral("lottie");
    case VectorKind::Svg:
        return QStringLiteral("svg");
    }
    return QStringLiteral("lottie");
}

VectorKind vectorKindFromString(const QString &kind)
{
    if (kind == QStringLiteral("svg"))
        return VectorKind::Svg;
    return VectorKind::Lottie;
}

QString vectorFitToString(VectorFit fit)
{
    switch (fit) {
    case VectorFit::Contain:
        return QStringLiteral("contain");
    case VectorFit::Cover:
        return QStringLiteral("cover");
    case VectorFit::Stretch:
        return QStringLiteral("stretch");
    }
    return QStringLiteral("contain");
}

VectorFit vectorFitFromString(const QString &fit)
{
    if (fit == QStringLiteral("cover"))
        return VectorFit::Cover;
    if (fit == QStringLiteral("stretch"))
        return VectorFit::Stretch;
    return VectorFit::Contain;
}

QString vectorLoopToString(VectorLoop loop)
{
    switch (loop) {
    case VectorLoop::Hold:
        return QStringLiteral("hold");
    case VectorLoop::Loop:
        return QStringLiteral("loop");
    case VectorLoop::PingPong:
        return QStringLiteral("pingpong");
    case VectorLoop::Hide:
        return QStringLiteral("hide");
    }
    return QStringLiteral("hold");
}

VectorLoop vectorLoopFromString(const QString &loop)
{
    if (loop == QStringLiteral("loop"))
        return VectorLoop::Loop;
    if (loop == QStringLiteral("pingpong"))
        return VectorLoop::PingPong;
    if (loop == QStringLiteral("hide"))
        return VectorLoop::Hide;
    return VectorLoop::Hold;
}

QString vectorSlotTypeToString(VectorSlotValue::Type type)
{
    switch (type) {
    case VectorSlotValue::Type::Color:
        return QStringLiteral("color");
    case VectorSlotValue::Type::Scalar:
        return QStringLiteral("scalar");
    case VectorSlotValue::Type::Vec2:
        return QStringLiteral("vec2");
    case VectorSlotValue::Type::Text:
        return QStringLiteral("text");
    case VectorSlotValue::Type::Image:
        return QStringLiteral("image");
    }
    return QStringLiteral("scalar");
}

VectorSlotValue::Type vectorSlotTypeFromString(const QString &type)
{
    if (type == QStringLiteral("color"))
        return VectorSlotValue::Type::Color;
    if (type == QStringLiteral("vec2"))
        return VectorSlotValue::Type::Vec2;
    if (type == QStringLiteral("text"))
        return VectorSlotValue::Type::Text;
    if (type == QStringLiteral("image"))
        return VectorSlotValue::Type::Image;
    return VectorSlotValue::Type::Scalar;
}

VectorSlotValue VectorSlotValue::fromColor(const QColor &c)
{
    VectorSlotValue v;
    v.type = Type::Color;
    v.color = c;
    return v;
}

VectorSlotValue VectorSlotValue::fromScalar(double s)
{
    VectorSlotValue v;
    v.type = Type::Scalar;
    v.scalar = s;
    return v;
}

VectorSlotValue VectorSlotValue::fromVec2(const QPointF &p)
{
    VectorSlotValue v;
    v.type = Type::Vec2;
    v.vec2 = p;
    return v;
}

VectorSlotValue VectorSlotValue::fromText(const QString &t)
{
    VectorSlotValue v;
    v.type = Type::Text;
    v.text = t;
    return v;
}

VectorSlotValue VectorSlotValue::fromImage(const QString &path)
{
    VectorSlotValue v;
    v.type = Type::Image;
    v.image = path;
    return v;
}

QJsonObject VectorSlotValue::toJson() const
{
    QJsonObject o{{QStringLiteral("type"), vectorSlotTypeToString(type)}};
    switch (type) {
    case Type::Color:
        o.insert(QStringLiteral("value"), color.name(QColor::HexArgb));
        break;
    case Type::Scalar:
        o.insert(QStringLiteral("value"), scalar);
        break;
    case Type::Vec2:
        o.insert(QStringLiteral("value"), QJsonArray{vec2.x(), vec2.y()});
        break;
    case Type::Text:
        o.insert(QStringLiteral("value"), text);
        break;
    case Type::Image:
        o.insert(QStringLiteral("value"), image);
        break;
    }
    return o;
}

VectorSlotValue VectorSlotValue::fromJson(const QJsonObject &o)
{
    VectorSlotValue v;
    v.type = vectorSlotTypeFromString(o.value(QStringLiteral("type")).toString());
    const QJsonValue value = o.value(QStringLiteral("value"));
    switch (v.type) {
    case Type::Color:
        v.color = QColor(value.toString());
        break;
    case Type::Scalar:
        v.scalar = value.toDouble();
        break;
    case Type::Vec2: {
        const QJsonArray a = value.toArray();
        v.vec2 = QPointF(a.at(0).toDouble(), a.at(1).toDouble());
        break;
    }
    case Type::Text:
        v.text = value.toString();
        break;
    case Type::Image:
        v.image = value.toString();
        break;
    }
    return v;
}

bool VectorSlotValue::operator==(const VectorSlotValue &other) const
{
    if (type != other.type)
        return false;
    switch (type) {
    case Type::Color:
        return color == other.color;
    case Type::Scalar:
        return scalar == other.scalar;
    case Type::Vec2:
        return vec2 == other.vec2;
    case Type::Text:
        return text == other.text;
    case Type::Image:
        return image == other.image;
    }
    return false;
}

QJsonObject VectorSource::toJson() const
{
    QJsonObject slotsJson;
    for (auto it = slotValues.cbegin(); it != slotValues.cend(); ++it)
        slotsJson.insert(it.key(), it.value().toJson());
    return QJsonObject{
        {QStringLiteral("kind"), vectorKindToString(kind)},
        {QStringLiteral("source"), source},
        {QStringLiteral("path"), path},
        {QStringLiteral("hash"), hash},
        {QStringLiteral("width"), width},
        {QStringLiteral("height"), height},
        {QStringLiteral("fps"), fps},
        {QStringLiteral("durationUs"), static_cast<double>(durationUs)},
        {QStringLiteral("title"), title},
        {QStringLiteral("fit"), vectorFitToString(fit)},
        {QStringLiteral("loop"), vectorLoopToString(loop)},
        {QStringLiteral("startOffsetUs"), static_cast<double>(startOffsetUs)},
        {QStringLiteral("slots"), slotsJson},
    };
}

VectorSource VectorSource::fromJson(const QJsonObject &o)
{
    VectorSource v;
    if (o.isEmpty())
        return v;
    v.kind = vectorKindFromString(o.value(QStringLiteral("kind")).toString());
    v.source = o.value(QStringLiteral("source")).toString();
    v.path = o.value(QStringLiteral("path")).toString();
    v.hash = o.value(QStringLiteral("hash")).toString();
    v.width = o.value(QStringLiteral("width")).toInt();
    v.height = o.value(QStringLiteral("height")).toInt();
    v.fps = o.value(QStringLiteral("fps")).toDouble();
    v.durationUs = static_cast<TimeUs>(o.value(QStringLiteral("durationUs")).toDouble());
    v.title = o.value(QStringLiteral("title")).toString();
    v.fit = vectorFitFromString(o.value(QStringLiteral("fit")).toString());
    v.loop = vectorLoopFromString(o.value(QStringLiteral("loop")).toString());
    v.startOffsetUs = static_cast<TimeUs>(o.value(QStringLiteral("startOffsetUs")).toDouble());
    const QJsonObject slotsJson = o.value(QStringLiteral("slots")).toObject();
    for (auto it = slotsJson.constBegin(); it != slotsJson.constEnd(); ++it)
        v.slotValues.insert(it.key(), VectorSlotValue::fromJson(it.value().toObject()));
    return v;
}

QString vectorSourceHash(const QByteArray &data)
{
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}

bool foldVectorTime(TimeUs animUs, TimeUs durationUs, VectorLoop loop, TimeUs *out)
{
    if (durationUs <= 0) {
        *out = 0;
        return true;
    }
    switch (loop) {
    case VectorLoop::Hold:
        *out = qBound(TimeUs{0}, animUs, durationUs);
        return true;
    case VectorLoop::Loop: {
        // C++ % keeps the sign of the dividend; a negative offset must still land inside the cycle.
        const TimeUs t = animUs % durationUs;
        *out = t < 0 ? t + durationUs : t;
        return true;
    }
    case VectorLoop::PingPong: {
        const TimeUs period = 2 * durationUs;
        TimeUs t = animUs % period;
        if (t < 0)
            t += period;
        *out = t > durationUs ? period - t : t;
        return true;
    }
    case VectorLoop::Hide:
        if (animUs < 0 || animUs > durationUs)
            return false;
        *out = animUs;
        return true;
    }
    *out = 0;
    return true;
}

} // namespace drift
