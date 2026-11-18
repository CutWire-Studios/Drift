#pragma once

#include <QColor>
#include <QQuickItem>
#include <QtQml/qqmlregistration.h>

// Infinite dot grid for the node canvas. Draws only the dots inside the item, in screen space,
// from the canvas zoom and pan, so panning and zooming never rebuild any QML.
class NodeGridItem : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(qreal zoom READ zoom WRITE setZoom NOTIFY zoomChanged)
    Q_PROPERTY(qreal panX READ panX WRITE setPanX NOTIFY panXChanged)
    Q_PROPERTY(qreal panY READ panY WRITE setPanY NOTIFY panYChanged)
    Q_PROPERTY(qreal spacing READ spacing WRITE setSpacing NOTIFY spacingChanged)
    Q_PROPERTY(int majorEvery READ majorEvery WRITE setMajorEvery NOTIFY majorEveryChanged)
    Q_PROPERTY(QColor minorColor READ minorColor WRITE setMinorColor NOTIFY minorColorChanged)
    Q_PROPERTY(QColor majorColor READ majorColor WRITE setMajorColor NOTIFY majorColorChanged)

public:
    explicit NodeGridItem(QQuickItem *parent = nullptr);

    qreal zoom() const { return m_zoom; }
    void setZoom(qreal v);
    qreal panX() const { return m_panX; }
    void setPanX(qreal v);
    qreal panY() const { return m_panY; }
    void setPanY(qreal v);
    qreal spacing() const { return m_spacing; }
    void setSpacing(qreal v);
    int majorEvery() const { return m_majorEvery; }
    void setMajorEvery(int v);
    QColor minorColor() const { return m_minorColor; }
    void setMinorColor(const QColor &c);
    QColor majorColor() const { return m_majorColor; }
    void setMajorColor(const QColor &c);

signals:
    void zoomChanged();
    void panXChanged();
    void panYChanged();
    void spacingChanged();
    void majorEveryChanged();
    void minorColorChanged();
    void majorColorChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *old, UpdatePaintNodeData *) override;

private:
    qreal m_zoom = 1.0;
    qreal m_panX = 0.0;
    qreal m_panY = 0.0;
    qreal m_spacing = 20.0;
    int m_majorEvery = 5;
    QColor m_minorColor = QColor(255, 255, 255, 20);
    QColor m_majorColor = QColor(255, 255, 255, 40);
};
