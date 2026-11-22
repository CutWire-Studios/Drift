#include "app/editor/sessions/KeyframeInteraction.h"

#include "core/geom/BezierPath.h"
#include "ui/manip/ManipModifiers.h"

#include <QtMath>

#include <algorithm>
#include <cmath>

using drift::manip::DragSession;
using drift::manip::ManipModifiers;
using namespace KeyframeCurves;

namespace {

QString keyOf(const QString &prop, double seconds)
{
    return KeyframeSelection::keyId(prop, seconds);
}

ManipModifiers modsOf(int modifiers)
{
    return ManipModifiers::fromQt(Qt::KeyboardModifiers::fromInt(modifiers));
}

double edgePush(double pos, double size)
{
    if (pos < KeyframeInteraction::kEdgeZonePx)
        return -(KeyframeInteraction::kEdgeZonePx - pos) * 0.5;
    if (pos > size - KeyframeInteraction::kEdgeZonePx)
        return (pos - (size - KeyframeInteraction::kEdgeZonePx)) * 0.5;
    return 0;
}

} // namespace

KeyframeInteraction::KeyframeInteraction(QObject *parent) : QObject(parent) {}

void KeyframeInteraction::setSelection(KeyframeSelection *selection)
{
    if (m_selection == selection)
        return;
    m_selection = selection;
    emit setupChanged();
}

void KeyframeInteraction::setDragSession(DragSession *session)
{
    if (m_session == session)
        return;
    if (m_session)
        m_session->disconnect(this);
    m_session = session;
    if (m_session) {
        connect(m_session, &DragSession::cancelled, this, &KeyframeInteraction::onSessionCancelled);
        connect(m_session, &DragSession::committed, this, &KeyframeInteraction::onSessionCommitted);
    }
}

void KeyframeInteraction::setEntryTarget(const QString &target)
{
    if (m_entryTarget == target || (target != QLatin1String("time") && target != QLatin1String("value")))
        return;
    m_entryTarget = target;
    emit entryTargetChanged();
}

QString KeyframeInteraction::entryUnit() const
{
    return m_entryTarget == QLatin1String("time") ? QStringLiteral("s") : QString();
}

void KeyframeInteraction::refresh()
{
    m_scene = Scene();
    if (m_ops.scene)
        m_ops.scene(m_scene);
}

bool KeyframeInteraction::busy() const
{
    return dragging() || m_marqueeActive || m_nudging;
}

void KeyframeInteraction::setBusyDirty()
{
    const bool now = busy();
    if (now == m_busyLast)
        return;
    m_busyLast = now;
    emit busyChanged();
}

void KeyframeInteraction::notifyChanged()
{
    setBusyDirty();
    emit changed();
}

void KeyframeInteraction::setReadout(const QString &text)
{
    if (m_readout == text)
        return;
    m_readout = text;
    if (m_session && !text.isEmpty())
        m_session->update(text);
    emit changed();
}

int KeyframeInteraction::cursorShape() const
{
    if (m_keyDrag || m_shaping)
        return curves() ? Qt::SizeAllCursor : Qt::SizeHorCursor;
    if (m_hoverRow >= 0 || !m_hoverId.isEmpty() || m_hoverGrab)
        return Qt::PointingHandCursor;
    return Qt::ArrowCursor;
}

QString KeyframeInteraction::formatTimecode(double seconds) const
{
    const int fps = std::max(1, int(std::lround(m_fps)));
    const qint64 total = std::llround(std::max(0.0, seconds) * fps);
    const qint64 whole = total / fps;
    return QStringLiteral("%1:%2:%3:%4")
        .arg(whole / 3600, 2, 10, QLatin1Char('0'))
        .arg((whole / 60) % 60, 2, 10, QLatin1Char('0'))
        .arg(whole % 60, 2, 10, QLatin1Char('0'))
        .arg(total - whole * fps, 2, 10, QLatin1Char('0'));
}

bool KeyframeInteraction::isFocusedSeries(int index) const
{
    const auto &series = m_scene.series;
    if (m_scene.focusedIndex < 0 || m_scene.focusedIndex >= series.size())
        return true;
    return index >= 0 && index < series.size()
           && series.at(index).rowProp == series.at(m_scene.focusedIndex).rowProp;
}

double KeyframeInteraction::screenY(double value, const Series &series) const
{
    return m_scene.graphMargin + yForValue(value, series, m_scene.graphHeight);
}

double KeyframeInteraction::valuePerPx(const Series &series) const
{
    return (series.valueMax - series.valueMin) / std::max(1.0, m_scene.graphHeight);
}

