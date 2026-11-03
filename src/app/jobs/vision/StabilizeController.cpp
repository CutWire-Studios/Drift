#include "app/jobs/vision/StabilizeController.h"

#include "app/editor/AppController.h"
#include "app/mcp/McpDetail.h"
#include "core/project/clip/Stabilize.h"
#include "engine/encode/MediaEditor.h"
#include "engine/decode/probe/MediaProbe.h"

#include <QAtomicInt>
#include <QDateTime>
#include <QFile>
#include <QMetaObject>
#include <QtConcurrent>

using drift::mcpdetail::findClipById;

StabilizeController::StabilizeController(AppController *app, QObject *parent)
    : QObject(parent)
    , m_app(app)
{
}

namespace {

// The camera original behind a clip's media, which stabilization always starts from: the clip's
// own asset, or — when the clip reads a "(stabilized)" render — the asset that render was made
// from, with offsetUs the render's start in that asset's source time. Empty when the original has
// left the bin.
struct StabilizeOrigin
{
    QString assetId;
    QString path;
    drift::TimeUs offsetUs = 0;
};

std::optional<StabilizeOrigin> stabilizeOrigin(const drift::Project &project, const drift::Clip &clip)
{
    const drift::MediaAsset *asset = project.asset(clip.assetId);
    if (!asset || asset->generator.value(QStringLiteral("kind")).toString() != QLatin1String("stabilize"))
        return StabilizeOrigin{clip.assetId, clip.path, 0};
    const drift::MediaAsset *source =
        project.asset(asset->generator.value(QStringLiteral("sourceAssetId")).toString());
    if (!source || source->path.isEmpty())
        return std::nullopt;
    return StabilizeOrigin{
        source->id, source->path,
        drift::TimeUs(asset->generator.value(QStringLiteral("sourceInUs")).toInteger())};
}


} // namespace

QString StabilizeController::detectFilter()
{
    return QStringLiteral("vidstabdetect=shakiness=5:accuracy=15");
}

QString StabilizeController::transformFilter(int smoothing, bool tripod)
{
    return QStringLiteral("vidstabtransform=smoothing=%1:tripod=%2:optzoom=1")
        .arg(smoothing)
        .arg(tripod ? 1 : 0);
}

QString StabilizeController::analysisPath(const QString &sourcePath, drift::TimeUs rangeIn,
                                          drift::TimeUs rangeOut)
{
    const QString dir = drift::stabilizationCacheDir();
    if (dir.isEmpty())
        return {};
    const QFileInfo sourceInfo(sourcePath);
    const QByteArray key = QStringLiteral("%1|%2|%3|%4|%5")
                               .arg(sourceInfo.absoluteFilePath())
                               .arg(sourceInfo.size())
                               .arg(sourceInfo.lastModified().toMSecsSinceEpoch())
                               .arg(rangeIn)
                               .arg(rangeOut)
                               .toUtf8();
    return QDir(dir).filePath(
        QStringLiteral("stabilize-%1.trf")
            .arg(QString::fromLatin1(QCryptographicHash::hash(key, QCryptographicHash::Sha1).toHex())));
}

