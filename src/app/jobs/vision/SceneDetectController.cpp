#include "app/jobs/vision/SceneDetectController.h"

#include "app/editor/AppController.h"
#include "app/mcp/McpDetail.h"

#include <QPointer>
#include <QSettings>
#include <QtConcurrent>

using drift::mcpdetail::sceneRowsFromAnalysis;

SceneDetectController::SceneDetectController(AppController *app, QObject *parent)
    : QObject(parent)
    , m_app(app)
{
}

double SceneDetectController::threshold() const
{
    return QSettings()
        .value(QStringLiteral("scenes/threshold"), drift::SceneDetectOptions{}.threshold)
        .toDouble();
}

void SceneDetectController::setThreshold(double threshold)
{
    QSettings().setValue(QStringLiteral("scenes/threshold"), qBound(4.0, threshold, 100.0));
}

void SceneDetectController::cancel()
{
    m_sceneDetectCancel.storeRelaxed(1);
}


void SceneDetectController::clear()
{
    if (m_scenes.isEmpty() && m_sceneClipId.isEmpty())
        return;
    m_scenes.clear();
    m_sceneClipId.clear();
    m_sceneClipPath.clear();
    emit listChanged();
}

void SceneDetectController::seekTo(int sceneIndex)
{
    if (sceneIndex < 0 || sceneIndex >= m_scenes.size())
        return;

    // Resolved by id, not index: the analysis outlives any number of timeline edits.
    for (const drift::Track &track : m_app->m_project.tracks()) {
        for (const drift::Clip &clip : track.clips) {
            if (clip.id != m_sceneClipId)
                continue;

            const QVariantMap scene = m_scenes.at(sceneIndex).toMap();
            const drift::TimeUs sourceUs =
                drift::secondsToUs(scene.value(QStringLiteral("sourceStart")).toDouble());
            // Scenes are in source time. Speed and reverse mean the clip's own mapping is the
            // only thing that knows where that lands on the timeline.
            const drift::TimeUs span = clip.sourceSpanUs();
            if (span <= 0)
                return;
            const double through = double(sourceUs - clip.srcIn) / double(span);
            const drift::TimeUs at =
                clip.timelineStart
                + drift::TimeUs(qBound(0.0, through, 1.0) * double(clip.timelineDuration));
            m_app->setPlayheadUs(clip.reverse ? clip.timelineStart + clip.timelineDuration
                                             - (at - clip.timelineStart)
                                       : at);
            return;
        }
    }
}


void SceneDetectController::applySceneAnalysis(const drift::SceneAnalysis &analysis,
                                               const QString &clipId, const QString &clipPath,
                                               int rotationCorrection)
{
    const QVariantList rows = sceneRowsFromAnalysis(analysis);
    m_scenes = rows;
    m_sceneClipId = clipId;
    m_sceneClipPath = clipPath;
    m_sceneClipRotationCorrection = rotationCorrection;
    emit listChanged();
}

drift::SceneDetectRequest SceneDetectController::sceneRequestFor(const drift::Clip &clip,
                                                                 bool withObjects,
                                                                 double minSceneSeconds) const
{
    drift::SceneDetectRequest request;
    request.path = clip.path;
    request.rotationCorrection = clip.rotationCorrection;
    request.sourceIn = clip.srcIn;
    request.sourceOut = clip.srcOut;
    request.options.threshold = threshold();
    request.options.detectObjects = withObjects && m_app->objectDetectionAvailable();
    if (minSceneSeconds > 0.0)
        request.options.minSceneSeconds = minSceneSeconds;
    return request;
}

void SceneDetectController::detectSource(
    const QString &path, int rotationCorrection, drift::TimeUs sourceIn, drift::TimeUs sourceOut,
    double minSceneSeconds, double threshold, const QSharedPointer<QAtomicInt> &cancel,
    const std::function<void(double, const QString &)> &progress,
    const std::function<void(bool, const drift::SceneAnalysis &, const QString &)> &done)
{
    drift::SceneDetectRequest request;
    request.path = path;
    request.rotationCorrection = rotationCorrection;
    request.sourceIn = sourceIn;
    request.sourceOut = sourceOut;
    request.options.threshold = threshold > 0.0 ? threshold : this->threshold();
    if (minSceneSeconds > 0.0)
        request.options.minSceneSeconds = minSceneSeconds;

    drift::SceneAnalysis cached;
    if (drift::loadCachedAnalysis(request, &cached)) {
        done(true, cached, QString());
        return;
    }

    m_app->setPlaying(false);
    QPointer<SceneDetectController> self(this);
    (void)QtConcurrent::run([self, request, cancel, progress, done]() {
        QString error;
        const drift::SceneAnalysis analysis = drift::detectScenes(
            request,
            [&](double fraction, const QString &status) {
                if (cancel->loadRelaxed() != 0)
                    return false;
                QMetaObject::invokeMethod(
                    self.data(),
                    [self, progress, fraction, status]() {
                        if (self)
                            progress(fraction, status);
                    },
                    Qt::QueuedConnection);
                return true;
            },
            &error);
        const bool cancelled = cancel->loadRelaxed() != 0;
        if (!analysis.isEmpty() && !cancelled)
            drift::storeCachedAnalysis(request, analysis);
        QMetaObject::invokeMethod(
            self.data(),
            [self, done, analysis, cancelled, error]() {
                if (!self)
                    return;
                const bool ok = !analysis.isEmpty() && !cancelled;
                done(ok, analysis, cancelled ? QString() : error);
            },
            Qt::QueuedConnection);
    });
}