KeyframeInteraction::Hit KeyframeInteraction::dotAt(double x, double y, double radius) const
{
    Hit hit;
    if (!curves()) {
        const QVariantMap m = KeyframeRows::hitTest(m_scene.rows, m_scene.axis, x, y, m_contentY, m_rowHeight, radius);
        hit.row = m.value(QStringLiteral("row"), -1).toInt();
        hit.seconds = m.value(QStringLiteral("seconds")).toDouble();
        if (m.value(QStringLiteral("dot")).isValid() && !m.value(QStringLiteral("dot")).isNull()) {
            hit.kind = QStringLiteral("dot");
            hit.map = m.value(QStringLiteral("dot")).toMap();
            hit.refs = m.value(QStringLiteral("refs")).toList();
        }
        return hit;
    }
    int bestSeries = -1;
    int bestPoint = -1;
    double bestDist = radius + 0.5;
    bool bestFocused = false;
    for (int s = 0; s < m_scene.series.size(); ++s) {
        const Series &entry = m_scene.series.at(s);
        const bool focused = isFocusedSeries(s);
        for (int i = 0; i < entry.points.size(); ++i) {
            const Point &p = entry.points.at(i);
            const double d = std::hypot(xFor(p.seconds) - x, screenY(p.value, entry) - y);
            if (d < bestDist || (d <= bestDist && focused && !bestFocused)) {
                bestDist = d;
                bestFocused = focused;
                bestSeries = s;
                bestPoint = i;
            }
        }
    }
    hit.seconds = m_scene.axis.secondsFor(x);
    if (bestSeries < 0)
        return hit;
    const Series &entry = m_scene.series.at(bestSeries);
    const Point &p = entry.points.at(bestPoint);
    hit.kind = QStringLiteral("dot");
    hit.entryIndex = bestSeries;
    hit.row = entry.rowIndex;
    hit.track = entry.prop;
    hit.rowProp = entry.rowProp;
    hit.seconds = p.seconds;
    hit.value = p.value;
    hit.refs = {KeyframeRows::makeRef(entry.prop, p.seconds)};
    hit.map = {{QStringLiteral("seconds"), p.seconds}, {QStringLiteral("refs"), hit.refs}, {QStringLiteral("hold"), p.hold}};
    return hit;
}

KeyframeInteraction::Hit KeyframeInteraction::handleAt(double x, double y, double radius) const
{
    Hit hit;
    const int fi = m_scene.focusedIndex;
    if (!curves() || !m_curveEditing || !m_scene.showHandles || fi < 0 || fi >= m_scene.series.size())
        return hit;
    const Series &entry = m_scene.series.at(fi);
    const QList<Point> points = sortedPoints(entry.points);
    for (const Handle &h : tangentHandles(points)) {
        const Point &p = points.at(h.pointIndex);
        const double dx = handleSeconds(p, h.outgoing);
        const double value = handleValue(p, h.outgoing);
        if (std::hypot(xFor(p.seconds + dx) - x, screenY(value, entry) - y) > radius)
            continue;
        hit.kind = QStringLiteral("handle");
        hit.entryIndex = fi;
        hit.track = entry.prop;
        hit.rowProp = entry.rowProp;
        hit.outgoing = h.outgoing;
        hit.point = p;
        hit.dx = dx;
        hit.handleValue = value;
        hit.spanIn = h.pointIndex > 0 ? p.seconds - points.at(h.pointIndex - 1).seconds : 0.0;
        hit.spanOut = h.pointIndex + 1 < points.size() ? points.at(h.pointIndex + 1).seconds - p.seconds : 0.0;
        return hit;
    }
    return hit;
}

KeyframeInteraction::Hit KeyframeInteraction::segmentAt(double x, double y, double radius) const
{
    Hit hit;
    if (!curves() || !m_curveEditing)
        return hit;
    const int fi = m_scene.focusedIndex;
    int bestSeries = -1;
    int bestIndex = -1;
    double bestSeconds = 0;
    double bestDist = radius + 0.5;
    const auto scan = [&](int s) {
        const Series &entry = m_scene.series.at(s);
        const QList<Point> points = sortedPoints(entry.points);
        for (int i = 0; i + 1 < points.size(); ++i) {
            const Point &a = points.at(i);
            const Point &b = points.at(i + 1);
            if (a.shaped() || b.seconds <= a.seconds)
                continue;
            const double xa = xFor(a.seconds);
            const double xb = xFor(b.seconds);
            if (x < xa - radius || x > xb + radius)
                continue;
            for (double sx = std::max(xa, x - radius); sx <= std::min(xb, x + radius); sx += 1.0) {
                const double seconds = m_scene.axis.secondsFor(sx);
                const double d = std::hypot(sx - x, screenY(segmentValue(a, b, seconds), entry) - y);
                if (d < bestDist) {
                    bestDist = d;
                    bestSeries = s;
                    bestIndex = i;
                    bestSeconds = seconds;
                }
            }
        }
    };
    if (fi >= 0 && fi < m_scene.series.size())
        scan(fi);
    if (bestSeries < 0) {
        for (int s = 0; s < m_scene.series.size(); ++s)
            if (s != fi)
                scan(s);
    }
    if (bestSeries < 0)
        return hit;
    const Series &entry = m_scene.series.at(bestSeries);
    const QList<Point> points = sortedPoints(entry.points);
    hit.kind = QStringLiteral("segment");
    hit.entryIndex = bestSeries;
    hit.track = entry.prop;
    hit.rowProp = entry.rowProp;
    hit.point = points.at(bestIndex);
    hit.other = points.at(bestIndex + 1);
    hit.t = segmentParameter(hit.point, hit.other, bestSeconds);
    return hit;
}

