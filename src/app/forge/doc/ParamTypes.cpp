#include "app/forge/doc/ParamTypes.h"

#include "app/forge/doc/Util.h"

#include <algorithm>

namespace drift::forge {

namespace {

double num(const QJsonValue &v)
{
    if (v.isBool())
        return v.toBool() ? 1 : 0;
    if (v.isString()) {
        bool ok = false;
        const double d = v.toString().trimmed().toDouble(&ok);
        return ok ? d : 0;
    }
    return v.toDouble();
}

QString hex8(const QJsonArray &c)
{
    return rgbaToHex(c);
}

}

const ParamTypeSpec &paramSpec(ParamType t)
{
    static const ParamTypeSpec plain{false, ParamTexture::None, true};
    static const ParamTypeSpec next{true, ParamTexture::None, true};
    static const ParamTypeSpec asset{true, ParamTexture::Asset, true};
    static const ParamTypeSpec track{true, ParamTexture::Track, false};
    static const ParamTypeSpec lookup{true, ParamTexture::Lookup, true};
    switch (t) {
    case ParamType::Float:
    case ParamType::Bool:
    case ParamType::Color:
        return plain;
    case ParamType::Int:
    case ParamType::Seed:
    case ParamType::Choice:
    case ParamType::Point:
    case ParamType::Region:
        return next;
    case ParamType::Image:
        return asset;
    case ParamType::Track:
    case ParamType::TrackMask:
    case ParamType::Audio:
        return track;
    case ParamType::Gradient:
    case ParamType::Curve:
        return lookup;
    }
    return plain;
}

QString paramGlslType(const ParamDef &p)
{
    switch (p.type) {
    case ParamType::Float:
    case ParamType::Bool:
    case ParamType::Int:
    case ParamType::Seed:
    case ParamType::Choice:
        return QStringLiteral("float");
    case ParamType::Color:
        return p.alpha.value_or(false) ? QStringLiteral("vec4") : QStringLiteral("vec3");
    case ParamType::Point:
        return QStringLiteral("vec2");
    case ParamType::Region:
        return QStringLiteral("vec4");
    default:
        return QStringLiteral("sampler2D");
    }
}

Literal paramLiteral(const ParamDef &p)
{
    switch (p.type) {
    case ParamType::Bool:
        return num(p.defaultValue) != 0 ? 1 : 0;
    case ParamType::Color:
        return hexToRgba(p.defaultValue.toString());
    case ParamType::Point: {
        const QJsonArray d = p.defaultValue.toArray();
        return QJsonArray{d.at(0), d.at(1)};
    }
    default:
        return num(p.defaultValue);
    }
}

QJsonValue paramDriftValue(const ForgeDoc &doc, const ParamDef &p, const QJsonValue &v)
{
    switch (p.type) {
    case ParamType::Float:
    case ParamType::Seed:
        return num(v);
    case ParamType::Int:
    case ParamType::Choice:
        return jsRound(num(v));
    case ParamType::Bool:
        return v.isBool() ? (v.toBool() ? 1 : 0) : (num(v) != 0 ? 1 : 0);
    case ParamType::Color: {
        const QString s = v.toString().toLower();
        if (!p.alpha.value_or(false))
            return s.left(7);
        return s.size() == 7 ? s + QStringLiteral("ff") : s;
    }
    case ParamType::Point:
    case ParamType::Region:
    case ParamType::Curve:
        return v;
    case ParamType::Image:
        return imageParamFile(doc, p);
    case ParamType::Track:
    case ParamType::TrackMask:
    case ParamType::Audio:
        return QString();
    case ParamType::Gradient: {
        QJsonArray out;
        for (const QJsonValue &s : v.toArray()) {
            const QJsonObject o = s.toObject();
            out.append(QJsonObject{{"position", o.value("pos")}, {"color", hex8(o.value("color").toArray())}});
        }
        return out;
    }
    }
    return v;
}

QJsonObject paramManifestFields(const ParamDef &p)
{
    switch (p.type) {
    case ParamType::Float:
    case ParamType::Int:
        return {{"minValue", p.min}, {"maxValue", p.max}};
    case ParamType::Choice: {
        QJsonArray o;
        for (const QString &s : p.options.value_or(QStringList()))
            o.append(s);
        return {{"options", o}};
    }
    case ParamType::Bool:
        return {{"minValue", 0}, {"maxValue", 1}};
    case ParamType::Color:
        return p.alpha.value_or(false) ? QJsonObject{{"alpha", true}} : QJsonObject{};
    case ParamType::Point:
        return {{"minValue", QJsonArray{p.min, p.min}}, {"maxValue", QJsonArray{p.max, p.max}}};
    case ParamType::Region:
        return {{"shape", p.shape.value_or(QStringLiteral("rect"))}};
    default:
        return {};
    }
}

QString mimeExt(const QString &mime)
{
    if (mime == QLatin1String("image/jpeg"))
        return QStringLiteral("jpg");
    if (mime == QLatin1String("image/webp"))
        return QStringLiteral("webp");
    return QStringLiteral("png");
}

QString imageParamFile(const ForgeDoc &doc, const ParamDef &p)
{
    const QString id = p.defaultValue.toString();
    const auto it = std::find_if(doc.assets.begin(), doc.assets.end(), [&](const ForgeAsset &a) { return a.id == id; });
    if (it == doc.assets.end())
        return {};
    return QStringLiteral("param_%1.%2").arg(p.identifier, mimeExt(it->mime));
}

bool isSamplerParam(const ParamDef &p)
{
    return paramSpec(p).texture != ParamTexture::None;
}

bool isTrackParam(const ParamDef &p)
{
    return p.type == ParamType::Track || p.type == ParamType::TrackMask || p.type == ParamType::Audio;
}

}
