#pragma once

#include "core/FadeShape.h"

#include <QObject>
#include <QString>
#include <QVariantList>

class AppController;

// Fade and transition curve sessions, reached from QML as EditorState.curves. Each candidate is
// auditioned on the live clip or transition until apply commits it, or end restores the prior
// shape. The two sessions stay prefixed so their properties do not collide.
class CurveEditorController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool fadeActive READ fadeActive NOTIFY fadeSessionChanged)
    Q_PROPERTY(QVariantList fadePoints READ fadePoints NOTIFY fadeChanged)
    Q_PROPERTY(QString fadeClipName READ fadeClipName NOTIFY fadeSessionChanged)
    // Cubic handles for FadeCurve::Bezier, as {c1x, c1y, c2x, c2y}. Separate from the point list
    // because the two modes edit different shapes, not two views of one.
    Q_PROPERTY(QVariantList fadeHandles READ fadeHandles NOTIFY fadeChanged)
    // "points" (polyline) or "bezier" (cubic). The editor opens on whichever the clip already
    // uses, so reopening Custom does not silently convert a bezier fade into a polyline.
    Q_PROPERTY(QString fadeMode READ fadeMode NOTIFY fadeChanged)

    // The same editor, scoped to a transition's progress curve rather than a clip's fade. Kept
    // separate from the clip session above because that one also drives animIn/animOut and the
    // linked-partner sync, none of which a transition has.
    Q_PROPERTY(bool transitionActive READ transitionActive NOTIFY transitionSessionChanged)
    Q_PROPERTY(QVariantList transitionPoints READ transitionPoints NOTIFY transitionChanged)
    Q_PROPERTY(QString transitionName READ transitionName NOTIFY transitionSessionChanged)
    Q_PROPERTY(QVariantList transitionHandles READ transitionHandles NOTIFY transitionChanged)
    Q_PROPERTY(QString transitionMode READ transitionMode NOTIFY transitionChanged)

public:
    explicit CurveEditorController(AppController *app, QObject *parent = nullptr);

    bool fadeActive() const { return m_fadeActive; }
    QVariantList fadePoints() const;
    QString fadeClipName() const { return m_fadeClipName; }
    QVariantList fadeHandles() const;
    QString fadeMode() const;
    Q_INVOKABLE void beginFade(int trackIndex, int clipIndex);
    Q_INVOKABLE void endFade();
    Q_INVOKABLE void setFadePoints(const QVariantList &points);
    Q_INVOKABLE void setFadeHandles(double c1x, double c1y, double c2x, double c2y);
    Q_INVOKABLE void resetFadePreset(const QString &preset);
    Q_INVOKABLE void applyFade();

    bool transitionActive() const { return m_transitionActive; }
    QVariantList transitionPoints() const;
    QString transitionName() const { return m_transitionName; }
    QVariantList transitionHandles() const;
    QString transitionMode() const;
    Q_INVOKABLE void beginTransition(int trackIndex, const QString &transitionId);
    Q_INVOKABLE void endTransition();
    Q_INVOKABLE void setTransitionPoints(const QVariantList &points);
    Q_INVOKABLE void setTransitionHandles(double c1x, double c1y, double c2x, double c2y);
    Q_INVOKABLE void resetTransitionPreset(const QString &preset);
    Q_INVOKABLE void applyTransition();

signals:
    void fadeSessionChanged();
    void fadeChanged();
    void fadeApplied();
    void transitionSessionChanged();
    void transitionChanged();
    void transitionApplied();

private:
    AppController *m_app = nullptr;

    bool m_fadeActive = false;
    int m_fadeTrack = -1;
    int m_fadeClipIndex = -1;
    QString m_fadeClipId;
    QString m_fadeClipName;
    drift::FadeShape m_fadeShape;
    drift::FadeCurve m_fadeBefore = drift::FadeCurve::Smooth;
    drift::FadeShape m_fadeShapeBefore;
    // Which shape the live session is editing; committed as-is by applyFade.
    drift::FadeCurve m_fadeMode = drift::FadeCurve::Custom;
    bool m_fadeApplied = false;

    bool m_transitionActive = false;
    int m_transitionTrack = -1;
    QString m_transitionId;
    QString m_transitionName;
    drift::FadeShape m_transitionShape;
    drift::FadeCurve m_transitionBefore = drift::FadeCurve::Linear;
    drift::FadeShape m_transitionShapeBefore;
    drift::FadeCurve m_transitionMode = drift::FadeCurve::Custom;
    bool m_transitionApplied = false;
};
