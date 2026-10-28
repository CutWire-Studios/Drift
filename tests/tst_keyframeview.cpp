#include <QtTest>

#include "app/editor/sessions/KeyframeRows.h"
#include "app/editor/sessions/KeyframeSelection.h"
#include "app/editor/sessions/KeyframeTimeView.h"

using namespace KeyframeRows;

namespace {

QVariantMap ref(const QString &prop, double seconds)
{
    return makeRef(prop, seconds);
}

QVariantMap key(double seconds, bool hold = false)
{
    return {{QStringLiteral("seconds"), seconds}, {QStringLiteral("value"), seconds * 10}, {QStringLiteral("hold"), hold}};
}

QVariantMap entry(const QString &prop, const QString &group, bool animated, bool enabled = true)
{
    return {{QStringLiteral("prop"), prop},
            {QStringLiteral("keyProp"), prop},
            {QStringLiteral("label"), prop.toUpper()},
            {QStringLiteral("group"), group},
            {QStringLiteral("groupLabel"), group.toUpper()},
            {QStringLiteral("kind"), QStringLiteral("scalar")},
            {QStringLiteral("tracks"), QStringList{prop}},
            {QStringLiteral("animated"), animated},
            {QStringLiteral("enabled"), enabled}};
}

Options optionsFor(const QHash<QString, QVariantList> &keys)
{
    Options o;
    o.keysFor = [keys](const QString &track) { return keys.value(track); };
    return o;
}

} // namespace

class KeyframeViewTest : public QObject
{
    Q_OBJECT

private slots:
    void selectionDedupesByMicrosecond();
    void selectionToggleSpanAndProps();
    void selectionPressRules();
    void timeViewZoomKeepsTheAnchor();
    void timeViewFitRangePads();
    void unionKeysMergesWithinEpsilon();
    void buildGroupsRowsAndHidesStaticTransformKey();
    void buildMergesLinkedSizeIntoScale();
    void buildKeepsCollapsedGroupSummary();
    void laneDotsSummariseGroups();
    void refsForRowAtHonoursTolerance();
    void prevAndNextKeyTime();
    void hitTestPicksNearestDot();
    void marqueeCollectsDotsInBox();
};

void KeyframeViewTest::selectionDedupesByMicrosecond()
{
    KeyframeSelection sel;
    QSignalSpy spy(&sel, &KeyframeSelection::revisionChanged);
    sel.replace({ref(QStringLiteral("x"), 1.0), ref(QStringLiteral("x"), 1.0000001), ref(QStringLiteral("y"), 1.0)});
    QCOMPARE(sel.count(), 2);
    QVERIFY(sel.has(QStringLiteral("x"), 1.0));
    QVERIFY(!sel.has(QStringLiteral("x"), 1.1));
    QCOMPARE(spy.count(), 1);
    sel.clear();
    sel.clear();
    QCOMPARE(sel.count(), 0);
    QCOMPARE(spy.count(), 2);
}

void KeyframeViewTest::selectionToggleSpanAndProps()
{
    KeyframeSelection sel;
    QVERIFY(!sel.span().isValid());
    sel.add({ref(QStringLiteral("x"), 2.0), ref(QStringLiteral("y"), 0.5)});
    sel.toggle(ref(QStringLiteral("x"), 3.0));
    sel.toggle(ref(QStringLiteral("y"), 0.5));
    QCOMPARE(sel.count(), 2);
    const QVariantMap span = sel.span().toMap();
    QCOMPARE(span.value(QStringLiteral("start")).toDouble(), 2.0);
    QCOMPARE(span.value(QStringLiteral("end")).toDouble(), 3.0);
    QCOMPARE(sel.props(), QStringList{QStringLiteral("x")});
    sel.remove({ref(QStringLiteral("x"), 2.0)});
    QCOMPARE(sel.count(), 1);
}

void KeyframeViewTest::selectionPressRules()
{
    KeyframeSelection sel;
    const QVariantList hit{ref(QStringLiteral("x"), 1.0)};
    QCOMPARE(sel.press(Qt::NoModifier, hit), 1);
    QVERIFY(sel.hasAll(hit));
    QCOMPARE(sel.press(Qt::NoModifier, hit), 2);
    sel.add({ref(QStringLiteral("x"), 2.0)});
    QCOMPARE(sel.press(Qt::ControlModifier, hit), 0);
    QVERIFY(!sel.hasAll(hit));
    QCOMPARE(sel.count(), 1);
    QCOMPARE(sel.press(Qt::ShiftModifier, hit), 1);
    QCOMPARE(sel.count(), 2);
    QVERIFY(!sel.hasAll({}));
}

