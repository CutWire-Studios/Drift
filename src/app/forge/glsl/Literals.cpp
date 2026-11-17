#include "app/forge/glsl/Literals.h"

#include <cmath>

namespace drift::forge {

QString glslFloat(double n)
{
    if (!std::isfinite(n))
        return QStringLiteral("0.0");
    QString s = QString::number(n, 'f', 6);
    if (s.contains(QLatin1Char('.'))) {
        while (s.endsWith(QLatin1Char('0')))
            s.chop(1);
        if (s.endsWith(QLatin1Char('.')))
            s.chop(1);
    }
    if (s == QLatin1String("-0"))
        s = QStringLiteral("0");
    return s.contains(QLatin1Char('.')) ? s : s + QStringLiteral(".0");
}

QString glslType(SocketType t)
{
    switch (t) {
    case SocketType::Float: return QStringLiteral("float");
    case SocketType::Vec2: return QStringLiteral("vec2");
    case SocketType::Color: return QStringLiteral("vec4");
    }
    return {};
}

QString glslLiteral(const Literal &lit, SocketType t)
{
    if (lit.isBool()) {
        const QString f = glslFloat(lit.toBool() ? 1 : 0);
        if (t == SocketType::Float)
            return f;
        if (t == SocketType::Vec2)
            return QStringLiteral("vec2(%1)").arg(f);
        return QStringLiteral("vec4(vec3(%1), 1.0)").arg(f);
    }
    if (lit.isDouble()) {
        const QString f = glslFloat(lit.toDouble());
        if (t == SocketType::Float)
            return f;
        if (t == SocketType::Vec2)
            return QStringLiteral("vec2(%1)").arg(f);
        return QStringLiteral("vec4(vec3(%1), 1.0)").arg(f);
    }
    const QJsonArray v = lit.toArray();
    const auto f = [&](int i) { return glslFloat(v.at(i).toDouble()); };
    if (v.size() == 2) {
        if (t == SocketType::Vec2)
            return QStringLiteral("vec2(%1, %2)").arg(f(0), f(1));
        if (t == SocketType::Float)
            return f(0);
        return QStringLiteral("vec4(%1, %2, 0.0, 1.0)").arg(f(0), f(1));
    }
    if (t == SocketType::Color) {
        QStringList parts;
        for (int i = 0; i < v.size(); ++i)
            parts << f(i);
        return QStringLiteral("vec4(%1)").arg(parts.join(QStringLiteral(", ")));
    }
    if (t == SocketType::Vec2)
        return QStringLiteral("vec2(%1, %2)").arg(f(0), f(1));
    return glslFloat(0.2126 * v.at(0).toDouble() + 0.7152 * v.at(1).toDouble() + 0.0722 * v.at(2).toDouble());
}

QString coerce(const QString &e, SocketType from, SocketType to)
{
    if (from == to)
        return e;
    switch (from) {
    case SocketType::Float:
        return to == SocketType::Vec2 ? QStringLiteral("vec2(%1)").arg(e) : QStringLiteral("vec4(vec3(%1), 1.0)").arg(e);
    case SocketType::Vec2:
        return to == SocketType::Float ? QStringLiteral("(%1).x").arg(e) : QStringLiteral("vec4(%1, 0.0, 1.0)").arg(e);
    case SocketType::Color:
        return to == SocketType::Float ? QStringLiteral("luma(%1)").arg(e) : QStringLiteral("(%1).xy").arg(e);
    }
    return e;
}

const QSet<QString> &glslReserved()
{
    static const QSet<QString> words = [] {
        const QString text = QStringLiteral(
            "attribute const uniform varying layout centroid flat smooth noperspective break continue do for while switch case default if else in out inout float int void bool true false invariant discard return mat2 mat3 mat4 mat2x2 mat2x3 mat2x4 mat3x2 mat3x3 mat3x4 mat4x2 mat4x3 mat4x4 vec2 vec3 vec4 ivec2 ivec3 ivec4 bvec2 bvec3 bvec4 uint uvec2 uvec3 uvec4 lowp mediump highp precision sampler2D sampler3D samplerCube struct common partition active asm class union enum typedef template this packed goto inline noinline volatile public static extern external interface long short double half fixed unsigned superp input output hvec2 hvec3 hvec4 dvec2 dvec3 dvec4 fvec2 fvec3 fvec4 filter sizeof cast namespace using "
            "radians degrees sin cos tan asin acos atan pow exp log exp2 log2 sqrt inversesqrt abs sign floor ceil fract mod min max clamp mix step smoothstep length distance dot cross normalize reflect refract texture transpose determinant inverse round trunc "
            "hash11 hash21 hash22 valueNoise fbm over luma rgb2hsv hsv2rgb rot2 rotX rotY rotZ rot3 cross2 invBilinear rayQuad rgb2ycc blendOverlay blendSoftLight easeOutBounce main uv fragColor v_texCoord "
            "driftMask u_clipMask u_hasClipMask");
        const QStringList parts = text.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        return QSet<QString>(parts.begin(), parts.end());
    }();
    return words;
}

}
