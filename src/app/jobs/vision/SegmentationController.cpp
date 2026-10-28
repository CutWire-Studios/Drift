#include "app/jobs/vision/SegmentationController.h"

#include "app/editor/AppController.h"
#include "app/mcp/McpDetail.h"
#include "ui/imageproviders/segment/SegmentImageStore.h"
#include "engine/packages/effects/EffectTemplateCatalog.h"
#include "engine/encode/writers/MatteWriter.h"
#include "engine/ml/vision/segmentation/RvmMatter.h"

#include "app/jobs/vision/SegmentationPlan.h"
#include "engine/decode/readers/ClipReaderPool.h"

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QtConcurrent>

#include <cmath>
#include <functional>

namespace {
// Offline scans read a file end to end at their own pace. They need decode cursors of their own so
// they never share one with timeline playback of the same media — see ClipReaderPool.
constexpr quint64 kSegmentEncodeStreamId = 0xA5'11'5C'A4'00'00'00'03ull;
constexpr quint64 kCutoutRenderStreamId = 0xA5'11'5C'A4'00'00'00'04ull;
constexpr quint64 kSegmentScrubStreamId = 0xA5'11'5C'A4'00'00'00'0Bull;
constexpr quint64 kRefineMatteStreamId = 0xA5'11'5C'A4'00'00'00'0Cull;

// Reverse propagation decodes this many frames ahead of itself, so it holds a chunk of canvas-sized
// frames at a time rather than the whole clip.
constexpr int kReverseChunkFrames = 8;
}

QString SegmentationController::previewMaskUrl() const
{
    return m_segHasMask ? QStringLiteral("image://segment/mask?rev=%1").arg(m_segPreviewCounter)
                        : QString();
}

void SegmentationController::setPreviewMask(const QImage &mask)
{
    SegmentImageStore::setMask(mask);
    m_segHasMask = !mask.isNull();
    ++m_segPreviewCounter;
    emit previewMaskChanged();
}

void SegmentationController::setSeedRunning(bool running)
{
    if (m_segSeedRunning == running)
        return;
    m_segSeedRunning = running;
    emit previewBusyChanged();
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
    m_segSourceUs = -1;
    m_segEmbedding = drift::Sam2Embedding{};
    ++m_segSeedGeneration;
    setPreviewMask(QImage());
    ++m_segRevision;
    emit sessionPointsChanged();
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
    m_segClipId = track.clips.at(clipIndex).id;
    m_segPoints.clear();
    m_segForTemplate = forTemplate;
    m_segSessionActive = true;
    // A new session starts from nothing: an encode still running for the previous clip would
    // otherwise make setFrame refuse, and its result is dropped by the generation bump anyway.
    ++m_segGeneration;
    ++m_segSeedGeneration;
    m_segEncoding = false;
    m_segSourceUs = -1;
    m_segEmbedding = drift::Sam2Embedding{};
    m_segFramePending.reset();
    setPreviewMask(QImage());
    emit sessionPointsChanged();
    emit sessionChanged();
    setFrame(seconds);
}

void SegmentationController::beginSessionAtPlayhead(int trackIndex, int clipIndex)
{
    beginSession(trackIndex, clipIndex, m_app->playheadSeconds());
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
    m_segClipId.clear();
    m_segPoints.clear();
    m_segFrame = QImage();
    m_segEmbedding = drift::Sam2Embedding{};
    m_segSourceUs = -1;
    m_segFramePending.reset();
    ++m_segGeneration;
    ++m_segSeedGeneration;
    setSeedRunning(false);
    SegmentImageStore::clear();
    m_segHasMask = false;
    ++m_segPreviewCounter;
    emit previewMaskChanged();
    emit sessionPointsChanged();
    ++m_segRevision;
    emit sessionChanged();
    emit previewBusyChanged();
}