void SceneDetectController::detect(int trackIndex, int clipIndex, bool withObjects,
                                   double minSceneSeconds)
{
    if (m_sceneDetecting) {
        m_app->setLastMessage(tr("Already looking for scenes"), QStringLiteral("warning"));
        return;
    }
    if (trackIndex < 0 || trackIndex >= m_app->m_project.tracks().size())
        return;
    const drift::Track &track = m_app->m_project.tracks().at(trackIndex);
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    const drift::Clip clip = track.clips.at(clipIndex);
    if (clip.type != drift::ClipType::Video) {
        m_app->setLastMessage(tr("Select a video clip to find scenes in"), QStringLiteral("warning"));
        return;
    }
    if (clip.path.isEmpty() || clip.srcOut <= clip.srcIn) {
        m_app->setLastMessage(tr("Clip has no video to scan"), QStringLiteral("warning"));
        return;
    }

    const drift::SceneDetectRequest request =
        sceneRequestFor(clip, withObjects, minSceneSeconds);

    // A cached analysis is the common case once a clip has been scanned, and it costs a file
    // read rather than a decode pass — so it is worth checking before disturbing playback.
    drift::SceneAnalysis cached;
    if (drift::loadCachedAnalysis(request, &cached)
        && (!withObjects || cached.objectsScanned)) {
        applySceneAnalysis(cached, clip.id, clip.path, clip.rotationCorrection);
        m_app->setLastMessage(tr("Found %n scene(s)", nullptr, int(cached.scenes.size())));
        emit finished(true, QString());
        return;
    }

    // Same reason as the export, segmentation and face jobs: playback would drive the decode
    // pool from a second thread while this job walks it frame by frame.
    m_app->setPlaying(false);

    m_sceneDetectCancel.storeRelaxed(0);
    m_sceneDetectProgress = 0.0;
    emit progressChanged();
    m_sceneDetectStatus = tr("Getting ready…");
    emit statusChanged();
    m_sceneDetecting = true;
    emit detectingChanged();
    m_app->setLastMessage(tr("Looking for scenes…"));

    // Resolved by id at the end rather than by index: the timeline can be edited while the
    // job runs, and a stale index would attach the analysis to the wrong clip.
    const QString clipId = clip.id;
    const QString clipPath = clip.path;
    const int rotationCorrection = clip.rotationCorrection;
    const quint64 generation = ++m_sceneGeneration;

    (void)QtConcurrent::run([this, request, clipId, clipPath, rotationCorrection, generation]() {
        auto setProgress = [this, generation](double fraction, const QString &status) {
            QMetaObject::invokeMethod(
                this,
                [this, fraction, status, generation]() {
                    if (generation != m_sceneGeneration)
                        return;
                    m_sceneDetectProgress = fraction;
                    emit progressChanged();
                    if (!status.isEmpty() && status != m_sceneDetectStatus) {
                        m_sceneDetectStatus = status;
                        emit statusChanged();
                    }
                },
                Qt::QueuedConnection);
        };

        auto finish = [this, clipId, clipPath, rotationCorrection, generation](
                          bool ok, const QString &message, const drift::SceneAnalysis &analysis) {
            QMetaObject::invokeMethod(
                this,
                [this, ok, message, analysis, clipId, clipPath, rotationCorrection, generation]() {
                    if (generation != m_sceneGeneration)
                        return; // superseded by a newer scan; this result is for nobody
                    m_sceneDetecting = false;
                    emit detectingChanged();
                    m_sceneDetectProgress = ok ? 1.0 : 0.0;
                    emit progressChanged();
                    m_sceneDetectStatus = ok ? tr("Done") : message;
                    emit statusChanged();
                    if (!ok) {
                        if (!message.isEmpty())
                            m_app->setLastMessage(message, QStringLiteral("error"));
                        emit finished(false, message);
                        return;
                    }
                    applySceneAnalysis(analysis, clipId, clipPath, rotationCorrection);
                    m_app->setLastMessage(tr("Found %n scene(s)", nullptr, int(analysis.scenes.size())));
                    emit finished(true, QString());
                },
                Qt::QueuedConnection);
        };

        QString error;
        const drift::SceneAnalysis analysis = drift::detectScenes(
            request,
            [&](double fraction, const QString &status) {
                if (m_sceneDetectCancel.loadRelaxed() != 0)
                    return false;
                setProgress(fraction, status);
                return true;
            },
            &error);

        if (analysis.isEmpty()) {
            const bool cancelled = m_sceneDetectCancel.loadRelaxed() != 0;
            finish(false, cancelled ? tr("Scene detection cancelled") : error, {});
            return;
        }

        drift::storeCachedAnalysis(request, analysis);
        finish(true, QString(), analysis);
    });
}



#include "moc_SceneDetectController.cpp"
