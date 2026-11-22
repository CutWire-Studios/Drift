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

// Draws keyframe curves: one polyline per series of a KeyframeCurvesModel, sampled with the engine's
// own evaluation so easing overshoot and handles read as playback does, and optionally the key dots
// with selection, the collision ring and hover. A model that shows handles has the focused curve's
// bezier handles drawn under the key dots.
class KeyframeCurvesItem : public QQuickPaintedItem
{
    Q_OBJECT

    Q_PROPERTY(KeyframeCurvesModel *model READ model WRITE setModel NOTIFY modelChanged)
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
    Q_PROPERTY(bool showKeys MEMBER m_showKeys NOTIFY keysChanged)
    Q_PROPERTY(KeyframeSelection *selection READ selection WRITE setSelection NOTIFY selectionSourceChanged)
    Q_PROPERTY(QObject *controller READ controller WRITE setController NOTIFY controllerSourceChanged)
    Q_PROPERTY(QString hoverId MEMBER m_hoverId NOTIFY keysChanged)
    Q_PROPERTY(qreal keyOpacityContext MEMBER m_keyOpacityContext NOTIFY keysChanged)
    Q_PROPERTY(int animationDuration MEMBER m_animationDuration NOTIFY keysChanged)
    Q_PROPERTY(QVariantMap dotStyle MEMBER m_dotStyleMap NOTIFY keysChanged)
    // keyframeGraphTangentHandle tokens, for the handles a model with showHandles draws.
    Q_PROPERTY(QVariantMap handleStyle MEMBER m_handleStyleMap NOTIFY keysChanged)

public:
    explicit KeyframeCurvesItem(QQuickItem *parent = nullptr);

    KeyframeCurvesModel *model() const { return m_model; }
    void setModel(KeyframeCurvesModel *model);
    KeyframeSelection *selection() const { return m_selection; }
    void setSelection(KeyframeSelection *selection);
    QObject *controller() const;
    void setController(QObject *controller);

    void paint(QPainter *painter) override;

signals:
    void modelChanged();
    void viewChanged();
    void styleChanged();
    void keysChanged();
    void selectionSourceChanged();
    void controllerSourceChanged();

protected:
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;

private:
    bool focusedAt(int index) const;
    qreal graphHeight() const;
    void pullModel();
    void paintHandles(QPainter *painter, double gh) const;

    QPointer<KeyframeCurvesModel> m_model;
    QPointer<KeyframeSelection> m_selection;
    QPointer<KeyframeController> m_controller;
    QList<KeyframeCurves::Series> m_series;
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
    bool m_showKeys = false;
    QString m_hoverId;
    qreal m_keyOpacityContext = 0.55;
    int m_animationDuration = 150;
    QVariantMap m_dotStyleMap;
    QVariantMap m_handleStyleMap;
    KeyframeDot::Tween m_tween;
    bool m_repaintScheduled = false;
};
