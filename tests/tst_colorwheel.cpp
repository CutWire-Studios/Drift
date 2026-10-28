#include "ui/color/ColorWheelMath.h"

#include <QtTest>

using namespace drift::colorwheel;

class TstColorWheel : public QObject
{
    Q_OBJECT

private slots:
    void dragIsRelativeToTheOffset()
    {
        const QPointF p = dragged({0.2, 0.1}, {30, -15}, 60, false);
        QVERIFY(qAbs(p.x() - 0.7) < 1e-12);
        QVERIFY(qAbs(p.y() - 0.35) < 1e-12);
    }

    void shiftMakesTheDragFine()
    {
        const QPointF p = dragged({0, 0}, {60, 0}, 60, true);
        QVERIFY(qAbs(p.x() - 0.1) < 1e-12);
        QCOMPARE(p.y(), 0.0);
    }

    void dragClampsToTheUnitCircle()
    {
        const QPointF p = dragged({0.5, 0.5}, {600, -600}, 60, false);
        QVERIFY(qAbs(std::hypot(p.x(), p.y()) - 1.0) < 1e-12);
        QVERIFY(p.x() > 0 && p.y() > 0);
        QCOMPARE(clampToUnitDisc({0.3, 0.4}), QPointF(0.3, 0.4));
    }

    void nudgeClamps()
    {
        QCOMPARE(nudged({0, 0}, 0.01, 0), QPointF(0.01, 0));
        const QPointF p = nudged({1, 0}, 0.5, 0);
        QVERIFY(qAbs(p.x() - 1.0) < 1e-12);
    }

    void discColorIsNeutralAtTheCentreAndTintedOnTheRim()
    {
        const QColor neutral(100, 110, 120);
        QCOMPARE(QColor::fromRgb(discColor(0, 0, neutral, 0.6)).rgb(), neutral.rgb());
        const QColor red = QColor::fromRgb(discColor(1, 0, neutral, 0.6));
        QVERIFY(red.red() > neutral.red());
        QVERIFY(red.green() < neutral.green());
        QVERIFY(red.blue() < neutral.blue());
        const QColor up = QColor::fromRgb(discColor(0, 1, neutral, 0.6));
        QVERIFY(up.blue() > neutral.blue());
        QVERIFY(up.green() < neutral.green());
    }

    void discImageIsOpaqueInsideAndClearOutside()
    {
        const QImage disc = buildDisc(64, QColor(100, 110, 120), 0.6);
        QCOMPARE(disc.size(), QSize(64, 64));
        QCOMPARE(qAlpha(disc.pixel(32, 32)), 255);
        QCOMPARE(qAlpha(disc.pixel(0, 0)), 0);
        QCOMPARE(qAlpha(disc.pixel(63, 63)), 0);
        const QColor right = QColor::fromRgb(disc.pixel(62, 32));
        QVERIFY(right.red() > right.green());
    }
};

QTEST_APPLESS_MAIN(TstColorWheel)
#include "tst_colorwheel.moc"
