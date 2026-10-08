#include "SegmentationController.h"

#include "AppController.h"
#include "SegmentImageStore.h"
#include "engine/EffectTemplateCatalog.h"
#include "engine/MatteWriter.h"
#include "engine/RvmMatter.h"

#include "engine/ClipReaderPool.h"

#include <QFile>
#include <QtConcurrent>

namespace {
// Offline scans read a file end to end at their own pace. They need decode cursors of their own so
// they never share one with timeline playback of the same media — see ClipReaderPool.
constexpr quint64 kSegmentEncodeStreamId = 0xA5'11'5C'A4'00'00'00'03ull;
constexpr quint64 kCutoutRenderStreamId = 0xA5'11'5C'A4'00'00'00'04ull;
constexpr quint64 kSegmentScrubStreamId = 0xA5'11'5C'A4'00'00'00'0Bull;
}

SegmentationController::SegmentationController(AppController *app, QObject *parent)
    : QObject(parent)
    , m_app(app)
{
}

bool SegmentationController::available()
{
    // Deliberately only checks that the model files exist. This is reached from a QML binding, and
    // loading the sessions here would block the GUI thread for seconds.
    return drift::Sam2Segmenter::modelPresent() || drift::RvmMatter::modelPresent();
}

QString SegmentationController::modelVariant()
{
    if (m_segBackend == QLatin1String("rvm")) {
        const QStringList variants = drift::RvmMatter::installedVariants();
        if (m_segQuality.isEmpty())
            return variants.value(0);
        return variants.contains(m_segQuality) ? m_segQuality : variants.value(0);
    }
    return drift::Sam2Segmenter::installedVariant();
}

QStringList SegmentationController::backends()
{
    // File-existence checks only, like segmentationAvailable(): this is reached from a QML binding.
    QStringList out;
    if (drift::Sam2Segmenter::modelPresent())
        out.append(QStringLiteral("sam2"));
    if (drift::RvmMatter::modelPresent())
        out.append(QStringLiteral("rvm"));
    return out;
}

QStringList SegmentationController::rvmQualities()
{
    return drift::RvmMatter::installedVariants();
}

void SegmentationController::setBackend(const QString &backend, const QString &quality)
{
    const QString wanted =
        backend == QLatin1String("rvm") ? QStringLiteral("rvm") : QStringLiteral("sam2");
    if (wanted == m_segBackend && quality == m_segQuality)
        return;

    m_segBackend = wanted;
    m_segQuality = quality;
    // The prompt belongs to SAM2, and the preview on screen was produced by the other model.
    m_segPoints.clear();
    SegmentImageStore::setMask(QImage());
    ++m_segRevision;
    emit sessionChanged();

    // Re-derive the preview for the new backend: SAM2 needs its encoder run on this frame, RVM
    // needs its one forward pass, and neither has been done.
    if (m_segSessionActive)
        setFrame(m_segSeconds);
}

void SegmentationController::cancel()
{
    if (m_segmenting)
        m_segmentCancel.storeRelaxed(1);
}

void SegmentationController::beginSession(int trackIndex, int clipIndex, double seconds,
                                             bool forTemplate)
{
    if (trackIndex < 0 || trackIndex >= m_app->m_project.tracks().size())
        return;
    const drift::Track &track = m_app->m_project.tracks().at(trackIndex);
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;
    if (track.clips.at(clipIndex).type != drift::ClipType::Video) {
        m_app->setLastMessage(tr("Select a video clip to cut out"), QStringLiteral("warning"));
        return;
    }

    // The remembered choice can outlive the addon it names — the user may have removed one model
    // since the last session.
    const QStringList installed = backends();
    if (!installed.isEmpty() && !installed.contains(m_segBackend))
        m_segBackend = installed.first();

    m_segTrack = trackIndex;
    m_segClip = clipIndex;
    m_segPoints.clear();
    m_segForTemplate = forTemplate;
    m_segSessionActive = true;
    emit sessionChanged();
    setFrame(seconds);
}