void SegmentationController::setFrame(double seconds)
{
    if (!m_segSessionActive)
        return;
    if (m_segEncoding) {
        m_segFramePending = seconds;
        return;
    }
    if (!resolveSessionClip())
        return;
    const drift::Track &track = m_app->m_project.tracks().at(m_segTrack);
    if (m_segClip < 0 || m_segClip >= track.clips.size())
        return;

    const drift::Clip clip = track.clips.at(m_segClip);

    const drift::TimeUs timelineUs =
        qBound(clip.timelineStart, drift::secondsToUs(seconds),
               clip.timelineStart + clip.timelineDuration - 1);
    const drift::TimeUs sourceUs = clip.timelineToSourceUs(timelineUs);
    // Same frame as the one already encoded: the embedding, the points and the preview all still
    // apply, so a playhead that did not move must not wipe them.
    if (sourceUs == m_segSourceUs && !m_segFrame.isNull()
        && (m_segBackend == QLatin1String("rvm") || m_segEmbedding.valid))
        return;
    m_segSeconds = drift::usToSeconds(timelineUs);
    m_segSourceUs = sourceUs;
    emit sessionFrameChanged();
    const QString path = clip.path;
    const int canvasW = m_app->m_project.width();
    const int canvasH = m_app->m_project.height();

    m_segEncoding = true;
    m_segPoints.clear();
    ++m_segSeedGeneration;
    const int generation = ++m_segGeneration;
    emit sessionPointsChanged();
    emit previewBusyChanged();
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
                if (frame.isNull() || !embedding.valid)
                    m_segSourceUs = -1;
                SegmentImageStore::setFrame(frame);
                setPreviewMask(mask);
                ++m_segRevision;
                if (frame.isNull() || !error.isEmpty())
                    m_app->setLastMessage(error, QStringLiteral("error"));
                emit previewBusyChanged();
                emit sessionChanged();
                if (m_segFramePending) {
                    const double next = *m_segFramePending;
                    m_segFramePending.reset();
                    setFrame(next);
                }
            },
            Qt::QueuedConnection);
    });
}

void SegmentationController::scrubFrame(double seconds)
{
    if (!m_segSessionActive)
        return;
    if (m_segEncoding) {
        m_segFramePending = seconds;
        return;
    }
    if (m_segScrubBusy) {
        m_segScrubPending = seconds;
        return;
    }
    if (!resolveSessionClip())
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
    m_segSourceUs = -1;
    ++m_segSeedGeneration;
    emit sessionPointsChanged();

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
                    setPreviewMask(QImage());
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
    emit sessionPointsChanged();
    refreshPreview();
}

void SegmentationController::removePoint(int index)
{
    if (!m_segSessionActive || index < 0 || index >= m_segPoints.size())
        return;
    m_segPoints.removeAt(index);
    emit sessionPointsChanged();
    refreshPreview();
}

void SegmentationController::clearPoints()
{
    if (!m_segSessionActive)
        return;
    m_segPoints.clear();
    emit sessionPointsChanged();
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
        setPreviewMask(QImage());
        ++m_segRevision;
        emit sessionChanged();
        return;
    }

    // Coalesce rapid point edits onto one ONNX seed at a time — sessions are not
    // safe to call concurrently, and only the latest prompt matters.
    ++m_segSeedGeneration;
    if (m_segSeedRunning)
        return;

    setSeedRunning(true);
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
                    setSeedRunning(false);
                    return;
                }
                if (generation == m_segSeedGeneration) {
                    setPreviewMask(result.ok ? result.mask : QImage());
                    if (!result.ok)
                        m_app->setLastMessage(result.error, QStringLiteral("error"));
                    ++m_segRevision;
                    emit sessionChanged();
                    setSeedRunning(false);
                    return;
                }
                // A newer prompt arrived while we were seeding — run again with the latest, unless
                // that prompt is now empty or its frame is still encoding.
                if (m_segPoints.isEmpty() || !m_segEmbedding.valid) {
                    setSeedRunning(false);
                    return;
                }
                runSeed(m_segSeedGeneration);
            },
            Qt::QueuedConnection);
    });
}

bool SegmentationController::resolveSessionClip()
{
    return !m_segClipId.isEmpty() && drift::mcpdetail::findClipById(m_app->m_project, m_segClipId, &m_segTrack, &m_segClip);
}

int SegmentationController::pointIndexAt(double nx, double ny, double tolPx, double frameW, double frameH) const
{
    QList<QPointF> points;
    points.reserve(m_segPoints.size());
    for (const QVariant &v : m_segPoints) {
        const QVariantMap p = v.toMap();
        points.append(QPointF(p.value(QStringLiteral("x")).toDouble(), p.value(QStringLiteral("y")).toDouble()));
    }
    return drift::segplan::nearestPointIndex(points, nx, ny, tolPx, frameW, frameH);
}

