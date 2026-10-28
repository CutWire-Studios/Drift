#pragma once

#include "app/editor/sessions/KeyframeCurves.h"
#include "app/editor/sessions/KeyframeCurvesModel.h"
#include "app/editor/sessions/KeyframeSelection.h"
#include "ui/keyframes/KeyframeDot.h"

#include <QPointer>
#include <QQuickPaintedItem>
#include <QVariantList>
#include <QVariantMap>

class KeyframeController;

// Draws keyframe curves: one polyline per series, sampled with the engine's own evaluation so easing
// overshoot and handles read as playback does, and optionally the key dots with selection, the
// collision ring and hover. The series come from a KeyframeCurvesModel (the Keyframes panel) or from
// a plain list (the clip graph). The `drag*` properties show a key or tangent being dragged before
// the project catches up, for the dragged curve and, with position linked, its x/y partner.
class KeyframeCurvesItem : public QQuickPaintedItem
{
    Q_OBJECT

    Q_PROPERTY(KeyframeCurvesModel *model READ model WRITE setModel NOTIFY modelChanged)
    Q_PROPERTY(QVariantList series READ series WRITE setSeries NOTIFY seriesChanged)
    Q_PROPERTY(int focusedIndex READ focusedIndex WRITE setFocusedIndex NOTIFY viewChanged)
    Q_PROPERTY(bool curveEditing MEMBER m_curveEditing NOTIFY viewChanged)
    Q_PROPERTY(qreal pxPerSecond MEMBER m_pxPerSecond NOTIFY viewChanged)
    Q_PROPERTY(qreal originX MEMBER m_originX NOTIFY viewChanged)
    Q_PROPERTY(qreal graphMargin MEMBER m_graphMargin NOTIFY viewChanged)
    Q_PROPERTY(qreal clipStart MEMBER m_clipStart NOTIFY viewChanged)
    Q_PROPERTY(qreal clipDuration MEMBER m_clipDuration NOTIFY viewChanged)
    Q_PROPERTY(qreal lineWidth MEMBER m_lineWidth NOTIFY styleChanged)
    Q_PROPERTY(qreal lineWidthMuted MEMBER m_lineWidthMuted NOTIFY styleChanged)
    Q_PROPERTY(qreal opacityContext MEMBER m_opacityContext NOTIFY styleChanged)
    Q_PROPERTY(qreal opacityDisabled MEMBER m_opacityDisabled NOTIFY styleChanged)
    Q_PROPERTY(bool positionLinked MEMBER m_positionLinked NOTIFY dragChanged)
    Q_PROPERTY(QString dragProp MEMBER m_dragProp NOTIFY dragChanged)
    Q_PROPERTY(int dragIndex MEMBER m_dragIndex NOTIFY dragChanged)
    Q_PROPERTY(qreal dragSeconds MEMBER m_dragSeconds NOTIFY dragChanged)
    Q_PROPERTY(qreal dragValue MEMBER m_dragValue NOTIFY dragChanged)
    Q_PROPERTY(int dragTangentIndex MEMBER m_dragTangentIndex NOTIFY dragChanged)
    Q_PROPERTY(qreal dragTangentInDx MEMBER m_dragTangentInDx NOTIFY dragChanged)
    Q_PROPERTY(qreal dragTangentInDy MEMBER m_dragTangentInDy NOTIFY dragChanged)
    Q_PROPERTY(qreal dragTangentOutDx MEMBER m_dragTangentOutDx NOTIFY dragChanged)
    Q_PROPERTY(qreal dragTangentOutDy MEMBER m_dragTangentOutDy NOTIFY dragChanged)
    Q_PROPERTY(bool showKeys MEMBER m_showKeys NOTIFY keysChanged)
    Q_PROPERTY(KeyframeSelection *selection READ selection WRITE setSelection NOTIFY selectionSourceChanged)
    Q_PROPERTY(QObject *controller READ controller WRITE setController NOTIFY controllerSourceChanged)
    Q_PROPERTY(QString hoverId MEMBER m_hoverId NOTIFY keysChanged)
    Q_PROPERTY(qreal keyOpacityContext MEMBER m_keyOpacityContext NOTIFY keysChanged)
    Q_PROPERTY(int animationDuration MEMBER m_animationDuration NOTIFY keysChanged)
    Q_PROPERTY(QVariantMap dotStyle MEMBER m_dotStyleMap NOTIFY keysChanged)

public:
    explicit KeyframeCurvesItem(QQuickItem *parent = nullptr);

    KeyframeCurvesModel *model() const { return m_model; }
    void setModel(KeyframeCurvesModel *model);
    QVariantList series() const { return m_seriesSource; }
    void setSeries(const QVariantList &series);
    int focusedIndex() const;
    void setFocusedIndex(int index);
    KeyframeSelection *selection() const { return m_selection; }
    void setSelection(KeyframeSelection *selection);
    QObject *controller() const;
    void setController(QObject *controller);

    void paint(QPainter *painter) override;

signals:
    void modelChanged();
    void seriesChanged();
    void viewChanged();
    void styleChanged();
    void dragChanged();
    void keysChanged();
    void selectionSourceChanged();
    void controllerSourceChanged();

protected:
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;

private:
    bool focusedAt(int index) const;
    qreal graphHeight() const;
    QList<KeyframeCurves::Point> livePoints(int index) const;
    void pullModel();

    QPointer<KeyframeCurvesModel> m_model;
    QPointer<KeyframeSelection> m_selection;
    QPointer<KeyframeController> m_controller;
    QVariantList m_seriesSource;
    QList<KeyframeCurves::Series> m_series;
    int m_focusedIndex = -1;
    bool m_curveEditing = true;
    qreal m_pxPerSecond = 100;
    qreal m_originX = 0;
    qreal m_graphMargin = 0;
    qreal m_clipStart = 0;
    qreal m_clipDuration = 1;
    qreal m_lineWidth = 2;
    qreal m_lineWidthMuted = 1;
    qreal m_opacityContext = 0.35;
    qreal m_opacityDisabled = 0.45;
    bool m_positionLinked = false;
    QString m_dragProp;
    int m_dragIndex = -1;
    qreal m_dragSeconds = 0;
    qreal m_dragValue = 0;
    int m_dragTangentIndex = -1;
    qreal m_dragTangentInDx = 0;
    qreal m_dragTangentInDy = 0;
    qreal m_dragTangentOutDx = 0;
    qreal m_dragTangentOutDy = 0;
    bool m_showKeys = false;
    QString m_hoverId;
    qreal m_keyOpacityContext = 0.55;
    int m_animationDuration = 150;
    QVariantMap m_dotStyleMap;
    KeyframeDot::Tween m_tween;
    bool m_repaintScheduled = false;
};
