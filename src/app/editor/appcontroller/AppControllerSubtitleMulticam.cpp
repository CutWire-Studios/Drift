#include "app/editor/AppController.h"
#include "app/editor/appcontroller/AppControllerInternal.h"
#include "app/editor/AppControllerDetail.h"
#include "ui/preview/PreviewController.h"

#include "app/online/addons/AddonManager.h"
#include "engine/render2d/pdf/PdfClipRenderer.h"
#include "engine/render2d/pdf/PdfiumRuntime.h"
#include "app/bin/AssetLibrary.h"
#include "app/platform/FileDialogs.h"
#include "core/project/clip/Clip.h"
#include "core/effects/Mask.h"
#include "core/animation/SpeedCurve.h"
#include "core/animation/EasingPresetStore.h"
#include "core/interchange/premiere/PrprojReader.h"
#include "core/interchange/premiere/MogrtReader.h"
#include "core/interchange/KdenliveReader.h"
#include "core/interchange/ResolveReader.h"
#include "core/interchange/EdlReader.h"
#include "core/interchange/OtioReader.h"
#include "core/shapes/ShapePath.h"
#include "core/project/SubtitleCue.h"
#include "core/interchange/subtitles/SrtIO.h"
#include "core/interchange/subtitles/VttIO.h"
#include "core/project/sources/DotLottie.h"
#include "core/text/animation/LottieTextImport.h"
#include "core/text/animation/TextAnimationPreset.h"
#include "core/text/TextLook.h"
#include "core/text/TextPresetStore.h"
#include "core/edit/TimelineOps.h"
#include "core/edit/CutOps.h"
#include "core/project/Transition.h"
#include "core/edit/commands/ProjectCommands.h"
#include "engine/addons/AddonRegistry.h"
#include "engine/platform/AndroidUri.h"
#include "engine/audio/AudioMixer.h"
#include "engine/decode/readers/ClipReaderPool.h"
#include "engine/render3d/editor/ClipGizmo.h"
#include "engine/render3d/scene/ClipTransform3d.h"
#include "engine/compositor/PreviewSnap.h"
#include "engine/compositor/layers/TransformLayer.h"
#include "engine/diagnostics/DebugReport.h"
#include "engine/project/ProjectDependencies.h"
#include "engine/packages/audio/AudioEffectCatalog.h"
#include "engine/packages/effects/EffectCatalog.h"
#include "engine/packages/effects/EffectTemplateCatalog.h"
#include "engine/packages/looks/LookCatalog.h"
#include "engine/encode/export/Exporter.h"
#include "engine/packages/assets/EmojiCatalog.h"
#include "engine/packages/assets/FontCatalog.h"
#include "engine/compositor/FrameCompositor.h"
// Engine-internal by its own header comment, and included here only for releaseCaches(): the GPU
// caches have no other owner outside src/engine to ask. A one-line forwarder on GpuCompositor
// would restore the boundary.
#include "engine/gpu/GlRuntime.h"
#include "engine/color/CubeLut.h"
#include "engine/media/thumbnails/MediaThumbnail.h"
#include "engine/ml/vision/detection/ObjectDetector.h"
#include "engine/ml/runtime/OrtRuntime.h"
#include "engine/compositor/layers/MaskApplier.h"
#include "engine/encode/writers/MatteWriter.h"
#include "engine/encode/MediaEditor.h"
#include "engine/audio/analysis/AudioOnsets.h"
#include "engine/audio/analysis/LoudnessMeter.h"
#include "engine/decode/probe/MediaProbe.h"
#include "engine/media/waveforms/MediaWaveform.h"
#include "engine/face/tracking/FaceLandmarker.h"
#include "engine/face/props/FacePropCatalog.h"
#include "engine/face/props/FacePropImport.h"
#include "engine/face/swap/FaceSwapSource.h"
#include "engine/face/tracking/FaceTrack.h"
#include "engine/ml/vision/depth/DepthSidecar.h"
#include "engine/ml/vision/depth/VdaDepth.h"
#include "engine/render3d/models/ModelAsset.h"
#include "engine/render3d/models/ModelClipRenderer.h"
#include "engine/render3d/models/ModelClipTransform.h"
#include "engine/render3d/splats/SplatAsset.h"
#include "engine/encode/proxies/ProxyNaming.h"
#include "engine/encode/proxies/ReverseProxyCache.h"
#include "engine/render2d/vector/VectorClipRenderer.h"
#include "engine/render2d/vector/VectorInspect.h"
#include "playback/stats/PerfLog.h"
#include "playback/stats/PlaybackDiagnostics.h"
#include "engine/encode/proxies/ReverseRenderer.h"
#include "engine/ml/vision/segmentation/Sam2Segmenter.h"
#include "engine/packages/assets/StickerCatalog.h"
#include "engine/decode/probe/StillImage.h"
#include "ui/imageproviders/multicam/MulticamImageStore.h"
#include "ui/timeline/ClipThumbnailStore.h"
#include "engine/render2d/text/TextLayout.h"
#ifdef DRIFT_WITH_SKIA
#include "engine/render2d/text/SkiaTextPainter.h"
#endif
#include "engine/packages/transitions/TransitionCatalog.h"
#include "app/jobs/JobRegistry.h"
#include "app/online/CloudProviders.h"
#include "engine/ml/speech/transcription/WhisperTranscriber.h"
#include "app/mcp/McpDetail.h"

#include <QBuffer>
#include <QClipboard>
#include <QColor>
#include <QCoreApplication>
#include <QPointer>
#include <QCryptographicHash>
#include <QCursor>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QMutex>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QOpenGLContext>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QAudioDevice>
#include <QThreadPool>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QElapsedTimer>
#include <QSettings>
#include <QStorageInfo>
#ifdef Q_OS_MACOS
#include <sys/mount.h>
#endif
#include <QByteArray>
#include <QSet>
#include <QStandardPaths>
#include <QScopedValueRollback>
#include <QTimer>
#include <QUrl>
#include <QDebug>
#include <QUuid>
#include <QVector>
#include <QFutureWatcher>
#include <numeric>
#include <QtConcurrent>
#include <QtMath>
#include <algorithm>
#include <climits>
#include <cmath>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <utility>

using namespace drift::appdetail;
using drift::mcpdetail::findClipById;
using drift::mcpdetail::runBeatAnalysis;
using namespace drift::appcontroller_detail;

namespace {

// Offline scans read a file end to end at their own pace. They need decode cursors of their own so
// they never share one with timeline playback of the same media — see ClipReaderPool.
constexpr quint64 kSubtitleScanStreamId = 0xA5'11'5C'A4'00'00'00'01ull;

} // namespace

