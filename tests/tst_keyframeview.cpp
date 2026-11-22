#include <QtTest>

#include "app/editor/sessions/KeyframeCurves.h"
#include "app/editor/sessions/KeyframeInteraction.h"
#include "app/editor/sessions/KeyframeRows.h"
#include "app/editor/sessions/KeyframeSelection.h"
#include "app/editor/sessions/KeyframeTimeView.h"

using namespace KeyframeRows;

// The app wiring is KeyframeInteractionBinding.cpp, which needs the models and the project; these tests
// feed the machine its scene and ops directly.
void KeyframeInteraction::attach(QObject *, QObject *, QObject *, QObject *) {}

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


using drift::manip::DragSession;

// A KeyframeInteraction on plain data: fake project ops that record what the machine asks for, and a
// DragSession whose hooks count begin, commit and cancel.
struct Rig
{
    explicit Rig(bool curves)
    {
        it.setCurves(curves);
        it.setProperty("rowHeight", 40.0);
        it.setProperty("viewWidth", 600.0);
        it.setProperty("viewHeight", 300.0);
        it.setProperty("fps", 10.0);
        it.setProperty("playhead", 1.2);
        it.setSelection(&selection);
        session.setHooks({[this](const QString &) { ++begins; }, [this] { ++commits; }, [this] { ++cancels; }});
        it.setDragSession(&session);

        scene.axis.pxPerSecond = 100;
        scene.clipStart = 0;
        scene.clipDuration = 10;
        scene.graphHeight = 100;
        if (curves) {
            KeyframeCurves::Series s;
            s.prop = QStringLiteral("x");
            s.rowProp = QStringLiteral("x");
            s.label = QStringLiteral("X");
            s.rowIndex = 0;
            s.valueMin = 0;
            s.valueMax = 10;
            KeyframeCurves::Point a;
            a.seconds = 1;
            a.value = 5;
            KeyframeCurves::Point b;
            b.seconds = 2;
            b.value = 5;
            b.inDx = -0.5;
            b.inDy = -1;
            b.outDx = 0.5;
            b.outDy = 1;
            KeyframeCurves::Point c;
            c.seconds = 3;
            c.value = 5;
            s.points = {a, b, c};
            scene.series = {s};
            scene.focusedIndex = 0;
        } else {
            const QVariantList entries{entry(QStringLiteral("opacity"), QStringLiteral("transform"), true)};
            scene.rows = build(entries, optionsFor({{QStringLiteral("opacity"), {key(1.0), key(2.0), key(3.0)}}}));
        }

        KeyframeInteraction::Ops ops;
        ops.scene = [this](KeyframeInteraction::Scene &out) { out = scene; };
        ops.moveKeys = [this](const QVariantList &refs, double dt) {
            lastMoveRefs = refs;
            lastDt = dt;
            ++moves;
            QVariantList out;
            for (const QVariant &r : refs) {
                const QVariantMap m = r.toMap();
                out.append(ref(m.value(QStringLiteral("prop")).toString(), m.value(QStringLiteral("seconds")).toDouble() + dt));
            }
            return out;
        };
        ops.stretchKeys = [this](const QVariantList &refs, double pivot, double factor) {
            stretchPivot = pivot;
            stretchFactor = factor;
            return refs;
        };
        ops.setTangents = [this](const QString &, double, double inDx, double inDy, double outDx, double outDy, bool corner) {
            tangents = {inDx, inDy, outDx, outDy};
            tangentCorner = corner;
            ++tangentWrites;
        };
        ops.removeKeys = [this](const QVariantList &refs) { removed = refs; };
        ops.snapTime = [this](double seconds, const QVariantList &, bool noSnap) {
            const double framed = std::round(seconds * 2) / 2;
            if (!noSnap && std::abs(seconds - 1.2) < 0.1)
                return 1.2;
            return framed;
        };
        it.setOps(ops);
    }

    bool press(double x, double y, int device = int(DragSession::Device::Mouse), int mods = 0)
    {
        return it.press(x, y, Qt::LeftButton, mods, device);
    }
    void release(double x, double y, int mods = 0) { it.release(x, y, Qt::LeftButton, mods); }

