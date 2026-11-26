#include <QtTest>

#include "core/text/TextStyle.h"
#include "core/text/TitleTemplate.h"

class TitleTemplatesTest : public QObject
{
    Q_OBJECT

private slots:
    void shipsTheBuiltInTemplates();
    void instantiatesBackToFrontOnTheCanvas();
    void textOverridesReplacePlaceholders();
    void clipsAreLooselyLinked();
    void creditsTemplateRolls();
    void scrollRollsBottomToTop();
    void scrollCrawlsAndSettles();
    void scrollSurvivesJson();
};

void TitleTemplatesTest::shipsTheBuiltInTemplates()
{
    const QList<drift::TitleTemplate> all = drift::TitleTemplateCatalog::instance().templates();
    QVERIFY2(all.size() >= 12, qPrintable(QString::number(all.size())));
    QSet<QString> ids;
    for (const drift::TitleTemplate &t : all) {
        QVERIFY2(!ids.contains(t.id), qPrintable(t.id));
        ids.insert(t.id);
        QVERIFY(!t.label.isEmpty());
        QVERIFY(t.durationSec > 0.0);
        QVERIFY2(!t.fields().isEmpty(), qPrintable(t.id));
    }
}

void TitleTemplatesTest::instantiatesBackToFrontOnTheCanvas()
{
    constexpr int kW = 1920;
    constexpr int kH = 1080;
    for (const drift::TitleTemplate &t : drift::TitleTemplateCatalog::instance().templates()) {
        const QList<drift::Clip> clips = drift::instantiateTitleTemplate(t, kW, kH, 2'000'000);
        QCOMPARE(clips.size(), t.elements.size());
        for (const drift::Clip &c : clips) {
            QVERIFY(c.type == drift::ClipType::Text || c.type == drift::ClipType::Shape);
            QVERIFY(c.timelineStart >= 2'000'000);
            QVERIFY(c.timelineEnd() <= 2'000'000 + drift::secondsToUs(t.durationSec) + 1);
            QVERIFY(c.timelineDuration > 0);
            const double cx = c.positionX.evaluateAt(0);
            const double cy = c.positionY.evaluateAt(0);
            QVERIFY2(cx > 0 && cx < kW && cy > 0 && cy < kH, qPrintable(t.id));
        }
    }
}

void TitleTemplatesTest::textOverridesReplacePlaceholders()
{
    const auto tpl = drift::TitleTemplateCatalog::instance().templateForId(QStringLiteral("lt-accent-bar"));
    QVERIFY(tpl);
    const QList<drift::Clip> clips =
        drift::instantiateTitleTemplate(*tpl, 1920, 1080, 0, {{QStringLiteral("name"), QStringLiteral("Ada Lovelace")}});
    QStringList texts;
    for (const drift::Clip &c : clips) {
        if (c.type == drift::ClipType::Text)
            texts.append(c.textContent);
    }
    QVERIFY(texts.contains(QStringLiteral("Ada Lovelace")));
    QVERIFY(texts.contains(QStringLiteral("Creative Director")));
}

void TitleTemplatesTest::clipsAreLooselyLinked()
{
    const auto tpl = drift::TitleTemplateCatalog::instance().templateForId(QStringLiteral("lt-solid-block"));
    QVERIFY(tpl);
    const QList<drift::Clip> clips = drift::instantiateTitleTemplate(*tpl, 1280, 720, 0);
    QVERIFY(clips.size() > 1);
    QSet<QString> ids;
    for (const drift::Clip &c : clips) {
        QVERIFY(!c.linkId.isEmpty());
        QCOMPARE(c.linkId, clips.first().linkId);
        QVERIFY(c.linkLoose);
        ids.insert(c.id);
    }
    QCOMPARE(ids.size(), clips.size());
}