void SegmentationController::runSession(const QString &outputMode)
{
    if (!m_segSessionActive)
        return;
    if (backendUsesPoints() && m_segPoints.isEmpty())
        return;
    if (!resolveSessionClip())
        return;
    QString mode = outputMode;
    if (m_segForTemplate && m_app->m_pendingEffectTemplate && m_app->m_pendingEffectTemplate->valid())
        mode = QStringLiteral("template");
    startCutout(m_segTrack, m_segClip, m_segPoints, m_segSourceUs, QStringLiteral("both"), mode, {},
                {}, {});
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
    startCutout(trackIndex, clipIndex, points, -1, QStringLiteral("forward"), outputMode, backend,
                quality, {});
}

void SegmentationController::propagate(int trackIndex, int clipIndex, const QString &direction,
                                       const QString &targetAdjustmentId)
{
    if (!m_segSessionActive)
        return;
    if (backendUsesPoints() && m_segSourceUs < 0) {
        m_app->setLastMessage(tr("Wait for the frame to finish loading"), QStringLiteral("warning"));
        return;
    }
    QString mode = QStringLiteral("adjustment");
    if (m_segForTemplate && m_app->m_pendingEffectTemplate && m_app->m_pendingEffectTemplate->valid())
        mode = QStringLiteral("template");
    startCutout(trackIndex, clipIndex, m_segPoints, m_segSourceUs, direction, mode, {}, {},
                targetAdjustmentId);
}