    KeyframeInteraction it;
    KeyframeSelection selection;
    DragSession session;
    KeyframeInteraction::Scene scene;
    int begins = 0;
    int commits = 0;
    int cancels = 0;
    int moves = 0;
    double lastDt = 0;
    QVariantList lastMoveRefs;
    double stretchPivot = -1;
    double stretchFactor = -1;
    QList<double> tangents;
    bool tangentCorner = false;
    int tangentWrites = 0;
    QVariantList removed;
};

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
    void interactionHitReachFollowsTheDevice();
    void interactionDragSnapsAndCtrlSuspendsIt();
    void interactionShiftLocksTheDominantAxis();
    void interactionTangentRules();
    void interactionMarqueeAndAutoscroll();
    void interactionNudgeBurstIsOneUndoStep();
    void interactionEscRestores();
    void interactionStretchBox();
    void interactionTouchFlows();
    void interactionTypedEntryAndKeyboard();
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
    const QVariantList entries{entry(QStringLiteral("scaleX"), QStringLiteral("transform"), true),
                               entry(QStringLiteral("scaleY"), QStringLiteral("transform"), false)};
    Options o = optionsFor({{QStringLiteral("scaleX"), {key(1.0)}}, {QStringLiteral("scaleY"), {key(1.0), key(2.0)}}});
    o.sizeLinked = true;
    o.scaleLabel = QStringLiteral("Scale");
    const QList<Row> rows = build(entries, o);
    QCOMPARE(rows.size(), 2);
    const Row &scale = rows.at(1);
    QCOMPARE(scale.type, QStringLiteral("scale"));
    QCOMPARE(scale.tracks, (QStringList{QStringLiteral("scaleX"), QStringLiteral("scaleY")}));
    QVERIFY(scale.animated);
    QCOMPARE(scale.keys.size(), 2);
    const QVariantMap map = scale.toMap();
    QCOMPARE(map.value(QStringLiteral("keyProp")).toString(), QStringLiteral("scaleX"));
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
}


void KeyframeViewTest::interactionHitReachFollowsTheDevice()
{
    Rig mouse(true);
    // A key sits at (200, 50). 12 px away a mouse press lands on it, 20 px away it does not.
    mouse.press(212, 50);
    QVERIFY(mouse.selection.has(QStringLiteral("x"), 2.0));
    mouse.release(212, 50);
    mouse.selection.clear();
    mouse.press(220, 50);
    QVERIFY(!mouse.selection.has(QStringLiteral("x"), 2.0));
    mouse.release(220, 50);

    // A finger reaches 24 px, and a tap selects what it hits.
    Rig touch(true);
    touch.press(220, 50, int(DragSession::Device::Touch));
    touch.release(220, 50);
    QVERIFY(touch.selection.has(QStringLiteral("x"), 2.0));
    touch.selection.clear();
    touch.press(230, 50, int(DragSession::Device::Touch));
    touch.release(230, 50);
    QCOMPARE(touch.selection.count(), 0);

    Rig lanes(false);
    lanes.press(108, 45);
    QVERIFY(lanes.selection.count() > 0);
}

void KeyframeViewTest::interactionDragSnapsAndCtrlSuspendsIt()
{
    Rig rig(false);
    rig.press(100, 45);
    QCOMPARE(rig.begins, 0);
    rig.it.move(125, 45, 0);
    QCOMPARE(rig.begins, 1);
    // Snaps to the playhead at 1.2.
    QVERIFY(std::abs(rig.lastDt - 0.2) < 1e-9);
    // Ctrl suspends the playhead pull and leaves the frame rounding: 1.25 rounds to 1.5.
    rig.it.move(125, 45, Qt::ControlModifier);
    QVERIFY(std::abs(rig.lastDt - 0.5) < 1e-9);
    QVERIFY(rig.it.readout().contains(QStringLiteral("+")));
    rig.release(125, 45);
    QCOMPARE(rig.commits, 1);
    QCOMPARE(rig.begins, 1);
    QVERIFY(rig.selection.has(QStringLiteral("opacity"), 1.5));
}