void TitleTemplatesTest::creditsTemplateRolls()
{
    const auto tpl = drift::TitleTemplateCatalog::instance().templateForId(QStringLiteral("credits-roll"));
    QVERIFY(tpl);
    const QList<drift::Clip> clips = drift::instantiateTitleTemplate(*tpl, 1920, 1080, 0);
    QCOMPARE(clips.size(), 1);
    QCOMPARE(clips.first().textStyle.scroll, drift::TextScroll::RollUp);
}

void TitleTemplatesTest::scrollRollsBottomToTop()
{
    drift::TextStyle s;
    s.scroll = drift::TextScroll::RollUp;
    const QRectF rest(300, 100, 1200, 2000); // taller than the canvas
    const QSizeF canvas(1920, 1080);
    const drift::TimeUs dur = 10'000'000;

    // Start: the top edge sits on the canvas bottom.
    const QPointF start = drift::textScrollOffset(s, rest, canvas, 0, dur);
    QCOMPARE(start.x(), 0.0);
    QCOMPARE(rest.top() + start.y(), 1080.0);
    // End: the bottom edge has just left the top.
    const QPointF end = drift::textScrollOffset(s, rest, canvas, dur, dur);
    QCOMPARE(rest.bottom() + end.y(), 0.0);
    // Linear and exact: the midpoint is the mean, and scrubbing past the ends clamps.
    const QPointF mid = drift::textScrollOffset(s, rest, canvas, dur / 2, dur);
    QVERIFY(qAbs(mid.y() - (start.y() + end.y()) / 2) < 1e-6);
    QCOMPARE(drift::textScrollOffset(s, rest, canvas, dur * 3, dur), end);
    QCOMPARE(drift::textScrollOffset(s, rest, canvas, -5, dur), start);

    s.scroll = drift::TextScroll::None;
    QCOMPARE(drift::textScrollOffset(s, rest, canvas, dur / 2, dur), QPointF());
}

void TitleTemplatesTest::scrollCrawlsAndSettles()
{
    drift::TextStyle s;
    s.scroll = drift::TextScroll::CrawlLeft;
    const QRectF rest(-500, 900, 3000, 100);
    const QSizeF canvas(1920, 1080);
    const drift::TimeUs dur = 4'000'000;

    QPointF p = drift::textScrollOffset(s, rest, canvas, 0, dur);
    QCOMPARE(rest.left() + p.x(), 1920.0);
    QCOMPARE(p.y(), 0.0);
    p = drift::textScrollOffset(s, rest, canvas, dur, dur);
    QCOMPARE(rest.right() + p.x(), 0.0);

    s.scroll = drift::TextScroll::CrawlRight;
    p = drift::textScrollOffset(s, rest, canvas, 0, dur);
    QCOMPARE(rest.right() + p.x(), 0.0);
    p = drift::textScrollOffset(s, rest, canvas, dur, dur);
    QCOMPARE(rest.left() + p.x(), 1920.0);

    // Exit on screen: it comes to rest where the frame lays the text out.
    s.scroll = drift::TextScroll::RollUp;
    s.scrollExitOffscreen = false;
    p = drift::textScrollOffset(s, QRectF(0, 200, 800, 400), canvas, dur, dur);
    QCOMPARE(p, QPointF());
}

void TitleTemplatesTest::scrollSurvivesJson()
{
    drift::TextStyle s;
    s.scroll = drift::TextScroll::CrawlRight;
    s.scrollEnterOffscreen = false;
    const drift::TextStyle back = drift::textStyleFromJson(drift::textStyleToJson(s));
    QCOMPARE(back.scroll, drift::TextScroll::CrawlRight);
    QVERIFY(!back.scrollEnterOffscreen);
    QVERIFY(back.scrollExitOffscreen);
    QVERIFY(back.isCrawl());
    QVERIFY(!back.wraps());

    // A style that does not scroll writes nothing extra.
    QVERIFY(!drift::textStyleToJson(drift::TextStyle{}).contains(QStringLiteral("scroll")));
}

QTEST_MAIN(TitleTemplatesTest)
#include "tst_titletemplates.moc"