namespace {

struct Sam2Job
{
    QString path;
    drift::TimeUs srcIn = 0;
    drift::TimeUs step = 1;
    int total = 0;
    int canvasW = 0;
    int canvasH = 0;
    int rotationCorrection = 0;
    drift::Sam2Prompt normalized;
    int promptIndex = 0;
    drift::segplan::Range range;
    QString oldMattePath; // frames outside `range` come from here; empty pads with black
    drift::TimeUs oldOffsetUs = 0;
    QString mattePath;
    int fps = 25;
};

// Runs the SAM2 passes for one job and writes the matte. Returns an error message, or empty on
// success; *cancelled is set instead when the user stopped it.
QString runSam2Job(const Sam2Job &job, const std::function<bool()> &isCancelled,
                   const std::function<void(double, const QString &)> &progress, int *occluded,
                   bool *cancelled)
{
    using namespace drift::segplan;
    drift::Sam2Segmenter &sam = drift::Sam2Segmenter::instance();

    const auto frameAt = [&](int i) {
        return ClipReaderPool::instance().readVideoFrame(
            job.path, kCutoutRenderStreamId, job.srcIn + drift::TimeUs(i) * job.step, job.canvasW,
            job.canvasH, job.rotationCorrection);
    };
    const auto decodeError = [](int i) {
        return QCoreApplication::translate("SegmentationController", "Could not decode frame %1")
            .arg(i);
    };
    const auto scaledPrompt = [&](const QSize &size) {
        drift::Sam2Prompt prompt;
        for (int p = 0; p < job.normalized.points.size(); ++p) {
            prompt.points.append(QPointF(job.normalized.points.at(p).x() * size.width(),
                                         job.normalized.points.at(p).y() * size.height()));
            prompt.labels.append(job.normalized.labels.at(p));
        }
        return prompt;
    };

    const bool wantBack = job.range.first < job.promptIndex;
    const bool wantFwd = job.range.last > job.promptIndex;
    const int work = job.range.size();
    int done = 0;
    const auto tick = [&] {
        progress(double(++done) / work,
                 QCoreApplication::translate("SegmentationController",
                                             "Processing frame %1 of %2…")
                     .arg(done)
                     .arg(work));
    };

    const QImage promptFrame = frameAt(job.promptIndex);
    if (promptFrame.isNull())
        return decodeError(job.promptIndex);
    const drift::Sam2Embedding promptEmbedding = sam.encode(promptFrame);
    if (!promptEmbedding.valid)
        return sam.lastError();
    const drift::Sam2Prompt prompt = scaledPrompt(promptFrame.size());
    const QSize size = promptFrame.size();

    QTemporaryDir spill;
    if (wantBack && !spill.isValid())
        return QCoreApplication::translate("SegmentationController", "Could not create a cutout file");
    const auto spillPath = [&](int i) { return spill.filePath(QString::number(i) + QStringLiteral(".png")); };

    QImage promptMask;
    *occluded = 0;

    // The reverse pass has to finish before anything is written: the matte is appended in
    // increasing time order, and these are the frames that come first.
    if (wantBack) {
        std::unique_ptr<drift::Sam2Segmenter::Track> track = sam.newTrack();
        if (!track)
            return sam.lastError();
        const drift::Sam2Result seeded = track->seed(promptEmbedding, prompt);
        if (!seeded.ok)
            return seeded.error;
        promptMask = seeded.mask;
        if (seeded.occluded)
            ++*occluded;
        tick();

        for (const Chunk &chunk : reverseChunks(job.range.first, job.promptIndex - 1, kReverseChunkFrames)) {
            QList<QImage> frames;
            for (int i = chunk.first; i <= chunk.last; ++i) {
                if (isCancelled()) {
                    *cancelled = true;
                    return {};
                }
                const QImage frame = frameAt(i);
                if (frame.isNull())
                    return decodeError(i);
                frames.append(frame);
            }
            for (int i = chunk.last; i >= chunk.first; --i) {
                if (isCancelled()) {
                    *cancelled = true;
                    return {};
                }
                const drift::Sam2Embedding embedding = sam.encode(frames.at(i - chunk.first));
                if (!embedding.valid)
                    return sam.lastError();
                frames[i - chunk.first] = QImage();
                const drift::Sam2Result result = track->step(embedding);
                if (!result.ok)
                    return result.error;
                if (result.occluded)
                    ++*occluded;
                if (!result.mask.save(spillPath(i), "PNG"))
                    return QCoreApplication::translate("SegmentationController",
                                                       "Could not create a cutout file");
                tick();
            }
        }
    }

    drift::MatteWriter writer;
    QString error;
    if (!writer.open(job.mattePath, size, job.fps, 1, &error))
        return error;
    const auto fail = [&](const QString &message) {
        writer.abort();
        return message;
    };

    QImage black(size, QImage::Format_Grayscale8);
    black.fill(0);
    const auto keptFrame = [&](int i) {
        if (job.oldMattePath.isEmpty())
            return black;
        const QImage old = ClipReaderPool::instance().readVideoFrame(
            job.oldMattePath, kRefineMatteStreamId,
            qMax<drift::TimeUs>(0, job.srcIn + drift::TimeUs(i) * job.step - job.oldOffsetUs),
            size.width(), size.height());
        if (old.isNull())
            return black;
        QImage gray = old.convertToFormat(QImage::Format_Grayscale8);
        return gray.size() == size ? gray : gray.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    };
    const auto writeKept = [&](int from, int to) -> QString {
        for (int i = from; i < to; ++i) {
            if (isCancelled()) {
                *cancelled = true;
                return {};
            }
            if (!writer.writeFrame(keptFrame(i), &error))
                return error;
        }
        return {};
    };

    QString result = writeKept(0, job.range.first);
    if (*cancelled || !result.isEmpty())
        return fail(result);

    for (int i = job.range.first; i < job.promptIndex; ++i) {
        if (isCancelled()) {
            *cancelled = true;
            return fail({});
        }
        QImage mask(spillPath(i));
        if (mask.isNull() || !writer.writeFrame(mask.convertToFormat(QImage::Format_Grayscale8), &error))
            return fail(error.isEmpty() ? QCoreApplication::translate("SegmentationController",
                                                                      "Could not create a cutout file")
                                        : error);
    }

    // A forward pass needs its own track — the reverse one has remembered frames from the other
    // side of the prompt — and a lone prompt frame still needs a seed to have a mask.
    std::unique_ptr<drift::Sam2Segmenter::Track> track;
    if (wantFwd || !wantBack) {
        track = sam.newTrack();
        if (!track)
            return fail(sam.lastError());
        const drift::Sam2Result seeded = track->seed(promptEmbedding, prompt);
        if (!seeded.ok)
            return fail(seeded.error);
        promptMask = seeded.mask;
        if (!wantBack) {
            if (seeded.occluded)
                ++*occluded;
            tick();
        }
    }
    if (!writer.writeFrame(promptMask, &error))
        return fail(error);

    for (int i = job.promptIndex + 1; i <= job.range.last; ++i) {
        if (isCancelled()) {
            *cancelled = true;
            return fail({});
        }
        const QImage frame = frameAt(i);
        if (frame.isNull())
            return fail(decodeError(i));
        const drift::Sam2Embedding embedding = sam.encode(frame);
        if (!embedding.valid)
            return fail(sam.lastError());
        const drift::Sam2Result stepped = track->step(embedding);
        if (!stepped.ok)
            return fail(stepped.error);
        if (stepped.occluded)
            ++*occluded;
        if (!writer.writeFrame(stepped.mask, &error))
            return fail(error);
        tick();
    }

    result = writeKept(job.range.last + 1, job.total);
    if (*cancelled || !result.isEmpty())
        return fail(result);
    if (!writer.finish(&error))
        return fail(error);
    return {};
}

} // namespace