KeyframeInteraction::Hit KeyframeInteraction::grabAt(double x, double y) const
{
    Hit hit = handleAt(x, y, reach());
    if (hit.kind.isEmpty())
        hit = segmentAt(x, y, reach());
    return hit;
}

QVariantMap KeyframeInteraction::hitMap(const Hit &hit) const
{
    QVariantMap m;
    m.insert(QStringLiteral("row"), hit.row);
    m.insert(QStringLiteral("dot"), hit.kind == QLatin1String("dot") ? QVariant(hit.map) : QVariant());
    m.insert(QStringLiteral("refs"), hit.refs);
    m.insert(QStringLiteral("seconds"), hit.seconds);
    m.insert(QStringLiteral("value"), hit.value);
    m.insert(QStringLiteral("entryIndex"), hit.entryIndex);
    m.insert(QStringLiteral("track"), hit.track);
    return m;
}

QString KeyframeInteraction::tipFor(const Hit &hit) const
{
    if (hit.kind != QLatin1String("dot"))
        return {};
    if (curves()) {
        const Series &entry = m_scene.series.at(hit.entryIndex);
        return entry.label + QLatin1Char('\n') + formatTimecode(hit.seconds) + QLatin1Char('\n')
               + QString::number(hit.value, 'f', 2);
    }
    if (hit.row < 0 || hit.row >= m_scene.rows.size())
        return {};
    const KeyframeRows::Row &row = m_scene.rows.at(hit.row);
    QString text = row.label + QLatin1Char('\n') + formatTimecode(hit.seconds);
    if (row.isGroup())
        return text + QLatin1Char('\n') + tr("%n key(s)", "", hit.refs.size());
    const int decimals = row.entry.value(QStringLiteral("decimals"), 2).toInt();
    for (const QVariant &k : row.keys) {
        const QVariantMap map = k.toMap();
        if (std::abs(map.value(QStringLiteral("seconds")).toDouble() - hit.seconds) < KeyframeRows::kTimeEps) {
            text += QLatin1Char('\n') + QString::number(map.value(QStringLiteral("value")).toDouble(), 'f', decimals);
            break;
        }
    }
    return text;
}

bool KeyframeInteraction::sameRefs(const QVariantList &a, const QVariantList &b)
{
    if (a.size() != b.size())
        return false;
    for (int i = 0; i < a.size(); ++i) {
        const QVariantMap x = a.at(i).toMap();
        const QVariantMap y = b.at(i).toMap();
        if (x.value(QStringLiteral("prop")) != y.value(QStringLiteral("prop"))
            || keyOf(x.value(QStringLiteral("prop")).toString(), x.value(QStringLiteral("seconds")).toDouble())
                   != keyOf(y.value(QStringLiteral("prop")).toString(), y.value(QStringLiteral("seconds")).toDouble()))
            return false;
    }
    return true;
}

void KeyframeInteraction::captureValues()
{
    m_ranges.clear();
    m_baseValue.clear();
    for (const Series &s : std::as_const(m_scene.series)) {
        m_ranges.insert(s.prop, {s.valueMin, s.valueMax, s.fixedRange});
        for (const Point &p : s.points)
            m_baseValue.insert(s.prop + QLatin1Char('@') + keyOf(s.prop, p.seconds), p.value);
    }
}

QVariantList KeyframeInteraction::valueRefs(const QVariantList &refs, double upPx, bool overridden,
                                            double absolute) const
{
    QVariantList out;
    out.reserve(refs.size());
    for (const QVariant &r : refs) {
        const QVariantMap m = r.toMap();
        const QString prop = m.value(QStringLiteral("prop")).toString();
        const double seconds = m.value(QStringLiteral("seconds")).toDouble();
        double dv = 0;
        const auto range = m_ranges.constFind(prop);
        if (curves() && range != m_ranges.constEnd()) {
            dv = overridden ? absolute : upPx * (range->max - range->min) / std::max(1.0, m_scene.graphHeight);
            if (range->fixed) {
                const double base = m_baseValue.value(prop + QLatin1Char('@') + keyOf(prop, seconds));
                dv = std::clamp(base + dv, range->min, range->max) - base;
            }
        }
        out.append(QVariantMap{{QStringLiteral("prop"), prop}, {QStringLiteral("seconds"), seconds},
                               {QStringLiteral("dv"), dv}});
    }
    return out;
}