void SegmentationController::endSession()
{
    if (m_segForTemplate)
        m_app->m_pendingEffectTemplate.reset();
    m_segSessionActive = false;
    m_segForTemplate = false;
    m_segEncoding = false;
    m_segTrack = -1;
    m_segClip = -1;
    m_segPoints.clear();
    m_segFrame = QImage();
    m_segEmbedding = drift::Sam2Embedding{};
    ++m_segGeneration;
    ++m_segSeedGeneration;
    m_segSeedRunning = false;
    SegmentImageStore::clear();
    ++m_segRevision;
    emit sessionChanged();
}

void SegmentationController::setFrame(double seconds)
{
    if (!m_segSessionActive || m_segEncoding)
        return;
    if (m_segTrack < 0 || m_segTrack >= m_app->m_project.tracks().size())
        return;
    const drift::Track &track = m_app->m_project.tracks().at(m_segTrack);
    if (m_segClip < 0 || m_segClip >= track.clips.size())
        return;

    const drift::Clip clip = track.clips.at(m_segClip);
    m_segSeconds = seconds;

    const drift::TimeUs timelineUs =
        qBound(clip.timelineStart, drift::secondsToUs(seconds),
               clip.timelineStart + clip.timelineDuration - 1);
    const drift::TimeUs sourceUs = clip.timelineToSourceUs(timelineUs);
    const QString path = clip.path;
    const int canvasW = m_app->m_project.width();
    const int canvasH = m_app->m_project.height();

    m_segEncoding = true;
    m_segPoints.clear();
    const int generation = ++m_segGeneration;
    emit sessionChanged();

    const bool rvm = m_segBackend == QLatin1String("rvm");
    const QString quality = m_segQuality;

    // The model pass is the expensive half (seconds per frame for the SAM2 encoder on a CPU
    // provider), so it runs off the GUI thread. Decodes after this are milliseconds and stay inline.
    const int rotationCorrection = clip.rotationCorrection;
    (void)QtConcurrent::run([this, path, sourceUs, canvasW, canvasH, generation, rvm, quality,
                             rotationCorrection]() {
        const QImage frame = ClipReaderPool::instance().readVideoFrame(
            path, kSegmentEncodeStreamId, sourceUs, canvasW, canvasH, rotationCorrection);
        drift::Sam2Embedding embedding;
        QImage mask;
        QString error;
        if (!frame.isNull()) {
            if (rvm) {
                // RVM has no prompt, so the preview is the whole answer rather than a seed: one
                // forward pass on a throwaway track, with no recurrent history to carry.
                std::unique_ptr<drift::RvmMatter::Track> track =
                    drift::RvmMatter::instance().newTrack(quality);
                if (!track) {
                    error = drift::RvmMatter::instance().lastError();
                } else {
                    const drift::RvmResult result = track->step(frame);
                    if (result.ok)
                        mask = result.alpha;
                    else
                        error = result.error;
                }
            } else {
                embedding = drift::Sam2Segmenter::instance().encode(frame);
                if (!embedding.valid)
                    error = drift::Sam2Segmenter::instance().lastError();
            }
        }

        QMetaObject::invokeMethod(
            this,
            [this, frame, embedding, mask, error, generation]() {
                // Dropped when the window closed, reopened, or the user scrubbed again while
                // this encode was running — otherwise a stale frame would land on a live session.
                if (generation != m_segGeneration)
                    return;
                m_segEncoding = false;
                if (!m_segSessionActive)
                    return;
                m_segFrame = frame;
                m_segEmbedding = embedding;
                SegmentImageStore::setFrame(frame);
                SegmentImageStore::setMask(mask);
                ++m_segRevision;
                if (frame.isNull() || !error.isEmpty())
                    m_app->setLastMessage(error, QStringLiteral("error"));
                emit sessionChanged();
            },
            Qt::QueuedConnection);
    });
}