void KeyframeViewTest::interactionShiftLocksTheDominantAxis()
{
    Rig rig(true);
    rig.press(100, 50);
    // Mostly horizontal: the value does not change.
    rig.it.move(160, 45, Qt::ShiftModifier);
    QVERIFY(rig.lastDt > 0);
    QCOMPARE(rig.lastMoveRefs.first().toMap().value(QStringLiteral("dv")).toDouble(), 0.0);
    // Mostly vertical: the time does not change, and the value follows 10 px = 1 unit.
    rig.it.move(105, 10, Qt::ShiftModifier);
    QCOMPARE(rig.lastDt, 0.0);
    QVERIFY(std::abs(rig.lastMoveRefs.first().toMap().value(QStringLiteral("dv")).toDouble() - 4.0) < 1e-9);
    rig.release(105, 10);
    QCOMPARE(rig.commits, 1);
}

void KeyframeViewTest::interactionTangentRules()
{
    // The out handle of the key at 2 s sits at (250, 40). Dragging it 10 px right and 10 px up.
    {
        Rig rig(true);
        rig.scene.showHandles = true;
        rig.press(250, 40);
        rig.it.move(260, 30, 0);
        QCOMPARE(rig.begins, 1);
        QVERIFY(std::abs(rig.tangents[2] - 0.6) < 1e-9);
        QVERIFY(std::abs(rig.tangents[3] - 2.0) < 1e-9);
        // A smooth key keeps the far handle's length and turns it to the same slope.
        QVERIFY(std::abs(rig.tangents[0] + 0.5) < 1e-9);
        QVERIFY(std::abs(rig.tangents[1] + 2.0 / 0.6 * 0.5) < 1e-9);
        QVERIFY(!rig.tangentCorner);
        rig.release(260, 30);
        QCOMPARE(rig.commits, 1);
    }
    {
        // Alt breaks it: the far handle stays and the key becomes a corner.
        Rig rig(true);
        rig.scene.showHandles = true;
        rig.press(250, 40);
        rig.it.move(260, 30, Qt::AltModifier);
        QVERIFY(std::abs(rig.tangents[1] + 1.0) < 1e-9);
        QVERIFY(rig.tangentCorner);
    }
    {
        // Shift keeps the handle horizontal.
        Rig rig(true);
        rig.scene.showHandles = true;
        rig.press(250, 40);
        rig.it.move(260, 30, Qt::ShiftModifier);
        QCOMPARE(rig.tangents[3], 0.0);
        QVERIFY(std::abs(rig.tangents[2] - 0.6) < 1e-9);
    }
    {
        // A corner key's handles are independent.
        Rig rig(true);
        rig.scene.showHandles = true;
        rig.scene.series[0].points[1].corner = true;
        rig.press(250, 40);
        rig.it.move(260, 30, 0);
        QVERIFY(std::abs(rig.tangents[1] + 1.0) < 1e-9);
        QVERIFY(rig.tangentCorner);
    }
    {
        // The handle cannot reach past the next key.
        Rig rig(true);
        rig.scene.showHandles = true;
        rig.press(250, 40);
        rig.it.move(600, 40, Qt::AltModifier);
        QVERIFY(rig.tangents[2] <= 1.0 + 1e-9);
    }
    {
        // Pressing the curve and dragging bends the segment through both its keys; esc reverts.
        Rig rig(true);
        rig.press(150, 50);
        rig.it.move(150, 30, 0);
        QCOMPARE(rig.tangentWrites, 2);
        rig.it.cancel();
        QCOMPARE(rig.cancels, 1);
        QCOMPARE(rig.commits, 0);
    }
}

