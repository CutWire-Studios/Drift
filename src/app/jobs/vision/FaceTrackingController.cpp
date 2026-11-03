#include "app/jobs/vision/FaceTrackingController.h"

#include "app/editor/AppController.h"
#include "engine/decode/readers/ClipReaderPool.h"
#include "engine/packages/effects/EffectCatalog.h"
#include "engine/face/tracking/FaceLandmarker.h"
#include "engine/face/swap/FaceSwapSource.h"
#include "engine/face/tracking/FaceTrack.h"
#include "engine/decode/probe/StillImage.h"

#include <QFile>
#include <QMetaObject>
#include <QtConcurrent>

FaceTrackingController::FaceTrackingController(AppController *app, QObject *parent)
    : QObject(parent)
    , m_app(app)
{
}

namespace {

constexpr quint64 kFaceDetectStreamId = 0xA5'11'5C'A4'00'00'00'05ull;

} // namespace

bool FaceTrackingController::available()
{
    return drift::FaceLandmarker::modelPresent();
}

void FaceTrackingController::cancel()
{
    if (m_detecting)
        m_cancel.storeRelaxed(1);
}

void FaceTrackingController::clearTrack(int trackIndex, int clipIndex)
{
    // Same redirect as detect: the caller is the effect adjustment's inspector.
    const drift::ClipRef source = m_app->sourceClipRef(trackIndex, clipIndex);
    trackIndex = source.trackIndex;
    clipIndex = source.clipIndex;
    if (trackIndex < 0 || trackIndex >= m_app->m_project.tracks().size())
        return;
    if (clipIndex < 0 || clipIndex >= m_app->m_project.tracks().at(trackIndex).clips.size())
        return;
    if (m_app->m_project.tracks().at(trackIndex).clips.at(clipIndex).faceTrackPath.isEmpty())
        return;

    // The sidecar itself is left on disk: undo has to be able to bring the track back, and these
    // files are small and live in a cache directory.
    const drift::Project before = m_app->m_project;
    m_app->m_project.tracks()[trackIndex].clips[clipIndex].faceTrackPath.clear();
    m_app->m_project.tracks()[trackIndex].clips[clipIndex].faceTrackSrcOffsetUs = 0;
    m_app->pushProjectEdit(before, tr("Clear Face Track"));
    m_app->finishEdit(tr("Clear Face Track"));
}