void AppController::addTextClip(const QString &text, double atSeconds, const QString &presetId,
                                int requestedTrack)
{
    const QString trimmed = text.trimmed();
    // Adding with no text is the "drop it in, then type on the preview" path:
    // the clip gets placeholder words and the preview opens an inline editor on
    // it. Passing text keeps the original behaviour.
    const bool placeholder = trimmed.isEmpty();
    const QString content = placeholder ? tr("Your text here") : trimmed;

    const drift::Project before = m_project;
    const bool requestedFits = requestedTrack >= 0 && requestedTrack < m_project.tracks().size()
                               && m_project.tracks().at(requestedTrack).allowsClipType(drift::ClipType::Text);
    const int trackIndex = requestedFits
        ? requestedTrack
        : drift::ensureTrackForClipType(m_project, drift::ClipType::Text, true);
    if (trackIndex < 0)
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    const drift::TimeUs startSeconds = atSeconds < 0.0 ? m_playheadUs : drift::secondsToUs(atSeconds);
    const drift::TimeUs start = drift::resolveClipStart(m_project, track, -1, startSeconds,
                                                        drift::kTextClipDurationUs, m_snapEnabled, m_playheadUs);

    drift::Clip clip;
    clip.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    clip.type = drift::ClipType::Text;
    clip.name = content.left(32);
    clip.textContent = content;
    clip.timelineStart = start;
    clip.timelineDuration = drift::kTextClipDurationUs;
    clip.srcIn = 0;
    clip.srcOut = drift::kTextClipDurationUs;
    if (!presetId.isEmpty()) {
        if (const std::optional<drift::TextStyle> preset = drift::textStyleForPresetId(presetId)) {
            clip.textStyle = *preset;
            clip.textStyle.packId = presetId;
        }
    }
    applyDefaultVisualLayout(clip, m_project.width(), m_project.height());

    track.clips.append(clip);
    const int newClipIndex = track.clips.size() - 1;
    pushProjectEdit(before, tr("Text clip added"));
    finishEdit(tr("Text clip added"));
    selectClip(trackIndex, newClipIndex);

    if (placeholder) {
        // resolveClipStart pushes the clip past anything already occupying the
        // playhead, so park the playhead on it: the preview can only show (and
        // edit) a clip that spans the current time.
        if (m_playheadUs < start || m_playheadUs >= start + drift::kTextClipDurationUs)
            setPlayheadSeconds(drift::usToSeconds(start));
        emit inlineTextEditRequested(trackIndex, newClipIndex);
    }
}

void AppController::addSubtitleClip(double atSeconds)
{
    const drift::Project before = m_project;
    const int trackIndex =
        drift::ensureTrackForClipType(m_project, drift::ClipType::Subtitle, true);
    if (trackIndex < 0)
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    const drift::TimeUs startSeconds = atSeconds < 0.0 ? m_playheadUs : drift::secondsToUs(atSeconds);
    const drift::TimeUs start = drift::resolveClipStart(m_project, track, -1, startSeconds,
                                                        drift::kSubtitleClipDurationUs, m_snapEnabled,
                                                        m_playheadUs);

    drift::Clip clip;
    clip.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    clip.type = drift::ClipType::Subtitle;
    clip.name = tr("Subtitles");
    clip.timelineStart = start;
    clip.timelineDuration = drift::kSubtitleClipDurationUs;
    clip.srcIn = 0;
    clip.srcOut = drift::kSubtitleClipDurationUs;
    if (const std::optional<drift::TextStyle> preset = drift::textStyleForPresetId(QStringLiteral("subtitle")))
        clip.textStyle = *preset;
    applyDefaultVisualLayout(clip, m_project.width(), m_project.height());

    track.clips.append(clip);
    const int newClipIndex = track.clips.size() - 1;
    pushProjectEdit(before, tr("Subtitle clip added"));
    finishEdit(tr("Subtitle clip added"));
    selectClip(trackIndex, newClipIndex);
}
namespace {


bool isVttPath(const QString &path)
{
    return path.endsWith(QLatin1String(".vtt"), Qt::CaseInsensitive);
}

drift::TimeUs subtitleClipDurationForCues(const QList<drift::SubtitleCue> &cues)
{
    drift::TimeUs duration = drift::kSubtitleClipDurationUs;
    for (const drift::SubtitleCue &cue : cues)
        duration = qMax(duration, cue.endUs);
    return duration;
}

} // namespace

bool AppController::importSubtitleFile(const QUrl &url, double atSeconds)
{
    const QString path = url.toLocalFile();
    if (path.isEmpty()) {
        setLastMessage(tr("No subtitle file selected"));
        return false;
    }

    QList<drift::SubtitleCue> cues;
    QString error;
    if (!(isVttPath(path) ? drift::parseVttFile(path, &cues, &error)
                              : drift::parseSrtFile(path, &cues, &error))) {
        setLastMessage(error.isEmpty() ? tr("Could not read subtitle file") : error, QStringLiteral("error"));
        return false;
    }

    const drift::TimeUs duration = subtitleClipDurationForCues(cues);
    const drift::Project before = m_project;
    const int trackIndex =
        drift::ensureTrackForClipType(m_project, drift::ClipType::Subtitle, true);
    if (trackIndex < 0)
        return false;

    drift::Track &track = m_project.tracks()[trackIndex];
    const drift::TimeUs startSeconds = atSeconds < 0.0 ? m_playheadUs : drift::secondsToUs(atSeconds);
    const drift::TimeUs start =
        drift::resolveClipStart(m_project, track, -1, startSeconds, duration, m_snapEnabled,
                                m_playheadUs);

    drift::Clip clip;
    clip.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    clip.type = drift::ClipType::Subtitle;
    clip.timelineStart = start;
    clip.timelineDuration = duration;
    clip.srcIn = 0;
    clip.srcOut = duration;
    clip.subtitleCues = cues;
    clip.name = drift::subtitleClipName(cues);
    if (const std::optional<drift::TextStyle> preset = drift::textStyleForPresetId(QStringLiteral("subtitle")))
        clip.textStyle = *preset;
    applyDefaultVisualLayout(clip, m_project.width(), m_project.height());

    track.clips.append(clip);
    const int newClipIndex = track.clips.size() - 1;
    pushProjectEdit(before, tr("Subtitles imported"));
    finishEdit(tr("Subtitles imported"));
    selectClip(trackIndex, newClipIndex);
    setLastMessage(tr("Imported %n subtitles", "", int(cues.size())));
    return true;
}

bool AppController::importSubtitleFileIntoClip(int trackIndex, int clipIndex, const QUrl &url)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return false;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return false;

    drift::Clip &clip = track.clips[clipIndex];
    if (clip.type != drift::ClipType::Subtitle) {
        setLastMessage(tr("Select a subtitle clip to import into"), QStringLiteral("warning"));
        return false;
    }

    const QString path = url.toLocalFile();
    if (path.isEmpty()) {
        setLastMessage(tr("No subtitle file selected"));
        return false;
    }

    QList<drift::SubtitleCue> cues;
    QString error;
    if (!(isVttPath(path) ? drift::parseVttFile(path, &cues, &error)
                              : drift::parseSrtFile(path, &cues, &error))) {
        setLastMessage(error.isEmpty() ? tr("Could not read subtitle file") : error, QStringLiteral("error"));
        return false;
    }

    const drift::TimeUs duration = subtitleClipDurationForCues(cues);
    const drift::Project before = m_project;
    clip.subtitleCues = cues;
    clip.name = drift::subtitleClipName(cues);
    if (duration > clip.timelineDuration) {
        clip.timelineDuration = duration;
        clip.srcOut = duration;
    }
    pushProjectEdit(before, tr("Subtitles imported"));
    finishEdit(tr("Subtitles imported"));
    setLastMessage(tr("Imported %n subtitles", "", int(cues.size())));
    return true;
}