void SegmentationController::scrubFrame(double seconds)
{
    if (!m_segSessionActive || m_segEncoding)
        return;
    if (m_segScrubBusy) {
        m_segScrubPending = seconds;
        return;
    }
    if (m_segTrack < 0 || m_segTrack >= m_app->m_project.tracks().size())
        return;
    const drift::Track &track = m_app->m_project.tracks().at(m_segTrack);
    if (m_segClip < 0 || m_segClip >= track.clips.size())
        return;

    const drift::Clip &clip = track.clips.at(m_segClip);
    const drift::TimeUs timelineUs =
        qBound(clip.timelineStart, drift::secondsToUs(seconds),
               clip.timelineStart + clip.timelineDuration - 1);
    const drift::TimeUs sourceUs = clip.timelineToSourceUs(timelineUs);
    const QString path = clip.path;
    const int canvasW = m_app->m_project.width();
    const int canvasH = m_app->m_project.height();
    const int rotationCorrection = clip.rotationCorrection;
    const int generation = m_segGeneration;

    // Points and mask belong to the frame being left.
    m_segScrubBusy = true;
    m_segPoints.clear();
    ++m_segSeedGeneration;

    (void)QtConcurrent::run([this, path, sourceUs, canvasW, canvasH, rotationCorrection,
                             generation]() {
        const QImage frame = ClipReaderPool::instance().readVideoFrame(
            path, kSegmentScrubStreamId, sourceUs, canvasW, canvasH, rotationCorrection);
        QMetaObject::invokeMethod(
            this,
            [this, frame, generation]() {
                m_segScrubBusy = false;
                // The release already started encoding its frame, or the window closed.
                if (generation != m_segGeneration || !m_segSessionActive || m_segEncoding) {
                    m_segScrubPending.reset();
                    return;
                }
                if (!frame.isNull()) {
                    SegmentImageStore::setFrame(frame);
                    SegmentImageStore::setMask(QImage());
                    ++m_segRevision;
                    emit sessionChanged();
                }
                if (m_segScrubPending) {
                    const double next = *m_segScrubPending;
                    m_segScrubPending.reset();
                    scrubFrame(next);
                }
            },
            Qt::QueuedConnection);
    });
}

void SegmentationController::addPoint(double x, double y, bool include)
{
    if (!m_segSessionActive || m_segEncoding)
        return;
    QVariantMap point;
    point.insert(QStringLiteral("x"), x);
    point.insert(QStringLiteral("y"), y);
    point.insert(QStringLiteral("include"), include);
    m_segPoints.append(point);
    refreshPreview();
}

void SegmentationController::removePoint(int index)
{
    if (!m_segSessionActive || index < 0 || index >= m_segPoints.size())
        return;
    m_segPoints.removeAt(index);
    refreshPreview();
}

void SegmentationController::clearPoints()
{
    if (!m_segSessionActive)
        return;
    m_segPoints.clear();
    refreshPreview();
}

void SegmentationController::refreshPreview()
{
    // RVM's preview is produced by setSegmentationFrame and has nothing to do with prompts;
    // falling through here would clear it the moment anything touched the point list.
    if (m_segBackend != QLatin1String("sam2"))
        return;
    if (m_segPoints.isEmpty() || !m_segEmbedding.valid) {
        ++m_segSeedGeneration;
        SegmentImageStore::setMask(QImage());
        ++m_segRevision;
        emit sessionChanged();
        return;
    }

    // Coalesce rapid point edits onto one ONNX seed at a time — sessions are not
    // safe to call concurrently, and only the latest prompt matters.
    ++m_segSeedGeneration;
    if (m_segSeedRunning)
        return;

    m_segSeedRunning = true;
    const int generation = m_segSeedGeneration;
    runSeed(generation);
}

