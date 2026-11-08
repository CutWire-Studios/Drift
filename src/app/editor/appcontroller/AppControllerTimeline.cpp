#include "app/editor/appcontroller/AppControllerInternal.h"
#include "app/bin/AssetPreviewController.h"
#include "app/mcp/McpController.h"
#include "app/editor/sessions/SpeedCurveController.h"
#include "app/editor/PreferencesController.h"
#include "app/editor/ProjectFileController.h"

#include "engine/render2d/pdf/PdfClipRenderer.h"
// Engine-internal by its own header comment, and included here only for releaseCaches(): the GPU
// caches have no other owner outside src/engine to ask. A one-line forwarder on GpuCompositor
// would restore the boundary.
#include "engine/render2d/vector/VectorInspect.h"


#include <QGuiApplication>
#include <QCoreApplication>
#include <QScopedValueRollback>
#include <QSettings>
#include <QPointer>

using namespace drift::appdetail;
using drift::mcpdetail::findClipById;
using drift::mcpdetail::runBeatAnalysis;
using namespace drift::appcontroller_detail;

namespace {

bool trackHasCompleteOverlap(const drift::Track &track, const drift::Clip &clip, const drift::Clip *before)
{
    for (const drift::Clip &other : track.clips) {
        if (other.id == clip.id || !clipsOverlapCompletely(clip, other))
            continue;
        if (!before || !clipsOverlapCompletely(*before, other))
            return true;
    }
    return false;
}

void syncOverlapTransitions(drift::Project &project)
{
    for (drift::Track &track : project.tracks())
        syncOverlapTransitionsOnTrack(track);
}


// How much source a trim of `delta` timeline µs eats into the clip's edge.
//
// A ramp is normalised over the clip's source range, so a trim rescales it and the duration has
// to be re-derived afterwards (syncDurationFromSpeedCurve) rather than simply shifted by delta.
// Extending past an edge is outside anything the curve describes, so the rate at that end stands
// in for it.
drift::TimeUs trimSourceDelta(const drift::Clip &clip, drift::TimeUs delta, bool extending,
                              bool atTail)
{
    if (!clip.hasSpeedCurve())
        return clip.sourceDeltaForTimelineDelta(delta);

    const drift::TimeUs span = clip.srcOut - clip.srcIn;
    if (extending) {
        const double edgeSpeed = clip.speedCurve.speedAt(atTail ? 1.0 : 0.0);
        return static_cast<drift::TimeUs>(llround(static_cast<double>(delta) * edgeSpeed));
    }
    return clip.speedCurve.sourceOffsetForTimelineOffset(delta, span);
}


// A bin asset of kind vector is a Lottie .json on disk and one of kind model3d a .glb; the clip
// carries either by path and the probe fills what the renderer and the inspector need.
void attachAssetSource(drift::Clip &clip)
{
    if (clip.type == drift::ClipType::Model3d) {
        clip.model3d.path = clip.path;
        probeModel3dSource(clip.model3d);
        return;
    }
    if (clip.type == drift::ClipType::Pdf) {
        clip.pdf.path = clip.path;
        drift::probePdfSource(clip.pdf);
        return;
    }
    if (clip.type != drift::ClipType::Vector)
        return;
    clip.vector.path = clip.path;
    clip.vector.kind = drift::vec::detectVectorKind(drift::vec::vectorSourceBytes(clip.vector));
    drift::vec::probeVectorSource(clip.vector);
}

// Ripple by time rather than by index, over several tracks at once: every clip on those tracks
// starting at or after fromUs moves by delta, and linked partners on other tracks follow. Index
// ripple (applyRippleShift) only ever reached one track, which is how a trim of a linked clip used
// to leave its partner's followers behind.
void rippleTracksFrom(drift::Project &project, const QSet<int> &trackIndexes, drift::TimeUs fromUs,
                      drift::TimeUs delta, const QSet<QString> &excludeIds)
{
    if (delta == 0)
        return;
    QList<drift::Clip> moved;
    QSet<QString> movedIds;
    for (const int t : trackIndexes) {
        if (t < 0 || t >= project.tracks().size())
            continue;
        for (drift::Clip &clip : project.tracks()[t].clips) {
            if (excludeIds.contains(clip.id) || clip.timelineStart < fromUs)
                continue;
            clip.timelineStart = qMax<drift::TimeUs>(0, clip.timelineStart + delta);
            moved.append(clip);
            movedIds.insert(clip.id);
        }
    }
    for (const drift::Clip &clip : std::as_const(moved))
        syncLinkedPartnersFrom(project, clip, movedIds + excludeIds);
}

void splitLinkedPartnerAt(drift::Project &project, const drift::Clip &sourceHead, drift::TimeUs playheadUs,
                          const QString &tailLinkId)
{
    if (sourceHead.linkId.isEmpty())
        return;

    for (const drift::ClipRef &ref : drift::linkedPartners(project, sourceHead)) {
        drift::Track &track = project.tracks()[ref.trackIndex];
        if (ref.clipIndex < 0 || ref.clipIndex >= track.clips.size())
            continue;

        drift::Clip &partner = track.clips[ref.clipIndex];
        if (!partner.containsTime(playheadUs) || playheadUs == partner.timelineStart)
            continue;

        const drift::TimeUs offset = playheadUs - partner.timelineStart;
        drift::Clip partnerTail;
        if (!drift::splitClipAtOffset(partner, partnerTail, offset, project.fps()))
            continue;

        partnerTail.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        if (!tailLinkId.isEmpty())
            partnerTail.linkId = tailLinkId;
        else
            drift::assignSplitLinkIds(partner, partnerTail);
        track.clips.insert(ref.clipIndex + 1, partnerTail);
    }
}

void splitLinkedAdjustmentsAt(drift::Project &project, int parentTrackIndex, const QString &headClipId,
                              const QString &tailClipId, drift::TimeUs offset)
{
    if (headClipId.isEmpty() || tailClipId.isEmpty())
        return;

    for (const int laneIndex : drift::adjustmentLaneIndexes(project, parentTrackIndex)) {
        drift::Track &lane = project.tracks()[laneIndex];
        for (int c = lane.clips.size() - 1; c >= 0; --c) {
            if (lane.clips.at(c).linkedClipId != headClipId)
                continue;
            drift::Clip tailAdjustment = lane.clips.at(c);
            tailAdjustment.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            tailAdjustment.linkedClipId = tailClipId;
            drift::shiftClipKeyframes(tailAdjustment, -offset);
            lane.clips.insert(c + 1, tailAdjustment);
        }
    }
}

void expandSelectionWithLinkedPartners(const drift::Project &project, QList<QPair<int, int>> &pairs)
{
    QList<QPair<int, int>> expanded = pairs;
    for (const QPair<int, int> &pair : pairs) {
        if (pair.first < 0 || pair.first >= project.tracks().size())
            continue;
        const drift::Track &track = project.tracks().at(pair.first);
        if (pair.second < 0 || pair.second >= track.clips.size())
            continue;

        for (const drift::ClipRef &ref : drift::linkedPartners(project, track.clips.at(pair.second))) {
            const QPair<int, int> linked(ref.trackIndex, ref.clipIndex);
            if (!expanded.contains(linked))
                expanded.append(linked);
        }
    }
    pairs = expanded;
}
} // namespace

QVariantList AppController::audioOutputDevices() const
{
    QVariantList devices;
    devices.append(QVariantMap{{QStringLiteral("id"), QString()},
                               {QStringLiteral("label"), tr("System default")}});
    const QList<QAudioDevice> outputs = QMediaDevices::audioOutputs();
    for (const QAudioDevice &device : outputs) {
        devices.append(QVariantMap{{QStringLiteral("id"), QString::fromUtf8(device.id())},
                                   {QStringLiteral("label"), device.description()}});
    }
    return devices;
}

void AppController::setAudioOutputDeviceId(const QString &id)
{
    if (id == m_audioOutputDeviceId)
        return;

    m_audioOutputDeviceId = id;
    QSettings().setValue(QStringLiteral("audio/outputDeviceId"), id);
    // The id is kept even when that device is not currently present: the sinks fall back to the
    // default and pick the chosen one back up when it reappears.
    const QByteArray bytes = id.toUtf8();
    m_playback.setAudioDeviceId(bytes);
    m_speedCurve->setAudioDeviceId(bytes);
    m_assetPreview->setAudioDeviceId(bytes);
    emit audioOutputDeviceIdChanged();
}

bool AppController::isRecordingAudio() const
{
    return m_audioRecorder.isRecording();
}

int AppController::recordingTrackIndex() const
{
    return m_audioRecorder.recordingTrackIndex();
}

float AppController::audioRecordLevel() const
{
    return m_audioRecorder.audioLevel();
}

double AppController::audioRecordSeconds() const
{
    return m_audioRecorder.recordedSeconds();
}

QVariantList AppController::availableMicrophones() const
{
    return m_audioRecorder.availableDevices();
}

QString AppController::currentMicrophoneName() const
{
    return m_audioRecorder.currentDeviceName();
}

void AppController::selectMicrophone(const QString &id)
{
    m_audioRecorder.selectDevice(id);
}

void AppController::startFocusedClipLoudness(bool normalize, double targetLufs)
{
    if (m_loudnessBusy)
        return;
    if (!isValidClipIndex(m_selectedTrack, m_selectedClip)) {
        emit loudnessFinished({{QStringLiteral("ok"), false},
                               {QStringLiteral("detail"),
                                normalize ? tr("Select a clip to normalize")
                                          : tr("Select a clip to measure")}});
        return;
    }
    // The clip is tracked by id: the selection indices can move while the measurement runs.
    const QString clipId = m_project.tracks().at(m_selectedTrack).clips.at(m_selectedClip).id;
    auto job = std::make_shared<const McpController::LoudnessJob>(
        m_mcpController->clipLoudnessJob(m_selectedTrack, m_selectedClip));
    m_loudnessCancel = std::make_shared<std::atomic<bool>>(false);
    m_loudnessBusy = true;
    m_loudnessProgress = 0.0;
    emit loudnessBusyChanged();
    emit loudnessProgressChanged();

    QPointer<AppController> self(this);
    (void)QtConcurrent::run([self, job, cancel = m_loudnessCancel, clipId, normalize, targetLufs]() {
        double reported = 0.0;
        const drift::LoudnessResult measured = McpController::measureLoudnessJob(
            *job, [&](double fraction) {
                if (cancel->load())
                    return false;
                if (fraction - reported >= 0.01) {
                    reported = fraction;
                    QMetaObject::invokeMethod(
                        QCoreApplication::instance(),
                        [self, fraction]() {
                            if (!self)
                                return;
                            self->m_loudnessProgress = fraction;
                            emit self->loudnessProgressChanged();
                        },
                        Qt::QueuedConnection);
                }
                return true;
            });
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [self, cancel, clipId, normalize, targetLufs, measured]() {
                if (!self)
                    return;
                self->m_loudnessBusy = false;
                self->m_loudnessProgress = 0.0;
                emit self->loudnessBusyChanged();
                emit self->loudnessProgressChanged();
                // A cancelled run stopped mid-file, so its numbers are partial and it reports nothing.
                if (cancel->load())
                    return;
                QVariantMap result;
                if (!measured.ok) {
                    result = QVariantMap{{QStringLiteral("ok"), false},
                                         {QStringLiteral("detail"), tr("No audio in range")}};
                } else if (!normalize) {
                    result = QVariantMap{{QStringLiteral("ok"), true},
                                         {QStringLiteral("lufs"), measured.integratedLufs},
                                         {QStringLiteral("true_peak_db"), measured.truePeakDb},
                                         {QStringLiteral("duration"), measured.durationSeconds}};
                } else {
                    const QPair<int, int> at = self->m_mcpController->locateClip(clipId);
                    if (at.first < 0) {
                        result = QVariantMap{{QStringLiteral("ok"), false},
                                             {QStringLiteral("detail"),
                                              tr("The clip is no longer in the timeline")}};
                    } else {
                        result = self->m_mcpController
                                     ->applyNormalizeGain(at.first, at.second,
                                                          measured.integratedLufs,
                                                          measured.truePeakDb, targetLufs)
                                     .toVariantMap();
                    }
                }
                emit self->loudnessFinished(result);
            },
            Qt::QueuedConnection);
    });
}

void AppController::cancelFocusedClipLoudness()
{
    if (m_loudnessBusy)
        m_loudnessCancel->store(true);
}

bool AppController::isAudioRecordingPaused() const
{
    return m_audioRecorder.isPaused();
}

float AppController::audioRecordGain() const
{
    return m_audioRecorder.gain();
}

void AppController::setAudioRecordGain(float gain)
{
    m_audioRecorder.setGain(gain);
}

int AppController::audioRecordLivePeakCount() const
{
    return m_audioRecorder.livePeakCount();
}

QVariantList AppController::audioRecordLivePeakColumns(double first, double count, int columns) const
{
    return m_audioRecorder.livePeakColumns(first, count, columns);
}

void AppController::pauseAudioRecording()
{
    if (!m_audioRecorder.isRecording() || m_audioRecorder.isPaused())
        return;

    m_audioRecorder.pause();
    if (m_playback.isPlaying() || m_playing) {
        m_playing = false;
        m_playback.pause();
        emit playingChanged();
    }
}

void AppController::resumeAudioRecording()
{
    if (!m_audioRecorder.isRecording() || !m_audioRecorder.isPaused())
        return;

    m_audioRecorder.resume();
    if (!m_playing) {
        setPlaying(true);
    }
}

void AppController::toggleAudioRecordingPause()
{
    if (m_audioRecorder.isPaused())
        resumeAudioRecording();
    else
        pauseAudioRecording();
}

void AppController::startAudioRecording(int trackIndex)
{
    if (m_audioRecorder.isRecording()) {
        stopAudioRecording();
        return;
    }

    int targetTrack = trackIndex;
    if (targetTrack < 0 || targetTrack >= m_project.tracks().size()
        || m_project.tracks().at(targetTrack).type != drift::TrackType::Audio) {
        if (m_selectedTrack >= 0 && m_selectedTrack < m_project.tracks().size()
            && m_project.tracks().at(m_selectedTrack).type == drift::TrackType::Audio) {
            targetTrack = m_selectedTrack;
        } else {
            targetTrack = drift::ensureTrackForClipType(m_project, drift::ClipType::Audio, true);
        }
    }

    if (targetTrack < 0 || targetTrack >= m_project.tracks().size()) {
        setLastMessage(tr("No audio track available for recording"), QStringLiteral("error"));
        return;
    }

    const QString outputPath = drift::newVoiceoverPath();
    if (outputPath.isEmpty()) {
        setLastMessage(tr("Failed to create audio recording file"), QStringLiteral("error"));
        return;
    }

    m_recordingStartPlayheadUs = m_playheadUs;

    QString error;
    if (!m_audioRecorder.startRecording(targetTrack, outputPath, &error)) {
        setLastMessage(error.isEmpty() ? tr("Failed to start audio recording") : error,
                       QStringLiteral("error"));
        return;
    }

    m_playback.setVoiceoverRecording(true);
    if (!m_playing) {
        setPlaying(true);
    }
    setLastMessage(tr("Recording audio…"), QStringLiteral("info"));
}