bool AppController::exportSubtitleFile(int trackIndex, int clipIndex, const QUrl &url, bool timelineTimes)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return false;

    const drift::Track &track = m_project.tracks().at(trackIndex);
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return false;

    const drift::Clip &clip = track.clips.at(clipIndex);
    if (clip.type != drift::ClipType::Subtitle) {
        setLastMessage(tr("Select a subtitle clip to export"), QStringLiteral("warning"));
        return false;
    }
    if (clip.subtitleCues.isEmpty()) {
        setLastMessage(tr("This subtitle clip has no captions"), QStringLiteral("warning"));
        return false;
    }

    const QString path = url.toLocalFile();
    if (path.isEmpty()) {
        setLastMessage(tr("No save location selected"));
        return false;
    }

    QList<drift::SubtitleCue> cues = clip.subtitleCues;
    if (timelineTimes) {
        for (drift::SubtitleCue &cue : cues) {
            cue.startUs += clip.timelineStart;
            cue.endUs += clip.timelineStart;
        }
    }

    QString error;
    if (!(isVttPath(path) ? drift::writeVttFile(path, cues, &error)
                         : drift::writeSrtFile(path, cues, &error))) {
        setLastMessage(error.isEmpty() ? tr("Could not write subtitle file") : error, QStringLiteral("error"));
        return false;
    }

    setLastMessage(tr("Subtitles saved"), QStringLiteral("success"));
    return true;
}

void AppController::cancelSubtitleGeneration()
{
    if (m_subtitleGenerating)
        m_subtitleGenCancel.storeRelaxed(1);
}

QVariantList AppController::whisperLanguages()
{
    QVariantList out;
    QVariantMap autoRow;
    autoRow.insert(QStringLiteral("code"), QString());
    autoRow.insert(QStringLiteral("label"), tr("Auto-detect"));
    out.append(autoRow);

    const QVariantList langs = drift::WhisperTranscriber::instance().supportedLanguages();
    for (const QVariant &row : langs)
        out.append(row);
    return out;
}

void AppController::generateSubtitlesForClip(int trackIndex, int clipIndex, const QString &language,
                                             int maxWordsPerCue)
{
    if (!isValidClipIndex(trackIndex, clipIndex))
        return;
    const drift::Clip &clip = m_project.tracks().at(trackIndex).clips.at(clipIndex);
    if (clip.type != drift::ClipType::Video && clip.type != drift::ClipType::Audio) {
        setLastMessage(tr("Select a video or audio clip to create captions"), QStringLiteral("warning"));
        return;
    }
    generateSubtitlesForSources({subtitleSourceFromClip(clip)}, language, maxWordsPerCue);
}

bool AppController::generateSubtitlesForSelection(const QString &language, int maxWordsPerCue)
{
    QList<QPair<int, int>> pairs = m_selection;
    if (pairs.isEmpty() && m_selectedTrack >= 0 && m_selectedClip >= 0)
        pairs.append(qMakePair(m_selectedTrack, m_selectedClip));
    return generateSubtitlesForClips(pairs, language, maxWordsPerCue);
}

bool AppController::generateSubtitlesForClips(const QList<QPair<int, int>> &pairs, const QString &language,
                                              int maxWordsPerCue)
{
    // Selecting a clip pulls in its linked A/V partner, which carries the same speech over the
    // same span. Keep one per link, the audio side when there is one, since that is what plays.
    QList<drift::Clip> clips;
    for (const QPair<int, int> &pair : pairs) {
        if (!isValidClipIndex(pair.first, pair.second))
            continue;
        const drift::Clip &clip = m_project.tracks().at(pair.first).clips.at(pair.second);
        if (clip.type != drift::ClipType::Video && clip.type != drift::ClipType::Audio) {
            setLastMessage(tr("Select video or audio clips to create captions"), QStringLiteral("warning"));
            return false;
        }
        auto partner = std::find_if(clips.begin(), clips.end(), [&clip](const drift::Clip &taken) {
            return !clip.linkId.isEmpty() && taken.linkId == clip.linkId;
        });
        if (partner == clips.end())
            clips.append(clip);
        else if (clip.type == drift::ClipType::Audio)
            *partner = clip;
    }
    if (clips.isEmpty()) {
        setLastMessage(tr("Select a video or audio clip to create captions"), QStringLiteral("warning"));
        return false;
    }

    QList<SubtitleSource> sources;
    for (const drift::Clip &clip : clips)
        sources.append(subtitleSourceFromClip(clip));
    return generateSubtitlesForSources(sources, language, maxWordsPerCue);
}

bool AppController::generateSubtitlesForRange(int trackIndex, double startSec, double endSec,
                                              const QString &language, int maxWordsPerCue)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return false;
    const drift::TimeUs rangeStart = drift::secondsToUs(startSec);
    const drift::TimeUs rangeEnd = drift::secondsToUs(endSec);
    if (rangeEnd <= rangeStart) {
        setLastMessage(tr("The caption range is empty"), QStringLiteral("warning"));
        return false;
    }

    QList<SubtitleSource> sources;
    for (const drift::Clip &clip : m_project.tracks().at(trackIndex).clips) {
        if (clip.type != drift::ClipType::Video && clip.type != drift::ClipType::Audio)
            continue;
        const drift::TimeUs from = qMax(rangeStart, clip.timelineStart);
        const drift::TimeUs to = qMin(rangeEnd, clip.timelineEnd());
        if (to <= from)
            continue;
        SubtitleSource source = subtitleSourceFromClip(clip);
        const drift::TimeUs a = clip.timelineToSourceUs(from);
        const drift::TimeUs b = clip.timelineToSourceUs(to);
        source.srcIn = qMin(a, b);
        source.srcOut = qMax(a, b);
        source.timelineStart = from;
        source.timelineDuration = to - from;
        sources.append(source);
    }
    if (sources.isEmpty()) {
        setLastMessage(tr("No video or audio clips in that range"), QStringLiteral("warning"));
        return false;
    }
    return generateSubtitlesForSources(sources, language, maxWordsPerCue);
}

AppController::SubtitleSource AppController::subtitleSourceFromClip(const drift::Clip &clip)
{
    return {clip.path,           clip.srcIn,           clip.srcOut, clip.timelineStart,
            clip.timelineDuration, clip.effectiveSpeed(), clip.reverse, clip.assetId};
}

std::optional<QList<drift::SubtitleCue>> AppController::cuesFromStoredTranscripts(
    const QList<SubtitleSource> &sources, drift::TimeUs rangeStart, int maxWordsPerCue) const
{
    QList<drift::SubtitleCue> mapped;
    for (const SubtitleSource &source : sources) {
        const drift::TranscriptPtr t = m_project.transcript(source.assetId);
        if (!t || !t->source.matches(source.path))
            return std::nullopt;
        const drift::TimeUs offset = source.timelineStart - rangeStart;
        const double speed = source.speed > 0 ? source.speed : 1.0;
        for (const drift::SubtitleCue &cue :
             drift::cuesFromTranscript(*t, source.srcIn, source.srcOut, 42, 1, qMax(0, maxWordsPerCue))) {
            const double a = drift::usToSeconds(cue.startUs - source.srcIn);
            const double b = drift::usToSeconds(cue.endUs - source.srcIn);
            const double span = drift::usToSeconds(source.srcOut - source.srcIn);
            const double tlStart = source.reverse ? (span - b) / speed : a / speed;
            const double tlEnd = source.reverse ? (span - a) / speed : b / speed;
            drift::SubtitleCue m;
            m.startUs = offset + qBound<drift::TimeUs>(0, drift::secondsToUs(tlStart), source.timelineDuration);
            m.endUs = offset + qBound<drift::TimeUs>(0, drift::secondsToUs(tlEnd), source.timelineDuration);
            m.text = cue.text;
            m.source = source.assetId.isEmpty() ? source.path : source.assetId;
            if (m.endUs > m.startUs)
                mapped.append(m);
        }
    }
    drift::sortSubtitleCues(mapped);
    return mapped;
}