void SegmentationController::runSeed(int generation)
{
    drift::Sam2Prompt prompt;
    for (const QVariant &entry : std::as_const(m_segPoints)) {
        const QVariantMap map = entry.toMap();
        prompt.points.append(QPointF(map.value(QStringLiteral("x")).toDouble() * m_segFrame.width(),
                                     map.value(QStringLiteral("y")).toDouble() * m_segFrame.height()));
        prompt.labels.append(map.value(QStringLiteral("include")).toBool() ? 1 : 0);
    }

    const drift::Sam2Embedding embedding = m_segEmbedding;
    (void)QtConcurrent::run([this, embedding, prompt, generation]() {
        const drift::Sam2Result result = drift::Sam2Segmenter::instance().segmentSeed(embedding, prompt);
        QMetaObject::invokeMethod(
            this,
            [this, result, generation]() {
                if (!m_segSessionActive) {
                    m_segSeedRunning = false;
                    return;
                }
                if (generation == m_segSeedGeneration) {
                    SegmentImageStore::setMask(result.ok ? result.mask : QImage());
                    if (!result.ok)
                        m_app->setLastMessage(result.error, QStringLiteral("error"));
                    ++m_segRevision;
                    emit sessionChanged();
                    m_segSeedRunning = false;
                    return;
                }
                // A newer prompt arrived while we were seeding — run again with the latest.
                runSeed(m_segSeedGeneration);
            },
            Qt::QueuedConnection);
    });
}

void SegmentationController::runSession(const QString &outputMode)
{
    if (!m_segSessionActive)
        return;
    if (backendUsesPoints() && m_segPoints.isEmpty())
        return;
    QString mode = outputMode;
    if (m_segForTemplate && m_app->m_pendingEffectTemplate && m_app->m_pendingEffectTemplate->valid())
        mode = QStringLiteral("template");
    segmentClip(m_segTrack, m_segClip, m_segPoints, mode);
}

void SegmentationController::openSegmentationForTemplate(int trackIndex, int clipIndex)
{
    if (trackIndex < 0 || trackIndex >= m_app->m_project.tracks().size())
        return;
    const drift::Track &track = m_app->m_project.tracks().at(trackIndex);
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    const drift::Clip &clip = track.clips.at(clipIndex);
    if (clip.type != drift::ClipType::Video)
        return;

    const double startSeconds = drift::usToSeconds(clip.timelineStart);
    const double durationSeconds = drift::usToSeconds(clip.timelineDuration);
    beginSession(trackIndex, clipIndex, startSeconds, true);
    emit openWindowRequested(trackIndex, clipIndex, startSeconds, durationSeconds);
}

