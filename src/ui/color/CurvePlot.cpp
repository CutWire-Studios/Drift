#include "ui/color/CurvePlot.h"

#include <QCursor>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QStyleHints>

#include <algorithm>
#include <cmath>

using namespace drift::curveedit;
using drift::manip::CurveGraphModel;
using drift::manip::DragSession;
using drift::manip::GraphPoint;

namespace {

const QStringList kColorChannels{QStringLiteral("master"), QStringLiteral("r"), QStringLiteral("g"),
                                 QStringLiteral("b")};
const QStringList kHueChannels{QStringLiteral("hue"), QStringLiteral("sat"), QStringLiteral("lum")};

constexpr int kHoldMs = 500;

Points toCurvePoints(const QList<GraphPoint> &list)
{
    Points out;
    out.reserve(size_t(list.size()));
    for (const GraphPoint &p : list)
        out.push_back({p.x, p.y, p.tag});
    return out;
}

QList<GraphPoint> toGraphPoints(const Points &pts)
{
    QList<GraphPoint> out;
    out.reserve(qsizetype(pts.size()));
    for (const drift::CurvePoint &p : pts) {
        GraphPoint g;
        g.x = p.x;
        g.y = p.y;
        g.tag = p.ease;
        out.append(g);
    }
    return out;
}

} // namespace

CurvePlot::CurvePlot(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setAcceptedMouseButtons(Qt::LeftButton | Qt::RightButton);
    setAcceptHoverEvents(true);
    setActiveFocusOnTab(true);
    setAntialiasing(true);
    setCursor(QCursor(Qt::CrossCursor));
    m_holdTimer.setSingleShot(true);
    m_holdTimer.setInterval(kHoldMs);
    configureModel();
    connect(&m_model, &CurveGraphModel::changed, this, [this] {
        emit selectedChanged();
        update();
    });
    connect(&m_model, &CurveGraphModel::edited, this, &CurvePlot::emitEdited);
    connect(&m_model, &CurveGraphModel::gestureBegan, this, &CurvePlot::began);
    connect(&m_model, &CurveGraphModel::gestureEnded, this, &CurvePlot::ended);
    connect(&m_model, &CurveGraphModel::gestureCancelled, this, &CurvePlot::cancelled);
    connect(&m_holdTimer, &QTimer::timeout, this, [this] {
        if (m_model.moved())
            return;
        const int idx = m_model.pointAt(m_pressPos, m_pressDevice);
        if (idx < 0)
            return;
        m_model.cancelPress();
        m_model.selectOnly(idx);
        emit contextRequested(idx, m_pressPos);
    });
    for (const auto signal : {&CurvePlot::styleChanged, &CurvePlot::curvesChanged, &CurvePlot::channelChanged,
                              &CurvePlot::hueModeChanged, &CurvePlot::selectedChanged})
        connect(this, signal, this, [this] { update(); });
}

void CurvePlot::configureModel()
{
    m_model.setupColor(m_hue, [this](double x) { return drift::evalCurve(pointsOf(m_channel), x, m_hue); });
    m_model.setPlot(QRectF(0, 0, std::max(1.0, width()), std::max(1.0, height())));
}

void CurvePlot::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);
    m_model.setPlot(QRectF(0, 0, std::max(1.0, newGeometry.width()), std::max(1.0, newGeometry.height())));
}

QStringList CurvePlot::channels() const
{
    return m_hue ? kHueChannels : kColorChannels;
}

Points CurvePlot::pointsOf(const QString &channel) const
{
    if (channel == m_channel)
        return toCurvePoints(m_model.graphPoints());
    const auto it = m_parsed.constFind(channel);
    if (it != m_parsed.cend())
        return it->points;
    return drift::identityCurve(m_hue);
}

void CurvePlot::loadModel()
{
    if (m_model.dragging())
        return;
    const auto it = m_parsed.constFind(m_channel);
    m_model.setGraphPoints(toGraphPoints(it != m_parsed.cend() && !it->points.empty() ? it->points
                                                                                       : drift::identityCurve(m_hue)));
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
    loadModel();
    emit selectedChanged();
    emit curvesChanged();
}

void CurvePlot::setHueMode(bool hue)
{
    if (hue == m_hue)
        return;
    m_model.cancelPress();
    m_hue = hue;
    m_parsed.clear();
    const QVariantMap curves = m_curves;
    m_curves.clear();
    configureModel();
    if (!channels().contains(m_channel))
        m_channel = channels().first();
    setCurves(curves);
    emit hueModeChanged();
}

void CurvePlot::setChannel(const QString &channel)
{
    if (channel == m_channel)
        return;
    m_model.cancelPress();
    m_channel = channel;
    m_model.clearSelection();
    loadModel();
    emit selectedChanged();
    emit channelChanged();
}

int CurvePlot::selected() const
{
    return m_model.selectionCount() > 0 ? m_model.primary() : -1;
}

QPointF CurvePlot::selectedPoint() const
{
    const int idx = selected();
    const auto &pts = m_model.graphPoints();
    if (idx < 0 || idx >= pts.size())
        return {};
    return {pts[idx].x, pts[idx].y};
}

void CurvePlot::emitEdited()
{
    emit changed(m_channel, toJson(toCurvePoints(m_model.graphPoints())));
    emit selectedChanged();
    update();
}

