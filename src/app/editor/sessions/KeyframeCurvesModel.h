#pragma once

#include "app/editor/sessions/KeyframeCurves.h"
#include "app/editor/sessions/KeyframeRowsModel.h"
#include "app/editor/sessions/KeyframeSelection.h"

#include <QColor>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QVariantList>
#include <QVariantMap>

// The curves of one Keyframes panel: a series per keyed track of every row whose curve is shown,
// each with the value range its axis is drawn in (shared, or its own when normalised), plus the
// fit, zoom and pan of the shared range, the grid, the tangent grips and the hit-testing a
// pointer needs. KeyframeCurvesItem draws from it; QML only forwards the pointer.
class KeyframeCurvesModel : public QObject
{
    Q_OBJECT

    Q_PROPERTY(KeyframeRowsModel *rowsModel READ rowsModel WRITE setRowsModel NOTIFY rowsModelChanged)
    Q_PROPERTY(KeyframeSelection *selection READ selection WRITE setSelection NOTIFY selectionChanged)
    // Series maps as KeyframeCurves::toMap gives them, each on its own axis (`valueMin`..`valueMax`), in
    // place of the ones the rows produce. Set to a list (even an empty one) to replace the rows.
    Q_PROPERTY(QVariantList externalSeries READ externalSeries WRITE setExternalSeries NOTIFY externalSeriesChanged)
    Q_PROPERTY(bool normalize READ normalize WRITE setNormalize NOTIFY inputsChanged)
    Q_PROPERTY(QVariantMap curveShown READ curveShown WRITE setCurveShown NOTIFY inputsChanged)
    Q_PROPERTY(QString focusedProp READ focusedProp WRITE setFocusedProp NOTIFY changed)
    Q_PROPERTY(QString focusedTrack READ focusedTrack WRITE setFocusedTrack NOTIFY changed)
    Q_PROPERTY(qreal graphMargin READ graphMargin WRITE setGraphMargin NOTIFY layoutChanged)
    Q_PROPERTY(qreal viewHeight READ viewHeight WRITE setViewHeight NOTIFY layoutChanged)
    Q_PROPERTY(qreal graphHeight READ graphHeight NOTIFY layoutChanged)
    Q_PROPERTY(qreal vMin READ vMin NOTIFY changed)
    Q_PROPERTY(qreal vMax READ vMax NOTIFY changed)
    Q_PROPERTY(bool frozen READ frozen WRITE setFrozen NOTIFY changed)
    Q_PROPERTY(QVariantList series READ series NOTIFY changed)
    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(int focusedIndex READ focusedIndex NOTIFY changed)
    // The focused curve's bezier handles are drawn and grabbable; off, a curve is shaped by dragging it.
    Q_PROPERTY(bool showHandles READ showHandles WRITE setShowHandles NOTIFY changed)
    Q_PROPERTY(QVariantList gridTicks READ gridTicks NOTIFY changed)
    Q_PROPERTY(QVariantMap labelRange READ labelRange NOTIFY changed)

public:
    explicit KeyframeCurvesModel(QObject *parent = nullptr);

    KeyframeRowsModel *rowsModel() const { return m_rows; }
    void setRowsModel(KeyframeRowsModel *rows);
    KeyframeSelection *selection() const { return m_selection; }
    void setSelection(KeyframeSelection *selection);
    QVariantList externalSeries() const { return m_externalList; }
    void setExternalSeries(const QVariantList &series);
    bool normalize() const { return m_normalize; }
    void setNormalize(bool normalize);
    QVariantMap curveShown() const { return m_curveShown; }
    void setCurveShown(const QVariantMap &shown);
    QString focusedProp() const { return m_focusedProp; }
    void setFocusedProp(const QString &prop);
    QString focusedTrack() const { return m_focusedTrack; }
    void setFocusedTrack(const QString &track);
    qreal graphMargin() const { return m_graphMargin; }
    void setGraphMargin(qreal margin);
    qreal viewHeight() const { return m_viewHeight; }
    void setViewHeight(qreal height);
    qreal graphHeight() const;
    qreal vMin() const { return m_vMin; }
    qreal vMax() const { return m_vMax; }
    bool frozen() const { return m_frozen; }
    void setFrozen(bool frozen);

    const QList<KeyframeCurves::Series> &seriesList() const { return m_series; }
    QVariantList series() const;
    int count() const { return m_series.size(); }
    int focusedIndex() const;
    // The series is the focused one, or no series is focused and so every one is.
    bool isFocused(int index) const;
    bool showHandles() const { return m_showHandles; }
    void setShowHandles(bool show);
    QVariantList gridTicks() const;
    QVariantMap labelRange() const;

    // Asks for the shared range to be fitted to the curves once there are some.
    Q_INVOKABLE void requestFit();
    Q_INVOKABLE void fitValues(bool selectedOnly);
    Q_INVOKABLE void zoomValuesAt(double y, double factor);
    Q_INVOKABLE void panValuesBy(double dy);
    // Holds every curve's axis where it is, so a dragged key does not move the axis under the cursor.
    Q_INVOKABLE void lockRanges();
    Q_INVOKABLE void unlockRanges();

    Q_INVOKABLE double screenY(double value, int seriesIndex) const;
    Q_INVOKABLE double valueForY(double y, int seriesIndex) const;
    // The value a vertical drag of `dy` px gives a property, from the locked axes; 0 when not locked.
    Q_INVOKABLE double valueDelta(const QString &prop, double dy) const;

signals:
    void rowsModelChanged();
    void selectionChanged();
    void externalSeriesChanged();
    void inputsChanged();
    void layoutChanged();
    void changed();

private:
    void rebuild();
    void applyRanges();
    void setRange(const KeyframeCurves::Range &range);
    const KeyframeCurves::Series *focusedSeries() const;
    bool selected(const QString &prop, double seconds) const;

    QPointer<KeyframeRowsModel> m_rows;
    QPointer<KeyframeSelection> m_selection;
    QVariantList m_externalList;
    bool m_external = false;
    bool m_normalize = false;
    QVariantMap m_curveShown;
    QString m_focusedProp;
    QString m_focusedTrack;
    qreal m_graphMargin = 8;
    qreal m_viewHeight = 0;
    qreal m_vMin = 0;
    qreal m_vMax = 1;
    bool m_frozen = false;
    bool m_showHandles = false;
    bool m_needsFit = true;
    bool m_locked = false;
    QHash<QString, KeyframeCurves::Range> m_lockedRanges;
    QList<KeyframeCurves::Series> m_base;
    QList<KeyframeCurves::Series> m_series;
    mutable QVariantList m_seriesCache;
    mutable bool m_seriesCacheValid = false;
};