void AppController::stopAudioRecording()
{
    if (!m_audioRecorder.isRecording())
        return;

    const int trackIndex = m_audioRecorder.recordingTrackIndex();
    drift::TimeUs recordedDurationUs = 0;
    const QString recordedPath = m_audioRecorder.stopRecording(&recordedDurationUs);

    m_playback.setVoiceoverRecording(false);
    if (m_playback.isPlaying() || m_playing) {
        m_playing = false;
        m_playback.pause();
        emit playingChanged();
    }

    if (recordedPath.isEmpty() || recordedDurationUs < drift::secondsToUs(0.2)) {
        if (!recordedPath.isEmpty()) {
            QFile::remove(recordedPath);
        }
        setPlayheadUs(m_recordingStartPlayheadUs);
        setLastMessage(tr("Audio recording cancelled (too short)"), QStringLiteral("info"));
        return;
    }

    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    const drift::Project before = m_project;

    drift::Clip clip;
    clip.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    clip.type = drift::ClipType::Audio;
    clip.name = tr("Voiceover %1").arg(++m_voiceoverCounter);
    clip.path = recordedPath;
    clip.timelineStart = m_recordingStartPlayheadUs;
    clip.timelineDuration = recordedDurationUs;
    clip.srcIn = 0;
    clip.srcOut = recordedDurationUs;

    m_project.tracks()[trackIndex].clips.append(clip);
    const int newClipIndex = m_project.tracks().at(trackIndex).clips.size() - 1;

    // Park playhead cleanly right at the end of the recorded clip
    const drift::TimeUs endPlayheadUs = m_recordingStartPlayheadUs + recordedDurationUs;
    setPlayheadUs(endPlayheadUs);

    pushProjectEdit(before, tr("Record audio"));
    finishEdit(tr("Recorded voiceover"));
    selectClip(trackIndex, newClipIndex);
    setLastMessage(tr("Voiceover recorded"), QStringLiteral("success"));
}

void AppController::cancelAudioRecording()
{
    if (!m_audioRecorder.isRecording())
        return;

    m_audioRecorder.cancelRecording();
    m_playback.setVoiceoverRecording(false);

    if (m_playback.isPlaying() || m_playing) {
        m_playing = false;
        m_playback.pause();
        emit playingChanged();
    }

    setPlayheadUs(m_recordingStartPlayheadUs);
    setLastMessage(tr("Recording cancelled"), QStringLiteral("info"));
}

void AppController::setLastMessage(const QString &message, const QString &severity)
{
    if (m_lastMessage == message && m_lastMessageSeverity == severity)
        return;

    m_lastMessage = message;
    m_lastMessageSeverity = severity;
    emit lastMessageChanged();
}

QUrl AppController::fileUrl(const QString &path) const
{
    if (path.isEmpty())
        return {};
    // projectLocation() stores a SAF document as its encoded URI, and currentProjectPath is what
    // Save-in-place feeds back through here. fromLocalFile on one produces "file:///content:/…".
    if (path.startsWith(QLatin1String("content://"), Qt::CaseInsensitive))
        return QUrl(path);
    return QUrl::fromLocalFile(path);
}

QString AppController::imageUrl(const QString &path) const
{
    if (path.isEmpty())
        return {};
    return QStringLiteral("image://drift/") + QString::fromUtf8(QUrl::toPercentEncoding(path));
}

QString AppController::filmstripFrameUrl(const QString &path, int frame, int count) const
{
    if (path.isEmpty())
        return {};
    return imageUrl(path) + QStringLiteral("?frame=%1&count=%2").arg(frame).arg(count);
}

QString AppController::filmstripTileUrl(const QString &path, int level, double index,
                                        int rotationCorrection) const
{
    if (path.isEmpty())
        return {};
    const QString tile =
        m_filmstripTiles.tile(path, level, static_cast<qint64>(index), rotationCorrection);
    return tile.isEmpty() ? QString() : imageUrl(tile);
}

double AppController::snapTime(double seconds, const QString &excludeClipId) const
{
    return drift::usToSeconds(drift::snapTime(m_project, drift::secondsToUs(seconds), m_snapEnabled,
                                              m_playheadUs, extraSnapTargetsCached(), excludeClipId,
                                              m_snapThresholdUs));
}

void AppController::setSnapRadius(double pixels, double pxPerSecond)
{
    if (pixels <= 0.0 || pxPerSecond <= 0.0)
        return;
    m_snapThresholdUs = qMax<drift::TimeUs>(1, drift::secondsToUs(pixels / pxPerSecond));
}

drift::TimeUs AppController::clipDurationForAssetIndex(int assetIndex) const
{
    if (!m_assetLibrary)
        return drift::imageClipDurationUs();
    return drift::clipDurationForAsset(m_project.asset(m_assetLibrary->assetIdAt(assetIndex)));
}

drift::TimeUs AppController::sourceDurationForClip(const drift::Clip &clip) const
{
    return drift::sourceDurationForClip(m_project, clip);
}
namespace drift::appdetail {


QVariantList transformToList(const QTransform &t)
{
    return {t.m11(), t.m12(), t.m13(), t.m21(), t.m22(), t.m23(), t.m31(), t.m32(), t.m33()};
}

// A 2D homography as a 4x4 on (x, y, z, w) that leaves z alone: what a QtQuick Matrix4x4 needs.
QMatrix4x4 liftHomography(const QTransform &t)
{
    return QMatrix4x4(float(t.m11()), float(t.m21()), 0.f, float(t.m31()),
                      float(t.m12()), float(t.m22()), 0.f, float(t.m32()),
                      0.f, 0.f, 1.f, 0.f,
                      float(t.m13()), float(t.m23()), 0.f, float(t.m33()));
}

QTransform previewBoxParent(const QVariantMap &box)
{
    if (!box.value(QStringLiteral("parentActive")).toBool())
        return {};
    const QVariantList m = box.value(QStringLiteral("parent")).toList();
    if (m.size() != 9)
        return {};
    return QTransform(m.at(0).toDouble(), m.at(1).toDouble(), m.at(2).toDouble(),
                      m.at(3).toDouble(), m.at(4).toDouble(), m.at(5).toDouble(),
                      m.at(6).toDouble(), m.at(7).toDouble(), m.at(8).toDouble());
}

} // namespace drift::appdetail
namespace {

// The transform clips over `clip` on `trackIndex`, innermost layer first: on each covering layer,
// the clip live at the playhead, else the first one overlapping `clip`.
QVariantList transformParentsForClip(const drift::Project &project, int trackIndex,
                                     const drift::Clip &clip, drift::TimeUs playheadUs)
{
    QVariantList out;
    const QList<int> layers = drift::transformLayersCovering(project.tracks(), trackIndex);
    for (auto it = layers.crbegin(); it != layers.crend(); ++it) {
        const drift::Track &layer = project.tracks().at(*it);
        int best = -1;
        bool atPlayhead = false;
        for (int c = 0; c < layer.clips.size(); ++c) {
            const drift::Clip &candidate = layer.clips.at(c);
            if (candidate.containsTime(playheadUs) && clip.containsTime(playheadUs)) {
                best = c;
                atPlayhead = true;
                break;
            }
            if (best < 0 && candidate.timelineStart < clip.timelineEnd()
                && candidate.timelineEnd() > clip.timelineStart)
                best = c;
        }
        if (best < 0)
            continue;
        out.append(QVariantMap{{QStringLiteral("track"), *it},
                               {QStringLiteral("clip"), best},
                               {QStringLiteral("name"), layer.clips.at(best).name},
                               {QStringLiteral("atPlayhead"), atPlayhead}});
    }
    return out;
}

} // namespace

QVariantMap AppController::clipAt(int trackIndex, int clipIndex) const
{
    const QList<drift::Track> &tracks = m_project.tracks();
    if (trackIndex < 0 || trackIndex >= tracks.size())
        return {};
    if (clipIndex < 0 || clipIndex >= tracks[trackIndex].clips.size())
        return {};

    // The single-clip form resolves its own hosts. Unlike tracks(), this runs once, so the two
    // scans cost nothing worth indexing around — which is also why the face source is looked up
    // here rather than in tracks(), where finding a clip by id per adjustment would be quadratic.
    const drift::ClipRef source = sourceClipRef(trackIndex, clipIndex);
    const bool redirected = source.trackIndex >= 0
                            && (source.trackIndex != trackIndex || source.clipIndex != clipIndex);
    QVariantMap map =
        clipToMap(tracks[trackIndex].clips.at(clipIndex),
                  effectHostClip(trackIndex, clipIndex, drift::AdjustmentKind::VideoEffects),
                  effectHostClip(trackIndex, clipIndex, drift::AdjustmentKind::AudioEffects),
                  effectHostClip(trackIndex, clipIndex, drift::AdjustmentKind::Mask),
                  redirected ? &tracks.at(source.trackIndex).clips.at(source.clipIndex) : nullptr);
    map.insert(QStringLiteral("transformParents"),
               transformParentsForClip(m_project, trackIndex, tracks.at(trackIndex).clips.at(clipIndex),
                                       m_playheadUs));
    return map;
}

QVariantMap AppController::activeVideoClipAtPlayhead() const
{
    QVariantMap result;
    for (const drift::Track &track : m_project.tracks()) {
        if (track.type != drift::TrackType::Video || track.hidden)
            continue;

        for (const drift::Clip &clip : track.clips) {
            if (clip.containsTime(m_playheadUs))
                result = clipToMap(clip);
        }
    }
    return result;
}

QVariantMap AppController::activeAudioClipAtPlayhead() const
{
    QVariantMap result;
    for (const drift::Track &track : m_project.tracks()) {
        if (track.type != drift::TrackType::Audio || track.muted || track.hidden)
            continue;

        for (const drift::Clip &clip : track.clips) {
            if (clip.containsTime(m_playheadUs))
                result = clipToMap(clip);
        }
    }
    return result;
}

double AppController::sourceTimeForClip(const QVariantMap &clip) const
{
    if (clip.isEmpty())
        return 0.0;

    const double start = clip.value(QStringLiteral("start")).toDouble();
    const double inPoint = clip.value(QStringLiteral("inPoint")).toDouble();
    const double outPoint = clip.value(QStringLiteral("outPoint")).toDouble();
    const double speed = clip.value(QStringLiteral("speed"), 1.0).toDouble();
    const double effectiveSpeed = speed <= 0.0 ? 1.0 : speed;
    const double offset = (playheadSeconds() - start) * effectiveSpeed;
    if (clip.value(QStringLiteral("reverse")).toBool())
        return outPoint - offset;
    return inPoint + offset;
}

double AppController::sourceTimeAtPlayheadForClip(int trackIndex, int clipIndex) const
{
    if (!isValidClipIndex(trackIndex, clipIndex))
        return 0.0;
    const drift::Clip &clip = m_project.tracks()[trackIndex].clips[clipIndex];
    const drift::TimeUs frameUs = 1000000 / qMax(1, m_project.fps());
    const drift::TimeUs lastUs = qMax(clip.srcIn, clip.srcOut - frameUs);
    return drift::usToSeconds(qBound(clip.srcIn, clip.timelineToSourceUs(m_playheadUs), lastUs));
}

double AppController::sourceTimeAtPlayhead() const
{
    return sourceTimeForClip(activeVideoClipAtPlayhead());
}

void AppController::pushProjectEdit(const drift::Project &before, const QString &text)
{
    // Before the snapshot, not after: the structural invariants have to be part of the recorded
    // state, or a redo would restore a project with effects still sitting on their clips and the
    // representation would only settle on the next unrelated edit.
    //
    // This is also where a track created by this edit gets its id — nested lanes address their
    // parent by it, and the dozen places that append a track all leave it empty.
    normalizeProjectStructure(&before);
    syncCompositeAssetDurations();
    // Belt and braces for the tracks cache: normalizeProjectStructure() rewrites the project, and
    // every edit in the app reaches this function or finishEdit(). One bool write buys immunity
    // from a future mutation path that forgets to call notifyTracksChanged().
    m_tracksCacheValid = false;
    m_durationCacheValid = false;
    if (m_mcpController->undoSuspended())
        return;
    {
        const QScopedValueRollback quietPush(m_quietUndoPush, true);
        m_undoStack.push(new drift::ProjectSnapshotCommand(&m_project, before, m_project, text));
    }
    // Normally finishEdit settles it straight after; this catches an edit that never calls it.
    if (m_undoRefreshOwed)
        QTimer::singleShot(0, this, &AppController::settleUndoRefresh);
}

void AppController::settleUndoRefresh()
{
    if (!m_undoRefreshOwed)
        return;
    m_undoRefreshOwed = false;
    normalizeSelection();
    notifyTracksChanged();
    notifySelectionChanged();
}

void AppController::finishEdit(const QString &message)
{
    syncOverlapTransitions(m_project);

    // Catches edit paths that bypass pushProjectEdit (preview drags, MCP batches with undo
    // suspended). Idempotent, and pushProjectEdit has normally already done the work.
    normalizeProjectStructure();

    // Pinned adjustments follow their clip from here rather than from each of the dozens of
    // paths that can move one, so a drag, trim, split, ripple delete or multicam retarget all
    // keep the link true for free.
    syncLinkedAdjustments(m_project);
    normalizeSelection();
    if (m_selectedTransitionTrack >= 0) {
        const QVariantMap selected = selectedTransitionData();
        if (selected.isEmpty())
            clearTransitionSelection();
    }
    // During playback the engine clock owns the playhead. Seeking here would
    // PlaybackClock::reset() and (historically) stop the clock while audio kept
    // pulling — freezing A/V at one spot after drops like adding an effect.
    // Audio only: the tracksChanged below schedules the one composite. A full setPlayheadUs here
    // dispatched its own, then the edit's invalidation made the scheduled one a second render.
    if (!m_playback.isPlaying())
        m_playback.resyncAudioAt(m_playheadUs);
    // Underlying audio may have moved; force the subtitle-lane waveform to recompute.
    m_subtitleWaveformCache.clear();
    // Beats are expensive and explicitly requested, so they are dropped only when the mix
    // itself changed — not on every edit. Keyframing is the whole point of having the grid
    // up, and it runs through here too.
    if (!m_beatAnalysis.isEmpty() && audioLayoutFingerprint() != m_beatAudioFingerprint)
        clearBeatAnalysis();
    m_undoRefreshOwed = false;
    notifyTracksChanged();
    if (!m_quietSelection)
        notifySelectionChanged();
    // tracksChanged already carries selectedClipDataChanged (see the constructor); only a batch
    // holding tracksChanged back leaves it unsent.
    if (m_tracksBatchDepth > 0)
        emit selectedClipDataChanged();
    // Routine edits used to announce themselves here ("Clip moved", "Split
    // clip", ...), which surfaced as a toast for every drag and cut. The
    // timeline already shows the result and the label lives on in the undo
    // stack, so an edit now only clears a stale warning from an earlier attempt.
    Q_UNUSED(message)
    setLastMessage(QString());
    m_mcpController->bumpEditRevision();
}

