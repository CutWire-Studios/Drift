#pragma once

#include "core/effects/Mask.h"

#include <QList>
#include <QObject>
#include <QPointF>
#include <QString>

class AppController;
class MaskingController;

// Direct manipulation of one mask adjustment from the preview overlay, addressed by clip id so a
// track inserted mid-drag cannot redirect a write. QML forwards pointer positions normalized to the
// host clip's frame; this class does the snapping, resize/rotate maths and picks static, keyframe or
// path-key writes. The live* properties are what the handles draw while the project lags the drag.
class MaskEditSession : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool active READ active NOTIFY liveChanged)
    Q_PROPERTY(QString clipId READ clipId NOTIFY liveChanged)
    Q_PROPERTY(double liveX READ liveX NOTIFY liveChanged)
    Q_PROPERTY(double liveY READ liveY NOTIFY liveChanged)
    Q_PROPERTY(double liveW READ liveW NOTIFY liveChanged)
    Q_PROPERTY(double liveH READ liveH NOTIFY liveChanged)
    Q_PROPERTY(double liveRotation READ liveRotation NOTIFY liveChanged)
    Q_PROPERTY(QList<QPointF> livePoints READ livePoints NOTIFY liveChanged)

public:
    explicit MaskEditSession(AppController *app, MaskingController *owner);

    bool active() const { return m_active; }
    QString clipId() const { return m_clipId; }
    double liveX() const { return m_x; }
    double liveY() const { return m_y; }
    double liveW() const { return m_w; }
    double liveH() const { return m_h; }
    double liveRotation() const { return m_rotation; }
    QList<QPointF> livePoints() const { return m_points; }

    // Starts a preview drag on the mask adjustment `clipId`. frameW/frameH are the clip frame's
    // size in pixels: the snap tolerance, nudge steps and rotate angle are screen distances.
    // False if the clip is not an enabled mask adjustment.
    Q_INVOKABLE bool begin(const QString &clipId, double frameW, double frameH);
    // Commits the preview drag as one undo step and clears the live state.
    Q_INVOKABLE void commit();
    // Abandons the preview drag, restoring the project.
    Q_INVOKABLE void cancel();

    // Body move. beginBody records the press (normalized, in the clip frame); moveBody takes the
    // current pointer. bypassSnap (Ctrl) skips edge/centre snapping. A freeform shifts every vertex.
    Q_INVOKABLE void beginBody(double nx, double ny);
    Q_INVOKABLE void moveBody(double nx, double ny, bool bypassSnap);
    // Resize about the opposite edge. gripDx/gripDy are -1/0/1 for the grip's side.
    Q_INVOKABLE void beginResize(int gripDx, int gripDy, double nx, double ny);
    Q_INVOKABLE void resize(double nx, double ny, bool lockAspect);
    // Angle of the pointer about the mask centre; snap15 rounds to 15 degrees.
    Q_INVOKABLE void rotate(double nx, double ny, bool snap15);
    Q_INVOKABLE void movePoint(int index, double nx, double ny);
    // Bars: both bands derive from h, clamped to 0..2.
    Q_INVOKABLE void setBarsHeight(double h);

    // One undo step each; not part of a drag.
    Q_INVOKABLE void insertPoint(const QString &clipId, int index, double nx, double ny);
    Q_INVOKABLE void removePoint(const QString &clipId, int index);
    // Arrow-key step of one pixel (ten when coarse) in the given direction, committed immediately.
    Q_INVOKABLE void nudge(const QString &clipId, int dirX, int dirY, bool coarse, double frameW, double frameH);

    static QPointF bounds(const QList<QPointF> &points, QSizeF *size);

signals:
    void liveChanged();

private:
    struct Ref
    {
        int track = -1;
        int clip = -1;
    };
    Ref find(const QString &clipId) const;
    bool load(const QString &clipId);
    // Writes the live geometry (or points) through the right channel for the mask's animation state.
    void write(bool includeRotation);
    void writeStatic(const drift::Mask &mask);
    void snapCentre(double &nx, double &ny) const;
    void syncFreeformBounds();

    AppController *m_app = nullptr;
    MaskingController *m_owner = nullptr;

    bool m_active = false;
    QString m_clipId;
    double m_frameW = 1;
    double m_frameH = 1;
    drift::Mask m_base;
    bool m_animated = false;
    bool m_freeform = false;
    bool m_pathAnimated = false;
    bool m_bodyMoved = false;

    double m_x = 0, m_y = 0, m_w = 0, m_h = 0, m_rotation = 0;
    QList<QPointF> m_points;

    double m_startX = 0, m_startY = 0, m_startW = 0, m_startH = 0;
    QList<QPointF> m_startPoints;
    double m_pressX = 0, m_pressY = 0;
    int m_gripDx = 0, m_gripDy = 0;
};
