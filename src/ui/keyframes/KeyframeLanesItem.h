#pragma once

#include "ui/keyframes/KeyframeDot.h"

#include <QColor>
#include <QList>
#include <QPointer>
#include <QQuickPaintedItem>
#include <QVariantMap>

class KeyframeController;
class KeyframeRowsModel;
class KeyframeSelection;
class KeyframeTimeView;

// The Keyframes panel's lanes: every visible row's background, the clip span, and its key dots
// (diamonds, or squares for holds) with selection, the collision ring and hover, drawn in one pass
// from KeyframeRowsModel, KeyframeSelection and KeyframeTimeView. It draws only; the pointer is
// handled by the panel's interaction surface.
class KeyframeLanesItem : public QQuickPaintedItem
{
    Q_OBJECT

    Q_PROPERTY(KeyframeRowsModel *rows READ rows WRITE setRows NOTIFY rowsSourceChanged)
    Q_PROPERTY(KeyframeSelection *selection READ selection WRITE setSelection NOTIFY selectionSourceChanged)
    Q_PROPERTY(KeyframeTimeView *timeView READ timeView WRITE setTimeView NOTIFY timeViewSourceChanged)
    Q_PROPERTY(QObject *controller READ controller WRITE setController NOTIFY controllerSourceChanged)
    Q_PROPERTY(qreal contentY MEMBER m_contentY NOTIFY viewChanged)
    Q_PROPERTY(qreal rowHeight MEMBER m_rowHeight NOTIFY viewChanged)
    Q_PROPERTY(int hoverRow MEMBER m_hoverRow NOTIFY viewChanged)
    Q_PROPERTY(int dotHoverRow MEMBER m_dotHoverRow NOTIFY viewChanged)
    Q_PROPERTY(qreal dotHoverSeconds MEMBER m_dotHoverSeconds NOTIFY viewChanged)
    Q_PROPERTY(qreal borderWidth MEMBER m_borderWidth NOTIFY viewChanged)
    Q_PROPERTY(int animationDuration MEMBER m_animationDuration NOTIFY styleChanged)
    Q_PROPERTY(QVariantMap laneStyle MEMBER m_laneStyle NOTIFY styleChanged)
    Q_PROPERTY(QVariantMap dotStyle MEMBER m_dotStyleMap NOTIFY styleChanged)

public:
    explicit KeyframeLanesItem(QQuickItem *parent = nullptr);

    KeyframeRowsModel *rows() const { return m_rows; }
    void setRows(KeyframeRowsModel *rows);
    KeyframeSelection *selection() const { return m_selection; }
    void setSelection(KeyframeSelection *selection);
    KeyframeTimeView *timeView() const { return m_timeView; }
    void setTimeView(KeyframeTimeView *view);
    QObject *controller() const;
    void setController(QObject *controller);

    void paint(QPainter *painter) override;

signals:
    void rowsSourceChanged();
    void selectionSourceChanged();
    void timeViewSourceChanged();
    void controllerSourceChanged();
    void viewChanged();
    void styleChanged();

protected:
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;

private:
    struct Ref
    {
        QString prop;
        double seconds = 0;
    };
    struct Dot
    {
        double seconds = 0;
        bool hold = false;
        QList<Ref> refs;
    };
    struct Lane
    {
        bool group = false;
        bool enabled = true;
        QString keyProp;
        QList<Dot> dots;
    };

    void reloadLanes();
    QColor laneColor(const char *key) const;

    QPointer<KeyframeRowsModel> m_rows;
    QPointer<KeyframeSelection> m_selection;
    QPointer<KeyframeTimeView> m_timeView;
    QPointer<KeyframeController> m_controller;
    QList<Lane> m_lanes;
    qreal m_contentY = 0;
    qreal m_rowHeight = 32;
    int m_hoverRow = -1;
    int m_dotHoverRow = -1;
    qreal m_dotHoverSeconds = 0;
    qreal m_borderWidth = 1;
    int m_animationDuration = 150;
    QVariantMap m_laneStyle;
    QVariantMap m_dotStyleMap;
    KeyframeDot::Tween m_tween;
    bool m_repaintScheduled = false;
};