void SegmentationController::segmentClip(int trackIndex, int clipIndex, const QVariantList &points,
                                const QString &outputMode, const QString &backend,
                                const QString &quality)
{
    if (m_segmenting) {
        m_app->setLastMessage(tr("Cutout is already running"), QStringLiteral("warning"));
        return;
    }
    if (trackIndex < 0 || trackIndex >= m_app->m_project.tracks().size())
        return;
    const drift::Track &track = m_app->m_project.tracks().at(trackIndex);
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    const drift::Clip clip = track.clips.at(clipIndex);
    if (clip.type != drift::ClipType::Video) {
        m_app->setLastMessage(tr("Select a video clip to cut out"), QStringLiteral("warning"));
        return;
    }
    if (clip.path.isEmpty() || clip.srcOut <= clip.srcIn) {
        m_app->setLastMessage(tr("This clip has no video to cut out"), QStringLiteral("warning"));
        return;
    }
    // Empty means "whatever the session is set to", which is also what a direct MCP call gets.
    const bool rvm = (backend.isEmpty() ? m_segBackend : backend) == QLatin1String("rvm");
    const QString rvmQuality = quality.isEmpty() ? m_segQuality : quality;

    if (!rvm && points.isEmpty()) {
        m_app->setLastMessage(tr("Click the subject first"), QStringLiteral("warning"));
        return;
    }

    // Kept normalized: the decoded frame size depends on the project canvas, and the prompt has
    // to be scaled to whatever each frame actually comes back as.
    drift::Sam2Prompt normalized;
    for (const QVariant &entry : points) {
        const QVariantMap map = entry.toMap();
        normalized.points.append(QPointF(map.value(QStringLiteral("x")).toDouble(),
                                         map.value(QStringLiteral("y")).toDouble()));
        normalized.labels.append(map.value(QStringLiteral("include"), true).toBool() ? 1 : 0);
    }

    // Same reason as the export and subtitle jobs: playback would drive the decode pool from a
    // second thread while this job walks it frame by frame.
    m_app->setPlaying(false);

    m_segmentCancel.storeRelaxed(0);
    m_segmentProgress = 0.0;
    emit progressChanged();
    m_segmentStatus = tr("Getting ready…");
    emit statusChanged();
    m_segmenting = true;
    emit runningChanged();
    m_app->setLastMessage(tr("Cutting out subject…"));

    const QString path = clip.path;
    const drift::TimeUs srcIn = clip.srcIn;
    const drift::TimeUs srcOut = clip.srcOut;
    const int fps = qMax(1, m_app->m_project.fps());
    const int canvasW = m_app->m_project.width();
    const int canvasH = m_app->m_project.height();
    const QString mode = outputMode.isEmpty() ? QStringLiteral("adjustment") : outputMode;
    // Resolved by id at the end rather than by index: the timeline can be edited while the job
    // runs, and stale indices would apply the matte to the wrong clip.
    const QString clipId = clip.id;

    // Masks are traced against the frames the compositor shows, so they must decode at the same
    // orientation or the matte lands transposed.
    const int rotationCorrection = clip.rotationCorrection;
    (void)QtConcurrent::run([this, path, srcIn, srcOut, fps, canvasW, canvasH, normalized, mode,
                             clipId, rvm, rvmQuality, rotationCorrection]() {
        auto setProgress = [this](double fraction, const QString &status) {
            QMetaObject::invokeMethod(
                this,
                [this, fraction, status]() {
                    m_segmentProgress = fraction;
                    emit progressChanged();
                    if (!status.isEmpty() && status != m_segmentStatus) {
                        m_segmentStatus = status;
                        emit statusChanged();
                    }
                },
                Qt::QueuedConnection);
        };

        auto finish = [this, clipId, srcIn, mode](bool ok, const QString &message,
                                                  const QString &mattePath,
                                                  const QString &fgrPath = QString()) {
            QMetaObject::invokeMethod(
                this,
                [this, ok, message, mattePath, fgrPath, clipId, srcIn, mode]() {
                    m_segmenting = false;
                    emit runningChanged();
                    m_segmentProgress = ok ? 1.0 : 0.0;
                    emit progressChanged();
                    m_segmentStatus = ok ? tr("Done") : message;
                    emit statusChanged();
                    if (!ok) {
                        m_app->setLastMessage(message, QStringLiteral("error"));
                        emit finished(false, message);
                        return;
                    }
                    if (mode == QLatin1String("template")) {
                        if (m_app->m_pendingEffectTemplate && m_app->m_pendingEffectTemplate->valid()) {
                            const EffectTemplateEntry *entry =
                                effectTemplateForId(m_app->m_pendingEffectTemplate->templateId);
                            const AppController::PendingEffectTemplate pending = *m_app->m_pendingEffectTemplate;
                            m_app->m_pendingEffectTemplate.reset();
                            if (entry) {
                                m_app->applyEffectTemplateInternal(pending.trackIndex, pending.clipIndex,
                                                                   *entry, mattePath, srcIn);
                            }
                        }
                        m_app->setLastMessage(message);
                        emit finished(true, message);
                        return;
                    }
                    finalizeSegmentation(clipId, mattePath, fgrPath, srcIn, mode);
                    m_app->setLastMessage(message);
                    emit finished(true, message);
                },
                Qt::QueuedConnection);
        };

        drift::Sam2Segmenter &sam = drift::Sam2Segmenter::instance();
        if (!rvm && !sam.available()) {
            finish(false, sam.lastError(), {});
            return;
        }

        const drift::TimeUs step = drift::kUsPerSecond / fps;
        const int totalFrames = int((srcOut - srcIn + step - 1) / step);
        if (totalFrames <= 0) {
            finish(false, tr("Clip is too short to cut out"), {});
            return;
        }

        const QString mattePath = drift::newMattePath();
        if (mattePath.isEmpty()) {
            finish(false, tr("Could not create a cutout file"), {});
            return;
        }

        // RVM also produces a colour-decontaminated foreground. It rides in a second sidecar next
        // to the matte, except for a template cutout, which only ever consumes the coverage map —
        // writing one there would encode a whole clip for a file nothing reads.
        const bool wantFgr = rvm && mode != QLatin1String("template");
        const QString fgrPath = wantFgr ? drift::newMattePath() : QString();
        if (wantFgr && fgrPath.isEmpty()) {
            finish(false, tr("Could not create a cutout file"), {});
            return;
        }

        drift::MatteWriter writer;
        drift::MatteWriter fgrWriter;
        bool writerOpen = false;

        std::unique_ptr<drift::Sam2Segmenter::Track> track;
        std::unique_ptr<drift::RvmMatter::Track> rvmTrack;
        if (rvm) {
            rvmTrack = drift::RvmMatter::instance().newTrack(rvmQuality);
            if (!rvmTrack) {
                finish(false, drift::RvmMatter::instance().lastError(), {});
                return;
            }
        } else {
            track = sam.newTrack();
            if (!track) {
                finish(false, sam.lastError(), {});
                return;
            }
        }
        int occludedFrames = 0;
        QString error;

        // Both sidecars are aborted together: a foreground with no matte is unusable, and a
        // half-written pair must not look like a finished cutout.
        const auto abortAll = [&] {
            writer.abort();
            if (wantFgr)
                fgrWriter.abort();
        };

        for (int i = 0; i < totalFrames; ++i) {
            if (m_segmentCancel.loadRelaxed() != 0) {
                abortAll();
                finish(false, tr("Cutout cancelled"), {});
                return;
            }

            const drift::TimeUs sourceUs = srcIn + drift::TimeUs(i) * step;
            const QImage frame = ClipReaderPool::instance().readVideoFrame(
                path, kCutoutRenderStreamId, sourceUs, canvasW, canvasH, rotationCorrection);
            if (frame.isNull()) {
                abortAll();
                finish(false, tr("Could not decode frame %1").arg(i), {});
                return;
            }

            if (!writerOpen) {
                if (!writer.open(mattePath, frame.size(), fps, 1, &error)) {
                    finish(false, error, {});
                    return;
                }
                if (wantFgr
                    && !fgrWriter.open(fgrPath, frame.size(), fps, 1, &error,
                                       drift::MatteWriter::Mode::Colour)) {
                    writer.abort();
                    finish(false, error, {});
                    return;
                }
                writerOpen = true;
            }

            QImage coverage;
            if (rvm) {
                // No prompt and no seed frame: the subject is "the people in shot", and the
                // recurrent state carries identity from one frame to the next.
                const drift::RvmResult result = rvmTrack->step(frame);
                if (!result.ok) {
                    abortAll();
                    finish(false, result.error, {});
                    return;
                }
                coverage = result.alpha;
                if (wantFgr && !fgrWriter.writeFrame(result.foreground, &error)) {
                    abortAll();
                    finish(false, error, {});
                    return;
                }
            } else {
                const drift::Sam2Embedding embedding = sam.encode(frame);
                if (!embedding.valid) {
                    abortAll();
                    finish(false, sam.lastError(), {});
                    return;
                }

                // The first frame is prompted; every later frame is propagated purely from the
                // model's memory bank, so no prompt is carried forward by hand.
                drift::Sam2Result result;
                if (i == 0) {
                    drift::Sam2Prompt prompt;
                    for (int p = 0; p < normalized.points.size(); ++p) {
                        prompt.points.append(QPointF(normalized.points.at(p).x() * frame.width(),
                                                     normalized.points.at(p).y() * frame.height()));
                        prompt.labels.append(normalized.labels.at(p));
                    }
                    result = track->seed(embedding, prompt);
                } else {
                    result = track->step(embedding);
                }

                if (!result.ok) {
                    abortAll();
                    finish(false, result.error, {});
                    return;
                }
                if (result.occluded)
                    ++occludedFrames;
                coverage = result.mask;
            }

            if (!writer.writeFrame(coverage, &error)) {
                abortAll();
                finish(false, error, {});
                return;
            }

            setProgress(double(i + 1) / totalFrames,
                        tr("Processing frame %1 of %2\u2026").arg(i + 1).arg(totalFrames));
        }

        if (!writer.finish(&error) || (wantFgr && !fgrWriter.finish(&error))) {
            abortAll();
            finish(false, error, {});
            return;
        }

        // Occlusion is the model's own call rather than a tracking failure — it recovers by
        // itself — but a clip that is mostly occluded usually means the wrong subject was marked.
        finish(true,
               occludedFrames > 0
                   ? tr("Cutout complete — subject cut out on %1 of %2 frames")
                         .arg(occludedFrames)
                         .arg(totalFrames)
                   : tr("Cutout complete"),
               mattePath, fgrPath);
    });
}