void AppController::applyRippleShift(drift::Track &track, int fromClipIndex, drift::TimeUs delta)
{
    if (!m_rippleEnabled || delta == 0)
        return;

    for (int i = fromClipIndex + 1; i < track.clips.size(); ++i)
        track.clips[i].timelineStart = qMax<drift::TimeUs>(0, track.clips[i].timelineStart + delta);
}

bool AppController::compositeAssetPlaceable(const QVariantMap &asset) const
{
    // One level of nesting: a composite goes on the main timeline only.
    return asset.value(QStringLiteral("kind")).toString() != QLatin1String("composite")
           || m_project.activeSequenceId().isEmpty();
}

void AppController::addClipFromAsset(int assetIndex)
{
    const QVariantMap asset = m_assetLibrary ? m_assetLibrary->assetAt(assetIndex) : QVariantMap{};
    if (asset.isEmpty())
        return;
    if (!compositeAssetPlaceable(asset))
        return;

    const QString kind = asset.value(QStringLiteral("kind")).toString();
    const drift::ClipType clipType = drift::clipTypeFromString(kind);
    const drift::Project before = m_project;
    int trackIndex = drift::defaultTrackForClipType(m_project, clipType);
    if (trackIndex < 0)
        trackIndex = drift::ensureTrackForClipType(m_project, clipType, false);

    m_assetLibrary->ensureMedia(assetIndex);
    const QString thumbnailPath = m_assetLibrary->thumbnailAt(assetIndex);
    const QString filmstripPath = m_assetLibrary->filmstripAt(assetIndex);

    drift::Track &track = m_project.tracks()[trackIndex];
    if (!track.allowsClipType(clipType))
        return;

    const drift::TimeUs duration = clipDurationForAssetIndex(assetIndex);
    // A bin-preview trim shortens what actually lands on the timeline (applyAssetLayout below
    // applies it), so collision/gap placement has to size itself against that, not the asset's
    // full duration, or a trimmed clip gets pushed out past a neighbour it would easily fit next
    // to.
    const drift::TimeUs placementDuration = trimmedClipDurationUs(asset, duration);
    const drift::TimeUs start = drift::resolveClipStart(m_project, track, -1, m_playheadUs,
                                                        placementDuration, m_snapEnabled, m_playheadUs);

    drift::Clip clip;
    clip.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    clip.assetId = m_assetLibrary->assetIdAt(assetIndex);
    clip.type = clipType;
    clip.name = asset.value(QStringLiteral("name")).toString();
    clip.path = asset.value(QStringLiteral("path")).toString();
    attachAssetSource(clip);
    clip.sequenceId = asset.value(QStringLiteral("sequenceId")).toString();
    clip.thumbnailPath = thumbnailPath;
    clip.filmstripPath = filmstripPath;
    clip.timelineStart = start;
    clip.timelineDuration = placementDuration;
    clip.srcIn = 0;
    clip.srcOut = duration;
    applyAssetLayout(clip, asset, m_project.width(), m_project.height());

    track.clips.append(clip);
    // Read the index out before the edit is pushed. ProjectSnapshotCommand::redo() assigns over
    // m_project, and TrackList::operator= releases the buffer `track` points into before it
    // detaches, so the reference dangles from there on. Every add-clip path below does the same,
    // and addClipsFromAssets has always captured its indices this way.
    const int newClipIndex = track.clips.size() - 1;
    pushProjectEdit(before, tr("Clip added"));
    finishEdit(tr("Clip added"));
    selectClip(trackIndex, newClipIndex);
}

void AppController::addClipsFromAssets(const QStringList &assetIds)
{
    if (!m_assetLibrary || assetIds.isEmpty())
        return;

    const drift::Project before = m_project;
    // Advances by each clip's duration as it's placed, so the batch reads as one sequence
    // rather than every clip landing at the playhead on top of each other. Shared across
    // tracks: two clips of the same kind stay back to back; a kind change (video, then audio)
    // still keeps its place in the sequence rather than resetting to the playhead.
    drift::TimeUs cursor = m_playheadUs;
    int lastTrackIndex = -1;
    int lastClipIndex = -1;

    for (const QString &id : assetIds) {
        const int assetIndex = m_assetLibrary->indexOfId(id);
        if (assetIndex < 0)
            continue;

        const QVariantMap asset = m_assetLibrary->assetAt(assetIndex);
        if (asset.isEmpty() || !compositeAssetPlaceable(asset))
            continue;

        const drift::ClipType clipType = drift::clipTypeFromString(asset.value(QStringLiteral("kind")).toString());
        int trackIndex = drift::defaultTrackForClipType(m_project, clipType);
        if (trackIndex < 0)
            trackIndex = drift::ensureTrackForClipType(m_project, clipType, false);
        if (trackIndex < 0)
            continue;

        drift::Track &track = m_project.tracks()[trackIndex];
        if (!track.allowsClipType(clipType))
            continue;

        m_assetLibrary->ensureMedia(assetIndex);
        const QString thumbnailPath = m_assetLibrary->thumbnailAt(assetIndex);
        const QString filmstripPath = m_assetLibrary->filmstripAt(assetIndex);
        const drift::TimeUs duration = clipDurationForAssetIndex(assetIndex);
        // See addClipFromAsset: placement must size against the trimmed length, not the asset's
        // full duration, or a trimmed clip gets pushed past a neighbour it would fit next to.
        const drift::TimeUs placementDuration = trimmedClipDurationUs(asset, duration);
        const drift::TimeUs start = drift::resolveClipStart(m_project, track, -1, cursor,
                                                            placementDuration, m_snapEnabled, cursor);

        drift::Clip clip;
        clip.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        clip.assetId = m_assetLibrary->assetIdAt(assetIndex);
        clip.type = clipType;
        clip.name = asset.value(QStringLiteral("name")).toString();
        clip.path = asset.value(QStringLiteral("path")).toString();
        attachAssetSource(clip);
        clip.sequenceId = asset.value(QStringLiteral("sequenceId")).toString();
        clip.thumbnailPath = thumbnailPath;
        clip.filmstripPath = filmstripPath;
        clip.timelineStart = start;
        clip.timelineDuration = placementDuration;
        clip.srcIn = 0;
        clip.srcOut = duration;
        applyAssetLayout(clip, asset, m_project.width(), m_project.height());

        track.clips.append(clip);
        lastTrackIndex = trackIndex;
        lastClipIndex = track.clips.size() - 1;
        // Not `duration`: applyAssetLayout above may have shortened clip.timelineDuration to a
        // bin-preview trim, and the cursor has to advance by what actually got placed or the
        // next clip lands after a gap the size of the trimmed-off tail.
        cursor = start + clip.timelineDuration;
    }

    if (lastTrackIndex < 0)
        return;

    pushProjectEdit(before, tr("Clips added"));
    finishEdit(tr("Clips added"));
    selectClip(lastTrackIndex, lastClipIndex);
}

bool AppController::trackAcceptsAsset(int trackIndex, int assetIndex) const
{
    if (!m_assetLibrary)
        return false;

    const QVariantMap asset = m_assetLibrary->assetAt(assetIndex);
    if (asset.isEmpty())
        return false;

    return trackAcceptsKind(trackIndex, asset.value(QStringLiteral("kind")).toString());
}

QString AppController::trackTypeForAsset(int assetIndex) const
{
    if (!m_assetLibrary)
        return QStringLiteral("video");

    const QVariantMap asset = m_assetLibrary->assetAt(assetIndex);
    if (asset.isEmpty())
        return QStringLiteral("video");

    return trackTypeForKind(asset.value(QStringLiteral("kind")).toString());
}

bool AppController::trackAcceptsKind(int trackIndex, const QString &mediaKind) const
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return false;

    const drift::ClipType clipType = drift::clipTypeFromString(mediaKind);
    if (clipType == drift::ClipType::Composite && !m_project.activeSequenceId().isEmpty())
        return false;
    return m_project.tracks().at(trackIndex).allowsClipType(clipType);
}

QString AppController::trackTypeForKind(const QString &mediaKind) const
{
    const drift::ClipType clipType = drift::clipTypeFromString(mediaKind);
    return drift::trackTypeToString(drift::trackTypeForClipType(clipType));
}

void AppController::addClipFromAssetOnNewTrack(int assetIndex, double atSeconds)
{
    addClipFromAssetOnNewTrackAt(assetIndex, 0, atSeconds);
}

void AppController::addClipFromAssetOnNewTrackAt(int assetIndex, int insertIndex, double atSeconds)
{
    if (!m_assetLibrary)
        return;

    const QVariantMap asset = m_assetLibrary->assetAt(assetIndex);
    if (asset.isEmpty())
        return;
    if (!compositeAssetPlaceable(asset))
        return;

    const drift::ClipType clipType = drift::clipTypeFromString(asset.value(QStringLiteral("kind")).toString());
    const drift::Project before = m_project;
    const int trackIndex = drift::insertTrackAboveForClipType(m_project, insertIndex, clipType);

    m_assetLibrary->ensureMedia(assetIndex);
    const QString thumbnailPath = m_assetLibrary->thumbnailAt(assetIndex);
    const QString filmstripPath = m_assetLibrary->filmstripAt(assetIndex);

    drift::Track &track = m_project.tracks()[trackIndex];
    const drift::TimeUs duration = clipDurationForAssetIndex(assetIndex);
    // See addClipFromAsset: placement must size against the trimmed length, not the asset's full
    // duration.
    const drift::TimeUs placementDuration = trimmedClipDurationUs(asset, duration);
    const drift::TimeUs start = drift::resolveClipStart(m_project, track, -1, drift::secondsToUs(atSeconds),
                                                        placementDuration, m_snapEnabled, m_playheadUs);

    drift::Clip clip;
    clip.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    clip.assetId = m_assetLibrary->assetIdAt(assetIndex);
    clip.type = clipType;
    clip.name = asset.value(QStringLiteral("name")).toString();
    clip.path = asset.value(QStringLiteral("path")).toString();
    attachAssetSource(clip);
    clip.sequenceId = asset.value(QStringLiteral("sequenceId")).toString();
    clip.thumbnailPath = thumbnailPath;
    clip.filmstripPath = filmstripPath;
    clip.timelineStart = start;
    clip.timelineDuration = placementDuration;
    clip.srcIn = 0;
    clip.srcOut = duration;
    applyAssetLayout(clip, asset, m_project.width(), m_project.height());

    track.clips.append(clip);
    const int newClipIndex = track.clips.size() - 1;
    pushProjectEdit(before, tr("Clip added on new track"));
    finishEdit(tr("Clip added on new track"));
    selectClip(trackIndex, newClipIndex);
}

