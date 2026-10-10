#include <QtTest>

#include "core/PdfSource.h"

using namespace drift;

namespace {

PdfSource letterDoc(int pages)
{
    PdfSource s;
    s.path = QStringLiteral("a.pdf");
    s.pageCount = pages;
    s.pageSizes = QVector<QSizeF>(pages, QSizeF(100, 200));
    s.gap = 0.0;
    return s;
}

} // namespace

class PdfSourceTest : public QObject
{
    Q_OBJECT

private slots:
    void columnFitsWidth()
    {
        const PdfSource s = letterDoc(3);
        const auto p = layoutPdfPages(s, QSizeF(100, 300));
        QCOMPARE(p.size(), 2);
        QCOMPARE(p.at(0).pageIndex, 0);
        QCOMPARE(p.at(0).rect, QRectF(0, 0, 100, 200));
        QCOMPARE(p.at(1).rect, QRectF(0, 200, 100, 200));
    }

    void columnZoomMultipliesScale()
    {
        PdfSource s = letterDoc(3);
        s.zoom = 2.0;
        const auto p = layoutPdfPages(s, QSizeF(100, 300));
        QCOMPARE(p.at(0).rect, QRectF(0, 0, 200, 400));
    }

    void columnScrollWholePages()
    {
        PdfSource s = letterDoc(5);
        s.gap = 10.0; // 10 pt on a 100 pt page scaled 1:1
        s.scrollY = 2.0;
        const auto p = layoutPdfPages(s, QSizeF(100, 300));
        QCOMPARE(p.first().pageIndex, 2);
        QCOMPARE(p.first().rect, QRectF(0, 0, 100, 200));
        QCOMPARE(p.at(1).pageIndex, 3);
        QCOMPARE(p.at(1).rect.top(), 210.0);
    }

    void columnScrollFraction()
    {
        PdfSource s = letterDoc(5);
        s.gap = 10.0;
        s.scrollY = 1.5;
        const auto p = layoutPdfPages(s, QSizeF(100, 300));
        // page 1 starts at 210, half of (200 + 10) scrolled away
        QCOMPARE(p.first().pageIndex, 1);
        QCOMPARE(p.first().rect.top(), -105.0);
        QCOMPARE(p.at(1).pageIndex, 2);
        QCOMPARE(p.at(1).rect.top(), 105.0);
    }

    void columnScrollPastEndExtrapolates()
    {
        PdfSource s = letterDoc(2);
        s.scrollY = 3.0;
        QVERIFY(layoutPdfPages(s, QSizeF(100, 100)).isEmpty());
        s.scrollY = 1.5;
        const auto p = layoutPdfPages(s, QSizeF(100, 300));
        QCOMPARE(p.size(), 1);
        QCOMPARE(p.first().rect.top(), -100.0);
    }

    void columnMixedSizes()
    {
        PdfSource s = letterDoc(2);
        s.pageSizes[1] = QSizeF(200, 200); // wider page scales down to the common width
        const auto p = layoutPdfPages(s, QSizeF(100, 400));
        QCOMPARE(p.size(), 2);
        QCOMPARE(p.at(1).rect, QRectF(0, 200, 100, 100));
    }

    void rowFitsHeight()
    {
        PdfSource s = letterDoc(3);
        s.layout = PdfLayout::Row;
        const auto p = layoutPdfPages(s, QSizeF(100, 100));
        QCOMPARE(p.size(), 2);
        QCOMPARE(p.at(0).rect, QRectF(0, 0, 50, 100));
        QCOMPARE(p.at(1).rect, QRectF(50, 0, 50, 100));
    }

    void gridFitsWidth()
    {
        PdfSource s = letterDoc(7);
        s.layout = PdfLayout::Grid;
        s.gridColumns = 3;
        const auto p = layoutPdfPages(s, QSizeF(300, 400));
        QCOMPARE(p.size(), 6);
        QCOMPARE(p.at(0).rect, QRectF(0, 0, 100, 200));
        QCOMPARE(p.at(2).rect.right(), 300.0);
        QCOMPARE(p.at(3).rect.top(), 200.0);
    }

    void gridGapCountsTowardWidth()
    {
        PdfSource s = letterDoc(4);
        s.layout = PdfLayout::Grid;
        s.gridColumns = 2;
        s.gap = 20.0;
        const auto p = layoutPdfPages(s, QSizeF(220, 1000));
        QCOMPARE(p.at(1).rect.right(), 220.0);
        QCOMPARE(p.at(1).rect.left(), 120.0);
    }

    void pageRangeClamps()
    {
        PdfSource s = letterDoc(4);
        s.firstPage = 2;
        s.lastPage = 99;
        auto p = layoutPdfPages(s, QSizeF(100, 1000));
        QCOMPARE(p.size(), 3);
        QCOMPARE(p.first().pageIndex, 1);
        QCOMPARE(p.last().pageIndex, 3);
        s.lastPage = 3;
        QCOMPARE(layoutPdfPages(s, QSizeF(100, 1000)).size(), 2);
        s.firstPage = 9;
        QVERIFY(layoutPdfPages(s, QSizeF(100, 1000)).isEmpty());
    }

    void unprobedIsEmpty()
    {
        PdfSource s = letterDoc(0);
        QVERIFY(layoutPdfPages(s, QSizeF(100, 100)).isEmpty());
        s = letterDoc(3);
        s.pageSizes.clear();
        QVERIFY(layoutPdfPages(s, QSizeF(100, 100)).isEmpty());
    }

    void jsonRoundTrip()
    {
        PdfSource s = letterDoc(3);
        s.layout = PdfLayout::Grid;
        s.gridColumns = 4;
        s.firstPage = 2;
        s.zoom = 3.0;
        KeyframeTrack<double> track;
        track.setKeyframe(0, 0.0);
        track.setKeyframe(1000000, 2.0);
        s.keyframes.insert(QStringLiteral("scrollY"), track);
        const PdfSource r = PdfSource::fromJson(s.toJson());
        QCOMPARE(r.pageSizes, s.pageSizes);
        QCOMPARE(r.layout, PdfLayout::Grid);
        QCOMPARE(r.gridColumns, 4);
        QCOMPARE(r.zoom, 3.0);
        QVERIFY(r.isAnimated());
        QCOMPARE(r.resolvedAt(500000).scrollY, 1.0);
    }
};

QTEST_APPLESS_MAIN(PdfSourceTest)
#include "tst_pdfsource.moc"
