#include <QtTest>

#include "app/editor/sessions/KeyframeCurves.h"
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
    void curveSamplingMatchesEngineEasing();
    void curveRangeAndFitIncludeOvershootAndHandles();
    void curveGridTicksAndTangentHandles();
    void curveDragDeformsSegmentThroughCursor();
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

void KeyframeViewTest::curveSamplingMatchesEngineEasing()
{
    using namespace KeyframeCurves;
    Point a;
    a.seconds = 0;
    a.value = 2;
    a.fn = drift::EasingFn::ElasticOut;
    a.fnAmplitude = 1.0;
    a.fnPeriod = 0.3;
    Point b;
    b.seconds = 1;
    b.value = 12;
    const QVector<QPointF> samples = sampleCurve({a, b}, 0, 0, 1, 0, 1, 0.01);
    QVERIFY(samples.size() > 100);
    double peak = 0;
    for (const QPointF &p : samples) {
        const double expected = a.value + (b.value - a.value) * drift::evaluateEasingFn(a.fn, p.x(), 1.0, 0.3);
        QVERIFY2(qAbs(p.y() - expected) < 1e-3, qPrintable(QString::number(p.x())));
        peak = std::max(peak, p.y());
    }
    QVERIFY(peak > b.value + 0.1);

    b.value = 12;
    a.fn = drift::EasingFn::None;
    a.outDx = 0.5;
    a.outDy = 20;
    for (const QPointF &p : sampleCurve({a, b}, 0, 0, 1, 0, 1, 0.01))
        QVERIFY(qAbs(p.y() - valueAt({a, b}, p.x())) < 1e-3);

    const QVector<QPointF> flat = sampleCurve({}, 7, 1, 3, 0, 5, 0.01);
    QCOMPARE(flat.size(), 2);
    QCOMPARE(flat.first(), QPointF(1, 7));
    QCOMPARE(flat.last(), QPointF(3, 7));

    a.fn = drift::EasingFn::None;
    a.hold = true;
    const QVector<QPointF> step = sampleCurve({a, b}, 0, 0, 1, 0, 1, 0.01);
    QVERIFY(step.contains(QPointF(1, a.value)));
    QVERIFY(step.contains(QPointF(1, b.value)));
}

void KeyframeViewTest::curveRangeAndFitIncludeOvershootAndHandles()
{
    using namespace KeyframeCurves;
    Point a;
    a.seconds = 0;
    a.value = 0;
    a.fn = drift::EasingFn::ElasticOut;
    Point b;
    b.seconds = 1;
    b.value = 10;
    Point straight = a;
    straight.fn = drift::EasingFn::None;
    const Range plain = valueRange(QStringLiteral("x"), {straight, b}, 0, false, 0, 0);
    const Range eased = valueRange(QStringLiteral("x"), {a, b}, 0, false, 0, 0);
    QVERIFY(eased.max > plain.max);
    QCOMPARE(valueRange(QStringLiteral("opacity"), {a, b}, 0, false, 0, 0).max, 1.0);
    QCOMPARE(valueRange(QStringLiteral("fx.0.amount"), {}, 0, true, -5, 5).min, -5.0);
    const Range empty = valueRange(QStringLiteral("x"), {}, 100, false, 0, 0);
    QCOMPARE(empty.min, 90.0);
    QCOMPARE(empty.max, 110.0);

    Series s;
    s.prop = QStringLiteral("x");
    Point lo;
    lo.seconds = 0;
    lo.value = 0;
    lo.outDy = 30;
    Point hi;
    hi.seconds = 1;
    hi.value = 10;
    s.points = {lo, hi};
    Series flat;
    flat.prop = QStringLiteral("y");
    flat.baseValue = -50;

    Range r;
    QVERIFY(fitRange({s}, {}, false, &r));
    QVERIFY(qAbs(r.min - (0 - 3.0)) < 1e-9);
    QVERIFY(qAbs(r.max - (30 + 3.0)) < 1e-9);
    QVERIFY(fitRange({s, flat}, {}, false, &r));
    QVERIFY(r.min < -50);
    QVERIFY(fitRange({s, flat}, {}, true, &r) == false);
    const auto onlyHigh = [](const QString &prop, double seconds) { return prop == QLatin1String("x") && seconds == 1.0; };
    QVERIFY(fitRange({s, flat}, onlyHigh, true, &r));
    QVERIFY(qAbs(r.min - 9.0) < 1e-9 && qAbs(r.max - 11.0) < 1e-9);

    const Range padded = padRange(5, 5);
    QCOMPARE(padded.min, 4.0);
    QCOMPARE(padded.max, 6.0);
}

void KeyframeViewTest::curveGridTicksAndTangentHandles()
{
    using namespace KeyframeCurves;
    const QList<Tick> ticks = gridTicks({0, 1}, true);
    QVERIFY(ticks.size() >= 4);
    QCOMPARE(ticks.first().text, QStringLiteral("0"));
    QCOMPARE(ticks.first().frac, 0.0);
    QCOMPARE(gridTicks({0, 1}, false).size(), 5);
    QVERIFY(gridTicks({0, 1}, false).first().text.isEmpty());

    Point a;
    Point b;
    b.seconds = 1;
    Point c;
    c.seconds = 2;
    c.fn = drift::EasingFn::BounceOut;
    QCOMPARE(tangentHandles({a, b, c}).size(), 4);
    b.hold = true;
    QCOMPARE(tangentHandles({a, b, c}).size(), 2);
    QCOMPARE(handleSeconds(a, true), kDefaultHandleSeconds);
    QCOMPARE(handleSeconds(a, false), -kDefaultHandleSeconds);
    a.outDx = 0.4;
    a.outDy = 3;
    QCOMPARE(handleSeconds(a, true), 0.4);
    QCOMPARE(handleValue(a, true), a.value + 3);
}

void KeyframeViewTest::curveDragDeformsSegmentThroughCursor()
{
    using namespace KeyframeCurves;
    Point a;
    Point b;
    b.seconds = 1;
    b.value = 10;
    // A straight segment gets chord handles, so it still evaluates as the line it was.
    QCOMPARE(segmentParameter(a, b, 0.25), 0.25);
    Point sa = a;
    Point sb = b;
    deformSegment(sa, sb, 0.5, 0, 0);
    QVERIFY(std::abs(segmentValue(sa, sb, 0.25) - 2.5) < 1e-6);

    // The dragged point follows the cursor: half way, 4 units up.
    deformSegment(a, b, 0.5, 0, 4);
    QVERIFY(std::abs(segmentValue(a, b, 0.5) - 9) < 1e-3);
    QVERIFY(a.outDx >= 0 && a.outDx <= 1);
    QVERIFY(b.inDx <= 0 && b.inDx >= -1);

    // A smooth key turns its far handle to the same slope; a corner leaves it.
    Point k;
    k.inDx = -0.2;
    k.inDy = 0;
    const Point smooth = withHandle(k, true, 0.4, 2);
    QCOMPARE(smooth.outDy, 2.0);
    QCOMPARE(smooth.inDy, -1.0);
    k.corner = true;
    QCOMPARE(withHandle(k, true, 0.4, 2).inDy, 0.0);
    QCOMPARE(withHandle(k, true, -0.4, 2).outDx, 0.0);
}

QTEST_APPLESS_MAIN(KeyframeViewTest)
#include "tst_keyframeview.moc"
