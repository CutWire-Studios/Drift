#include "ui/timeline/TimelineCursors.h"

#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPolygonF>
#include <QTransform>
#include <QtMath>

namespace {

QHash<quint64, QCursor> &cache()
{
    static QHash<quint64, QCursor> c;
    return c;
}

const QColor kOutline(255, 255, 255);
const QColor kFill(20, 20, 20);

struct Glyph {
    QPainterPath lines;
    QPainterPath fills;
};

void head(QPainterPath &fills, QPointF tip, qreal dx, qreal dy, qreal size)
{
    const QPointF back = tip - QPointF(dx, dy) * size;
    const QPointF side(-dy * size * 0.75, dx * size * 0.75);
    fills.addPolygon(QPolygonF({tip, back + side, back - side}));
    fills.closeSubpath();
}

void hArrows(Glyph &g, qreal cx, qreal y, qreal half, qreal headSize)
{
    g.lines.moveTo(cx - half + headSize, y);
    g.lines.lineTo(cx + half - headSize, y);
    head(g.fills, {cx - half, y}, -1, 0, headSize);
    head(g.fills, {cx + half, y}, 1, 0, headSize);
}

void vArrows(Glyph &g, qreal x, qreal cy, qreal half, qreal headSize)
{
    g.lines.moveTo(x, cy - half + headSize);
    g.lines.lineTo(x, cy + half - headSize);
    head(g.fills, {x, cy - half}, 0, -1, headSize);
    head(g.fills, {x, cy + half}, 0, 1, headSize);
}

void bowtie(Glyph &g, qreal cx, qreal cy, qreal halfW, qreal halfH)
{
    g.fills.addPolygon(QPolygonF({{cx - halfW, cy - halfH}, {cx, cy}, {cx - halfW, cy + halfH}}));
    g.fills.closeSubpath();
    g.fills.addPolygon(QPolygonF({{cx + halfW, cy - halfH}, {cx, cy}, {cx + halfW, cy + halfH}}));
    g.fills.closeSubpath();
}

void bracket(Glyph &g, qreal x, qreal top, qreal bot, qreal serif)
{
    g.lines.moveTo(x + serif, top);
    g.lines.lineTo(x, top);
    g.lines.lineTo(x, bot);
    g.lines.lineTo(x + serif, bot);
}

void paintGlyph(QPainter &p, const Glyph &g)
{
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(kOutline, 3.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawPath(g.lines);
    p.setBrush(kOutline);
    p.setPen(QPen(kOutline, 2.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawPath(g.fills);

    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(kFill, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawPath(g.lines);
    p.setBrush(kFill);
    p.setPen(Qt::NoPen);
    p.drawPath(g.fills);
}

// Premiere Selection-tool trim pointer: a bar sized to the clip with a solid triangle pointing
// toward the clip interior. side < 0 is the left edge.
QCursor paintTrim(int side, int heightPx, qreal dpr)
{
    const int h = qBound(18, heightPx, 160);
    const int w = 22;
    QPixmap pixmap(QSize(qCeil(w * dpr), qCeil(h * dpr)));
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const qreal barX = side < 0 ? 14.0 : 7.0;
    const qreal top = 1.0;
    const qreal bot = h - 2.0;
    const qreal midY = h * 0.5;
    const qreal tipX = side < 0 ? w - 2.0 : 2.0;
    const qreal baseX = side < 0 ? barX + 1.0 : barX - 1.0;
    const qreal arrowH = qBound(5.0, h * 0.18, 9.0);

    QPainterPath arrow;
    arrow.moveTo(tipX, midY);
    arrow.lineTo(baseX, midY - arrowH);
    arrow.lineTo(baseX, midY + arrowH);
    arrow.closeSubpath();

    painter.setPen(QPen(kOutline, 3.2, Qt::SolidLine, Qt::FlatCap, Qt::MiterJoin));
    painter.drawLine(QPointF(barX, top), QPointF(barX, bot));
    painter.setPen(QPen(kOutline, 2.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(kOutline);
    painter.drawPath(arrow);

    painter.setPen(QPen(kFill, 1.5, Qt::SolidLine, Qt::FlatCap, Qt::MiterJoin));
    painter.drawLine(QPointF(barX, top), QPointF(barX, bot));
    painter.setBrush(kFill);
    painter.setPen(Qt::NoPen);
    painter.drawPath(arrow);

    return QCursor(pixmap, qRound(barX), qRound(midY));
}

// A craft knife leaning right with its tip on the hotspot, so the cut lands under the blade's
// point. The trim tools add an arrow toward the side that gets dropped.
QCursor paintBlade(TimelineCursorKind kind, qreal dpr)
{
    const QSize size(26, 26);
    const QPointF tip(5, 23);
    QPixmap pixmap(QSize(qCeil(size.width() * dpr), qCeil(size.height() * dpr)));
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing, true);

    QPainterPath blade;
    blade.addPolygon(QPolygonF({{0, 0}, {-2.6, -10.5}, {2.8, -10.5}, {2.8, -5}}));
    blade.closeSubpath();
    QPainterPath collar;
    collar.addRect(QRectF(-3.4, -13.2, 6.8, 2.7));
    QPainterPath handle;
    handle.addRoundedRect(QRectF(-2.9, -24, 5.8, 10.8), 1.6, 1.6);

    const QTransform knife = QTransform().translate(tip.x(), tip.y()).rotate(30);
    const QPainterPath all = knife.map(blade.united(collar).united(handle));

    p.setPen(QPen(kOutline, 3.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(kOutline);
    p.drawPath(all);

    p.setPen(QPen(kFill, 1.1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(QColor(206, 212, 220));
    p.drawPath(knife.map(blade));
    p.setBrush(kFill);
    p.drawPath(knife.map(collar));
    p.drawPath(knife.map(handle));
    p.setPen(QPen(QColor(120, 126, 134), 1.0, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(knife.map(QLineF(0, -21.5, 0, -15.5)));

    if (kind != TimelineCursorKind::Blade) {
        Glyph arrow;
        const bool start = kind == TimelineCursorKind::TrimStartTool;
        const qreal y = start ? 5 : 21;
        const qreal from = start ? 11 : 13;
        const qreal to = start ? 3 : 23;
        arrow.lines.moveTo(from, y);
        arrow.lines.lineTo(start ? to + 3 : to - 3, y);
        head(arrow.fills, {to, y}, start ? -1 : 1, 0, 4);
        paintGlyph(p, arrow);
    }
    p.end();
    return QCursor(pixmap, qRound(tip.x()), qRound(tip.y()));
}

struct Spec {
    QSize size = {24, 24};
    QPointF hotspot = {12, 12};
    Glyph glyph;
    QTransform transform;
};

Spec specFor(TimelineCursorKind kind)
{
    Spec s;
    Glyph &g = s.glyph;
    const QTransform mirror = QTransform().translate(24, 0).scale(-1, 1);

    switch (kind) {
    case TimelineCursorKind::Move:
    case TimelineCursorKind::MoveActive: {
        const bool active = kind == TimelineCursorKind::MoveActive;
        const qreal arm = active ? 7.5 : 9.5;
        g.lines.moveTo(12 - arm + 3, 12);
        g.lines.lineTo(12 + arm - 3, 12);
        g.lines.moveTo(12, 12 - arm + 3);
        g.lines.lineTo(12, 12 + arm - 3);
        head(g.fills, {12 - arm, 12}, -1, 0, 4.5);
        head(g.fills, {12 + arm, 12}, 1, 0, 4.5);
        head(g.fills, {12, 12 - arm}, 0, -1, 4.5);
        head(g.fills, {12, 12 + arm}, 0, 1, 4.5);
        if (active)
            g.fills.addEllipse(QPointF(12, 12), 3.6, 3.6);
        break;
    }
    case TimelineCursorKind::Blade:
    case TimelineCursorKind::TrimStartTool:
    case TimelineCursorKind::TrimEndTool:
    case TimelineCursorKind::TrimIn:
    case TimelineCursorKind::TrimOut:
        break;
    case TimelineCursorKind::FadeIn:
    case TimelineCursorKind::FadeOut:
        s.size = {26, 24};
        s.hotspot = {13, 12};
        g.lines.moveTo(4, 10);
        g.lines.cubicTo(11, 10, 14, 5, 22, 3);
        hArrows(g, 13, 18, 11, 4);
        if (kind == TimelineCursorKind::FadeOut)
            s.transform = QTransform().translate(26, 0).scale(-1, 1);
        break;
    case TimelineCursorKind::TransitionResize:
        bowtie(g, 12, 12, 4, 4);
        bracket(g, 3, 4, 20, 3);
        bracket(g, 21, 4, 20, -3);
        break;
    case TimelineCursorKind::TransitionMove:
        bowtie(g, 12, 7, 4.5, 4);
        hArrows(g, 12, 18, 10.5, 4.5);
        s.hotspot = {12, 12};
        break;
    case TimelineCursorKind::PlayheadScrub:
        g.fills.addPolygon(QPolygonF({{6, 2}, {18, 2}, {18, 6}, {12, 11}, {6, 6}}));
        g.fills.closeSubpath();
        g.lines.moveTo(12, 11);
        g.lines.lineTo(12, 22);
        g.lines.moveTo(8, 17);
        g.lines.lineTo(3, 17);
        g.lines.moveTo(16, 17);
        g.lines.lineTo(21, 17);
        head(g.fills, {2, 17}, -1, 0, 4);
        head(g.fills, {22, 17}, 1, 0, 4);
        break;
    case TimelineCursorKind::TrackReorder:
        vArrows(g, 9, 12, 10.5, 4.5);
        for (int i = 0; i < 3; ++i) {
            g.lines.moveTo(15, 7 + i * 5);
            g.lines.lineTo(21, 7 + i * 5);
        }
        break;
    case TimelineCursorKind::TrackResizeH:
    case TimelineCursorKind::TrackResizeV:
        g.lines.moveTo(12, 3);
        g.lines.lineTo(12, 21);
        g.lines.moveTo(8, 12);
        g.lines.lineTo(16, 12);
        head(g.fills, {2, 12}, -1, 0, 4.5);
        head(g.fills, {22, 12}, 1, 0, 4.5);
        if (kind == TimelineCursorKind::TrackResizeV)
            s.transform = QTransform().translate(12, 12).rotate(90).translate(-12, -12);
        break;
    case TimelineCursorKind::Pan: {
        QPainterPath hand;
        hand.addRoundedRect(QRectF(5.5, 11, 13, 9), 3.5, 3.5);
        for (int i = 0; i < 4; ++i)
            hand.addRoundedRect(QRectF(5.5 + i * 3.4, 6.5, 3.2, 8), 1.6, 1.6);
        hand.addEllipse(QPointF(5, 14), 2.4, 3.2);
        g.fills = hand.simplified();
        break;
    }
    }
    return s;
}

} // namespace

bool timelineCursorKindFromName(const QString &name, TimelineCursorKind *kind)
{
    static const QHash<QString, TimelineCursorKind> names = {
        {QStringLiteral("move"), TimelineCursorKind::Move},
        {QStringLiteral("moveActive"), TimelineCursorKind::MoveActive},
        {QStringLiteral("blade"), TimelineCursorKind::Blade},
        {QStringLiteral("trimStartTool"), TimelineCursorKind::TrimStartTool},
        {QStringLiteral("trimEndTool"), TimelineCursorKind::TrimEndTool},
        {QStringLiteral("trimIn"), TimelineCursorKind::TrimIn},
        {QStringLiteral("trimOut"), TimelineCursorKind::TrimOut},
        {QStringLiteral("fadeIn"), TimelineCursorKind::FadeIn},
        {QStringLiteral("fadeOut"), TimelineCursorKind::FadeOut},
        {QStringLiteral("transitionResize"), TimelineCursorKind::TransitionResize},
        {QStringLiteral("transitionMove"), TimelineCursorKind::TransitionMove},
        {QStringLiteral("playheadScrub"), TimelineCursorKind::PlayheadScrub},
        {QStringLiteral("trackReorder"), TimelineCursorKind::TrackReorder},
        {QStringLiteral("trackResizeH"), TimelineCursorKind::TrackResizeH},
        {QStringLiteral("trackResizeV"), TimelineCursorKind::TrackResizeV},
        {QStringLiteral("pan"), TimelineCursorKind::Pan},
    };
    const auto it = names.constFind(name);
    if (it == names.constEnd())
        return false;
    *kind = *it;
    return true;
}

QCursor timelineCursor(TimelineCursorKind kind, int heightPx, qreal dpr)
{
    dpr = qBound(1.0, dpr, 4.0);
    const bool trim = kind == TimelineCursorKind::TrimIn || kind == TimelineCursorKind::TrimOut;
    const int h = trim ? qBound(18, heightPx, 160) : 0;
    const quint64 key = (quint64(kind) << 48) | (quint64(h) << 32) | quint64(qRound(dpr * 100));
    const auto it = cache().constFind(key);
    if (it != cache().constEnd())
        return *it;

    if (trim)
        return *cache().insert(key, paintTrim(kind == TimelineCursorKind::TrimIn ? -1 : 1, h, dpr));
    if (kind == TimelineCursorKind::Blade || kind == TimelineCursorKind::TrimStartTool
        || kind == TimelineCursorKind::TrimEndTool)
        return *cache().insert(key, paintBlade(kind, dpr));

    const Spec spec = specFor(kind);
    QPixmap pixmap(QSize(qCeil(spec.size.width() * dpr), qCeil(spec.size.height() * dpr)));
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setTransform(spec.transform, true);
    paintGlyph(painter, spec.glyph);
    painter.end();
    return *cache().insert(key, QCursor(pixmap, qRound(spec.hotspot.x()), qRound(spec.hotspot.y())));
}

void clearTimelineCursorCache()
{
    cache().clear();
}