double KeyframeInteraction::snapDelta(double dxPx, bool noSnap, bool zeroWhenStill) const
{
    if (zeroWhenStill && dxPx == 0)
        return 0;
    const double lo = m_scene.clipStart;
    const double hi = lo + m_scene.clipDuration;
    const double raw = std::clamp(m_origin + dxPx / std::max(1e-9, m_scene.axis.pxPerSecond), lo, hi);
    const double snapped = m_ops.snapTime ? m_ops.snapTime(raw, m_dragRefs, noSnap) : raw;
    return snapped - m_origin;
}

bool KeyframeInteraction::press(double x, double y, int button, int modifiers, int device)
{
    refresh();
    m_touch = device != int(DragSession::Device::Mouse);
    m_deviceInt = device;
    m_pressed = true;
    m_press = m_last = m_panLast = {x, y};
    m_lastMods = modifiers;
    m_pressContentX = m_scene.axis.contentX;
    m_pressContentY = m_contentY;
    m_moved = false;
    m_armed = false;
    m_collapse = false;
    m_hoverTip.clear();
    m_mode = Mode::Idle;
    m_pressHit = dotAt(x, y, reach());
    if (button == Qt::MiddleButton) {
        m_mode = Mode::Pan;
        return true;
    }
    if (button == Qt::RightButton) {
        m_pressed = false;
        if (!m_touch)
            openContext(x, y, modifiers);
        return true;
    }
    if (button != Qt::LeftButton) {
        m_pressed = false;
        return false;
    }
    if (m_touch) {
        m_mode = Mode::Touch;
        return true;
    }
    if (m_pressHit.kind == QLatin1String("dot")) {
        pressKey(modifiers);
        return true;
    }
    const Hit grab = grabAt(x, y);
    if (!grab.kind.isEmpty()) {
        emit focusRequested(grab.track, grab.rowProp);
        m_pressHit = grab;
        m_mode = grab.kind == QLatin1String("handle") ? Mode::Handle : Mode::Segment;
    } else {
        if (!(modifiers & Qt::ShiftModifier) && m_selection)
            m_selection->clear();
        beginMarquee(modifiers);
    }
    notifyChanged();
    return true;
}

void KeyframeInteraction::openContext(double x, double y, int)
{
    if (m_pressHit.kind == QLatin1String("dot") && m_selection && !m_selection->hasAll(m_pressHit.refs))
        m_selection->replace(m_pressHit.refs);
    emit contextRequested(x, y, hitMap(m_pressHit));
}

void KeyframeInteraction::pressKey(int modifiers)
{
    if (curves())
        emit focusRequested(m_pressHit.track, m_pressHit.rowProp);
    const int outcome = m_selection ? m_selection->press(modifiers, m_pressHit.refs) : 1;
    m_collapse = outcome == 2;
    if (outcome != 0)
        m_mode = Mode::Key;
}

void KeyframeInteraction::move(double x, double y, int modifiers)
{
    m_last = {x, y};
    m_lastMods = modifiers;
    if (!m_pressed)
        return;
    const bool far = std::abs(x - m_press.x()) > kMovePx || std::abs(y - m_press.y()) > kMovePx;
    m_moved = m_moved || far;
    if (m_mode == Mode::Touch) {
        if (!far)
            return;
        m_mode = Mode::Pan;
    }
    switch (m_mode) {
    case Mode::Pan: {
        const double dx = x - m_panLast.x();
        const double dy = y - m_panLast.y();
        m_panLast = {x, y};
        emit panRequested(dx, dy);
        break;
    }
    case Mode::Key:
        if (!m_keyDrag && far)
            beginKeyDrag();
        if (m_keyDrag)
            updateKeyDrag(x, y, modifiers);
        break;
    case Mode::Handle:
    case Mode::Segment:
        if (!m_shaping && far)
            beginShape();
        if (m_shaping)
            updateShape(x, y, modifiers);
        break;
    case Mode::Marquee:
        if (!m_marqueeActive && far) {
            m_marqueeActive = true;
            setBusyDirty();
        }
        if (m_marqueeActive)
            updateMarquee(x, y);
        break;
    default:
        break;
    }
}

bool KeyframeInteraction::longPress()
{
    if (!m_pressed || m_mode != Mode::Touch)
        return false;
    m_armed = true;
    if (m_pressHit.kind == QLatin1String("dot")) {
        if (curves())
            emit focusRequested(m_pressHit.track, m_pressHit.rowProp);
        if (m_selection)
            m_selection->press(0, m_pressHit.refs);
        m_mode = Mode::Key;
        beginKeyDrag();
        return true;
    }
    const Hit grab = grabAt(m_press.x(), m_press.y());
    if (!grab.kind.isEmpty()) {
        emit focusRequested(grab.track, grab.rowProp);
        m_pressHit = grab;
        m_mode = grab.kind == QLatin1String("handle") ? Mode::Handle : Mode::Segment;
        beginShape();
        return true;
    }
    beginMarquee(0);
    m_marqueeActive = true;
    notifyChanged();
    return true;
}