void AppController::addClipFromAssetAt(int assetIndex, int trackIndex, double atSeconds)
{
    if (!m_assetLibrary || trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    const QVariantMap asset = m_assetLibrary->assetAt(assetIndex);
    if (asset.isEmpty())
        return;
    if (!compositeAssetPlaceable(asset))
        return;

    m_assetLibrary->ensureMedia(assetIndex);
    const QString thumbnailPath = m_assetLibrary->thumbnailAt(assetIndex);
    const QString filmstripPath = m_assetLibrary->filmstripAt(assetIndex);
    const QString kind = asset.value(QStringLiteral("kind")).toString();
    const drift::ClipType clipType = drift::clipTypeFromString(kind);

    if (!m_project.tracks().at(trackIndex).allowsClipType(clipType))
        return;

    // Snapshot before a non-const Track&. QList implicit sharing would otherwise let
    // the append below mutate `before` as well, and Ctrl+Z would be a no-op.
    const drift::Project before = m_project;
    drift::Track &track = m_project.tracks()[trackIndex];
    const drift::TimeUs duration = clipDurationForAssetIndex(assetIndex);
    // See addClipFromAsset: placement must size against the trimmed length, not the asset's full
    // duration.
    const drift::TimeUs placementDuration = trimmedClipDurationUs(asset, duration);
    const drift::TimeUs start = drift::resolveClipStart(m_project, track, -1, drift::secondsToUs(atSeconds),
                                                        placementDuration, m_snapEnabled, m_playheadUs);

    drift::Clip clip;
    clip.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    clip.assetId = m_assetLibrary->assetIdAt(assetIndex);
    clip.type = clipType;
    clip.name = asset.value(QStringLiteral("name")).toString();
    clip.path = asset.value(QStringLiteral("path")).toString();
    attachAssetSource(clip);
    clip.sequenceId = asset.value(QStringLiteral("sequenceId")).toString();
    clip.thumbnailPath = thumbnailPath;
    clip.filmstripPath = filmstripPath;
    clip.timelineStart = start;
    clip.timelineDuration = placementDuration;
    clip.srcIn = 0;
    clip.srcOut = duration;
    applyAssetLayout(clip, asset, m_project.width(), m_project.height());

    track.clips.append(clip);
    const int newClipIndex = track.clips.size() - 1;
    pushProjectEdit(before, tr("Clip added"));
    finishEdit(tr("Clip added"));
    selectClip(trackIndex, newClipIndex);
}

AppController::GeneratedAudioImport AppController::importGeneratedAudio(
    const QString &path, const QJsonObject &generator, bool place, double atSeconds, int track)
{
    GeneratedAudioImport result;
    if (!m_assetLibrary) {
        result.errorCode = QStringLiteral("not_found");
        result.error = QStringLiteral("No media bin");
        return result;
    }
    const QStringList ids = m_assetLibrary->importLocalPaths({path});
    if (ids.isEmpty()) {
        result.errorCode = QStringLiteral("import_failed");
        result.error = QStringLiteral("Could not import %1").arg(path);
        return result;
    }
    const QString assetId = ids.first();
    {
        // Placing needs the probed duration.
        QEventLoop loop;
        QTimer timeout;
        timeout.setSingleShot(true);
        timeout.start(15000);
        connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
        connect(m_assetLibrary, &AssetLibrary::assetMetadataChanged, &loop, &QEventLoop::quit);
        while (timeout.isActive() && m_assetLibrary->isImportPending(assetId))
            loop.exec();
    }
    if (drift::MediaAsset *asset = m_project.asset(assetId)) {
        asset->generator = generator;
        projectFile()->setDirty(true);
    }
    result.ok = true;
    result.assetId = assetId;
    if (const drift::MediaAsset *asset = m_project.asset(assetId))
        result.durationSeconds = drift::usToSeconds(asset->durationUs);

    if (!place)
        return result;
    const int assetIndex = m_assetLibrary->indexOfPath(path);
    if (assetIndex < 0)
        return result;
    QSet<QString> before;
    for (const drift::Track &t : m_project.tracks())
        for (const drift::Clip &c : t.clips)
            before.insert(c.id);
    if (track < 0) {
        // The first audio lane with room for the whole clip at `atSeconds`; otherwise a new one.
        const drift::TimeUs atUs = drift::secondsToUs(atSeconds);
        const drift::MediaAsset *asset = m_project.asset(assetId);
        const drift::TimeUs endUs = atUs + qMax<drift::TimeUs>(1, asset ? asset->durationUs : 1);
        for (int t = 0; t < m_project.tracks().size() && track < 0; ++t) {
            if (m_project.tracks().at(t).type != drift::TrackType::Audio || !trackAcceptsAsset(t, assetIndex))
                continue;
            bool free = true;
            for (const drift::Clip &c : m_project.tracks().at(t).clips)
                free = free && (c.timelineEnd() <= atUs || c.timelineStart >= endUs);
            if (free)
                track = t;
        }
    }
    if (track >= 0)
        addClipFromAssetAt(assetIndex, track, atSeconds);
    else
        addClipFromAssetOnNewTrackAt(assetIndex, m_project.tracks().size(), atSeconds);
    for (const drift::Track &t : m_project.tracks())
        for (const drift::Clip &c : t.clips)
            if (!before.contains(c.id))
                result.clipId = c.id;
    return result;
}

int AppController::placeGeneratedAudioOnNewTracks(const QList<GeneratedAudioPlacement> &items,
                                                  const QString &undoText)
{
    if (!m_assetLibrary || items.isEmpty())
        return 0;
    const drift::Project before = m_project;
    struct Lane
    {
        int track;
        drift::TimeUs endUs;
    };
    QList<Lane> lanes;
    int placed = 0;
    int lastTrack = -1;
    int lastClip = -1;
    for (const GeneratedAudioPlacement &item : items) {
        const int assetIndex = m_assetLibrary->indexOfPath(item.path);
        if (assetIndex < 0)
            continue;
        const QVariantMap asset = m_assetLibrary->assetAt(assetIndex);
        const drift::TimeUs duration = clipDurationForAssetIndex(assetIndex);
        const drift::TimeUs placementDuration = trimmedClipDurationUs(asset, duration);
        const drift::TimeUs start = qMax<drift::TimeUs>(0, drift::secondsToUs(item.atSeconds));
        int lane = -1;
        for (int i = 0; i < lanes.size(); ++i) {
            if (lanes.at(i).endUs <= start) {
                lane = i;
                break;
            }
        }
        if (lane < 0) {
            const int track = drift::insertTrackAboveForClipType(m_project, m_project.tracks().size(),
                                                                 drift::ClipType::Audio);
            lanes.append({track, 0});
            lane = lanes.size() - 1;
        }
        m_assetLibrary->ensureMedia(assetIndex);
        drift::Clip clip;
        clip.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        clip.assetId = m_assetLibrary->assetIdAt(assetIndex);
        clip.type = drift::ClipType::Audio;
        clip.name = asset.value(QStringLiteral("name")).toString();
        clip.path = asset.value(QStringLiteral("path")).toString();
        attachAssetSource(clip);
        clip.thumbnailPath = m_assetLibrary->thumbnailAt(assetIndex);
        clip.filmstripPath = m_assetLibrary->filmstripAt(assetIndex);
        clip.timelineStart = start;
        clip.timelineDuration = placementDuration;
        clip.srcIn = 0;
        clip.srcOut = duration;
        applyAssetLayout(clip, asset, m_project.width(), m_project.height());
        drift::Track &track = m_project.tracks()[lanes.at(lane).track];
        track.clips.append(clip);
        lanes[lane].endUs = start + placementDuration;
        lastTrack = lanes.at(lane).track;
        lastClip = track.clips.size() - 1;
        ++placed;
    }
    if (placed == 0)
        return 0;
    pushProjectEdit(before, undoText);
    finishEdit(undoText);
    selectClip(lastTrack, lastClip);
    return placed;
}

void AppController::selectClip(int trackIndex, int clipIndex)
{
    if (!isValidClipIndex(trackIndex, clipIndex))
        return;

    m_selectedTrack = trackIndex;
    m_selectedClip = clipIndex;
    m_selection = selectionWithLinkedPartners(m_project, trackIndex, clipIndex);
    m_selectedTransitionTrack = -1;
    m_selectedTransitionLeftClip = -1;
    notifySelectionChanged();
    emit selectedTransitionDataChanged();
}

void AppController::selectClipUnderPlayhead()
{
    const QList<drift::Track> &tracks = m_project.tracks();
    // The first clip on `trackIndex` spanning the playhead, or -1.
    auto clipUnderPlayhead = [&](int trackIndex) {
        if (trackIndex < 0 || trackIndex >= tracks.size())
            return -1;
        const QList<drift::Clip> &clips = tracks.at(trackIndex).clips;
        for (int i = 0; i < clips.size(); ++i) {
            if (clips.at(i).containsTime(m_playheadUs))
                return i;
        }
        return -1;
    };

    if (m_selectedTrack >= 0) {
        const int clipIndex = clipUnderPlayhead(m_selectedTrack);
        if (clipIndex >= 0) {
            selectClip(m_selectedTrack, clipIndex);
            return;
        }
    }
    for (drift::TrackType type : {drift::TrackType::Video, drift::TrackType::Audio}) {
        for (int trackIndex = 0; trackIndex < tracks.size(); ++trackIndex) {
            if (tracks.at(trackIndex).type != type || tracks.at(trackIndex).hidden)
                continue;
            const int clipIndex = clipUnderPlayhead(trackIndex);
            if (clipIndex >= 0) {
                selectClip(trackIndex, clipIndex);
                return;
            }
        }
    }
}

QVariantList AppController::locateClip(const QString &clipId) const
{
    const QList<drift::Track> &tracks = m_project.tracks();
    for (int t = 0; t < tracks.size(); ++t) {
        const QList<drift::Clip> &clips = tracks.at(t).clips;
        for (int c = 0; c < clips.size(); ++c) {
            if (clips.at(c).id == clipId)
                return {t, c};
        }
    }
    return {};
}

void AppController::addToSelection(int trackIndex, int clipIndex)
{
    if (!isValidClipIndex(trackIndex, clipIndex))
        return;

    for (const QPair<int, int> &pair : selectionWithLinkedPartners(m_project, trackIndex, clipIndex)) {
        if (!m_selection.contains(pair))
            m_selection.append(pair);
    }
    m_selectedTrack = trackIndex;
    m_selectedClip = clipIndex;
    notifySelectionChanged();
}

void AppController::setSelection(const QVariantList &pairs)
{
    QList<QPair<int, int>> next;
    for (const QVariant &value : pairs) {
        const QVariantMap map = value.toMap();
        const int trackIndex = map.value(QStringLiteral("track")).toInt();
        const int clipIndex = map.value(QStringLiteral("clip")).toInt();
        // Match click-select: bring linked A/V partners along.
        for (const QPair<int, int> &pair : selectionWithLinkedPartners(m_project, trackIndex, clipIndex)) {
            if (!next.contains(pair))
                next.append(pair);
        }
    }
    m_selection = next;
    m_selectedTransitionTrack = -1;
    m_selectedTransitionLeftClip = -1;
    if (m_selection.isEmpty()) {
        m_selectedTrack = -1;
        m_selectedClip = -1;
    } else {
        m_selectedTrack = m_selection.constLast().first;
        m_selectedClip = m_selection.constLast().second;
    }
    notifySelectionChanged();
    emit selectedTransitionDataChanged();
}

void AppController::selectAllClips()
{
    QVariantList pairs;
    for (int t = 0; t < m_project.tracks().size(); ++t) {
        const int clipCount = m_project.tracks().at(t).clips.size();
        for (int c = 0; c < clipCount; ++c) {
            pairs.append(QVariantMap{
                {QStringLiteral("track"), t},
                {QStringLiteral("clip"), c},
            });
        }
    }
    setSelection(pairs);
}

void AppController::clearSelection()
{
    if (m_selectedTrack < 0 && m_selectedClip < 0 && m_selection.isEmpty() && m_selectedTransitionTrack < 0)
        return;

    m_selectedTrack = -1;
    m_selectedClip = -1;
    m_selection.clear();
    m_selectedTransitionTrack = -1;
    m_selectedTransitionLeftClip = -1;
    notifySelectionChanged();
    emit selectedTransitionDataChanged();
}

void AppController::deleteSelectedClip()
{
    deleteSelection(/*withPinnedAdjustments=*/false);
}

void AppController::deleteSelectedClipWithEffects()
{
    deleteSelection(/*withPinnedAdjustments=*/true);
}

bool AppController::selectionHasPinnedAdjustments() const
{
    QList<QPair<int, int>> pairs = m_selection;
    if (pairs.isEmpty() && isValidClipIndex(m_selectedTrack, m_selectedClip))
        pairs = {qMakePair(m_selectedTrack, m_selectedClip)};
    if (pairs.isEmpty())
        return false;
    expandSelectionWithLinkedPartners(m_project, pairs);
    QSet<QString> hostIds;
    for (const QPair<int, int> &pair : std::as_const(pairs)) {
        if (isValidClipIndex(pair.first, pair.second))
            hostIds.insert(m_project.tracks().at(pair.first).clips.at(pair.second).id);
    }
    for (const drift::Track &track : m_project.tracks()) {
        if (!track.isAdjustment())
            continue;
        for (const drift::Clip &clip : track.clips) {
            if (!clip.linkedClipId.isEmpty() && hostIds.contains(clip.linkedClipId))
                return true;
        }
    }
    return false;
}

void AppController::deleteSelection(bool withPinnedAdjustments)
{
    if (m_selection.isEmpty() && m_selectedTrack >= 0 && m_selectedClip >= 0)
        m_selection = {qMakePair(m_selectedTrack, m_selectedClip)};
    if (m_selection.isEmpty())
        return;

    const drift::Project before = m_project;
    QList<QPair<int, int>> pairs = m_selection;
    expandSelectionWithLinkedPartners(m_project, pairs);
    // Pinned adjustments are removed without rippling their lane: the lane follows its parent
    // track, and syncLinkedAdjustments re-spans the remaining ones onto their clips afterwards.
    QSet<QPair<int, int>> pinnedAdjustments;
    if (withPinnedAdjustments) {
        QSet<QString> hostIds;
        for (const QPair<int, int> &pair : std::as_const(pairs)) {
            if (isValidClipIndex(pair.first, pair.second))
                hostIds.insert(m_project.tracks().at(pair.first).clips.at(pair.second).id);
        }
        for (int t = 0; t < m_project.tracks().size(); ++t) {
            const drift::Track &track = m_project.tracks().at(t);
            if (!track.isAdjustment())
                continue;
            for (int c = 0; c < track.clips.size(); ++c) {
                const drift::Clip &clip = track.clips.at(c);
                if (clip.linkedClipId.isEmpty() || !hostIds.contains(clip.linkedClipId))
                    continue;
                // Also when the user selected it alongside its host: it is still removed as a
                // pinned adjustment, not as a free clip that would ripple its lane.
                pinnedAdjustments.insert(qMakePair(t, c));
                if (!pairs.contains(qMakePair(t, c)))
                    pairs.append(qMakePair(t, c));
            }
        }
    }
    QSet<QString> removedClipIds;
    struct RemovedRange { int track; drift::TimeUs start; drift::TimeUs duration; };
    QList<RemovedRange> removedRanges;
    std::sort(pairs.begin(), pairs.end(), [](const QPair<int, int> &a, const QPair<int, int> &b) {
        if (a.first != b.first)
            return a.first > b.first;
        return a.second > b.second;
    });
    for (const QPair<int, int> &pair : pairs) {
        if (!isValidClipIndex(pair.first, pair.second))
            continue;
        const drift::Clip &clip = m_project.tracks().at(pair.first).clips.at(pair.second);
        removedClipIds.insert(clip.id);
        if (!pinnedAdjustments.contains(pair))
            removedRanges.append({pair.first, clip.timelineStart, clip.timelineDuration});
        m_project.tracks()[pair.first].clips.removeAt(pair.second);
    }
    if (m_rippleEnabled) {
        // Latest first, so each range is closed while the clips before it are still in place.
        std::sort(removedRanges.begin(), removedRanges.end(),
                  [](const RemovedRange &a, const RemovedRange &b) { return a.start > b.start; });
        for (const RemovedRange &range : std::as_const(removedRanges))
            rippleTracksFrom(m_project, {range.track}, range.start + range.duration, -range.duration, {});
    }
    for (drift::Track &track : m_project.tracks()) {
        for (int i = track.transitions.size() - 1; i >= 0; --i) {
            const drift::Transition &transition = track.transitions.at(i);
            if (removedClipIds.contains(transition.fromClipId) || removedClipIds.contains(transition.toClipId))
                track.transitions.removeAt(i);
        }
    }
    // A nested lane this delete emptied existed only to hold those effects; leaving it would
    // add a dead row under the parent track every time a graded clip is deleted.
    if (!pinnedAdjustments.isEmpty()) {
        QSet<QString> touchedLaneIds;
        bool removedVideo = false;
        bool removedAudio = false;
        for (const QPair<int, int> &pair : std::as_const(pinnedAdjustments)) {
            touchedLaneIds.insert(before.tracks().at(pair.first).id);
            const drift::AdjustmentKind kind = before.tracks().at(pair.first).clips.at(pair.second).adjustmentKind;
            removedVideo = removedVideo || kind == drift::AdjustmentKind::VideoEffects;
            removedAudio = removedAudio || kind == drift::AdjustmentKind::AudioEffects;
        }
        // The graph's hidden entries address effects by stack index; with the stack gone they
        // would hide parameters of whatever effect lands in that slot next.
        dropKeyframeGraphEffectProperties(removedVideo, removedAudio);
        for (int t = m_project.tracks().size() - 1; t >= 0; --t) {
            const drift::Track &track = m_project.tracks().at(t);
            if (track.isAdjustmentLane() && track.clips.isEmpty() && !track.id.isEmpty()
                && touchedLaneIds.contains(track.id))
                m_project.tracks().removeAt(t);
        }
    }
    pushProjectEdit(before, tr("Clip deleted"));
    clearSelection();
    finishEdit(tr("Clip deleted"));
}

// Overlaps the edit just applied created: physically overlapping pairs with no transition yet,
// that did not already overlap in `before`. Each gets the same crossfade the blend-zone click adds.
void AppController::autoCrossfadeNewOverlaps(const drift::Project &before)
{
    if (!m_preferences->autoCrossfadeOverlaps())
        return;

    QList<QPair<int, int>> created;
    for (int t = 0; t < m_project.tracks().size(); ++t) {
        const drift::Track &track = m_project.tracks().at(t);
        if (!trackAllowsTransitions(track.type))
            continue;
        // By id: an edit that mints or reorders tracks leaves the two lists out of step by index.
        int oldTrackIndex = before.trackIndexById(track.id);
        if (oldTrackIndex < 0 && before.tracks().size() == m_project.tracks().size())
            oldTrackIndex = t;
        const drift::Track *oldTrack = oldTrackIndex >= 0 ? &before.tracks().at(oldTrackIndex) : nullptr;
        for (int left = 0; left < track.clips.size(); ++left) {
            const int right = findTransitionPartnerIndex(track, left);
            if (right < 0)
                continue;
            const drift::Clip &fromClip = track.clips.at(left);
            const drift::Clip &toClip = track.clips.at(right);
            if (!drift::clipsPhysicallyOverlap(fromClip, toClip))
                continue;
            const drift::Clip *oldFrom = oldTrack ? drift::clipById(*oldTrack, fromClip.id) : nullptr;
            const drift::Clip *oldTo = oldTrack ? drift::clipById(*oldTrack, toClip.id) : nullptr;
            if (oldFrom && oldTo && drift::clipsPhysicallyOverlap(*oldFrom, *oldTo))
                continue;
            const bool hasTransition = std::any_of(track.transitions.cbegin(), track.transitions.cend(),
                                                   [&](const drift::Transition &t) {
                                                       return t.fromClipId == fromClip.id && t.toClipId == toClip.id;
                                                   });
            if (!hasTransition)
                created.append(qMakePair(t, left));
        }
    }
    // addTransition only changes transitions, so the clip indices recorded above stay valid.
    for (const QPair<int, int> &pair : std::as_const(created))
        addTransition(pair.first, pair.second, QStringLiteral("crossfade"), 0.5);
}

void AppController::moveClip(int trackIndex, int clipIndex, double newStart)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    const QPair<int, int> requested(trackIndex, clipIndex);
    QList<QPair<int, int>> targets = m_selection.contains(requested) ? m_selection
                                                                      : QList<QPair<int, int>>{requested};
    // Linked partners ride along by the same delta whether or not they are selected.
    expandSelectionWithLinkedPartners(m_project, targets);
    const drift::Project before = m_project;
    const drift::TimeUs desiredUs = drift::secondsToUs(newStart);
    const drift::TimeUs baseUs = m_project.tracks().at(trackIndex).clips.at(clipIndex).timelineStart;
    const drift::TimeUs delta = desiredUs - baseUs;

    // When moving multiple clips, clamp leftward movement so the earliest clip in the selection
    // stops at 0 and the relative distance between selected clips is preserved.
    drift::TimeUs minGroupStartUs = -1;
    for (const QPair<int, int> &pair : targets) {
        if (isValidClipIndex(pair.first, pair.second)) {
            const drift::TimeUs s = m_project.tracks().at(pair.first).clips.at(pair.second).timelineStart;
            if (minGroupStartUs < 0 || s < minGroupStartUs)
                minGroupStartUs = s;
        }
    }
    drift::TimeUs clampedDelta = delta;
    if (clampedDelta < 0 && minGroupStartUs >= 0 && -clampedDelta > minGroupStartUs) {
        clampedDelta = -minGroupStartUs;
    }

    QSet<QString> movedIds;
    for (const QPair<int, int> &pair : targets) {
        if (!isValidClipIndex(pair.first, pair.second))
            continue;
        drift::Clip &clip = m_project.tracks()[pair.first].clips[pair.second];
        clip.timelineStart = qMax<drift::TimeUs>(0, clip.timelineStart + clampedDelta);
        movedIds.insert(clip.id);
    }
    if (!m_allowClipOverlap) {
        drift::TimeUs maxPushRight = 0;
        for (const QPair<int, int> &pair : targets) {
            if (!isValidClipIndex(pair.first, pair.second))
                continue;
            const drift::Track &targetTrack = m_project.tracks().at(pair.first);
            const drift::Clip &clip = targetTrack.clips.at(pair.second);
            const drift::TimeUs clampedStart = drift::clampClipStartNoOverlap(targetTrack, movedIds, clip.timelineStart,
                                                                              clip.timelineDuration);
            if (clampedStart > clip.timelineStart) {
                const drift::TimeUs push = clampedStart - clip.timelineStart;
                if (push > maxPushRight)
                    maxPushRight = push;
            }
        }
        if (maxPushRight > 0) {
            for (const QPair<int, int> &pair : targets) {
                if (!isValidClipIndex(pair.first, pair.second))
                    continue;
                drift::Clip &clip = m_project.tracks()[pair.first].clips[pair.second];
                clip.timelineStart += maxPushRight;
            }
        }
    }
    for (const QPair<int, int> &pair : targets) {
        if (!isValidClipIndex(pair.first, pair.second))
            continue;
        const drift::Clip &clip = m_project.tracks().at(pair.first).clips.at(pair.second);
        syncLinkedPartnersFrom(m_project, clip, movedIds);
    }
    if (editCreatesCompleteOverlap(before, m_project)) {
        m_project = before;
        setLastMessage(tr("A clip can't completely cover another"), QStringLiteral("warning"));
        return;
    }
    m_undoStack.beginMacro(tr("Clip moved"));
    pushProjectEdit(before, tr("Clip moved"));
    autoCrossfadeNewOverlaps(before);
    m_undoStack.endMacro();
    finishEdit(tr("Clip moved"));
}

