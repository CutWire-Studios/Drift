#include "ui/keyframes/KeyframeCurvesItem.h"

#include "app/editor/sessions/KeyframeController.h"
#include "app/editor/sessions/KeyframeCurvesModel.h"
#include "app/editor/sessions/KeyframeSelection.h"

#include <QPainter>
#include <QPolygonF>
#include <QTimer>

#include <algorithm>
#include <cmath>

using namespace KeyframeCurves;

KeyframeCurvesItem::KeyframeCurvesItem(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setAntialiasing(true);
    for (const auto signal : {&KeyframeCurvesItem::viewChanged, &KeyframeCurvesItem::styleChanged,
                              &KeyframeCurvesItem::keysChanged})
        connect(this, signal, this, [this] { update(); });
}

void KeyframeCurvesItem::setModel(KeyframeCurvesModel *model)
{
    if (m_model == model)
        return;
    if (m_model)
        m_model->disconnect(this);
    m_model = model;
    if (m_model)
        connect(m_model, &KeyframeCurvesModel::changed, this, [this] {
            pullModel();
            update();
        });
    pullModel();
    update();
    emit modelChanged();
}

void KeyframeCurvesItem::pullModel()
{
    if (m_model)
        m_series = m_model->seriesList();
}

void KeyframeCurvesItem::setSelection(KeyframeSelection *selection)
{
    if (m_selection == selection)
        return;
    if (m_selection)
        m_selection->disconnect(this);
    m_selection = selection;
    if (m_selection)
        connect(m_selection, &KeyframeSelection::revisionChanged, this, [this] { update(); });
    update();
    emit selectionSourceChanged();
}

QObject *KeyframeCurvesItem::controller() const
{
    return m_controller;
}

void KeyframeCurvesItem::setController(QObject *controller)
{
    KeyframeController *typed = qobject_cast<KeyframeController *>(controller);
    if (m_controller == typed)
        return;
    if (m_controller)
        m_controller->disconnect(this);
    m_controller = typed;
    if (m_controller)
        connect(m_controller, &KeyframeController::collisionsChanged, this, [this] { update(); });
    update();
    emit controllerSourceChanged();
}

void KeyframeCurvesItem::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);
    update();
}

bool KeyframeCurvesItem::focusedAt(int index) const
{
    return m_model && m_model->isFocused(index);
}

qreal KeyframeCurvesItem::graphHeight() const
{
    return std::max(1.0, height() - 2 * m_graphMargin);
}

void KeyframeCurvesItem::paintHandles(QPainter *painter, double gh) const
{
    const int fi = m_model ? m_model->focusedIndex() : -1;
    if (fi < 0 || fi >= m_series.size())
        return;
    const Series &entry = m_series.at(fi);
    const auto number = [this](const char *key, qreal fallback) {
        const QVariant v = m_handleStyleMap.value(QLatin1String(key));
        return v.isValid() ? v.toReal() : fallback;
    };
    const qreal radius = number("radius", 4);
    const qreal borderWidth = number("borderWidth", 1.5);
    const QPen leader(entry.color, number("leaderWidth", 1), Qt::SolidLine, Qt::RoundCap);
    const QPen ring(entry.color, borderWidth);
    const QColor background = m_handleStyleMap.value(QStringLiteral("background")).value<QColor>();
    const qreal leaderOpacity = number("leaderOpacity", 0.55);
    const double w = width();

    const QList<Point> points = sortedPoints(entry.points);
    for (const Handle &h : tangentHandles(points)) {
        const Point &p = points.at(h.pointIndex);
        const QPointF key(p.seconds * m_pxPerSecond - m_originX, m_graphMargin + yForValue(p.value, entry, gh));
        const QPointF tip((p.seconds + handleSeconds(p, h.outgoing)) * m_pxPerSecond - m_originX,
                          m_graphMargin + yForValue(handleValue(p, h.outgoing), entry, gh));
        if (std::max(key.x(), tip.x()) < -radius || std::min(key.x(), tip.x()) > w + radius)
            continue;
        painter->setOpacity(entry.enabled ? leaderOpacity : leaderOpacity * m_opacityDisabled);
        painter->setPen(leader);
        painter->drawLine(key, tip);
        painter->setOpacity(entry.enabled ? 1.0 : m_opacityDisabled);
        painter->setPen(ring);
        painter->setBrush(background);
        painter->drawEllipse(tip, radius - borderWidth / 2, radius - borderWidth / 2);
    }
    painter->setOpacity(1.0);
}

void KeyframeCurvesItem::paint(QPainter *painter)
{
    const double w = width();
    if (w < 1 || height() < 1 || m_pxPerSecond <= 0)
        return;
    painter->setRenderHint(QPainter::Antialiasing);

    const double gh = graphHeight();
    const double s0 = (m_originX - 4) / m_pxPerSecond;
    const double s1 = (m_originX + w + 4) / m_pxPerSecond;
    const double clipEnd = m_clipStart + m_clipDuration;

    for (int s = 0; s < m_series.size(); ++s) {
        const Series &entry = m_series.at(s);
        const bool focused = focusedAt(s);
        double alpha = m_curveEditing && !focused ? m_opacityContext : 1.0;
        if (!entry.enabled)
            alpha *= m_opacityDisabled;
        const QVector<QPointF> samples = sampleCurve(sortedPoints(entry.points), entry.baseValue, m_clipStart, clipEnd, s0, s1,
                                                     2.0 / m_pxPerSecond);
        QPolygonF line;
        line.reserve(samples.size());
        for (const QPointF &p : samples)
            line.append({p.x() * m_pxPerSecond - m_originX, m_graphMargin + yForValue(p.y(), entry, gh)});
        painter->setOpacity(alpha);
        painter->setPen(QPen(entry.color, focused ? m_lineWidth : m_lineWidthMuted, Qt::SolidLine, Qt::FlatCap,
                             Qt::RoundJoin));
        painter->setBrush(Qt::NoBrush);
        painter->drawPolyline(line);
    }
    painter->setOpacity(1.0);

    if (m_model && m_model->showHandles())
        paintHandles(painter, gh);
    if (!m_showKeys)
        return;
    const KeyframeDot::Style style = KeyframeDot::Style::fromMap(m_dotStyleMap);
    m_tween.setDuration(m_animationDuration);
    for (int s = 0; s < m_series.size(); ++s) {
        const Series &entry = m_series.at(s);
        const qreal opacity = (entry.enabled ? 1.0 : style.opacityDisabled) * (focusedAt(s) ? 1.0 : m_keyOpacityContext);
        for (const Point &p : entry.points) {
            const double x = p.seconds * m_pxPerSecond - m_originX;
            if (x <= -style.size || x >= w + style.size)
                continue;
            const QString id = KeyframeSelection::keyId(entry.prop, p.seconds);
            KeyframeDot::State state;
            state.hold = p.hold;
            state.opacity = opacity;
            state.selected = m_selection && m_selection->has(entry.prop, p.seconds);
            state.collides = m_controller && m_controller->collides(entry.prop, p.seconds);
            state.hovered = !m_hoverId.isEmpty() && id == m_hoverId;
            const qreal scale = m_tween.scaleFor(id, KeyframeDot::targetScale(style, state));
            KeyframeDot::paint(painter, QPointF(x, m_graphMargin + yForValue(p.value, entry, gh)), style, entry.color,
                               state, scale);
        }
    }
    if (m_tween.finishPass() && !m_repaintScheduled) {
        m_repaintScheduled = true;
        QTimer::singleShot(16, this, [this] {
            m_repaintScheduled = false;
            update();
        });
    }
}

#include "moc_KeyframeCurvesItem.cpp"