void KeyframeInteraction::release(double x, double y, int button, int modifiers)
{
    m_last = {x, y};
    m_lastMods = modifiers;
    if (button == Qt::RightButton || !m_pressed)
        return;
    const Mode mode = m_mode;
    m_mode = Mode::Idle;
    m_pressed = false;
    switch (mode) {
    case Mode::Key:
        if (m_keyDrag) {
            finishKeyDrag();
            if (m_armed && !m_moved)
                emit contextRequested(x, y, hitMap(m_pressHit));
        } else if (m_collapse && m_selection) {
            m_selection->replace(m_pressHit.refs);
        }
        break;
    case Mode::Handle:
    case Mode::Segment:
        if (m_shaping)
            finishShape();
        else if (mode == Mode::Segment && m_selection && !m_armed)
            m_selection->clear();
        break;
    case Mode::Marquee:
        m_marqueeActive = false;
        break;
    case Mode::Touch:
        if (m_pressHit.kind == QLatin1String("dot") && m_selection) {
            if (curves())
                emit focusRequested(m_pressHit.track, m_pressHit.rowProp);
            if (m_selection->hasAll(m_pressHit.refs))
                emit contextRequested(x, y, hitMap(m_pressHit));
            else
                m_selection->replace(m_pressHit.refs);
        } else if (m_selection) {
            m_selection->clear();
        }
        break;
    default:
        break;
    }
    m_armed = false;
    m_readout.clear();
    notifyChanged();
}

void KeyframeInteraction::cancelPress()
{
    if (!m_pressed && !dragging() && !m_marqueeActive)
        return;
    cancel();
    m_mode = Mode::Idle;
    m_pressed = false;
    notifyChanged();
}

void KeyframeInteraction::hover(double x, double y, int device)
{
    m_last = {x, y};
    m_touch = device != int(DragSession::Device::Mouse);
    refresh();
    const Hit hit = dotAt(x, y, reach());
    const bool dot = hit.kind == QLatin1String("dot");
    const QString id = curves() && dot ? keyOf(hit.refs.first().toMap().value(QStringLiteral("prop")).toString(), hit.seconds)
                                       : QString();
    const int row = !curves() && dot ? hit.row : -1;
    const double seconds = !curves() && dot ? hit.seconds : 0.0;
    const bool grab = curves() && !dot && !grabAt(x, y).kind.isEmpty();
    const QString tip = tipFor(hit);
    if (id == m_hoverId && row == m_hoverRow && seconds == m_hoverSeconds && tip == m_hoverTip && grab == m_hoverGrab)
        return;
    m_hoverId = id;
    m_hoverRow = row;
    m_hoverSeconds = seconds;
    m_hoverTip = tip;
    m_hoverGrab = grab;
    emit changed();
}

void KeyframeInteraction::clearHover()
{
    if (m_hoverId.isEmpty() && m_hoverRow < 0 && m_hoverTip.isEmpty() && !m_hoverGrab)
        return;
    m_hoverId.clear();
    m_hoverRow = -1;
    m_hoverTip.clear();
    m_hoverGrab = false;
    emit changed();
}

void KeyframeInteraction::doubleClick(double x, double y, int device)
{
    refresh();
    m_touch = device != int(DragSession::Device::Mouse);
    const Hit hit = dotAt(x, y, reach());
    if (hit.kind == QLatin1String("dot")) {
        if (m_doubleClickKey == QLatin1String("remove")) {
            if (m_ops.removeKeys)
                m_ops.removeKeys(hit.refs);
            if (m_selection)
                m_selection->clear();
        } else {
            emit playheadRequested(hit.seconds);
        }
        return;
    }
    const double lo = m_scene.clipStart;
    const double hi = lo + m_scene.clipDuration;
    QStringList props;
    if (curves()) {
        const int fi = m_scene.focusedIndex;
        if (fi < 0 || fi >= m_scene.series.size())
            return;
        props = {m_scene.series.at(fi).prop};
    } else if (hit.row >= 0 && hit.row < m_scene.rows.size() && !m_scene.rows.at(hit.row).isGroup()) {
        props = m_scene.rows.at(hit.row).tracks;
    }
    if (props.isEmpty() || !m_ops.addKeys)
        return;
    double seconds = std::clamp(hit.seconds, lo, hi);
    if (m_ops.snapTime)
        seconds = std::clamp(m_ops.snapTime(seconds, {}, false), lo, hi);
    m_ops.addKeys(props, seconds);
    emit keysAdded(props);
}