void AppController::closeGap(int trackIndex, double gapStartSeconds)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    // Snapshot before taking any non-const reference into m_project: QList is
    // copy-on-write, and a reference grabbed first mutates the buffer the copy
    // still shares, leaving `before` already rippled and undo a no-op.
    const drift::Project before = m_project;
    drift::Track &track = m_project.tracks()[trackIndex];
    const drift::TimeUs gapStartUs = drift::secondsToUs(gapStartSeconds);

    bool foundNext = false;
    drift::TimeUs nextStart = 0;
    for (const drift::Clip &clip : track.clips) {
        if (clip.timelineStart < gapStartUs)
            continue;
        if (!foundNext || clip.timelineStart < nextStart) {
            nextStart = clip.timelineStart;
            foundNext = true;
        }
    }
    if (!foundNext)
        return;

    const drift::TimeUs gapLength = nextStart - gapStartUs;
    if (gapLength <= 0)
        return;

    QSet<QString> movedIds;
    for (drift::Clip &clip : track.clips) {
        if (clip.timelineStart < gapStartUs)
            continue;
        clip.timelineStart -= gapLength;
        movedIds.insert(clip.id);
    }
    for (const drift::Clip &clip : track.clips) {
        if (!movedIds.contains(clip.id))
            continue;
        syncLinkedPartnersFrom(m_project, clip, movedIds);
    }

    pushProjectEdit(before, tr("Close gap"));
    finishEdit(tr("Close gap"));
}

void AppController::splitAtPlayhead()
{
    const drift::Project before = m_project;
    bool splitAny = false;
    QSet<QString> handledLinkIds;
    struct PendingAdjustmentSplit
    {
        int trackIndex;
        QString headId;
        QString tailId;
        drift::TimeUs offset;
    };
    QList<PendingAdjustmentSplit> pendingAdjustmentSplits;

    const QList<drift::Track> &tracksView = m_project.tracks();
    for (int trackIndex = 0; trackIndex < tracksView.size(); ++trackIndex) {
        drift::Track &track = m_project.tracks()[trackIndex];
        for (int clipIndex = 0; clipIndex < track.clips.size(); ++clipIndex) {
            drift::Clip &clip = track.clips[clipIndex];
            if (!clip.containsTime(m_playheadUs))
                continue;
            if (m_playheadUs == clip.timelineStart)
                continue;
            if (!clip.linkId.isEmpty() && handledLinkIds.contains(clip.linkId))
                continue;
            // An adjustment pinned to a host is split by the host below, which is the only place
            // that knows the tail's id. Splitting it here as well would leave two adjustments
            // claiming the same clip.
            if (!clip.linkedClipId.isEmpty())
                continue;

            const drift::TimeUs offset = m_playheadUs - clip.timelineStart;
            drift::Clip tail;
            if (!drift::splitClipAtOffset(clip, tail, offset, m_project.fps()))
                continue;

            tail.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            const QString tailLinkId = drift::assignSplitLinkIds(clip, tail);
            if (!clip.linkId.isEmpty())
                handledLinkIds.insert(clip.linkId);
            const QString headId = clip.id;
            splitLinkedPartnerAt(m_project, clip, m_playheadUs, tailLinkId);
            track.clips.insert(clipIndex + 1, tail);
            pendingAdjustmentSplits.append({trackIndex, headId, tail.id, offset});
            splitAny = true;
            ++clipIndex;
        }
    }

    for (const PendingAdjustmentSplit &pending : pendingAdjustmentSplits) {
        splitLinkedAdjustmentsAt(m_project, pending.trackIndex, pending.headId, pending.tailId,
                                 pending.offset);
    }

    if (splitAny) {
        pushProjectEdit(before, tr("Split at current time"));
        finishEdit(tr("Split at current time"));
    } else {
        setLastMessage(tr("Nothing to split here — move to a clip first"), QStringLiteral("warning"));
    }
}

void AppController::splitClipAt(int trackIndex, int clipIndex, double seconds)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    drift::Clip &clip = track.clips[clipIndex];
    const drift::TimeUs atUs = drift::secondsToUs(seconds);
    if (!clip.containsTime(atUs) || atUs == clip.timelineStart)
        return;

    const drift::Project before = m_project;
    const drift::TimeUs offset = atUs - clip.timelineStart;
    drift::Clip tail;
    if (!drift::splitClipAtOffset(clip, tail, offset, m_project.fps()))
        return;

    tail.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString tailLinkId = drift::assignSplitLinkIds(clip, tail);
    const QString headId = clip.id;
    splitLinkedPartnerAt(m_project, clip, atUs, tailLinkId);
    track.clips.insert(clipIndex + 1, tail);
    splitLinkedAdjustmentsAt(m_project, trackIndex, headId, tail.id, offset);

    pushProjectEdit(before, tr("Split clip"));
    finishEdit(tr("Split clip"));
}

void AppController::splitClipLeftAt(int trackIndex, int clipIndex, double seconds)
{
    trimClipGroupAt(trackIndex, clipIndex, drift::secondsToUs(seconds), true);
}

void AppController::splitClipRightAt(int trackIndex, int clipIndex, double seconds)
{
    trimClipGroupAt(trackIndex, clipIndex, drift::secondsToUs(seconds), false);
}

// Split-left / split-right are trims: they drop everything on one side of `atUs` from the clip
// and every linked partner that spans it, keep the clip's id (so transitions, selection and pinned
// effects stay attached), and ripple all the group's tracks together.
void AppController::trimClipGroupAt(int trackIndex, int clipIndex, drift::TimeUs atUs, bool dropLeft)
{
    if (!isValidClipIndex(trackIndex, clipIndex))
        return;

    QList<QPair<int, int>> group;
    QList<drift::Clip> kept;
    drift::TimeUs offset = 0;
    drift::TimeUs oldEnd = 0;
    drift::TimeUs oldDuration = 0;
    {
        // Read through a const view so nothing detaches before the snapshot below.
        const drift::Project &project = m_project;
        const drift::Clip &probe = project.tracks().at(trackIndex).clips.at(clipIndex);
        if (!probe.containsTime(atUs) || atUs == probe.timelineStart)
            return;
        offset = atUs - probe.timelineStart;
        oldEnd = probe.timelineEnd();
        oldDuration = probe.timelineDuration;
        for (const QPair<int, int> &ref : selectionWithLinkedPartners(project, trackIndex, clipIndex)) {
            const drift::Clip &member = project.tracks().at(ref.first).clips.at(ref.second);
            const bool isMain = ref.first == trackIndex && ref.second == clipIndex;
            if (!member.containsTime(atUs) || atUs == member.timelineStart) {
                if (isMain)
                    return;
                continue;
            }
            drift::Clip left = member;
            drift::Clip right;
            if (!drift::splitClipAtOffset(left, right, atUs - member.timelineStart, m_project.fps())) {
                if (isMain)
                    return;
                continue;
            }
            drift::Clip keep = dropLeft ? right : left;
            keep.id = member.id;
            keep.linkId = member.linkId;
            if (dropLeft) {
                keep.fadeInUs = qMin(member.fadeInUs, keep.timelineDuration);
                keep.animIn = member.animIn;
                keep.audioFadeInUs = member.audioFadeInUs;
            } else {
                keep.fadeOutUs = qMin(member.fadeOutUs, keep.timelineDuration);
                keep.animOut = member.animOut;
                keep.audioFadeOutUs = member.audioFadeOutUs;
            }
            group.append(ref);
            kept.append(keep);
        }
    }

    // Snapshot before taking any non-const reference into m_project: QList is
    // copy-on-write, and a reference grabbed first mutates the buffer the copy
    // still shares, leaving `before` already split and undo a no-op.
    const drift::Project before = m_project;
    QSet<int> tracks;
    QSet<QString> ids;
    for (int i = 0; i < group.size(); ++i) {
        drift::Clip &slot = m_project.tracks()[group.at(i).first].clips[group.at(i).second];
        slot = kept.at(i);
        // Close the leading gap: keep the left edge put.
        if (dropLeft && m_rippleEnabled)
            slot.timelineStart -= offset;
        tracks.insert(group.at(i).first);
        ids.insert(slot.id);
    }
    if (m_rippleEnabled) {
        if (dropLeft)
            rippleTracksFrom(m_project, tracks, atUs, -offset, ids);
        else
            rippleTracksFrom(m_project, tracks, oldEnd, offset - oldDuration, ids);
    }

    pushProjectEdit(before, dropLeft ? tr("Split left") : tr("Split right"));
    finishEdit(dropLeft ? tr("Split left") : tr("Split right"));
    selectClip(trackIndex, clipIndex);
}

