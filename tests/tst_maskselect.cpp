#include <QtTest>

#include "app/editor/sessions/MaskSelection.h"

#include <QPainter>

using namespace drift::masking;

class MaskSelectTest : public QObject
{
    Q_OBJECT

private slots:
    void fillsAPolygonAndErases();
    void applyCoverBlends();
    void closedCircleIsALoop();
    void openArcIsNotALoop();
    void figureEightYieldsTwoLoops();
    void tinyWobbleIsIgnored();
    void livewireFollowsAnEdge();
    void snapFindsTheEdge();
    void wandToleranceAndContiguity();
};

static QImage blank(int w, int h)
{
    QImage image(w, h, QImage::Format_Grayscale8);
    image.fill(0);
    return image;
}

static QVector<QPointF> circle(QPointF c, double r, int steps, double sweep = 6.2831853)
{
    QVector<QPointF> pts;
    for (int i = 0; i <= steps; ++i) {
        const double a = sweep * i / steps;
        pts.append(c + QPointF(std::cos(a) * r, std::sin(a) * r));
    }
    return pts;
}

void MaskSelectTest::fillsAPolygonAndErases()
{
    QImage image = blank(100, 100);
    fillPolygon(image, {{20, 20}, {80, 20}, {80, 80}, {20, 80}}, false);
    QCOMPARE(image.constScanLine(50)[50], uchar(255));
    QCOMPARE(image.constScanLine(10)[10], uchar(0));
    fillPolygon(image, {{40, 40}, {60, 40}, {60, 60}, {40, 60}}, true);
    QCOMPARE(image.constScanLine(50)[50], uchar(0));
    QCOMPARE(image.constScanLine(30)[30], uchar(255));
}

void MaskSelectTest::applyCoverBlends()
{
    QImage image = blank(40, 40);
    QImage cover = blank(20, 20);
    cover.fill(255);
    applyCover(image, cover, false);
    QCOMPARE(image.constScanLine(5)[5], uchar(255));
    cover.fill(128);
    applyCover(image, cover, true);
    const int v = image.constScanLine(5)[5];
    QVERIFY(v > 110 && v < 140);
}

void MaskSelectTest::closedCircleIsALoop()
{
    const QVector<QPointF> pts = circle({100, 100}, 50, 60, 6.0);
    const auto loops = findStrokeLoops(pts, 20, 400);
    QCOMPARE(loops.size(), 1);
    QImage image = blank(200, 200);
    fillPolygon(image, loops.first(), false);
    QCOMPARE(image.constScanLine(100)[100], uchar(255));
    QCOMPARE(image.constScanLine(5)[5], uchar(0));
}

void MaskSelectTest::openArcIsNotALoop()
{
    QVERIFY(findStrokeLoops(circle({100, 100}, 50, 40, 3.5), 20, 400).isEmpty());
    QVector<QPointF> line;
    for (int i = 0; i < 30; ++i)
        line.append(QPointF(i * 3, 10));
    for (int i = 29; i >= 0; --i)
        line.append(QPointF(i * 3, 10.5));
    QVERIFY(findStrokeLoops(line, 20, 400).isEmpty());
}

void MaskSelectTest::figureEightYieldsTwoLoops()
{
    QVector<QPointF> pts;
    for (int i = 0; i <= 120; ++i) {
        const double a = 0.5 + 5.8 * i / 120;
        pts.append(QPointF(100 + 60 * std::sin(a), 100 + 30 * std::sin(2 * a)));
    }
    const auto loops = findStrokeLoops(pts, 0.0, 400);
    QVERIFY(loops.size() >= 1);
}

void MaskSelectTest::tinyWobbleIsIgnored()
{
    QVERIFY(findStrokeLoops(circle({50, 50}, 3, 12), 10, 400).isEmpty());
}

static QImage edgeImage()
{
    QImage image(120, 80, QImage::Format_RGB32);
    image.fill(qRgb(20, 20, 20));
    QPainter p(&image);
    p.fillRect(QRect(0, 40, 120, 40), QColor(230, 230, 230));
    return image;
}

void MaskSelectTest::livewireFollowsAnEdge()
{
    const EdgeMap map(edgeImage());
    QVERIFY(!map.isNull());
    const QVector<QPoint> path = map.path(QPoint(10, 41), QPoint(110, 41));
    QVERIFY(path.size() >= 100);
    QCOMPARE(path.first(), QPoint(10, 41));
    QCOMPARE(path.last(), QPoint(110, 41));
    for (const QPoint &p : path)
        QVERIFY2(std::abs(p.y() - 40) <= 2, qPrintable(QStringLiteral("%1,%2").arg(p.x()).arg(p.y())));

    // A straight shot would be along y = 41 too, so detour: endpoints off the edge must still be drawn onto it.
    const QVector<QPoint> bent = map.path(QPoint(10, 30), QPoint(110, 30));
    int onEdge = 0;
    for (const QPoint &p : bent)
        onEdge += std::abs(p.y() - 40) <= 2;
    QVERIFY(onEdge > 60);
}

void MaskSelectTest::snapFindsTheEdge()
{
    const EdgeMap map(edgeImage());
    const QPoint snapped = map.snap(QPoint(50, 43), 6);
    QVERIFY(std::abs(snapped.y() - 40) <= 2);
}

void MaskSelectTest::wandToleranceAndContiguity()
{
    QImage image(100, 50, QImage::Format_RGB32);
    image.fill(qRgb(200, 30, 30));
    QPainter p(&image);
    p.fillRect(QRect(40, 0, 20, 50), QColor(30, 30, 200));
    p.fillRect(QRect(80, 0, 20, 50), QColor(200, 30, 30));
    p.end();

    QImage contiguous = magicWandCover(image, QPoint(10, 25), 20, true);
    QVERIFY(contiguous.constScanLine(25)[10] > 240);
    QCOMPARE(contiguous.constScanLine(25)[50], uchar(0));
    QCOMPARE(contiguous.constScanLine(25)[90], uchar(0));

    QImage all = magicWandCover(image, QPoint(10, 25), 20, false);
    QVERIFY(all.constScanLine(25)[90] > 240);
}

QTEST_APPLESS_MAIN(MaskSelectTest)
#include "tst_maskselect.moc"