void KeyframeInteraction::beginKeyDrag()
{
    if (!m_selection)
        return;
    m_dragRefs = m_selection->refs();
    m_dragResult = m_dragRefs;
    m_origin = m_pressHit.seconds;
    m_dragTime = m_origin;
    m_pressValue = m_pressHit.value;
    m_dragValue = m_pressValue;
    m_dt = 0;
    m_useOverride = false;
    captureValues();
    if (m_ops.lockRanges)
        m_ops.lockRanges(true);
    if (m_session)
        m_session->begin(QStringLiteral("keyframes"), DragSession::Device(m_deviceInt), tr("Move keyframes"));
    m_keyDrag = true;
    setBusyDirty();
}

void KeyframeInteraction::updateKeyDrag(double x, double y, int modifiers)
{
    refresh();
    const ManipModifiers mm = modsOf(modifiers);
    double dx = x - m_press.x() + (m_scene.axis.contentX - m_pressContentX);
    double dy = curves() ? y - m_press.y() : 0.0;
    if (mm.constrain && curves()) {
        if (std::abs(dx) >= std::abs(dy))
            dy = 0;
        else
            dx = 0;
    }
    m_dyPx = dy;
    m_useOverride = false;
    m_dt = snapDelta(dx, mm.noSnap, curves());
    applyKeyDrag();
}

void KeyframeInteraction::applyKeyDrag()
{
    m_dragTime = m_origin + m_dt;
    const QVariantList refs = valueRefs(m_dragRefs, -m_dyPx, m_useOverride, m_overrideValue);
    for (const QVariant &r : refs) {
        const QVariantMap m = r.toMap();
        if (m.value(QStringLiteral("prop")).toString() == m_pressHit.track
            && keyOf(m_pressHit.track, m.value(QStringLiteral("seconds")).toDouble()) == keyOf(m_pressHit.track, m_origin)) {
            m_dragValue = m_pressValue + m.value(QStringLiteral("dv")).toDouble();
            break;
        }
    }
    const QVariantList res = m_ops.moveKeys ? m_ops.moveKeys(refs, m_dt) : QVariantList();
    if (!res.isEmpty() && m_selection) {
        m_dragResult = res;
        m_selection->replace(res);
    }
    const int frames = int(std::lround((m_dragTime - m_origin) * m_fps));
    QString text = formatTimecode(m_dragTime) + QStringLiteral("  ") + (frames >= 0 ? QStringLiteral("+") : QString())
                   + QString::number(frames);
    if (curves())
        text += QLatin1Char('\n') + QString::number(m_dragValue, 'f', 2);
    setReadout(text);
}

void KeyframeInteraction::finishKeyDrag()
{
    if (!m_keyDrag)
        return;
    m_keyDrag = false;
    if (m_session)
        m_session->commit();
    if (m_ops.clearCollisions)
        m_ops.clearCollisions();
    if (m_selection)
        m_selection->replace(m_dragResult);
    if (m_ops.lockRanges)
        m_ops.lockRanges(false);
    m_readout.clear();
    notifyChanged();
}

void KeyframeInteraction::beginShape()
{
    if (m_ops.lockRanges)
        m_ops.lockRanges(true);
    captureValues();
    if (m_session)
        m_session->begin(QStringLiteral("curve"), DragSession::Device(m_deviceInt), tr("Edit keyframe curve"));
    m_shaping = true;
    setBusyDirty();
}

void KeyframeInteraction::updateShape(double x, double y, int modifiers)
{
    refresh();
    if (!m_ops.setTangents)
        return;
    const ManipModifiers mm = modsOf(modifiers);
    const Hit &hit = m_pressHit;
    const double dSeconds = (x - m_press.x() + m_scene.axis.contentX - m_pressContentX)
                            / std::max(1e-9, m_scene.axis.pxPerSecond);
    const auto range = m_ranges.constFind(hit.track);
    const double perPx = range != m_ranges.constEnd()
                             ? (range->max - range->min) / std::max(1.0, m_scene.graphHeight)
                             : 0.0;
    const double dValue = -(y - m_press.y()) * perPx;
    if (hit.kind == QLatin1String("handle")) {
        const Point &p = hit.point;
        const drift::TangentMode mode = (mm.breakTangent || p.corner) ? drift::TangentMode::Free
                                                                       : drift::TangentMode::Aligned;
        const drift::geom::GraphTangents t{p.inDx, p.inDy, p.outDx, p.outDy};
        const drift::geom::GraphTangents r = drift::geom::applyGraphTangentDrag(
            t, hit.outgoing ? drift::geom::TangentHandle::Out : drift::geom::TangentHandle::In, hit.dx + dSeconds,
            hit.handleValue + dValue - p.value, mode, mm.constrain, hit.spanIn, hit.spanOut);
        m_ops.setTangents(hit.track, p.seconds, r.inDx, r.inDy, r.outDx, r.outDy, p.corner || mm.breakTangent);
        return;
    }
    Point a = hit.point;
    Point b = hit.other;
    deformSegment(a, b, hit.t, mm.constrain ? 0.0 : dSeconds, dValue);
    for (const Point &k : {a, b})
        m_ops.setTangents(hit.track, k.seconds, k.inDx, k.inDy, k.outDx, k.outDy, k.corner);
}