bool AppController::replaceClipGroupWithSegments(
    int trackIndex, int clipIndex, const std::function<QList<drift::Clip>(const drift::Clip &)> &build,
    bool ripple, SegmentReplaceResult *out, QString *error)
{
    if (!isValidClipIndex(trackIndex, clipIndex)) {
        if (error)
            *error = QStringLiteral("Unknown clip");
        return false;
    }

    struct Member
    {
        int track = -1;
        drift::Clip original;
        QList<drift::Clip> segments;
    };
    QList<Member> members;
    {
        const drift::Project &project = m_project;
        const drift::Clip &main = project.tracks().at(trackIndex).clips.at(clipIndex);
        const QList<drift::Clip> mainSegments = build(main);
        for (const QPair<int, int> &ref : selectionWithLinkedPartners(project, trackIndex, clipIndex)) {
            const drift::Clip &clip = project.tracks().at(ref.first).clips.at(ref.second);
            Member m{ref.first, clip, {}};
            if (ref.first == trackIndex && ref.second == clipIndex) {
                m.segments = mainSegments;
            } else {
                m.segments = build(clip);
                // A partner that doesn't cut the same way (its own trim drifted) follows the
                // main clip's timing, which is what a linked pair means.
                bool same = m.segments.size() == mainSegments.size();
                for (int i = 0; same && i < mainSegments.size(); ++i) {
                    same = m.segments.at(i).timelineStart == mainSegments.at(i).timelineStart
                           && m.segments.at(i).timelineDuration == mainSegments.at(i).timelineDuration;
                }
                if (!same) {
                    m.segments.clear();
                    for (const drift::Clip &seg : mainSegments) {
                        drift::Clip p = clip;
                        drift::syncLinkedTiming(p, seg);
                        m.segments.append(p);
                    }
                }
            }
            members.append(m);
        }
    }

    const drift::Clip &main = members.first().original;
    drift::TimeUs newEnd = main.timelineStart;
    for (const drift::Clip &seg : members.first().segments)
        newEnd = qMax(newEnd, seg.timelineEnd());
    const drift::TimeUs oldEnd = main.timelineEnd();
    const drift::TimeUs delta = newEnd - oldEnd;

    QSet<int> groupTracks;
    QSet<QString> groupIds;
    for (const Member &m : std::as_const(members)) {
        groupTracks.insert(m.track);
        groupIds.insert(m.original.id);
    }
    if (!ripple && delta > 0) {
        for (const int t : std::as_const(groupTracks)) {
            for (const drift::Clip &clip : m_project.tracks().at(t).clips) {
                if (!groupIds.contains(clip.id) && clip.timelineStart >= oldEnd
                    && clip.timelineStart < newEnd) {
                    if (error)
                        *error = QStringLiteral("would_overlap");
                    return false;
                }
            }
        }
    }

    // Ids and links: segment 0 inherits the original's, so everything addressing the clip keeps
    // working; each later segment index shares one fresh linkId across the group.
    const int count = members.first().segments.size();
    QStringList segmentLinks;
    for (int k = 0; k < count; ++k)
        segmentLinks.append(k == 0 || main.linkId.isEmpty()
                                ? main.linkId
                                : QUuid::createUuid().toString(QUuid::WithoutBraces));
    QSet<QString> newIds;
    for (Member &m : members) {
        for (int k = 0; k < m.segments.size(); ++k) {
            drift::Clip &seg = m.segments[k];
            seg.id = k == 0 ? m.original.id : QUuid::createUuid().toString(QUuid::WithoutBraces);
            if (!m.original.linkId.isEmpty())
                seg.linkId = k < segmentLinks.size() ? segmentLinks.at(k) : m.original.linkId;
            newIds.insert(seg.id);
        }
    }

    for (const Member &m : std::as_const(members)) {
        drift::Track &track = m_project.tracks()[m.track];
        int index = -1;
        for (int c = 0; c < track.clips.size(); ++c) {
            if (track.clips.at(c).id == m.original.id) {
                index = c;
                break;
            }
        }
        if (index < 0)
            continue;
        track.clips.removeAt(index);
        for (int k = 0; k < m.segments.size(); ++k)
            track.clips.insert(index + k, m.segments.at(k));

        if (m.segments.isEmpty()) {
            for (int i = track.transitions.size() - 1; i >= 0; --i) {
                if (track.transitions.at(i).fromClipId == m.original.id
                    || track.transitions.at(i).toClipId == m.original.id)
                    track.transitions.removeAt(i);
            }
        } else {
            const QString lastId = m.segments.last().id;
            for (drift::Transition &transition : track.transitions) {
                if (transition.fromClipId == m.original.id)
                    transition.fromClipId = lastId;
            }
        }

        // Pinned effect stacks: one per segment, or gone with the clip.
        for (const int laneIndex : drift::adjustmentLaneIndexes(m_project, m.track)) {
            drift::Track &lane = m_project.tracks()[laneIndex];
            for (int c = lane.clips.size() - 1; c >= 0; --c) {
                if (lane.clips.at(c).linkedClipId != m.original.id)
                    continue;
                if (m.segments.isEmpty()) {
                    lane.clips.removeAt(c);
                    continue;
                }
                for (int k = m.segments.size() - 1; k >= 1; --k) {
                    drift::Clip copy = lane.clips.at(c);
                    copy.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
                    copy.linkedClipId = m.segments.at(k).id;
                    lane.clips.insert(c + 1, copy);
                }
            }
        }
    }

    if (ripple)
        rippleTracksFrom(m_project, groupTracks, oldEnd, delta, newIds);

    if (out) {
        out->ids.clear();
        for (const drift::Clip &seg : members.first().segments)
            out->ids.append(seg.id);
        out->deltaUs = delta;
    }
    return true;
}

const QList<drift::TimeUs> &AppController::extraSnapTargetsCached() const
{
    if (!m_extraSnapTargetsValid) {
        m_extraSnapTargetsCache = extraSnapTargets();
        m_extraSnapTargetsValid = true;
    }
    return m_extraSnapTargetsCache;
}

drift::TimeUs AppController::snapTimeForGesture(drift::TimeUs rawUs) const
{
    if (m_trimGestureActive)
        return drift::snapTimeTo(m_gestureSnapTargets, rawUs, m_snapEnabled, m_snapThresholdUs);
    return drift::snapTime(m_project, rawUs, m_snapEnabled, m_playheadUs,
                           extraSnapTargetsCached(), {}, m_snapThresholdUs);
}

void AppController::beginTrimGesture(int trackIndex, int clipIndex, int side)
{
    Q_UNUSED(trackIndex)
    Q_UNUSED(clipIndex)
    Q_UNUSED(side)
    m_gestureSnapTargets.build(m_project, m_playheadUs, extraSnapTargetsCached());
    m_trimGestureActive = true;
    m_trimGestureChanged = false;
    m_trimGestureLastInputUs = -1;
    m_trimGestureLastOutcome = TrimNone;
}

void AppController::endTrimGesture()
{
    m_gestureSnapTargets.sorted.clear();
    m_gestureSnapTargets.sorted.shrink_to_fit();
    m_trimGestureActive = false;
    m_trimGestureLastInputUs = -1;
    m_trimGestureLastOutcome = TrimNone;
}

AppController::TrimComputation AppController::computeTrimLeft(int trackIndex, int clipIndex,
                                                              double newStart, bool snap) const
{
    TrimComputation out;
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return out;
    const drift::Track &track = m_project.tracks().at(trackIndex);
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return out;

    out.ok = true;
    out.outcome = TrimNone;
    // Worked on a copy: the caller decides whether this ever reaches the project.
    drift::Clip clip = track.clips.at(clipIndex);
    out.clip = clip;

    const drift::TimeUs rawUs = drift::secondsToUs(newStart);
    drift::TimeUs snappedStart = snap ? snapTimeForGesture(rawUs) : rawUs;
    // Whether the edge landed where the pointer asked or was pulled onto a snap target is the
    // difference between the two feelings Haptics offers.
    const int movedOutcome = (snappedStart != rawUs) ? TrimSnapped : TrimMoved;
    // With ripple on the left edge stays put and everything after the clip moves instead, the
    // way trimClipGroupAt closes the leading gap -- so extending cannot run into a neighbour.
    const drift::TimeUs oldStart = clip.timelineStart;
    const drift::TimeUs oldEnd = clip.timelineEnd();
    const drift::TimeUs oldDuration = clip.timelineDuration;
    const auto finishRipple = [&](drift::Clip &trimmed) {
        if (!m_rippleEnabled)
            return;
        trimmed.timelineStart = oldStart;
        out.rippleFrom = oldEnd;
        out.rippleDelta = trimmed.timelineDuration - oldDuration;
    };
    // Extending left can create a new overlap; clamp against neighbors when overlap is off.
    if (!m_allowClipOverlap && !m_rippleEnabled && snappedStart < clip.timelineStart) {
        const QSet<QString> exclude{clip.id};
        snappedStart = drift::clampClipStartAgainstLeftNeighbors(track, exclude, clip.timelineStart,
                                                                 snappedStart);
    }
    const drift::TimeUs delta = snappedStart - clip.timelineStart;
    if (delta == 0)
        return out;

    if (isSyntheticTimelineClip(clip.type)) {
        if (delta > 0) {
            if (clip.timelineDuration - delta < drift::minClipDurationUs(m_project.fps())) {
                out.outcome = TrimBlocked;
                return out;
            }
            clip.timelineStart += delta;
            clip.timelineDuration -= delta;
        } else {
            const drift::TimeUs extendBy = -delta;
            if (clip.timelineDuration + extendBy > syntheticClipMaxDurationUs()) {
                out.outcome = TrimBlocked;
                return out;
            }
            clip.timelineStart = snappedStart;
            clip.timelineDuration += extendBy;
        }
        // Cue times are relative to the clip's timeline start, so they have to travel with it or
        // every subtitle would slide by the trim amount. Cues pushed outside the clip keep their
        // (possibly negative) offsets so dragging the edge back restores them.
        for (drift::SubtitleCue &cue : clip.subtitleCues) {
            cue.startUs -= delta;
            cue.endUs -= delta;
        }
        if (clip.type == drift::ClipType::Adjustment && clip.adjustmentKind == drift::AdjustmentKind::Mask)
            clip.srcIn += delta;
        syncSyntheticSourceRange(clip);
        if (!m_rippleEnabled && trackHasCompleteOverlap(track, clip, &track.clips.at(clipIndex))) {
            out.outcome = TrimBlocked;
            return out;
        }
        finishRipple(clip);
        out.clip = clip;
        out.changed = true;
        out.outcome = movedOutcome;
        return out;
    }

    if (delta > 0) {
        if (clip.timelineDuration - delta < drift::minClipDurationUs(m_project.fps())) {
            out.outcome = TrimBlocked;
            return out;
        }
        const drift::TimeUs sourceDelta = trimSourceDelta(clip, delta, false, false);
        if (sourceDelta <= 0) {
            out.outcome = TrimBlocked;
            return out;
        }
        if (clip.reverse) {
            if (clip.srcOut <= clip.srcIn + sourceDelta + drift::minClipDurationUs(m_project.fps())) {
                out.outcome = TrimBlocked;
                return out;
            }
        } else if (clip.srcIn + sourceDelta > clip.srcOut - drift::minClipDurationUs(m_project.fps())) {
            out.outcome = TrimBlocked;
            return out;
        }

        clip.timelineStart += delta;
        clip.timelineDuration -= delta;
        if (clip.reverse)
            clip.srcOut -= sourceDelta;
        else
            clip.srcIn += sourceDelta;
    } else {
        const drift::TimeUs extendBy = -delta;
        const drift::TimeUs sourceExtend = trimSourceDelta(clip, extendBy, true, clip.reverse);
        if (clip.reverse) {
            const drift::TimeUs maxSource = sourceDurationForClip(clip);
            if (clip.srcOut + sourceExtend > maxSource) {
                out.outcome = TrimBlocked;
                return out;
            }
            clip.timelineStart = snappedStart;
            clip.srcOut += sourceExtend;
            clip.timelineDuration += extendBy;
        } else {
            if (sourceExtend > clip.srcIn) {
                out.outcome = TrimBlocked;
                return out;
            }
            clip.timelineStart = snappedStart;
            clip.srcIn -= sourceExtend;
            clip.timelineDuration += extendBy;
        }
    }

    if (!m_rippleEnabled && trackHasCompleteOverlap(track, clip, &track.clips.at(clipIndex))) {
        out.outcome = TrimBlocked;
        return out;
    }
    clip.syncDurationFromSpeedCurve();
    finishRipple(clip);
    out.clip = clip;
    out.changed = true;
    out.outcome = movedOutcome;
    return out;
}

AppController::TrimComputation AppController::computeTrimRight(int trackIndex, int clipIndex,
                                                               double newEnd, bool snap) const
{
    TrimComputation out;
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return out;
    const drift::Track &track = m_project.tracks().at(trackIndex);
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return out;

    out.ok = true;
    out.outcome = TrimNone;
    drift::Clip clip = track.clips.at(clipIndex);
    out.clip = clip;

    const drift::TimeUs rawUs = drift::secondsToUs(newEnd);
    drift::TimeUs snappedEnd = snap ? snapTimeForGesture(rawUs) : rawUs;
    // With ripple on, extending pushes the followers along rather than stopping at them.
    if (!m_allowClipOverlap && !m_rippleEnabled && snappedEnd > clip.timelineEnd()) {
        const QSet<QString> exclude{clip.id};
        snappedEnd = drift::clampClipEndNoOverlap(track, exclude, clip.timelineEnd(), snappedEnd);
    }
    const drift::TimeUs oldEnd = clip.timelineEnd();
    const drift::TimeUs oldDuration = clip.timelineDuration;
    drift::TimeUs newDuration = snappedEnd - clip.timelineStart;

    const bool syntheticVisual = isSyntheticTimelineClip(clip.type);
    const drift::TimeUs maxSource = sourceDurationForClip(clip);
    const drift::TimeUs maxSourceSpan =
        clip.reverse ? clip.srcOut : (maxSource > clip.srcIn ? maxSource - clip.srcIn : 0);
    const drift::TimeUs mediaMaxDuration =
        clip.effectiveSpeed() > 0.0
            ? static_cast<drift::TimeUs>(llround(static_cast<double>(maxSourceSpan) / clip.effectiveSpeed()))
            : maxSourceSpan;
    const drift::TimeUs maxDuration =
        syntheticVisual ? syntheticClipMaxDurationUs() : mediaMaxDuration;
    newDuration = qBound(drift::minClipDurationUs(m_project.fps()), newDuration, maxDuration);

    // An edge parked against a snap target or against maxDuration has nowhere to go.
    if (newDuration == clip.timelineDuration)
        return out;
    // Distinguishes "the edge is where you asked" from "the edge was pulled onto a target", and
    // the clamp above from a free move.
    const int movedOutcome = (newDuration != snappedEnd - clip.timelineStart) ? TrimBlocked
                             : (snappedEnd != rawUs)                          ? TrimSnapped
                                                                              : TrimMoved;

    clip.timelineDuration = newDuration;
    const drift::TimeUs span =
        clip.hasSpeedCurve() ? trimSourceDelta(clip, newDuration, false, true) : clip.sourceSpanUs();
    const drift::TimeUs maxSrcOut = syntheticVisual ? syntheticClipMaxDurationUs() : maxSource;
    if (clip.reverse) {
        clip.srcIn = qMax<drift::TimeUs>(0, clip.srcOut - span);
    } else {
        clip.srcOut = qMin(clip.srcIn + span, maxSrcOut);
    }
    if (!m_rippleEnabled && trackHasCompleteOverlap(track, clip, &track.clips.at(clipIndex))) {
        out.outcome = TrimBlocked;
        return out;
    }
    clip.syncDurationFromSpeedCurve();
    if (m_rippleEnabled) {
        out.rippleFrom = oldEnd;
        out.rippleDelta = clip.timelineDuration - oldDuration;
    }
    out.clip = clip;
    out.changed = true;
    out.outcome = movedOutcome;
    return out;
}