void StabilizeController::migrateLegacyClips()
{
    // Clips split from one stabilized clip share its render, and so share one asset.
    QHash<QString, QString> assetForRender;
    m_app->m_project.forEachTrackList([&](QList<drift::Track> &tracks) {
        for (drift::Track &track : tracks) {
            for (drift::Clip &clip : track.clips) {
                const QString render = clip.legacyStabilizePath;
                clip.legacyStabilizePath.clear();
                // A render that is gone leaves the clip on its original, as it always played then.
                if (render.isEmpty() || !QFile::exists(render))
                    continue;
                const drift::MediaAsset *original = m_app->m_project.asset(clip.assetId);
                if (!original)
                    continue;

                QString assetId = assetForRender.value(render);
                if (assetId.isEmpty()) {
                    // The render is the whole source in its own time, upright with the rotation tag
                    // dropped, with the same frame rate and audio.
                    drift::MediaAsset asset;
                    asset.kind = drift::MediaKind::Video;
                    asset.path = render;
                    asset.name = tr("%1 (stabilized)").arg(original->name);
                    asset.durationUs = original->durationUs;
                    const bool quarterTurn =
                        original->rotationDegrees == 90 || original->rotationDegrees == 270;
                    asset.width = quarterTurn ? original->height : original->width;
                    asset.height = quarterTurn ? original->width : original->height;
                    asset.fps = original->fps;
                    const int binCorrection = drift::rotationCorrectionOf(*original);
                    if (binCorrection != 0)
                        asset.rotationOverride = binCorrection;
                    asset.hasAudio = original->hasAudio;
                    asset.hasAudioKnown = original->hasAudioKnown;
                    asset.frameRateKnown = true;
                    asset.folderId = original->folderId;
                    asset.generator = QJsonObject{
                        {QStringLiteral("kind"), QStringLiteral("stabilize")},
                        {QStringLiteral("sourceAssetId"), original->id},
                        {QStringLiteral("sourceInUs"), 0},
                        {QStringLiteral("smoothing"), clip.stabilizeAppliedSmoothing},
                        {QStringLiteral("tripod"), clip.stabilizeAppliedTripod}};
                    assetId = m_app->m_project.addAsset(asset);
                    assetForRender.insert(render, assetId);
                }
                clip.assetId = assetId;
                clip.path = render;
                // restoreFilmstripsAfterLoad fills it from the new asset.
                clip.filmstripPath.clear();
            }
        }
    });
}

void StabilizeController::setProgress(const QString &clipId, double progress, const QString &status,
                                         bool force)
{
    const double clamped = qBound(0.0, progress, 1.0);
    const bool statusChanged = !status.isEmpty() && m_status.value(clipId) != status;
    m_progress.insert(clipId, clamped);
    if (!status.isEmpty())
        m_status.insert(clipId, status);

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (!force && !statusChanged && now - m_lastProgressEmit.value(clipId, 0) < 100)
        return;
    m_lastProgressEmit.insert(clipId, now);
    m_app->notifyStabilizeStateChanged(clipId);
}

void StabilizeController::clearProgress(const QString &clipId)
{
    m_progress.remove(clipId);
    m_status.remove(clipId);
    m_lastProgressEmit.remove(clipId);
    m_cancelRequested.remove(clipId);
}

std::function<bool(double)> StabilizeController::progressReporter(
    const QString &clipId, const QSharedPointer<QAtomicInt> &cancel, double rangeFrom,
    double rangeTo)
{
    double lastPosted = -1.0;
    return [this, clipId, cancel, rangeFrom, rangeTo, lastPosted](double fraction) mutable {
        if (cancel->loadRelaxed() != 0)
            return false;
        const double progress = rangeFrom + (rangeTo - rangeFrom) * fraction;
        if (progress - lastPosted >= 0.005) {
            lastPosted = progress;
            QMetaObject::invokeMethod(
                this,
                [this, clipId, progress]() {
                    if (m_jobs.contains(clipId))
                        setProgress(clipId, progress, QString(), false);
                },
                Qt::QueuedConnection);
        }
        return true;
    };
}

