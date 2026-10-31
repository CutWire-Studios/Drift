#pragma once

#include "app/jobs/vision/TrackingController.h"

#include <QColor>
#include <QPointer>
#include <QQuickItem>

// A tracker's confidence over its clip: one colour run per stretch of buckets (none, bad, warn,
// good), buckets about 3 px wide. Addressed by clip id, so a mask row can show the strip of the
// tracker that drives it. Repaints when the tracker's samples stream in.
class ConfidenceStripItem : public QQuickItem
{
    Q_OBJECT

    Q_PROPERTY(QObject *tracking READ tracking WRITE setTracking NOTIFY trackingChanged)
    Q_PROPERTY(QString clipId READ clipId WRITE setClipId NOTIFY clipIdChanged)
    Q_PROPERTY(QString trackerId READ trackerId WRITE setTrackerId NOTIFY trackerIdChanged)
    Q_PROPERTY(double minConfidence READ minConfidence WRITE setMinConfidence NOTIFY minConfidenceChanged)
    Q_PROPERTY(QColor emptyColor MEMBER m_empty NOTIFY colorsChanged)
    Q_PROPERTY(QColor goodColor MEMBER m_good NOTIFY colorsChanged)
    Q_PROPERTY(QColor warnColor MEMBER m_warn NOTIFY colorsChanged)
    Q_PROPERTY(QColor badColor MEMBER m_bad NOTIFY colorsChanged)

public:
    explicit ConfidenceStripItem(QQuickItem *parent = nullptr);

    QObject *tracking() const;
    void setTracking(QObject *tracking);
    QString clipId() const { return m_clipId; }
    void setClipId(const QString &id);
    QString trackerId() const { return m_trackerId; }
    void setTrackerId(const QString &id);
    double minConfidence() const { return m_minConfidence; }
    void setMinConfidence(double value);

signals:
    void trackingChanged();
    void clipIdChanged();
    void trackerIdChanged();
    void minConfidenceChanged();
    void colorsChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *old, UpdatePaintNodeData *) override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;

private:
    QPointer<TrackingController> m_tracking;
    QString m_clipId;
    QString m_trackerId;
    double m_minConfidence = 0.5;
    QColor m_empty, m_good, m_warn, m_bad;
};
