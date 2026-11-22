#include "ui/color/CurveEdit.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace drift::curveedit {

QString toJson(const Points &pts)
{
    QJsonArray arr;
    for (const CurvePoint &p : pts) {
        arr.append(QJsonObject{{QStringLiteral("x"), p.x},
                               {QStringLiteral("y"), p.y},
                               {QStringLiteral("ease"), p.ease.isEmpty() ? QStringLiteral("spline") : p.ease}});
    }
    return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

} // namespace drift::curveedit