bool AppController::generateSubtitlesForSources(QList<SubtitleSource> sources, const QString &language,
                                                int maxWordsPerCue)
{
    if (m_subtitleGenerating) {
        setLastMessage(tr("Subtitle generation already in progress"), QStringLiteral("warning"));
        return false;
    }
    if (sources.isEmpty())
        return false;
    for (const SubtitleSource &source : sources) {
        if (source.path.isEmpty() || source.srcOut <= source.srcIn) {
            setLastMessage(sources.size() == 1 ? tr("This clip has no sound")
                                               : tr("One of these clips has no sound"),
                           QStringLiteral("warning"));
            return false;
        }
    }
    std::sort(sources.begin(), sources.end(), [](const SubtitleSource &a, const SubtitleSource &b) {
        return a.timelineStart < b.timelineStart;
    });
    // Overlapping sources would be two voices over the same instant; transcribed back to back they
    // come out as captions that belong to neither.
    for (int i = 1; i < sources.size(); ++i) {
        const SubtitleSource &prev = sources.at(i - 1);
        if (sources.at(i).timelineStart < prev.timelineStart + prev.timelineDuration) {
            setLastMessage(tr("These clips overlap in time — caption them separately"),
                           QStringLiteral("warning"));
            return false;
        }
    }

    setPlaying(false);

    // Media already transcribed with word timings: caption from that, no Whisper pass, and the
    // cues land exactly on the words however the clips were cut since.
    {
        const drift::TimeUs rangeStart = sources.first().timelineStart;
        drift::TimeUs rangeEnd = rangeStart;
        for (const SubtitleSource &source : sources)
            rangeEnd = qMax(rangeEnd, source.timelineStart + source.timelineDuration);
        if (const auto stored = cuesFromStoredTranscripts(sources, rangeStart, maxWordsPerCue)) {
            if (stored->isEmpty()) {
                setLastMessage(tr("No speech detected"), QStringLiteral("warning"));
                return false;
            }
            finalizeGeneratedSubtitles(rangeStart, rangeEnd - rangeStart, *stored);
            return true;
        }
    }

    m_subtitleGenCancel.storeRelaxed(0);
    m_subtitleGenProgress = 0.0;
    emit subtitleGenProgressChanged();
    m_subtitleGenStatus = tr("Starting…");
    emit subtitleGenStatusChanged();
    m_subtitleGenerating = true;
    emit subtitleGeneratingChanged();
    setLastMessage(tr("Creating captions…"));

    const drift::TimeUs rangeStart = sources.first().timelineStart;
    drift::TimeUs rangeEnd = rangeStart;
    for (const SubtitleSource &source : sources)
        rangeEnd = qMax(rangeEnd, source.timelineStart + source.timelineDuration);
    const drift::TimeUs rangeDuration = rangeEnd - rangeStart;
    const QString languageCode = language.trimmed().toLower();
    const int wordsPerCue = std::max(0, maxWordsPerCue);

    (void)QtConcurrent::run([this, sources, rangeStart, rangeDuration, languageCode, wordsPerCue]() {
        auto setProgress = [this](double fraction, const QString &status) {
            QMetaObject::invokeMethod(
                this,
                [this, fraction, status]() {
                    m_subtitleGenProgress = fraction;
                    emit subtitleGenProgressChanged();
                    if (!status.isEmpty() && status != m_subtitleGenStatus) {
                        m_subtitleGenStatus = status;
                        emit subtitleGenStatusChanged();
                    }
                },
                Qt::QueuedConnection);
        };

        auto finish = [this, rangeStart, rangeDuration](bool ok, const QString &message,
                                                        const QList<drift::SubtitleCue> &cues) {
            QMetaObject::invokeMethod(
                this,
                [this, ok, message, cues, rangeStart, rangeDuration]() {
                    m_subtitleGenerating = false;
                    emit subtitleGeneratingChanged();
                    m_subtitleGenProgress = ok ? 1.0 : 0.0;
                    emit subtitleGenProgressChanged();
                    m_subtitleGenStatus = ok ? tr("Done") : message;
                    emit subtitleGenStatusChanged();
                    if (!ok || cues.isEmpty()) {
                        setLastMessage(message, QStringLiteral("error"));
                        emit subtitleGenerationFinished(false, message);
                        return;
                    }
                    finalizeGeneratedSubtitles(rangeStart, rangeDuration, cues);
                },
                Qt::QueuedConnection);
        };

        setProgress(0.02, tr("Getting speech recognition ready…"));
        drift::WhisperTranscriber &whisper = drift::WhisperTranscriber::instance();
        if (!whisper.available()) {
            qWarning() << "[subtitles] whisper unavailable:" << whisper.lastError();
            finish(false, whisper.lastError(), {});
            return;
        }

        // Decode every source's raw audio over [srcIn, srcOut] at 16 kHz mono, back to back into
        // one buffer so Whisper runs once; spans remember where each source landed in it.
        setProgress(0.05, tr("Reading audio…"));
        const int rate = 16000;
        const int chunkFrames = 30 * rate;
        drift::TimeUs totalSpanUs = 0;
        for (const SubtitleSource &source : sources)
            totalSpanUs += source.srcOut - source.srcIn;
        totalSpanUs = std::max<drift::TimeUs>(1, totalSpanUs);

        struct Span
        {
            size_t sampleStart;
            size_t sampleEnd;
        };
        std::vector<float> mono;
        std::vector<Span> spans;
        drift::TimeUs decodedUs = 0;
        for (const SubtitleSource &source : sources) {
            const size_t spanStart = mono.size();
            drift::TimeUs pos = source.srcIn;
            while (pos < source.srcOut) {
                if (m_subtitleGenCancel.loadRelaxed()) {
                    finish(false, tr("Subtitle generation cancelled"), {});
                    return;
                }
                const drift::TimeUs remainUs = source.srcOut - pos;
                const int frames =
                    qMin<int64_t>(chunkFrames, (remainUs * rate) / drift::kUsPerSecond + 1);
                if (frames <= 0)
                    break;
                QVector<float> stereo(static_cast<qsizetype>(frames) * 2);
                // Its own decode cursor: this scan walks the file at its own pace while playback
                // may be reading the same file, and a shared cursor would corrupt both.
                const int got = ClipReaderPool::instance().readAudioInterleaved(
                    source.path, kSubtitleScanStreamId, pos, frames, rate, stereo.data());
                if (got <= 0)
                    break;
                const size_t base = mono.size();
                mono.resize(base + got);
                for (int i = 0; i < got; ++i)
                    mono[base + i] = 0.5f * (stereo[i * 2] + stereo[i * 2 + 1]);
                const drift::TimeUs step =
                    static_cast<drift::TimeUs>((static_cast<int64_t>(got) * drift::kUsPerSecond) / rate);
                pos += step;
                decodedUs += step;
                const double decodeFrac =
                    std::min(1.0, static_cast<double>(decodedUs) / static_cast<double>(totalSpanUs));
                setProgress(0.05 + 0.10 * decodeFrac,
                            tr("Reading audio… %1%").arg(qRound(100.0 * decodeFrac)));
            }
            spans.push_back({spanStart, mono.size()});
        }

        qWarning() << "[subtitles] decoded mono samples:" << mono.size()
                   << "seconds:" << (mono.size() / 16000.0) << "sources:" << sources.size()
                   << "language:" << (languageCode.isEmpty() ? QStringLiteral("auto") : languageCode);
        if (mono.empty()) {
            finish(false, tr("No audio decoded"), {});
            return;
        }

        setProgress(0.15, languageCode.isEmpty()
                              ? tr("Transcribing…")
                              : tr("Transcribing (%1)…").arg(languageCode));

        const drift::WhisperResult res = whisper.transcribe(
            mono,
            [this, setProgress](double fraction, const QString &status) {
                // Map Whisper's 0–1 into the remaining 15%–95% of the overall bar.
                setProgress(0.15 + 0.80 * fraction, status);
                return m_subtitleGenCancel.loadRelaxed() == 0;
            },
            languageCode, wordsPerCue);

        qWarning() << "[subtitles] transcribe done. ok:" << res.ok << "cancelled:" << res.cancelled
                   << "cues:" << res.cues.size() << "error:" << res.error;

        if (res.cancelled) {
            finish(false, tr("Subtitle generation cancelled"), {});
            return;
        }
        if (!res.ok) {
            finish(false, res.error, {});
            return;
        }

        setProgress(0.96, tr("Building caption track…"));

        auto sampleToUs = [rate](size_t sample) {
            return static_cast<drift::TimeUs>((static_cast<int64_t>(sample) * drift::kUsPerSecond) / rate);
        };

        // Each cue belongs to the source its start falls in. Its times are made relative to that
        // source, then mapped onto timeline time (accounting for speed and reverse) and clamped to
        // that source's slot, so a cue running across a join ends where its source does.
        QList<drift::SubtitleCue> mapped;
        for (const drift::SubtitleCue &cue : res.cues) {
            size_t spanIndex = spans.size() - 1;
            for (size_t i = 0; i < spans.size(); ++i) {
                if (cue.startUs < sampleToUs(spans[i].sampleEnd)) {
                    spanIndex = i;
                    break;
                }
            }
            const Span &span = spans[spanIndex];
            const SubtitleSource &source = sources.at(static_cast<qsizetype>(spanIndex));
            const drift::TimeUs spanStartUs = sampleToUs(span.sampleStart);
            const drift::TimeUs spanLengthUs = sampleToUs(span.sampleEnd) - spanStartUs;
            const double spanSec = drift::usToSeconds(source.srcOut - source.srcIn);
            const double srcStart = drift::usToSeconds(qBound<drift::TimeUs>(0, cue.startUs - spanStartUs, spanLengthUs));
            const double srcEnd = drift::usToSeconds(qBound<drift::TimeUs>(0, cue.endUs - spanStartUs, spanLengthUs));
            const double tlStart = source.reverse ? (spanSec - srcEnd) / source.speed : srcStart / source.speed;
            const double tlEnd = source.reverse ? (spanSec - srcStart) / source.speed : srcEnd / source.speed;
            const drift::TimeUs offset = source.timelineStart - rangeStart;
            const drift::TimeUs s =
                offset + qBound<drift::TimeUs>(0, drift::secondsToUs(tlStart), source.timelineDuration);
            const drift::TimeUs e =
                offset + qBound<drift::TimeUs>(0, drift::secondsToUs(tlEnd), source.timelineDuration);
            if (e > s) {
                drift::SubtitleCue m;
                m.startUs = s;
                m.endUs = e;
                m.text = cue.text;
                m.source = source.assetId.isEmpty() ? source.path : source.assetId;
                mapped.append(m);
            }
        }
        drift::sortSubtitleCues(mapped);

        qWarning() << "[subtitles] mapped cues:" << mapped.size()
                   << "range duration us:" << rangeDuration;

        if (mapped.isEmpty()) {
            finish(false, tr("No speech detected"), {});
            return;
        }
        finish(true, tr("Subtitles generated"), mapped);
    });
    return true;
}
namespace {


// --- Multicam ------------------------------------------------------------------------------
//
// The multicam window is a second view of the timeline, not a second timeline. An "angle" is an
// existing video track, synced the way the user already laid it out; the "program" is the track
// the cuts are written to. Both are held as indices and resolved against the live project on
// every read, so nothing here can go stale or drift out of step with the main window.


// Tiles decode into half the canvas, uniformly across angles. ClipReader resets its decode size
// on every read and drops the frame cache when it changes, so angles that asked for different
// boxes would evict each other's frames on every refresh.
constexpr double kMulticamTileScale = 0.5;
// ~12 Hz while playing. The main preview runs at project fps and shares the reader pool; tiles
// exist to show what each camera is doing, which does not need every frame.
constexpr int kMulticamPlaybackIntervalMs = 83;

// The grid reads the same files the compositor is playing, at its own positions. Salting the
// timeline's stream id gives each tile its own decode cursor, so scrubbing the grid cannot
// reseek the frame the preview is about to show — the same guard ClipPreviewPlayer uses.
constexpr quint64 kMulticamStreamSalt = 0x517CC1B727220A95ull;

int sortedInsertIndex(const drift::Track &track, drift::TimeUs startUs)
{
    int index = 0;
    while (index < track.clips.size() && track.clips.at(index).timelineStart <= startUs)
        ++index;
    return index;
}
} // namespace

