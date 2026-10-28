#include "ui/color/CurvePlot.h"

#include <QCursor>
#include <QGuiApplication>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QStyleHints>

#include <algorithm>
#include <cmath>

using namespace drift::curveedit;

namespace {

const QStringList kColorChannels{QStringLiteral("master"), QStringLiteral("r"), QStringLiteral("g"),
                                 QStringLiteral("b")};
const QStringList kHueChannels{QStringLiteral("hue"), QStringLiteral("sat"), QStringLiteral("lum")};

} // namespace

CurvePlot::CurvePlot(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setAcceptedMouseButtons(Qt::LeftButton);
    setActiveFocusOnTab(true);
    setAntialiasing(true);
    setCursor(QCursor(Qt::CrossCursor));
    for (const auto signal : {&CurvePlot::styleChanged, &CurvePlot::curvesChanged, &CurvePlot::channelChanged,
                              &CurvePlot::hueModeChanged, &CurvePlot::selectedChanged})
        connect(this, signal, this, [this] { update(); });
}

QStringList CurvePlot::channels() const
{
    return m_hue ? kHueChannels : kColorChannels;
}

const Points &CurvePlot::pointsOf(const QString &channel) const
{
    if (channel == m_channel && m_hasLive)
        return m_live;
    const auto it = m_parsed.constFind(channel);
    if (it != m_parsed.cend())
        return it->points;
    static Points identity[2];
    Points &fallback = identity[m_hue ? 1 : 0];
    if (fallback.empty())
        fallback = drift::identityCurve(m_hue);
    return fallback;
}

void CurvePlot::setCurves(const QVariantMap &curves)
{
    if (curves == m_curves)
        return;
    m_curves = curves;
    for (auto it = curves.cbegin(); it != curves.cend(); ++it) {
        const QString json = it.value().toString();
        Parsed &parsed = m_parsed[it.key()];
        if (!parsed.points.empty() && parsed.json == json)
            continue;
        parsed.json = json;
        parsed.points = drift::parseCurvePoints(json, m_hue);
    }
    if (m_selected >= pointCount())
        m_selected = -1;
    emit selectedChanged();
    emit curvesChanged();
}

void CurvePlot::setHueMode(bool hue)
{
    if (hue == m_hue)
        return;
    endDrag();
    m_hue = hue;
    m_parsed.clear();
    const QVariantMap curves = m_curves;
    m_curves.clear();
    if (!channels().contains(m_channel))
        setChannel(channels().first());
    setCurves(curves);
    emit hueModeChanged();
}

void CurvePlot::setChannel(const QString &channel)
{
    if (channel == m_channel)
        return;
    endDrag();
    m_channel = channel;
    m_selected = -1;
    emit selectedChanged();
    emit channelChanged();
}

QPointF CurvePlot::selectedPoint() const
{
    const Points &pts = pointsOf(m_channel);
    if (m_selected < 0 || m_selected >= int(pts.size()))
        return {};
    return {pts[size_t(m_selected)].x, pts[size_t(m_selected)].y};
}

void CurvePlot::setSelected(int idx)
{
    if (idx == m_selected)
        return;
    m_selected = idx;
    emit selectedChanged();
}

double CurvePlot::reach() const
{
    return std::max(m_handleRadius + 4.0, 10.0);
}

bool CurvePlot::recentlyAdded(int idx) const
{
    return idx >= 0 && idx == m_clickAddedIndex && m_clickAddedAt.isValid()
           && m_clickAddedAt.elapsed() < QGuiApplication::styleHints()->mouseDoubleClickInterval();
}

void CurvePlot::openGesture()
{
    if (m_gestureBegun)
        return;
    m_gestureBegun = true;
    emit began();
}

void CurvePlot::closeGesture()
{
    if (!m_gestureBegun)
        return;
    m_gestureBegun = false;
    emit ended();
}

void CurvePlot::emitPoints(const Points &pts)
{
    m_live = pts;
    m_hasLive = true;
    emit changed(m_channel, toJson(pts));
    emit selectedChanged();
    update();
}

void CurvePlot::commitPoints(const Points &pts)
{
    openGesture();
    emitPoints(pts);
    m_hasLive = false;
    closeGesture();
}

void CurvePlot::endDrag()
{
    m_dragging = false;
    m_dragIndex = -1;
    m_hasLive = false;
    setKeepMouseGrab(false);
    setKeepTouchGrab(false);
    closeGesture();
}

void CurvePlot::resetChannel()
{
    endDrag();
    commitPoints(drift::identityCurve(m_hue));
    setSelected(-1);
}

void CurvePlot::beginPress(const QPointF &pos)
{
    Points pts = pointsOf(m_channel);
    int idx = hit(pts, size(), pos, reach());
    m_dragging = true;
    if (idx < 0) {
        idx = insertAt(pts, pos.x(), width(), m_hue);
        if (idx < 0) {
            m_dragIndex = -1;
            setSelected(-1);
            return;
        }
        openGesture();
        emitPoints(pts);
        m_clickAddedAt.start();
        m_clickAddedIndex = idx;
    } else {
        m_live = pts;
        m_hasLive = true;
    }
    m_dragIndex = idx;
    setSelected(idx);
}

