#pragma once

#include "core/base/Time.h"

#include <QAtomicInt>
#include <QObject>
#include <QSet>
#include <QString>

class AppController;

// Face landmark tracking and Face Swap source ingest, reached from QML as EditorState.faces.
class FaceTrackingController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool detecting READ detecting NOTIFY detectingChanged)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)

public:
    explicit FaceTrackingController(AppController *app, QObject *parent = nullptr);

    bool detecting() const { return m_detecting; }
    double progress() const { return m_progress; }
    QString status() const { return m_status; }

    // Bakes the clip's face landmarks to a sidecar so the face warp effects have something to
    // follow. Runs off the GUI thread; the result lands on the clip through the undo stack.
    Q_INVOKABLE void detect(int trackIndex, int clipIndex);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void clearTrack(int trackIndex, int clipIndex);
    Q_INVOKABLE bool available();

    // One keyframed Ellipse mask per face in the clip's baked track, pinned to the clip, so a
    // "requires": "mask" effect — Privacy Blur is the one this exists for — covers every face and
    // follows it. The effect has to be on the clip too: a layer whose chain reads its masks is not
    // cut out by them, so masks alone would crop the picture to the faces instead of blurring them.
    //
    // Replaces what a previous run generated (Mask::faceSlot) and leaves hand-drawn masks alone,
    // so re-running after a re-scan does not stack a second set over the first.
    //
    // `padding` scales the detected face oval, which traces the skin boundary and so leaves hair,
    // ears and jaw outside it. Returns the number of faces masked, 0 with a message when the clip
    // has no usable track. One undo step tr("Mask faces").
    Q_INVOKABLE int addFaceMasks(int trackIndex, int clipIndex, double padding = 1.4);

    // Landmark a Face Swap source photo in the background and cache the result. Cheap enough
    // (one still, sub-second once the session is warm) that it gets no progress UI of its own —
    // the effect renders pass-through until the landmarks land, then the preview refreshes.
    void ingestSwapSource(const QString &photoPath);
    // Every Face Swap photo in the project that has no cached landmarks. Runs on open, because
    // the sidecar is derived and does not travel with the bundle.
    void ingestSwapSourcesInProject();

signals:
    void detectingChanged();
    void progressChanged();
    void statusChanged();
    void finished(bool ok, const QString &message);

private:
    void finalize(const QString &clipId, const QString &trackPath, drift::TimeUs srcOffsetUs);

    AppController *m_app = nullptr;
    bool m_detecting = false;
    double m_progress = 0.0;
    QString m_status;
    QAtomicInt m_cancel = 0;
    // Photos with an ingest in flight, so a slider nudge or a second clip using the same photo
    // does not queue the landmarker twice.
    QSet<QString> m_swapIngesting;
};
