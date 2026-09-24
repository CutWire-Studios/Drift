#include "GuideSet.h"

#include <QJsonArray>
#include <QtGlobal>

namespace drift {

QString guideKindToString(GuideKind kind)
{
    switch (kind) {
    case GuideKind::Horizontal:
        return QStringLiteral("h");
    case GuideKind::Rect:
        return QStringLiteral("rect");
    case GuideKind::Aspect:
        return QStringLiteral("aspect");
    case GuideKind::Vertical:
    default:
        return QStringLiteral("v");
    }
}

GuideKind guideKindFromString(const QString &kind)
{
    if (kind == QLatin1String("h"))
        return GuideKind::Horizontal;
    if (kind == QLatin1String("rect"))
        return GuideKind::Rect;
    if (kind == QLatin1String("aspect"))
        return GuideKind::Aspect;
    return GuideKind::Vertical;
}

static QJsonObject guideItemToJson(const GuideItem &item)
{
    QJsonObject o{
        {QStringLiteral("id"), item.id},
        {QStringLiteral("kind"), guideKindToString(item.kind)},
        {QStringLiteral("color"), item.color.name(QColor::HexRgb)},
        {QStringLiteral("opacity"), item.opacity},
    };
    if (item.locked)
        o.insert(QStringLiteral("locked"), true);
    switch (item.kind) {
    case GuideKind::Horizontal:
    case GuideKind::Vertical:
        o.insert(QStringLiteral("pos"), item.pos);
        break;
    case GuideKind::Rect:
        o.insert(QStringLiteral("left"), item.left);
        o.insert(QStringLiteral("top"), item.top);
        o.insert(QStringLiteral("right"), item.right);
        o.insert(QStringLiteral("bottom"), item.bottom);
        break;
    case GuideKind::Aspect:
        o.insert(QStringLiteral("aspectW"), item.aspectW);
        o.insert(QStringLiteral("aspectH"), item.aspectH);
        break;
    }
    return o;
}

static GuideItem guideItemFromJson(const QJsonObject &o)
{
    GuideItem item;
    item.id = o.value(QStringLiteral("id")).toString();
    item.kind = guideKindFromString(o.value(QStringLiteral("kind")).toString());
    item.pos = qBound(0.0, o.value(QStringLiteral("pos")).toDouble(0.5), 1.0);
    item.left = qBound(0.0, o.value(QStringLiteral("left")).toDouble(), 0.5);
    item.top = qBound(0.0, o.value(QStringLiteral("top")).toDouble(), 0.5);
    item.right = qBound(0.0, o.value(QStringLiteral("right")).toDouble(), 0.5);
    item.bottom = qBound(0.0, o.value(QStringLiteral("bottom")).toDouble(), 0.5);
    item.aspectW = o.value(QStringLiteral("aspectW")).toDouble(16);
    item.aspectH = o.value(QStringLiteral("aspectH")).toDouble(9);
    if (item.aspectW <= 0 || item.aspectH <= 0) {
        item.aspectW = 16;
        item.aspectH = 9;
    }
    const QColor color(o.value(QStringLiteral("color")).toString());
    item.color = color.isValid() ? color : QColor(Qt::white);
    item.opacity = qBound(0.0, o.value(QStringLiteral("opacity")).toDouble(0.5), 1.0);
    item.locked = o.value(QStringLiteral("locked")).toBool(false);
    return item;
}

QJsonObject guideSetToJson(const GuideSet &set)
{
    QJsonArray items;
    for (const GuideItem &item : set.items)
        items.append(guideItemToJson(item));
    return QJsonObject{
        {QStringLiteral("id"), set.id},
        {QStringLiteral("name"), set.name},
        {QStringLiteral("items"), items},
    };
}

GuideSet guideSetFromJson(const QJsonObject &object)
{
    GuideSet set;
    set.id = object.value(QStringLiteral("id")).toString();
    set.name = object.value(QStringLiteral("name")).toString();
    for (const QJsonValue &value : object.value(QStringLiteral("items")).toArray())
        set.items.append(guideItemFromJson(value.toObject()));
    return set;
}

// Opacities match Theme.guideMedium (0x80) and Theme.guideWeak (0x66), which drew these before.
static constexpr double kMedium = 128.0 / 255.0;
static constexpr double kWeak = 102.0 / 255.0;

static GuideItem line(const char *id, GuideKind kind, double pos)
{
    GuideItem item;
    item.id = QString::fromLatin1(id);
    item.kind = kind;
    item.pos = pos;
    item.opacity = kMedium;
    return item;
}

static GuideItem inset(const char *id, double amount, double opacity)
{
    GuideItem item;
    item.id = QString::fromLatin1(id);
    item.kind = GuideKind::Rect;
    item.left = item.top = item.right = item.bottom = amount;
    item.opacity = opacity;
    return item;
}

static GuideItem frame(const char *id, double w, double h)
{
    GuideItem item;
    item.id = QString::fromLatin1(id);
    item.kind = GuideKind::Aspect;
    item.aspectW = w;
    item.aspectH = h;
    item.opacity = kMedium;
    return item;
}

const QList<GuideSet> &builtInGuideSets()
{
    static const QList<GuideSet> sets = {
        {QStringLiteral("thirds"), QT_TRANSLATE_NOOP("GuideSet", "Rule of thirds"), true,
         {line("v1", GuideKind::Vertical, 1.0 / 3), line("v2", GuideKind::Vertical, 2.0 / 3),
          line("h1", GuideKind::Horizontal, 1.0 / 3), line("h2", GuideKind::Horizontal, 2.0 / 3)}},
        {QStringLiteral("crosshair"), QT_TRANSLATE_NOOP("GuideSet", "Center cross"), true,
         {line("v", GuideKind::Vertical, 0.5), line("h", GuideKind::Horizontal, 0.5)}},
        {QStringLiteral("safe"), QT_TRANSLATE_NOOP("GuideSet", "Safe margins"), true,
         {inset("inner", 0.05, kMedium), inset("outer", 0.025, kWeak)}},
        {QStringLiteral("aspect-9x16"), QT_TRANSLATE_NOOP("GuideSet", "9:16 frame"), true,
         {frame("frame", 9, 16)}},
        {QStringLiteral("aspect-4x5"), QT_TRANSLATE_NOOP("GuideSet", "4:5 frame"), true,
         {frame("frame", 4, 5)}},
        {QStringLiteral("aspect-1x1"), QT_TRANSLATE_NOOP("GuideSet", "1:1 frame"), true,
         {frame("frame", 1, 1)}},
    };
    return sets;
}

} // namespace drift