void KeyframeViewTest::interactionMarqueeAndAutoscroll()
{
    Rig rig(false);
    rig.selection.replace({ref(QStringLiteral("z"), 9.0)});
    rig.press(150, 41);
    // Pressing empty space drops the old selection.
    QCOMPARE(rig.selection.count(), 0);
    rig.it.move(250, 70, 0);
    QVERIFY(rig.it.marqueeActive());
    QCOMPARE(rig.selection.count(), 1);
    QVERIFY(rig.selection.has(QStringLiteral("opacity"), 2.0));
    QSignalSpy scrolls(&rig.it, &KeyframeInteraction::scrollRequested);
    rig.it.move(595, 70, 0);
    rig.it.autoScroll();
    QCOMPARE(scrolls.count(), 1);
    QVERIFY(scrolls.first().first().toDouble() > 0);
    rig.release(595, 70);
    QVERIFY(!rig.it.marqueeActive());

    // Shift adds to the selection.
    rig.selection.replace({ref(QStringLiteral("z"), 9.0)});
    rig.press(150, 41, int(DragSession::Device::Mouse), Qt::ShiftModifier);
    rig.it.move(250, 70, Qt::ShiftModifier);
    QCOMPARE(rig.selection.count(), 2);
    rig.release(250, 70);
}

void KeyframeViewTest::interactionNudgeBurstIsOneUndoStep()
{
    Rig rig(false);
    rig.selection.replace({ref(QStringLiteral("opacity"), 1.0)});
    QVERIFY(rig.it.nudge(1, 0, false));
    QVERIFY(rig.it.nudge(1, 0, false));
    QVERIFY(rig.it.nudge(1, 0, true));
    QCOMPARE(rig.begins, 1);
    QCOMPARE(rig.commits, 0);
    QVERIFY(std::abs(rig.lastDt - 1.2) < 1e-9);
    QVERIFY(rig.selection.has(QStringLiteral("opacity"), 2.2));
    // Up and down do nothing in lanes.
    QVERIFY(!rig.it.nudge(0, 1, false));
    rig.session.commit();
    QCOMPARE(rig.commits, 1);
    QVERIFY(!rig.it.busy());
    // The next burst is its own step and starts from where the keys are now.
    QVERIFY(rig.it.nudge(-1, 0, false));
    QCOMPARE(rig.begins, 2);
    QVERIFY(std::abs(rig.lastDt + 0.1) < 1e-9);
    rig.session.commit();

    Rig curves(true);
    curves.selection.replace({ref(QStringLiteral("x"), 1.0)});
    QVERIFY(curves.it.nudge(0, 1, false));
    // One px of a 100 px, 10 unit axis is 0.1.
    QVERIFY(std::abs(curves.lastMoveRefs.first().toMap().value(QStringLiteral("dv")).toDouble() - 0.1) < 1e-9);
    QVERIFY(curves.it.nudge(0, 1, true));
    QVERIFY(std::abs(curves.lastMoveRefs.first().toMap().value(QStringLiteral("dv")).toDouble() - 1.1) < 1e-9);
    QCOMPARE(curves.begins, 1);
    curves.session.commit();
}

void KeyframeViewTest::interactionEscRestores()
{
    Rig rig(false);
    const QVariantList original{ref(QStringLiteral("opacity"), 1.0)};
    QSignalSpy cancelled(&rig.it, &KeyframeInteraction::cancelled);
    rig.press(100, 45);
    rig.it.move(180, 45, 0);
    QVERIFY(rig.it.dragging());
    QVERIFY(!rig.selection.has(QStringLiteral("opacity"), 1.0));
    QVERIFY(rig.it.escape());
    QCOMPARE(rig.cancels, 1);
    QCOMPARE(rig.commits, 0);
    QCOMPARE(cancelled.count(), 1);
    QVERIFY(!rig.it.dragging());
    QVERIFY(rig.selection.has(QStringLiteral("opacity"), 1.0));
    // The release that follows commits nothing.
    rig.release(180, 45);
    QCOMPARE(rig.commits, 0);

    // Esc with no gesture clears the selection, then there is nothing left to do.
    QVERIFY(rig.it.escape());
    QCOMPARE(rig.selection.count(), 0);
    QVERIFY(!rig.it.escape());

    // A cancel through the session itself, as another surface's Esc would, also resets the machine.
    rig.press(100, 45);
    rig.it.move(180, 45, 0);
    rig.session.cancel();
    QVERIFY(!rig.it.dragging());
    QVERIFY(rig.selection.has(QStringLiteral("opacity"), 1.0));
}