void KeyframeViewTest::timeViewZoomKeepsTheAnchor()
{
    KeyframeTimeView view;
    view.setPxPerSecond(100);
    view.setContentX(50);
    const double anchor = view.secondsFor(200);
    view.zoomAt(200, 2.0);
    QCOMPARE(view.pxPerSecond(), 200.0);
    QVERIFY(qAbs(view.secondsFor(200) - anchor) < 1e-9);
    view.zoomAt(0, 1e9);
    QCOMPARE(view.pxPerSecond(), view.maxPxPerSecond());
    QCOMPARE(view.xFor(1.0), view.pxPerSecond() - view.contentX());
}

void KeyframeViewTest::timeViewFitRangePads()
{
    KeyframeTimeView view;
    view.setViewportWidth(1100);
    view.setClipStart(2.0);
    view.setClipDuration(10.0);
    view.fitClip();
    QVERIFY(qAbs(view.pxPerSecond() - 1100.0 / 11.0) < 1e-9);
    QVERIFY(qAbs(view.xFor(2.0) - 0.5 * view.pxPerSecond()) < 1e-9);
}

void KeyframeViewTest::unionKeysMergesWithinEpsilon()
{
    const QVariantList merged = unionKeys({{key(2.0), key(0.5)}, {key(2.00005), key(1.0)}});
    QCOMPARE(merged.size(), 3);
    QCOMPARE(secondsOf(merged.at(0)), 0.5);
    QCOMPARE(secondsOf(merged.at(2)), 2.0);
    QCOMPARE(unionKeys({{key(1.0), key(1.0 + 1.0 / 30.0)}}).size(), 2);
}

void KeyframeViewTest::buildGroupsRowsAndHidesStaticTransformKey()
{
    const QVariantList entries{entry(QStringLiteral("x"), QStringLiteral("transform"), false),
                               entry(QStringLiteral("opacity"), QStringLiteral("transform"), true),
                               entry(QStringLiteral("volume"), QStringLiteral("audio"), false)};
    Options o = optionsFor({{QStringLiteral("x"), {key(0.0)}},
                            {QStringLiteral("opacity"), {key(0.0), key(1.0)}}});
    const QList<Row> rows = build(entries, o);
    QCOMPARE(rows.size(), 5);
    QVERIFY(rows.at(0).isGroup());
    QCOMPARE(rows.at(0).count, 2);
    QCOMPARE(rows.at(1).prop, QStringLiteral("x"));
    QVERIFY(rows.at(1).keys.isEmpty());
    QCOMPARE(rows.at(2).keys.size(), 2);
    QCOMPARE(rows.at(3).rowKey(), QStringLiteral("g:audio"));

    o.animatedOnly = true;
    const QList<Row> animated = build(entries, o);
    QCOMPARE(animated.size(), 2);
    QCOMPARE(animated.at(1).prop, QStringLiteral("opacity"));
}

void KeyframeViewTest::buildMergesLinkedSizeIntoScale()
{
    const QVariantList entries{entry(QStringLiteral("width"), QStringLiteral("transform"), true),
                               entry(QStringLiteral("height"), QStringLiteral("transform"), false)};
    Options o = optionsFor({{QStringLiteral("width"), {key(1.0)}}, {QStringLiteral("height"), {key(1.0), key(2.0)}}});
    o.sizeLinked = true;
    o.scaleLabel = QStringLiteral("Scale");
    const QList<Row> rows = build(entries, o);
    QCOMPARE(rows.size(), 2);
    const Row &scale = rows.at(1);
    QCOMPARE(scale.type, QStringLiteral("scale"));
    QCOMPARE(scale.tracks, (QStringList{QStringLiteral("width"), QStringLiteral("height")}));
    QVERIFY(scale.animated);
    QCOMPARE(scale.keys.size(), 2);
    const QVariantMap map = scale.toMap();
    QCOMPARE(map.value(QStringLiteral("keyProp")).toString(), QStringLiteral("width"));
    QCOMPARE(map.value(QStringLiteral("entries")).toList().size(), 2);
}

