#include "ui/keyframes/KeyframeLanesItem.h"

#include "app/editor/sessions/KeyframeController.h"
#include "app/editor/sessions/KeyframeRows.h"
#include "app/editor/sessions/KeyframeRowsModel.h"
#include "app/editor/sessions/KeyframeSelection.h"
#include "app/editor/sessions/KeyframeTimeView.h"

#include <QPainter>
#include <QTimer>

#include <algorithm>
#include <cmath>

KeyframeLanesItem::KeyframeLanesItem(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setAntialiasing(true);
    for (const auto signal : {&KeyframeLanesItem::viewChanged, &KeyframeLanesItem::styleChanged})
        connect(this, signal, this, [this] { update(); });
}

void KeyframeLanesItem::setRows(KeyframeRowsModel *rows)
{
    if (m_rows == rows)
        return;
    if (m_rows)
        m_rows->disconnect(this);
    m_rows = rows;
    if (m_rows) {
        connect(m_rows, &KeyframeRowsModel::rowsChanged, this, [this] {
            reloadLanes();
            update();
        });
        connect(m_rows, &KeyframeRowsModel::styleChanged, this, [this] { update(); });
    }
    reloadLanes();
    update();
    emit rowsSourceChanged();
}

void KeyframeLanesItem::setSelection(KeyframeSelection *selection)
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

void KeyframeLanesItem::setTimeView(KeyframeTimeView *view)
{
    if (m_timeView == view)
        return;
    if (m_timeView)
        m_timeView->disconnect(this);
    m_timeView = view;
    if (m_timeView) {
        for (const auto signal : {&KeyframeTimeView::pxPerSecondChanged, &KeyframeTimeView::contentXChanged,
                                  &KeyframeTimeView::clipStartChanged, &KeyframeTimeView::clipDurationChanged})
            connect(m_timeView, signal, this, [this] { update(); });
    }
    update();
    emit timeViewSourceChanged();
}

QObject *KeyframeLanesItem::controller() const
{
    return m_controller;
}

void KeyframeLanesItem::setController(QObject *controller)
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

void KeyframeLanesItem::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);
    update();
}

void KeyframeLanesItem::reloadLanes()
{
    m_lanes.clear();
    if (!m_rows)
        return;
    const QList<KeyframeRows::Row> &rows = m_rows->list();
    m_lanes.reserve(rows.size());
    for (int i = 0; i < rows.size(); ++i) {
        const KeyframeRows::Row &row = rows.at(i);
        Lane lane;
        lane.group = row.isGroup();
        lane.enabled = row.enabled;
        lane.keyProp = row.keyProp;
        for (const QVariant &d : KeyframeRows::laneDots(rows, i)) {
            const QVariantMap map = d.toMap();
            Dot dot;
            dot.seconds = map.value(QStringLiteral("seconds")).toDouble();
            dot.hold = map.value(QStringLiteral("hold")).toBool();
            for (const QVariant &r : map.value(QStringLiteral("refs")).toList()) {
                const QVariantMap rm = r.toMap();
                dot.refs.append({rm.value(QStringLiteral("prop")).toString(),
                                 rm.value(QStringLiteral("seconds")).toDouble()});
            }
            lane.dots.append(dot);
        }
        m_lanes.append(lane);
    }
}

QColor KeyframeLanesItem::laneColor(const char *key) const
{
    return m_laneStyle.value(QLatin1String(key)).value<QColor>();
}

void KeyframeLanesItem::paint(QPainter *painter)
{
    const double w = width();
    const double h = height();
    if (w < 1 || h < 1 || m_rowHeight <= 0)
        return;
    painter->setRenderHint(QPainter::Antialiasing);

    const KeyframeDot::Style dotStyle = KeyframeDot::Style::fromMap(m_dotStyleMap);
    m_tween.setDuration(m_animationDuration);
    const QColor groupBackground = laneColor("groupBackground");
    QColor alt = laneColor("backgroundAlt");
    alt.setAlphaF(m_laneStyle.value(QStringLiteral("opacityAlt")).toDouble());
    QColor hover = laneColor("backgroundHover");
    hover.setAlphaF(m_laneStyle.value(QStringLiteral("opacityHover")).toDouble());
    QColor span = laneColor("clipSpan");
    span.setAlphaF(m_laneStyle.value(QStringLiteral("opacityClipSpan")).toDouble());
    const QColor border = laneColor("border");

    const double spanX0 = m_timeView ? m_timeView->xFor(m_timeView->clipStart()) : 0;
    const double spanX1 = m_timeView ? m_timeView->xFor(m_timeView->clipStart() + m_timeView->clipDuration()) : 0;
    const double spanLeft = std::max(0.0, spanX0);
    const double spanWidth = std::max(0.0, std::min(w, spanX1) - spanLeft);

    const int first = std::max(0, int(std::floor(m_contentY / m_rowHeight)));
    const int last = std::min(int(m_lanes.size()) - 1, int(std::floor((m_contentY + h) / m_rowHeight)));
    for (int i = first; i <= last; ++i) {
        const Lane &lane = m_lanes.at(i);
        const QRectF band(0, i * m_rowHeight - m_contentY, w, m_rowHeight);
        if (lane.group)
            painter->fillRect(band, groupBackground);
        else if (i % 2 == 1)
            painter->fillRect(band, alt);
        if (m_hoverRow == i)
            painter->fillRect(band, hover);
        if (!lane.group && spanWidth > 0)
            painter->fillRect(QRectF(spanLeft, band.top(), spanWidth, band.height()), span);
        painter->fillRect(QRectF(0, band.bottom() - m_borderWidth, w, m_borderWidth), border);
    }

    for (int i = first; i <= last; ++i) {
        const Lane &lane = m_lanes.at(i);
        const double cy = i * m_rowHeight - m_contentY + m_rowHeight / 2;
        const QColor fill = lane.group ? dotStyle.summary : (m_rows ? m_rows->colorFor(lane.keyProp) : QColor());
        for (const Dot &dot : lane.dots) {
            const double cx = m_timeView ? m_timeView->xFor(dot.seconds) : 0;
            if (cx <= -dotStyle.size || cx >= w + dotStyle.size)
                continue;
            KeyframeDot::State state;
            state.hold = dot.hold;
            state.disabled = !lane.group && !lane.enabled;
            state.selected = m_selection && !dot.refs.isEmpty()
                             && std::all_of(dot.refs.cbegin(), dot.refs.cend(), [this](const Ref &r) {
                                    return m_selection->has(r.prop, r.seconds);
                                });
            state.collides = m_controller
                             && std::any_of(dot.refs.cbegin(), dot.refs.cend(), [this](const Ref &r) {
                                    return m_controller->collides(r.prop, r.seconds);
                                });
            state.hovered = m_dotHoverRow == i && std::abs(m_dotHoverSeconds - dot.seconds) < KeyframeRows::kTimeEps;
            const QString key = QString::number(i) + QLatin1Char('@') + QString::number(qRound64(dot.seconds * 1e6));
            const qreal scale = m_tween.scaleFor(key, KeyframeDot::targetScale(dotStyle, state));
            KeyframeDot::paint(painter, QPointF(cx, cy), dotStyle, fill, state, scale);
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