void KeyframeInteraction::finishShape()
{
    if (!m_shaping)
        return;
    m_shaping = false;
    if (m_session)
        m_session->commit();
    if (m_ops.lockRanges)
        m_ops.lockRanges(false);
    notifyChanged();
}

void KeyframeInteraction::beginMarquee(int modifiers)
{
    m_marqueeBase = (modifiers & Qt::ShiftModifier) && m_selection ? m_selection->refs() : QVariantList();
    m_mode = Mode::Marquee;
}

void KeyframeInteraction::updateMarquee(double x, double y)
{
    refresh();
    const double anchorX = m_press.x() - (m_scene.axis.contentX - m_pressContentX);
    const double anchorY = m_press.y() - (m_contentY - m_pressContentY);
    const double px = std::clamp(x, 0.0, m_viewWidth);
    const double py = std::clamp(y, 0.0, m_viewHeight);
    const double x0 = std::min(anchorX, px);
    const double x1 = std::max(anchorX, px);
    const double y0 = std::min(anchorY, py);
    const double y1 = std::max(anchorY, py);
    const double left = std::max(0.0, x0);
    const double top = std::max(0.0, y0);
    m_marquee = QRectF(left, top, std::min(m_viewWidth, x1) - left, std::min(m_viewHeight, y1) - top);
    QVariantList refs;
    if (curves()) {
        refs = m_marqueeBase;
        for (const Series &s : std::as_const(m_scene.series)) {
            for (const Point &p : s.points) {
                const double sx = xFor(p.seconds);
                const double sy = screenY(p.value, s);
                if (sx >= x0 && sx <= x1 && sy >= y0 && sy <= y1)
                    refs.append(KeyframeRows::makeRef(s.prop, p.seconds));
            }
        }
    } else {
        refs = KeyframeRows::marqueeRefs(m_scene.rows, m_scene.axis, x0, x1, y0, y1, m_contentY, m_rowHeight,
                                         m_marqueeBase);
    }
    if (m_selection)
        m_selection->replace(refs);
    emit changed();
}

void KeyframeInteraction::autoScroll()
{
    if (!m_keyDrag && !m_marqueeActive)
        return;
    const double dx = edgePush(m_last.x(), m_viewWidth);
    const double dy = m_marqueeActive && !curves() ? edgePush(m_last.y(), m_viewHeight) : 0.0;
    if (dx == 0 && dy == 0)
        return;
    emit scrollRequested(dx, dy);
    if (m_marqueeActive)
        updateMarquee(m_last.x(), m_last.y());
    else
        updateKeyDrag(m_last.x(), m_last.y(), m_lastMods);
}

void KeyframeInteraction::beginStretch(bool fromRight, int modifiers, int device)
{
    if (!m_selection)
        return;
    const QVariant span = m_selection->span();
    if (!span.isValid())
        return;
    refresh();
    const QVariantMap s = span.toMap();
    const double start = s.value(QStringLiteral("start")).toDouble();
    const double end = s.value(QStringLiteral("end")).toDouble();
    m_stretchRefs = m_selection->refs();
    m_edgeSeconds = fromRight ? end : start;
    m_pivot = modsOf(modifiers).fromCenter ? m_playhead : (fromRight ? start : end);
    m_dragRefs = m_stretchRefs;
    if (m_session)
        m_session->begin(QStringLiteral("stretch"), DragSession::Device(device), tr("Stretch keyframes"));
    m_stretching = true;
    setBusyDirty();
}

void KeyframeInteraction::updateStretch(double x, int modifiers)
{
    if (!m_stretching)
        return;
    const double denom = m_edgeSeconds - m_pivot;
    if (std::abs(denom) < KeyframeRows::kTimeEps)
        return;
    refresh();
    double target = std::max(0.0, m_scene.axis.secondsFor(x));
    if (m_ops.snapTime)
        target = m_ops.snapTime(target, m_stretchRefs, modsOf(modifiers).noSnap);
    const double factor = std::max(0.01, (target - m_pivot) / denom);
    const QVariantList res = m_ops.stretchKeys ? m_ops.stretchKeys(m_stretchRefs, m_pivot, factor) : QVariantList();
    if (!res.isEmpty() && m_selection)
        m_selection->replace(res);
}

void KeyframeInteraction::endStretch()
{
    if (!m_stretching)
        return;
    m_stretching = false;
    if (m_session)
        m_session->commit();
    notifyChanged();
}