void KeyframeViewTest::buildKeepsCollapsedGroupSummary()
{
    const QVariantList entries{entry(QStringLiteral("opacity"), QStringLiteral("transform"), true)};
    Options o = optionsFor({{QStringLiteral("opacity"), {key(0.0), key(1.0)}}});
    o.collapsed.insert(QStringLiteral("transform"));
    const QList<Row> rows = build(entries, o);
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows.first().count, 2);
    QVERIFY(rows.first().collapsed);
    QCOMPARE(laneDots(rows, 0).size(), 2);
    QCOMPARE(allKeyTimes(rows), (QList<double>{0.0, 1.0}));
}

void KeyframeViewTest::laneDotsSummariseGroups()
{
    const QVariantList entries{entry(QStringLiteral("opacity"), QStringLiteral("transform"), true),
                               entry(QStringLiteral("rotation"), QStringLiteral("transform"), true)};
    const Options o = optionsFor({{QStringLiteral("opacity"), {key(1.0), key(2.0, true)}},
                                  {QStringLiteral("rotation"), {key(1.00005)}}});
    const QList<Row> rows = build(entries, o);
    const QVariantList group = laneDots(rows, 0);
    QCOMPARE(group.size(), 2);
    QCOMPARE(group.at(0).toMap().value(QStringLiteral("refs")).toList().size(), 2);
    const QVariantList own = laneDots(rows, 1);
    QCOMPARE(own.size(), 2);
    QVERIFY(own.at(1).toMap().value(QStringLiteral("hold")).toBool());
    QVERIFY(laneDots(rows, 9).isEmpty());
}

void KeyframeViewTest::refsForRowAtHonoursTolerance()
{
    const QVariantList entries{entry(QStringLiteral("opacity"), QStringLiteral("transform"), true)};
    const QList<Row> rows = build(entries, optionsFor({{QStringLiteral("opacity"), {key(1.0)}}}));
    const Row &row = rows.at(1);
    QVERIFY(refsForRowAt(row, 1.01).isEmpty());
    QCOMPARE(refsForRowAt(row, 1.01, 0.02).size(), 1);
    QCOMPARE(secondsOf(QVariant(refsForRowAt(row, 1.00001).first())), 1.0);
}

void KeyframeViewTest::prevAndNextKeyTime()
{
    const QList<double> times{0.5, 1.0, 3.0};
    QCOMPARE(prevKeyTime(times, 1.0), 0.5);
    QCOMPARE(nextKeyTime(times, 1.0), 3.0);
    QCOMPARE(prevKeyTime(times, 0.5), -1.0);
    QCOMPARE(nextKeyTime(times, 3.0), -1.0);
}

void KeyframeViewTest::hitTestPicksNearestDot()
{
    const QVariantList entries{entry(QStringLiteral("opacity"), QStringLiteral("transform"), true)};
    const QList<Row> rows = build(entries, optionsFor({{QStringLiteral("opacity"), {key(1.0), key(2.0)}}}));
    Axis axis;
    axis.pxPerSecond = 100;
    axis.contentX = 0;
    const QVariantMap hit = hitTest(rows, axis, 103, 40 + 5, 0, 40, 6);
    QCOMPARE(hit.value(QStringLiteral("row")).toInt(), 1);
    QCOMPARE(hit.value(QStringLiteral("seconds")).toDouble(), 1.0);
    QCOMPARE(hit.value(QStringLiteral("refs")).toList().size(), 1);
    const QVariantMap miss = hitTest(rows, axis, 150, 45, 0, 40, 6);
    QVERIFY(!miss.value(QStringLiteral("dot")).isValid());
    QCOMPARE(miss.value(QStringLiteral("seconds")).toDouble(), 1.5);
    QCOMPARE(hitTest(rows, axis, 10, 500, 0, 40, 6).value(QStringLiteral("row")).toInt(), -1);
}

void KeyframeViewTest::marqueeCollectsDotsInBox()
{
    const QVariantList entries{entry(QStringLiteral("opacity"), QStringLiteral("transform"), true),
                               entry(QStringLiteral("rotation"), QStringLiteral("transform"), true)};
    const QList<Row> rows = build(entries, optionsFor({{QStringLiteral("opacity"), {key(1.0), key(2.0)}},
                                                       {QStringLiteral("rotation"), {key(1.0)}}}));
    Axis axis;
    const QVariantList base{ref(QStringLiteral("z"), 9.0)};
    const QVariantList refs = marqueeRefs(rows, axis, 50, 150, 45, 125, 0, 40, base);
    QCOMPARE(refs.size(), 3);
    QCOMPARE(marqueeRefs(rows, axis, 150, 250, 45, 85, 0, 40, {}).size(), 1);
}

QTEST_APPLESS_MAIN(KeyframeViewTest)
#include "tst_keyframeview.moc"