void FaceTrackingController::detect(int trackIndex, int clipIndex)
{
    if (m_detecting) {
        m_app->setLastMessage(tr("Face detection already in progress"), QStringLiteral("warning"));
        return;
    }
    // The prompt that asks for a scan lives in the effect adjustment's inspector, so what it
    // hands over is the adjustment. The landmarks belong on the clip it is pinned to.
    const drift::ClipRef source = m_app->sourceClipRef(trackIndex, clipIndex);
    trackIndex = source.trackIndex;
    clipIndex = source.clipIndex;
    if (trackIndex < 0 || trackIndex >= m_app->m_project.tracks().size())
        return;
    const drift::Track &track = m_app->m_project.tracks().at(trackIndex);
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    const drift::Clip clip = track.clips.at(clipIndex);
    if (clip.type != drift::ClipType::Video && clip.type != drift::ClipType::Image) {
        m_app->setLastMessage(tr("Select a video clip to detect faces in"), QStringLiteral("warning"));
        return;
    }
    if (clip.path.isEmpty() || clip.srcOut <= clip.srcIn) {
        m_app->setLastMessage(tr("Clip has no video to scan"), QStringLiteral("warning"));
        return;
    }

    // Same reason as the export and segmentation jobs: playback would drive the decode pool from a
    // second thread while this job walks it frame by frame.
    m_app->setPlaying(false);

    m_cancel.storeRelaxed(0);
    m_progress = 0.0;
    emit progressChanged();
    m_status = tr("Getting ready…");
    emit statusChanged();
    m_detecting = true;
    emit detectingChanged();
    m_app->setLastMessage(tr("Detecting faces…"));

    const QString path = clip.path;
    // A still is one frame however long it sits on the timeline, and FaceTrack::sample clamps to
    // its last frame, so it is scanned once rather than once per frame of its duration.
    const bool still = clip.type == drift::ClipType::Image;
    const drift::TimeUs srcIn = still ? 0 : clip.srcIn;
    const drift::TimeUs srcOut = still ? 1 : clip.srcOut;
    const int fps = qMax(1, m_app->m_project.fps());
    const int canvasW = m_app->m_project.width();
    const int canvasH = m_app->m_project.height();
    // Resolved by id at the end rather than by index: the timeline can be edited while the job
    // runs, and stale indices would attach the track to the wrong clip.
    const QString clipId = clip.id;

    // Landmarks are in frame pixels, so detect on frames oriented the way the compositor shows them.
    const int rotationCorrection = clip.rotationCorrection;
    (void)QtConcurrent::run([this, path, still, srcIn, srcOut, fps, canvasW, canvasH, clipId,
                             rotationCorrection]() {
        auto setProgress = [this](double fraction, const QString &status) {
            QMetaObject::invokeMethod(
                this,
                [this, fraction, status]() {
                    m_progress = fraction;
                    emit progressChanged();
                    if (!status.isEmpty() && status != m_status) {
                        m_status = status;
                        emit statusChanged();
                    }
                },
                Qt::QueuedConnection);
        };

        auto finish = [this, clipId, srcIn](bool ok, const QString &message,
                                            const QString &trackPath) {
            QMetaObject::invokeMethod(
                this,
                [this, ok, message, trackPath, clipId, srcIn]() {
                    m_detecting = false;
                    emit detectingChanged();
                    m_progress = ok ? 1.0 : 0.0;
                    emit progressChanged();
                    m_status = ok ? tr("Done") : message;
                    emit statusChanged();
                    if (!ok) {
                        m_app->setLastMessage(message, QStringLiteral("error"));
                        emit finished(false, message);
                        return;
                    }
                    finalize(clipId, trackPath, srcIn);
                    m_app->setLastMessage(message);
                    emit finished(true, message);
                },
                Qt::QueuedConnection);
        };

        drift::FaceLandmarker &landmarker = drift::FaceLandmarker::instance();
        if (!landmarker.available()) {
            finish(false, landmarker.lastError(), {});
            return;
        }

        const drift::TimeUs step = drift::kUsPerSecond / fps;
        const int totalFrames = int((srcOut - srcIn + step - 1) / step);
        if (totalFrames <= 0) {
            finish(false, tr("Clip is too short to scan"), {});
            return;
        }

        drift::FaceTrack faceTrack;
        faceTrack.fps = fps;
        faceTrack.startSrcUs = srcIn;
        faceTrack.frames.reserve(totalFrames);

        QList<drift::FaceAnchors> previous;
        int framesWithFace = 0;

        for (int i = 0; i < totalFrames; ++i) {
            if (m_cancel.loadRelaxed() != 0) {
                finish(false, tr("Face detection cancelled"), {});
                return;
            }

            const drift::TimeUs sourceUs = srcIn + drift::TimeUs(i) * step;
            // Stills decode the way the compositor draws them (Qt first, FFmpeg as fallback).
            // FFmpeg alone cannot open some files Qt reads fine — a .jpeg, for one — which is
            // how an image that renders could still fail the scan on frame 0.
            const QImage frame = still
                ? drift::decodeStillImage(path, canvasW, canvasH)
                : ClipReaderPool::instance().readVideoFrame(path, kFaceDetectStreamId, sourceUs,
                                                            canvasW, canvasH, rotationCorrection);
            if (frame.isNull()) {
                finish(false, tr("Could not decode frame %1").arg(i), {});
                return;
            }

            // The previous frame seeds the next one's ROI, which is what keeps a face in the same
            // slot for the whole clip and skips the detector while tracking holds.
            QList<drift::FaceAnchors> faces =
                landmarker.detect(frame, previous.isEmpty() ? nullptr : &previous);
            previous = faces;

            drift::FaceTrackFrame baked;
            baked.faces = faces;
            faceTrack.frames.append(baked);

            for (const drift::FaceAnchors &a : faces) {
                if (a.valid) {
                    ++framesWithFace;
                    break;
                }
            }

            setProgress(double(i + 1) / totalFrames,
                        tr("Scanning frame %1 of %2…").arg(i + 1).arg(totalFrames));
        }

        if (framesWithFace == 0) {
            finish(false, tr("No faces found in this clip"), {});
            return;
        }

        drift::smoothFaceTrack(&faceTrack);

        const QString trackPath = drift::newFaceTrackPath();
        QString error;
        if (trackPath.isEmpty() || !drift::writeFaceTrack(trackPath, faceTrack, &error)) {
            finish(false, error.isEmpty() ? tr("Could not write the face track") : error,
                   {});
            return;
        }

        finish(true,
               framesWithFace < totalFrames
                   ? tr("Face detection complete — a face was visible in %1 of %2 frames")
                         .arg(framesWithFace)
                         .arg(totalFrames)
                   : tr("Face detection complete"),
               trackPath);
    });
}