void StabilizeController::run(int trackIndex, int clipIndex)
{
    if (trackIndex < 0 || trackIndex >= m_app->m_project.tracks().size())
        return;
    const drift::Track &track = m_app->m_project.tracks().at(trackIndex);
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    const drift::Clip clip = track.clips.at(clipIndex);
    if (clip.type != drift::ClipType::Video) {
        m_app->setLastMessage(tr("Select a video clip to stabilize"), QStringLiteral("warning"));
        return;
    }
    if (clip.path.isEmpty()) {
        m_app->setLastMessage(tr("Clip has no video file"), QStringLiteral("warning"));
        return;
    }

    const QString clipId = clip.id;
    if (clip.stabilizing || m_jobs.contains(clipId)) {
        m_app->setLastMessage(tr("Stabilization already in progress for this clip"), QStringLiteral("warning"));
        return;
    }

    m_app->setPlaying(false);

    if (!drift::hasVideoFilter("vidstabdetect") || !drift::hasVideoFilter("vidstabtransform")) {
        m_app->setLastMessage(tr("This build of Drift has no video stabilization support"),
                       QStringLiteral("error"));
        return;
    }

    const std::optional<StabilizeOrigin> origin = stabilizeOrigin(m_app->m_project, clip);
    if (!origin) {
        m_app->setLastMessage(tr("The video this clip was stabilized from is no longer in the media bin"),
                       QStringLiteral("warning"));
        return;
    }

    const QString dir = drift::stabilizationCacheDir();
    if (dir.isEmpty()) {
        m_app->setLastMessage(tr("Could not create stabilization cache directory"), QStringLiteral("error"));
        return;
    }

    // Both passes go through the seconds editMedia takes, so the analysis sees exactly the frames
    // the render will.
    const double inSeconds = drift::usToSeconds(clip.srcIn + origin->offsetUs);
    const double outSeconds = drift::usToSeconds(clip.srcOut + origin->offsetUs);
    const drift::TimeUs rangeIn = drift::secondsToUs(inSeconds);
    const drift::TimeUs rangeOut = drift::secondsToUs(outSeconds);

    // The motion analysis depends only on the frames, not on smoothing or tripod, so it is cached
    // per source range and a settings change re-renders without re-scanning.
    const QString trfPath = analysisPath(origin->path, rangeIn, rangeOut);

    m_cancelRequested.remove(clipId);
    m_app->m_project.tracks()[trackIndex].clips[clipIndex].stabilizing = true;
    const bool skipDetect = QFile::exists(trfPath);
    const bool keyframeMode = clip.stabilizeMode == drift::StabilizeMode::Keyframes;
    const QString startStatus = skipDetect
                                    ? (keyframeMode ? tr("Building keyframes…")
                                                    : tr("Rendering stabilized video…"))
                                    : tr("Analyzing camera motion…");
    // setProgress(force=true) below already notifies for this clip if it is the one
    // currently selected.
    setProgress(clipId, 0.0, startStatus, true);

    auto finishStabilizeFailure = [this, clipId](const QString &message, const QString &severity) {
        int foundTrack = -1;
        int foundClip = -1;
        if (findClipById(m_app->m_project, clipId, &foundTrack, &foundClip)) {
            m_app->m_project.tracks()[foundTrack].clips[foundClip].stabilizing = false;
        }
        clearProgress(clipId);
        m_app->notifyStabilizeStateChanged(clipId);
        m_app->setLastMessage(message, severity);
    };

    auto runKeyframes = [this, clipId, trfPath, rangeIn, skipDetect, finishStabilizeFailure]() {
        if (m_cancelRequested.contains(clipId)) {
            finishStabilizeFailure(tr("Stabilization cancelled."), QStringLiteral("info"));
            return;
        }

        int foundTrack = -1;
        int foundClip = -1;
        if (!findClipById(m_app->m_project, clipId, &foundTrack, &foundClip)) {
            clearProgress(clipId);
            return;
        }
        const std::optional<StabilizeOrigin> origin =
            stabilizeOrigin(m_app->m_project, m_app->m_project.tracks()[foundTrack].clips[foundClip]);
        if (!origin) {
            finishStabilizeFailure(tr("The video this clip was stabilized from is no longer in the media bin"),
                                   QStringLiteral("warning"));
            return;
        }

        setProgress(clipId, skipDetect ? 0.15 : 0.85, tr("Building keyframes…"), true);

        // Planned against the original's source time, which is what the keys are applied to.
        drift::Clip clipCopy = m_app->m_project.tracks()[foundTrack].clips[foundClip];
        clipCopy.srcIn += origin->offsetUs;
        clipCopy.srcOut += origin->offsetUs;
        double fps = 30.0;
        int sourceW = 0;
        int sourceH = 0;
        if (const drift::MediaAsset *asset = m_app->m_project.asset(origin->assetId)) {
            if (asset->fps > 1.0)
                fps = asset->fps;
            sourceW = asset->width;
            sourceH = asset->height;
        }
        const double layoutW = clipCopy.stabilizeHasRestPose
                                   ? clipCopy.stabilizeRestW
                                   : (clipCopy.transformW.isEmpty() ? sourceW
                                                                    : clipCopy.transformW.evaluateAt(0));
        const double layoutH = clipCopy.stabilizeHasRestPose
                                   ? clipCopy.stabilizeRestH
                                   : (clipCopy.transformH.isEmpty() ? sourceH
                                                                    : clipCopy.transformH.evaluateAt(0));
        const double scaleX = (sourceW > 0 && layoutW > 0.0) ? layoutW / sourceW : 1.0;
        const double scaleY = (sourceH > 0 && layoutH > 0.0) ? layoutH / sourceH : 1.0;
        const int smoothing = clipCopy.stabilizeSmoothing;
        const bool tripod = clipCopy.stabilizeTripod;

        (void)QtConcurrent::run([this, clipId, trfPath, rangeIn, clipCopy, fps, scaleX, scaleY,
                                 smoothing, tripod, finishStabilizeFailure]() {
            const drift::StabilizePlan plan = drift::planStabilizeKeyframes(
                trfPath, rangeIn, clipCopy, fps, scaleX, scaleY, smoothing, tripod);
            QMetaObject::invokeMethod(
                this,
                [this, clipId, plan, finishStabilizeFailure]() {
                    if (m_cancelRequested.contains(clipId)) {
                        finishStabilizeFailure(tr("Stabilization cancelled."), QStringLiteral("info"));
                        return;
                    }

                    int foundTrack2 = -1;
                    int foundClip2 = -1;
                    if (!findClipById(m_app->m_project, clipId, &foundTrack2, &foundClip2)) {
                        clearProgress(clipId);
                        return;
                    }
                    const std::optional<StabilizeOrigin> origin =
                        stabilizeOrigin(m_app->m_project, m_app->m_project.tracks()[foundTrack2].clips[foundClip2]);
                    if (!origin) {
                        finishStabilizeFailure(
                            tr("The video this clip was stabilized from is no longer in the media bin"),
                            QStringLiteral("warning"));
                        return;
                    }

                    if (plan.keys.isEmpty()) {
                        finishStabilizeFailure(tr("Could not read camera motion from the analysis file."),
                                               QStringLiteral("error"));
                        return;
                    }

                    const drift::Project before = m_app->m_project;
                    drift::Clip &outClip = m_app->m_project.tracks()[foundTrack2].clips[foundClip2];
                    outClip.stabilizing = false;
                    // Keys move the original's pixels; on a stabilized render they would stack a
                    // second correction on the first.
                    if (outClip.assetId != origin->assetId) {
                        outClip.assetId = origin->assetId;
                        outClip.path = origin->path;
                        outClip.srcIn += origin->offsetUs;
                        outClip.srcOut += origin->offsetUs;
                        if (const drift::MediaAsset *asset = m_app->m_project.asset(origin->assetId)) {
                            outClip.thumbnailPath = asset->thumbnailPath;
                            outClip.filmstripPath = asset->filmstripPath;
                        }
                        outClip.faceTrackPath.clear();
                        outClip.faceTrackSrcOffsetUs = 0;
                        outClip.depthPath.clear();
                        // Can drop lanes, which moves track indexes.
                        drift::clearLinkedMasks(m_app->m_project, foundTrack2, foundClip2, true);
                        findClipById(m_app->m_project, clipId, &foundTrack2, &foundClip2);
                    }
                    drift::Clip &applied = m_app->m_project.tracks()[foundTrack2].clips[foundClip2];
                    drift::applyStabilizePlan(applied, plan);
                    applied.stabilizeAppliedSmoothing = applied.stabilizeSmoothing;
                    applied.stabilizeAppliedTripod = applied.stabilizeTripod;
                    applied.stabilizeAppliedMode = drift::StabilizeMode::Keyframes;
                    m_app->pushProjectEdit(before, tr("Stabilize with Keyframes"));
                    clearProgress(clipId);
                    m_app->finishEdit(tr("Stabilize with Keyframes"));
                    m_app->setLastMessage(tr("Stabilization keyframes applied."));
                },
                Qt::QueuedConnection);
        });
    };

    auto runPass2 = [this, clipId, trfPath, inSeconds, outSeconds, rangeIn, skipDetect,
                     finishStabilizeFailure, runKeyframes]() {
        if (m_cancelRequested.contains(clipId)) {
            finishStabilizeFailure(tr("Stabilization cancelled."), QStringLiteral("info"));
            return;
        }

        int foundTrack = -1;
        int foundClip = -1;
        if (!findClipById(m_app->m_project, clipId, &foundTrack, &foundClip)) {
            clearProgress(clipId);
            return;
        }

        const drift::Clip &sourceClip = m_app->m_project.tracks()[foundTrack].clips[foundClip];
        if (sourceClip.stabilizeMode == drift::StabilizeMode::Keyframes) {
            runKeyframes();
            return;
        }
        const std::optional<StabilizeOrigin> origin = stabilizeOrigin(m_app->m_project, sourceClip);
        if (!origin) {
            finishStabilizeFailure(tr("The video this clip was stabilized from is no longer in the media bin"),
                                   QStringLiteral("warning"));
            return;
        }
        if (!QFile::exists(trfPath)) {
            finishStabilizeFailure(tr("Stabilization analysis file is missing."),
                                   QStringLiteral("error"));
            return;
        }
        const QString outputPath = drift::newEditedMediaPath(m_app->m_project.id(), QStringLiteral("video"));
        if (outputPath.isEmpty()) {
            finishStabilizeFailure(tr("Could not create an output file"), QStringLiteral("error"));
            return;
        }

        const double rangeFrom = skipDetect ? 0.0 : 0.5;
        setProgress(clipId, rangeFrom, tr("Rendering stabilized video…"), true);

        drift::MediaEditSpec spec;
        spec.inputPath = origin->path;
        spec.outputPath = outputPath;
        spec.kind = QStringLiteral("video");
        spec.inSeconds = inSeconds;
        spec.outSeconds = outSeconds;
        spec.videoFilter = transformFilter(sourceClip.stabilizeSmoothing, sourceClip.stabilizeTripod);
        spec.videoFilterOptions = {{"vidstabtransform", "input", trfPath}};

        QJsonObject generator{{QStringLiteral("kind"), QStringLiteral("stabilize")},
                              {QStringLiteral("sourceAssetId"), origin->assetId},
                              {QStringLiteral("sourceInUs"), qint64(rangeIn)},
                              {QStringLiteral("smoothing"), sourceClip.stabilizeSmoothing},
                              {QStringLiteral("tripod"), sourceClip.stabilizeTripod}};

        const auto cancel = QSharedPointer<QAtomicInt>::create(0);
        m_jobs.insert(clipId, cancel);
        const auto onProgress = progressReporter(clipId, cancel, rangeFrom, 1.0);
        (void)QtConcurrent::run([this, clipId, spec, generator, onProgress, finishStabilizeFailure]() {
            QString error;
            const bool ok = drift::editMedia(spec, &error, onProgress);
            const MediaInfo info = ok ? MediaProbe::probe(spec.outputPath) : MediaInfo();
            QMetaObject::invokeMethod(
                this,
                [this, clipId, ok, error, info, generator, outputPath = spec.outputPath,
                 finishStabilizeFailure]() {
                    m_jobs.remove(clipId);
                    const bool cancelled = m_cancelRequested.contains(clipId);

                    int foundTrack2 = -1;
                    int foundClip2 = -1;
                    if (!findClipById(m_app->m_project, clipId, &foundTrack2, &foundClip2)) {
                        QFile::remove(outputPath);
                        clearProgress(clipId);
                        return;
                    }

                    if (!ok || cancelled) {
                        QFile::remove(outputPath);
                        if (cancelled)
                            finishStabilizeFailure(tr("Stabilization cancelled."),
                                                   QStringLiteral("info"));
                        else
                            finishStabilizeFailure(
                                error.isEmpty() ? tr("Stabilization rendering failed.")
                                                : tr("Stabilization rendering failed: %1").arg(error),
                                QStringLiteral("error"));
                        return;
                    }

                    const std::optional<StabilizeOrigin> origin =
                        stabilizeOrigin(m_app->m_project, m_app->m_project.tracks()[foundTrack2].clips[foundClip2]);
                    const drift::MediaAsset *source =
                        m_app->m_project.asset(generator.value(QStringLiteral("sourceAssetId")).toString());
                    if (!origin || !source) {
                        QFile::remove(outputPath);
                        finishStabilizeFailure(
                            tr("The video this clip was stabilized from is no longer in the media bin"),
                            QStringLiteral("warning"));
                        return;
                    }

                    const drift::Project before = m_app->m_project;
                    const int binCorrection = drift::rotationCorrectionOf(*source);
                    const QString name = tr("%1 (stabilized)").arg(source->name);
                    const QString folderId = source->folderId;
                    const QString assetId = m_app->insertRenderedVideoAsset(outputPath, name, folderId, info);
                    drift::MediaAsset *stabilized = m_app->m_project.asset(assetId);
                    stabilized->generator = generator;
                    // The render is upright with no rotation tag, but it is still the source the
                    // bin's correction was chosen for.
                    if (binCorrection != 0)
                        stabilized->rotationOverride = binCorrection;

                    // The render has the original's audio, so the clip plays its own and a linked
                    // audio clip, which reads the original in the original's time, goes.
                    const drift::Clip current = m_app->m_project.tracks()[foundTrack2].clips[foundClip2];
                    const bool wasSelected =
                        m_app->m_selectedTrack == foundTrack2 && m_app->m_selectedClip == foundClip2;
                    QSet<QString> removedClipIds;
                    std::optional<drift::Clip> partnerAudio;
                    QList<drift::ClipRef> partners = drift::linkedPartners(m_app->m_project, current);
                    std::sort(partners.begin(), partners.end(),
                              [](const drift::ClipRef &a, const drift::ClipRef &b) {
                                  return a.trackIndex != b.trackIndex ? a.trackIndex > b.trackIndex
                                                                      : a.clipIndex > b.clipIndex;
                              });
                    for (const drift::ClipRef &ref : partners) {
                        const drift::Clip &partner =
                            m_app->m_project.tracks()[ref.trackIndex].clips[ref.clipIndex];
                        if (partner.type != drift::ClipType::Audio)
                            continue;
                        if (!partnerAudio)
                            partnerAudio = partner;
                        removedClipIds.insert(partner.id);
                        m_app->m_project.tracks()[ref.trackIndex].clips.removeAt(ref.clipIndex);
                    }
                    for (drift::Track &track : m_app->m_project.tracks()) {
                        for (int i = track.transitions.size() - 1; i >= 0; --i) {
                            const drift::Transition &transition = track.transitions.at(i);
                            if (removedClipIds.contains(transition.fromClipId)
                                || removedClipIds.contains(transition.toClipId))
                                track.transitions.removeAt(i);
                        }
                    }
                    if (!findClipById(m_app->m_project, clipId, &foundTrack2, &foundClip2))
                        return;

                    drift::Clip &outClip = m_app->m_project.tracks()[foundTrack2].clips[foundClip2];
                    outClip.stabilizing = false;
                    // The render starts at the range's in-point, so the clip's range moves with it.
                    const drift::TimeUs renderIn =
                        drift::TimeUs(generator.value(QStringLiteral("sourceInUs")).toInteger());
                    const drift::TimeUs shift = origin->offsetUs - renderIn;
                    outClip.assetId = assetId;
                    outClip.path = outputPath;
                    outClip.srcIn = qMax(drift::TimeUs{0}, outClip.srcIn + shift);
                    outClip.srcOut = qMax(outClip.srcIn, outClip.srcOut + shift);
                    if (m_app->m_assetLibrary) {
                        const int index = m_app->m_assetLibrary->indexOfId(assetId);
                        if (!stabilized->thumbnailPath.isEmpty())
                            outClip.thumbnailPath = stabilized->thumbnailPath;
                        const QString strip = m_app->m_assetLibrary->filmstripAt(index);
                        if (!strip.isEmpty())
                            outClip.filmstripPath = strip;
                    }
                    if (partnerAudio) {
                        outClip.volume = partnerAudio->volume;
                        outClip.audioEffects = partnerAudio->audioEffects;
                    }
                    outClip.linkId.clear();
                    outClip.suppressEmbeddedAudio = false;
                    // Landmarks, depth and media mattes were made from the shaky pixels.
                    outClip.faceTrackPath.clear();
                    outClip.faceTrackSrcOffsetUs = 0;
                    outClip.depthPath.clear();

                    drift::restoreStabilizeRestPose(outClip);
                    if (outClip.transformX.keyframes().size() > 1
                        || outClip.transformY.keyframes().size() > 1) {
                        const double x = outClip.transformX.isEmpty()
                                             ? 0.0
                                             : outClip.transformX.evaluateAt(0);
                        const double y = outClip.transformY.isEmpty()
                                             ? 0.0
                                             : outClip.transformY.evaluateAt(0);
                        outClip.transformX = {};
                        outClip.transformY = {};
                        outClip.transformX.setKeyframe(0, x);
                        outClip.transformY.setKeyframe(0, y);
                    }
                    outClip.stabilizeAppliedSmoothing = outClip.stabilizeSmoothing;
                    outClip.stabilizeAppliedTripod = outClip.stabilizeTripod;
                    outClip.stabilizeAppliedMode = drift::StabilizeMode::Bake;
                    drift::clearLinkedMasks(m_app->m_project, foundTrack2, foundClip2, true);

                    // Removing the audio partner and mask lanes moves indexes under the selection.
                    if (wasSelected && findClipById(m_app->m_project, clipId, &foundTrack2, &foundClip2))
                        m_app->selectClip(foundTrack2, foundClip2);
                    else if (!removedClipIds.isEmpty())
                        m_app->clearSelection();

                    m_app->pushProjectEdit(before, tr("Stabilize Video"));
                    clearProgress(clipId);
                    m_app->finishEdit(tr("Stabilize Video"));
                    m_app->setLastMessage(tr("Stabilized video added to the media bin"));
                },
                Qt::QueuedConnection);
        });
    };

    if (skipDetect) {
        runPass2();
        return;
    }

    // Written beside the cache entry and renamed on success, so a cancelled scan never leaves a
    // partial analysis that a later run would take as complete.
    const QString partPath = trfPath + QStringLiteral(".part");
    QFile::remove(partPath);
    const auto cancel = QSharedPointer<QAtomicInt>::create(0);
    m_jobs.insert(clipId, cancel);
    const auto onProgress = progressReporter(clipId, cancel, 0.0, keyframeMode ? 0.8 : 0.5);
    (void)QtConcurrent::run([this, clipId, inputPath = origin->path, rangeIn, rangeOut, onProgress,
                             trfPath, partPath, runPass2, finishStabilizeFailure]() {
        QString error;
        const bool ok = drift::analyzeVideo(
            inputPath, rangeIn, rangeOut, detectFilter(),
            {{"vidstabdetect", "result", partPath}}, &error, onProgress);
        QMetaObject::invokeMethod(
            this,
            [this, clipId, ok, error, trfPath, partPath, runPass2, finishStabilizeFailure]() {
                m_jobs.remove(clipId);
                const bool cancelled = m_cancelRequested.contains(clipId);

                int foundTrack = -1;
                int foundClip = -1;
                if (!findClipById(m_app->m_project, clipId, &foundTrack, &foundClip)) {
                    QFile::remove(partPath);
                    clearProgress(clipId);
                    return;
                }

                if (!ok || cancelled || !QFile::exists(partPath)) {
                    QFile::remove(partPath);
                    if (cancelled)
                        finishStabilizeFailure(tr("Stabilization cancelled."), QStringLiteral("info"));
                    else
                        finishStabilizeFailure(
                            error.isEmpty() ? tr("Stabilization analysis failed.")
                                            : tr("Stabilization analysis failed: %1").arg(error),
                            QStringLiteral("error"));
                    return;
                }

                QFile::remove(trfPath);
                QFile::rename(partPath, trfPath);
                runPass2();
            },
            Qt::QueuedConnection);
    });
}

