#pragma once

#include <QColor>
#include <QJsonObject>
#include <QList>
#include <QString>

namespace drift {

enum class GuideKind { Horizontal, Vertical, Rect, Aspect };

QString guideKindToString(GuideKind kind);
GuideKind guideKindFromString(const QString &kind);

// Positions are fractions of the canvas so a guide stays put when the canvas is resized or cropped.
struct GuideItem
{
    QString id;
    GuideKind kind = GuideKind::Vertical;
    double pos = 0.5;                                // Horizontal, Vertical
    double left = 0, top = 0, right = 0, bottom = 0; // Rect insets
    double aspectW = 16, aspectH = 9;                // Aspect: the largest centred frame of that ratio
    QColor color = Qt::white;
    double opacity = 0.5;
    bool locked = false;
};

struct GuideSet
{
    QString id;
    // Built-in names are untranslated source strings (context "GuideSet"); translate on display.
    QString name;
    bool builtIn = false;
    QList<GuideItem> items;
};

QJsonObject guideSetToJson(const GuideSet &set);
GuideSet guideSetFromJson(const QJsonObject &object);

// Ids of the first three match the old single guide type, so saved settings keep working.
const QList<GuideSet> &builtInGuideSets();

} // namespace drift