bool CurvePlot::recentlyAdded(int idx) const
{
    return idx >= 0 && idx == m_clickAddedIndex && m_clickAddedAt.isValid()
           && m_clickAddedAt.elapsed() < QGuiApplication::styleHints()->mouseDoubleClickInterval();
}

int CurvePlot::deviceOf(const QMouseEvent *event)
{
    return event->source() != Qt::MouseEventNotSynthesized ? int(DragSession::Device::Touch)
                                                            : int(DragSession::Device::Mouse);
}

void CurvePlot::resetChannel()
{
    m_model.resetCurve();
}

void CurvePlot::applyTyped(double value)
{
    m_model.applyTyped(value);
}

void CurvePlot::mousePressEvent(QMouseEvent *event)
{
    forceActiveFocus(Qt::MouseFocusReason);
    event->accept();
    m_pressPos = m_lastPos = event->position();
    m_pressDevice = deviceOf(event);
    if (event->button() == Qt::RightButton) {
        const int idx = m_model.pointAt(m_pressPos, m_pressDevice);
        if (idx >= 0) {
            m_model.selectOnly(idx);
            emit contextRequested(idx, m_pressPos);
        }
        return;
    }
    // The second press of a double-click on a point just added would drag it; skip it.
    if (recentlyAdded(m_model.pointAt(m_pressPos, m_pressDevice)))
        return;
    setKeepMouseGrab(true);
    setKeepTouchGrab(true);
    m_model.press(m_pressPos, int(event->modifiers()), m_pressDevice);
    if (m_pressDevice == int(DragSession::Device::Touch))
        m_holdTimer.start();
}

void CurvePlot::mouseMoveEvent(QMouseEvent *event)
{
    m_lastPos = event->position();
    m_model.move(event->position(), int(event->modifiers()));
    if (m_model.moved())
        m_holdTimer.stop();
    event->accept();
}

void CurvePlot::mouseReleaseEvent(QMouseEvent *event)
{
    m_holdTimer.stop();
    const QString result = m_model.release(event->position(), int(event->modifiers()));
    if (result == QLatin1String("add")) {
        m_clickAddedAt.start();
        m_clickAddedIndex = m_model.primary();
    }
    setKeepMouseGrab(false);
    setKeepTouchGrab(false);
    event->accept();
}

void CurvePlot::mouseUngrabEvent()
{
    m_holdTimer.stop();
    m_model.release(m_lastPos, 0);
    setKeepMouseGrab(false);
    setKeepTouchGrab(false);
}

void CurvePlot::touchUngrabEvent()
{
    mouseUngrabEvent();
}

void CurvePlot::hoverMoveEvent(QHoverEvent *event)
{
    m_model.hover(event->position(), int(DragSession::Device::Mouse));
}

void CurvePlot::hoverLeaveEvent(QHoverEvent *)
{
    m_model.clearHover();
}

void CurvePlot::mouseDoubleClickEvent(QMouseEvent *event)
{
    const int device = deviceOf(event);
    const int idx = m_model.pointAt(event->position(), device);
    event->accept();
    if (recentlyAdded(idx))
        return;
    if (idx < 0) {
        mousePressEvent(event);
        return;
    }
    m_model.doubleClick(event->position(), device);
}

void CurvePlot::keyPressEvent(QKeyEvent *event)
{
    const bool shift = event->modifiers() & Qt::ShiftModifier;
    switch (event->key()) {
    case Qt::Key_Escape:
        if (!m_model.dragging()) {
            event->ignore();
            return;
        }
        m_model.cancel();
        break;
    case Qt::Key_Delete:
    case Qt::Key_Backspace:
        m_model.deleteSelected();
        break;
    case Qt::Key_Left:
        m_model.nudge(-1, 0, shift);
        break;
    case Qt::Key_Right:
        m_model.nudge(1, 0, shift);
        break;
    case Qt::Key_Up:
        m_model.nudge(0, 1, shift);
        break;
    case Qt::Key_Down:
        m_model.nudge(0, -1, shift);
        break;
    default:
        if (selected() >= 0 && event->text().size() == 1 && QStringLiteral("0123456789-.").contains(event->text())) {
            emit entryRequested(event->text());
            break;
        }
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
    const auto &pts = m_model.graphPoints();
    painter->setOpacity(1.0);
    for (int i = 0; i < pts.size(); ++i) {
        const QPointF c(pts[i].x * w, (1.0 - pts[i].y) * h);
        const bool doomed = m_model.deleteZoneActive() && m_model.isSelected(i);
        painter->setBrush(doomed ? m_deleteColor : m_model.isSelected(i) ? channelColor : m_pointBackground);
        painter->setPen(QPen(doomed ? m_deleteColor : channelColor, m_knobBorderWidth));
        const double r = (i == m_model.hot() ? 1.3 : 1.0) * (m_handleRadius - m_knobBorderWidth / 2);
        painter->drawEllipse(c, r, r);
    }
    if (m_model.deleteZoneActive()) {
        painter->setBrush(Qt::NoBrush);
        painter->setPen(QPen(m_deleteColor, 2.0));
        painter->setOpacity(1.0);
        painter->drawRect(QRectF(1, 1, w - 2, h - 2));
        painter->fillRect(QRectF(0, 0, w, h), QColor(m_deleteColor.red(), m_deleteColor.green(), m_deleteColor.blue(),
                                                      int(255 * m_deleteOpacity * 0.5)));
    }
}

#include "moc_CurvePlot.cpp"