void StabilizeController::cancel(int trackIndex, int clipIndex)
{
    if (trackIndex < 0 || trackIndex >= m_app->m_project.tracks().size())
        return;
    const drift::Track &track = m_app->m_project.tracks().at(trackIndex);
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    const drift::Clip &clip = track.clips.at(clipIndex);
    const QString clipId = clip.id;
    if (!clip.stabilizing && !m_jobs.contains(clipId))
        return;

    m_cancelRequested.insert(clipId);
    if (const QSharedPointer<QAtomicInt> cancel = m_jobs.value(clipId))
        cancel->storeRelaxed(1);
}

// Keyframe mode only: a stabilized video is a bin asset of its own, and the original is still in
// the bin to put back.
void StabilizeController::remove(int trackIndex, int clipIndex)
{
    if (trackIndex < 0 || trackIndex >= m_app->m_project.tracks().size())
        return;
    const drift::Track &track = m_app->m_project.tracks().at(trackIndex);
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    const drift::Clip &clip = track.clips.at(clipIndex);
    if (clip.stabilizeAppliedSmoothing < 0
        || clip.stabilizeAppliedMode != drift::StabilizeMode::Keyframes)
        return;

    const drift::Project before = m_app->m_project;
    drift::Clip &outClip = m_app->m_project.tracks()[trackIndex].clips[clipIndex];
    outClip.stabilizing = false;
    outClip.stabilizeAppliedSmoothing = -1;
    outClip.stabilizeAppliedTripod = false;
    outClip.stabilizeAppliedMode = drift::StabilizeMode::Bake;
    drift::restoreStabilizeRestPose(outClip);
    m_app->pushProjectEdit(before, tr("Remove Stabilization"));
    emit m_app->selectedClipDataChanged();
    m_app->finishEdit(tr("Remove Stabilization"));
}