bool AppController::beginMulticamSession()
{
    if (m_multicamActive)
        return true;

    QList<QPair<int, int>> selected = m_selection;
    if (selected.isEmpty() && m_selectedTrack >= 0 && m_selectedClip >= 0)
        selected.append(qMakePair(m_selectedTrack, m_selectedClip));

    QList<QPair<int, int>> videos;
    QSet<int> usedTracks;
    for (const QPair<int, int> &pair : selected) {
        if (!isValidClipIndex(pair.first, pair.second))
            continue;
        const drift::Clip &clip = m_project.tracks().at(pair.first).clips.at(pair.second);
        if (clip.type != drift::ClipType::Video)
            continue;
        if (usedTracks.contains(pair.first))
            continue;
        usedTracks.insert(pair.first);
        videos.append(pair);
    }
    std::sort(videos.begin(), videos.end(),
              [](const QPair<int, int> &a, const QPair<int, int> &b) { return a.first < b.first; });

    if (videos.size() < 2) {
        if (!multicamCanSetUp()) {
            setLastMessage(tr("Select at least two video clips on different tracks."),
                           QStringLiteral("warning"));
            return false;
        }
        m_multicamActive = true;
        if (!m_multicamTimer) {
            m_multicamTimer = new QTimer(this);
            m_multicamTimer->setInterval(kMulticamPlaybackIntervalMs);
            connect(m_multicamTimer, &QTimer::timeout, this, [this] { refreshMulticamTiles(); });
        }
        if (m_playing)
            m_multicamTimer->start();
        emit multicamChanged();
        return true;
    }

    return startMulticamPunching(videos);
}

bool AppController::startMulticamPunching(const QList<QPair<int, int>> &videoClips)
{
    m_multicamSnaps.clear();
    m_multicamCuts.clear();
    m_multicamRangeStart = std::numeric_limits<drift::TimeUs>::max();
    m_multicamRangeEnd = 0;

    for (const QPair<int, int> &pair : videoClips) {
        if (!isValidClipIndex(pair.first, pair.second))
            continue;
        const drift::Clip &clip = m_project.tracks().at(pair.first).clips.at(pair.second);
        MulticamAngleSnap snap;
        snap.trackIndex = pair.first;
        snap.clipId = clip.id;
        snap.original = clip;
        const QList<drift::ClipRef> partners = drift::linkedPartners(m_project, clip);
        for (const drift::ClipRef &ref : partners) {
            const drift::Clip &partner = m_project.tracks().at(ref.trackIndex).clips.at(ref.clipIndex);
            if (partner.type != drift::ClipType::Audio)
                continue;
            snap.audioTrackIndex = ref.trackIndex;
            snap.audioClipId = partner.id;
            snap.originalAudio = partner;
            snap.hasAudio = true;
            break;
        }
        m_multicamRangeStart = qMin(m_multicamRangeStart, clip.timelineStart);
        m_multicamRangeEnd = qMax(m_multicamRangeEnd, clip.timelineEnd());
        m_multicamSnaps.append(snap);
    }

    if (m_multicamSnaps.size() < 2) {
        m_multicamSnaps.clear();
        return false;
    }

    m_multicamBase = m_project.detachedCopy();
    m_multicamActive = true;
    if (!m_multicamTimer) {
        m_multicamTimer = new QTimer(this);
        m_multicamTimer->setInterval(kMulticamPlaybackIntervalMs);
        connect(m_multicamTimer, &QTimer::timeout, this, [this] { refreshMulticamTiles(); });
    }
    if (m_playing)
        m_multicamTimer->start();

    rebuildMulticamStaged();
    emit multicamChanged();
    refreshMulticamTiles();
    return true;
}

