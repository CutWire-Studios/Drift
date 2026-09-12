#include "VectorSource.h"

#include "Effect.h"

#include <QCoreApplication>
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
    QJsonObject o{
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
    QJsonObject keyframesJson;
    for (auto it = keyframes.cbegin(); it != keyframes.cend(); ++it) {
        if (!it->isEmpty())
            keyframesJson.insert(it.key(), keyframesToJson(it.value()));
    }
    // Only animated sources carry the key, so projects without one stay byte-identical.
    if (!keyframesJson.isEmpty())
        o.insert(QStringLiteral("keyframes"), keyframesJson);
    return o;
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
    const QJsonObject keyframesJson = o.value(QStringLiteral("keyframes")).toObject();
    for (auto it = keyframesJson.constBegin(); it != keyframesJson.constEnd(); ++it)
        v.keyframes.insert(it.key(), keyframesFromJson(it.value().toObject()));
    return v;
}

bool VectorSource::isAnimated() const
{
    for (auto it = keyframes.constBegin(); it != keyframes.constEnd(); ++it) {
        if (!it->isEmpty() && it->enabled())
            return true;
    }
    return false;
}

VectorSource VectorSource::resolvedAt(TimeUs clipTimeUs) const
{
    VectorSource out = *this;
    for (auto it = keyframes.constBegin(); it != keyframes.constEnd(); ++it) {
        if (!it->isEmpty())
            setVectorSlotScalar(out, it.key(), it->evaluateAt(clipTimeUs));
    }
    out.keyframes.clear();
    return out;
}

// ---------------------------------------------------------------------------------------------
// SVG overrides

namespace {

const QStringList &svgOverrideProps()
{
    static const QStringList props{QStringLiteral("fill"), QStringLiteral("stroke"), QStringLiteral("strokeWidth"),
                                   QStringLiteral("opacity"), QStringLiteral("visible")};
    return props;
}

} // namespace

bool parseSvgOverrideKey(const QString &slot, SvgOverrideKey *out)
{
    if (!slot.startsWith(QLatin1String("svg.")))
        return false;
    const int dot = slot.lastIndexOf(QLatin1Char('.'));
    const QString prop = slot.mid(dot + 1);
    if (!svgOverrideProps().contains(prop))
        return false;
    const QString elementId = dot > 3 ? slot.mid(4, dot - 4) : QString();
    if (elementId.isEmpty() && prop == QLatin1String("visible"))
        return false;
    out->elementId = elementId;
    out->prop = prop;
    return true;
}

VectorSlotValue::Type svgOverrideType(const QString &prop)
{
    return prop == QLatin1String("fill") || prop == QLatin1String("stroke") ? VectorSlotValue::Type::Color
                                                                              : VectorSlotValue::Type::Scalar;
}

QString svgOverrideLabel(const SvgOverrideKey &key)
{
    QString prop;
    if (key.prop == QLatin1String("fill"))
        prop = QCoreApplication::translate("VectorSource", "Fill");
    else if (key.prop == QLatin1String("stroke"))
        prop = QCoreApplication::translate("VectorSource", "Stroke");
    else if (key.prop == QLatin1String("strokeWidth"))
        prop = QCoreApplication::translate("VectorSource", "Stroke width");
    else if (key.prop == QLatin1String("opacity"))
        prop = QCoreApplication::translate("VectorSource", "Opacity");
    else if (key.prop == QLatin1String("visible"))
        prop = QCoreApplication::translate("VectorSource", "Visible");
    else
        prop = key.prop;
    return key.elementId.isEmpty() ? prop : QStringLiteral("#%1 · %2").arg(key.elementId, prop);
}

namespace {

// Splits "svg.logo.fill.r" into the slot key and the channel; channel is empty for a scalar.
bool splitSlotChannel(const VectorSource &source, const QString &key, QString *slot, QChar *channel)
{
    static const QString channels = QStringLiteral("rgba");
    SvgOverrideKey parsed;
    if (parseSvgOverrideKey(key, &parsed)) {
        if (parsed.prop == QLatin1String("visible"))
            return false;
        if (svgOverrideType(parsed.prop) == VectorSlotValue::Type::Color)
            return false; // a colour needs a channel
        *slot = key;
        *channel = QChar();
        return true;
    }
    const int dot = key.lastIndexOf(QLatin1Char('.'));
    if (dot < 0 || dot + 2 != key.size() || !channels.contains(key.at(dot + 1)))
        return false;
    const QString base = key.left(dot);
    VectorSlotValue::Type type;
    if (parseSvgOverrideKey(base, &parsed)) {
        type = svgOverrideType(parsed.prop);
    } else {
        const auto it = source.slotValues.constFind(base);
        if (it == source.slotValues.constEnd())
            return false;
        type = it->type;
    }
    if (type != VectorSlotValue::Type::Color)
        return false;
    *slot = base;
    *channel = key.at(dot + 1);
    return true;
}

} // namespace

bool vectorSlotScalar(const VectorSource &source, const QString &key, double *out)
{
    QString slot;
    QChar channel;
    if (!splitSlotChannel(source, key, &slot, &channel))
        return false;
    const auto it = source.slotValues.constFind(slot);
    if (channel.isNull()) {
        if (it != source.slotValues.constEnd() && it->type != VectorSlotValue::Type::Scalar)
            return false;
        *out = it == source.slotValues.constEnd() ? 0.0 : it->scalar;
        return true;
    }
    const QColor c = it == source.slotValues.constEnd() ? QColor(Qt::black) : it->color;
    switch (channel.toLatin1()) {
    case 'r':
        *out = c.redF();
        break;
    case 'g':
        *out = c.greenF();
        break;
    case 'b':
        *out = c.blueF();
        break;
    default:
        *out = c.alphaF();
        break;
    }
    return true;
}

bool setVectorSlotScalar(VectorSource &source, const QString &key, double value)
{
    QString slot;
    QChar channel;
    if (!splitSlotChannel(source, key, &slot, &channel))
        return false;
    if (channel.isNull()) {
        VectorSlotValue &v = source.slotValues[slot];
        if (v.type != VectorSlotValue::Type::Scalar)
            v = VectorSlotValue::fromScalar(0.0);
        v.scalar = value;
        return true;
    }
    VectorSlotValue &v = source.slotValues[slot];
    if (v.type != VectorSlotValue::Type::Color)
        v = VectorSlotValue::fromColor(Qt::black);
    const double clamped = qBound(0.0, value, 1.0);
    switch (channel.toLatin1()) {
    case 'r':
        v.color.setRedF(clamped);
        break;
    case 'g':
        v.color.setGreenF(clamped);
        break;
    case 'b':
        v.color.setBlueF(clamped);
        break;
    default:
        v.color.setAlphaF(clamped);
        break;
    }
    return true;
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
