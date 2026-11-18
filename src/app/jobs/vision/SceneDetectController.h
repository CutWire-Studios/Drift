#pragma once

#include "core/project/clip/Clip.h"
#include "engine/ml/vision/detection/SceneDetect.h"

#include <QAtomicInt>
#include <QObject>
#include <QSharedPointer>
#include <QString>
#include <QVariantList>

#include <functional>

class AppController;

// Shot detection for a video clip, reached from QML as EditorState.scenes.
// Detected shots for the clip named by clipId. Analysis state, not project state:
// it describes the source media, so it is cached on disk rather than saved (see
// engine/ml/vision/detection/SceneDetect.h) and is never pushed through the undo stack.
class SceneDetectController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QVariantList list READ list NOTIFY listChanged)
    Q_PROPERTY(QString clipId READ clipId NOTIFY listChanged)
    // Source file the live analysis describes, so the panel can ask for thumbnails.
    Q_PROPERTY(QString clipPath READ clipPath NOTIFY listChanged)
    Q_PROPERTY(int clipRotationCorrection READ clipRotationCorrection NOTIFY listChanged)
    Q_PROPERTY(bool detecting READ detecting NOTIFY detectingChanged)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)

public:
    explicit SceneDetectController(AppController *app, QObject *parent = nullptr);

    QVariantList list() const { return m_scenes; }
    QString clipId() const { return m_sceneClipId; }
    QString clipPath() const { return m_sceneClipPath; }
    int clipRotationCorrection() const { return m_sceneClipRotationCorrection; }
    bool detecting() const { return m_sceneDetecting; }
    double progress() const { return m_sceneDetectProgress; }
    QString status() const { return m_sceneDetectStatus; }

    // Finds the shot boundaries in a clip's source range. Runs off the GUI thread; the
    // result lands in `list` and in the on-disk cache, never in the project. A cached
    // analysis for the same clip and settings is published immediately without rescanning.
    // minSceneSeconds <= 0 means "leave the engine default alone".
    Q_INVOKABLE void detect(int trackIndex, int clipIndex, bool withObjects,
                            double minSceneSeconds = 0.0);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void clear();
    // Move the playhead to a detected scene. Scenes are in source time; this maps back
    // through the clip's trim and speed to reach the right timeline position.
    Q_INVOKABLE void seekTo(int sceneIndex);
    // Map a moment in a clip's source to where it lands on the timeline, through trim, speed
    // and reverse.
    static double sourceToTimeline(const drift::Clip &clip, double sourceSeconds);
    static bool sceneMatches(const QVariantMap &scene, const QString &label, double minScore);
    // Timeline seconds of every detected boundary inside the clip that was analysed.
    QList<double> cutTimes(double minScore, const QString &label) const;
    // Failure comes back as {error: code, message}; success as {clips, at}.
    QVariantMap splitClipAtScenes(int trackIndex, int clipIndex, double minScore, const QString &label,
                                  double minGap);
    // Markers at the cuts; returns how many were added.
    int bookmarkScenes(double minScore, const QString &label, const QString &labelPrefix);
    // The Scenes tab's actions: results go to the status toast.
    Q_INVOKABLE void splitAtScenes(int trackIndex, int clipIndex);
    Q_INVOKABLE void addMarkersAtScenes();
    // Sensitivity, persisted in QSettings so a scan does not forget it between sessions.
    Q_INVOKABLE double threshold() const;
    Q_INVOKABLE void setThreshold(double threshold);

    // Scans a file's range for callers that keep their own state (Source Lab): the scene list,
    // `detecting` and the progress above are left alone. Off the GUI thread; `progress` and `done`
    // are called back on it (`done` exactly once, with ok false when the scan failed or `cancel`
    // was set, and the reason, empty for a cancel). A cached analysis is delivered without scanning.
    // minSceneSeconds <= 0 means "leave the engine default alone".
    void detectSource(const QString &path, int rotationCorrection, drift::TimeUs sourceIn,
                      drift::TimeUs sourceOut, double minSceneSeconds, double threshold,
                      const QSharedPointer<QAtomicInt> &cancel,
                      const std::function<void(double, const QString &)> &progress,
                      const std::function<void(bool ok, const drift::SceneAnalysis &analysis,
                                               const QString &error)> &done);

    // The one place a scan request is built, so the GUI and MCP paths cannot disagree about
    // the settings — and therefore about the cache key derived from them.
    drift::SceneDetectRequest sceneRequestFor(const drift::Clip &clip, bool withObjects,
                                              double minSceneSeconds) const;
    // Publishes a finished scene analysis into m_scenes, shaped for QML.
    void applySceneAnalysis(const drift::SceneAnalysis &analysis, const QString &clipId,
                            const QString &clipPath, int rotationCorrection);

signals:
    void listChanged();
    void detectingChanged();
    void progressChanged();
    void statusChanged();
    void finished(bool ok, const QString &message);

private:
    AppController *m_app = nullptr;
    // Only one clip's analysis is live at a time — the panel shows the
    // selected clip — so this needs no cache of its own beyond the on-disk one.
    QVariantList m_scenes;
    QString m_sceneClipId;
    QString m_sceneClipPath;
    int m_sceneClipRotationCorrection = 0;
    bool m_sceneDetecting = false;
    double m_sceneDetectProgress = 0.0;
    QString m_sceneDetectStatus;
    QAtomicInt m_sceneDetectCancel = 0;
    quint64 m_sceneGeneration = 0;
};