void SegmentationController::finalizeSegmentation(const QString &clipId, const QString &mattePath,
                                         const QString &matteFgrPath,
                                         drift::TimeUs matteSrcOffsetUs, const QString &outputMode)
{
    int trackIndex = -1;
    int clipIndex = -1;
    for (int t = 0; t < m_app->m_project.tracks().size() && trackIndex < 0; ++t) {
        const drift::Track &track = m_app->m_project.tracks().at(t);
        for (int c = 0; c < track.clips.size(); ++c) {
            if (track.clips.at(c).id == clipId) {
                trackIndex = t;
                clipIndex = c;
                break;
            }
        }
    }
    if (trackIndex < 0) {
        // The clip was deleted while the job ran; the matte has nothing to attach to.
        QFile::remove(mattePath);
        if (!matteFgrPath.isEmpty())
            QFile::remove(matteFgrPath);
        m_app->setLastMessage(tr("That clip no longer exists"), QStringLiteral("warning"));
        return;
    }

    const drift::Project before = m_app->m_project;
    const QString sourceId = m_app->m_project.tracks().at(trackIndex).clips.at(clipIndex).id;

    // Full-frame: a segmentation matte's own pixels place the subject, so the mask rect must not
    // crop it. The parametric defaults would.
    drift::Mask matte = drift::fullFrameMediaMask(mattePath, matteSrcOffsetUs);
    matte.mediaFgrPath = matteFgrPath;
    matte.name = tr("Cutout");
    // "clips" used to derive a foreground/background pair onto two new video tracks. One mask
    // layer on the clip itself does the same job non-destructively, so both output modes now land
    // here; the string is kept accepted for the agents that still pass it.
    matte.invert = false;

    // The original clip is deliberately left alone: the cutout is a mask pinned to it, visible on
    // its own lane, and removing the lane restores the shot. Stacked, not replaced — a cutout is
    // one more layer on whatever masks the clip already carries.
    drift::addLinkedMask(m_app->m_project, trackIndex, clipIndex, matte);

    m_app->pushProjectEdit(before, tr("Cut out subject"));
    m_app->finishEdit(tr("Cut out subject"));

    // Minting a lane inserts a track, and normalization can reorder the list again, so the
    // indices captured above are stale — re-resolve by id before selecting.
    for (int t = 0; t < m_app->m_project.tracks().size(); ++t) {
        const drift::Track &track = m_app->m_project.tracks().at(t);
        for (int c = 0; c < track.clips.size(); ++c) {
            if (track.clips.at(c).id == sourceId) {
                m_app->selectClip(t, c);
                return;
            }
        }
    }
}
