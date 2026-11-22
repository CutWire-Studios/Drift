#include "ui/color/CurveEdit.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

using namespace drift;
using namespace drift::curveedit;

class TstCurveEdit : public QObject
{
    Q_OBJECT

private slots:
    void jsonRoundTripsPointsAndEases()
    {
        Points pts = identityCurve(false);
        pts[0].ease = QStringLiteral("hold");
        const Points back = parseCurvePoints(toJson(pts), false);
        QCOMPARE(back.size(), size_t(2));
        QCOMPARE(back[0].ease, QStringLiteral("hold"));
        QCOMPARE(back[1].y, 1.0);
        QCOMPARE(evalCurve(back, 0.5, false), 0.0);
    }
};

QTEST_APPLESS_MAIN(TstCurveEdit)
#include "tst_curveedit.moc"