void StabilizeController::setSmoothing(int trackIndex, int clipIndex, int value)
{
    if (trackIndex < 0 || trackIndex >= m_app->m_project.tracks().size())
        return;
    const drift::Track &track = m_app->m_project.tracks().at(trackIndex);
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    const drift::Clip clip = track.clips.at(clipIndex);
    if (clip.stabilizeSmoothing == value)
        return;

    const drift::Project before = m_app->m_project;
    m_app->m_project.tracks()[trackIndex].clips[clipIndex].stabilizeSmoothing = value;
    m_app->pushProjectEdit(before, tr("Change Stabilization Smoothing"));
    emit m_app->selectedClipDataChanged();
    m_app->finishEdit(tr("Change Stabilization Smoothing"));
}

void StabilizeController::setTripod(int trackIndex, int clipIndex, bool enabled)
{
    if (trackIndex < 0 || trackIndex >= m_app->m_project.tracks().size())
        return;
    const drift::Track &track = m_app->m_project.tracks().at(trackIndex);
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    const drift::Clip clip = track.clips.at(clipIndex);
    if (clip.stabilizeTripod == enabled)
        return;

    const drift::Project before = m_app->m_project;
    m_app->m_project.tracks()[trackIndex].clips[clipIndex].stabilizeTripod = enabled;
    m_app->pushProjectEdit(before, tr("Change Stabilization Tripod Mode"));
    emit m_app->selectedClipDataChanged();
    m_app->finishEdit(tr("Change Stabilization Tripod Mode"));
}

void StabilizeController::setMode(int trackIndex, int clipIndex, const QString &mode)
{
    if (trackIndex < 0 || trackIndex >= m_app->m_project.tracks().size())
        return;
    const drift::Track &track = m_app->m_project.tracks().at(trackIndex);
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    const drift::StabilizeMode parsed = drift::stabilizeModeFromString(mode);
    drift::Clip &clip = m_app->m_project.tracks()[trackIndex].clips[clipIndex];
    if (clip.stabilizeMode == parsed)
        return;

    const drift::Project before = m_app->m_project;
    clip.stabilizeMode = parsed;
    m_app->pushProjectEdit(before, tr("Change Stabilization Mode"));
    emit m_app->selectedClipDataChanged();
    m_app->finishEdit(tr("Change Stabilization Mode"));
}

#include "moc_StabilizeController.cpp"