void CurvePlot::mousePressEvent(QMouseEvent *event)
{
    forceActiveFocus(Qt::MouseFocusReason);
    // The second press of a double-click on a point just added would drag it; skip it.
    if (recentlyAdded(hit(pointsOf(m_channel), size(), event->position(), reach()))) {
        event->accept();
        return;
    }
    setKeepMouseGrab(true);
    setKeepTouchGrab(true);
    beginPress(event->position());
    event->accept();
}

void CurvePlot::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_dragging || m_dragIndex < 0 || !m_hasLive)
        return;
    openGesture();
    Points pts = m_live;
    m_dragIndex = dragTo(pts, m_dragIndex, event->position(), size(), m_hue);
    if (m_dragIndex < 0)
        setSelected(-1);
    emitPoints(pts);
    event->accept();
}

void CurvePlot::mouseReleaseEvent(QMouseEvent *event)
{
    endDrag();
    event->accept();
}

void CurvePlot::mouseUngrabEvent()
{
    endDrag();
}

void CurvePlot::touchUngrabEvent()
{
    endDrag();
}

void CurvePlot::mouseDoubleClickEvent(QMouseEvent *event)
{
    const int idx = hit(pointsOf(m_channel), size(), event->position(), reach());
    event->accept();
    if (recentlyAdded(idx))
        return;
    if (idx < 0) {
        mousePressEvent(event);
        return;
    }
    endDrag();
    Points pts = pointsOf(m_channel);
    if (removeAt(pts, idx)) {
        commitPoints(pts);
        setSelected(-1);
    }
}

void CurvePlot::keyPressEvent(QKeyEvent *event)
{
    const double step = (event->modifiers() & Qt::ShiftModifier ? 10.0 : 1.0) / 255.0;
    const auto nudgeSelected = [this](double dx, double dy) {
        Points pts = pointsOf(m_channel);
        if (m_selected < 0 || m_selected >= int(pts.size()))
            return;
        nudge(pts, m_selected, dx, dy, m_hue);
        commitPoints(pts);
    };
    switch (event->key()) {
    case Qt::Key_Delete:
    case Qt::Key_Backspace: {
        Points pts = pointsOf(m_channel);
        if (removeAt(pts, m_selected)) {
            commitPoints(pts);
            setSelected(-1);
        }
        break;
    }
    case Qt::Key_Left:
        nudgeSelected(-step, 0);
        break;
    case Qt::Key_Right:
        nudgeSelected(step, 0);
        break;
    case Qt::Key_Up:
        nudgeSelected(0, step);
        break;
    case Qt::Key_Down:
        nudgeSelected(0, -step);
        break;
    default:
        QQuickPaintedItem::keyPressEvent(event);
        return;
    }
    event->accept();
}

void CurvePlot::paint(QPainter *painter)
{
    const double w = width();
    const double h = height();
    if (w < 2 || h < 2)
        return;
    painter->setRenderHint(QPainter::Antialiasing);

    if (m_hue) {
        QLinearGradient band(0, 0, w, 0);
        for (int i = 0; i <= 6; ++i)
            band.setColorAt(i / 6.0, QColor::fromHslF(std::fmod(i * 60.0, 360.0) / 360.0, 0.8, 0.55));
        painter->setOpacity(0.18);
        painter->fillRect(QRectF(0, 0, w, h), band);
    }

    QPen gridPen(m_gridColor, m_gridLineWidth);
    painter->setPen(gridPen);
    painter->setOpacity(m_gridOpacity);
    for (int i = 1; i < 4; ++i) {
        painter->drawLine(QPointF(0, h * i / 4), QPointF(w, h * i / 4));
        painter->drawLine(QPointF(w * i / 4, 0), QPointF(w * i / 4, h));
    }
    if (!m_hue) {
        painter->setOpacity(m_gridOpacity * 0.5);
        painter->drawLine(QPointF(0, h), QPointF(w, 0));
    }

    const int steps = std::max(32, int(w / 2));
    const auto drawCurve = [&](const QString &ch, double alpha, double lineWidth) {
        const Points &pts = pointsOf(ch);
        QPainterPath path;
        for (int s = 0; s <= steps; ++s) {
            const double x = double(s) / steps;
            const QPointF p(x * w, (1.0 - drift::evalCurve(pts, x, m_hue)) * h);
            if (s == 0)
                path.moveTo(p);
            else
                path.lineTo(p);
        }
        painter->setOpacity(alpha);
        painter->setPen(QPen(m_channelColors.value(ch).value<QColor>(), lineWidth, Qt::SolidLine, Qt::FlatCap,
                             Qt::RoundJoin));
        painter->drawPath(path);
    };
    for (const QString &ch : channels()) {
        if (ch != m_channel)
            drawCurve(ch, 0.25, m_dimLineWidth);
    }
    drawCurve(m_channel, 1.0, m_curveLineWidth);

    const QColor channelColor = m_channelColors.value(m_channel).value<QColor>();
    const Points &pts = pointsOf(m_channel);
    painter->setOpacity(1.0);
    for (size_t i = 0; i < pts.size(); ++i) {
        const QPointF c(pts[i].x * w, (1.0 - pts[i].y) * h);
        painter->setBrush(int(i) == m_selected ? channelColor : m_pointBackground);
        painter->setPen(QPen(channelColor, m_knobBorderWidth));
        painter->drawEllipse(c, m_handleRadius - m_knobBorderWidth / 2, m_handleRadius - m_knobBorderWidth / 2);
    }
}