void FaceTrackingController::ingestSwapSource(const QString &photoPath)
{
    if (photoPath.isEmpty() || drift::faceSwapSourceReady(photoPath))
        return;
    if (m_swapIngesting.contains(photoPath))
        return;
    // Gated on the models being present rather than attempted and failed: without the addon this
    // would report an error on every clip in the project the moment one is opened.
    if (!drift::FaceLandmarker::modelPresent())
        return;

    m_swapIngesting.insert(photoPath);
    (void)QtConcurrent::run([this, photoPath]() {
        QString error;
        const bool ok = drift::ingestFaceSwapSource(photoPath, &error);
        QMetaObject::invokeMethod(
            this,
            [this, photoPath, ok, error]() {
                m_swapIngesting.remove(photoPath);
                if (!ok) {
                    m_app->setLastMessage(error, QStringLiteral("error"));
                    return;
                }
                // The effect has been rendering pass-through while this ran.
                m_app->emitPreviewFrame();
            },
            Qt::QueuedConnection);
    });
}

void FaceTrackingController::ingestSwapSourcesInProject()
{
    for (const drift::Track &track : m_app->m_project.tracks()) {
        for (const drift::Clip &clip : track.clips) {
            for (const drift::Effect &effect : clip.effects) {
                const EffectPresetEntry *def = effectDefForId(effect.catalogId);
                if (!def || !def->isFaceSwap)
                    continue;
                ingestSwapSource(
                    effect.parameters.value(QStringLiteral("sourceImage")).toString());
            }
        }
    }
}

void FaceTrackingController::finalize(const QString &clipId, const QString &trackPath,
                                          drift::TimeUs srcOffsetUs)
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
        // The clip was deleted while the job ran; the track has nothing to attach to.
        QFile::remove(trackPath);
        m_app->setLastMessage(tr("Scanned clip no longer exists"), QStringLiteral("warning"));
        return;
    }

    const drift::Project before = m_app->m_project;
    m_app->m_project.tracks()[trackIndex].clips[clipIndex].faceTrackPath = trackPath;
    m_app->m_project.tracks()[trackIndex].clips[clipIndex].faceTrackSrcOffsetUs = srcOffsetUs;
    m_app->pushProjectEdit(before, tr("Detect Faces"));
    m_app->finishEdit(tr("Detect Faces"));
    m_app->selectClip(trackIndex, clipIndex);
}

#include "moc_FaceTrackingController.cpp"