// The clips a trim's ripple would move, for the live preview. Mirrors what applyTrim hands to
// rippleTracksFrom: followers on the clip's and its partners' tracks, plus their own partners.
static QStringList rippleFollowerIds(const drift::Project &project, int trackIndex, int clipIndex,
                                     drift::TimeUs fromUs)
{
    const drift::Clip &clip = project.tracks().at(trackIndex).clips.at(clipIndex);
    QSet<int> tracks{trackIndex};
    QSet<QString> exclude{clip.id};
    for (const drift::ClipRef &ref : drift::linkedPartners(project, clip)) {
        tracks.insert(ref.trackIndex);
        exclude.insert(project.tracks().at(ref.trackIndex).clips.at(ref.clipIndex).id);
    }
    QSet<QString> moved;
    for (const int t : std::as_const(tracks)) {
        for (const drift::Clip &follower : project.tracks().at(t).clips) {
            if (exclude.contains(follower.id) || follower.timelineStart < fromUs)
                continue;
            moved.insert(follower.id);
            for (const drift::ClipRef &ref : drift::linkedPartners(project, follower))
                moved.insert(project.tracks().at(ref.trackIndex).clips.at(ref.clipIndex).id);
        }
    }
    moved.subtract(exclude);
    return QStringList(moved.cbegin(), moved.cend());
}

QVariantMap AppController::trimPreviewToMap(int trackIndex, int clipIndex,
                                            const TrimComputation &c) const
{
    if (!c.ok)
        return QVariantMap{{QStringLiteral("ok"), false}};
    const QStringList rippleIds = c.changed && c.rippleDelta != 0
                                      ? rippleFollowerIds(m_project, trackIndex, clipIndex, c.rippleFrom)
                                      : QStringList();
    return QVariantMap{
        {QStringLiteral("rippleIds"), rippleIds},
        {QStringLiteral("rippleDelta"), drift::usToSeconds(c.rippleDelta)},
        {QStringLiteral("ok"), true},
        {QStringLiteral("changed"), c.changed},
        {QStringLiteral("outcome"), c.outcome},
        {QStringLiteral("start"), drift::usToSeconds(c.clip.timelineStart)},
        {QStringLiteral("duration"), drift::usToSeconds(c.clip.timelineDuration)},
        {QStringLiteral("inPoint"), drift::usToSeconds(c.clip.srcIn)},
        {QStringLiteral("outPoint"), drift::usToSeconds(c.clip.srcOut)},
    };
}

QVariantMap AppController::previewTrimLeft(int trackIndex, int clipIndex, double newStart) const
{
    return trimPreviewToMap(trackIndex, clipIndex, computeTrimLeft(trackIndex, clipIndex, newStart));
}

QVariantMap AppController::previewTrimRight(int trackIndex, int clipIndex, double newEnd) const
{
    return trimPreviewToMap(trackIndex, clipIndex, computeTrimRight(trackIndex, clipIndex, newEnd));
}

// Writes a computed trim back to the project and brings everything that hangs off the clip with
// it. Shared by both edges.
int AppController::applyTrim(int trackIndex, int clipIndex, const TrimComputation &computed, int side)
{
    if (!computed.ok)
        return TrimNone;
    if (!computed.changed)
        return m_trimGestureLastOutcome = computed.outcome;

    drift::Track &track = m_project.tracks()[trackIndex];
    drift::Clip &clip = track.clips[clipIndex];
    const drift::TimeUs durationDelta = computed.clip.timelineDuration - clip.timelineDuration;
    clip = computed.clip;

    // A loose partner has its own length, so it gets the same edge moved by the same amount
    // rather than a copy of this clip's timing. Ripple, when on, keeps every left edge put.
    QList<QPair<int, TrimComputation>> loosePartners;
    if (clip.linkLoose && durationDelta != 0) {
        for (const drift::ClipRef &ref : drift::linkedPartners(m_project, clip)) {
            const drift::Clip &partner = m_project.tracks().at(ref.trackIndex).clips.at(ref.clipIndex);
            const TrimComputation c =
                side < 0 ? computeTrimLeft(ref.trackIndex, ref.clipIndex,
                                           drift::usToSeconds(partner.timelineStart - durationDelta), false)
                         : computeTrimRight(ref.trackIndex, ref.clipIndex,
                                            drift::usToSeconds(partner.timelineEnd() + durationDelta), false);
            if (!c.changed)
                continue;
            m_project.tracks()[ref.trackIndex].clips[ref.clipIndex] = c.clip;
            loosePartners.append(qMakePair(ref.trackIndex, c));
        }
    }

    // linkId, not linkedClipId: linkId is what symmetrically pairs A/V companions, which is what
    // syncLinkedPartnersFrom walks. linkedClipId is the directional pin used by adjustments, and
    // guarding on it here skipped the partner sync for every linked audio clip.
    if (!clip.linkId.isEmpty())
        syncLinkedPartnersFrom(m_project, clip);
    if (computed.rippleDelta != 0) {
        QSet<int> tracks{trackIndex};
        QSet<QString> ids{clip.id};
        for (const drift::ClipRef &ref : drift::linkedPartners(m_project, clip)) {
            tracks.insert(ref.trackIndex);
            ids.insert(m_project.tracks().at(ref.trackIndex).clips.at(ref.clipIndex).id);
        }
        if (clip.linkLoose)
            tracks = {trackIndex};
        rippleTracksFrom(m_project, tracks, computed.rippleFrom, computed.rippleDelta, ids);
        for (const int t : std::as_const(tracks)) {
            if (t != trackIndex)
                syncOverlapTransitionsOnTrack(m_project.tracks()[t]);
        }
    }
    // Each loose partner ripples its own track from its own end, which need not be this clip's.
    for (const QPair<int, TrimComputation> &p : std::as_const(loosePartners)) {
        QSet<QString> ids{clip.id};
        for (const drift::ClipRef &ref : drift::linkedPartners(m_project, clip))
            ids.insert(m_project.tracks().at(ref.trackIndex).clips.at(ref.clipIndex).id);
        if (p.second.rippleDelta != 0)
            rippleTracksFrom(m_project, {p.first}, p.second.rippleFrom, p.second.rippleDelta, ids);
        if (p.first != trackIndex)
            syncOverlapTransitionsOnTrack(m_project.tracks()[p.first]);
    }
    syncOverlapTransitionsOnTrack(track);
    // A pinned adjustment takes its extent from its clip, so it has to be brought along here --
    // this path does not always reach finishEdit.
    syncLinkedAdjustments(m_project);
    m_trimGestureChanged = true;
    notifyTracksChanged();
    return m_trimGestureLastOutcome = computed.outcome;
}

void AppController::commitTrim(int trackIndex, int clipIndex, int side, double seconds)
{
    beginTracksBatch();
    if (seconds >= 0) {
        if (side < 0)
            trimClipLeft(trackIndex, clipIndex, seconds);
        else
            trimClipRight(trackIndex, clipIndex, seconds);
    }
    // A press and release that never moved the edge is a click, not an edit: committing it would
    // push an undo step that restores nothing and mark the project dirty for having done nothing.
    const bool moved = m_trimGestureChanged;
    endTrimGesture();
    if (moved)
        commitPreviewDrag();
    else
        cancelPreviewDrag();
    endTracksBatch();
}

int AppController::trimClipLeft(int trackIndex, int clipIndex, double newStart)
{
    const drift::TimeUs rawUs = drift::secondsToUs(newStart);
    // Same pointer position, same project state, same answer.
    if (m_trimGestureActive && rawUs == m_trimGestureLastInputUs)
        return m_trimGestureLastOutcome;
    m_trimGestureLastInputUs = rawUs;
    return applyTrim(trackIndex, clipIndex, computeTrimLeft(trackIndex, clipIndex, newStart), -1);
}

int AppController::trimClipRight(int trackIndex, int clipIndex, double newEnd)
{
    const drift::TimeUs rawUs = drift::secondsToUs(newEnd);
    // See the note in trimClipLeft.
    if (m_trimGestureActive && rawUs == m_trimGestureLastInputUs)
        return m_trimGestureLastOutcome;
    m_trimGestureLastInputUs = rawUs;
    return applyTrim(trackIndex, clipIndex, computeTrimRight(trackIndex, clipIndex, newEnd), 1);
}

void AppController::setClipTrim(int trackIndex, int clipIndex, double inPoint, double outPoint)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    drift::Clip &clip = track.clips[clipIndex];
    const bool syntheticVisual = isSyntheticTimelineClip(clip.type);
    const drift::TimeUs sourceDuration = syntheticVisual ? syntheticClipMaxDurationUs() : sourceDurationForClip(clip);
    const drift::TimeUs clampedIn = qBound<drift::TimeUs>(0, drift::secondsToUs(inPoint),
                                                          sourceDuration - drift::minClipDurationUs(m_project.fps()));
    const drift::TimeUs clampedOut = qBound(clampedIn + drift::minClipDurationUs(m_project.fps()), drift::secondsToUs(outPoint),
                                            sourceDuration);
    const drift::TimeUs newDuration = clampedOut - clampedIn;
    const double speed = clip.effectiveSpeed();

    const drift::Project before = m_project;
    clip.srcIn = clampedIn;
    clip.srcOut = clampedOut;
    clip.timelineDuration = static_cast<drift::TimeUs>(llround(static_cast<double>(newDuration) / speed));
    clip.timelineDuration = qMax(clip.timelineDuration, drift::minClipDurationUs(m_project.fps()));
    // A ramp re-derives the duration from the range that survived the trim.
    clip.syncDurationFromSpeedCurve();
    pushProjectEdit(before, tr("Trim updated"));
    finishEdit(tr("Trim updated"));
}

void AppController::duplicateSelectedClip()
{
    if (m_selectedTrack < 0 || m_selectedClip < 0)
        return;

    drift::Track &track = m_project.tracks()[m_selectedTrack];
    if (m_selectedClip >= track.clips.size())
        return;

    const drift::Project before = m_project;
    const drift::Clip original = track.clips.at(m_selectedClip);
    drift::Clip copy = original;
    copy.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    // Sharing the original's linkId would pair the copy with the original's partners.
    copy.linkId.clear();
    copy.linkLoose = false;
    // Likewise the video an extracted audio clip came from: the copy is not that video's audio.
    copy.sourceVideoClipId.clear();
    copy.timelineStart = drift::resolveClipStart(
        m_project, track, -1, original.timelineEnd(), original.timelineDuration, m_snapEnabled, m_playheadUs);

    track.clips.append(copy);
    const int newClipIndex = track.clips.size() - 1;
    pushProjectEdit(before, tr("Clip duplicated"));
    finishEdit(tr("Clip duplicated"));
    selectClip(m_selectedTrack, newClipIndex);
}

void AppController::alignSelectedClipLeft()
{
    splitSelectedClipLeft();
}

void AppController::alignSelectedClipRight()
{
    splitSelectedClipRight();
}

// The playhead-relative half of the pair above, reached by the "delete left"/"delete right"
// shortcuts and by the align_clip_left/right MCP tools. These used to carry their own copy
// of the split, which both broke their undo snapshot and left them ignoring rippleEnabled
// while the identical context-menu path honoured it.
void AppController::splitSelectedClipLeft()
{
    splitClipLeftAt(m_selectedTrack, m_selectedClip, playheadSeconds());
}

void AppController::splitSelectedClipRight()
{
    splitClipRightAt(m_selectedTrack, m_selectedClip, playheadSeconds());
}

QVariantList AppController::compatibleTracksForClip(int trackIndex, int clipIndex) const
{
    QVariantList result;
    if (!isValidClipIndex(trackIndex, clipIndex))
        return result;
    const drift::Clip &clip = m_project.tracks().at(trackIndex).clips.at(clipIndex);
    for (int i = 0; i < m_project.tracks().size(); ++i) {
        const drift::Track &track = m_project.tracks().at(i);
        // Nested lanes have no row or label of their own to pick.
        if (i == trackIndex || track.isAdjustmentLane() || !track.acceptsClip(clip))
            continue;
        result.append(QVariantMap{{QStringLiteral("index"), i}, {QStringLiteral("name"), track.name}});
    }
    return result;
}

namespace {

// What a replacement asset turns the clip into, or nullopt when it cannot stand in for it.
// Audio clips stay audio (a video's soundtrack is fine); visual clips follow the asset's kind but
// only where the clip's own track still takes the result.
std::optional<drift::ClipType> replacementClipType(const drift::Project &project, AssetLibrary *library,
                                                   const drift::Track &track, const drift::Clip &clip,
                                                   const drift::MediaAsset &asset)
{
    if (asset.id == clip.assetId)
        return std::nullopt;
    if (clip.type == drift::ClipType::Audio) {
        if (asset.kind == drift::MediaKind::Audio
            || (asset.kind == drift::MediaKind::Video
                && assetHasAudioStreams(project, library, asset.id)))
            return drift::ClipType::Audio;
        return std::nullopt;
    }
    if (clip.type != drift::ClipType::Video && clip.type != drift::ClipType::Image)
        return std::nullopt;
    if (asset.kind != drift::MediaKind::Video && asset.kind != drift::MediaKind::Image)
        return std::nullopt;
    const drift::ClipType type = asset.kind == drift::MediaKind::Image ? drift::ClipType::Image
                                                                      : drift::ClipType::Video;
    if (!track.allowsClipType(type))
        return std::nullopt;
    return type;
}

} // namespace

QVariantList AppController::replaceCandidatesForClip(int trackIndex, int clipIndex) const
{
    QVariantList result;
    if (!m_assetLibrary || !isValidClipIndex(trackIndex, clipIndex))
        return result;
    const drift::Track &track = m_project.tracks().at(trackIndex);
    const drift::Clip &clip = track.clips.at(clipIndex);
    if (!clip.sequenceId.isEmpty() || clip.assetId.isEmpty())
        return result;
    for (int i = 0; i < m_assetLibrary->rowCount(); ++i) {
        const drift::MediaAsset *asset = m_project.asset(m_assetLibrary->assetIdAt(i));
        if (!asset || !replacementClipType(m_project, m_assetLibrary, track, clip, *asset))
            continue;
        const QVariantMap info = m_assetLibrary->assetAt(i);
        result.append(QVariantMap{
            {QStringLiteral("assetIndex"), i},
            {QStringLiteral("name"), asset->name},
            {QStringLiteral("kind"), info.value(QStringLiteral("kind"))},
            {QStringLiteral("duration"), info.value(QStringLiteral("duration"))},
            {QStringLiteral("thumbnail"), asset->thumbnailPath},
        });
    }
    return result;
}

