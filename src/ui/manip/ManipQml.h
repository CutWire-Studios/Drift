#pragma once

#include "ui/manip/ClipFrameMapper.h"
#include "ui/manip/ManipModifiers.h"

#include <QMatrix4x4>
#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QVariantList>
#include <QVariantMap>

namespace drift::manip {

// ClipFrameMapper has no setters QML can reach, so QML holds this and the gadget is rebuilt from
// its properties. Bindings that call the mapping functions should also read `revision`, which
// is what tells them the frame moved.
class ClipFrameMapperItem : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QSizeF canvasSize READ canvasSize WRITE setCanvasSize NOTIFY changed)
    Q_PROPERTY(QRectF viewportRect READ viewportRect WRITE setViewportRect NOTIFY changed)
    Q_PROPERTY(QRectF layoutRect READ layoutRect WRITE setLayoutRect NOTIFY changed)
    Q_PROPERTY(double rotation READ rotation WRITE setRotation NOTIFY changed)
    Q_PROPERTY(QPointF pivot READ pivot WRITE setPivot NOTIFY changed)
    Q_PROPERTY(bool hasPivot READ hasPivot WRITE setHasPivot NOTIFY changed)
    Q_PROPERTY(QMatrix4x4 pose READ pose WRITE setPose NOTIFY changed)
    Q_PROPERTY(bool hasPose READ hasPose WRITE setHasPose NOTIFY changed)
    Q_PROPERTY(QMatrix4x4 parentMatrix READ parentMatrix WRITE setParentMatrix NOTIFY changed)
    Q_PROPERTY(bool hasParent READ hasParent WRITE setHasParent NOTIFY changed)
    Q_PROPERTY(double viewScale READ viewScale NOTIFY changed)
    Q_PROPERTY(int revision READ revision NOTIFY changed)

public:
    explicit ClipFrameMapperItem(QObject *parent = nullptr);

    QSizeF canvasSize() const { return m_canvas; }
    void setCanvasSize(const QSizeF &v);
    QRectF viewportRect() const { return m_viewport; }
    void setViewportRect(const QRectF &v);
    QRectF layoutRect() const { return m_layout; }
    void setLayoutRect(const QRectF &v);
    double rotation() const { return m_rotation; }
    void setRotation(double v);
    QPointF pivot() const { return m_pivot; }
    void setPivot(const QPointF &v);
    bool hasPivot() const { return m_hasPivot; }
    void setHasPivot(bool v);
    QMatrix4x4 pose() const { return m_pose; }
    void setPose(const QMatrix4x4 &v);
    bool hasPose() const { return m_hasPose; }
    void setHasPose(bool v);
    QMatrix4x4 parentMatrix() const { return m_parent; }
    void setParentMatrix(const QMatrix4x4 &v);
    bool hasParent() const { return m_hasParent; }
    void setHasParent(bool v);
    double viewScale() const { return m_mapper.viewScale(); }
    int revision() const { return m_revision; }

    Q_INVOKABLE QPointF canvasToViewport(const QPointF &p) const { return m_mapper.canvasToViewport(p); }
    Q_INVOKABLE QPointF viewportToCanvas(const QPointF &p) const { return m_mapper.viewportToCanvas(p); }
    Q_INVOKABLE QPointF layoutToCanvas(const QPointF &p) const { return m_mapper.layoutToCanvas(p); }
    Q_INVOKABLE QPointF canvasToLayout(const QPointF &p) const { return m_mapper.canvasToLayout(p); }
    Q_INVOKABLE QPointF clipToCanvas(const QPointF &n) const { return m_mapper.clipToCanvas(n); }
    Q_INVOKABLE QPointF canvasToClip(const QPointF &p) const { return m_mapper.canvasToClip(p); }
    Q_INVOKABLE QPointF clipToViewport(const QPointF &n) const { return m_mapper.clipToViewport(n); }
    Q_INVOKABLE QPointF viewportToClip(const QPointF &p) const { return m_mapper.viewportToClip(p); }
    Q_INVOKABLE QPointF toParentLocal(const QPointF &p) const { return m_mapper.toParentLocal(p); }

signals:
    void changed();

private:
    template <typename T> void assign(T &field, const T &value);
    void rebuild();

    QSizeF m_canvas{1, 1};
    QRectF m_viewport{0, 0, 1, 1};
    QRectF m_layout{0, 0, 1, 1};
    double m_rotation = 0.0;
    QPointF m_pivot;
    bool m_hasPivot = false;
    QMatrix4x4 m_pose;
    bool m_hasPose = false;
    QMatrix4x4 m_parent;
    bool m_hasParent = false;
    int m_revision = 0;
    ClipFrameMapper m_mapper;
};

// Registered as the `Manip` singleton: the pieces of the interaction core that are plain functions.
class ManipService : public QObject
{
    Q_OBJECT
    Q_PROPERTY(double snapTolerancePx READ snapTolerancePx NOTIFY snapToleranceChanged)

public:
    explicit ManipService(QObject *parent = nullptr);

    double snapTolerancePx() const;

    // `modifiers` is the event's Qt::KeyboardModifiers; the same intents the touch chips write.
    Q_INVOKABLE drift::manip::ManipModifiers fromQtFlags(int modifiers) const;
    // value pulled onto the nearest of `targets` within the shared tolerance at `screenPerUnit`
    // px per unit. Returns {value, snapped, target}; target is -1 when nothing was in reach.
    Q_INVOKABLE QVariantMap snapValue(double value, const QVariantList &targets, double screenPerUnit,
                                      bool noSnap) const;
    Q_INVOKABLE double constrainAngle(double degrees, double stepDeg = 15.0) const;

signals:
    void snapToleranceChanged();
};

} // namespace drift::manip
