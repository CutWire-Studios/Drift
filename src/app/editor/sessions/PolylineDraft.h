#pragma once

#include <QList>
#include <QObject>
#include <QPointF>
#include <QVariantList>

// A polyline placed one click at a time in frame pixels (the polygon mask tool and the planar
// tracker's corners). The overlay owns the drawing; this owns the vertices, the 45-degree
// constraint, the "click the first point to close" test and the normalization.
class PolylineDraft : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QList<QPointF> points READ points NOTIFY pointsChanged)
    Q_PROPERTY(int count READ count NOTIFY pointsChanged)
    Q_PROPERTY(QPointF first READ first NOTIFY pointsChanged)
    Q_PROPERTY(QPointF last READ last NOTIFY pointsChanged)
    Q_PROPERTY(QPointF hover READ hover NOTIFY hoverChanged)
    // Vertices needed before a click on the first one closes the shape.
    Q_PROPERTY(int minPoints READ minPoints WRITE setMinPoints NOTIFY minPointsChanged)
    Q_PROPERTY(bool hoverNearFirst READ hoverNearFirst NOTIFY hoverChanged)

public:
    explicit PolylineDraft(QObject *parent = nullptr);

    QList<QPointF> points() const { return m_points; }
    int count() const { return m_points.size(); }
    QPointF first() const { return m_points.isEmpty() ? QPointF() : m_points.first(); }
    QPointF last() const { return m_points.isEmpty() ? QPointF() : m_points.last(); }
    QPointF hover() const { return m_hover; }
    int minPoints() const { return m_minPoints; }
    void setMinPoints(int n);
    bool hoverNearFirst() const;

    // Shift: p snapped to 45-degree steps around the last vertex; p itself with no vertex yet.
    Q_INVOKABLE QPointF constrained(const QPointF &p, bool constrain45) const;
    // Whether p is within tolPx of the first vertex and the shape has enough vertices to close.
    Q_INVOKABLE bool nearFirst(const QPointF &p, double tolPx) const;
    // Appends the (constrained) point. False when it lands on the vertex just placed, which is a
    // double-click's second press.
    Q_INVOKABLE bool add(const QPointF &p, bool constrain45);
    Q_INVOKABLE void moveHover(const QPointF &p, bool constrain45);
    Q_INVOKABLE void undo();
    Q_INVOKABLE void clear();
    // [{x, y}] divided by the frame size (at least 1 px); `clamp` limits each to 0..1.
    Q_INVOKABLE QVariantList normalized(double frameW, double frameH, bool clamp = false) const;

signals:
    void pointsChanged();
    void hoverChanged();
    void minPointsChanged();

private:
    QList<QPointF> m_points;
    QPointF m_hover;
    int m_minPoints = 3;
};