// Swaps the media under a clip and leaves everything about its place alone: start, length on the
// timeline, transform, effects and keyframes. The source range restarts at the head of the new
// file, and the clip gets shorter if the file runs out. Linked audio follows when the new file
// has sound; otherwise the partner keeps its own media and the link turns loose, which is the one
// link kind that tolerates partners with unrelated media and lengths.
bool AppController::replaceClipMedia(int trackIndex, int clipIndex, int assetIndex)
{
    if (!m_assetLibrary || !isValidClipIndex(trackIndex, clipIndex))
        return false;
    const drift::MediaAsset *asset = m_project.asset(m_assetLibrary->assetIdAt(assetIndex));
    if (!asset)
        return false;
    const drift::Clip target = m_project.tracks().at(trackIndex).clips.at(clipIndex);
    if (!target.sequenceId.isEmpty() || target.assetId.isEmpty())
        return false;
    const auto newType = replacementClipType(m_project, m_assetLibrary, m_project.tracks().at(trackIndex),
                                             target, *asset);
    if (!newType)
        return false;

    m_assetLibrary->ensureMedia(assetIndex);
    const drift::MediaAsset assetCopy = *asset;
    const QVariantMap info = m_assetLibrary->assetAt(assetIndex);
    const bool newHasAudio = assetHasAudioStreams(m_project, m_assetLibrary, assetCopy.id);

    const drift::Project before = m_project.detachedCopy();
    const QString oldAssetId = target.assetId;
    const QString linkId = target.linkId;
    const QString oldName = m_project.asset(oldAssetId) ? m_project.asset(oldAssetId)->name : QString();

    const auto rebind = [&](drift::Clip &clip, drift::ClipType type) {
        const bool still = type == drift::ClipType::Image;
        if (clip.name.isEmpty() || clip.name == oldName)
            clip.name = assetCopy.name;
        clip.assetId = assetCopy.id;
        clip.path = assetCopy.path;
        clip.thumbnailPath = assetCopy.thumbnailPath;
        clip.filmstripPath = assetCopy.filmstripPath;
        clip.type = type;
        clip.audioStreamIndex = 0;
        if (type != drift::ClipType::Audio) {
            clip.sourceFrame = info.value(QStringLiteral("sourceFrame"), QRectF(0, 0, 1, 1)).toRectF();
            clip.rotationCorrection = info.value(QStringLiteral("rotationCorrection")).toInt();
        }
        // The landmarks and depth maps were made from the old pixels.
        clip.faceTrackPath.clear();
        clip.faceTrackSrcOffsetUs = 0;
        clip.depthPath.clear();

        if (still) {
            clip.speed = 1.0;
            clip.speedCurve = {};
            clip.reverse = false;
            clip.srcIn = 0;
            clip.srcOut = clip.timelineDuration;
            return;
        }
        const drift::TimeUs span = clip.srcOut - clip.srcIn;
        clip.srcIn = 0;
        const drift::TimeUs available = assetCopy.durationUs;
        if (clip.hasSpeedCurve()) {
            clip.srcOut = span;
            if (available > 0 && clip.srcOut > available) {
                clip.srcOut = available;
                clip.syncDurationFromSpeedCurve();
            }
        } else {
            clip.srcOut = clip.sourceSpanUs();
            if (available > 0)
                clip.syncSrcOutFromSpeed(available);
        }
    };

    QList<drift::ClipRef> partners;
    if (!linkId.isEmpty())
        partners = drift::linkedPartners(m_project, target);

    rebind(m_project.tracks()[trackIndex].clips[clipIndex], *newType);
    for (const drift::ClipRef &ref : std::as_const(partners)) {
        drift::Clip &partner = m_project.tracks()[ref.trackIndex].clips[ref.clipIndex];
        if (partner.assetId != oldAssetId)
            continue;
        if (newHasAudio && partner.type == drift::ClipType::Audio) {
            rebind(partner, drift::ClipType::Audio);
        } else {
            partner.linkLoose = true;
            m_project.tracks()[trackIndex].clips[clipIndex].linkLoose = true;
        }
    }

    pushProjectEdit(before, tr("Replace clip"));
    finishEdit(tr("Replace clip"));
    return true;
}

void AppController::moveClipToTrack(int trackIndex, int clipIndex, int newTrackIndex, double newStart)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;
    if (newTrackIndex < 0 || newTrackIndex >= m_project.tracks().size())
        return;
    if (!isValidClipIndex(trackIndex, clipIndex))
        return;

    const int trackDelta = newTrackIndex - trackIndex;
    if (trackDelta == 0) {
        moveClip(trackIndex, clipIndex, newStart);
        return;
    }

    const QPair<int, int> requested(trackIndex, clipIndex);
    QList<QPair<int, int>> targets = m_selection.contains(requested) ? m_selection
                                                                      : QList<QPair<int, int>>{requested};

    // A linked video carries its audio partner by the same delta even when only the video was
    // grabbed. The audio keeps its own track unless that is taken (resolved below).
    QSet<QString> movingVideoLinkIds;
    for (const QPair<int, int> &pair : std::as_const(targets)) {
        if (!isValidClipIndex(pair.first, pair.second))
            continue;
        const drift::Clip &c = m_project.tracks().at(pair.first).clips.at(pair.second);
        if ((c.type == drift::ClipType::Video || c.type == drift::ClipType::Composite)
            && !c.linkId.isEmpty())
            movingVideoLinkIds.insert(c.linkId);
    }
    if (!movingVideoLinkIds.isEmpty()) {
        const QList<QPair<int, int>> videoTargets = targets;
        for (const QPair<int, int> &pair : videoTargets) {
            if (!isValidClipIndex(pair.first, pair.second))
                continue;
            const drift::Clip &c = m_project.tracks().at(pair.first).clips.at(pair.second);
            if (c.type != drift::ClipType::Video && c.type != drift::ClipType::Composite)
                continue;
            for (const drift::ClipRef &ref : drift::linkedPartners(m_project, c)) {
                const QPair<int, int> partner(ref.trackIndex, ref.clipIndex);
                if (m_project.tracks().at(partner.first).clips.at(partner.second).type
                        == drift::ClipType::Audio
                    && !targets.contains(partner))
                    targets.append(partner);
            }
        }
    }

    const drift::Project before = m_project;
    const drift::TimeUs desiredUs = drift::secondsToUs(newStart);
    const drift::TimeUs baseUs = m_project.tracks().at(trackIndex).clips.at(clipIndex).timelineStart;
    const drift::TimeUs timeDelta = desiredUs - baseUs;

    // Verify the leader clip can land on the destination track
    const drift::Clip &leaderClip = m_project.tracks().at(trackIndex).clips.at(clipIndex);
    if (!m_project.tracks().at(newTrackIndex).acceptsClip(leaderClip))
        return;
    const QString leaderId = leaderClip.id;

    drift::TimeUs minGroupStartUs = -1;
    for (const QPair<int, int> &pair : targets) {
        if (!isValidClipIndex(pair.first, pair.second))
            continue;
        const drift::Clip &c = m_project.tracks().at(pair.first).clips.at(pair.second);
        if (minGroupStartUs < 0 || c.timelineStart < minGroupStartUs)
            minGroupStartUs = c.timelineStart;
    }

    drift::TimeUs clampedTimeDelta = timeDelta;
    if (clampedTimeDelta < 0 && minGroupStartUs >= 0 && -clampedTimeDelta > minGroupStartUs) {
        clampedTimeDelta = -minGroupStartUs;
    }

    struct ClipToMove {
        int fromTrack;
        int fromClipIndex;
        int toTrack;
        drift::Clip clip;
        drift::TimeUs newTimelineStart;
        bool followsVideo = false;
    };

    QList<ClipToMove> toMove;
    QSet<QString> movedIds;
    for (const QPair<int, int> &pair : targets) {
        if (!isValidClipIndex(pair.first, pair.second))
            continue;
        const drift::Clip &c = m_project.tracks().at(pair.first).clips.at(pair.second);
        int destTrack = pair.first;
        const bool followsVideo = c.type == drift::ClipType::Audio && !c.linkId.isEmpty()
                                  && movingVideoLinkIds.contains(c.linkId);
        if (followsVideo) {
            destTrack = pair.first;
        } else if (pair.first == trackIndex) {
            destTrack = newTrackIndex;
        } else {
            const int candidate = pair.first + trackDelta;
            if (candidate >= 0 && candidate < m_project.tracks().size()
                && m_project.tracks().at(candidate).acceptsClip(c)) {
                destTrack = candidate;
            }
        }
        ClipToMove item;
        item.fromTrack = pair.first;
        item.fromClipIndex = pair.second;
        item.toTrack = destTrack;
        item.clip = c;
        item.newTimelineStart = qMax<drift::TimeUs>(0, c.timelineStart + clampedTimeDelta);
        item.followsVideo = followsVideo;
        movedIds.insert(c.id);
        toMove.append(item);
    }

    if (!m_allowClipOverlap) {
        drift::TimeUs maxPushRight = 0;
        for (const ClipToMove &item : toMove) {
            if (item.followsVideo)
                continue;
            const drift::Track &destTrack = m_project.tracks().at(item.toTrack);
            const drift::TimeUs clampedStart = drift::clampClipStartNoOverlap(destTrack, movedIds, item.newTimelineStart,
                                                                              item.clip.timelineDuration);
            if (clampedStart > item.newTimelineStart) {
                const drift::TimeUs push = clampedStart - item.newTimelineStart;
                if (push > maxPushRight)
                    maxPushRight = push;
            }
        }
        if (maxPushRight > 0) {
            for (ClipToMove &item : toMove) {
                item.newTimelineStart += maxPushRight;
            }
        }

        const auto slotFree = [&](int trackIdx, const ClipToMove &self) {
            const drift::TimeUs end = self.newTimelineStart + self.clip.timelineDuration;
            for (const drift::Clip &other : m_project.tracks().at(trackIdx).clips) {
                if (!movedIds.contains(other.id) && self.newTimelineStart < other.timelineEnd()
                    && other.timelineStart < end)
                    return false;
            }
            for (const ClipToMove &other : toMove) {
                if (other.clip.id != self.clip.id && other.toTrack == trackIdx
                    && self.newTimelineStart < other.newTimelineStart + other.clip.timelineDuration
                    && other.newTimelineStart < end)
                    return false;
            }
            return true;
        };
        for (ClipToMove &item : toMove) {
            if (!item.followsVideo || slotFree(item.toTrack, item))
                continue;
            int found = -1;
            for (int t = 0; t < m_project.tracks().size() && found < 0; ++t) {
                const drift::Track &candidate = m_project.tracks().at(t);
                if (t != item.toTrack && candidate.type == drift::TrackType::Audio && !candidate.locked
                    && slotFree(t, item))
                    found = t;
            }
            if (found < 0) {
                drift::Track audioTrack;
                audioTrack.type = drift::TrackType::Audio;
                m_project.tracks().append(audioTrack);
                found = m_project.tracks().size() - 1;
            }
            item.toTrack = found;
        }
    }

    // For clips that stay on the same track, update their timeline start in place
    for (const ClipToMove &item : toMove) {
        if (item.fromTrack == item.toTrack) {
            m_project.tracks()[item.fromTrack].clips[item.fromClipIndex].timelineStart = item.newTimelineStart;
        }
    }

    // Removals for clips that change tracks, in descending clip index order
    QList<ClipToMove> removals;
    for (const ClipToMove &item : toMove) {
        if (item.fromTrack != item.toTrack)
            removals.append(item);
    }
    std::sort(removals.begin(), removals.end(), [](const ClipToMove &a, const ClipToMove &b) {
        if (a.fromTrack != b.fromTrack)
            return a.fromTrack < b.fromTrack;
        return a.fromClipIndex > b.fromClipIndex;
    });
    for (const ClipToMove &item : removals) {
        m_project.tracks()[item.fromTrack].clips.removeAt(item.fromClipIndex);
    }

    // Append clips changing tracks to their new destination tracks
    for (const ClipToMove &item : toMove) {
        if (item.fromTrack != item.toTrack) {
            drift::Clip moved = item.clip;
            moved.timelineStart = item.newTimelineStart;
            m_project.tracks()[item.toTrack].clips.append(moved);
        }
    }

    // Selection is re-resolved by clip id after the lanes below settle: moving a pinned adjustment
    // can insert tracks and shift every index taken here.
    for (const ClipToMove &item : toMove)
        syncLinkedPartnersFrom(m_project, item.clip, movedIds);
    for (const ClipToMove &item : toMove) {
        if (!item.clip.linkLoose)
            continue;
        const drift::TimeUs delta = item.newTimelineStart - item.clip.timelineStart;
        if (delta == 0)
            continue;
        for (const drift::ClipRef &ref : drift::linkedPartners(m_project, item.clip)) {
            drift::Clip &partner = m_project.tracks()[ref.trackIndex].clips[ref.clipIndex];
            if (movedIds.contains(partner.id))
                continue;
            partner.timelineStart = qMax<drift::TimeUs>(0, partner.timelineStart + delta);
            movedIds.insert(partner.id);
        }
    }
    for (const ClipToMove &item : toMove) {
        if (item.fromTrack != item.toTrack)
            drift::moveLinkedAdjustmentsToHostTrack(m_project, item.clip.id);
    }

    if (editCreatesCompleteOverlap(before, m_project)) {
        m_project = before;
        setLastMessage(tr("A clip can't completely cover another"), QStringLiteral("warning"));
        return;
    }
    QList<QPair<int, int>> newSelection;
    int selectedT = -1;
    int selectedC = -1;
    for (const ClipToMove &item : toMove) {
        int t = -1;
        int c = -1;
        if (!findClipById(m_project, item.clip.id, &t, &c))
            continue;
        newSelection.append(qMakePair(t, c));
        if (item.clip.id == leaderId) {
            selectedT = t;
            selectedC = c;
        }
    }
    m_selection = newSelection;
    if (selectedT >= 0 && selectedC >= 0) {
        m_selectedTrack = selectedT;
        m_selectedClip = selectedC;
    } else if (!newSelection.isEmpty()) {
        m_selectedTrack = newSelection.constFirst().first;
        m_selectedClip = newSelection.constFirst().second;
    }
    m_selectedTransitionTrack = -1;
    m_selectedTransitionLeftClip = -1;

    m_undoStack.beginMacro(tr("Clips moved"));
    pushProjectEdit(before, tr("Clips moved"));
    autoCrossfadeNewOverlaps(before);
    m_undoStack.endMacro();
    finishEdit(tr("Clips moved"));
}