void AppController::rebuildMulticamStaged()
{
    m_multicamStaged = m_multicamBase.detachedCopy();
    if (!m_multicamCuts.isEmpty())
        applyMulticamSlicesToProject(m_multicamStaged, false);
    // Exclusive slices can land on a track that set-up muted so the stack would not roar;
    // the program has to be audible while punching.
    for (const MulticamAngleSnap &snap : m_multicamSnaps) {
        if (snap.trackIndex >= 0 && snap.trackIndex < m_multicamStaged.tracks().size())
            m_multicamStaged.tracks()[snap.trackIndex].muted = false;
    }
    m_playback.setProject(&m_multicamStaged);
    if (!m_playback.isPlaying())
        m_playback.setPlayheadUs(m_playheadUs);
}

void AppController::waitForMulticamRefresh()
{
    ++m_multicamGeneration;
    m_multicamRefreshing = false;
    if (m_multicamRefreshFuture.isRunning())
        m_multicamRefreshFuture.waitForFinished();
}

void AppController::endMulticamSession()
{
    if (!m_multicamActive)
        return;

    m_multicamActive = false;
    if (m_multicamTimer)
        m_multicamTimer->stop();
    waitForMulticamRefresh();
    MulticamImageStore::clear();
    m_multicamSnaps.clear();
    m_multicamCuts.clear();
    // Playback holds a bare pointer into the staged tree; switch it back before
    // that project is replaced, otherwise the compositor may still be reading it.
    m_playback.setProject(&m_project);
    if (!m_playback.isPlaying())
        m_playback.setPlayheadUs(m_playheadUs);
    m_multicamBase = drift::Project();
    m_multicamStaged = drift::Project();
    ++m_multicamRevision;
    emit multicamChanged();
    emit multicamFramesChanged();
}

QVariantList AppController::multicamAngles() const
{
    QVariantList out;
    out.reserve(m_multicamSnaps.size());
    const int active = multicamActiveAngle();
    for (int i = 0; i < m_multicamSnaps.size(); ++i) {
        const MulticamAngleSnap &snap = m_multicamSnaps.at(i);
        out.append(QVariantMap{
            {QStringLiteral("trackIndex"), snap.trackIndex},
            {QStringLiteral("label"), tr("Angle %1").arg(i + 1)},
            {QStringLiteral("clipName"), snap.original.name},
            {QStringLiteral("hasClip"), snap.original.containsTime(m_playheadUs)},
            {QStringLiteral("active"), i == active},
        });
    }
    return out;
}

quint64 AppController::multicamPlayheadSignature() const
{
    quint64 signature = static_cast<quint32>(multicamActiveAngle() + 1);
    const int count = qMin<int>(int(m_multicamSnaps.size()), 32);
    for (int i = 0; i < count; ++i) {
        if (m_multicamSnaps.at(i).original.containsTime(m_playheadUs))
            signature |= quint64(1) << (32 + i);
    }
    return signature;
}

int AppController::multicamActiveAngle() const
{
    if (m_multicamSnaps.isEmpty())
        return -1;
    return drift::multicamAngleAt(m_multicamCuts, m_multicamRangeStart, m_multicamRangeEnd,
                                  m_playheadUs, 0);
}

QVariantList AppController::multicamProgramClips() const
{
    QVariantList out;
    if (m_multicamSnaps.isEmpty())
        return out;
    const QList<drift::MulticamInterval> intervals =
        drift::multicamIntervals(m_multicamCuts, m_multicamRangeStart, m_multicamRangeEnd, 0);
    for (const drift::MulticamInterval &interval : intervals) {
        QString name;
        if (interval.angle >= 0 && interval.angle < m_multicamSnaps.size())
            name = m_multicamSnaps.at(interval.angle).original.name;
        out.append(QVariantMap{
            {QStringLiteral("start"), drift::usToSeconds(interval.startUs)},
            {QStringLiteral("duration"), drift::usToSeconds(interval.endUs - interval.startUs)},
            {QStringLiteral("name"), name},
            {QStringLiteral("angle"), interval.angle},
        });
    }
    return out;
}

void AppController::applyMulticamSlicesToProject(drift::Project &project, bool combined)
{
    struct AngleSlices {
        QList<drift::Clip> video;
        QList<drift::Clip> audio;
    };
    QList<AngleSlices> perAngle;
    perAngle.reserve(m_multicamSnaps.size());

    const QList<drift::MulticamInterval> intervals =
        drift::multicamIntervals(m_multicamCuts, m_multicamRangeStart, m_multicamRangeEnd, 0);

    for (int i = 0; i < m_multicamSnaps.size(); ++i) {
        AngleSlices slices;
        const MulticamAngleSnap &snap = m_multicamSnaps.at(i);
        const bool keepUnedited = m_multicamCuts.isEmpty() && i == 0;
        if (m_multicamCuts.isEmpty() && !combined) {
            // Separate save of an unedited session is a no-op; combined keeps only the topmost.
            perAngle.append(slices);
            continue;
        }
        if (keepUnedited || !m_multicamCuts.isEmpty()) {
            auto appendInterval = [&](drift::TimeUs startUs, drift::TimeUs endUs) {
                drift::Clip video;
                if (!drift::sliceClipToTimelineRange(snap.original, startUs, endUs, video))
                    return;
                video.id = newClipId();
                video.linkId.clear();
                if (snap.hasAudio) {
                    drift::Clip audio;
                    if (drift::sliceClipToTimelineRange(snap.originalAudio, startUs, endUs, audio)) {
                        audio.id = newClipId();
                        const QString link = newClipId();
                        video.linkId = link;
                        audio.linkId = link;
                        slices.audio.append(audio);
                    }
                }
                slices.video.append(video);
            };
            if (m_multicamCuts.isEmpty()) {
                appendInterval(snap.original.timelineStart, snap.original.timelineEnd());
            } else {
                for (const drift::MulticamInterval &interval : intervals) {
                    if (interval.angle != i)
                        continue;
                    appendInterval(interval.startUs, interval.endUs);
                }
            }
        }
        perAngle.append(slices);
    }

    auto replaceClip = [&project](int trackIndex, const QString &clipId, const QList<drift::Clip> &slices) {
        if (trackIndex < 0 || trackIndex >= project.tracks().size())
            return;
        drift::Track &track = project.tracks()[trackIndex];
        const int index = clipIndexById(track, clipId);
        if (index < 0)
            return;
        track.clips.removeAt(index);
        for (const drift::Clip &slice : slices) {
            const int at = sortedInsertIndex(track, slice.timelineStart);
            track.clips.insert(at, slice);
        }
    };

    if (combined) {
        QList<drift::Clip> flattened;
        for (const AngleSlices &slices : perAngle) {
            for (const drift::Clip &clip : slices.video)
                flattened.append(clip);
        }
        std::sort(flattened.begin(), flattened.end(),
                  [](const drift::Clip &a, const drift::Clip &b) {
                      return a.timelineStart < b.timelineStart;
                  });

        const int topTrack = m_multicamSnaps.first().trackIndex;
        const QString topId = m_multicamSnaps.first().clipId;
        for (int i = 1; i < m_multicamSnaps.size(); ++i)
            replaceClip(m_multicamSnaps.at(i).trackIndex, m_multicamSnaps.at(i).clipId, {});
        replaceClip(topTrack, topId, flattened);

        for (int i = 0; i < m_multicamSnaps.size(); ++i) {
            const MulticamAngleSnap &snap = m_multicamSnaps.at(i);
            if (!snap.hasAudio)
                continue;
            replaceClip(snap.audioTrackIndex, snap.audioClipId, perAngle.at(i).audio);
        }
        return;
    }

    for (int i = 0; i < m_multicamSnaps.size(); ++i) {
        const MulticamAngleSnap &snap = m_multicamSnaps.at(i);
        replaceClip(snap.trackIndex, snap.clipId, perAngle.at(i).video);
        if (snap.hasAudio)
            replaceClip(snap.audioTrackIndex, snap.audioClipId, perAngle.at(i).audio);
    }
}

