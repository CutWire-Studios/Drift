#include "app/forge/doc/Util.h"

#include <QRandomGenerator>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>

namespace drift::forge {

namespace {
QString randomBase36(int n)
{
    static const char digits[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    QString s;
    for (int i = 0; i < n; ++i)
        s += QLatin1Char(digits[QRandomGenerator::global()->bounded(36)]);
    return s;
}

QString byte2(double v)
{
    const int b = int(jsRound(std::clamp(v, 0.0, 1.0) * 255));
    return QStringLiteral("%1").arg(b, 2, 16, QLatin1Char('0'));
}
}

double jsRound(double v)
{
    return std::floor(v + 0.5);
}

QString uid(const QString &prefix)
{
    return prefix + randomBase36(6);
}

QString slugify(const QString &name)
{
    static const QRegularExpression nonAlnum(QStringLiteral("[^a-z0-9]+"));
    static const QRegularExpression edges(QStringLiteral("^_+|_+$"));
    QString s = name.toLower();
    s.replace(nonAlnum, QStringLiteral("_"));
    s.remove(edges);
    s = s.left(32);
    return s.isEmpty() ? QStringLiteral("untitled") : s;
}

QString makeEffectId(const QString &name)
{
    return QStringLiteral("forge_%1_%2").arg(slugify(name), randomBase36(4));
}

ForgeDoc emptyDoc(Kind kind, const QString &name)
{
    const KindInfo &info = kindInfo(kind);
    const QString n = name.isNull() ? info.defaultName : name;
    ForgeDoc d;
    d.kind = kind;
    d.meta.id = makeEffectId(n);
    d.meta.displayName = n;
    d.meta.category = info.defaultCategory;
    d.meta.version = QStringLiteral("1.0.0");
    if (kind == Kind::Audio) {
        d.audio = QJsonObject{{"rack", QJsonObject{{"chain", QJsonArray()}, {"modulators", QJsonArray()}, {"routes", QJsonArray()}}}};
    }
    d.preview.thumbTime = 0.5;
    return d;
}

QJsonArray hexToRgba(const QString &hex)
{
    static const QRegularExpression re(QStringLiteral("^#?([0-9a-fA-F]{6})([0-9a-fA-F]{2})?$"));
    const auto m = re.match(hex.trimmed());
    if (!m.hasMatch())
        return {0, 0, 0, 1};
    const uint n = m.captured(1).toUInt(nullptr, 16);
    const double a = m.captured(2).isEmpty() ? 1.0 : m.captured(2).toUInt(nullptr, 16) / 255.0;
    return {((n >> 16) & 255) / 255.0, ((n >> 8) & 255) / 255.0, (n & 255) / 255.0, a};
}

QString rgbToHex(const QJsonArray &c)
{
    return QStringLiteral("#") + byte2(c.at(0).toDouble()) + byte2(c.at(1).toDouble()) + byte2(c.at(2).toDouble());
}

QString rgbaToHex(const QJsonArray &c)
{
    return rgbToHex(c) + byte2(c.size() > 3 ? c.at(3).toDouble(1.0) : 1.0);
}

}