bool KeyframeInteraction::nudge(int dirX, int dirY, bool shift)
{
    if (!m_selection || m_selection->count() == 0 || dragging() || !m_session)
        return false;
    if (!curves())
        dirY = 0;
    if (dirX == 0 && dirY == 0)
        return false;
    const QVariantList refs = m_selection->refs();
    if (m_nudging && !sameRefs(refs, m_nudgeResult))
        m_session->commit();
    if (!m_nudging) {
        refresh();
        captureValues();
        m_nudgeBase = refs;
        m_nudgeResult = refs;
        m_nudgeDt = 0;
        m_nudgePx = 0;
        m_origin = 0;
    }
    m_session->nudge(tr("Nudge keyframes"));
    m_nudging = true;
    const int step = shift ? 10 : 1;
    m_nudgeDt += dirX * step / std::max(1.0, m_fps);
    m_nudgePx += dirY * step;
    const QVariantList res = m_ops.moveKeys ? m_ops.moveKeys(valueRefs(m_nudgeBase, m_nudgePx, false, 0), m_nudgeDt)
                                            : QVariantList();
    if (res.size() == m_nudgeBase.size() && !res.isEmpty()) {
        m_nudgeDt = res.first().toMap().value(QStringLiteral("seconds")).toDouble()
                    - m_nudgeBase.first().toMap().value(QStringLiteral("seconds")).toDouble();
        m_nudgeResult = res;
        m_selection->replace(res);
    }
    notifyChanged();
    return true;
}

bool KeyframeInteraction::deleteSelected()
{
    if (!m_selection || m_selection->count() == 0)
        return false;
    if (m_ops.removeKeys)
        m_ops.removeKeys(m_selection->refs());
    m_selection->clear();
    return true;
}

QVariantList KeyframeInteraction::allRefs() const
{
    KeyframeInteraction *self = const_cast<KeyframeInteraction *>(this);
    self->refresh();
    QVariantList refs;
    if (curves()) {
        for (const Series &s : std::as_const(m_scene.series))
            for (const Point &p : s.points)
                refs.append(KeyframeRows::makeRef(s.prop, p.seconds));
        return refs;
    }
    for (int i = 0; i < m_scene.rows.size(); ++i)
        for (const QVariant &d : KeyframeRows::laneDots(m_scene.rows, i))
            refs.append(d.toMap().value(QStringLiteral("refs")).toList());
    return refs;
}

void KeyframeInteraction::selectAll()
{
    if (m_selection)
        m_selection->replace(allRefs());
}

bool KeyframeInteraction::escape()
{
    if (busy()) {
        cancel();
        return true;
    }
    if (m_selection && m_selection->count() > 0) {
        m_selection->clear();
        return true;
    }
    return false;
}

void KeyframeInteraction::cancel()
{
    if (m_marqueeActive) {
        m_marqueeActive = false;
        m_mode = Mode::Idle;
        m_pressed = false;
        if (m_selection)
            m_selection->replace(m_marqueeBase);
    }
    if ((m_keyDrag || m_shaping || m_stretching || m_nudging) && m_session && m_session->interacting()) {
        m_session->cancel();
        return;
    }
    onSessionCancelled();
}

void KeyframeInteraction::onSessionCancelled()
{
    const bool ours = m_keyDrag || m_shaping || m_stretching || m_nudging;
    if (!ours)
        return;
    if (m_selection) {
        if (m_keyDrag)
            m_selection->replace(m_dragRefs);
        else if (m_stretching)
            m_selection->replace(m_stretchRefs);
        else if (m_nudging)
            m_selection->replace(m_nudgeBase);
    }
    if ((m_keyDrag || m_nudging) && m_ops.clearCollisions)
        m_ops.clearCollisions();
    if ((m_keyDrag || m_shaping) && m_ops.lockRanges)
        m_ops.lockRanges(false);
    m_keyDrag = m_shaping = m_stretching = m_nudging = false;
    m_mode = Mode::Idle;
    m_pressed = false;
    m_readout.clear();
    notifyChanged();
    emit cancelled();
}

void KeyframeInteraction::onSessionCommitted()
{
    if (!m_nudging)
        return;
    m_nudging = false;
    if (m_ops.clearCollisions)
        m_ops.clearCollisions();
    notifyChanged();
}

void KeyframeInteraction::applyTyped(double value)
{
    if (!m_keyDrag)
        return;
    if (m_entryTarget == QLatin1String("time")) {
        const double lo = m_scene.clipStart;
        m_dt = std::clamp(value, lo, lo + m_scene.clipDuration) - m_origin;
    } else if (curves()) {
        m_useOverride = true;
        m_overrideValue = value - m_pressValue;
    }
    applyKeyDrag();
    m_mode = Mode::Idle;
    finishKeyDrag();
}

#include "moc_KeyframeInteraction.cpp"