bool AppController::multicamCanSetUp() const
{
    if (!m_assetLibrary)
        return false;

    // Building a rig rearranges the whole track stack, so it is only ever offered against a
    // timeline with nothing on it to lose. Once there are clips, the layout is the user's.
    for (const drift::Track &track : m_project.tracks()) {
        if (track.type != drift::TrackType::Audio && !track.clips.isEmpty())
            return false;
    }

    int cameras = 0;
    for (int i = 0; i < m_assetLibrary->count(); ++i) {
        if (m_assetLibrary->assetAt(i).value(QStringLiteral("kind")).toString()
            == QStringLiteral("video")) {
            ++cameras;
        }
    }
    return cameras >= 2;
}

void AppController::setUpMulticamFromAssets()
{
    if (!multicamCanSetUp())
        return;

    QList<int> cameras;
    for (int i = 0; i < m_assetLibrary->count(); ++i) {
        if (m_assetLibrary->assetAt(i).value(QStringLiteral("kind")).toString()
            == QStringLiteral("video")) {
            cameras.append(i);
        }
    }

    const drift::Project before = m_project;

    for (int i = m_project.tracks().size() - 1; i >= 0; --i) {
        const drift::Track &track = m_project.tracks().at(i);
        if (track.type == drift::TrackType::Video && track.clips.isEmpty())
            m_project.tracks().removeAt(i);
    }

    QList<QPair<int, int>> added;
    for (int n = 0; n < cameras.size(); ++n) {
        const int assetIndex = cameras.at(n);
        const QVariantMap asset = m_assetLibrary->assetAt(assetIndex);
        m_assetLibrary->ensureMedia(assetIndex);

        drift::Clip clip;
        clip.id = newClipId();
        clip.assetId = m_assetLibrary->assetIdAt(assetIndex);
        clip.type = drift::ClipType::Video;
        clip.name = asset.value(QStringLiteral("name")).toString();
        clip.path = asset.value(QStringLiteral("path")).toString();
        clip.thumbnailPath = m_assetLibrary->thumbnailAt(assetIndex);
        clip.filmstripPath = m_assetLibrary->filmstripAt(assetIndex);
        clip.timelineStart = 0;
        clip.timelineDuration = clipDurationForAssetIndex(assetIndex);
        clip.srcIn = 0;
        clip.srcOut = clip.timelineDuration;
        applyAssetLayout(clip, asset, m_project.width(), m_project.height());

        drift::Track track;
        track.type = drift::TrackType::Video;
        track.clips.append(clip);
        // Unmuted they would all play at once while the session is still unedited. The first
        // camera is left audible; the rest of a shoot usually shares that bed, or the user
        // drops in a proper one.
        track.muted = n > 0;
        m_project.tracks().append(track);
        added.append(qMakePair(m_project.tracks().size() - 1, 0));
    }

    pushProjectEdit(before, tr("Set up multicam"));
    finishEdit(tr("Set up multicam"));
    setLastMessage(tr("Multicam ready: %n camera(s) lined up at the start. Drag a clip to adjust "
                      "its sync, then pick a shot.",
                      nullptr, cameras.size()),
                   QStringLiteral("success"));

    if (m_multicamActive)
        startMulticamPunching(added);
    else
        emit multicamChanged();
}

drift::TimeUs AppController::multicamSwitchTimeUs() const
{
    const drift::TimeUs step = drift::frameDurationUs(m_projectFile->projectFps());
    if (step <= 0)
        return m_playheadUs;
    return ((m_playheadUs + step / 2) / step) * step;
}

void AppController::switchMulticamAngle(int angleIndex)
{
    if (!m_multicamActive || m_multicamSnaps.isEmpty())
        return;
    if (angleIndex < 0 || angleIndex >= m_multicamSnaps.size())
        return;

    const drift::TimeUs atUs = multicamSwitchTimeUs();
    const drift::MulticamSwitchResult result =
        drift::applyMulticamSwitch(m_multicamCuts, m_multicamRangeStart, m_multicamRangeEnd,
                                   angleIndex, atUs, 0);
    switch (result) {
    case drift::MulticamSwitchResult::NoOp:
        return;
    case drift::MulticamSwitchResult::OutOfRange:
        setLastMessage(tr("That angle has nothing at the current time."), QStringLiteral("warning"));
        return;
    case drift::MulticamSwitchResult::TooCloseToEdge:
        setLastMessage(tr("Too close to the edge of the shot to cut here."),
                       QStringLiteral("warning"));
        return;
    case drift::MulticamSwitchResult::Applied:
        break;
    }

    rebuildMulticamStaged();
    emit multicamChanged();
}

void AppController::saveMulticamAsSeparateTracks()
{
    if (!m_multicamActive || m_multicamSnaps.isEmpty()) {
        endMulticamSession();
        return;
    }
    if (m_multicamCuts.isEmpty()) {
        endMulticamSession();
        return;
    }

    const drift::Project before = m_project;
    applyMulticamSlicesToProject(m_project, false);
    for (const MulticamAngleSnap &snap : m_multicamSnaps) {
        if (snap.trackIndex >= 0 && snap.trackIndex < m_project.tracks().size())
            m_project.tracks()[snap.trackIndex].muted = false;
    }
    endMulticamSession();
    pushProjectEdit(before, tr("Save multicam as separate tracks"));
    finishEdit(tr("Save multicam as separate tracks"));
}

void AppController::saveMulticamCombined()
{
    if (!m_multicamActive || m_multicamSnaps.isEmpty()) {
        endMulticamSession();
        return;
    }

    const drift::Project before = m_project;
    applyMulticamSlicesToProject(m_project, true);
    if (!m_multicamSnaps.isEmpty() && m_multicamSnaps.first().trackIndex >= 0
        && m_multicamSnaps.first().trackIndex < m_project.tracks().size()) {
        m_project.tracks()[m_multicamSnaps.first().trackIndex].muted = false;
    }
    endMulticamSession();
    pushProjectEdit(before, tr("Save combined multicam"));
    finishEdit(tr("Save combined multicam"));
}

