#include "app/jobs/vision/DepthController.h"

#include "app/editor/AppController.h"
#include "app/mcp/McpDetail.h"
#include "app/jobs/JobRegistry.h"
#include "engine/decode/readers/ClipReaderPool.h"
#include "engine/ml/vision/depth/DepthSidecar.h"
#include "engine/decode/probe/StillImage.h"
#include "engine/ml/vision/depth/VdaDepth.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QPointer>

#include <memory>

using drift::mcpdetail::findClipById;

DepthController::DepthController(AppController *app, QObject *parent)
    : QObject(parent)
    , m_app(app)
{
}

// --- depth estimation -------------------------------------------------------

namespace {

// Short side of the depth map. 392 measured about 0.55 s a frame on CPU against 1.2 s at the
// model's native 518, for depth that differs mostly in fine edges.
constexpr int kDepthShortSide = 392;
constexpr int kDepthShortSideHigh = 518;
// Frames are decoded no larger than this; the model never sees more than 518 on the short side.
constexpr int kDepthDecodeBound = 1280;
// Bump when the sidecar written for the same inputs would change, so stale ones stop matching.
constexpr int kDepthSchema = 1;
constexpr quint64 kDepthScanStreamId = 0xA5'11'5C'A4'00'00'00'09ull;

} // namespace

bool DepthController::available()
{
    return drift::VdaDepth::modelPresent();
}