void SegmentationController::startCutout(int trackIndex, int clipIndex, const QVariantList &points,
                                         drift::TimeUs promptSourceUs, const QString &direction,
                                         const QString &outputMode, const QString &backend,
                                         const QString &quality, const QString &targetAdjustmentId)
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

    // Refining splices into a matte this clip already carries; anything else would overwrite a
    // mask the user did not ask to touch.
    QString oldMattePath;
    drift::TimeUs oldOffsetUs = 0;
    const QString target = rvm ? QString() : targetAdjustmentId;
    if (!target.isEmpty()) {
        bool found = false;
        for (const drift::ClipRef &ref :
             drift::linkedMaskAdjustments(m_app->m_project, trackIndex, clipIndex)) {
            const drift::Clip &adjustment =
                m_app->m_project.tracks().at(ref.trackIndex).clips.at(ref.clipIndex);
            if (adjustment.id == target && adjustment.mask.shape == drift::MaskShape::Media) {
                oldMattePath = adjustment.mask.mediaPath;
                oldOffsetUs = adjustment.mask.mediaSrcOffsetUs;
                found = true;
                break;
            }
        }
        if (!found) {
            m_app->setLastMessage(tr("That object mask no longer exists"), QStringLiteral("warning"));
            return;
        }
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

    const drift::TimeUs srcIn = clip.srcIn;
    const int fps = qMax(1, m_app->m_project.fps());
    const QString mode = outputMode.isEmpty() ? QStringLiteral("adjustment") : outputMode;
    // Resolved by id at the end rather than by index: the timeline can be edited while the job
    // runs, and stale indices would apply the matte to the wrong clip.
    const QString clipId = clip.id;

    Sam2Job job;
    job.path = clip.path;
    job.srcIn = srcIn;
    job.step = drift::kUsPerSecond / fps;
    job.total = drift::segplan::frameCount(srcIn, clip.srcOut, job.step);
    job.canvasW = m_app->m_project.width();
    job.canvasH = m_app->m_project.height();
    // Masks are traced against the frames the compositor shows, so they must decode at the same
    // orientation or the matte lands transposed.
    job.rotationCorrection = clip.rotationCorrection;
    job.normalized = normalized;
    job.promptIndex = drift::segplan::frameIndexAt(srcIn, job.step, job.total,
                                                   promptSourceUs < 0 ? srcIn : promptSourceUs);
    job.range = drift::segplan::propagationRange(job.total, job.promptIndex, direction);
    job.oldMattePath = oldMattePath;
    job.oldOffsetUs = oldOffsetUs;
    job.fps = fps;

    (void)QtConcurrent::run([this, job, fps, mode, clipId, rvm, rvmQuality, target, oldMattePath]() mutable {
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

        auto finish = [this, clipId, srcIn = job.srcIn, mode, target, oldMattePath](
                          bool ok, const QString &message, const QString &mattePath,
                          const QString &fgrPath = QString()) {
            QMetaObject::invokeMethod(
                this,
                [this, ok, message, mattePath, fgrPath, clipId, srcIn, mode, target,
                 oldMattePath]() {
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
                    if (!target.isEmpty())
                        finalizeRefine(clipId, target, oldMattePath, mattePath, srcIn);
                    else
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

        if (job.total <= 0) {
            finish(false, tr("Clip is too short to cut out"), {});
            return;
        }

        job.mattePath = drift::newMattePath();
        if (job.mattePath.isEmpty()) {
            finish(false, tr("Could not create a cutout file"), {});
            return;
        }

        if (!rvm) {
            int occludedFrames = 0;
            bool cancelled = false;
            const QString error = runSam2Job(
                job, [this] { return m_segmentCancel.loadRelaxed() != 0; }, setProgress,
                &occludedFrames, &cancelled);
            if (cancelled || !error.isEmpty()) {
                QFile::remove(job.mattePath);
                finish(false, cancelled ? tr("Cutout cancelled") : error, {});
                return;
            }
            // Occlusion is the model's own call rather than a tracking failure — it recovers by
            // itself — but a clip that is mostly occluded usually means the wrong subject was marked.
            finish(true,
                   !target.isEmpty() ? tr("Object mask refined")
                   : occludedFrames > 0
                       ? tr("Cutout complete — subject cut out on %1 of %2 frames")
                             .arg(occludedFrames)
                             .arg(job.range.size())
                       : tr("Cutout complete"),
                   job.mattePath, {});
            return;
        }

        const QString mattePath = job.mattePath;
        const drift::TimeUs srcIn = job.srcIn;
        const drift::TimeUs step = job.step;
        const int totalFrames = job.total;
        const QString path = job.path;
        const int canvasW = job.canvasW;
        const int canvasH = job.canvasH;
        const int rotationCorrection = job.rotationCorrection;

        // RVM also produces a colour-decontaminated foreground. It rides in a second sidecar next
        // to the matte, except for a template cutout, which only ever consumes the coverage map —
        // writing one there would encode a whole clip for a file nothing reads.
        const bool wantFgr = mode != QLatin1String("template");
        const QString fgrPath = wantFgr ? drift::newMattePath() : QString();
        if (wantFgr && fgrPath.isEmpty()) {
            finish(false, tr("Could not create a cutout file"), {});
            return;
        }

        drift::MatteWriter writer;
        drift::MatteWriter fgrWriter;
        bool writerOpen = false;

        std::unique_ptr<drift::RvmMatter::Track> rvmTrack =
            drift::RvmMatter::instance().newTrack(rvmQuality);
        if (!rvmTrack) {
            finish(false, drift::RvmMatter::instance().lastError(), {});
            return;
        }
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

            // No prompt and no seed frame: the subject is "the people in shot", and the
            // recurrent state carries identity from one frame to the next.
            const drift::RvmResult result = rvmTrack->step(frame);
            if (!result.ok) {
                abortAll();
                finish(false, result.error, {});
                return;
            }
            if (wantFgr && !fgrWriter.writeFrame(result.foreground, &error)) {
                abortAll();
                finish(false, error, {});
                return;
            }

            if (!writer.writeFrame(result.alpha, &error)) {
                abortAll();
                finish(false, error, {});
                return;
            }

            setProgress(double(i + 1) / totalFrames,
                        tr("Processing frame %1 of %2…").arg(i + 1).arg(totalFrames));
        }

        if (!writer.finish(&error) || (wantFgr && !fgrWriter.finish(&error))) {
            abortAll();
            finish(false, error, {});
            return;
        }

        finish(true, tr("Cutout complete"), mattePath, fgrPath);
    });
}

void SegmentationController::finalizeRefine(const QString &clipId, const QString &adjustmentId,
                                            const QString &oldMattePath, const QString &newMattePath,
                                            drift::TimeUs newOffsetUs)
{
    for (int t = 0; t < m_app->m_project.tracks().size(); ++t) {
        drift::Track &track = m_app->m_project.tracks()[t];
        for (int c = 0; c < track.clips.size(); ++c) {
            drift::Clip &adjustment = track.clips[c];
            if (adjustment.id != adjustmentId)
                continue;
            // Edited under the job: the matte it was spliced from is no longer the one on screen.
            if (adjustment.linkedClipId != clipId || adjustment.mask.shape != drift::MaskShape::Media
                || adjustment.mask.mediaPath != oldMattePath) {
                QFile::remove(newMattePath);
                m_app->setLastMessage(tr("That object mask changed while it was being refined"),
                                      QStringLiteral("warning"));
                return;
            }
            const drift::Project before = m_app->m_project;
            // Frame fixes are keyed by media time, which moves with the offset; the host frame each fixes stays put.
            const bool keepsFixes = !adjustment.mask.mediaPatches.isEmpty();
            QMap<drift::TimeUs, QString> rebased;
            for (auto it = adjustment.mask.mediaPatches.constBegin(); it != adjustment.mask.mediaPatches.constEnd(); ++it)
                rebased.insert(it.key() + adjustment.mask.mediaSrcOffsetUs - newOffsetUs, it.value());
            adjustment.mask.mediaPath = newMattePath;
            adjustment.mask.mediaSrcOffsetUs = newOffsetUs;
            adjustment.mask.mediaFgrPath.clear();
            adjustment.mask.mediaPatches = rebased;
            m_app->pushProjectEdit(before, tr("Refine object mask"));
            m_app->finishEdit(keepsFixes ? tr("Object mask refined. Frame fixes were kept") : tr("Refine object mask"));
            return;
        }
    }
    QFile::remove(newMattePath);
    m_app->setLastMessage(tr("That object mask no longer exists"), QStringLiteral("warning"));
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
