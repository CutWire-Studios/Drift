#pragma once

#include <QColor>
#include <QPointer>
#include <QQuickItem>

class MaskingController;

// Draws one brush stroke as an opaque polyline with round caps, in the item's own pixels (the item
// fills the clip frame, so a stroke's normalized points scale by its size). index -1 is the stroke
// being painted; 0.. are strokes released but not yet in the composite. The stroke opacity is left
// to the item: enable `layer` and set `opacity`, so overlaps inside one stroke do not build up.
class BrushStrokeItem : public QQuickItem
{
    Q_OBJECT

    Q_PROPERTY(QObject *masking READ masking WRITE setMasking NOTIFY maskingChanged)
    Q_PROPERTY(int strokeIndex READ strokeIndex WRITE setStrokeIndex NOTIFY strokeIndexChanged)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY colorChanged)
    Q_PROPERTY(bool erase READ erase NOTIFY strokeChanged)
    Q_PROPERTY(bool hasStroke READ hasStroke NOTIFY strokeChanged)
    Q_PROPERTY(double strokeOpacity READ strokeOpacity NOTIFY strokeChanged)

public:
    explicit BrushStrokeItem(QQuickItem *parent = nullptr);

    QObject *masking() const;
    void setMasking(QObject *masking);
    int strokeIndex() const { return m_strokeIndex; }
    void setStrokeIndex(int index);
    QColor color() const { return m_color; }
    void setColor(const QColor &color);
    bool erase() const;
    bool hasStroke() const;
    double strokeOpacity() const;

signals:
    void maskingChanged();
    void strokeIndexChanged();
    void colorChanged();
    void strokeChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *old, UpdatePaintNodeData *) override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;

private:
    QPointer<MaskingController> m_masking;
    int m_strokeIndex = -1;
    QColor m_color = Qt::white;
};