QString DepthController::estimate(int trackIndex, int clipIndex, bool highQuality)
{
    // Same redirect as FaceTrackingController::detect: the prompt lives in the effect
    // adjustment's inspector, and the depth belongs on the clip it is pinned to.
    const drift::ClipRef source = m_app->sourceClipRef(trackIndex, clipIndex);
    trackIndex = source.trackIndex;
    clipIndex = source.clipIndex;
    if (trackIndex < 0 || trackIndex >= m_app->m_project.tracks().size())
        return {};
    const drift::Track &track = m_app->m_project.tracks().at(trackIndex);
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return {};

    const drift::Clip clip = track.clips.at(clipIndex);
    if (clip.type != drift::ClipType::Video && clip.type != drift::ClipType::Image) {
        m_app->setLastMessage(tr("Select a video or image clip to estimate depth for"),
                       QStringLiteral("warning"));
        return {};
    }
    if (clip.path.isEmpty() || (clip.type == drift::ClipType::Video && clip.srcOut <= clip.srcIn)) {
        m_app->setLastMessage(tr("Clip has no video to estimate depth for"), QStringLiteral("warning"));
        return {};
    }
    if (!drift::VdaDepth::modelPresent()) {
        m_app->setLastMessage(tr("Depth estimation needs the Depth addon"), QStringLiteral("warning"));
        return {};
    }
    if (m_app->m_jobs->hasActive(QStringLiteral("depth"), clip.id)) {
        m_app->setLastMessage(tr("Depth is already being estimated for this clip"),
                       QStringLiteral("warning"));
        return {};
    }

    // Same reason as the face and segmentation jobs: playback would drive the decode pool from a
    // second thread while this walks it frame by frame.
    m_app->setPlaying(false);

    const QString path = clip.path;
    const bool still = clip.type == drift::ClipType::Image;
    const drift::TimeUs srcIn = still ? 0 : clip.srcIn;
    const drift::TimeUs srcOut = still ? 1 : clip.srcOut;
    const int fps = qMax(1, m_app->m_project.fps());
    const int rotationCorrection = clip.rotationCorrection;
    const int shortSide = highQuality ? kDepthShortSideHigh : kDepthShortSide;
    // Resolved by id at the end rather than by index: the timeline can be edited while this runs.
    const QString clipId = clip.id;
    auto result = std::make_shared<QString>();
    QPointer<DepthController> self(this);

    const QString id = m_app->m_jobs->start(
        QStringLiteral("depth"), clipId, JobRegistry::Lane::Model,
        [self, path, still, srcIn, srcOut, fps, rotationCorrection, shortSide, clipId,
         result](JobContext &ctx) {
            const auto notify = [self, clipId]() {
                QMetaObject::invokeMethod(
                    self.data(),
                    [self, clipId]() {
                        if (self)
                            emit self->jobChanged(clipId);
                    },
                    Qt::QueuedConnection);
            };
            ctx.progress(0.0, QObject::tr("Loading the depth model…"));
            notify();

            drift::VdaDepth &vda = drift::VdaDepth::instance();
            if (!vda.available())
                return ctx.fail(QStringLiteral("model_error"), vda.lastError());

            // Named by what produced it, so estimating the same pixels again — this clip after an
            // undo, or another clip cut from the same range — finds the first result.
            const QFileInfo info(path);
            QCryptographicHash hash(QCryptographicHash::Sha256);
            hash.addData(QStringLiteral("%1|%2|%3|%4|%5|%6|%7|%8|%9")
                             .arg(info.absoluteFilePath())
                             .arg(info.size())
                             .arg(info.lastModified().toMSecsSinceEpoch())
                             .arg(rotationCorrection)
                             .arg(srcIn)
                             .arg(srcOut)
                             .arg(fps)
                             .arg(shortSide)
                             .arg(vda.variant() + QLatin1Char('/') + QString::number(kDepthSchema))
                             .toUtf8());
            const QString dir = drift::depthCacheDir();
            if (dir.isEmpty())
                return ctx.fail(QStringLiteral("io_error"), QObject::tr("No cache directory"));
            const QString sidecarPath = QDir(dir).filePath(
                QString::fromLatin1(hash.result().toHex().left(32)) + QStringLiteral(".driftdepth"));
            if (drift::DepthSidecar::open(sidecarPath)) {
                *result = sidecarPath;
                return ctx.succeed({{QStringLiteral("path"), sidecarPath},
                                    {QStringLiteral("cached"), true}});
            }

            const drift::TimeUs step = drift::kUsPerSecond / fps;
            const int total = still ? 1 : int((srcOut - srcIn + step - 1) / step);

            drift::DepthSidecarWriter writer;
            bool writerOpen = false;
            QString error;
            int written = 0;
            std::unique_ptr<drift::VdaDepth::Pass> pass;
            pass = vda.newPass(shortSide, [&](drift::TimeUs ptsUs, const float *disparity) {
                if (!writerOpen) {
                    writerOpen = writer.open(sidecarPath, pass->size(), vda.variant(), &error);
                    if (!writerOpen)
                        return false;
                }
                ++written;
                return writer.writeFrame(ptsUs, disparity, &error) && !ctx.cancelled();
            });
            if (!pass)
                return ctx.fail(QStringLiteral("model_error"), vda.lastError());

            int lastPercent = -1;
            for (int i = 0; i < total; ++i) {
                if (ctx.cancelled())
                    return;
                const drift::TimeUs sourceUs = srcIn + drift::TimeUs(i) * step;
                // Stills decode the way the compositor draws them; FFmpeg alone cannot open some
                // files Qt reads fine (a .jpeg, for one). See FaceTrackingController::detect.
                const QImage frame = still
                    ? drift::decodeStillImage(path, kDepthDecodeBound, kDepthDecodeBound)
                    : ClipReaderPool::instance().readVideoFrame(
                          path, kDepthScanStreamId, sourceUs, kDepthDecodeBound, kDepthDecodeBound,
                          rotationCorrection);
                if (frame.isNull())
                    return ctx.fail(QStringLiteral("decode_error"),
                                    QObject::tr("Could not decode frame %1").arg(i));
                if (!pass->push(frame, sourceUs)) {
                    if (ctx.cancelled())
                        return;
                    return ctx.fail(QStringLiteral("model_error"),
                                    error.isEmpty() ? pass->error() : error);
                }
                const int percent = (i + 1) * 100 / total;
                if (percent != lastPercent) {
                    lastPercent = percent;
                    ctx.progress(double(i + 1) / total,
                                 QObject::tr("Estimating depth, frame %1 of %2…").arg(i + 1).arg(total));
                    notify();
                }
            }
            if (!pass->flush() || !writerOpen || !writer.finish(&error)) {
                if (ctx.cancelled())
                    return;
                return ctx.fail(QStringLiteral("model_error"),
                                error.isEmpty() ? pass->error() : error);
            }
            *result = sidecarPath;
            ctx.succeed({{QStringLiteral("path"), sidecarPath},
                         {QStringLiteral("frames"), written},
                         {QStringLiteral("cached"), false}});
        },
        [this, clipId, result](const QJsonObject &job) {
            if (job.value(QStringLiteral("ok")).toBool()) {
                finalize(clipId, *result);
            } else {
                const QJsonObject error = job.value(QStringLiteral("error")).toObject();
                if (error.value(QStringLiteral("code")).toString() != QLatin1String("cancelled"))
                    m_app->setLastMessage(error.value(QStringLiteral("message")).toString(),
                                   QStringLiteral("error"));
            }
            emit jobChanged(clipId);
            return QJsonObject{};
        });
    emit jobChanged(clipId);
    m_app->setLastMessage(tr("Estimating depth…"));
    return id;
}