void KeyframeViewTest::interactionStretchBox()
{
    Rig rig(false);
    rig.selection.replace({ref(QStringLiteral("opacity"), 1.0), ref(QStringLiteral("opacity"), 2.0)});
    rig.it.beginStretch(true, 0, int(DragSession::Device::Mouse));
    QCOMPARE(rig.begins, 1);
    rig.it.updateStretch(300, 0);
    QCOMPARE(rig.stretchPivot, 1.0);
    QVERIFY(std::abs(rig.stretchFactor - 2.0) < 1e-9);
    rig.it.endStretch();
    QCOMPARE(rig.commits, 1);

    // Alt pivots on the playhead instead of the far end.
    rig.it.beginStretch(true, Qt::AltModifier, int(DragSession::Device::Mouse));
    rig.it.updateStretch(300, 0);
    QCOMPARE(rig.stretchPivot, 1.2);
    rig.it.cancel();
    QCOMPARE(rig.cancels, 1);
}

void KeyframeViewTest::interactionTouchFlows()
{
    Rig rig(false);
    const int touch = int(DragSession::Device::Touch);
    QSignalSpy pans(&rig.it, &KeyframeInteraction::panRequested);
    QSignalSpy menus(&rig.it, &KeyframeInteraction::contextRequested);

    // A finger on empty space pans.
    rig.press(150, 41, touch);
    rig.it.move(170, 41, 0);
    QCOMPARE(pans.count(), 1);
    QCOMPARE(pans.first().first().toDouble(), 20.0);
    rig.release(170, 41);
    QCOMPARE(rig.begins, 0);

    // A tap selects a key; a second tap on the selected key asks for the menu.
    rig.press(100, 45, touch);
    rig.release(100, 45);
    QVERIFY(rig.selection.count() > 0);
    QCOMPARE(menus.count(), 0);
    rig.press(100, 45, touch);
    rig.release(100, 45);
    QCOMPARE(menus.count(), 1);

    // A long press picks the key up; lifting without moving asks for the menu, moving drags it.
    rig.selection.clear();
    rig.press(100, 45, touch);
    QVERIFY(rig.it.longPress());
    QCOMPARE(rig.begins, 1);
    rig.release(100, 45);
    QCOMPARE(menus.count(), 2);
    QCOMPARE(rig.commits, 1);

    rig.selection.clear();
    rig.press(100, 45, touch);
    QVERIFY(rig.it.longPress());
    rig.it.move(160, 45, 0);
    QVERIFY(rig.lastDt > 0);
    rig.release(160, 45);
    QCOMPARE(menus.count(), 2);

    // A long press on empty space starts a marquee.
    rig.press(150, 41, touch);
    QVERIFY(rig.it.longPress());
    QVERIFY(rig.it.marqueeActive());
    rig.release(150, 41);
}

void KeyframeViewTest::interactionTypedEntryAndKeyboard()
{
    Rig rig(false);
    rig.press(100, 45);
    rig.it.move(130, 45, 0);
    QVERIFY(rig.it.entryAvailable());
    QCOMPARE(rig.it.entryTarget(), QStringLiteral("time"));
    rig.it.applyTyped(2.5);
    QVERIFY(std::abs(rig.lastDt - 1.5) < 1e-9);
    QCOMPARE(rig.commits, 1);
    QVERIFY(!rig.it.dragging());
    rig.release(130, 45);
    QCOMPARE(rig.commits, 1);

    Rig curves(true);
    curves.it.setEntryTarget(QStringLiteral("value"));
    curves.press(100, 50);
    curves.it.move(100, 40, 0);
    curves.it.applyTyped(8.0);
    QVERIFY(std::abs(curves.lastMoveRefs.first().toMap().value(QStringLiteral("dv")).toDouble() - 3.0) < 1e-9);
    QCOMPARE(curves.commits, 1);

    Rig keys(false);
    keys.it.selectAll();
    QCOMPARE(keys.selection.count(), 3);
    QVERIFY(keys.it.deleteSelected());
    QCOMPARE(keys.removed.size(), 3);
    QCOMPARE(keys.selection.count(), 0);
    QVERIFY(!keys.it.deleteSelected());
}

QTEST_GUILESS_MAIN(KeyframeViewTest)
#include "tst_keyframeview.moc"