void AppController::refreshMulticamTiles()
{
    if (!m_multicamActive)
        return;
    // A refresh is already running. Dropping this one rather than queueing it means a machine
    // that cannot keep up shows fewer tile updates, not tile updates from further and further
    // in the past.
    if (m_multicamRefreshing)
        return;

    if (m_multicamSnaps.isEmpty()) {
        MulticamImageStore::clear();
        ++m_multicamRevision;
        emit multicamFramesChanged();
        return;
    }

    const int maxWidth = qMax(1, static_cast<int>(std::lround(m_project.width() * kMulticamTileScale)));
    const int maxHeight = qMax(1, static_cast<int>(std::lround(m_project.height() * kMulticamTileScale)));

    struct AngleRead
    {
        int angle = 0;
        QString path;
        quint64 streamId = 0;
        drift::TimeUs sourceUs = 0;
        int rotationCorrection = 0;
    };

    QList<AngleRead> reads;
    QList<int> emptyAngles;
    for (int i = 0; i < m_multicamSnaps.size(); ++i) {
        const drift::Clip &clip = m_multicamSnaps.at(i).original;
        if (!clip.containsTime(m_playheadUs) || clip.path.isEmpty()
            || clip.type != drift::ClipType::Video) {
            emptyAngles.append(i);
            continue;
        }
        reads.append(AngleRead{i, clip.path,
                               kMulticamStreamSalt ^ ClipReaderPool::streamIdForClip(clip.id),
                               clip.timelineToSourceUs(m_playheadUs), clip.rotationCorrection});
    }

    // Angles with nothing under the playhead clear immediately; there is no decode to wait for.
    for (const int angle : emptyAngles)
        MulticamImageStore::setTile(angle, QImage());

    if (reads.isEmpty()) {
        ++m_multicamRevision;
        emit multicamFramesChanged();
        return;
    }

    QList<ClipReaderPool::VideoRequest> requests;
    requests.reserve(reads.size());
    for (const AngleRead &read : reads)
        requests.append(ClipReaderPool::VideoRequest{read.path, read.streamId, read.sourceUs,
                                                     maxWidth, maxHeight, read.rotationCorrection});

    m_multicamRefreshing = true;
    const int generation = ++m_multicamGeneration;

    m_multicamRefreshFuture = QtConcurrent::run([this, reads, requests, maxWidth, maxHeight, generation]() {
        // Kicks every angle off on its own per-path worker thread, so the reads below hit each
        // reader's cache instead of decoding one camera after another.
        ClipReaderPool::instance().warmVideoFrames(requests);

        QList<QPair<int, QImage>> tiles;
        tiles.reserve(reads.size());
        for (const AngleRead &read : reads) {
            tiles.append(qMakePair(read.angle,
                                   ClipReaderPool::instance().readVideoFrame(
                                       read.path, read.streamId, read.sourceUs, maxWidth, maxHeight,
                                       read.rotationCorrection)));
        }

        QMetaObject::invokeMethod(this, [this, tiles, generation]() {
            // The playhead has moved on, or the session closed, since this was requested. The
            // flag belongs to whichever refresh is current now, so it is only ours to release
            // once this decode is confirmed to be that one — clearing it first would drop the
            // guard while a newer decode is still running, and let a third start alongside it.
            if (generation != m_multicamGeneration)
                return;
            m_multicamRefreshing = false;

            for (const auto &tile : tiles)
                MulticamImageStore::setTile(tile.first, tile.second);
            ++m_multicamRevision;
            emit multicamFramesChanged();
        }, Qt::QueuedConnection);
    });
}

void AppController::clearClipSpeedCurve(int trackIndex, int clipIndex)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;
    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;
    drift::Clip &clip = track.clips[clipIndex];
    if (!clip.hasSpeedCurve())
        return;

    const drift::Project before = m_project;
    clip.speedCurve.clear();
    // Keep the source range the user framed and let the scalar speed decide how long it takes,
    // rather than syncSrcOutFromSpeed's other direction — the retimed duration it would read
    // from is exactly the thing being discarded.
    clip.timelineDuration = qMax<drift::TimeUs>(
        1, llround(static_cast<double>(clip.srcOut - clip.srcIn) / clip.effectiveSpeed()));
    drift::retimeLinkedSubtitles(before, m_project);
    pushProjectEdit(before, tr("Speed curve removed"));
    finishEdit(tr("Speed curve removed"));
}

void AppController::setTransitionEasing(int trackIndex, const QString &transitionId,
                                        const QString &curve)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;
    drift::Transition *transition = findTransition(m_project.tracks()[trackIndex], transitionId);
    if (!transition)
        return;

    const drift::FadeCurve next = drift::fadeCurveFromString(curve);
    if (next == transition->easingCurve)
        return;

    const drift::Project before = m_project;
    transition->easingCurve = next;
    if (next != drift::FadeCurve::Custom)
        transition->easingShape.clear();
    if (drift::Transition *mirror = mirroredAudioTransition(m_project, trackIndex, *transition)) {
        mirror->easingCurve = transition->easingCurve;
        mirror->easingShape = transition->easingShape;
    }
    pushProjectEdit(before, tr("Transition curve"));
    finishEdit(tr("Transition curve updated"));
    emit selectedTransitionDataChanged();
    emitPreviewFrame();
}


QString AppController::addRenderedVideoAsset(const QString &path, const QString &name,
                                             const QString &folderId, const MediaInfo &info,
                                             const QString &undoText)
{
    const drift::Project before = m_project.detachedCopy();
    const QString assetId = insertRenderedVideoAsset(path, name, folderId, info);
    pushProjectEdit(before, undoText);
    finishEdit(undoText);
    return assetId;
}

QString AppController::insertRenderedVideoAsset(const QString &path, const QString &name,
                                                const QString &folderId, const MediaInfo &info)
{
    drift::MediaAsset asset;
    asset.kind = drift::MediaKind::Video;
    asset.path = path;
    asset.name = name;
    asset.durationUs = info.durationUs;
    for (const StreamInfo &stream : info.streams) {
        if (stream.type == StreamInfo::Type::Video && asset.width == 0) {
            asset.width = stream.width;
            asset.height = stream.height;
            asset.fps = stream.fps;
        } else if (stream.type == StreamInfo::Type::Audio) {
            asset.hasAudio = true;
        }
    }
    asset.hasAudioKnown = true;
    asset.frameRateKnown = true;
    asset.folderId = folderId;
    const QString assetId = m_project.addAsset(asset);
    if (m_assetLibrary) {
        m_assetLibrary->syncToProject();
        m_assetLibrary->ensureMedia(m_assetLibrary->indexOfId(assetId));
    }
    return assetId;
}

QString AppController::addRenderedAudioAsset(const QString &path, const QString &name,
                                             const QString &folderId, const MediaInfo &info,
                                             const QString &undoText)
{
    const drift::Project before = m_project.detachedCopy();
    drift::MediaAsset asset;
    asset.kind = drift::MediaKind::Audio;
    asset.path = path;
    asset.name = name;
    asset.durationUs = info.durationUs;
    asset.hasAudio = true;
    asset.hasAudioKnown = true;
    asset.folderId = folderId;
    const QString assetId = m_project.addAsset(asset);
    if (m_assetLibrary) {
        m_assetLibrary->syncToProject();
        m_assetLibrary->ensureMedia(m_assetLibrary->indexOfId(assetId));
    }
    pushProjectEdit(before, undoText);
    finishEdit(undoText);
    return assetId;
}