void DepthController::finalize(const QString &clipId, const QString &path)
{
    int trackIndex = -1;
    int clipIndex = -1;
    if (!findClipById(m_app->m_project, clipId, &trackIndex, &clipIndex)) {
        // The clip was deleted while the job ran. The sidecar stays: it is keyed by content and a
        // redo or another clip of the same range will find it.
        m_app->setLastMessage(tr("Clip no longer exists"), QStringLiteral("warning"));
        return;
    }
    const drift::Project before = m_app->m_project;
    m_app->m_project.tracks()[trackIndex].clips[clipIndex].depthPath = path;
    m_app->pushProjectEdit(before, tr("Estimate Depth"));
    m_app->finishEdit(tr("Estimate Depth"));
}

void DepthController::cancel(const QString &clipId)
{
    for (const QJsonValue &v : m_app->m_jobs->jobs()) {
        const QJsonObject job = v.toObject();
        if (job.value(QStringLiteral("kind")).toString() == QLatin1String("depth")
            && job.value(QStringLiteral("target")).toString() == clipId
            && job.value(QStringLiteral("active")).toBool()) {
            m_app->m_jobs->cancel(job.value(QStringLiteral("id")).toString());
        }
    }
}

QVariantMap DepthController::job(const QString &clipId) const
{
    const QJsonArray jobs = m_app->m_jobs->jobs();
    for (qsizetype i = jobs.size() - 1; i >= 0; --i) {
        const QJsonObject job = jobs.at(i).toObject();
        if (job.value(QStringLiteral("kind")).toString() != QLatin1String("depth")
            || job.value(QStringLiteral("target")).toString() != clipId) {
            continue;
        }
        return {{QStringLiteral("active"), job.value(QStringLiteral("active")).toBool()},
                {QStringLiteral("progress"), job.value(QStringLiteral("progress")).toDouble()},
                {QStringLiteral("status"), job.value(QStringLiteral("status")).toString()},
                {QStringLiteral("error"), job.value(QStringLiteral("error"))
                                              .toObject()
                                              .value(QStringLiteral("message"))
                                              .toString()}};
    }
    return {};
}

void DepthController::clear(int trackIndex, int clipIndex)
{
    const drift::ClipRef source = m_app->sourceClipRef(trackIndex, clipIndex);
    trackIndex = source.trackIndex;
    clipIndex = source.clipIndex;
    if (trackIndex < 0 || trackIndex >= m_app->m_project.tracks().size())
        return;
    if (clipIndex < 0 || clipIndex >= m_app->m_project.tracks().at(trackIndex).clips.size())
        return;
    if (m_app->m_project.tracks().at(trackIndex).clips.at(clipIndex).depthPath.isEmpty())
        return;

    // The sidecar stays on disk so undo can bring it back.
    const drift::Project before = m_app->m_project;
    m_app->m_project.tracks()[trackIndex].clips[clipIndex].depthPath.clear();
    m_app->pushProjectEdit(before, tr("Clear Depth"));
    m_app->finishEdit(tr("Clear Depth"));
}

double DepthController::sampleAt(int trackIndex, int clipIndex, double nx, double ny,
                                    double atSeconds)
{
    const drift::ClipRef source = m_app->sourceClipRef(trackIndex, clipIndex);
    if (source.trackIndex < 0 || source.trackIndex >= m_app->m_project.tracks().size())
        return -1.0;
    const drift::Track &track = m_app->m_project.tracks().at(source.trackIndex);
    if (source.clipIndex < 0 || source.clipIndex >= track.clips.size())
        return -1.0;
    const drift::Clip &clip = track.clips.at(source.clipIndex);
    const std::shared_ptr<const drift::DepthSidecar> sidecar =
        drift::loadDepthSidecarCached(clip.depthPath);
    if (!sidecar)
        return -1.0;
    const drift::TimeUs at = atSeconds < 0.0 ? m_app->m_playheadUs : drift::secondsToUs(atSeconds);
    return sidecar->sample(clip.timelineToSourceUs(at), qBound(0.0, nx, 1.0), qBound(0.0, ny, 1.0));
}

#include "moc_DepthController.cpp"
