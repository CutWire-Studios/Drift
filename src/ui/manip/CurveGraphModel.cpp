#include "ui/manip/CurveGraphModel.h"

#include "core/base/Bezier.h"
#include "ui/manip/SnapService.h"

#include <QLineF>

#include <algorithm>
#include <cmath>

namespace drift::manip {

namespace {

constexpr double kDeleteMargin = 0.25;
constexpr double kHandleMinPx = 2.0;
constexpr int kCubicSamples = 64;
constexpr int kSpeedSamples = 24;

QPointF closestOnSegment(const QPointF &a, const QPointF &b, const QPointF &p)
{
    const QPointF ab = b - a;
    const double len2 = ab.x() * ab.x() + ab.y() * ab.y();
    if (len2 <= 1e-12)
        return a;
    const double t = std::clamp(((p.x() - a.x()) * ab.x() + (p.y() - a.y()) * ab.y()) / len2, 0.0, 1.0);
    return a + ab * t;
}

double dist2(const QPointF &a, const QPointF &b)
{
    const QPointF d = a - b;
    return d.x() * d.x() + d.y() * d.y();
}

QVariantList numbers(std::initializer_list<double> v)
{
    QVariantList out;
    for (double d : v)
        out.append(d);
    return out;
}

} // namespace

CurveGraphModel::CurveGraphModel(QObject *parent) : QObject(parent) {}

QString CurveGraphModel::backend() const
{
    switch (m_kind) {
    case Kind::Cubic:
        return QStringLiteral("cubic");
    case Kind::Polyline:
        return QStringLiteral("polyline");
    case Kind::Speed:
        return QStringLiteral("speed");
    case Kind::Color:
        return QStringLiteral("color");
    case Kind::Samples:
        break;
    }
    return QStringLiteral("samples");
}

void CurveGraphModel::setPlot(const QRectF &rect)
{
    if (rect == m_plot || rect.width() <= 0.0 || rect.height() <= 0.0)
        return;
    m_plot = rect;
    m_pathDirty = true;
    emit layoutChanged();
    bump();
}

void CurveGraphModel::setSessionObject(QObject *session)
{
    setSession(qobject_cast<DragSession *>(session));
}

void CurveGraphModel::setSession(DragSession *session)
{
    if (session == m_session)
        return;
    if (m_session)
        disconnect(m_session, nullptr, this, nullptr);
    m_session = session;
    if (m_session)
        connect(m_session, &DragSession::cancelled, this, &CurveGraphModel::onSessionCancelled);
    emit sessionChanged();
}

void CurveGraphModel::setReadOnly(bool on)
{
    if (on == m_readOnly)
        return;
    m_readOnly = on;
    emit setupChanged();
}

void CurveGraphModel::setSnapEnabled(bool on)
{
    if (on == m_snap)
        return;
    m_snap = on;
    emit snapChanged();
}

bool CurveGraphModel::zoomed() const
{
    return m_vx0 > m_xMin + 1e-9 || m_vx1 < m_xMax - 1e-9 || m_vu0 > 1e-9 || m_vu1 < 1.0 - 1e-9;
}

ManipModifiers CurveGraphModel::modsFrom(int qtFlags) const
{
    ManipModifiers chips;
    chips.constrain = m_chipConstrain;
    chips.breakTangent = m_chipBreak;
    chips.noSnap = !m_snap;
    return ManipModifiers::fromQt(Qt::KeyboardModifiers::fromInt(qtFlags)).merged(chips);
}

// --- Backends -------------------------------------------------------------------------------

void CurveGraphModel::setupCubic(bool unitRange)
{
    m_kind = Kind::Cubic;
    m_xMin = 0.0;
    m_xMax = 1.0;
    m_yMin = unitRange ? 0.0 : -0.5;
    m_yMax = unitRange ? 1.0 : 1.5;
    m_yLog = false;
    m_gap = 0.0;
    m_maxPoints = 2;
    m_tangents = true;
    m_canAdd = false;
    m_addAnywhere = false;
    m_marqueeOk = false;
    m_deleteOff = false;
    m_hue = false;
    m_entryUnit = QString();
    m_snapX = numbers({0, 0.25, 0.5, 0.75, 1});
    m_snapY = numbers({0, 0.25, 0.5, 0.75, 1});
    m_gridXList = numbers({0.25, 0.5, 0.75});
    m_gridYList = unitRange ? numbers({0.25, 0.5, 0.75}) : numbers({0, 0.25, 0.5, 0.75, 1});
    m_emphasisYList = numbers({0, 1});
    m_gridLabelSuffix.clear();
    setCubic({1.0 / 3.0, 1.0 / 3.0, 2.0 / 3.0, 2.0 / 3.0});
    resetView();
    emit setupChanged();
}

void CurveGraphModel::setupPolyline()
{
    m_kind = Kind::Polyline;
    m_xMin = 0.0;
    m_xMax = 1.0;
    m_yMin = 0.0;
    m_yMax = 1.0;
    m_yLog = false;
    m_gap = 0.002;
    m_maxPoints = 64;
    m_tangents = false;
    m_canAdd = true;
    m_addAnywhere = false;
    m_marqueeOk = true;
    m_deleteOff = false;
    m_hue = false;
    m_entryUnit = QString();
    m_snapX = numbers({0, 0.25, 0.5, 0.75, 1});
    m_snapY = numbers({0, 0.25, 0.5, 0.75, 1});
    m_gridXList = numbers({0.25, 0.5, 0.75});
    m_gridYList = numbers({0.25, 0.5, 0.75});
    m_emphasisYList = {};
    m_gridLabelSuffix.clear();
    m_points = {GraphPoint{0, 0}, GraphPoint{1, 1}};
    applyLocks();
    resetView();
    emit setupChanged();
}

void CurveGraphModel::setupSpeed(double minSpeed, double maxSpeed)
{
    m_kind = Kind::Speed;
    m_xMin = 0.0;
    m_xMax = 1.0;
    m_yMin = minSpeed;
    m_yMax = maxSpeed;
    m_yLog = true;
    m_gap = 0.002;
    m_maxPoints = 64;
    m_tangents = true;
    m_canAdd = true;
    m_addAnywhere = false;
    m_marqueeOk = true;
    m_deleteOff = false;
    m_hue = false;
    m_entryUnit = QStringLiteral("×");
    m_snapX = numbers({0, 0.25, 0.5, 0.75, 1});
    m_snapY = numbers({0.1, 0.5, 1, 2, 5, 10, 25, 100});
    m_gridXList = {};
    m_gridYList = numbers({0.1, 0.5, 1, 2, 5, 10, 25, 100});
    m_emphasisYList = numbers({1});
    m_gridLabelSuffix = QStringLiteral("×");
    m_points = {GraphPoint{0, 1}, GraphPoint{1, 1}};
    applyLocks();
    resetView();
    emit setupChanged();
}

void CurveGraphModel::setupColor(bool hue, std::function<double(double)> eval)
{
    m_kind = Kind::Color;
    m_xMin = 0.0;
    m_xMax = 1.0;
    m_yMin = 0.0;
    m_yMax = 1.0;
    m_yLog = false;
    m_gap = 0.01;
    m_maxPoints = 16;
    m_tangents = false;
    m_canAdd = true;
    m_addAnywhere = true;
    m_marqueeOk = false;
    m_deleteOff = true;
    m_hue = hue;
    m_eval = std::move(eval);
    m_entryUnit = QStringLiteral("/255");
    m_snapX = {};
    m_snapY = numbers({0, 1});
    m_gridXList = {};
    m_gridYList = {};
    m_emphasisYList = {};
    m_gridLabelSuffix.clear();
    m_points = {GraphPoint{0, hue ? 0.5 : 0.0}, GraphPoint{1, hue ? 0.5 : 1.0}};
    applyLocks();
    resetView();
    emit setupChanged();
}

void CurveGraphModel::setupSamples()
{
    m_kind = Kind::Samples;
    m_xMin = 0.0;
    m_xMax = 1.0;
    m_yMin = -0.5;
    m_yMax = 1.5;
    m_yLog = false;
    m_tangents = false;
    m_canAdd = false;
    m_marqueeOk = false;
    m_deleteOff = false;
    m_points.clear();
    m_gridXList = numbers({0.25, 0.5, 0.75});
    m_gridYList = numbers({0, 0.25, 0.5, 0.75, 1});
    m_emphasisYList = numbers({0, 1});
    m_gridLabelSuffix.clear();
    resetView();
    emit setupChanged();
}

void CurveGraphModel::applyLocks()
{
    const int n = int(m_points.size());
    for (int i = 0; i < n; ++i) {
        GraphPoint &p = m_points[i];
        const bool end = i == 0 || i == n - 1;
        p.lockX = end;
        p.lockY = end && (m_kind == Kind::Polyline || m_kind == Kind::Cubic);
        p.removable = !end;
    }
}

void CurveGraphModel::setCubic(const QVariantList &h)
{
    if (dragging() || h.size() < 4)
        return;
    GraphPoint a;
    a.x = 0.0;
    a.y = 0.0;
    a.t.outDx = h[0].toDouble();
    a.t.outDy = h[1].toDouble();
    GraphPoint b;
    b.x = 1.0;
    b.y = 1.0;
    b.t.inDx = h[2].toDouble() - 1.0;
    b.t.inDy = h[3].toDouble() - 1.0;
    const QList<GraphPoint> next{a, b};
    m_points = next;
    applyLocks();
    bump();
}

QVariantList CurveGraphModel::cubic() const
{
    if (m_points.size() < 2)
        return numbers({0.42, 0, 0.58, 1});
    const GraphPoint &a = m_points[0];
    const GraphPoint &b = m_points[1];
    return numbers({a.t.outDx, a.t.outDy, 1.0 + b.t.inDx, 1.0 + b.t.inDy});
}

void CurveGraphModel::setPoints(const QVariantList &list)
{
    if (dragging())
        return;
    QList<GraphPoint> next;
    for (const QVariant &v : list) {
        const QVariantMap m = v.toMap();
        GraphPoint p;
        p.x = m.value(QStringLiteral("x")).toDouble();
        p.y = m.value(QStringLiteral("y")).toDouble();
        p.t.inDx = m.value(QStringLiteral("inDx")).toDouble();
        p.t.inDy = m.value(QStringLiteral("inDy")).toDouble();
        p.t.outDx = m.value(QStringLiteral("outDx")).toDouble();
        p.t.outDy = m.value(QStringLiteral("outDy")).toDouble();
        p.smooth = m.value(QStringLiteral("smooth")).toBool();
        p.tag = m.value(QStringLiteral("tag")).toString();
        next.append(p);
    }
    setGraphPoints(next);
}

QVariantList CurveGraphModel::points() const
{
    QVariantList out;
    for (const GraphPoint &p : m_points) {
        out.append(QVariantMap{{QStringLiteral("x"), p.x},
                               {QStringLiteral("y"), p.y},
                               {QStringLiteral("inDx"), p.t.inDx},
                               {QStringLiteral("inDy"), p.t.inDy},
                               {QStringLiteral("outDx"), p.t.outDx},
                               {QStringLiteral("outDy"), p.t.outDy},
                               {QStringLiteral("smooth"), p.smooth},
                               {QStringLiteral("tag"), p.tag}});
    }
    return out;
}

void CurveGraphModel::setGraphPoints(const QList<GraphPoint> &points)
{
    if (dragging())
        return;
    m_points = points;
    applyLocks();
    m_selection.erase(std::remove_if(m_selection.begin(), m_selection.end(),
                                     [this](int i) { return i >= int(m_points.size()); }),
                      m_selection.end());
    if (m_primary >= int(m_points.size()))
        m_primary = -1;
    bump();
}

void CurveGraphModel::setSamples(const QVariantList &samples)
{
    QList<QPointF> next;
    for (const QVariant &v : samples) {
        const QVariantMap m = v.toMap();
        next.append({m.value(QStringLiteral("x")).toDouble(), m.value(QStringLiteral("y")).toDouble()});
    }
    m_samples = next;
    bump();
}

// --- Mapping --------------------------------------------------------------------------------

double CurveGraphModel::yToU(double y) const
{
    if (m_yLog) {
        const double c = std::clamp(y, m_yMin, m_yMax);
        return std::log(c / m_yMin) / std::log(m_yMax / m_yMin);
    }
    return m_yMax > m_yMin ? (y - m_yMin) / (m_yMax - m_yMin) : 0.0;
}

double CurveGraphModel::uToY(double u) const
{
    if (m_yLog)
        return m_yMin * std::exp(u * std::log(m_yMax / m_yMin));
    return m_yMin + u * (m_yMax - m_yMin);
}

QPointF CurveGraphModel::toPx(double x, double y) const
{
    const double fx = xSpan() > 0.0 ? (x - m_vx0) / xSpan() : 0.0;
    const double fu = uSpan() > 0.0 ? (yToU(y) - m_vu0) / uSpan() : 0.0;
    return {m_plot.left() + fx * m_plot.width(), m_plot.bottom() - fu * m_plot.height()};
}

QPointF CurveGraphModel::fromPx(const QPointF &px) const
{
    const double fx = (px.x() - m_plot.left()) / m_plot.width();
    const double fu = (m_plot.bottom() - px.y()) / m_plot.height();
    return {m_vx0 + fx * xSpan(), uToY(m_vu0 + fu * uSpan())};
}

void CurveGraphModel::resetView()
{
    m_vx0 = m_xMin;
    m_vx1 = m_xMax;
    m_vu0 = 0.0;
    m_vu1 = 1.0;
    m_pathDirty = true;
    emit layoutChanged();
    bump();
}

void CurveGraphModel::zoomAt(double px, double py, double factor)
{
    if (factor <= 0.0 || (!m_zoomX && !m_zoomY))
        return;
    const double fx = std::clamp((px - m_plot.left()) / m_plot.width(), 0.0, 1.0);
    const double fu = std::clamp((m_plot.bottom() - py) / m_plot.height(), 0.0, 1.0);
    const auto apply = [](double &lo, double &hi, double full0, double full1, double frac, double f) {
        const double fullSpan = full1 - full0;
        const double anchor = lo + frac * (hi - lo);
        const double span = std::clamp((hi - lo) / f, fullSpan * 0.05, fullSpan);
        lo = std::clamp(anchor - frac * span, full0, full1 - span);
        hi = lo + span;
    };
    if (m_zoomX)
        apply(m_vx0, m_vx1, m_xMin, m_xMax, fx, factor);
    if (m_zoomY)
        apply(m_vu0, m_vu1, 0.0, 1.0, fu, factor);
    m_pathDirty = true;
    emit layoutChanged();
    bump();
}

void CurveGraphModel::panBy(double dxPx, double dyPx)
{
    if (!m_zoomX && !m_zoomY)
        return;
    if (m_zoomX) {
        const double d = -dxPx / m_plot.width() * xSpan();
        const double span = xSpan();
        m_vx0 = std::clamp(m_vx0 + d, m_xMin, m_xMax - span);
        m_vx1 = m_vx0 + span;
    }
    if (m_zoomY) {
        const double d = dyPx / m_plot.height() * uSpan();
        const double span = uSpan();
        m_vu0 = std::clamp(m_vu0 + d, 0.0, 1.0 - span);
        m_vu1 = m_vu0 + span;
    }
    m_pathDirty = true;
    emit layoutChanged();
    bump();
}

// --- Geometry of the curve and its handles --------------------------------------------------

double CurveGraphModel::spanIn(int i) const
{
    if (m_kind == Kind::Cubic)
        return i == 1 ? 1.0 : 0.0;
    return i > 0 ? m_points[i].x - m_points[i - 1].x : 0.0;
}

double CurveGraphModel::spanOut(int i) const
{
    if (m_kind == Kind::Cubic)
        return i == 0 ? 1.0 : 0.0;
    return i + 1 < m_points.size() ? m_points[i + 1].x - m_points[i].x : 0.0;
}

QPointF CurveGraphModel::handlePos(int i, bool out) const
{
    const GraphPoint &p = m_points[i];
    return toPx(p.x + (out ? p.t.outDx : p.t.inDx), p.y + (out ? p.t.outDy : p.t.inDy));
}

bool CurveGraphModel::handlesShown(int i) const
{
    if (!m_tangents)
        return false;
    if (m_kind == Kind::Cubic)
        return true;
    return m_selection.contains(i);
}

bool CurveGraphModel::handleActive(int i, bool out) const
{
    if (!m_tangents || i < 0 || i >= m_points.size())
        return false;
    if (m_kind == Kind::Cubic)
        return out ? i == 0 : i == 1;
    if (out ? i + 1 >= m_points.size() : i == 0)
        return false;
    return QLineF(toPx(m_points[i].x, m_points[i].y), handlePos(i, out)).length() > kHandleMinPx;
}

bool CurveGraphModel::handleHittable(int i, bool out) const
{
    return handlesShown(i) && handleActive(i, out);
}

double CurveGraphModel::evalY(double x) const
{
    if (m_kind == Kind::Color && m_eval)
        return m_eval(x);
    const int n = int(m_points.size());
    if (n == 0)
        return 0.0;
    if (x <= m_points.first().x)
        return m_points.first().y;
    if (x >= m_points.last().x)
        return m_points.last().y;
    for (int i = 0; i + 1 < n; ++i) {
        const GraphPoint &a = m_points[i];
        const GraphPoint &b = m_points[i + 1];
        if (x < a.x || x > b.x)
            continue;
        const double span = b.x - a.x;
        if (span < 1e-9)
            return b.y;
        if (m_kind == Kind::Speed) {
            const double t = bezierParameterForX(a.x, a.x + a.t.outDx, b.x + b.t.inDx, b.x, x);
            return cubicBezier(a.y, a.y + a.t.outDy, b.y + b.t.inDy, b.y, t);
        }
        return a.y + (b.y - a.y) * (x - a.x) / span;
    }
    return m_points.last().y;
}

void CurveGraphModel::rebuildCurve() const
{
    m_path.clear();
    const auto add = [this](double x, double y) { m_path.append(toPx(x, y)); };
    switch (m_kind) {
    case Kind::Samples:
        for (const QPointF &s : m_samples)
            add(s.x(), s.y());
        break;
    case Kind::Cubic: {
        if (m_points.size() < 2)
            break;
        const double x1 = m_points[0].t.outDx, y1 = m_points[0].t.outDy;
        const double x2 = 1.0 + m_points[1].t.inDx, y2 = 1.0 + m_points[1].t.inDy;
        for (int i = 0; i <= kCubicSamples; ++i) {
            const double t = double(i) / kCubicSamples;
            add(cubicBezier(0.0, x1, x2, 1.0, t), cubicBezier(0.0, y1, y2, 1.0, t));
        }
        break;
    }
    case Kind::Polyline:
        for (const GraphPoint &p : m_points)
            add(p.x, p.y);
        break;
    case Kind::Speed:
        if (!m_points.isEmpty())
            add(m_points.first().x, m_points.first().y);
        for (int i = 0; i + 1 < m_points.size(); ++i) {
            const GraphPoint &a = m_points[i];
            const GraphPoint &b = m_points[i + 1];
            for (int k = 1; k <= kSpeedSamples; ++k) {
                const double t = double(k) / kSpeedSamples;
                add(cubicBezier(a.x, a.x + a.t.outDx, b.x + b.t.inDx, b.x, t),
                    cubicBezier(a.y, a.y + a.t.outDy, b.y + b.t.inDy, b.y, t));
            }
        }
        break;
    case Kind::Color: {
        const int steps = std::max(32, int(m_plot.width() / 2));
        for (int s = 0; s <= steps; ++s) {
            const double x = m_vx0 + xSpan() * double(s) / steps;
            add(x, std::clamp(evalY(x), m_yMin, m_yMax));
        }
        break;
    }
    }
    m_pathDirty = false;
}

QList<QPointF> CurveGraphModel::curvePath() const
{
    if (m_pathDirty)
        rebuildCurve();
    return m_path;
}

QList<QPointF> CurveGraphModel::fillPath() const
{
    QList<QPointF> out = curvePath();
    if (out.size() < 2)
        return {};
    out.append(m_plot.bottomRight());
    out.append(m_plot.bottomLeft());
    return out;
}

QVariantMap CurveGraphModel::knobAt(const QString &kind, int index, bool out, const QPointF &px, bool selected,
                                    bool smooth, bool fixed) const
{
    return {{QStringLiteral("kind"), kind},
            {QStringLiteral("index"), index},
            {QStringLiteral("out"), out},
            {QStringLiteral("x"), px.x()},
            {QStringLiteral("y"), px.y()},
            {QStringLiteral("selected"), selected},
            {QStringLiteral("smooth"), smooth},
            {QStringLiteral("hot"), index == m_hover && kind != QLatin1String("anchor")},
            {QStringLiteral("fixed"), fixed}};
}

QVariantList CurveGraphModel::knobs() const
{
    QVariantList out;
    for (int i = 0; i < m_points.size(); ++i) {
        const GraphPoint &p = m_points[i];
        const QPointF px = toPx(p.x, p.y);
        if (m_kind == Kind::Cubic) {
            out.append(knobAt(QStringLiteral("anchor"), i, false, px, false, false, true));
            continue;
        }
        out.append(knobAt(QStringLiteral("point"), i, false, px, m_selection.contains(i), !m_tangents || p.smooth,
                          p.lockX && p.lockY));
    }
    for (int i = 0; i < m_points.size(); ++i) {
        if (!handlesShown(i))
            continue;
        for (const bool o : {false, true}) {
            if (!handleActive(i, o))
                continue;
            const bool sel = m_kind == Kind::Cubic ? (m_selection.contains(i)) : (m_primary == i && m_primaryOut == o);
            out.append(knobAt(QStringLiteral("handle"), i, o, handlePos(i, o), sel, false, false));
        }
    }
    return out;
}

QVariantList CurveGraphModel::arms() const
{
    QVariantList out;
    for (int i = 0; i < m_points.size(); ++i) {
        if (!handlesShown(i))
            continue;
        const QPointF a = toPx(m_points[i].x, m_points[i].y);
        for (const bool o : {false, true}) {
            if (!handleActive(i, o))
                continue;
            out.append(QVariant::fromValue(QVariantList{a, handlePos(i, o)}));
        }
    }
    return out;
}

QVariantList CurveGraphModel::gridLines() const
{
    QVariantList out;
    const auto fmt = [this](double v) {
        return m_gridLabelSuffix.isEmpty() ? QString() : QString::number(v) + m_gridLabelSuffix;
    };
    for (const QVariant &v : m_gridXList) {
        const double x = v.toDouble();
        if (x < m_vx0 - 1e-9 || x > m_vx1 + 1e-9)
            continue;
        const double px = toPx(x, m_yMin).x();
        out.append(QVariantMap{{QStringLiteral("x1"), px},
                               {QStringLiteral("y1"), m_plot.top()},
                               {QStringLiteral("x2"), px},
                               {QStringLiteral("y2"), m_plot.bottom()},
                               {QStringLiteral("label"), QString()},
                               {QStringLiteral("emphasis"), false}});
    }
    for (const QVariant &v : m_gridYList) {
        const double y = v.toDouble();
        const double u = yToU(y);
        if (u < m_vu0 - 1e-9 || u > m_vu1 + 1e-9)
            continue;
        const double py = toPx(m_xMin, y).y();
        out.append(QVariantMap{{QStringLiteral("x1"), m_plot.left()},
                               {QStringLiteral("y1"), py},
                               {QStringLiteral("x2"), m_plot.right()},
                               {QStringLiteral("y2"), py},
                               {QStringLiteral("label"), fmt(y)},
                               {QStringLiteral("emphasis"), m_emphasisYList.contains(y)}});
    }
    return out;
}

QString CurveGraphModel::entryUnit() const
{
    return m_entryUnit;
}

QString CurveGraphModel::hudText() const
{
    if (m_deleting)
        return tr("Release to delete");
    if (m_primary < 0 || m_primary >= m_points.size())
        return {};
    const GraphPoint &p = m_points[m_primary];
    switch (m_kind) {
    case Kind::Cubic: {
        const QPointF h = m_primary == 0 ? QPointF(p.t.outDx, p.t.outDy) : QPointF(1.0 + p.t.inDx, 1.0 + p.t.inDy);
        return QStringLiteral("%1, %2").arg(h.x(), 0, 'f', 2).arg(h.y(), 0, 'f', 2);
    }
    case Kind::Polyline:
        return QStringLiteral("%1, %2").arg(p.x, 0, 'f', 2).arg(p.y, 0, 'f', 2);
    case Kind::Speed:
        if (m_gesture == Gesture::Handle) {
            const double dx = m_primaryOut ? p.t.outDx : p.t.inDx;
            const double dy = m_primaryOut ? p.t.outDy : p.t.inDy;
            return QStringLiteral("%1×, %2%").arg(p.y + dy, 0, 'f', 2).arg(std::round((p.x + dx) * 100.0));
        }
        return QStringLiteral("%1×, %2%").arg(p.y, 0, 'f', 2).arg(std::round(p.x * 100.0));
    case Kind::Color:
        if (m_hue)
            return tr("Hue %1° → %2").arg(std::lround(p.x * 360.0)).arg((p.y - 0.5) * 2.0, 0, 'f', 2);
        return tr("In %1 → Out %2").arg(std::lround(p.x * 255.0)).arg(std::lround(p.y * 255.0));
    case Kind::Samples:
        break;
    }
    return {};
}

// --- Hit testing ------------------------------------------------------------------------------

double CurveGraphModel::reachFor(int device) const
{
    return device == int(DragSession::Device::Touch) ? m_touchReach : m_mouseReach;
}

CurveGraphModel::Hit CurveGraphModel::hitTest(const QPointF &px, double reach) const
{
    Hit best;
    double bestD = reach * reach;
    for (int i = 0; i < m_points.size(); ++i) {
        for (const bool o : {false, true}) {
            if (!handleHittable(i, o))
                continue;
            const double d = dist2(px, handlePos(i, o));
            if (d <= bestD) {
                bestD = d;
                best = {o ? Target::HandleOut : Target::HandleIn, i};
            }
        }
    }
    if (m_kind != Kind::Cubic) {
        for (int i = 0; i < m_points.size(); ++i) {
            const double d = dist2(px, toPx(m_points[i].x, m_points[i].y));
            if (d < bestD) {
                bestD = d;
                best = {Target::Point, i};
            }
        }
    }
    if (best.valid() || !m_canAdd || m_readOnly)
        return best;
    if (m_addAnywhere) {
        if (m_plot.adjusted(-reach, -reach, reach, reach).contains(px))
            return {Target::Curve, -1};
        return best;
    }
    const QList<QPointF> path = curvePath();
    for (int i = 0; i + 1 < path.size(); ++i) {
        if (dist2(closestOnSegment(path[i], path[i + 1], px), px) <= reach * reach)
            return {Target::Curve, -1};
    }
    return best;
}

int CurveGraphModel::pointAt(const QPointF &px, int device) const
{
    const Hit h = hitTest(px, reachFor(device));
    return h.target == Target::Point || h.target == Target::HandleIn || h.target == Target::HandleOut ? h.index : -1;
}

int CurveGraphModel::hover(const QPointF &px, int device)
{
    int next = -1;
    if (!m_readOnly && m_gesture == Gesture::None) {
        const Hit h = hitTest(px, reachFor(device));
        if (h.target != Target::None && h.target != Target::Curve)
            next = h.index;
    }
    if (next != m_hover) {
        m_hover = next;
        emit changed();
    }
    return next;
}

void CurveGraphModel::clearHover()
{
    if (m_hover < 0)
        return;
    m_hover = -1;
    emit changed();
}

// --- Selection --------------------------------------------------------------------------------

void CurveGraphModel::selectOnly(int index)
{
    m_selection.clear();
    if (index >= 0 && index < m_points.size())
        m_selection.append(index);
    m_primary = m_selection.isEmpty() ? -1 : index;
    bump();
}

void CurveGraphModel::selectAll()
{
    m_selection.clear();
    for (int i = 0; i < m_points.size(); ++i)
        m_selection.append(i);
    m_primary = m_points.isEmpty() ? -1 : 0;
    bump();
}

void CurveGraphModel::clearSelection()
{
    if (m_selection.isEmpty() && m_primary < 0)
        return;
    m_selection.clear();
    m_primary = -1;
    bump();
}

bool CurveGraphModel::selectedSmooth() const
{
    return m_primary >= 0 && m_primary < m_points.size() && m_points[m_primary].smooth;
}

bool CurveGraphModel::canDelete() const
{
    for (const int i : m_selection) {
        if (i >= 0 && i < m_points.size() && m_points[i].removable)
            return true;
    }
    return false;
}

// --- Gesture ----------------------------------------------------------------------------------

void CurveGraphModel::bump()
{
    ++m_revision;
    m_pathDirty = true;
    emit changed();
}

void CurveGraphModel::markEdited()
{
    bump();
    emit edited();
}

void CurveGraphModel::ensureOpen(const QString &undoText)
{
    if (m_open)
        return;
    m_open = true;
    if (m_session)
        m_session->begin(QStringLiteral("curve"), DragSession::Device(m_device),
                         undoText.isEmpty() ? m_undoText : undoText);
    emit gestureBegan();
}

void CurveGraphModel::closeGesture(bool commit)
{
    const bool wasOpen = m_open;
    m_open = false;
    m_gesture = Gesture::None;
    m_marqueeActive = false;
    m_deleting = false;
    m_pendingAdd = false;
    m_dragRemovable = false;
    m_guideX = m_guideY = -1.0;
    if (wasOpen && commit) {
        if (m_session)
            m_session->commit();
        emit gestureEnded();
    }
    bump();
}

void CurveGraphModel::restoreBefore()
{
    m_points = m_orig;
    m_selection.erase(std::remove_if(m_selection.begin(), m_selection.end(),
                                     [this](int i) { return i >= int(m_points.size()); }),
                      m_selection.end());
    if (m_primary >= int(m_points.size()))
        m_primary = -1;
}

void CurveGraphModel::cancel()
{
    if (!m_open && m_gesture == Gesture::None)
        return;
    const bool wasOpen = m_open;
    m_open = false;
    m_gesture = Gesture::None;
    m_marqueeActive = false;
    m_deleting = false;
    m_pendingAdd = false;
    m_guideX = m_guideY = -1.0;
    if (wasOpen) {
        restoreBefore();
        if (m_session)
            m_session->cancel();
        emit gestureCancelled();
    }
    bump();
}

void CurveGraphModel::onSessionCancelled()
{
    if (!m_open)
        return;
    m_open = false;
    m_gesture = Gesture::None;
    m_marqueeActive = false;
    m_deleting = false;
    m_pendingAdd = false;
    restoreBefore();
    emit gestureCancelled();
    bump();
}

void CurveGraphModel::discrete(const QString &undoText, const std::function<void()> &fn)
{
    if (m_readOnly)
        return;
    const QList<GraphPoint> snapshot = m_points;
    fn();
    if (m_points == snapshot)
        return;
    const bool wasOpen = m_open;
    if (!wasOpen) {
        m_orig = snapshot;
        m_device = int(DragSession::Device::Mouse);
        ensureOpen(undoText);
    }
    markEdited();
    if (!wasOpen)
        closeGesture(true);
}

int CurveGraphModel::insertPoint(double x)
{
    if (m_points.size() >= m_maxPoints)
        return -1;
    x = std::clamp(x, m_xMin, m_xMax);
    for (const GraphPoint &p : m_points) {
        if (p.x > x - m_gap && p.x < x + m_gap)
            return -1;
    }
    int at = 0;
    while (at < m_points.size() && m_points[at].x < x)
        ++at;
    if (at == 0 || at >= m_points.size())
        return -1;

    GraphPoint q;
    q.x = x;
    q.tag = QStringLiteral("spline");
    q.removable = true;
    if (m_kind == Kind::Speed) {
        GraphPoint &a = m_points[at - 1];
        GraphPoint &b = m_points[at];
        const double ax1 = a.x + a.t.outDx, ay1 = a.y + a.t.outDy;
        const double bx2 = b.x + b.t.inDx, by2 = b.y + b.t.inDy;
        const double t = bezierParameterForX(a.x, ax1, bx2, b.x, x);
        const auto lerp = [t](double u, double v) { return u + (v - u) * t; };
        const double x01 = lerp(a.x, ax1), x12 = lerp(ax1, bx2), x23 = lerp(bx2, b.x);
        const double y01 = lerp(a.y, ay1), y12 = lerp(ay1, by2), y23 = lerp(by2, b.y);
        const double x012 = lerp(x01, x12), x123 = lerp(x12, x23);
        const double y012 = lerp(y01, y12), y123 = lerp(y12, y23);
        q.x = lerp(x012, x123);
        q.y = lerp(y012, y123);
        const bool curved = QLineF(a.x, a.y, ax1, ay1).length() > 1e-9 || QLineF(b.x, b.y, bx2, by2).length() > 1e-9;
        q.smooth = curved;
        q.t.inDx = x012 - q.x;
        q.t.inDy = y012 - q.y;
        q.t.outDx = x123 - q.x;
        q.t.outDy = y123 - q.y;
        a.t.outDx = x01 - a.x;
        a.t.outDy = y01 - a.y;
        b.t.inDx = x23 - b.x;
        b.t.inDy = y23 - b.y;
    } else {
        q.y = std::clamp(evalY(x), m_yMin, m_yMax);
    }
    m_points.insert(at, q);
    return at;
}

int CurveGraphModel::addPointAtX(double x)
{
    int idx = -1;
    discrete(tr("Add point"), [&] {
        idx = insertPoint(x);
        if (idx >= 0) {
            m_selection = {idx};
            m_primary = idx;
        }
    });
    return idx;
}

bool CurveGraphModel::outsideZone(const QPointF &px) const
{
    return px.x() < m_plot.left() - m_plot.width() * kDeleteMargin
           || px.x() > m_plot.right() + m_plot.width() * kDeleteMargin
           || px.y() < m_plot.top() - m_plot.height() * kDeleteMargin
           || px.y() > m_plot.bottom() + m_plot.height() * kDeleteMargin;
}

void CurveGraphModel::snapValue(double &x, double &y, bool lockX, bool lockY, bool noSnap)
{
    m_guideX = m_guideY = -1.0;
    if (noSnap || !m_snap)
        return;
    if (!lockX && !m_snapX.isEmpty()) {
        QList<double> targets;
        for (const QVariant &v : m_snapX)
            targets.append(v.toDouble());
        const AxisHit hit = snapAxis({x}, targets, toleranceInUnits(m_plot.width() / xSpan()));
        if (hit.snapped) {
            x = hit.target;
            m_guideX = toPx(x, m_yMin).x();
        }
    }
    if (!lockY && !m_snapY.isEmpty()) {
        QList<double> targets;
        for (const QVariant &v : m_snapY)
            targets.append(yToU(v.toDouble()));
        const AxisHit hit = snapAxis({yToU(y)}, targets, toleranceInUnits(m_plot.height() / uSpan()));
        if (hit.snapped) {
            y = uToY(hit.target);
            m_guideY = toPx(m_xMin, y).y();
        }
    }
}

void CurveGraphModel::tieEnds(int index)
{
    const int last = int(m_points.size()) - 1;
    if (!m_hue || last < 1 || (index != 0 && index != last))
        return;
    m_points[index == 0 ? last : 0].y = m_points[index].y;
}

void CurveGraphModel::moveSelection(double dx, double du)
{
    double lo = -1e18;
    double hi = 1e18;
    const int n = int(m_before.size());
    const auto fixedAt = [&](int i) { return !m_selection.contains(i) || m_before[i].lockX; };
    for (const int i : m_selection) {
        if (i < 0 || i >= n || m_before[i].lockX)
            continue;
        const GraphPoint &p = m_before[i];
        lo = std::max(lo, m_xMin - p.x);
        hi = std::min(hi, m_xMax - p.x);
        if (i > 0 && fixedAt(i - 1))
            lo = std::max(lo, m_before[i - 1].x + m_gap - p.x);
        if (i + 1 < n && fixedAt(i + 1))
            hi = std::min(hi, m_before[i + 1].x - m_gap - p.x);
    }
    dx = lo <= hi ? std::clamp(dx, lo, hi) : 0.0;
    for (const int i : m_selection) {
        if (i < 0 || i >= n)
            continue;
        GraphPoint p = m_before[i];
        if (!p.lockX)
            p.x += dx;
        if (!p.lockY)
            p.y = uToY(std::clamp(yToU(p.y) + du, 0.0, 1.0));
        m_points[i] = p;
    }
    for (const int i : m_selection)
        tieEnds(i);
}

void CurveGraphModel::applyPointDrag(const QPointF &px, const ManipModifiers &mods)
{
    QPointF eff = px;
    if (mods.constrain)
        eff = m_pressPx + lockAxis(px - m_pressPx);
    m_deleting = m_deleteOff && m_dragRemovable && outsideZone(eff);
    const GraphPoint &b = m_before[m_primary];
    QPointF v = fromPx(eff - m_grabOffset);
    double x = std::clamp(v.x(), m_xMin, m_xMax);
    double y = std::clamp(v.y(), m_yMin, m_yMax);
    snapValue(x, y, b.lockX, b.lockY, mods.noSnap);
    moveSelection(x - b.x, yToU(y) - yToU(b.y));
}

void CurveGraphModel::setHandleAbs(int i, bool out, double x, double y, bool breakTangent, bool horizontalOnly)
{
    GraphPoint &p = m_points[i];
    const bool end = i == 0 || i == int(m_points.size()) - 1;
    y = std::clamp(y, m_yMin, m_yMax);
    double dx = x - p.x;
    const double dy = y - p.y;
    TangentMode mode = TangentMode::Free;
    if (m_kind == Kind::Speed && p.smooth && !breakTangent && !end)
        mode = TangentMode::Mirrored;
    if (m_kind == Kind::Cubic)
        dx = out ? std::clamp(dx, 0.0, 1.0) : std::clamp(dx, -1.0, 0.0);
    p.t = geom::applyGraphTangentDrag(p.t, out ? geom::TangentHandle::Out : geom::TangentHandle::In, dx, dy, mode,
                                      horizontalOnly, spanIn(i), spanOut(i));
    if (m_kind == Kind::Speed && breakTangent && p.smooth)
        p.smooth = false;
}

void CurveGraphModel::applyHandleDrag(const QPointF &px, const ManipModifiers &mods)
{
    QPointF eff = px;
    const QPointF v = fromPx(eff - m_grabOffset);
    double x = v.x();
    double y = std::clamp(v.y(), m_yMin, m_yMax);
    snapValue(x, y, false, false, mods.noSnap);
    m_points[m_hit.index] = m_before[m_hit.index];
    setHandleAbs(m_hit.index, m_hit.target == Target::HandleOut, x, y, mods.breakTangent, mods.constrain);
}

QString CurveGraphModel::press(const QPointF &px, int modifiers, int device)
{
    m_pointer = px;
    if (m_readOnly || m_kind == Kind::Samples)
        return QStringLiteral("none");
    if (m_open || m_gesture != Gesture::None)
        closeGesture(true);
    const ManipModifiers mods = modsFrom(modifiers);
    m_device = device;
    m_pressPx = px;
    m_moved = false;
    m_added = false;
    m_deleting = false;
    m_pendingAdd = false;
    m_dragRemovable = false;
    m_guideX = m_guideY = -1.0;
    m_orig = m_points;
    m_before = m_points;
    m_shiftOnPress = Qt::KeyboardModifiers::fromInt(modifiers).testFlag(Qt::ShiftModifier);
    m_hover = -1;

    m_hit = hitTest(px, reachFor(device));
    switch (m_hit.target) {
    case Target::Point: {
        const int i = m_hit.index;
        m_wasSelected = m_selection.contains(i);
        if (m_shiftOnPress) {
            if (!m_wasSelected)
                m_selection.append(i);
        } else if (!m_wasSelected) {
            m_selection = {i};
        }
        m_primary = i;
        m_primaryOut = false;
        m_gesture = Gesture::Point;
        m_grabOffset = px - toPx(m_points[i].x, m_points[i].y);
        m_dragRemovable = m_points[i].removable;
        bump();
        return QStringLiteral("point");
    }
    case Target::HandleIn:
    case Target::HandleOut: {
        const int i = m_hit.index;
        m_selection = {i};
        m_primary = i;
        m_primaryOut = m_hit.target == Target::HandleOut;
        m_gesture = Gesture::Handle;
        m_grabOffset = px - handlePos(i, m_primaryOut);
        bump();
        return QStringLiteral("handle");
    }
    case Target::Curve: {
        double x = fromPx(px).x();
        if (!m_addAnywhere) {
            const QList<QPointF> path = curvePath();
            double best = 1e18;
            QPointF at = px;
            for (int i = 0; i + 1 < path.size(); ++i) {
                const QPointF c = closestOnSegment(path[i], path[i + 1], px);
                if (dist2(c, px) < best) {
                    best = dist2(c, px);
                    at = c;
                }
            }
            x = fromPx(at).x();
        }
        m_pendingAdd = true;
        m_pendingAddX = x;
        m_gesture = Gesture::Point;
        m_primary = -1;
        return QStringLiteral("curve");
    }
    case Target::None:
        break;
    }
    if (m_marqueeOk) {
        m_gesture = Gesture::Marquee;
        m_marqueeAdd = m_shiftOnPress;
        m_marqueeBase = m_marqueeAdd ? m_selection : QList<int>();
        m_marquee = QRectF(px, px);
        m_marqueeActive = true;
        if (!m_marqueeAdd) {
            m_selection.clear();
            m_primary = -1;
        }
        bump();
        return QStringLiteral("marquee");
    }
    clearSelection();
    return QStringLiteral("none");
}

void CurveGraphModel::move(const QPointF &px, int modifiers)
{
    m_pointer = px;
    if (m_gesture == Gesture::None)
        return;
    const double slop = m_device == int(DragSession::Device::Touch) ? 6.0 : 3.0;
    if (!m_moved) {
        if (QLineF(px, m_pressPx).length() < slop)
            return;
        m_moved = true;
    }
    const ManipModifiers mods = modsFrom(modifiers);
    switch (m_gesture) {
    case Gesture::Point: {
        if (m_pendingAdd) {
            m_pendingAdd = false;
            const int idx = insertPoint(m_pendingAddX);
            if (idx < 0) {
                m_gesture = Gesture::None;
                return;
            }
            m_added = true;
            m_selection = {idx};
            m_primary = idx;
            m_before = m_points;
            m_wasSelected = false;
            m_grabOffset = m_pressPx - toPx(m_points[idx].x, m_points[idx].y);
            m_dragRemovable = m_points[idx].removable;
        }
        if (m_primary < 0 || m_primary >= m_before.size())
            return;
        ensureOpen();
        applyPointDrag(px, mods);
        markEdited();
        break;
    }
    case Gesture::Handle:
        ensureOpen();
        applyHandleDrag(px, mods);
        markEdited();
        break;
    case Gesture::Marquee: {
        m_marquee = QRectF(m_pressPx, px).normalized();
        QList<int> sel = m_marqueeBase;
        for (int i = 0; i < m_points.size(); ++i) {
            if (m_marquee.contains(toPx(m_points[i].x, m_points[i].y)) && !sel.contains(i))
                sel.append(i);
        }
        m_selection = sel;
        m_primary = sel.isEmpty() ? -1 : sel.last();
        bump();
        break;
    }
    case Gesture::None:
        break;
    }
}

QString CurveGraphModel::release(const QPointF &px, int modifiers)
{
    Q_UNUSED(modifiers)
    m_pointer = px;
    const Gesture g = m_gesture;
    if (g == Gesture::None)
        return {};
    if (g == Gesture::Marquee) {
        m_gesture = Gesture::None;
        m_marqueeActive = false;
        bump();
        return QStringLiteral("select");
    }
    if (g == Gesture::Point && m_pendingAdd) {
        m_pendingAdd = false;
        m_gesture = Gesture::None;
        const int idx = addPointAtX(m_pendingAddX);
        return idx >= 0 ? QStringLiteral("add") : QString();
    }
    if (!m_moved) {
        const int i = m_hit.index;
        m_gesture = Gesture::None;
        if (g == Gesture::Point) {
            if (m_shiftOnPress && m_wasSelected)
                m_selection.removeAll(i);
            else if (!m_shiftOnPress)
                m_selection = {i};
            m_primary = m_selection.contains(i) ? i : (m_selection.isEmpty() ? -1 : m_selection.last());
        }
        bump();
        return QStringLiteral("click");
    }
    if (m_deleting) {
        QList<int> doomed;
        for (const int i : m_selection) {
            if (i < m_orig.size() && m_orig[i].removable)
                doomed.append(i);
        }
        m_points = m_orig;
        removeIndices(doomed);
        markEdited();
        closeGesture(true);
        return QStringLiteral("delete");
    }
    const bool added = m_added;
    closeGesture(true);
    return added ? QStringLiteral("add") : QStringLiteral("drag");
}

void CurveGraphModel::cancelPress()
{
    if (m_open) {
        cancel();
        return;
    }
    m_gesture = Gesture::None;
    m_marqueeActive = false;
    m_pendingAdd = false;
    bump();
}

bool CurveGraphModel::doubleClick(const QPointF &px, int device)
{
    const Hit h = hitTest(px, reachFor(device));
    if (h.target != Target::Point || !m_points[h.index].removable)
        return false;
    selectOnly(h.index);
    return deleteSelected();
}

// --- Edits ------------------------------------------------------------------------------------

void CurveGraphModel::removeIndices(QList<int> indices)
{
    std::sort(indices.begin(), indices.end(), std::greater<int>());
    for (const int i : indices) {
        if (i >= 0 && i < m_points.size() && m_points[i].removable)
            m_points.removeAt(i);
    }
    m_selection.clear();
    m_primary = -1;
}

bool CurveGraphModel::deleteSelected()
{
    if (!canDelete())
        return false;
    const QList<int> doomed = m_selection;
    bool changed = false;
    discrete(tr("Delete point"), [&] {
        const auto before = m_points.size();
        removeIndices(doomed);
        changed = m_points.size() != before;
    });
    return changed;
}

bool CurveGraphModel::setSelectedSmooth(bool smooth)
{
    if (m_kind != Kind::Speed || m_selection.isEmpty())
        return false;
    discrete(smooth ? tr("Smooth point") : tr("Corner point"), [&] {
        for (const int i : m_selection) {
            GraphPoint &p = m_points[i];
            p.smooth = smooth;
            if (!smooth) {
                p.t = {};
                continue;
            }
            const GraphPoint &prev = m_points[std::max(0, i - 1)];
            const GraphPoint &next = m_points[std::min(int(m_points.size()) - 1, i + 1)];
            const double span = std::max(1e-6, next.x - prev.x);
            const double slope = (next.y - prev.y) / span;
            const double outX = (next.x - p.x) / 3.0;
            const double inX = (p.x - prev.x) / 3.0;
            p.t.outDx = outX;
            p.t.outDy = slope * outX;
            p.t.inDx = -inX;
            p.t.inDy = -slope * inX;
        }
    });
    return true;
}

void CurveGraphModel::resetSelected()
{
    discrete(tr("Reset point"), [&] {
        if (m_kind == Kind::Cubic) {
            if (m_primary == 0)
                m_points[0].t = {0, 0, 1.0 / 3.0, 1.0 / 3.0};
            else if (m_primary == 1)
                m_points[1].t = {-1.0 / 3.0, -1.0 / 3.0, 0, 0};
            return;
        }
        for (const int i : m_selection) {
            GraphPoint &p = m_points[i];
            if (p.lockY)
                continue;
            if (m_kind == Kind::Speed) {
                p.y = 1.0;
                p.t = {};
                p.smooth = false;
            } else {
                p.y = m_hue ? 0.5 : std::clamp(p.x, m_yMin, m_yMax);
            }
            tieEnds(i);
        }
    });
}

void CurveGraphModel::resetCurve()
{
    discrete(tr("Reset curve"), [&] {
        switch (m_kind) {
        case Kind::Cubic:
            m_points[0].t = {0, 0, 1.0 / 3.0, 1.0 / 3.0};
            m_points[1].t = {-1.0 / 3.0, -1.0 / 3.0, 0, 0};
            break;
        case Kind::Polyline:
            m_points = {GraphPoint{0, 0}, GraphPoint{1, 1}};
            break;
        case Kind::Speed:
            m_points = {GraphPoint{0, 1}, GraphPoint{1, 1}};
            break;
        case Kind::Color:
            m_points = {GraphPoint{0, m_hue ? 0.5 : 0.0, {}, true, false, false, false, QStringLiteral("spline")},
                        GraphPoint{1, m_hue ? 0.5 : 1.0, {}, true, false, false, false, QStringLiteral("spline")}};
            break;
        case Kind::Samples:
            break;
        }
        if (m_kind != Kind::Cubic) {
            applyLocks();
            m_selection.clear();
            m_primary = -1;
        }
    });
}

void CurveGraphModel::nudge(int dirX, int dirY, bool shift)
{
    if (m_readOnly || m_primary < 0 || m_selection.isEmpty() || (dirX == 0 && dirY == 0))
        return;
    const double step = shift ? 10.0 : 1.0;
    double dx = 0.0;
    double du = 0.0;
    if (m_kind == Kind::Color) {
        dx = dirX * step / 255.0;
        du = dirY * step / 255.0;
    } else {
        dx = dirX * step * xSpan() / m_plot.width();
        du = dirY * step * uSpan() / m_plot.height();
    }
    const QString text = tr("Nudge points");
    const QList<GraphPoint> snapshot = m_points;
    if (m_kind == Kind::Cubic) {
        const bool out = m_primary == 0;
        const QPointF h = handlePos(m_primary, out);
        const QPointF v = fromPx(h);
        const double ny = uToY(std::clamp(yToU(v.y()) + du, 0.0, 1.0));
        setHandleAbs(m_primary, out, v.x() + dx, ny, false, false);
    } else {
        m_before = m_points;
        moveSelection(dx, du);
    }
    if (m_points == snapshot)
        return;
    if (m_session && !m_open) {
        m_session->nudge(text);
        markEdited();
        return;
    }
    const bool wasOpen = m_open;
    if (!wasOpen) {
        m_orig = snapshot;
        m_device = int(DragSession::Device::Mouse);
        ensureOpen(text);
    }
    markEdited();
    if (!wasOpen)
        closeGesture(true);
}

void CurveGraphModel::applyTyped(double value)
{
    if (m_primary < 0 || m_primary >= m_points.size())
        return;
    discrete(tr("Edit curve"), [&] {
        GraphPoint &p = m_points[m_primary];
        switch (m_kind) {
        case Kind::Cubic: {
            const double y = std::clamp(value, m_yMin, m_yMax);
            if (m_primary == 0)
                p.t.outDy = y;
            else
                p.t.inDy = y - 1.0;
            break;
        }
        case Kind::Color:
            if (!p.lockY || m_hue)
                p.y = std::clamp(m_hue ? 0.5 + value / 2.0 : value / 255.0, 0.0, 1.0);
            tieEnds(m_primary);
            break;
        default:
            if (!p.lockY)
                p.y = std::clamp(value, m_yMin, m_yMax);
            break;
        }
    });
}

} // namespace drift::manip
