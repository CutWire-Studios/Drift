#include "app/mcp/McpController.h"

#include "app/mcp/tools/McpCatalog.h"
#ifndef Q_OS_WASM
#include "app/mcp/transport/McpServer.h"
#endif
#include "app/editor/AppController.h"
#include "ui/preview/PreviewController.h"
#include "app/mcp/McpDetail.h"
#include "app/online/downloads/DownloadCenter.h"
#include "app/jobs/JobRegistry.h"
#include "app/jobs/audio/TtsController.h"
#include "app/jobs/audio/VoiceLibrary.h"
#include "engine/audio/AudioMixer.h"
#include "engine/decode/readers/ClipReaderPool.h"
#include "engine/compositor/FrameCompositor.h"
#include "engine/diagnostics/sheets/FrameSheet.h"
#include "engine/ml/vision/detection/SceneDetect.h"
#include "app/online/addons/AddonManager.h"
#include "app/bin/AssetLibrary.h"
#include "app/online/CloudProviders.h"
#include "core/edit/CutOps.h"
#include "core/edit/TimelineOps.h"
#include "engine/addons/AddonRegistry.h"
#include "engine/encode/writers/AudioFileWriter.h"
#include "engine/audio/analysis/AudioOnsets.h"
#include "engine/ml/audio/AudioTagging.h"
#include "engine/ml/speech/transcription/CtcAligner.h"
#include "engine/ml/speech/DeepFilterDenoiser.h"
#include "engine/face/tracking/FaceLandmarker.h"
#include "engine/face/tracking/FaceTrack.h"
#include "engine/ml/speech/transcription/LocalTranscription.h"
#include "engine/audio/analysis/LoudnessMeter.h"
#include "engine/media/waveforms/MediaWaveform.h"
#include "engine/ml/runtime/OrtRuntime.h"
#include "engine/ml/vision/segmentation/RvmMatter.h"
#include "engine/ml/vision/segmentation/Sam2Segmenter.h"
#include "engine/ml/speech/transcription/SileroVad.h"
#include "engine/ml/speech/diarization/SpeakerDiarizer.h"
#include "engine/ml/speech/SpeechAudio.h"
#include "engine/ml/vision/depth/VdaDepth.h"
#include "engine/diagnostics/sheets/WaveformSheet.h"
#include "engine/diagnostics/sheets/AudioSheet.h"
#include "engine/audio/analysis/AudioEvents.h"
#include "engine/audio/analysis/AudioSync.h"
#include "engine/ml/speech/transcription/WhisperTranscriber.h"
#include "app/mcp/protocol/McpJson.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>
#include <QtConcurrent>

#include <QClipboard>
#include <QFutureWatcher>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QPointer>
#include <QSettings>
#include <QImage>
#include <QSet>
#include <QTimer>
#include <QVector>

#include <cmath>
#include <limits>
#include <vector>

#include <algorithm>
#include <iterator>

using drift::mcpdetail::blockingSourcePeaks;
using drift::mcpdetail::imageResult;
using drift::mcpdetail::reduceRawPeaks;
using drift::mcpdetail::round2;
using drift::mcpdetail::round3;
using drift::mcpdetail::findClipById;
using drift::mcpdetail::runBeatAnalysis;
using drift::mcpdetail::sceneRowsFromAnalysis;

McpController::McpController(AppController *app, QObject *parent)
    : QObject(parent)
    , m_app(app)
    , m_imageLog(new AgentImageLog(this))
{
#ifndef Q_OS_WASM
    m_mcp = std::make_unique<drift::mcp::McpServer>(m_app);
    connect(m_mcp.get(), &drift::mcp::McpServer::runningChanged, this,
            &McpController::runningChanged);
    // The token, URL and setup snippets all notify on runningChanged; a rotation
    // changes the same set.
    connect(m_mcp.get(), &drift::mcp::McpServer::tokenChanged, this,
            &McpController::runningChanged);
    connect(m_mcp.get(), &drift::mcp::McpServer::errorChanged, this, &McpController::errorChanged);
    connect(m_mcp.get(), &drift::mcp::McpServer::toolCallStarted, this,
            [this](int id, const QString &tool, const QString &summary) {
                constexpr int kMaxToolLog = 500;
                m_toolLog.append(QVariantMap{{QStringLiteral("id"), id},
                                             {QStringLiteral("time"), QDateTime::currentMSecsSinceEpoch()},
                                             {QStringLiteral("tool"), tool},
                                             {QStringLiteral("summary"), summary},
                                             {QStringLiteral("pending"), true},
                                             {QStringLiteral("ok"), true},
                                             {QStringLiteral("error"), QString()},
                                             {QStringLiteral("ms"), 0}});
                while (m_toolLog.size() > kMaxToolLog)
                    m_toolLog.removeFirst();
                emit toolLogChanged();
                emit toolCallStarted(tool);
            });
    connect(m_mcp.get(), &drift::mcp::McpServer::toolCallFinished, this,
            [this](int id, bool ok, const QString &error, qint64 ms) {
                for (int i = m_toolLog.size() - 1; i >= 0; --i) {
                    QVariantMap entry = m_toolLog.at(i).toMap();
                    if (entry.value(QStringLiteral("id")).toInt() != id)
                        continue;
                    entry[QStringLiteral("pending")] = false;
                    entry[QStringLiteral("ok")] = ok;
                    entry[QStringLiteral("error")] = error;
                    entry[QStringLiteral("ms")] = ms;
                    m_toolLog[i] = entry;
                    emit toolLogChanged();
                    break;
                }
            });
    connect(m_mcp.get(), &drift::mcp::McpServer::toolCallResult, m_imageLog, &AgentImageLog::recordResult);
#endif
    // Loaded here so the property already reads correctly for anything constructed on
    // this object, but NOT acted on here — headless mode constructs the same
    // AppController/EditorState and configures the MCP server itself from CLI args
    // (port, token, transport); starting it early with the defaults would make that
    // later start() a no-op against the wrong port/token, and would start HTTP even
    // for a stdio-only headless run. applyStartOnLaunch() is the GUI-only opt-in,
    // called once from Main.qml's own startup sequence.
    m_mcpStartOnLaunch =
        QSettings().value(QStringLiteral("mcp/startOnLaunch"), false).toBool();
}

McpController::~McpController()
{
#ifndef Q_OS_WASM
    if (m_mcp)
        m_mcp->stop();
#endif
}

void McpController::clearToolLog()
{
    if (m_toolLog.isEmpty())
        return;
    m_toolLog.clear();
    emit toolLogChanged();
}

bool McpController::running() const
{
#ifndef Q_OS_WASM
    return m_mcp && m_mcp->running();
#else
    return false;
#endif
}

QString McpController::url() const
{
#ifndef Q_OS_WASM
    return m_mcp ? m_mcp->url() : QString();
#else
    return QString();
#endif
}

QString McpController::token() const
{
#ifndef Q_OS_WASM
    return m_mcp ? m_mcp->token() : QString();
#else
    return QString();
#endif
}

int McpController::port() const
{
#ifndef Q_OS_WASM
    return m_mcp ? int(m_mcp->port()) : 0;
#else
    return 0;
#endif
}

QString McpController::error() const
{
#ifndef Q_OS_WASM
    return m_mcp ? m_mcp->error() : QString();
#else
    return QString();
#endif
}

QString McpController::cursorSnippet() const
{
#ifndef Q_OS_WASM
    return m_mcp ? m_mcp->cursorSnippet() : QString();
#else
    return QString();
#endif
}

QString McpController::claudeCommand() const
{
#ifndef Q_OS_WASM
    return m_mcp ? m_mcp->claudeCommand() : QString();
#else
    return QString();
#endif
}

QString McpController::stdioSnippet() const
{
    const QJsonObject server{
        {QStringLiteral("command"), QCoreApplication::applicationFilePath()},
        {QStringLiteral("args"), QJsonArray{QStringLiteral("--mcp-stdio")}},
    };
    const QJsonObject root{
        {QStringLiteral("mcpServers"), QJsonObject{{QStringLiteral("drift"), server}}},
    };
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

void McpController::setEnabled(bool enabled)
{
#ifndef Q_OS_WASM
    if (!m_mcp)
        return;
    if (enabled) {
        m_mcp->start();
    } else {
        m_mcp->stop();
        // Turning access off is the security-relevant choice; carrying "start on
        // launch" past it would silently reopen access next launch that nobody
        // asked for at the time. Only a manual disable resets it — an error-driven
        // stop from inside McpServer never reaches this branch.
        setStartOnLaunch(false);
    }
#endif
}

void McpController::rotateToken()
{
#ifndef Q_OS_WASM
    if (m_mcp)
        m_mcp->rotateToken();
#endif
}

void McpController::setStartOnLaunch(bool enabled)
{
    if (m_mcpStartOnLaunch == enabled)
        return;
    m_mcpStartOnLaunch = enabled;
    QSettings().setValue(QStringLiteral("mcp/startOnLaunch"), enabled);
    emit startOnLaunchChanged();
}

// GUI-only: called once from Main.qml's own startup sequence, never from headless
// (which configures and starts the server itself from CLI args). Keeping this out of
// the constructor is what stops the two from racing over the same server instance.
void McpController::applyStartOnLaunch()
{
#ifndef Q_OS_WASM
    if (m_mcpStartOnLaunch && m_mcp)
        m_mcp->start();
#endif
}

namespace {

void copyToClipboard(const QString &text)
{
    if (text.isEmpty())
        return;
    if (QClipboard *clip = QGuiApplication::clipboard())
        clip->setText(text);
}

} // namespace

void McpController::copyCursorSnippet()
{
    copyToClipboard(cursorSnippet());
}

void McpController::copyClaudeCommand()
{
    copyToClipboard(claudeCommand());
}

void McpController::copyStdioSnippet()
{
    copyToClipboard(stdioSnippet());
}

QString McpController::agentGuide() const
{
#if !defined(Q_OS_ANDROID) && !defined(Q_OS_WASM)
    QString guide = drift::mcp::agentGuideText();
    if (m_mcp && m_mcp->running()) {
        guide += QStringLiteral("\nThis session:\nURL: %1\nToken: %2\n")
                     .arg(url(), token());
    }
    return guide;
#else
    return {};
#endif
}

void McpController::copyAgentGuide()
{
    copyToClipboard(agentGuide());
}

void McpController::rebuildClipIndexIfNeeded() const
{
    if (m_clipIndexRevision == m_editRevision)
        return;
    m_clipIndex.clear();
    const QList<drift::Track> &tracks = m_app->m_project.tracks();
    for (int t = 0; t < tracks.size(); ++t) {
        const QList<drift::Clip> &clips = tracks.at(t).clips;
        for (int c = 0; c < clips.size(); ++c)
            m_clipIndex.insert(clips.at(c).id, {t, c});
    }
    m_clipIndexRevision = m_editRevision;
}

QPair<int, int> McpController::locateClip(const QString &id) const
{
    if (id.isEmpty())
        return {-1, -1};
    rebuildClipIndexIfNeeded();
    return m_clipIndex.value(id, {-1, -1});
}

QString McpController::clipId(int trackIndex, int clipIndex) const
{
    if (!m_app->isValidClipIndex(trackIndex, clipIndex))
        return {};
    return m_app->m_project.tracks().at(trackIndex).clips.at(clipIndex).id;
}

namespace {

// Detail rows come from the QML clip map, which spells out every field for the inspector's
// bindings. Agents pay per token, so drop what a clip of this kind cannot use and what still
// sits at its default; an absent boolean reads as false. `verbose` returns the map untouched.
QJsonObject mcpDetailRow(const QVariantMap &clipMap, const QVariantMap &transform, bool verbose)
{
    if (verbose)
        return QJsonObject::fromVariantMap(clipMap);
    QVariantMap m = clipMap;
    const QString kind = m.value(QStringLiteral("kind")).toString();
    if (kind != QLatin1String("text") && kind != QLatin1String("subtitle")) {
        m.remove(QStringLiteral("textStyle"));
        m.remove(QStringLiteral("textContent"));
    } else {
        // The canonical style only: the flat v6 mirrors exist for the inspector, not for agents,
        // and an empty look or caret says nothing.
        QVariantMap style = m.value(QStringLiteral("textStyle")).toMap();
        for (const char *key : {"fillKind", "colorSecondary", "gradientAngle", "outlineEnabled", "outlineWidth",
                                "outlineColor", "shadowEnabled", "shadowOffsetX", "shadowOffsetY", "shadowBlur",
                                "shadowOpacity", "shadowColor", "glowEnabled", "glowColor", "glowRadius",
                                "glowOpacity", "animIn", "animOut"})
            style.remove(QLatin1String(key));
        if (style.value(QStringLiteral("lookId")).toString().isEmpty()) {
            style.remove(QStringLiteral("lookId"));
            style.remove(QStringLiteral("lookParams"));
        }
        QVariantMap animation = style.value(QStringLiteral("animation")).toMap();
        if (!animation.value(QStringLiteral("caret")).toMap().value(QStringLiteral("enabled")).toBool())
            animation.remove(QStringLiteral("caret"));
        if (animation.value(QStringLiteral("anchorGrouping")).toString() == QLatin1String("character")) {
            animation.remove(QStringLiteral("anchorGrouping"));
            animation.remove(QStringLiteral("anchorAlignment"));
        }
        if (!animation.value(QStringLiteral("custom")).toBool())
            animation.remove(QStringLiteral("custom"));
        style.insert(QStringLiteral("animation"), animation);
        m.insert(QStringLiteral("textStyle"), style);
    }
    if (kind != QLatin1String("shape"))
        m.remove(QStringLiteral("shapeStyle"));
    if (kind != QLatin1String("adjustment"))
        m.remove(QStringLiteral("adjustmentKind"));

    QVariantMap mask = m.value(QStringLiteral("mask")).toMap();
    if (mask.value(QStringLiteral("shape")).toString() == QLatin1String("none")) {
        m.remove(QStringLiteral("mask"));
    } else {
        if (mask.value(QStringLiteral("mediaPath")).toString().isEmpty()) {
            for (const char *key : {"mediaPath", "mediaFgrPath", "mediaSrcOffsetUs", "mediaFit",
                                    "mediaChannel", "mediaLoop", "mediaTimeBase"})
                mask.remove(QLatin1String(key));
        }
        if (!mask.value(QStringLiteral("animated")).toBool())
            mask.remove(QStringLiteral("keyframes"));
        m.insert(QStringLiteral("mask"), mask);
    }

    const QVariantMap keyframes = m.value(QStringLiteral("keyframes")).toMap();
    QVariantMap animatedTracks;
    QStringList animated;
    for (auto it = keyframes.constBegin(); it != keyframes.constEnd(); ++it) {
        if (it.value().toMap().value(QStringLiteral("points")).toList().size() > 1) {
            animatedTracks.insert(it.key(), it.value());
            animated.append(it.key());
        }
    }
    m.remove(QStringLiteral("keyframes"));
    if (!animatedTracks.isEmpty()) {
        m.insert(QStringLiteral("keyframes"), animatedTracks);
        m.insert(QStringLiteral("animated"), animated);
    }
    m.insert(QStringLiteral("transform"), transform);

    if (!m.value(QStringLiteral("stabilized")).toBool() && !m.value(QStringLiteral("stabilizing")).toBool()) {
        for (const char *key : {"stabilizeMode", "stabilizeAppliedMode", "stabilizeSmoothing",
                                "stabilizeTripod", "stabilizeStale", "stabilizeProgress",
                                "stabilizeStatus"})
            m.remove(QLatin1String(key));
    }
    for (const char *which : {"animIn", "animOut"}) {
        if (m.value(QLatin1String(which)).toMap().value(QStringLiteral("kind")).toString() == QLatin1String("none"))
            m.remove(QLatin1String(which));
    }
    for (const char *key : {"filmstripPath", "thumbnailPath", "canFaceTrack", "canDepth", "depthClipId"})
        m.remove(QLatin1String(key));
    if (!m.value(QStringLiteral("hasFaceTrack")).toBool()) {
        m.remove(QStringLiteral("faceTrackHasContours"));
        m.remove(QStringLiteral("faceTrackHasMesh"));
        m.remove(QStringLiteral("faceTrackFaceCount"));
    }
    if (m.value(QStringLiteral("fadeCurve")).toString() != QLatin1String("custom")) {
        m.remove(QStringLiteral("fadeShape"));
        m.remove(QStringLiteral("fadeHandles"));
    }
    if (m.value(QStringLiteral("audioStreamIndex")).toInt() == 0)
        m.remove(QStringLiteral("audioStreamIndex"));
    if (m.value(QStringLiteral("pan")).toDouble() == 0.0)
        m.remove(QStringLiteral("pan"));
    if (m.value(QStringLiteral("assetIndex")).toInt() < 0)
        m.remove(QStringLiteral("assetIndex"));
    if (m.value(QStringLiteral("sourceDuration")).toDouble() <= 0.0)
        m.remove(QStringLiteral("sourceDuration"));
    if (m.value(QStringLiteral("fadeIn")).toDouble() == 0.0
        && m.value(QStringLiteral("fadeOut")).toDouble() == 0.0) {
        m.remove(QStringLiteral("fadeIn"));
        m.remove(QStringLiteral("fadeOut"));
        m.remove(QStringLiteral("fadeCurve"));
    }

    for (auto it = m.begin(); it != m.end();) {
        const QVariant &v = it.value();
        bool drop = false;
        switch (v.typeId()) {
        case QMetaType::Bool:
            drop = !v.toBool();
            break;
        case QMetaType::QString:
            drop = v.toString().isEmpty();
            break;
        case QMetaType::QVariantList:
        case QMetaType::QStringList:
            drop = v.toList().isEmpty();
            break;
        default:
            break;
        }
        it = drop ? m.erase(it) : std::next(it);
    }
    return QJsonObject::fromVariantMap(m);
}

} // namespace

QVariantMap McpController::compactClip(int trackIndex, int clipIndex, bool includeCanvas) const
{
    if (!m_app->isValidClipIndex(trackIndex, clipIndex))
        return {};
    const drift::Clip &clip = m_app->m_project.tracks().at(trackIndex).clips.at(clipIndex);
    QVariantMap out{
        {QStringLiteral("id"), clip.id},
        {QStringLiteral("kind"), drift::clipTypeToString(clip.type)},
        {QStringLiteral("name"), clip.name},
        {QStringLiteral("start"), drift::usToSeconds(clip.timelineStart)},
        {QStringLiteral("duration"), drift::usToSeconds(clip.timelineDuration)},
        {QStringLiteral("inPoint"), drift::usToSeconds(clip.srcIn)},
        {QStringLiteral("outPoint"), drift::usToSeconds(clip.srcOut)},
        {QStringLiteral("assetId"), clip.assetId},
    };
    if (!clip.sequenceId.isEmpty())
        out.insert(QStringLiteral("sequenceId"), clip.sequenceId);
    if (clip.type == drift::ClipType::Adjustment)
        out.insert(QStringLiteral("adjustmentKind"), drift::adjustmentKindToString(clip.adjustmentKind));
    if (!includeCanvas)
        return out;

    const double at = m_app->playheadSeconds();
    out.insert(QStringLiteral("x"), m_app->propertyValueAt(trackIndex, clipIndex, QStringLiteral("x"), at, 0));
    out.insert(QStringLiteral("y"), m_app->propertyValueAt(trackIndex, clipIndex, QStringLiteral("y"), at, 0));
    out.insert(QStringLiteral("w"), m_app->propertyValueAt(trackIndex, clipIndex, QStringLiteral("width"), at, 0));
    out.insert(QStringLiteral("h"), m_app->propertyValueAt(trackIndex, clipIndex, QStringLiteral("height"), at, 0));
    out.insert(QStringLiteral("rotation"),
               m_app->propertyValueAt(trackIndex, clipIndex, QStringLiteral("rotation"), at, 0));
    out.insert(QStringLiteral("opacity"),
               m_app->propertyValueAt(trackIndex, clipIndex, QStringLiteral("opacity"), at, 1));
    if (clip.layer3d) {
        out.insert(QStringLiteral("layer3d"), true);
        for (const char *key : {"rotationX", "rotationY", "z", "perspective"}) {
            const QString k = QLatin1String(key);
            out.insert(k, m_app->propertyValueAt(trackIndex, clipIndex, k, at, 0));
        }
    }
    return out;
}

QJsonObject McpController::inspect(const InspectOptions &options) const
{
    using namespace drift::mcp;
    if (options.since >= 0 && options.since == m_editRevision)
        return ok({{QStringLiteral("unchanged"), true}, {QStringLiteral("revision"), m_editRevision}});

    const QList<drift::Track> &projectTracks = m_app->m_project.tracks();
    QPair<int, int> only{-1, -1};
    if (!options.clip.isEmpty()) {
        only = locateClip(options.clip);
        if (only.first < 0)
            return err("bad_args", QStringLiteral("clip %1 not found — re-read inspect({clips:true}); "
                                                  "ids change after set_speed_curve/undo")
                                       .arg(options.clip));
    } else if (options.track >= 0 && options.track >= projectTracks.size()) {
        return err("bad_args", QStringLiteral("track %1 does not exist; the timeline has %2 track(s)")
                                   .arg(options.track)
                                   .arg(projectTracks.size()));
    }
    const int onlyTrack = only.first >= 0 ? only.first : options.track;
    const bool includeClips = options.clips || only.first >= 0;
    const bool detail = options.detail || only.first >= 0;
    const bool includeCues = options.cues;

    int clipCount = 0;
    QJsonArray trackRows;
    const QVariantList trackModels = detail ? m_app->tracks() : QVariantList{};
    for (int t = 0; t < projectTracks.size(); ++t) {
        const drift::Track &track = projectTracks.at(t);
        clipCount += track.clips.size();
        if (onlyTrack >= 0 && t != onlyTrack)
            continue;
        QJsonObject row{
            {QStringLiteral("i"), t},
            {QStringLiteral("type"), drift::trackTypeToString(track.type)},
            {QStringLiteral("clips"), track.clips.size()},
            {QStringLiteral("muted"), track.muted},
            {QStringLiteral("hidden"), track.hidden},
        };
        if (track.isAdjustment())
            row.insert(QStringLiteral("scope"), drift::adjustmentScopeToString(track.adjustmentScope));
        if (track.isAdjustmentLane())
            row.insert(QStringLiteral("parent"), drift::adjustmentLaneParentIndex(m_app->m_project, t));
        if (track.isTransformLayer()) {
            QJsonArray covers;
            for (const int i : drift::transformSpanTrackIndexes(projectTracks, t))
                covers.append(i);
            row.insert(QStringLiteral("span_end"), drift::transformSpanEndIndex(projectTracks, t));
            row.insert(QStringLiteral("covers"), covers);
        }
        {
            QJsonArray transformedBy;
            for (const int layer : drift::transformLayersCovering(projectTracks, t))
                transformedBy.append(layer);
            if (!transformedBy.isEmpty())
                row.insert(QStringLiteral("transformedBy"), transformedBy);
        }
        if (detail && t < trackModels.size()) {
            const QVariantMap tm = trackModels.at(t).toMap();
            if (tm.value(QStringLiteral("clipDisplay")).toInt()
                == static_cast<int>(drift::Track::ClipDisplay::Waveform))
                row.insert(QStringLiteral("showWaveform"), true);
            if (tm.value(QStringLiteral("heightScale")).toDouble() != 1.0)
                row.insert(QStringLiteral("heightScale"), tm.value(QStringLiteral("heightScale")).toDouble());
            const QVariantList transitions = tm.value(QStringLiteral("transitions")).toList();
            QJsonArray trJson;
            for (const QVariant &tr : transitions)
                trJson.append(QJsonObject::fromVariantMap(tr.toMap()));
            if (!trJson.isEmpty())
                row.insert(QStringLiteral("transitions"), trJson);
        }
        if (includeClips) {
            QJsonArray clips;
            for (int c = 0; c < track.clips.size(); ++c) {
                if (only.second >= 0 && c != only.second)
                    continue;
                if (detail) {
                    // clipAt(), not the tracks() list: that one carries only what the timeline
                    // strip draws, and an inspect row is the whole clip — textStyle, mask,
                    // keyframes and all.
                    const QVariantMap fullClip = m_app->clipAt(t, c);
                    if (!fullClip.isEmpty()) {
                        const QVariantMap canvas = compactClip(t, c, true);
                        QVariantMap transform;
                        for (const char *key : {"x", "y", "w", "h", "rotation", "opacity", "layer3d",
                                                "rotationX", "rotationY", "z", "perspective"}) {
                            if (canvas.contains(QLatin1String(key)))
                                transform.insert(QLatin1String(key), canvas.value(QLatin1String(key)));
                        }
                        clips.append(mcpDetailRow(fullClip, transform, options.verbose));
                    }
                } else {
                    const QVariantMap compact = compactClip(t, c, false);
                    QJsonObject row = QJsonObject::fromVariantMap(compact);
                    if (includeCues) {
                        QJsonArray cues;
                        for (const drift::SubtitleCue &cue : track.clips.at(c).subtitleCues) {
                            cues.append(QJsonObject{
                                {QStringLiteral("start"), drift::usToSeconds(cue.startUs)},
                                {QStringLiteral("end"), drift::usToSeconds(cue.endUs)},
                                {QStringLiteral("text"), cue.text},
                            });
                        }
                        row.insert(QStringLiteral("subtitleCues"), cues);
                    }
                    clips.append(row);
                }
            }
            row.insert(QStringLiteral("items"), clips);
        }
        trackRows.append(row);
    }

    QJsonArray assets;
    if (m_app->m_assetLibrary) {
        for (int i = 0; i < m_app->m_assetLibrary->count(); ++i) {
            const QVariantMap a = m_app->m_assetLibrary->assetAt(i);
            assets.append(QJsonObject{
                {QStringLiteral("index"), i},
                {QStringLiteral("id"), a.value(QStringLiteral("id")).toString()},
                {QStringLiteral("name"), a.value(QStringLiteral("name")).toString()},
                {QStringLiteral("kind"), a.value(QStringLiteral("kind")).toString()},
                {QStringLiteral("dur"), a.value(QStringLiteral("durationSeconds")).toDouble()},
            });
        }
    }

    QJsonObject extra{
        {QStringLiteral("revision"), m_editRevision},
        {QStringLiteral("name"), m_app->m_project.name()},
        {QStringLiteral("w"), m_app->m_project.width()},
        {QStringLiteral("h"), m_app->m_project.height()},
        {QStringLiteral("fps"), m_app->m_project.fps()},
        {QStringLiteral("dur"), drift::usToSeconds(m_app->m_project.durationUs())},
        {QStringLiteral("playhead"), m_app->playheadSeconds()},
        {QStringLiteral("playing"), m_app->playing()},
        {QStringLiteral("preview_mode"), m_app->preview()->mode()},
        {QStringLiteral("overlap"), m_app->m_allowClipOverlap},
        {QStringLiteral("clips"), clipCount},
        {QStringLiteral("tracks"), trackRows},
        {QStringLiteral("assets"), assets},
        {QStringLiteral("path"), m_app->projectFile()->currentProjectPath()},
        {QStringLiteral("dirty"), m_app->projectFile()->hasUnsavedChanges()},
        {QStringLiteral("background"), QJsonObject::fromVariantMap(m_app->projectFile()->background())},
        {QStringLiteral("export"),
         QJsonObject{{QStringLiteral("active"), m_app->m_exportInProgress},
                     {QStringLiteral("progress"), m_app->m_exportProgress}}},
    };
    if (m_app->preview()->mode() == QLatin1String("3d"))
        extra.insert(QStringLiteral("look_through"), m_app->preview()->lookThrough());
    if (detail) {
        QJsonArray marks;
        for (const QVariant &v : m_app->bookmarks()) {
            const QVariantMap b = v.toMap();
            marks.append(QJsonObject{
                {QStringLiteral("at"), b.value(QStringLiteral("seconds")).toDouble()},
                {QStringLiteral("label"), b.value(QStringLiteral("label")).toString()},
            });
        }
        extra.insert(QStringLiteral("bookmarks"), marks);
        QJsonObject jobs;
        if (m_app->projectFile()->packaging()) {
            jobs.insert(QStringLiteral("package"),
                        QJsonObject{{QStringLiteral("active"), true},
                                    {QStringLiteral("progress"), m_app->projectFile()->packageProgress()}});
        }
        if (m_app->subtitleGenerating()) {
            jobs.insert(QStringLiteral("subtitleGen"),
                        QJsonObject{{QStringLiteral("active"), true},
                                    {QStringLiteral("progress"), m_app->subtitleGenProgress()},
                                    {QStringLiteral("status"), m_app->subtitleGenStatus()}});
        }
        if (m_app->reverseRendering()) {
            jobs.insert(QStringLiteral("reverseRender"),
                        QJsonObject{{QStringLiteral("active"), true},
                                    {QStringLiteral("progress"), m_app->reverseRenderProgress()},
                                    {QStringLiteral("status"), m_app->reverseRenderStatus()}});
        }
        // Scene state without the rows — list_scenes returns those. There is deliberately no
        // `stale` flag as there is for beats: this analysis describes the source file, not the
        // mix, so edits do not invalidate it.
        if (m_app->scenes()->detecting() || !m_app->scenes()->list().isEmpty()) {
            jobs.insert(QStringLiteral("sceneDetect"),
                        QJsonObject{{QStringLiteral("active"), m_app->scenes()->detecting()},
                                    {QStringLiteral("progress"), m_app->scenes()->progress()},
                                    {QStringLiteral("status"), m_app->scenes()->status()},
                                    {QStringLiteral("clip"), m_app->scenes()->clipId()},
                                    {QStringLiteral("scenes"), int(m_app->scenes()->list().size())}});
        }
        if (DownloadCenter::instance()->activeCount() > 0) {
            QJsonObject byKind;
            for (const QVariant &row : DownloadCenter::instance()->jobs()) {
                const QVariantMap job = row.toMap();
                if (job.value(QStringLiteral("finished")).toBool())
                    continue;
                const QString kind = job.value(QStringLiteral("kind")).toString();
                byKind.insert(kind, byKind.value(kind).toInt() + 1);
            }
            jobs.insert(QStringLiteral("downloads"),
                        QJsonObject{{QStringLiteral("active"), DownloadCenter::instance()->activeCount()},
                                    {QStringLiteral("byKind"), byKind}});
        }
        const QJsonArray jobList = m_app->m_jobs->jobs();
        if (!jobList.isEmpty())
            jobs.insert(QStringLiteral("list"), jobList);
        if (!jobs.isEmpty())
            extra.insert(QStringLiteral("jobs"), jobs);
        // Beat state without the arrays — detect_beats returns those. `stale` matters because
        // finishEdit drops the analysis as soon as the mix changes, so a grid an agent found a
        // few ops ago may already be gone.
        QJsonObject beatState{
            {QStringLiteral("active"), m_app->m_beatAnalysisRunning},
            {QStringLiteral("analysed"), !m_app->m_beatAnalysis.isEmpty()},
            {QStringLiteral("gridVisible"), m_app->m_beatGridVisible},
            {QStringLiteral("onsetsVisible"), m_app->m_onsetsVisible},
        };
        if (!m_app->m_beatAnalysis.isEmpty()) {
            beatState.insert(QStringLiteral("bpm"), m_app->m_beatAnalysisRaw.bpm);
            beatState.insert(QStringLiteral("confidence"), m_app->m_beatAnalysisRaw.confidence);
            beatState.insert(QStringLiteral("rangeStart"),
                             m_app->m_beatAnalysis.value(QStringLiteral("rangeStart")).toDouble());
            beatState.insert(QStringLiteral("rangeDuration"),
                             m_app->m_beatAnalysis.value(QStringLiteral("rangeDuration")).toDouble());
            beatState.insert(QStringLiteral("n"), static_cast<int>(m_app->m_beatAnalysisRaw.beats.size()));
            beatState.insert(QStringLiteral("onsets"),
                             static_cast<int>(m_app->m_beatAnalysisRaw.onsets.size()));
            beatState.insert(QStringLiteral("stale"),
                             m_app->m_beatAudioFingerprint != m_app->audioLayoutFingerprint());
        }
        if (m_app->m_beatAnalysisRunning || !m_app->m_beatAnalysis.isEmpty())
            extra.insert(QStringLiteral("beats"), beatState);
    }
    if (m_app->m_selectedTrack >= 0 && m_app->m_selectedClip >= 0
        && m_app->isValidClipIndex(m_app->m_selectedTrack, m_app->m_selectedClip)) {
        extra.insert(QStringLiteral("selection"),
                     QJsonObject{
                         {QStringLiteral("track"), m_app->m_selectedTrack},
                         {QStringLiteral("index"), m_app->m_selectedClip},
                         {QStringLiteral("clip"),
                          m_app->m_project.tracks().at(m_app->m_selectedTrack).clips.at(m_app->m_selectedClip).id},
                     });
    }
    extra.insert(QStringLiteral("undo"),
                 QJsonObject{{QStringLiteral("can"), m_app->m_undoStack.canUndo()},
                             {QStringLiteral("canRedo"), m_app->m_undoStack.canRedo()},
                             {QStringLiteral("depth"), m_app->m_undoStack.count()},
                             {QStringLiteral("index"), m_app->m_undoStack.index()},
                             {QStringLiteral("hash"), m_app->historyHashAt(m_app->m_undoStack.index()).left(12)}});
    if (detail && m_app->m_multicamActive) {
        extra.insert(QStringLiteral("multicam"),
                     QJsonObject{
                         {QStringLiteral("active"), true},
                         {QStringLiteral("activeAngle"), m_app->multicamActiveAngle()},
                         {QStringLiteral("angles"), QJsonArray::fromVariantList(m_app->multicamAngles())},
                         {QStringLiteral("program"),
                          QJsonArray::fromVariantList(m_app->multicamProgramClips())},
                     });
    }
    if (m_app->m_project.hasWorkArea()) {
        extra.insert(QStringLiteral("work_in"), m_app->workAreaInSeconds());
        extra.insert(QStringLiteral("work_out"), m_app->workAreaOutSeconds());
    }
    return ok(extra);
}

bool McpController::setWorkArea(double inSeconds, double outSeconds)
{
    const drift::TimeUs inUs = qMax<drift::TimeUs>(0, drift::secondsToUs(inSeconds));
    const drift::TimeUs outUs = drift::secondsToUs(outSeconds);
    if (outUs <= inUs)
        return false;

    const drift::Project before = m_app->m_project;
    m_app->m_project.setWorkAreaInUs(inUs);
    m_app->m_project.setWorkAreaOutUs(outUs);
    m_app->pushProjectEdit(before, QStringLiteral("Work area"));
    m_app->finishEdit(QStringLiteral("Work area set"));
    emit m_app->workAreaChanged();
    return true;
}

// The agent's own corner of the settings store. An MCP export used to write straight into the
// export dialog's memory, so a scripted audio-only render silently changed what the user was
// offered the next time they opened the dialog.
void McpController::rememberExportSettings(const QVariantMap &settings)
{
    QSettings store;
    store.beginGroup(QStringLiteral("export-agent"));
    for (auto it = settings.begin(); it != settings.end(); ++it)
        store.setValue(it.key(), it.value());
    store.endGroup();
}

QVariantMap McpController::lastExportSettings() const
{
    QSettings store;
    store.beginGroup(QStringLiteral("export-agent"));
    QVariantMap out;
    for (const QString &key : store.childKeys())
        out.insert(key, store.value(key));
    store.endGroup();
    return out;
}

bool McpController::setClipCanvas(int trackIndex, int clipIndex, const QVariantMap &patch)
{
    if (!m_app->isValidClipIndex(trackIndex, clipIndex))
        return false;

    drift::Track &track = m_app->m_project.tracks()[trackIndex];
    drift::Clip &clip = track.clips[clipIndex];
    if (clip.type == drift::ClipType::Audio)
        return false;

    const drift::Project before = m_app->m_project;
    // See setClipKeyframe: a transform written with the playhead outside the clip has to land
    // inside it, or it scatters keys at times the clip never plays.
    const drift::TimeUs relative =
        qBound<drift::TimeUs>(0, m_app->m_playheadUs - clip.timelineStart, clip.timelineDuration);
    bool any = false;
    // Keys at the playhead only where the property already animates or auto-key is on; a
    // constant (or single-key) property takes the value everywhere, as the tool describes.
    auto write = [&](const QString &patchKey, const QString &prop) {
        if (!patch.contains(patchKey))
            return;
        const drift::KeyframeTrack<double> *track = m_app->transformTrack(clip, prop);
        const bool animated = track && track->enabled() && track->keyframes().size() > 1;
        any = m_app->writeClipProp(clip, prop, relative, patch.value(patchKey).toDouble(),
                                 m_app->m_autoKeyEnabled, animated)
              || any;
    };
    write(QStringLiteral("x"), QStringLiteral("x"));
    write(QStringLiteral("y"), QStringLiteral("y"));
    write(QStringLiteral("w"), QStringLiteral("width"));
    write(QStringLiteral("h"), QStringLiteral("height"));
    write(QStringLiteral("rotation"), QStringLiteral("rotation"));
    write(QStringLiteral("rotationX"), QStringLiteral("rotationX"));
    write(QStringLiteral("rotationY"), QStringLiteral("rotationY"));
    write(QStringLiteral("z"), QStringLiteral("z"));
    write(QStringLiteral("perspective"), QStringLiteral("perspective"));
    write(QStringLiteral("opacity"), QStringLiteral("opacity"));
    // After the value writes, so an explicit layer3d:false wins over their auto-enable.
    if (patch.contains(QStringLiteral("layer3d")) && clip.type != drift::ClipType::Model3d) {
        clip.layer3d = patch.value(QStringLiteral("layer3d")).toBool();
        if (!clip.layer3d)
            m_app->clearPose3d(clip);
        any = true;
    }
    if (!any)
        return false;

    if (!m_undoSuspended) {
        m_app->pushProjectEdit(before, QStringLiteral("MCP canvas"));
        m_app->finishEdit(QStringLiteral("MCP canvas"));
    } else {
        m_app->emitPreviewFrame();
    }
    return true;
}

QJsonObject McpController::captureFrame(double atSeconds, bool full, bool editor3d)
{
    using namespace drift::mcp;
    m_app->setPlaying(false);

    const drift::TimeUs timeUs =
        atSeconds < 0.0 ? m_app->m_playheadUs : qMax<drift::TimeUs>(0, drift::secondsToUs(atSeconds));
    const auto snapshot = std::make_shared<const drift::Project>(m_app->m_project.detachedCopy());
    FrameCompositor::RenderOptions options;
    if (!full) {
        const int longEdge = qMax(snapshot->width(), snapshot->height());
        if (longEdge > 1280)
            options.previewScale = 1280.0 / double(longEdge);
    }
    if (editor3d) {
        options.editor.active = true;
        options.editor.view = m_app->preview()->currentView();
    }

    auto frame = std::make_shared<QImage>();
    QEventLoop loop;
    (void)QtConcurrent::run([snapshot, timeUs, options, frame, &loop]() {
        FrameCompositor compositor;
        compositor.setProject(snapshot.get());
        *frame = compositor.compositeAt(timeUs, options);
        QMetaObject::invokeMethod(&loop, &QEventLoop::quit, Qt::QueuedConnection);
    });
    loop.exec();

    if (frame->isNull())
        return textResult(err("capture_failed", QStringLiteral("Compositor returned no frame")), true);

    QJsonObject meta = ok({
        {QStringLiteral("at"), drift::usToSeconds(timeUs)},
        {QStringLiteral("w"), frame->width()},
        {QStringLiteral("h"), frame->height()},
        {QStringLiteral("full"), full},
    });
    if (editor3d)
        meta.insert(QStringLiteral("view"), QStringLiteral("editor3d"));
    if (timeUs > snapshot->durationUs()) {
        meta.insert(QStringLiteral("beyond_end"), true);
        meta.insert(QStringLiteral("dur"), drift::usToSeconds(snapshot->durationUs()));
    }
    meta = compactJson(meta);

    if (full) {
        const QString outPath = AppController::newFreezeFramePath(m_app->m_project.id());
        if (outPath.isEmpty() || !frame->save(outPath, "PNG"))
            return textResult(err("capture_failed", QStringLiteral("Could not write PNG")), true);
        QJsonObject withPath = meta;
        withPath.insert(QStringLiteral("path"), outPath);
        return textResult(withPath);
    }

    QByteArray jpeg;
    QBuffer buffer(&jpeg);
    buffer.open(QIODevice::WriteOnly);
    if (!frame->save(&buffer, "JPEG", 80))
        return textResult(err("capture_failed", QStringLiteral("Could not encode JPEG")), true);

    QJsonArray content;
    content.append(QJsonObject{
        {QStringLiteral("type"), QStringLiteral("text")},
        {QStringLiteral("text"), QString::fromUtf8(QJsonDocument(meta).toJson(QJsonDocument::Compact))},
    });
    content.append(QJsonObject{
        {QStringLiteral("type"), QStringLiteral("image")},
        {QStringLiteral("mimeType"), QStringLiteral("image/jpeg")},
        {QStringLiteral("data"), QString::fromLatin1(jpeg.toBase64())},
    });
    return {{QStringLiteral("content"), content}, {QStringLiteral("isError"), false}};
}

void McpController::beginBatch()
{
    if (m_batchDepth++ == 0) {
        m_batchBefore = m_app->m_project.detachedCopy();
        m_undoSuspended = true;
    }
}

void McpController::endBatch(const QString &text, bool pushUndo)
{
    if (m_batchDepth <= 0)
        return;
    if (--m_batchDepth > 0)
        return;
    m_undoSuspended = false;
    if (m_app->m_previewDragActive) {
        m_app->m_previewDragActive = false;
        m_app->m_previewDragAuto = false;
        m_app->m_previewDragDirty = false;
        m_app->m_previewAutoCommit->stop();
        emit m_app->previewDragActiveChanged();
    }
    if (pushUndo)
        m_app->pushProjectEdit(m_batchBefore, text);
    m_app->finishEdit(text);
    m_batchBefore = {};
}

namespace {

constexpr int kMcpSheetMaxTiles = 20;
constexpr int kMcpSheetMaxCandidates = 120;
constexpr int kMcpSheetMinTile = 120;
constexpr int kMcpSheetMaxTile = 720;
constexpr int kMcpSheetHashEdge = 160;
constexpr int kMcpActivityMinSamples = 8;
constexpr int kMcpActivityMaxSamples = 600;
constexpr double kMcpActivityMaxSeconds = 3600.0;
constexpr int kMcpActivityScanWidth = 64;

QString newCapturePath(const QString &projectId, const QString &prefix, const QString &ext)
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty())
        return {};
    const QString dir = QDir(base).filePath(QStringLiteral("projects/%1/media").arg(projectId));
    if (!QDir().mkpath(dir))
        return {};
    return QDir(dir).filePath(QStringLiteral("%1-%2.%3")
                                  .arg(prefix, QUuid::createUuid().toString(QUuid::WithoutBraces), ext));
}

// One decode cursor for the sheet/activity reads so they never share one with playback.
constexpr quint64 kFrameSheetStreamId = 0xA5'11'5C'A4'00'00'00'07ull;

// Where the frames come from: the composited timeline, or one clip's source file.
struct FrameSource
{
    std::shared_ptr<const drift::Project> project;
    QString path;          // source mode when non-empty
    int rotationCorrection = 0;
    bool source() const { return !path.isEmpty(); }
    int longEdge() const { return qMax(project->width(), project->height()); }

    QImage render(FrameCompositor &compositor, double seconds, int maxW, int maxH) const
    {
        const drift::TimeUs us = qMax<drift::TimeUs>(0, drift::secondsToUs(seconds));
        if (source())
            return ClipReaderPool::instance().readVideoFrame(path, kFrameSheetStreamId, us, maxW, maxH,
                                                             rotationCorrection);
        FrameCompositor::RenderOptions options;
        options.previewScale =
            qBound(kMinPreviewScale, double(maxW) / double(longEdge()), 1.0);
        return compositor.compositeAt(us, options);
    }
};

double clipLocalToTimelineSeconds(const drift::Clip &clip, drift::TimeUs sourceUs)
{
    return drift::usToSeconds(clip.timelineStart + clip.sourceUsToClipLocalUs(sourceUs));
}

QByteArray encodeJpeg(const QImage &image, int quality)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "JPEG", quality);
    return bytes;
}

QVector<float> blockingMixedPeaks(const drift::Project &snap, double startSeconds, double durSeconds,
                                  int buckets)
{
    const int rate = 8000;
    const qint64 frames = static_cast<qint64>(durSeconds * rate);
    if (frames <= 0 || buckets <= 0)
        return {};
    const drift::TimeUs startUs = drift::secondsToUs(startSeconds);
    auto raw = std::make_shared<QVector<float>>();
    QEventLoop loop;
    (void)QtConcurrent::run([snap, startUs, frames, rate, buckets, raw, &loop]() {
        AudioMixer mixer;
        mixer.setProject(&snap);
        *raw = MediaWaveform::mixedPeaks(
            frames, rate, static_cast<int>(qMin<qint64>(buckets, frames)),
            [&mixer, startUs, rate](float *out, qint64 frameOffset, int maxFrames) {
                const drift::TimeUs at = startUs + frameOffset * drift::kUsPerSecond / rate;
                mixer.mix(at, maxFrames, rate, out);
                return maxFrames;
            });
        QMetaObject::invokeMethod(&loop, &QEventLoop::quit, Qt::QueuedConnection);
    });
    loop.exec();
    return *raw;
}

} // namespace

QJsonObject McpController::frameSheet(const FrameSheetRequest &request)
{
    using namespace drift::mcp;
    using namespace drift::framesheet;
    m_app->setPlaying(false);

    FrameSource src;
    src.project = std::make_shared<const drift::Project>(m_app->m_project.detachedCopy());
    const bool sourceMode = request.track >= 0 || request.clip >= 0;
    drift::Clip clip;
    if (sourceMode) {
        const auto &tracks = src.project->tracks();
        if (request.track < 0 || request.track >= tracks.size() || request.clip < 0
            || request.clip >= tracks.at(request.track).clips.size())
            return err("not_found", QStringLiteral("no clip at track %1 index %2")
                                        .arg(request.track).arg(request.clip));
        clip = tracks.at(request.track).clips.at(request.clip);
        if (clip.path.isEmpty() || clip.type != drift::ClipType::Video)
            return err("type_mismatch", QStringLiteral("clip has no video file; omit clip to render the composition"));
        src.path = clip.path;
        src.rotationCorrection = clip.rotationCorrection;
    } else if (src.project->durationUs() <= 0) {
        return err("not_found", QStringLiteral("Timeline is empty"));
    }

    double start = 0.0;
    double end = 0.0;
    if (sourceMode) {
        start = drift::usToSeconds(clip.srcIn);
        end = drift::usToSeconds(clip.srcOut);
    } else if (src.project->hasWorkArea()) {
        start = drift::usToSeconds(src.project->workAreaInUs());
        end = drift::usToSeconds(src.project->workAreaOutUs());
    } else {
        end = drift::usToSeconds(src.project->durationUs());
    }
    if (request.start >= 0.0)
        start = request.start;
    if (request.end >= 0.0)
        end = request.end;
    if (end <= start)
        return err("bad_args", QStringLiteral("end must be > start (got %1..%2)").arg(start).arg(end));

    const int n = qBound(1, request.n, kMcpSheetMaxTiles);
    QString sample = request.sample.isEmpty() ? QStringLiteral("changes") : request.sample;
    QList<double> candidates;
    QJsonArray unscanned;
    QList<QPair<QString, int>> sceneOf;   // parallel to candidates in scenes mode

    if (!request.at.isEmpty()) {
        sample = QStringLiteral("at");
        for (double t : request.at) {
            if (candidates.size() >= kMcpSheetMaxTiles)
                break;
            candidates.append(t);
        }
    } else if (sample == QLatin1String("scenes")) {
        QList<QPair<double, QPair<QString, int>>> hits;
        const auto collect = [&](const drift::Clip &c) {
            drift::SceneAnalysis analysis;
            if (!drift::loadCachedAnalysis(m_app->scenes()->sceneRequestFor(c, true, 0.0), &analysis)
                && !drift::loadCachedAnalysis(m_app->scenes()->sceneRequestFor(c, false, 0.0), &analysis)) {
                unscanned.append(c.id);
                const double t = sourceMode ? drift::usToSeconds(c.srcIn)
                                            : drift::usToSeconds(c.timelineStart);
                if (t >= start && t < end)
                    hits.append({t, {c.id, -1}});
                return;
            }
            for (int i = 0; i < analysis.scenes.size(); ++i) {
                const drift::TimeUs thumb = analysis.scenes.at(i).thumbnailUs;
                const double t = sourceMode ? drift::usToSeconds(thumb)
                                            : clipLocalToTimelineSeconds(c, thumb);
                if (t >= start && t < end)
                    hits.append({t, {c.id, i}});
            }
        };
        if (sourceMode) {
            collect(clip);
        } else {
            for (const drift::Track &track : src.project->tracks()) {
                if (track.type != drift::TrackType::Video)
                    continue;
                for (const drift::Clip &c : track.clips) {
                    if (c.type != drift::ClipType::Video || c.path.isEmpty())
                        continue;
                    if (drift::usToSeconds(c.timelineEnd()) <= start
                        || drift::usToSeconds(c.timelineStart) >= end)
                        continue;
                    collect(c);
                }
            }
        }
        std::sort(hits.begin(), hits.end(),
                  [](const auto &a, const auto &b) { return a.first < b.first; });
        for (int i : selectUniform(hits.size(), qMin(n, int(hits.size())))) {
            candidates.append(hits.at(i).first);
            sceneOf.append(hits.at(i).second);
        }
        if (candidates.isEmpty())
            return err("not_found", QStringLiteral("No scanned shots in %1..%2 — call detect_scenes first or use sample:\"uniform\"").arg(start).arg(end));
    } else if (sample == QLatin1String("uniform")) {
        for (int i = 0; i < n; ++i)
            candidates.append(start + (end - start) * i / n);
    } else if (sample == QLatin1String("changes")) {
        const int count = qBound(n, int((end - start) * 4.0), kMcpSheetMaxCandidates);
        for (int i = 0; i < count; ++i)
            candidates.append(start + (end - start) * i / count);
    } else {
        return err("bad_args", QStringLiteral("sample must be one of changes, uniform, scenes"));
    }

    const bool changes = sample == QLatin1String("changes");
    const int minChange = qBound(1, request.minChange, 32);
    const int tileWidth = request.tileWidth > 0
                              ? qBound(kMcpSheetMinTile, request.tileWidth, kMcpSheetMaxTile)
                              : 0;

    struct Result
    {
        QList<int> kept;
        int skipped = 0;
        bool dropped = false;
        Layout layout;
        QList<QImage> tiles;
        QList<int> diff;
        QImage sheet;
        double aspect = 16.0 / 9.0;
    };
    auto result = std::make_shared<Result>();
    const bool label = request.label;
    const int cols = request.cols;
    QEventLoop loop;
    (void)QtConcurrent::run([=, &loop]() {
        FrameCompositor compositor;
        compositor.setProject(src.project.get());

        if (changes) {
            QList<quint64> hashes;
            for (double t : candidates) {
                const QImage frame = src.render(compositor, t, kMcpSheetHashEdge, kMcpSheetHashEdge * 9 / 16);
                if (!frame.isNull() && hashes.isEmpty())
                    result->aspect = double(frame.width()) / double(qMax(1, frame.height()));
                hashes.append(dHash(frame));
            }
            const Selection all = selectChanges(hashes, minChange, 4, hashes.size());
            const Selection sel = all.kept.size() > n ? selectChanges(hashes, minChange, 4, n) : all;
            result->kept = sel.kept;
            result->skipped = sel.skipped;
            result->dropped = all.kept.size() > n;
        } else {
            for (int i = 0; i < candidates.size(); ++i)
                result->kept.append(i);
        }

        if (!src.source())
            result->aspect = double(src.project->width()) / double(qMax(1, src.project->height()));
        else if (!changes && !candidates.isEmpty()) {
            const QImage probe = src.render(compositor, candidates.first(), kMcpSheetHashEdge, kMcpSheetHashEdge);
            if (!probe.isNull())
                result->aspect = double(probe.width()) / double(qMax(1, probe.height()));
        }

        result->layout = layoutFor(result->kept.size(), result->aspect, cols, tileWidth);
        QList<Tile> tiles;
        quint64 previous = 0;
        for (int k = 0; k < result->kept.size(); ++k) {
            const double t = candidates.at(result->kept.at(k));
            Tile tile;
            tile.image = src.render(compositor, t, result->layout.tile.width(), result->layout.tile.height());
            if (label)
                tile.label = QStringLiteral("#%1 %2s").arg(k).arg(QString::number(t, 'f', 2));
            const quint64 hash = dHash(tile.image);
            result->diff.append(k == 0 ? 0 : hammingDistance(previous, hash));
            previous = hash;
            tiles.append(tile);
        }
        result->sheet = compose(result->layout, tiles, label);
        QMetaObject::invokeMethod(&loop, &QEventLoop::quit, Qt::QueuedConnection);
    });
    loop.exec();

    if (result->sheet.isNull() || result->kept.isEmpty())
        return err("capture_failed", QStringLiteral("Compositor returned no frame"));

    const double materialStart = sourceMode ? drift::usToSeconds(clip.srcIn) : 0.0;
    const double materialEnd = sourceMode ? drift::usToSeconds(clip.srcOut)
                                          : drift::usToSeconds(src.project->durationUs());
    int outside = 0;
    QJsonArray frames;
    for (int k = 0; k < result->kept.size(); ++k) {
        const int idx = result->kept.at(k);
        const double t = candidates.at(idx);
        QJsonObject row{{QStringLiteral("i"), k},
                        {QStringLiteral("t"), round3(t)},
                        {QStringLiteral("diff"), result->diff.at(k)}};
        if (t < materialStart || t >= materialEnd) {
            row.insert(QStringLiteral("beyond_end"), true);
            ++outside;
        }
        if (sourceMode)
            row.insert(QStringLiteral("tl"), round3(clipLocalToTimelineSeconds(clip, drift::secondsToUs(t))));
        if (!sceneOf.isEmpty()) {
            row.insert(QStringLiteral("clip"), sceneOf.at(idx).first);
            if (sceneOf.at(idx).second >= 0)
                row.insert(QStringLiteral("scene"), sceneOf.at(idx).second);
        }
        frames.append(row);
    }

    QJsonObject meta = ok({
        {QStringLiteral("space"), sourceMode ? QStringLiteral("source") : QStringLiteral("timeline")},
        {QStringLiteral("sample"), sample},
        {QStringLiteral("start"), round3(start)},
        {QStringLiteral("end"), round3(end)},
        {QStringLiteral("grid"), QStringLiteral("%1x%2").arg(result->layout.cols).arg(result->layout.rows)},
        {QStringLiteral("tile"), QJsonArray{result->layout.tile.width(), result->layout.tile.height()}},
        {QStringLiteral("frames"), frames},
        {QStringLiteral("w"), result->sheet.width()},
        {QStringLiteral("h"), result->sheet.height()},
    });
    meta.insert(QStringLiteral("dur"), round3(materialEnd - materialStart));
    if (outside > 0)
        meta.insert(QStringLiteral("beyond_end"), outside);
    if (sourceMode)
        meta.insert(QStringLiteral("clip"), clip.id);
    if (changes) {
        meta.insert(QStringLiteral("candidates"), candidates.size());
        meta.insert(QStringLiteral("skipped"), result->skipped);
        if (result->dropped) {
            meta.insert(QStringLiteral("next"),
                        QJsonObject{{QStringLiteral("start"), round3(candidates.at(result->kept.last()))},
                                    {QStringLiteral("end"), round3(end)}});
        }
    }
    if (!unscanned.isEmpty())
        meta.insert(QStringLiteral("unscanned"), unscanned);

    if (request.toPath) {
        const QString outPath = newCapturePath(m_app->m_project.id(), QStringLiteral("sheet"), QStringLiteral("jpg"));
        if (outPath.isEmpty() || !result->sheet.save(outPath, "JPEG", 80))
            return err("capture_failed", QStringLiteral("Could not write JPEG"));
        meta.insert(QStringLiteral("path"), outPath);
        return meta;
    }
    return imageResult(meta, encodeJpeg(result->sheet, 80), QStringLiteral("image/jpeg"));
}

QJsonObject McpController::activity(const ActivityRequest &request)
{
    using namespace drift::mcp;
    m_app->setPlaying(false);

    FrameSource src;
    src.project = std::make_shared<const drift::Project>(m_app->m_project.detachedCopy());
    const bool sourceMode = request.track >= 0 || request.clip >= 0;
    drift::Clip clip;
    if (sourceMode) {
        const auto &tracks = src.project->tracks();
        if (request.track < 0 || request.track >= tracks.size() || request.clip < 0
            || request.clip >= tracks.at(request.track).clips.size())
            return err("not_found", QStringLiteral("no clip at track %1 index %2")
                                        .arg(request.track).arg(request.clip));
        clip = tracks.at(request.track).clips.at(request.clip);
        if (clip.path.isEmpty() || clip.type != drift::ClipType::Video)
            return err("type_mismatch", QStringLiteral("clip has no video file; omit clip to profile the composition"));
        src.path = clip.path;
        src.rotationCorrection = clip.rotationCorrection;
    } else if (src.project->durationUs() <= 0) {
        return err("not_found", QStringLiteral("Timeline is empty"));
    }

    double start = 0.0;
    double end = 0.0;
    if (sourceMode) {
        start = drift::usToSeconds(clip.srcIn);
        end = drift::usToSeconds(clip.srcOut);
    } else if (src.project->hasWorkArea()) {
        start = drift::usToSeconds(src.project->workAreaInUs());
        end = drift::usToSeconds(src.project->workAreaOutUs());
    } else {
        end = drift::usToSeconds(src.project->durationUs());
    }
    if (request.start >= 0.0)
        start = request.start;
    if (request.end >= 0.0)
        end = request.end;
    if (end <= start)
        return err("bad_args", QStringLiteral("end must be > start (got %1..%2)").arg(start).arg(end));
    if (end - start > kMcpActivityMaxSeconds)
        return err("bad_args", QStringLiteral("range must be <= %1 seconds").arg(kMcpActivityMaxSeconds));

    const int samples = qBound(kMcpActivityMinSamples, request.samples, kMcpActivityMaxSamples);
    const double step = (end - start) / samples;

    struct Result
    {
        QVector<double> content;
        QVector<double> motion;
        QSize scan;
    };
    auto result = std::make_shared<Result>();
    QEventLoop loop;
    (void)QtConcurrent::run([=, &loop]() {
        FrameCompositor compositor;
        compositor.setProject(src.project.get());
        drift::HsvFrame previous;
        drift::HsvFrame current;
        for (int i = 0; i < samples; ++i) {
            const QImage frame = src.render(compositor, start + i * step, kMcpActivityScanWidth,
                                            kMcpActivityScanWidth * 9 / 16);
            if (frame.isNull()) {
                result->content.append(0.0);
                result->motion.append(0.0);
                continue;
            }
            if (result->scan.isEmpty())
                result->scan = frame.size();
            drift::toHsv(frame, &current);
            if (i == 0 || !previous.matches(current)) {
                result->content.append(0.0);
                result->motion.append(0.0);
            } else {
                const drift::FrameDelta delta = drift::compareFrames(previous, current);
                result->content.append(delta.content);
                result->motion.append(delta.motion);
            }
            std::swap(previous, current);
        }
        QMetaObject::invokeMethod(&loop, &QEventLoop::quit, Qt::QueuedConnection);
    });
    loop.exec();

    if (result->scan.isEmpty())
        return err("capture_failed", QStringLiteral("Compositor returned no frame"));

    QVector<float> audio;
    if (request.audio) {
        if (sourceMode)
            audio = blockingSourcePeaks(clip.path, start, end - start, samples);
        else
            audio = blockingMixedPeaks(*src.project, start, end - start, samples);
    }

    QJsonArray content;
    QJsonArray motion;
    QJsonArray audioArr;
    double maxContent = 0.0;
    double maxMotion = 0.0;
    double maxAudio = 0.0;
    for (int i = 0; i < samples; ++i) {
        const double c = std::round(result->content.at(i) * 10.0) / 10.0;
        const double m = round2(result->motion.at(i));
        content.append(c);
        motion.append(m);
        maxContent = qMax(maxContent, c);
        maxMotion = qMax(maxMotion, m);
        if (i < audio.size()) {
            const double a = round2(audio.at(i));
            audioArr.append(a);
            maxAudio = qMax(maxAudio, a);
        }
    }

    QList<QPair<double, int>> maxima;
    for (int i = 1; i < samples; ++i) {
        const double c = result->content.at(i);
        if (c <= 0.0)
            continue;
        bool isPeak = true;
        for (int j = qMax(0, i - 2); j <= qMin(samples - 1, i + 2); ++j) {
            if (j != i && result->content.at(j) > c) {
                isPeak = false;
                break;
            }
        }
        if (isPeak)
            maxima.append({c, i});
    }
    std::sort(maxima.begin(), maxima.end(),
              [](const auto &a, const auto &b) { return a.first > b.first; });
    const int keep = qBound(0, request.peaks, 30);
    if (maxima.size() > keep)
        maxima.resize(keep);
    std::sort(maxima.begin(), maxima.end(),
              [](const auto &a, const auto &b) { return a.second < b.second; });
    QJsonArray peaks;
    for (const auto &m : maxima) {
        peaks.append(QJsonObject{{QStringLiteral("t"), round3(start + m.second * step)},
                                 {QStringLiteral("content"), std::round(m.first * 10.0) / 10.0}});
    }

    QJsonObject reply = ok({
        {QStringLiteral("space"), sourceMode ? QStringLiteral("source") : QStringLiteral("timeline")},
        {QStringLiteral("start"), round3(start)},
        {QStringLiteral("end"), round3(end)},
        {QStringLiteral("step"), round3(step)},
        {QStringLiteral("n"), samples},
        {QStringLiteral("scan"), QJsonArray{result->scan.width(), result->scan.height()}},
        {QStringLiteral("content"), content},
        {QStringLiteral("motion"), motion},
        {QStringLiteral("peaks"), peaks},
        {QStringLiteral("max"), QJsonObject{{QStringLiteral("content"), maxContent},
                                            {QStringLiteral("motion"), maxMotion},
                                            {QStringLiteral("audio"), maxAudio}}},
    });
    if (sourceMode)
        reply.insert(QStringLiteral("clip"), clip.id);
    if (!audioArr.isEmpty())
        reply.insert(QStringLiteral("audio"), audioArr);
    return reply;
}

namespace {

QString shortHash(const QString &hash)
{
    return hash.left(12);
}

} // namespace

QJsonObject McpController::listHistory(int limit) const
{
    using namespace drift::mcp;
    QJsonArray entries;
    const QString dir = m_app->historySnapshotDir();
    const int total = m_app->m_undoStack.count() + 1;
    for (int i = total - 1; i >= 0 && entries.size() < qMax(1, limit); --i) {
        const QString hash = m_app->historyHashAt(i);
        QJsonObject entry{{QStringLiteral("index"), i},
                          {QStringLiteral("label"), i == 0 ? QStringLiteral("Origin") : m_app->m_undoStack.text(i - 1)},
                          {QStringLiteral("short"), shortHash(hash)}};
        if (QFile::exists(dir + QLatin1Char('/') + hash + QStringLiteral(".json")))
            entry.insert(QStringLiteral("snapshot"), true);
        entries.append(entry);
    }
    const int current = m_app->m_undoStack.index();
    const QString head = m_app->historyHashAt(current);
    return ok({{QStringLiteral("entries"), entries},
               {QStringLiteral("current"), current},
               {QStringLiteral("hash"), head},
               {QStringLiteral("short"), shortHash(head)},
               {QStringLiteral("total"), total},
               {QStringLiteral("linear"), true}});
}

QJsonObject McpController::undoTo(int index, const QString &hash)
{
    using namespace drift::mcp;
    int target = index;
    if (!hash.trimmed().isEmpty()) {
        target = m_app->historyIndexForHash(hash);
        if (target < 0)
            return err("not_found", QStringLiteral("No history entry matches that hash"));
    } else if (index < 0) {
        return err("bad_args", QStringLiteral("index or hash required"));
    }
    if (target < 0 || target > m_app->m_undoStack.count())
        return err("bad_args", QStringLiteral("index out of range"));
    m_app->m_undoStack.setIndex(target);
    ++m_editRevision;
    const QString at = m_app->historyHashAt(m_app->m_undoStack.index());
    return ok({{QStringLiteral("index"), m_app->m_undoStack.index()},
               {QStringLiteral("hash"), at},
               {QStringLiteral("short"), shortHash(at)}});
}

QJsonObject McpController::takeSnapshot(const QString &label)
{
    using namespace drift::mcp;
    const int current = m_app->m_undoStack.index();
    const QByteArray json = m_app->historyJsonAt(current);
    const QString hash = m_app->historyHashAt(current);
    if (json.isEmpty() || hash.isEmpty())
        return err("bad_args", QStringLiteral("Nothing to snapshot"));

    const QString dir = m_app->historySnapshotDir();
    if (!QDir().mkpath(dir))
        return err("bad_args", QStringLiteral("Could not create history folder"));
    const QString path = dir + QLatin1Char('/') + hash + QStringLiteral(".json");
    bool existed = QFile::exists(path);
    if (!existed) {
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly))
            return err("bad_args", QStringLiteral("Could not write snapshot"));
        file.write(json);
        if (!file.commit())
            return err("bad_args", QStringLiteral("Could not write snapshot"));
        m_app->pruneHistorySnapshots();
    }
    QJsonObject extra{{QStringLiteral("hash"), hash},
                      {QStringLiteral("short"), shortHash(hash)},
                      {QStringLiteral("path"), path},
                      {QStringLiteral("index"), current},
                      {QStringLiteral("existed"), existed},
                      {QStringLiteral("bytes"), json.size()}};
    if (!label.trimmed().isEmpty())
        extra.insert(QStringLiteral("label"), label.trimmed());
    else if (current > 0)
        extra.insert(QStringLiteral("label"), m_app->m_undoStack.text(current - 1));
    else
        extra.insert(QStringLiteral("label"), QStringLiteral("Origin"));
    return ok(extra);
}

QJsonObject McpController::listSnapshots() const
{
    using namespace drift::mcp;
    QJsonArray snapshots;
    QDir dir(m_app->historySnapshotDir());
    const QFileInfoList files =
        dir.entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Time);
    for (const QFileInfo &info : files) {
        const QString hash = info.completeBaseName();
        snapshots.append(QJsonObject{
            {QStringLiteral("hash"), hash},
            {QStringLiteral("short"), shortHash(hash)},
            {QStringLiteral("path"), info.absoluteFilePath()},
            {QStringLiteral("bytes"), info.size()},
            {QStringLiteral("savedAt"), info.lastModified().toUTC().toString(Qt::ISODate)},
        });
    }
    return ok({{QStringLiteral("snapshots"), snapshots}, {QStringLiteral("n"), snapshots.size()}});
}

QJsonObject McpController::restoreSnapshot(const QString &hash)
{
    using namespace drift::mcp;
    const QString needle = hash.trimmed();
    if (needle.size() < 8)
        return err("bad_args", QStringLiteral("hash required"));

    const int onStack = m_app->historyIndexForHash(needle);
    if (onStack >= 0)
        return undoTo(onStack, {});

    QDir dir(m_app->historySnapshotDir());
    QString path;
    int matches = 0;
    const QFileInfoList files =
        dir.entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    for (const QFileInfo &info : files) {
        const QString name = info.completeBaseName();
        if (name.startsWith(needle, Qt::CaseInsensitive) || needle.startsWith(name, Qt::CaseInsensitive)) {
            ++matches;
            path = info.absoluteFilePath();
            if (name.compare(needle, Qt::CaseInsensitive) == 0) {
                path = info.absoluteFilePath();
                matches = 1;
                break;
            }
        }
    }
    if (matches != 1 || path.isEmpty())
        return err("not_found", QStringLiteral("No snapshot matches that hash"));

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return err("bad_args", QStringLiteral("Could not read snapshot"));
    const QByteArray json = file.readAll();
    const QString fileHash = QString::fromLatin1(
        QCryptographicHash::hash(json, QCryptographicHash::Sha256).toHex());
    // Snapshots are hashed without transcripts; keep the session's so restoring one never loses them.
    const auto transcripts = m_app->m_project.transcripts();
    QString error;
    if (!m_app->applyProjectJson(json, &error))
        return err("bad_args", error.isEmpty() ? QStringLiteral("Snapshot refused") : error);
    for (auto it = transcripts.cbegin(); it != transcripts.cend(); ++it) {
        if (!m_app->m_project.transcript(it.key()))
            m_app->m_project.setTranscript(it.key(), it.value());
    }
    ++m_editRevision;
    return ok({{QStringLiteral("index"), 0},
               {QStringLiteral("hash"), fileHash},
               {QStringLiteral("short"), shortHash(fileHash)},
               {QStringLiteral("reset"), true}});
}

namespace {

// Caps for the audio reads. The MCP transport has no chunking — textResult serialises one
// compact JSON block and writes it whole — so every unbounded array needs a ceiling here.
constexpr int kMcpDefaultBuckets = 400;
constexpr int kMcpMaxBuckets = 4096;   // matches waveformPeaksRange
constexpr double kMcpMaxWaveformSeconds = 3600.0;
constexpr double kMcpMaxBeatSeconds = 600.0;  // bounds the windowed mix to ~53 MB of mono
constexpr int kMcpMaxBeats = 2000;
constexpr int kMcpMaxOnsets = 500;


QJsonObject peaksReply(const QVector<float> &peaks, double startSeconds, double durSeconds,
                       const QString &source)
{
    using namespace drift::mcp;
    if (peaks.isEmpty())
        return err("not_found", QStringLiteral("No audio decoded for that range"));

    QJsonArray values;
    double maxPeak = 0.0;
    for (float p : peaks) {
        values.append(round3(p));
        maxPeak = qMax(maxPeak, static_cast<double>(p));
    }
    return ok({
        {QStringLiteral("source"), source},
        {QStringLiteral("start"), round3(startSeconds)},
        {QStringLiteral("duration"), round3(durSeconds)},
        {QStringLiteral("buckets"), values.size()},
        {QStringLiteral("max"), round3(maxPeak)},
        {QStringLiteral("peaks"), values},
    });
}

} // namespace

QJsonObject McpController::waveformForClip(int trackIndex, int clipIndex, int buckets) const
{
    using namespace drift::mcp;
    const QList<drift::Track> &tracks = m_app->m_project.tracks();
    if (trackIndex < 0 || trackIndex >= tracks.size())
        return err("not_found", QStringLiteral("No such track"));
    const drift::Track &track = tracks.at(trackIndex);
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return err("not_found", QStringLiteral("No such clip"));

    const drift::Clip &clip = track.clips.at(clipIndex);
    if (clip.path.isEmpty())
        return err("type_mismatch", QStringLiteral("Clip has no media file (text or shape clip)"));

    const double srcIn = drift::usToSeconds(clip.srcIn);
    const double span = drift::usToSeconds(clip.srcOut - clip.srcIn);
    if (span <= 0.0)
        return err("type_mismatch", QStringLiteral("Clip has no source span"));

    const QVector<float> peaks = blockingSourcePeaks(clip.path, srcIn, span, buckets);
    return peaksReply(peaks, srcIn, span, QStringLiteral("clip"));
}

QJsonObject McpController::waveformForAsset(const QString &assetId, double startSeconds,
                                               double durSeconds, int buckets) const
{
    using namespace drift::mcp;
    const drift::MediaAsset *asset = m_app->m_project.asset(assetId);
    if (!asset)
        return err("not_found", QStringLiteral("Unknown asset"));
    if (asset->path.isEmpty())
        return err("type_mismatch", QStringLiteral("Asset has no file"));

    const double total = drift::usToSeconds(asset->durationUs);
    const double start = qMax(0.0, startSeconds);
    const double span = durSeconds > 0.0 ? qMin(durSeconds, total - start) : total - start;
    if (span <= 0.0)
        return err("bad_args", QStringLiteral("Range is past the end of the asset"));
    if (span > kMcpMaxWaveformSeconds) {
        return err("bad_args", QStringLiteral("duration must be <= %1 seconds")
                                   .arg(kMcpMaxWaveformSeconds));
    }

    const QVector<float> peaks = blockingSourcePeaks(asset->path, start, span, buckets);
    return peaksReply(peaks, start, span, QStringLiteral("asset"));
}

QJsonObject McpController::waveformForTimeline(double startSeconds, double durSeconds,
                                                  int buckets) const
{
    using namespace drift::mcp;
    if (durSeconds <= 0.0)
        return err("bad_args", QStringLiteral("duration must be > 0"));
    if (durSeconds > kMcpMaxWaveformSeconds) {
        return err("bad_args", QStringLiteral("duration must be <= %1 seconds")
                                   .arg(kMcpMaxWaveformSeconds));
    }

    const double start = qMax(0.0, startSeconds);
    const drift::TimeUs startUs = drift::secondsToUs(start);
    const int rate = 8000; // envelope only; the sample rate does not change where the peaks land
    const qint64 frames = static_cast<qint64>(durSeconds * rate);
    if (frames <= 0)
        return err("bad_args", QStringLiteral("duration is too short to measure"));

    const drift::Project snap = m_app->m_project;
    auto raw = std::make_shared<QVector<float>>();
    QEventLoop loop;
    (void)QtConcurrent::run([snap, startUs, frames, rate, buckets, raw, &loop]() {
        AudioMixer mixer;
        mixer.setProject(&snap);
        const int peakBuckets = static_cast<int>(qMin<qint64>(buckets, frames));
        *raw = MediaWaveform::mixedPeaks(
            frames, rate, peakBuckets,
            [&mixer, startUs, rate](float *out, qint64 frameOffset, int maxFrames) {
                const drift::TimeUs at = startUs + frameOffset * drift::kUsPerSecond / rate;
                mixer.mix(at, maxFrames, rate, out);
                return maxFrames;
            });
        QMetaObject::invokeMethod(&loop, &QEventLoop::quit, Qt::QueuedConnection);
    });
    loop.exec();

    // The mixer always returns samples, silent or not, so an all-zero result is a real answer
    // here rather than the "nothing decoded" peaksReply reports for a source read.
    if (raw->isEmpty())
        return err("not_found", QStringLiteral("Nothing to mix in that range"));
    return peaksReply(*raw, start, durSeconds, QStringLiteral("timeline"));
}

QJsonObject McpController::beatPayload() const
{
    using namespace drift::mcp;
    if (m_app->m_beatAnalysis.isEmpty())
        return err("not_found", QStringLiteral("No beat analysis yet — call detect_beats first"));

    QJsonArray beats;
    for (double b : m_app->m_beatAnalysisRaw.beats) {
        if (beats.size() >= kMcpMaxBeats)
            break;
        beats.append(round3(b));
    }

    // Keep the strongest onsets when there are too many, then put them back in time order —
    // a truncation by time would silently hide the whole back half of the range.
    QList<AudioOnset> onsets = m_app->m_beatAnalysisRaw.onsets;
    if (onsets.size() > kMcpMaxOnsets) {
        std::partial_sort(onsets.begin(), onsets.begin() + kMcpMaxOnsets, onsets.end(),
                          [](const AudioOnset &a, const AudioOnset &b) {
                              return a.strength > b.strength;
                          });
        onsets.resize(kMcpMaxOnsets);
        std::sort(onsets.begin(), onsets.end(),
                  [](const AudioOnset &a, const AudioOnset &b) { return a.seconds < b.seconds; });
    }
    QJsonArray onsetRows;
    for (const AudioOnset &o : std::as_const(onsets)) {
        onsetRows.append(QJsonObject{{QStringLiteral("at"), round3(o.seconds)},
                                     {QStringLiteral("s"), round2(o.strength)}});
    }

    return ok({
        {QStringLiteral("start"), round3(m_app->m_beatAnalysis.value(QStringLiteral("rangeStart")).toDouble())},
        {QStringLiteral("duration"), round3(m_app->m_beatAnalysis.value(QStringLiteral("rangeDuration")).toDouble())},
        {QStringLiteral("bpm"), round2(m_app->m_beatAnalysisRaw.bpm)},
        {QStringLiteral("confidence"), round2(m_app->m_beatAnalysisRaw.confidence)},
        {QStringLiteral("beatsPerBar"), m_app->m_beatAnalysisRaw.beatsPerBar},
        {QStringLiteral("firstDownbeat"), m_app->m_beatAnalysisRaw.firstDownbeat},
        {QStringLiteral("beats"), beats},
        {QStringLiteral("onsets"), onsetRows},
        {QStringLiteral("truncated"), beats.size() < m_app->m_beatAnalysisRaw.beats.size()
                                          || onsetRows.size() < m_app->m_beatAnalysisRaw.onsets.size()},
        {QStringLiteral("gridVisible"), m_app->m_beatGridVisible},
        {QStringLiteral("onsetsVisible"), m_app->m_onsetsVisible},
    });
}

QJsonObject McpController::detectBeats(double startSeconds, double durSeconds, bool force)
{
    using namespace drift::mcp;
    if (durSeconds < AudioOnsets::kMinAnalysisSec) {
        return err("bad_args", QStringLiteral("duration must be >= %1 seconds to find a tempo")
                                   .arg(AudioOnsets::kMinAnalysisSec));
    }
    if (durSeconds > kMcpMaxBeatSeconds) {
        return err("bad_args",
                   QStringLiteral("duration must be <= %1 seconds").arg(kMcpMaxBeatSeconds));
    }

    const double start = qMax(0.0, startSeconds);

    // A cached grid is only reusable if the audio it describes has not moved since.
    if (!force && !m_app->m_beatAnalysis.isEmpty()
        && qFuzzyCompare(m_app->m_beatAnalysis.value(QStringLiteral("rangeStart")).toDouble() + 1.0,
                         start + 1.0)
        && qFuzzyCompare(m_app->m_beatAnalysis.value(QStringLiteral("rangeDuration")).toDouble() + 1.0,
                         durSeconds + 1.0)
        && m_app->m_beatAudioFingerprint == m_app->audioLayoutFingerprint()) {
        QJsonObject cached = beatPayload();
        cached.insert(QStringLiteral("cached"), true);
        return cached;
    }

    // The editor may have started its own pass for the visible range. Two analyses fighting
    // over one result slot would leave whichever finished second describing the other's range.
    if (m_app->m_beatAnalysisRunning)
        return err("conflict", QStringLiteral("A beat analysis is already running"));

    const drift::TimeUs startUs = drift::secondsToUs(start);
    const drift::TimeUs durUs = drift::secondsToUs(durSeconds);
    const quint64 generation = ++m_app->m_beatAnalysisGeneration;

    m_app->m_beatAnalysisRunning = true;
    emit m_app->beatAnalysisChanged();

    const drift::Project snap = m_app->m_project;
    const QByteArray fingerprint = m_app->audioLayoutFingerprint();

    auto analysis = std::make_shared<AudioBeatAnalysis>();
    QEventLoop loop;
    (void)QtConcurrent::run([snap, startUs, durUs, start, analysis, &loop]() {
        *analysis = runBeatAnalysis(snap, startUs, durUs, start);
        QMetaObject::invokeMethod(&loop, &QEventLoop::quit, Qt::QueuedConnection);
    });
    loop.exec();

    // The nested loop kept the GUI live, so clearBeatAnalysis() or another pass could have run
    // underneath us. Publishing now would resurrect a range the user already dismissed.
    if (generation != m_app->m_beatAnalysisGeneration)
        return err("conflict", QStringLiteral("Beat analysis was superseded"));

    // applyBeatAnalysis is the shared publish path: it fills m_beatAnalysis, rebuilds the snap
    // targets, stores the staleness fingerprint, clears the running flag and resumes any
    // pending effect template.
    m_app->applyBeatAnalysis(*analysis, start, durSeconds, fingerprint);

    QJsonObject payload = beatPayload();
    payload.insert(QStringLiteral("cached"), false);
    return payload;
}

QJsonObject McpController::setBeatLayers(bool grid, bool onsets)
{
    using namespace drift::mcp;
    m_app->setBeatGridVisible(grid);
    m_app->setOnsetsVisible(onsets);
    return ok({
        {QStringLiteral("gridVisible"), m_app->m_beatGridVisible},
        {QStringLiteral("onsetsVisible"), m_app->m_onsetsVisible},
        {QStringLiteral("snapTargets"), static_cast<int>(m_app->m_beatSnapTargets.size())},
        {QStringLiteral("snapEnabled"), m_app->m_snapEnabled},
    });
}

QList<double> McpController::beatTimes(const QString &unit, double minStrength) const
{
    QList<double> times;
    const QString u = unit.trimmed().toLower();

    if (u == QLatin1String("onset")) {
        for (const AudioOnset &o : m_app->m_beatAnalysisRaw.onsets) {
            if (o.strength >= minStrength)
                times.append(o.seconds);
        }
        return times;
    }

    const QList<double> &beats = m_app->m_beatAnalysisRaw.beats;
    if (u == QLatin1String("bar")) {
        const int per = qMax(1, m_app->m_beatAnalysisRaw.beatsPerBar);
        // Count bars from the detected downbeat, not from index 0, or every bar line lands on
        // whichever beat happened to be first in the analysed window.
        const int first = qBound(0, m_app->m_beatAnalysisRaw.firstDownbeat, qMax(0, beats.size() - 1));
        for (int i = first; i < beats.size(); i += per)
            times.append(beats.at(i));
        return times;
    }

    times = beats;
    return times;
}

int McpController::bookmarkBeats(double startSeconds, double durSeconds, const QString &unit,
                                    double minStrength, const QString &labelPrefix)
{
    const QList<double> times = beatTimes(unit, minStrength);
    if (times.isEmpty())
        return 0;

    const bool ranged = durSeconds > 0.0;
    const double from = qMax(0.0, startSeconds);
    const double to = from + durSeconds;

    const drift::Project before = m_app->m_project;
    QList<drift::Bookmark> marks = m_app->m_project.bookmarks();
    int added = 0;
    int n = 1;
    for (double t : times) {
        if (t < from || (ranged && t > to)) {
            ++n;
            continue;
        }
        const drift::TimeUs at = drift::secondsToUs(t);
        // Bookmarks are snap targets themselves; stacking several inside one snap threshold
        // would make the magnet ambiguous rather than stronger.
        const bool crowded = std::any_of(marks.cbegin(), marks.cend(),
                                         [at](const drift::Bookmark &b) {
                                             return qAbs(b.timeUs - at) < drift::kSnapThresholdUs;
                                         });
        if (crowded) {
            ++n;
            continue;
        }
        marks.append(drift::Bookmark{at, QStringLiteral("%1 %2").arg(labelPrefix).arg(n)});
        ++added;
        ++n;
    }
    if (added == 0)
        return 0;

    std::sort(marks.begin(), marks.end(),
              [](const drift::Bookmark &a, const drift::Bookmark &b) { return a.timeUs < b.timeUs; });
    m_app->m_project.bookmarks() = marks;
    m_app->pushProjectEdit(before, QStringLiteral("Bookmark beats"));
    m_app->finishEdit(QStringLiteral("Bookmark beats"));
    return added;
}

// --- scene toolbox ----------------------------------------------------------

namespace {

// Map a moment in a clip's source to where it lands on the timeline, through trim, speed
// and reverse. Agents act in timeline seconds, so every scene time is reported both ways
// rather than leaving this mapping for the caller to rediscover.
double sceneSourceToTimeline(const drift::Clip &clip, double sourceSeconds)
{
    const drift::TimeUs span = clip.sourceSpanUs();
    if (span <= 0)
        return drift::usToSeconds(clip.timelineStart);

    const double through =
        qBound(0.0, double(drift::secondsToUs(sourceSeconds) - clip.srcIn) / double(span), 1.0);
    const double offset = (clip.reverse ? 1.0 - through : through)
                          * drift::usToSeconds(clip.timelineDuration);
    return drift::usToSeconds(clip.timelineStart) + offset;
}

bool sceneMatches(const QVariantMap &scene, const QString &label, double minScore)
{
    if (scene.value(QStringLiteral("score")).toDouble() < minScore)
        return false;
    if (label.isEmpty())
        return true;
    return scene.value(QStringLiteral("labels")).toStringList().contains(label,
                                                                        Qt::CaseInsensitive);
}

} // namespace

QJsonObject McpController::detectScenes(int trackIndex, int clipIndex, double threshold,
                                           double minScene, bool withObjects)
{
    using namespace drift::mcp;
    if (trackIndex < 0 || trackIndex >= m_app->m_project.tracks().size())
        return err("not_found", QStringLiteral("No such track"));
    const drift::Track &track = m_app->m_project.tracks().at(trackIndex);
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return err("not_found", QStringLiteral("No such clip"));

    const drift::Clip &clip = track.clips.at(clipIndex);
    if (clip.type != drift::ClipType::Video)
        return err("bad_args", QStringLiteral("Scene detection needs a video clip"));
    if (clip.path.isEmpty() || clip.srcOut <= clip.srcIn)
        return err("bad_args", QStringLiteral("That clip has no video to scan"));
    if (withObjects && !m_app->objectDetectionAvailable()) {
        return err("not_found",
                   QStringLiteral("Object labelling needs the object-model addon — ask the user "
                                  "to install it from Extras"));
    }
    if (m_app->scenes()->detecting())
        return err("conflict", QStringLiteral("A scene scan is already running"));

    if (threshold > 0.0)
        m_app->scenes()->setThreshold(threshold);

    const drift::SceneDetectRequest request = m_app->scenes()->sceneRequestFor(clip, withObjects, minScene);

    // Report a cache hit synchronously: an agent that would otherwise poll for an async job
    // can carry straight on to list_scenes.
    drift::SceneAnalysis cached;
    if (drift::loadCachedAnalysis(request, &cached) && (!withObjects || cached.objectsScanned)) {
        m_app->scenes()->applySceneAnalysis(cached, clip.id, clip.path, clip.rotationCorrection);
        return ok({{QStringLiteral("cached"), true},
                   {QStringLiteral("clip"), clip.id},
                   {QStringLiteral("scenes"), int(cached.scenes.size())},
                   {QStringLiteral("cuts"), int(cached.cuts.size())}});
    }

    m_app->scenes()->detect(trackIndex, clipIndex, withObjects, minScene);
    return ok({{QStringLiteral("started"), true}, {QStringLiteral("clip"), clip.id}});
}

QVariantList McpController::sceneRows(int trackIndex, int clipIndex, const drift::Clip **clip) const
{
    *clip = nullptr;
    if (trackIndex < 0 || clipIndex < 0) {
        for (const drift::Track &track : m_app->m_project.tracks()) {
            for (const drift::Clip &candidate : track.clips) {
                if (candidate.id == m_app->scenes()->clipId())
                    *clip = &candidate;
            }
        }
        return *clip ? m_app->scenes()->list() : QVariantList{};
    }
    const auto &tracks = m_app->m_project.tracks();
    if (trackIndex >= tracks.size() || clipIndex >= tracks.at(trackIndex).clips.size())
        return {};
    *clip = &tracks.at(trackIndex).clips.at(clipIndex);
    if ((*clip)->id == m_app->scenes()->clipId())
        return m_app->scenes()->list();
    drift::SceneAnalysis analysis;
    if (drift::loadCachedAnalysis(m_app->scenes()->sceneRequestFor(**clip, true, 0.0), &analysis)
        || drift::loadCachedAnalysis(m_app->scenes()->sceneRequestFor(**clip, false, 0.0), &analysis))
        return sceneRowsFromAnalysis(analysis);
    return {};
}

QJsonObject McpController::listScenes(const QString &label, double minScore,
                                         const QString &sort, int limit, int trackIndex,
                                         int clipIndex) const
{
    using namespace drift::mcp;
    const drift::Clip *clip = nullptr;
    const QVariantList scenes = sceneRows(trackIndex, clipIndex, &clip);
    if (trackIndex >= 0 && !clip)
        return err("not_found", QStringLiteral("no clip at track %1 index %2").arg(trackIndex).arg(clipIndex));
    if (scenes.isEmpty()) {
        return err("not_found", clip ? QStringLiteral("Clip %1 has no scene analysis — call detect_scenes({clip}) first").arg(clip->id)
                                     : QStringLiteral("No scene analysis yet — call detect_scenes first"));
    }

    QList<QVariantMap> rows;
    for (const QVariant &value : scenes) {
        const QVariantMap scene = value.toMap();
        if (sceneMatches(scene, label, minScore))
            rows.append(scene);
    }

    if (sort.compare(QLatin1String("score"), Qt::CaseInsensitive) == 0) {
        std::sort(rows.begin(), rows.end(), [](const QVariantMap &a, const QVariantMap &b) {
            return a.value(QStringLiteral("score")).toDouble()
                   > b.value(QStringLiteral("score")).toDouble();
        });
    }

    QJsonArray out;
    for (const QVariantMap &scene : std::as_const(rows)) {
        if (limit > 0 && out.size() >= limit)
            break;
        const double sourceStart = scene.value(QStringLiteral("sourceStart")).toDouble();
        const double sourceEnd = scene.value(QStringLiteral("sourceEnd")).toDouble();
        QJsonArray labels;
        for (const QString &name : scene.value(QStringLiteral("labels")).toStringList())
            labels.append(name);
        out.append(QJsonObject{
            {QStringLiteral("index"), scene.value(QStringLiteral("index")).toInt()},
            {QStringLiteral("start"), sourceStart},
            {QStringLiteral("end"), sourceEnd},
            {QStringLiteral("duration"), scene.value(QStringLiteral("duration")).toDouble()},
            {QStringLiteral("timeline_start"), sceneSourceToTimeline(*clip, sourceStart)},
            {QStringLiteral("timeline_end"), sceneSourceToTimeline(*clip, sourceEnd)},
            {QStringLiteral("thumb"), scene.value(QStringLiteral("thumbnailSeconds")).toDouble()},
            {QStringLiteral("timeline_thumb"),
             sceneSourceToTimeline(*clip, scene.value(QStringLiteral("thumbnailSeconds")).toDouble())},
            {QStringLiteral("motion"), scene.value(QStringLiteral("motion")).toDouble()},
            {QStringLiteral("loudness"), scene.value(QStringLiteral("loudness")).toDouble()},
            {QStringLiteral("objects"), scene.value(QStringLiteral("objects")).toDouble()},
            {QStringLiteral("score"), scene.value(QStringLiteral("score")).toDouble()},
            {QStringLiteral("labels"), labels},
        });
    }

    return ok({{QStringLiteral("clip"), clip->id},
               {QStringLiteral("scenes"), out},
               {QStringLiteral("n"), out.size()},
               {QStringLiteral("total"), int(scenes.size())}});
}

QJsonObject McpController::describeClip(int topCount, int trackIndex, int clipIndex) const
{
    using namespace drift::mcp;
    const drift::Clip *clip = nullptr;
    const QVariantList scenes = sceneRows(trackIndex, clipIndex, &clip);
    if (trackIndex >= 0 && !clip)
        return err("not_found", QStringLiteral("no clip at track %1 index %2").arg(trackIndex).arg(clipIndex));
    if (scenes.isEmpty()) {
        return err("not_found", clip ? QStringLiteral("Clip %1 has no scene analysis — call detect_scenes({clip}) first").arg(clip->id)
                                     : QStringLiteral("No scene analysis yet — call detect_scenes first"));
    }

    double shortest = std::numeric_limits<double>::max();
    double longest = 0.0;
    double totalScore = 0.0;
    double totalDuration = 0.0;

    // Screen time per object class, which is the figure that says what a clip is actually
    // *of* — a label on one brief shot means much less than one spanning half the footage.
    QHash<QString, int> labelScenes;
    QHash<QString, double> labelSeconds;

    for (const QVariant &value : scenes) {
        const QVariantMap scene = value.toMap();
        const double duration = scene.value(QStringLiteral("duration")).toDouble();
        shortest = qMin(shortest, duration);
        longest = qMax(longest, duration);
        totalScore += scene.value(QStringLiteral("score")).toDouble();
        totalDuration += duration;
        for (const QString &name : scene.value(QStringLiteral("labels")).toStringList()) {
            labelScenes[name] += 1;
            labelSeconds[name] += duration;
        }
    }

    QList<QString> names = labelScenes.keys();
    std::sort(names.begin(), names.end(), [&labelSeconds](const QString &a, const QString &b) {
        return labelSeconds.value(a) > labelSeconds.value(b);
    });
    QJsonArray labels;
    for (const QString &name : std::as_const(names)) {
        labels.append(QJsonObject{{QStringLiteral("name"), name},
                                  {QStringLiteral("scenes"), labelScenes.value(name)},
                                  {QStringLiteral("seconds"), labelSeconds.value(name)}});
    }

    QList<QVariantMap> ranked;
    for (const QVariant &value : scenes)
        ranked.append(value.toMap());
    std::sort(ranked.begin(), ranked.end(), [](const QVariantMap &a, const QVariantMap &b) {
        return a.value(QStringLiteral("score")).toDouble()
               > b.value(QStringLiteral("score")).toDouble();
    });

    QJsonArray top;
    for (const QVariantMap &scene : std::as_const(ranked)) {
        if (top.size() >= qMax(0, topCount))
            break;
        top.append(QJsonObject{
            {QStringLiteral("index"), scene.value(QStringLiteral("index")).toInt()},
            {QStringLiteral("start"), scene.value(QStringLiteral("sourceStart")).toDouble()},
            {QStringLiteral("duration"), scene.value(QStringLiteral("duration")).toDouble()},
            {QStringLiteral("score"), scene.value(QStringLiteral("score")).toDouble()},
        });
    }

    bool objectsScanned = false;
    for (const QVariant &value : scenes) {
        if (!value.toMap().value(QStringLiteral("labels")).toStringList().isEmpty()) {
            objectsScanned = true;
            break;
        }
    }

    return ok({{QStringLiteral("clip"), clip->id},
               {QStringLiteral("duration"), totalDuration},
               {QStringLiteral("scenes"), int(scenes.size())},
               {QStringLiteral("cuts"), int(scenes.size()) - 1},
               {QStringLiteral("shortest"), shortest},
               {QStringLiteral("longest"), longest},
               {QStringLiteral("mean_score"), totalScore / scenes.size()},
               {QStringLiteral("objects_scanned"), objectsScanned},
               {QStringLiteral("labels"), labels},
               {QStringLiteral("top"), top}});
}

QJsonObject McpController::findScenes(const QString &label, double minScore, int trackIndex,
                                         int limit) const
{
    using namespace drift::mcp;

    struct Hit
    {
        QString clipId;
        int index = 0;
        double start = 0.0;
        double end = 0.0;
        double timelineStart = 0.0;
        double timelineEnd = 0.0;
        double score = 0.0;
        QStringList labels;
    };

    QList<Hit> hits;
    QJsonArray unscanned;

    for (int t = 0; t < m_app->m_project.tracks().size(); ++t) {
        if (trackIndex >= 0 && t != trackIndex)
            continue;
        for (const drift::Clip &clip : m_app->m_project.tracks().at(t).clips) {
            if (clip.type != drift::ClipType::Video || clip.path.isEmpty())
                continue;

            // Read from the on-disk cache rather than the live analysis: only one clip's
            // scenes are live at a time, and the point of this op is to search them all.
            // Labelled and unlabelled scans are cached separately, so look for both rather
            // than reporting a clip as unscanned because only the labelled pass exists.
            drift::SceneAnalysis analysis;
            if (!drift::loadCachedAnalysis(m_app->scenes()->sceneRequestFor(clip, true, 0.0), &analysis)
                && !drift::loadCachedAnalysis(m_app->scenes()->sceneRequestFor(clip, false, 0.0), &analysis)) {
                unscanned.append(clip.id);
                continue;
            }

            for (int i = 0; i < analysis.scenes.size(); ++i) {
                const drift::Scene &scene = analysis.scenes.at(i);
                if (scene.score < minScore)
                    continue;
                if (!label.isEmpty() && !scene.labels.contains(label, Qt::CaseInsensitive))
                    continue;

                Hit hit;
                hit.clipId = clip.id;
                hit.index = i;
                hit.start = drift::usToSeconds(scene.sourceIn);
                hit.end = drift::usToSeconds(scene.sourceOut);
                hit.timelineStart = sceneSourceToTimeline(clip, hit.start);
                hit.timelineEnd = sceneSourceToTimeline(clip, hit.end);
                hit.score = scene.score;
                hit.labels = scene.labels;
                hits.append(hit);
            }
        }
    }

    std::sort(hits.begin(), hits.end(),
              [](const Hit &a, const Hit &b) { return a.score > b.score; });

    QJsonArray out;
    for (const Hit &hit : std::as_const(hits)) {
        if (limit > 0 && out.size() >= limit)
            break;
        QJsonArray labels;
        for (const QString &name : hit.labels)
            labels.append(name);
        out.append(QJsonObject{{QStringLiteral("clip"), hit.clipId},
                               {QStringLiteral("index"), hit.index},
                               {QStringLiteral("start"), hit.start},
                               {QStringLiteral("end"), hit.end},
                               {QStringLiteral("timeline_start"), hit.timelineStart},
                               {QStringLiteral("timeline_end"), hit.timelineEnd},
                               {QStringLiteral("score"), hit.score},
                               {QStringLiteral("labels"), labels}});
    }

    return ok({{QStringLiteral("scenes"), out},
               {QStringLiteral("n"), out.size()},
               {QStringLiteral("unscanned"), unscanned}});
}

QList<double> McpController::sceneCutTimes(double minScore, const QString &label) const
{
    if (m_app->scenes()->list().isEmpty())
        return {};

    const drift::Clip *clip = nullptr;
    for (const drift::Track &track : m_app->m_project.tracks()) {
        for (const drift::Clip &candidate : track.clips) {
            if (candidate.id == m_app->scenes()->clipId()) {
                clip = &candidate;
                break;
            }
        }
    }
    if (!clip)
        return {};

    QList<double> times;
    for (const QVariant &value : m_app->scenes()->list()) {
        const QVariantMap scene = value.toMap();
        // Scene 0 opens at the clip's own start, which is not a cut.
        if (scene.value(QStringLiteral("index")).toInt() == 0)
            continue;
        if (!sceneMatches(scene, label, minScore))
            continue;
        times.append(
            sceneSourceToTimeline(*clip, scene.value(QStringLiteral("sourceStart")).toDouble()));
    }
    // Reverse playback maps later source times to earlier timeline ones.
    std::sort(times.begin(), times.end());
    return times;
}

int McpController::bookmarkScenes(double minScore, const QString &label,
                                     const QString &labelPrefix)
{
    const QList<double> times = sceneCutTimes(minScore, label);
    if (times.isEmpty())
        return 0;

    const drift::Project before = m_app->m_project;
    QList<drift::Bookmark> marks = m_app->m_project.bookmarks();
    int added = 0;
    int n = 1;
    for (double t : times) {
        const drift::TimeUs at = drift::secondsToUs(t);
        // Same reasoning as bookmark_beats: bookmarks are snap targets, and stacking several
        // inside one snap threshold makes the magnet ambiguous rather than stronger.
        const bool crowded =
            std::any_of(marks.cbegin(), marks.cend(), [at](const drift::Bookmark &b) {
                return qAbs(b.timeUs - at) < drift::kSnapThresholdUs;
            });
        if (crowded) {
            ++n;
            continue;
        }
        marks.append(drift::Bookmark{at, QStringLiteral("%1 %2").arg(labelPrefix).arg(n)});
        ++added;
        ++n;
    }
    if (added == 0)
        return 0;

    std::sort(marks.begin(), marks.end(),
              [](const drift::Bookmark &a, const drift::Bookmark &b) { return a.timeUs < b.timeUs; });
    m_app->m_project.bookmarks() = marks;
    m_app->pushProjectEdit(before, QStringLiteral("Bookmark scenes"));
    m_app->finishEdit(QStringLiteral("Bookmark scenes"));
    return added;
}

QJsonObject McpController::aiCapabilities() const
{
    using namespace drift::mcp;

    struct Capability
    {
        const char *kind;
        bool installed;
        const char *unlocks;
    };

    const Capability capabilities[] = {
        {"whisper-model", drift::WhisperTranscriber::modelPresent(),
         "generate_subtitles — speech to timed captions"},
        {"sam2-model", drift::Sam2Segmenter::modelPresent(),
         "subject cutout and mask generation — click to pick any subject"},
        {"rvm-model", drift::RvmMatter::modelPresent(),
         "people cutout — no prompt, soft alpha, decontaminated foreground"},
        {"face-model", drift::FaceLandmarker::modelPresent(),
         "face tracking and the face warp effects"},
        {"denoise-model", drift::DeepFilterDenoiser::modelPresent(),
         "background noise removal from audio"},
        {"object-model", m_app->objectDetectionAvailable(),
         "detect_scenes({with_objects:true}) — labels each shot with what is in it"},
        {"vad-model", drift::SileroVad::modelPresent(),
         "detect_silence / remove_silence method:\"vad\" — speech detection that ignores music and noise"},
        {"align-model", !drift::CtcAligner::installedLanguages().isEmpty(),
         "transcribe — measured word timings (per language) for cut_words and word-exact captions"},
        {"diarize-model", drift::SpeakerDiarizer::modelPresent(),
         "diarize / transcribe({diarize:true}) — who is speaking when"},
        {"depth-model", drift::VdaDepth::modelPresent(),
         "depth estimation for the depth effects — 3D relighting, depth of field, fog, occlusion"},
        {"audio-tag-model", drift::ClapTagger::modelPresent(),
         "audio_tag / audio_find / audio_rank — name the sounds and sections of audio, find or rank media by a sound"},
        {"audio-class-model", drift::AstClassifier::modelPresent(),
         "audio_presence — where speech and music are; also speech / music in audio_tag and audio_sheet"},
    };

    QJsonArray models;
    for (const Capability &capability : capabilities) {
        QJsonObject row{{QStringLiteral("kind"), QString::fromUtf8(capability.kind)},
                        {QStringLiteral("installed"), capability.installed},
                        {QStringLiteral("unlocks"), QString::fromUtf8(capability.unlocks)}};
        if (qstrcmp(capability.kind, "align-model") == 0)
            row.insert(QStringLiteral("languages"), QJsonArray::fromStringList(drift::CtcAligner::installedLanguages()));
        models.append(row);
    }

    // A model is useless without a runtime to execute it, so report that too rather than
    // letting an agent conclude a feature is available when nothing can run it.
    const QString variant = drift::ort::activeVariant();
    return ok({{QStringLiteral("models"), models},
               {QStringLiteral("cloud"), m_app->m_cloud->statusJson()},
               {QStringLiteral("runtime"), variant.isEmpty() ? QStringLiteral("none") : variant},
               {QStringLiteral("hint"),
                QStringLiteral("Missing pieces install with list_addons / install_addon, or from "
                               "Extras in the app.")}});
}

QJsonObject McpController::setClipVolume(int trackIndex, int clipIndex, double value,
                                            bool atGiven, double atSeconds)
{
    using namespace drift::mcp;
    if (trackIndex < 0 || trackIndex >= m_app->m_project.tracks().size())
        return err("not_found", QStringLiteral("No such track"));
    if (clipIndex < 0 || clipIndex >= m_app->m_project.tracks().at(trackIndex).clips.size())
        return err("not_found", QStringLiteral("No such clip"));

    const double clamped = qBound(0.0, value, 2.0);

    // With a time, this is unambiguously a keyframe write and setClipKeyframe already forces
    // one there.
    if (atGiven) {
        m_app->setClipKeyframe(trackIndex, clipIndex, QStringLiteral("volume"), atSeconds, clamped);
    } else {
        // Without one it is an ordinary level change, which is the slider's contract, not the
        // diamond's: no key on an un-animated clip, retarget the only key on a clip that has
        // one, and on a genuinely animated clip retarget whichever key is at the playhead.
        const drift::Project before = m_app->m_project;
        drift::Clip &clip = m_app->m_project.tracks()[trackIndex].clips[clipIndex];
        const drift::TimeUs relative = qMax<drift::TimeUs>(0, m_app->m_playheadUs - clip.timelineStart);
        if (!m_app->writeClipProp(clip, QStringLiteral("volume"), relative, clamped,
                                /*autoKey=*/false, /*force=*/false)) {
            return err("bad_args",
                       QStringLiteral("Clip volume is animated and no key sits at the playhead — "
                                      "pass `at` to write one, or seek to an existing key"));
        }
        m_app->pushProjectEdit(before, QStringLiteral("Set volume"));
        m_app->finishEdit(QStringLiteral("Set volume"));
    }

    const drift::Clip &after = m_app->m_project.tracks().at(trackIndex).clips.at(clipIndex);
    QJsonObject reply{
        {QStringLiteral("id"), after.id},
        {QStringLiteral("value"), round3(clamped)},
        {QStringLiteral("volumeKeys"), after.volume.keyframes().size()},
    };
    if (!qFuzzyCompare(clamped + 1.0, value + 1.0))
        reply.insert(QStringLiteral("clamped"), true);
    return ok(reply);
}

QJsonObject McpController::audioSummary() const
{
    using namespace drift::mcp;
    QJsonArray trackRows;
    int audioClips = 0;

    const QList<drift::Track> &tracks = m_app->m_project.tracks();
    for (int t = 0; t < tracks.size(); ++t) {
        const drift::Track &track = tracks.at(t);
        // Same rule the mixer applies: audio clips always, video clips unless their embedded
        // audio is suppressed. Anything else on the timeline is silent by construction.
        if (track.type != drift::TrackType::Audio && track.type != drift::TrackType::Video)
            continue;

        QJsonArray clipRows;
        for (int c = 0; c < track.clips.size(); ++c) {
            const drift::Clip &clip = track.clips.at(c);
            const bool carries = clip.type == drift::ClipType::Audio
                                 || (clip.type == drift::ClipType::Video
                                     && !clip.suppressEmbeddedAudio);
            if (!carries)
                continue;

            if (m_app->m_assetLibrary)
                m_app->m_assetLibrary->ensureAudioPresence(clip.assetId);
            const drift::MediaAsset *asset = m_app->m_project.asset(clip.assetId);

            QJsonObject row{
                {QStringLiteral("clip"), clip.id},
                {QStringLiteral("start"), round3(drift::usToSeconds(clip.timelineStart))},
                {QStringLiteral("dur"), round3(drift::usToSeconds(clip.timelineDuration))},
                {QStringLiteral("type"), drift::clipTypeToString(clip.type)},
                {QStringLiteral("volumeKeys"), clip.volume.keyframes().size()},
                {QStringLiteral("audioEffects"), clip.audioEffects.size()},
            };
            if (clip.fadeInUs > 0)
                row.insert(QStringLiteral("fadeIn"), round3(drift::usToSeconds(clip.fadeInUs)));
            if (clip.fadeOutUs > 0)
                row.insert(QStringLiteral("fadeOut"), round3(drift::usToSeconds(clip.fadeOutUs)));
            if (asset) {
                row.insert(QStringLiteral("hasAudio"), asset->hasAudio);
                if (asset->sampleRate > 0)
                    row.insert(QStringLiteral("sampleRate"), asset->sampleRate);
                if (asset->channels > 0)
                    row.insert(QStringLiteral("channels"), asset->channels);
            }
            clipRows.append(row);
            ++audioClips;
        }

        if (clipRows.isEmpty())
            continue;
        trackRows.append(QJsonObject{
            {QStringLiteral("i"), t},
            {QStringLiteral("type"), drift::trackTypeToString(track.type)},
            {QStringLiteral("muted"), track.muted},
            {QStringLiteral("items"), clipRows},
        });
    }

    QJsonObject beats{
        {QStringLiteral("analysed"), !m_app->m_beatAnalysis.isEmpty()},
        {QStringLiteral("gridVisible"), m_app->m_beatGridVisible},
        {QStringLiteral("onsetsVisible"), m_app->m_onsetsVisible},
    };
    if (!m_app->m_beatAnalysis.isEmpty()) {
        beats.insert(QStringLiteral("bpm"), round2(m_app->m_beatAnalysisRaw.bpm));
        beats.insert(QStringLiteral("stale"), m_app->m_beatAudioFingerprint != m_app->audioLayoutFingerprint());
    }

    return ok({
        {QStringLiteral("sampleRate"), m_app->m_project.sampleRate()},
        {QStringLiteral("dur"), round3(drift::usToSeconds(m_app->m_project.durationUs()))},
        {QStringLiteral("audioClips"), audioClips},
        {QStringLiteral("tracks"), trackRows},
        {QStringLiteral("beats"), beats},
    });
}

namespace {

struct SilenceRange
{
    double start = 0.0;
    double end = 0.0;
};

QVector<float> blockingSpeechPeaks(const drift::Project &snap, double startSeconds, double durSeconds,
                                   int buckets)
{
    const int rate = 8000;
    const qint64 frames = static_cast<qint64>(durSeconds * rate);
    if (frames <= 0 || buckets <= 0)
        return {};
    const drift::TimeUs startUs = drift::secondsToUs(startSeconds);
    auto raw = std::make_shared<QVector<float>>();
    QEventLoop loop;
    (void)QtConcurrent::run([snap, startUs, frames, rate, buckets, raw, &loop]() {
        AudioMixer mixer;
        mixer.setProject(&snap);
        *raw = MediaWaveform::speechPeaks(
            frames, rate, buckets,
            [&mixer, startUs, rate](float *out, qint64 frameOffset, int maxFrames) {
                const drift::TimeUs at = startUs + frameOffset * drift::kUsPerSecond / rate;
                mixer.mix(at, maxFrames, rate, out);
                return maxFrames;
            });
        QMetaObject::invokeMethod(&loop, &QEventLoop::quit, Qt::QueuedConnection);
    });
    loop.exec();
    return *raw;
}

// The mix of `snap` over the range as 16 kHz mono, for the speech models.
std::vector<float> blockingMixMono16k(const drift::Project &snap, double startSeconds, double durSeconds)
{
    const int rate = drift::kSpeechSampleRate;
    const qint64 frames = static_cast<qint64>(durSeconds * rate);
    if (frames <= 0)
        return {};
    const drift::TimeUs startUs = drift::secondsToUs(startSeconds);
    auto mono = std::make_shared<std::vector<float>>();
    QEventLoop loop;
    (void)QtConcurrent::run([snap, startUs, frames, rate, mono, &loop]() {
        AudioMixer mixer;
        mixer.setProject(&snap);
        mono->resize(static_cast<size_t>(frames));
        constexpr int kBlock = 4096;
        std::vector<float> stereo(kBlock * 2);
        for (qint64 off = 0; off < frames; off += kBlock) {
            const int n = static_cast<int>(std::min<qint64>(kBlock, frames - off));
            std::fill(stereo.begin(), stereo.begin() + n * 2, 0.0f);
            mixer.mix(startUs + off * drift::kUsPerSecond / rate, n, rate, stereo.data());
            for (int i = 0; i < n; ++i)
                (*mono)[static_cast<size_t>(off + i)] = 0.5f * (stereo[i * 2] + stereo[i * 2 + 1]);
        }
        QMetaObject::invokeMethod(&loop, &QEventLoop::quit, Qt::QueuedConnection);
    });
    loop.exec();
    return *mono;
}

drift::LoudnessResult blockingLoudness(McpController::LoudnessJob job)
{
    auto result = std::make_shared<drift::LoudnessResult>();
    QEventLoop loop;
    (void)QtConcurrent::run([job = std::move(job), result, &loop]() {
        *result = McpController::measureLoudnessJob(job);
        QMetaObject::invokeMethod(&loop, &QEventLoop::quit, Qt::QueuedConnection);
    });
    loop.exec();
    return *result;
}

void muteAllButClip(drift::Project &snap, const QString &clipId)
{
    for (drift::Track &track : snap.tracks()) {
        bool has = false;
        for (drift::Clip &clip : track.clips) {
            if (clip.id == clipId) {
                has = true;
                continue;
            }
            clip.volume.setKeyframe(0, 0.0);
            clip.suppressEmbeddedAudio = true;
        }
        if (!has)
            track.muted = true;
    }
}

// The clip whose sound a clip-scoped analysis should hear. A video whose audio was separated
// plays nothing itself (its embedded audio is suppressed), so muting everything else left a clip
// that read as wall-to-wall silence — and remove_silence then deleted it outright.
QString audibleClipIdFor(const drift::Project &project, const drift::Clip &clip)
{
    if (!clip.suppressEmbeddedAudio)
        return clip.id;
    for (const drift::ClipRef &ref : drift::linkedPartners(project, clip)) {
        const drift::Clip &partner = project.tracks().at(ref.trackIndex).clips.at(ref.clipIndex);
        if (project.tracks().at(ref.trackIndex).type == drift::TrackType::Audio)
            return partner.id;
    }
    return clip.id;
}

QList<SilenceRange> rangesFromPeaks(const QVector<float> &peaks, double startSeconds,
                                    double durSeconds, double threshold, double minDuration,
                                    double padding)
{
    QList<SilenceRange> ranges;
    if (peaks.isEmpty() || durSeconds <= 0.0)
        return ranges;
    const double bucket = durSeconds / double(peaks.size());
    int run = -1;
    for (int i = 0; i <= peaks.size(); ++i) {
        const bool silent = i < peaks.size() && peaks.at(i) < threshold;
        if (silent && run < 0)
            run = i;
        if (!silent && run >= 0) {
            SilenceRange r;
            r.start = startSeconds + run * bucket + padding;
            r.end = startSeconds + i * bucket - padding;
            if (r.end - r.start >= minDuration)
                ranges.append(r);
            run = -1;
        }
    }
    return ranges;
}

} // namespace


namespace {

constexpr double kMcpWaveformImageMaxSeconds = 600.0;
constexpr int kMcpWaveformImageMaxWidth = 2000;
constexpr int kMcpSpectrogramBins = 64;


QByteArray encodePng(const QImage &image)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}


} // namespace


QJsonObject McpController::waveformImage(const QString &mode, int trackIndex, int clipIndex,
                                            const QString &assetId, double startSeconds,
                                            double durSeconds, int width, int height,
                                            bool spectrogram, int summaryBuckets, bool words) const
{
    using namespace drift::mcp;
    using namespace drift::waveformsheet;

    width = qBound(200, width, kMcpWaveformImageMaxWidth);
    height = qBound(120, height, 800);
    summaryBuckets = qBound(1, summaryBuckets, kMcpMaxBuckets);

    Input in;
    Options opt;
    opt.width = width;
    opt.height = height;
    QStringList lanes{QStringLiteral("mixed")};
    QString source = mode;
    double start = qMax(0.0, startSeconds);
    double dur = durSeconds;

    if (mode == QLatin1String("asset")) {
        const drift::MediaAsset *asset = m_app->m_project.asset(assetId);
        if (!asset)
            return err("not_found", QStringLiteral("Unknown asset"));
        if (asset->path.isEmpty())
            return err("type_mismatch", QStringLiteral("Asset has no file"));
        const double total = drift::usToSeconds(asset->durationUs);
        dur = durSeconds > 0.0 ? qMin(durSeconds, total - start) : total - start;
        if (dur <= 0.0)
            return err("bad_args", QStringLiteral("Range is past the end of the asset"));
        if (dur > kMcpWaveformImageMaxSeconds)
            return err("bad_args", QStringLiteral("duration must be <= %1 seconds for image mode").arg(kMcpWaveformImageMaxSeconds));
        in.mixed = blockingSourcePeaks(asset->path, start, dur, width);
        if (in.mixed.isEmpty())
            return err("not_found", QStringLiteral("No audio decoded for that range"));
    } else {
        drift::Project snap = m_app->m_project;
        if (mode == QLatin1String("clip")) {
            const auto &tracks = m_app->m_project.tracks();
            if (trackIndex < 0 || trackIndex >= tracks.size() || clipIndex < 0
                || clipIndex >= tracks.at(trackIndex).clips.size())
                return err("not_found", QStringLiteral("no clip at track %1 index %2").arg(trackIndex).arg(clipIndex));
            const drift::Clip &clip = tracks.at(trackIndex).clips.at(clipIndex);
            muteAllButClip(snap, clip.id);
            start = drift::usToSeconds(clip.timelineStart);
            dur = drift::usToSeconds(clip.timelineDuration);
        }
        if (dur <= 0.0)
            return err("bad_args", QStringLiteral("duration must be > 0"));
        if (dur > kMcpWaveformImageMaxSeconds)
            return err("bad_args", QStringLiteral("duration must be <= %1 seconds for image mode").arg(kMcpWaveformImageMaxSeconds));

        const int rate = spectrogram ? 16000 : 8000;
        const qint64 frames = static_cast<qint64>(dur * rate);
        if (frames <= 0)
            return err("bad_args", QStringLiteral("duration is too short to measure"));
        const drift::TimeUs startUs = drift::secondsToUs(start);

        struct Mixed
        {
            QVector<float> mixed;
            QVector<float> speech;
            QVector<QVector<float>> spectrogram;
        };
        auto out = std::make_shared<Mixed>();
        QEventLoop loop;
        (void)QtConcurrent::run([snap, startUs, frames, rate, width, spectrogram, out, &loop]() {
            AudioMixer mixer;
            mixer.setProject(&snap);
            QVector<float> pcm(static_cast<qsizetype>(frames) * 2);
            constexpr int kChunk = 4096;
            for (qint64 done = 0; done < frames; done += kChunk) {
                const int want = static_cast<int>(qMin<qint64>(kChunk, frames - done));
                const drift::TimeUs at = startUs + done * drift::kUsPerSecond / rate;
                mixer.mix(at, want, rate, pcm.data() + done * 2);
            }
            const MediaWaveform::FillChunk fill = [&pcm, frames](float *dst, qint64 offset, int maxFrames) {
                const int got = static_cast<int>(qMin<qint64>(maxFrames, frames - offset));
                if (got <= 0)
                    return 0;
                std::copy_n(pcm.constData() + offset * 2, static_cast<qsizetype>(got) * 2, dst);
                return got;
            };
            out->mixed = MediaWaveform::mixedPeaks(frames, rate, width, fill);
            out->speech = MediaWaveform::speechPeaks(frames, rate, width, fill);
            if (spectrogram) {
                QVector<float> mono(static_cast<qsizetype>(frames));
                for (qint64 i = 0; i < frames; ++i)
                    mono[i] = 0.5f * (pcm[i * 2] + pcm[i * 2 + 1]);
                out->spectrogram = drift::waveformsheet::spectrogram(mono.constData(), frames, rate,
                                                                     kMcpSpectrogramBins, width);
            }
            QMetaObject::invokeMethod(&loop, &QEventLoop::quit, Qt::QueuedConnection);
        });
        loop.exec();

        in.mixed = out->mixed;
        in.speech = out->speech;
        in.spectrogram = out->spectrogram;
        lanes.append(QStringLiteral("speech"));
        if (spectrogram)
            lanes.append(QStringLiteral("spectrogram"));
        for (const SilenceRange &r : rangesFromPeaks(out->speech, start, dur, 0.02, 0.35, 0.0))
            in.silence.append({r.start, r.end});

        if (!m_app->m_beatAnalysis.isEmpty() && m_app->audioLayoutFingerprint() == m_app->m_beatAudioFingerprint) {
            for (const AudioOnset &o : m_app->m_beatAnalysisRaw.onsets)
                if (o.seconds >= start && o.seconds <= start + dur)
                    in.onsets.append(o.seconds);
            for (double b : m_app->m_beatAnalysisRaw.beats)
                if (b >= start && b <= start + dur)
                    in.beats.append(b);
        }
    }

    in.startSeconds = start;
    in.durationSeconds = dur;
    // The transcript's words under the waveform: asset mode reads them in source time, clip and
    // timeline modes as the timeline plays them.
    if (words) {
        const drift::TimeUs a = drift::secondsToUs(start);
        const drift::TimeUs b = drift::secondsToUs(start + dur);
        if (mode == QLatin1String("asset")) {
            if (const drift::TranscriptPtr t = m_app->m_project.transcript(assetId)) {
                for (const int i : t->wordsInRange(a, b)) {
                    const drift::TranscriptWord &w = t->words.at(i);
                    if (drift::isSpeechToken(w))
                        in.words.append({drift::usToSeconds(w.startUs), drift::usToSeconds(w.endUs), w.text, i});
                }
            }
        } else {
            const QString only = mode == QLatin1String("clip")
                                     ? m_app->m_project.tracks().at(trackIndex).clips.at(clipIndex).id
                                     : QString();
            for (const drift::TimelineWord &tw : drift::transcriptWordsOnTimeline(m_app->m_project, a, b, only)) {
                if (drift::isSpeechToken(tw.word))
                    in.words.append({drift::usToSeconds(tw.word.startUs), drift::usToSeconds(tw.word.endUs),
                                     tw.word.text, tw.index});
            }
        }
        if (!in.words.isEmpty())
            lanes.append(QStringLiteral("words"));
    }
    const QImage image = render(in, opt);
    if (image.isNull())
        return err("capture_failed", QStringLiteral("Could not render waveform"));

    QJsonObject meta = peaksReply(reduceRawPeaks(in.mixed, summaryBuckets), start, dur, source);
    if (!meta.value(QStringLiteral("ok")).toBool())
        return meta;
    QJsonArray laneArr;
    for (const QString &l : lanes)
        laneArr.append(l);
    meta.insert(QStringLiteral("image"),
                QJsonObject{{QStringLiteral("w"), image.width()},
                            {QStringLiteral("h"), image.height()},
                            {QStringLiteral("lanes"), laneArr},
                            {QStringLiteral("axis_step"), axisStepSeconds(dur, width)}});
    QJsonArray silence;
    for (const auto &r : in.silence)
        silence.append(QJsonObject{{QStringLiteral("start"), round3(r.first)},
                                   {QStringLiteral("end"), round3(r.second)}});
    meta.insert(QStringLiteral("silence"), silence);
    if (!in.words.isEmpty()) {
        constexpr int kMaxWords = 400;
        QJsonArray wordArr;
        for (const WordLabel &w : std::as_const(in.words)) {
            if (wordArr.size() >= kMaxWords)
                break;
            wordArr.append(QJsonObject{{QStringLiteral("i"), w.index},
                                       {QStringLiteral("text"), w.text},
                                       {QStringLiteral("start"), round3(w.start)},
                                       {QStringLiteral("end"), round3(w.end)}});
        }
        meta.insert(QStringLiteral("words"), wordArr);
        if (in.words.size() > kMaxWords)
            meta.insert(QStringLiteral("words_truncated"), true);
    }
    meta.insert(QStringLiteral("onsets"), in.onsets.size());
    meta.insert(QStringLiteral("beats"), in.beats.size());
    if (spectrogram && !in.spectrogram.isEmpty()) {
        meta.insert(QStringLiteral("spectrogram"),
                    QJsonObject{{QStringLiteral("bins"), kMcpSpectrogramBins},
                                {QStringLiteral("min_hz"), 50},
                                {QStringLiteral("max_hz"), 8000}});
    }
    return imageResult(meta, encodePng(image), QStringLiteral("image/png"));
}

QJsonObject McpController::detectSilence(int trackIndex, int clipIndex, double startSeconds,
                                            double durSeconds, double threshold, double minDuration,
                                            double padding, const QString &method) const
{
    using namespace drift::mcp;
    drift::Project snap = m_app->m_project.detachedCopy();
    QString source = QStringLiteral("timeline");
    double start = startSeconds;
    double dur = durSeconds;

    if (trackIndex >= 0 && clipIndex >= 0) {
        if (!m_app->isValidClipIndex(trackIndex, clipIndex))
            return err("not_found", QStringLiteral("Unknown clip"));
        const drift::Clip &clip = m_app->m_project.tracks().at(trackIndex).clips.at(clipIndex);
        muteAllButClip(snap, audibleClipIdFor(m_app->m_project, clip));
        start = drift::usToSeconds(clip.timelineStart);
        dur = drift::usToSeconds(clip.timelineDuration);
        source = QStringLiteral("clip");
    } else {
        if (dur <= 0.0)
            return err("bad_args", QStringLiteral("clip, or start+duration, required"));
        start = qMax(0.0, start);
    }
    if (dur <= 0.0)
        return err("bad_args", QStringLiteral("Nothing to analyse"));

    QList<SilenceRange> ranges;
    if (method == QLatin1String("vad")) {
        if (!drift::SileroVad::modelPresent())
            return err("addon_missing", QStringLiteral("method vad needs the voice activity model: "
                                                       "install_addon({id:\"silero.vad\"})"));
        drift::SileroVad &vad = drift::SileroVad::instance();
        if (!vad.available())
            return err("addon_missing", vad.lastError());
        const std::vector<float> mono = blockingMixMono16k(snap, start, dur);
        // Off the GUI thread like the mix: an hour of audio is ~100k model runs.
        std::vector<float> probs;
        {
            QEventLoop loop;
            (void)QtConcurrent::run([&vad, &mono, &probs, &loop]() {
                probs = vad.probabilities(mono);
                QMetaObject::invokeMethod(&loop, &QEventLoop::quit, Qt::QueuedConnection);
            });
            loop.exec();
        }
        if (probs.empty() && !mono.empty())
            return err("internal", vad.lastError());
        drift::VadParams params;
        params.threshold = static_cast<float>(qBound(0.05, threshold, 0.95));
        params.speechPadMs = qRound(qMax(0.0, padding) * 1000.0);
        const QList<drift::VadRange> speech = drift::vadSpeechRanges(probs, mono.size(), params);
        double cursor = start;
        auto addGap = [&](double a, double b) {
            if (b - a >= minDuration)
                ranges.append({a, b});
        };
        for (const drift::VadRange &r : speech) {
            addGap(cursor, start + drift::usToSeconds(r.startUs));
            cursor = start + drift::usToSeconds(r.endUs);
        }
        addGap(cursor, start + dur);
    } else if (method == QLatin1String("energy")) {
        const int buckets = qBound(8, int(qCeil(dur * 50.0)), 4096);
        const QVector<float> peaks = blockingSpeechPeaks(snap, start, dur, buckets);
        ranges = rangesFromPeaks(peaks, start, dur, threshold, minDuration, padding);
    } else {
        return err("bad_args", QStringLiteral("method must be energy or vad"));
    }
    QJsonArray out;
    for (const SilenceRange &r : ranges) {
        out.append(QJsonObject{{QStringLiteral("start"), r.start}, {QStringLiteral("end"), r.end}});
    }
    return ok({{QStringLiteral("ranges"), out},
               {QStringLiteral("threshold"), threshold},
               {QStringLiteral("source"), source},
               {QStringLiteral("method"), method},
               {QStringLiteral("n"), out.size()}});
}

QJsonObject McpController::removeSilence(int trackIndex, int clipIndex, double threshold,
                                            double minDuration, double padding, double declick,
                                            const QString &method)
{
    using namespace drift::mcp;
    QStringList targets;
    if (clipIndex >= 0 && trackIndex >= 0) {
        if (!m_app->isValidClipIndex(trackIndex, clipIndex))
            return err("not_found", QStringLiteral("Unknown clip"));
        targets.append(m_app->m_project.tracks().at(trackIndex).clips.at(clipIndex).id);
    } else if (trackIndex >= 0 && trackIndex < m_app->m_project.tracks().size()) {
        for (const drift::Clip &clip : m_app->m_project.tracks().at(trackIndex).clips)
            targets.append(clip.id);
    } else {
        return err("bad_args", QStringLiteral("clip or track required"));
    }

    const drift::TimeUs declickUs = drift::secondsToUs(qBound(0.0, declick, 0.5));
    const drift::TimeUs minKeepUs = qMax<drift::TimeUs>(drift::kCutMinEdgeUs, 2 * declickUs);
    const drift::Project before = m_app->m_project;
    QJsonArray removed;
    QSet<QString> handledLinks;
    bool changed = false;

    // Back to front: ripple only moves what lies after an edit, so earlier targets keep their place.
    for (int i = targets.size() - 1; i >= 0; --i) {
        int tr = -1, cl = -1;
        if (!findClipById(m_app->m_project, targets.at(i), &tr, &cl))
            continue;
        const drift::Clip clip = m_app->m_project.tracks().at(tr).clips.at(cl);
        if (clip.path.isEmpty() && clip.sequenceId.isEmpty())
            continue;
        if (!clip.linkId.isEmpty()) {
            if (handledLinks.contains(clip.linkId))
                continue;
            handledLinks.insert(clip.linkId);
        }
        const QJsonObject detected =
            detectSilence(tr, cl, 0, 0, threshold, minDuration, padding, method);
        if (!detected.value(QStringLiteral("ok")).toBool())
            return detected;
        QList<drift::TimeRangeUs> silences;
        for (const QJsonValue &v : detected.value(QStringLiteral("ranges")).toArray()) {
            const QJsonObject r = v.toObject();
            silences.append({drift::secondsToUs(r.value(QStringLiteral("start")).toDouble()),
                             drift::secondsToUs(r.value(QStringLiteral("end")).toDouble())});
        }
        const QList<drift::TimeRangeUs> kept = drift::keptTimelineIntervals(clip, silences, minKeepUs);
        if (kept.size() == 1 && kept.first().startUs == clip.timelineStart
            && kept.first().endUs == clip.timelineEnd())
            continue;

        // Report what actually goes, measured against the clip before this edit.
        drift::TimeUs cursor = clip.timelineStart;
        QJsonArray clipRemoved;
        for (const drift::TimeRangeUs &k : kept) {
            if (k.startUs > cursor)
                clipRemoved.append(QJsonObject{{QStringLiteral("start"), drift::usToSeconds(cursor)},
                                               {QStringLiteral("end"), drift::usToSeconds(k.startUs)}});
            cursor = k.endUs;
        }
        if (cursor < clip.timelineEnd())
            clipRemoved.append(QJsonObject{{QStringLiteral("start"), drift::usToSeconds(cursor)},
                                           {QStringLiteral("end"), drift::usToSeconds(clip.timelineEnd())}});

        QString error;
        if (!m_app->replaceClipGroupWithSegments(
                tr, cl,
                [&kept, declickUs](const drift::Clip &member) {
                    return drift::packedSegments(member, kept, declickUs);
                },
                true, nullptr, &error))
            continue;
        changed = true;
        for (int r = clipRemoved.size() - 1; r >= 0; --r)
            removed.prepend(clipRemoved.at(r));
    }

    if (changed) {
        m_app->pushProjectEdit(before, AppController::tr("Remove silence"));
        m_app->finishEdit(AppController::tr("Remove silence"));
    }

    QJsonArray surviving;
    for (const drift::Track &track : m_app->m_project.tracks()) {
        for (const drift::Clip &clip : track.clips)
            surviving.append(clip.id);
    }
    return ok({{QStringLiteral("removed"), removed},
               {QStringLiteral("clips"), surviving},
               {QStringLiteral("n"), removed.size()}});
}

McpController::LoudnessJob McpController::clipLoudnessJob(int trackIndex, int clipIndex) const
{
    LoudnessJob job;
    job.snap = m_app->m_project.detachedCopy();
    const drift::Clip &clip = m_app->m_project.tracks().at(trackIndex).clips.at(clipIndex);
    muteAllButClip(job.snap, clip.id);
    job.startSeconds = drift::usToSeconds(clip.timelineStart);
    job.durSeconds = drift::usToSeconds(clip.timelineDuration);
    return job;
}

drift::LoudnessResult McpController::measureLoudnessJob(const LoudnessJob &job,
                                                        const std::function<bool(double)> &keepGoing)
{
    const int rate = 48000;
    const qint64 frames = static_cast<qint64>(job.durSeconds * rate);
    if (frames <= 0)
        return {};
    const drift::TimeUs startUs = drift::secondsToUs(job.startSeconds);
    AudioMixer mixer;
    mixer.setProject(&job.snap);
    // Measure the mix as it is, not as the master clipper leaves it: softClip saturates to
    // exactly 1.0f, so a peak read after it is 0.0 dBFS however hot the mix really is.
    mixer.setMasterClipEnabled(false);
    return drift::measureLoudness(
        frames, rate, [&](float *out, qint64 frameOffset, int maxFrames) {
            if (keepGoing && !keepGoing(double(frameOffset) / double(frames)))
                return 0;
            const drift::TimeUs at = startUs + frameOffset * drift::kUsPerSecond / rate;
            mixer.mix(at, maxFrames, rate, out);
            return maxFrames;
        });
}

QJsonObject McpController::analyzeLoudness(int trackIndex, int clipIndex, double startSeconds,
                                              double durSeconds) const
{
    using namespace drift::mcp;
    LoudnessJob job;
    if (trackIndex >= 0 && clipIndex >= 0) {
        if (!m_app->isValidClipIndex(trackIndex, clipIndex))
            return err("not_found", QStringLiteral("Unknown clip"));
        job = clipLoudnessJob(trackIndex, clipIndex);
    } else if (durSeconds <= 0.0) {
        return err("bad_args", QStringLiteral("clip, or start+duration, required"));
    } else {
        job.snap = m_app->m_project.detachedCopy();
        job.startSeconds = startSeconds;
        job.durSeconds = durSeconds;
    }
    if (job.durSeconds <= 0.0)
        return err("bad_args", QStringLiteral("Nothing to measure"));
    const drift::LoudnessResult measured = blockingLoudness(std::move(job));
    if (!measured.ok)
        return err("bad_args", QStringLiteral("No audio in range"));
    return ok({{QStringLiteral("lufs"), measured.integratedLufs},
               {QStringLiteral("true_peak_db"), measured.truePeakDb},
               {QStringLiteral("duration"), measured.durationSeconds}});
}

namespace {

constexpr int kMcpAudioRate = 48000;
constexpr double kMcpMaxMeasureSeconds = 3600.0;
constexpr double kMcpMaxAnalysisSeconds = 600.0;
constexpr int kMcpMaxAudioEvents = 500;
constexpr int kMcpMaxSyncCues = 400;
constexpr int kMcpMaxCurvePoints = 600;
constexpr int kMcpMaxGapRows = 50;
constexpr int kMcpMaxClipRows = 20;
constexpr int kMcpMaxPresenceRows = 300;
constexpr int kMcpMaxRankedAssets = 100;
constexpr double kMcpSilentDbfs = -60.0;
constexpr int kMcpSheetMaxFrames = 30;
constexpr int kMcpSheetLongEdge = 1600;
constexpr int kMcpSheetJpegQuality = 88;
constexpr int kMcpSheetFrameHeight = 280;
constexpr double kMcpSheetFrameLag = 0.15;

struct MixedAudio
{
    QVector<float> mono;
    drift::LoudnessReport loudness;
};

// Mixes the project at 48 kHz in windows off the GUI thread, feeding the loudness pass as it
// pulls and keeping the mono fold-down when the event detector wants it. Master clipping is off:
// a hot mix has to read as hot, not as the 1.0 the soft clipper pins it to.
MixedAudio mixAnalysis(const drift::Project &snap, double startSeconds, double durSeconds,
                       bool wantMono, bool wantLoudness)
{
    const qint64 frames = static_cast<qint64>(durSeconds * kMcpAudioRate);
    if (frames <= 0)
        return {};
    MixedAudio out;
    if (wantMono)
        out.mono.resize(static_cast<qsizetype>(frames));
    const drift::TimeUs startUs = drift::secondsToUs(startSeconds);
    AudioMixer mixer;
    mixer.setProject(&snap);
    mixer.setMasterClipEnabled(false);
    const auto fill = [&](float *dst, qint64 offset, int maxFrames) {
        const int n = static_cast<int>(qMin<qint64>(maxFrames, frames - offset));
        if (n <= 0)
            return 0;
        std::fill(dst, dst + static_cast<qsizetype>(n) * 2, 0.0f);
        mixer.mix(startUs + offset * drift::kUsPerSecond / kMcpAudioRate, n, kMcpAudioRate, dst);
        if (wantMono) {
            for (int i = 0; i < n; ++i)
                out.mono[static_cast<qsizetype>(offset + i)] = 0.5f * (dst[i * 2] + dst[i * 2 + 1]);
        }
        return n;
    };
    if (wantLoudness) {
        out.loudness = drift::analyzeLoudness(frames, kMcpAudioRate, fill);
    } else {
        constexpr int kChunk = 1 << 16;
        QVector<float> buffer(kChunk * 2);
        for (qint64 done = 0; done < frames; done += kChunk)
            fill(buffer.data(), done, kChunk);
    }
    return out;
}

MixedAudio blockingMixAnalysis(const drift::Project &snap, double startSeconds, double durSeconds,
                               bool wantMono, bool wantLoudness)
{
    auto out = std::make_shared<MixedAudio>();
    QEventLoop loop;
    (void)QtConcurrent::run([&snap, &loop, out, startSeconds, durSeconds, wantMono, wantLoudness]() {
        *out = mixAnalysis(snap, startSeconds, durSeconds, wantMono, wantLoudness);
        QMetaObject::invokeMethod(&loop, &QEventLoop::quit, Qt::QueuedConnection);
    });
    loop.exec();
    return *out;
}

drift::AudioEventsResult detectEvents(const QVector<float> &mono, double startSeconds, bool leadIn)
{
    drift::AudioEventsOptions options;
    options.leadIn = leadIn;
    return drift::AudioEvents::analyze(mono.constData(), mono.size(), kMcpAudioRate, startSeconds, options);
}

drift::AudioEventsResult blockingEvents(const QVector<float> &mono, double startSeconds, bool leadIn)
{
    auto out = std::make_shared<drift::AudioEventsResult>();
    QEventLoop loop;
    (void)QtConcurrent::run([&mono, &loop, out, startSeconds, leadIn]() {
        *out = detectEvents(mono, startSeconds, leadIn);
        QMetaObject::invokeMethod(&loop, &QEventLoop::quit, Qt::QueuedConnection);
    });
    loop.exec();
    return *out;
}

// Starts of the clips on video tracks, and their ends where nothing follows inside the
// timeline: the places the picture changes, strictly inside (start, end).
QList<double> videoCutTimes(const drift::Project &project, double start, double end)
{
    QList<double> raw;
    const double projectEnd = drift::usToSeconds(project.durationUs());
    for (const drift::Track &track : project.tracks()) {
        if (track.type != drift::TrackType::Video)
            continue;
        for (const drift::Clip &clip : track.clips) {
            raw.append(drift::usToSeconds(clip.timelineStart));
            const double e = drift::usToSeconds(clip.timelineEnd());
            if (e < projectEnd - 0.001)
                raw.append(e);
        }
    }
    std::sort(raw.begin(), raw.end());
    QList<double> cuts;
    for (double t : std::as_const(raw)) {
        if (t <= start + 0.001 || t >= end - 0.001)
            continue;
        if (!cuts.isEmpty() && t - cuts.last() < 0.001)
            continue;
        cuts.append(t);
    }
    return cuts;
}

QJsonObject curveJson(const drift::LoudnessCurve &curve, double offset)
{
    const int n = curve.lufs.size();
    const int stride = qMax(1, (n + kMcpMaxCurvePoints - 1) / kMcpMaxCurvePoints);
    QJsonArray values;
    for (int i = 0; i < n; i += stride)
        values.append(round2(curve.lufs.at(i)));
    return {{QStringLiteral("t0"), round3(offset + curve.t0)},
            {QStringLiteral("dt"), round3(curve.dt * stride)},
            {QStringLiteral("lufs"), values}};
}

QJsonObject loudnessJson(const drift::LoudnessReport &r, double offset, bool curves)
{
    QJsonObject o;
    if (r.hasIntegrated)
        o.insert(QStringLiteral("lufs"), round2(r.integratedLufs));
    if (r.hasLra)
        o.insert(QStringLiteral("lra_lu"), round2(r.lraLu));
    if (r.hasTruePeak) {
        o.insert(QStringLiteral("true_peak_db"), round2(r.truePeakDb));
        o.insert(QStringLiteral("sample_peak_db"), round2(r.samplePeakDb));
    }
    if (!r.momentary.lufs.isEmpty()) {
        o.insert(QStringLiteral("max_momentary"),
                 QJsonObject{{QStringLiteral("at"), round3(offset + r.maxMomentaryAt)},
                             {QStringLiteral("lufs"), round2(r.maxMomentaryLufs)}});
    }
    if (!r.shortTerm.lufs.isEmpty())
        o.insert(QStringLiteral("max_short_term_lufs"), round2(r.maxShortTermLufs));
    QJsonArray runs;
    for (const drift::ClippingRun &c : r.clippingRuns) {
        if (runs.size() >= kMcpMaxClipRows)
            break;
        runs.append(QJsonObject{{QStringLiteral("at"), round3(offset + c.start)}, {QStringLiteral("samples"), c.samples}});
    }
    o.insert(QStringLiteral("clipping"),
             QJsonObject{{QStringLiteral("samples"), static_cast<double>(r.clippedSamples)},
                         {QStringLiteral("runs"), r.clippingRuns.size()},
                         {QStringLiteral("at"), runs}});
    QJsonArray gaps;
    for (const drift::LoudnessGap &g : r.gaps) {
        if (gaps.size() >= kMcpMaxGapRows)
            break;
        QJsonObject row{{QStringLiteral("start"), round3(offset + g.start)},
                        {QStringLiteral("end"), round3(offset + g.end)},
                        {QStringLiteral("level_lufs"), round2(g.levelLufs)},
                        {QStringLiteral("kind"), g.kind}};
        if (!g.where.isEmpty())
            row.insert(QStringLiteral("where"), g.where);
        gaps.append(row);
    }
    o.insert(QStringLiteral("gaps"), gaps);
    if (r.gaps.size() > gaps.size())
        o.insert(QStringLiteral("gaps_total"), r.gaps.size());
    if (curves) {
        o.insert(QStringLiteral("momentary"), curveJson(r.momentary, offset));
        o.insert(QStringLiteral("short_term"), curveJson(r.shortTerm, offset));
    }
    return o;
}

QJsonObject eventJson(const drift::AudioEvent &e)
{
    return {{QStringLiteral("at"), round3(e.seconds)},
            {QStringLiteral("kind"), e.kind == drift::AudioEvent::Kind::Swell ? QStringLiteral("swell") : QStringLiteral("hit")},
            {QStringLiteral("strength"), round2(e.strength)},
            {QStringLiteral("level_db"), round2(e.levelDbfs)},
            {QStringLiteral("prominence_db"), round2(e.prominenceDb)},
            {QStringLiteral("band"), e.band},
            {QStringLiteral("length_ms"), e.lengthMs}};
}

QJsonObject syncStatsJson(const drift::AudioSyncStats &st)
{
    QJsonObject o{{QStringLiteral("n"), st.n},
                  {QStringLiteral("tolerance_ms"), st.toleranceMs},
                  {QStringLiteral("hit_within_tolerance"), st.hitWithinTolerance},
                  {QStringLiteral("hit_within_100ms"), st.hitWithin100},
                  {QStringLiteral("answered_by_swell"), st.answeredBySwell},
                  {QStringLiteral("answered_by_level_peak"), st.answeredByLevelPeak}};
    if (st.hasMedianOffset)
        o.insert(QStringLiteral("median_abs_offset_ms"), st.medianAbsOffsetMs);
    if (st.hasChance) {
        o.insert(QStringLiteral("chance_within_tolerance"), st.chanceTolerance);
        o.insert(QStringLiteral("chance_within_100ms"), st.chance100);
    }
    if (st.hasAccents) {
        o.insert(QStringLiteral("accent_min_db"), st.accentMinDb);
        o.insert(QStringLiteral("accent_within_tolerance"), st.accentWithinTolerance);
        o.insert(QStringLiteral("accent_chance_within_tolerance"), st.accentChanceTolerance);
        o.insert(QStringLiteral("accent_within_100ms"), st.accentWithin100);
        o.insert(QStringLiteral("accent_chance_within_100ms"), st.accentChance100);
    }
    QJsonArray none;
    for (double t : st.noSound)
        none.append(round3(t));
    o.insert(QStringLiteral("no_sound"), none);
    return o;
}

QJsonObject syncItemJson(const drift::AudioSyncItem &it)
{
    QJsonObject o{{QStringLiteral("at"), round3(it.seconds)}, {QStringLiteral("status"), drift::syncStatusName(it.status)}};
    if (it.answered) {
        o.insert(QStringLiteral("offset_ms"), std::round(it.offsetMs * 10.0) / 10.0);
        o.insert(QStringLiteral("event_at"), round3(it.eventSeconds));
        o.insert(QStringLiteral("prominence_db"), round2(it.eventProminenceDb));
    }
    if (it.hasAccent)
        o.insert(QStringLiteral("accent_offset_ms"), std::round(it.accentOffsetMs * 10.0) / 10.0);
    return o;
}

template <typename F>
auto blockingRun(F f)
{
    using R = decltype(f());
    auto out = std::make_shared<R>();
    QEventLoop loop;
    (void)QtConcurrent::run([&loop, out, f = std::move(f)]() mutable {
        *out = f();
        QMetaObject::invokeMethod(&loop, &QEventLoop::quit, Qt::QueuedConnection);
    });
    loop.exec();
    return *out;
}

// An error object when the audio.clap / audio.ast model cannot be used, else {}. Missing and
// failed-to-load are both addon_missing, with the install call or the loader's reason.
QJsonObject clapUnavailable(const char *tool)
{
    using namespace drift::mcp;
    if (!drift::ClapTagger::modelPresent())
        return err("addon_missing", QStringLiteral("%1 needs the audio tagging model: install_addon({id:\"audio.clap\"})")
                                        .arg(QLatin1String(tool)));
    if (!blockingRun([] { return drift::ClapTagger::instance().available(); }))
        return err("addon_missing", drift::ClapTagger::instance().lastError());
    return {};
}

QJsonObject astUnavailable(const char *tool)
{
    using namespace drift::mcp;
    if (!drift::AstClassifier::modelPresent())
        return err("addon_missing", QStringLiteral("%1 needs the speech and music model: install_addon({id:\"audio.ast\"})")
                                        .arg(QLatin1String(tool)));
    if (!blockingRun([] { return drift::AstClassifier::instance().available(); }))
        return err("addon_missing", drift::AstClassifier::instance().lastError());
    return {};
}

QJsonObject unknownTerm(const QString &term)
{
    using namespace drift::mcp;
    const QStringList closest = drift::ClapTagger::instance().closestTerms(term, 8);
    QJsonObject o = err("unknown_term", QStringLiteral("\"%1\" is not in the vocabulary (322 sound, music and ambience terms; no free text). Closest: %2")
                                            .arg(term, closest.join(QStringLiteral("; "))));
    o.insert(QStringLiteral("closest"), QJsonArray::fromStringList(closest));
    return o;
}

// The models were calibrated on ffmpeg's mono downmix, 0.707 * (L + R); the mixer's analysis mono
// is the mean, 0.5 * (L + R).
QVector<float> modelGain(const QVector<float> &mono)
{
    QVector<float> out = mono;
    for (float &v : out)
        v *= static_cast<float>(M_SQRT2);
    return out;
}

QJsonObject tagJson(const drift::ClapTag &t)
{
    QJsonObject o{{QStringLiteral("tag"), t.text}, {QStringLiteral("z"), round2(t.z)}};
    if (!t.kind.isEmpty())
        o.insert(QStringLiteral("sound"), t.kind);
    return o;
}

QJsonObject taggedEventJson(const drift::AudioEvent &e, const drift::TaggedEvent *t)
{
    QJsonObject o = eventJson(e);
    if (!t)
        return o;
    o.insert(QStringLiteral("sound"), t->kind);
    o.insert(QStringLiteral("label"), t->label);
    QJsonArray tags;
    for (const drift::ClapTag &tag : t->tags)
        tags.append(tagJson(tag));
    o.insert(QStringLiteral("tags"), tags);
    o.insert(QStringLiteral("exposed"), t->exposed);
    if (t->hasAst) {
        QJsonArray ast;
        for (const drift::AstLabel &l : t->ast)
            ast.append(QJsonObject{{QStringLiteral("label"), l.label}, {QStringLiteral("p"), round2(l.probability)}});
        o.insert(QStringLiteral("ast"), ast);
        o.insert(QStringLiteral("speech"), round2(t->speech));
    }
    if (!t->soundsLike.isEmpty()) {
        QJsonArray like;
        for (const drift::ClapTag &tag : t->soundsLike) {
            like.append(QJsonObject{{QStringLiteral("term"), tag.text},
                                    {QStringLiteral("z"), round2(tag.z)},
                                    {QStringLiteral("verdict"), drift::clapVerdict(tag.z)}});
        }
        o.insert(QStringLiteral("sounds_like"), like);
    }
    return o;
}

QJsonObject sectionJson(const drift::ClapSection &s, double offset)
{
    static const char *const names[] = {"music_genre", "music_mood", "instruments", "ambience", "voice"};
    QJsonObject o{{QStringLiteral("start"), round3(offset + s.start)},
                  {QStringLiteral("end"), round3(offset + s.end)},
                  {QStringLiteral("label"), s.label},
                  {QStringLiteral("windows"), s.windows},
                  {QStringLiteral("coherence"), round2(s.coherence)}};
    if (s.levelDb > -120.0)
        o.insert(QStringLiteral("level_lufs"), std::round(s.levelDb * 10.0) / 10.0);
    QJsonObject tags;
    for (int c = 0; c < drift::kClapSectionCategories; ++c) {
        QJsonArray rows;
        for (const drift::ClapTag &t : s.tags[c])
            rows.append(tagJson(t));
        tags.insert(QLatin1String(names[c]), rows);
    }
    o.insert(QStringLiteral("tags"), tags);
    if (s.hasAst) {
        QJsonArray top;
        for (const drift::AstLabel &l : s.ast.top)
            top.append(QJsonObject{{QStringLiteral("label"), l.label}, {QStringLiteral("p"), round2(l.probability)}});
        o.insert(QStringLiteral("speech"), round2(s.ast.speech));
        o.insert(QStringLiteral("music"), round2(s.ast.music));
        o.insert(QStringLiteral("silence"), round2(s.ast.silence));
        o.insert(QStringLiteral("ast_top"), top);
    }
    return o;
}

QJsonObject presenceJson(const drift::AstPresence &p, double offset)
{
    QJsonArray rows;
    for (const drift::AstPresenceSpan &s : p.spans) {
        if (rows.size() >= kMcpMaxPresenceRows)
            break;
        rows.append(QJsonObject{{QStringLiteral("start"), round3(offset + s.start)},
                                {QStringLiteral("end"), round3(offset + s.end)},
                                {QStringLiteral("kind"), s.kind}});
    }
    QJsonObject o{{QStringLiteral("speech_seconds"), round2(p.speechSeconds)},
                  {QStringLiteral("music_seconds"), round2(p.musicSeconds)},
                  {QStringLiteral("silence_seconds"), round2(p.silenceSeconds)},
                  {QStringLiteral("segments"), rows}};
    if (p.spans.size() > rows.size())
        o.insert(QStringLiteral("truncated"), true);
    return o;
}

std::vector<float> astInput(const QVector<float> &modelMono)
{
    return drift::AstClassifier::resample48kTo16k(modelMono.constData(), modelMono.size());
}

QJsonObject momentJson(const drift::AudioMoment &m, double offset)
{
    return {{QStringLiteral("start"), round3(offset + m.start)},
            {QStringLiteral("end"), round3(offset + m.end)},
            {QStringLiteral("z"), round2(m.z)},
            {QStringLiteral("cosine"), round3(m.cosine)},
            {QStringLiteral("verdict"), drift::clapVerdict(m.z)},
            {QStringLiteral("source"), m.event ? QStringLiteral("event") : QStringLiteral("window")}};
}

QString findSummary(const QString &term, const QList<drift::AudioMoment> &moments)
{
    if (moments.isEmpty())
        return QStringLiteral("No moments scored for \"%1\".").arg(term);
    const drift::AudioMoment &best = moments.first();
    QString text = QStringLiteral("Best match for \"%1\": %2 (z %3), %4-%5 s.")
                       .arg(term, drift::clapVerdict(best.z)).arg(double(best.z), 0, 'f', 1)
                       .arg(best.start, 0, 'f', 1).arg(best.end, 0, 'f', 1);
    if (best.z < 2.0f)
        text += QStringLiteral(" Below 2 nothing in this audio sounds like it.");
    return text;
}

} // namespace

struct McpController::ResolvedAudio
{
    drift::Project snap;
    double start = 0.0;
    double duration = 0.0;
    QString source;
    QString title;
    QString assetPath;      // asset mode on a video file: where the frames come from
    bool leadIn = false;    // the span begins where the sound does
    QList<double> cuts;     // video cut points inside the span
};

QJsonObject McpController::resolveAudio(const AudioRequest &request, double maxSeconds,
                                        ResolvedAudio *out) const
{
    using namespace drift::mcp;
    out->source = request.mode;
    if (request.mode == QLatin1String("clip")) {
        if (!m_app->isValidClipIndex(request.track, request.clip))
            return err("not_found", QStringLiteral("Unknown clip"));
        const drift::Clip &clip = m_app->m_project.tracks().at(request.track).clips.at(request.clip);
        out->snap = m_app->m_project.detachedCopy();
        muteAllButClip(out->snap, audibleClipIdFor(m_app->m_project, clip));
        out->start = drift::usToSeconds(clip.timelineStart);
        out->duration = drift::usToSeconds(clip.timelineDuration);
        out->title = clip.path.isEmpty() ? QStringLiteral("Clip") : QFileInfo(clip.path).fileName();
        out->leadIn = true;
    } else if (request.mode == QLatin1String("asset")) {
        const drift::MediaAsset *asset = m_app->m_project.asset(request.assetId);
        if (!asset)
            return err("not_found", QStringLiteral("Unknown asset"));
        if (asset->path.isEmpty())
            return err("type_mismatch", QStringLiteral("Asset has no file"));
        const double total = drift::usToSeconds(asset->durationUs);
        out->start = qMax(0.0, request.start);
        out->duration = request.duration > 0.0 ? qMin(request.duration, total - out->start) : total - out->start;
        if (out->duration <= 0.0)
            return err("bad_args", QStringLiteral("Range is past the end of the asset"));
        const bool audioOnly = asset->kind == drift::MediaKind::Audio;
        drift::Clip clip;
        clip.id = QStringLiteral("audio-analysis");
        clip.type = audioOnly ? drift::ClipType::Audio : drift::ClipType::Video;
        clip.path = asset->path;
        clip.srcIn = drift::secondsToUs(out->start);
        clip.srcOut = drift::secondsToUs(out->start + out->duration);
        clip.timelineStart = 0;
        clip.timelineDuration = drift::secondsToUs(out->duration);
        out->snap.tracks().clear();
        drift::Track track;
        track.type = audioOnly ? drift::TrackType::Audio : drift::TrackType::Video;
        track.clips.append(clip);
        out->snap.tracks().append(track);
        out->title = asset->name.isEmpty() ? QFileInfo(asset->path).fileName() : asset->name;
        out->leadIn = out->start <= 0.001;
        if (asset->kind == drift::MediaKind::Video)
            out->assetPath = asset->path;
    } else {
        if (request.duration <= 0.0)
            return err("bad_args", QStringLiteral("duration required for a timeline range (or pass clip/asset)"));
        out->snap = m_app->m_project.detachedCopy();
        out->start = qMax(0.0, request.start);
        out->duration = request.duration;
        out->title = QStringLiteral("Timeline");
        out->leadIn = out->start <= 0.001;
    }
    if (out->duration <= 0.0)
        return err("bad_args", QStringLiteral("Nothing to analyse"));
    if (out->duration > maxSeconds)
        return err("bad_args", QStringLiteral("duration must be <= %1 seconds").arg(maxSeconds));
    if (request.mode != QLatin1String("asset"))
        out->cuts = videoCutTimes(m_app->m_project, out->start, out->start + out->duration);
    return {};
}

QJsonObject McpController::audioMeasure(const AudioRequest &request) const
{
    using namespace drift::mcp;
    ResolvedAudio audio;
    if (const QJsonObject failure = resolveAudio(request, kMcpMaxMeasureSeconds, &audio); !failure.isEmpty())
        return failure;
    const MixedAudio mixed = blockingMixAnalysis(audio.snap, audio.start, audio.duration, false, true);
    if (!mixed.loudness.ok)
        return err("bad_args", QStringLiteral("No audio in range"));
    QJsonObject out = loudnessJson(mixed.loudness, audio.start, request.curve);
    out.insert(QStringLiteral("source"), audio.source);
    out.insert(QStringLiteral("start"), round3(audio.start));
    out.insert(QStringLiteral("duration"), round3(audio.duration));
    if (!mixed.loudness.hasTruePeak)
        out.insert(QStringLiteral("silent"), true);
    return ok(out);
}

QJsonObject McpController::audioEvents(const AudioRequest &request) const
{
    using namespace drift::mcp;
    ResolvedAudio audio;
    if (const QJsonObject failure = resolveAudio(request, kMcpMaxAnalysisSeconds, &audio); !failure.isEmpty())
        return failure;
    const MixedAudio mixed = blockingMixAnalysis(audio.snap, audio.start, audio.duration, true, false);
    const drift::AudioEventsResult found = blockingEvents(mixed.mono, audio.start, audio.leadIn);

    QList<drift::AudioEvent> events;
    int swells = 0;
    for (const drift::AudioEvent &e : found.events) {
        if (e.prominenceDb < request.minProminence)
            continue;
        events.append(e);
        swells += e.kind == drift::AudioEvent::Kind::Swell;
    }
    const int total = events.size();
    const int limit = qBound(1, request.limit, kMcpMaxAudioEvents);
    if (events.size() > limit) {
        std::stable_sort(events.begin(), events.end(), [](const drift::AudioEvent &a, const drift::AudioEvent &b) {
            return a.prominenceDb > b.prominenceDb;
        });
        events.resize(limit);
        std::sort(events.begin(), events.end(),
                  [](const drift::AudioEvent &a, const drift::AudioEvent &b) { return a.seconds < b.seconds; });
    }
    QJsonArray rows;
    for (const drift::AudioEvent &e : std::as_const(events))
        rows.append(eventJson(e));
    return ok({{QStringLiteral("source"), audio.source},
               {QStringLiteral("start"), round3(audio.start)},
               {QStringLiteral("duration"), round3(audio.duration)},
               {QStringLiteral("n"), total},
               {QStringLiteral("swells"), swells},
               {QStringLiteral("truncated"), total > events.size()},
               {QStringLiteral("events"), rows}});
}

QJsonObject McpController::audioSync(const AudioRequest &request) const
{
    using namespace drift::mcp;
    ResolvedAudio audio;
    if (const QJsonObject failure = resolveAudio(request, kMcpMaxAnalysisSeconds, &audio); !failure.isEmpty())
        return failure;

    QList<double> cues;
    if (request.hasAt) {
        for (double t : request.at) {
            if (t >= audio.start && t <= audio.start + audio.duration)
                cues.append(t);
        }
    } else {
        cues = audio.cuts;
    }
    if (cues.isEmpty()) {
        return err("not_found", request.hasAt ? QStringLiteral("None of the at times fall inside the range")
                                              : QStringLiteral("No cuts between video clips in that range — pass at:[…] with the times to check"));
    }
    std::sort(cues.begin(), cues.end());
    const bool truncated = cues.size() > kMcpMaxSyncCues;
    if (truncated)
        cues.resize(kMcpMaxSyncCues);

    const MixedAudio mixed = blockingMixAnalysis(audio.snap, audio.start, audio.duration, true, false);
    const drift::AudioEventsResult found = blockingEvents(mixed.mono, audio.start, audio.leadIn);
    drift::AudioSyncOptions options;
    options.toleranceSeconds = qBound(5.0, request.toleranceMs, 250.0) / 1000.0;
    options.levels = &found.levels;
    const drift::AudioSyncResult sync = drift::matchSync(cues, found.events, options);

    QJsonArray items;
    for (const drift::AudioSyncItem &it : sync.items)
        items.append(syncItemJson(it));
    QJsonObject out{{QStringLiteral("source"), audio.source},
                    {QStringLiteral("start"), round3(audio.start)},
                    {QStringLiteral("duration"), round3(audio.duration)},
                    {QStringLiteral("cues"), request.hasAt ? QStringLiteral("at") : QStringLiteral("cuts")},
                    {QStringLiteral("events"), found.events.size()},
                    {QStringLiteral("stats"), syncStatsJson(sync.stats)},
                    {QStringLiteral("items"), items}};
    if (truncated)
        out.insert(QStringLiteral("truncated"), true);
    return ok(out);
}

QJsonObject McpController::audioTag(const AudioRequest &request)
{
    using namespace drift::mcp;
    if (const QJsonObject missing = clapUnavailable("audio_tag"); !missing.isEmpty())
        return missing;
    ResolvedAudio audio;
    if (const QJsonObject failure = resolveAudio(request, kMcpMaxAnalysisSeconds, &audio); !failure.isEmpty())
        return failure;
    const MixedAudio mixed = blockingMixAnalysis(audio.snap, audio.start, audio.duration, true, false);
    const drift::AudioEventsResult found = blockingEvents(mixed.mono, audio.start, audio.leadIn);
    const QVector<float> mono = modelGain(mixed.mono);

    drift::AudioTagOptions options;
    options.maxTagged = qBound(1, request.tagLimit, 96);
    options.sections = request.sections;
    options.astEvents = drift::AstClassifier::modelPresent() ? qMax(1, options.maxTagged / 20) : 0;
    options.astSections = drift::AstClassifier::modelPresent() ? 3 : 0;
    const QList<double> &cues = request.hasAt ? request.at : audio.cuts;
    for (double t : cues)
        options.near.append(t - audio.start);
    const drift::AudioTags tags = blockingRun([&]() {
        return drift::tagAudio(mono.constData(), mono.size(), audio.start, found.events, options);
    });
    if (!tags.clap)
        return err("internal", tags.clapError);

    QList<const drift::TaggedEvent *> byEvent(found.events.size(), nullptr);
    for (const drift::TaggedEvent &t : tags.events)
        byEvent[t.index] = &t;
    QJsonArray events;
    for (int i = 0; i < found.events.size(); ++i) {
        if (byEvent.at(i))
            events.append(taggedEventJson(found.events.at(i), byEvent.at(i)));
    }
    QJsonArray sections;
    QStringList labels;
    for (const drift::ClapSection &s : tags.sections) {
        sections.append(sectionJson(s, audio.start));
        labels << QStringLiteral("%1-%2 s %3").arg(audio.start + s.start, 0, 'f', 1).arg(audio.start + s.end, 0, 'f', 1).arg(s.label);
    }
    QJsonObject out{{QStringLiteral("source"), audio.source},
                    {QStringLiteral("start"), round3(audio.start)},
                    {QStringLiteral("duration"), round3(audio.duration)},
                    {QStringLiteral("models"), QJsonObject{{QStringLiteral("clap"), true}, {QStringLiteral("ast"), tags.ast}}},
                    {QStringLiteral("n_events"), found.events.size()},
                    {QStringLiteral("tagged"), events.size()},
                    {QStringLiteral("sections"), sections},
                    {QStringLiteral("events"), events},
                    {QStringLiteral("summary"), QStringLiteral("%1 section(s)%2; %3 of %4 events tagged.")
                                                    .arg(sections.size())
                                                    .arg(labels.isEmpty() ? QString() : QStringLiteral(": ") + labels.mid(0, 6).join(QStringLiteral("; ")))
                                                    .arg(events.size()).arg(found.events.size())}};
    if (!tags.ast)
        out.insert(QStringLiteral("note"), tags.astError.isEmpty()
                       ? QStringLiteral("Speech and music need the audio.ast addon: install_addon({id:\"audio.ast\"})")
                       : tags.astError);
    return ok(out);
}

QJsonObject McpController::audioFind(const AudioRequest &request)
{
    using namespace drift::mcp;
    const QString term = request.term.trimmed();
    if (term.isEmpty())
        return err("bad_args", QStringLiteral("term required (one of the vocabulary terms)"));
    if (const QJsonObject missing = clapUnavailable("audio_find"); !missing.isEmpty())
        return missing;
    drift::ClapTagger &clap = drift::ClapTagger::instance();
    const int termIndex = clap.termIndex(term);
    if (termIndex < 0)
        return unknownTerm(term);
    ResolvedAudio audio;
    if (const QJsonObject failure = resolveAudio(request, kMcpMaxAnalysisSeconds, &audio); !failure.isEmpty())
        return failure;
    const MixedAudio mixed = blockingMixAnalysis(audio.snap, audio.start, audio.duration, true, false);
    const drift::AudioEventsResult found = blockingEvents(mixed.mono, audio.start, audio.leadIn);
    const QVector<float> mono = modelGain(mixed.mono);
    const drift::MomentIndex index = blockingRun([&]() {
        return drift::indexMoments(clap, mono.constData(), mono.size(), audio.start, found.events);
    });
    if (!index.valid())
        return err("internal", clap.lastError());
    const QList<drift::AudioMoment> moments = drift::findMoments(clap, index, termIndex, qBound(1, request.top, 20));
    QJsonArray rows;
    for (const drift::AudioMoment &m : moments)
        rows.append(momentJson(m, audio.start));
    const QString canonical = clap.vocabularyTerms().at(termIndex);
    return ok({{QStringLiteral("term"), canonical},
               {QStringLiteral("category"), clap.termCategory(termIndex)},
               {QStringLiteral("source"), audio.source},
               {QStringLiteral("start"), round3(audio.start)},
               {QStringLiteral("duration"), round3(audio.duration)},
               {QStringLiteral("candidates"), static_cast<int>(index.entries.size())},
               {QStringLiteral("moments"), rows},
               {QStringLiteral("summary"), findSummary(canonical, moments)}});
}

QJsonObject McpController::audioRank(const AudioRequest &request)
{
    using namespace drift::mcp;
    const QString term = request.term.trimmed();
    if (term.isEmpty())
        return err("bad_args", QStringLiteral("term required (one of the vocabulary terms)"));
    if (const QJsonObject missing = clapUnavailable("audio_rank"); !missing.isEmpty())
        return missing;
    drift::ClapTagger &clap = drift::ClapTagger::instance();
    const int termIndex = clap.termIndex(term);
    if (termIndex < 0)
        return unknownTerm(term);

    struct Item
    {
        QString id, name, path;
        double seconds = 0.0;
    };
    QList<Item> items;
    auto add = [&](const drift::MediaAsset &a) {
        if (a.path.isEmpty() || (a.kind != drift::MediaKind::Audio && a.kind != drift::MediaKind::Video))
            return;
        items.append({a.id, a.name.isEmpty() ? QFileInfo(a.path).fileName() : a.name, a.path, drift::usToSeconds(a.durationUs)});
    };
    if (request.assetIds.isEmpty()) {
        for (const drift::MediaAsset &a : m_app->m_project.assets())
            add(a);
        std::sort(items.begin(), items.end(), [](const Item &a, const Item &b) { return a.name.localeAwareCompare(b.name) < 0; });
    } else {
        for (const QString &id : request.assetIds) {
            if (const drift::MediaAsset *a = m_app->m_project.asset(id))
                add(*a);
        }
    }
    if (items.isEmpty())
        return err("not_found", QStringLiteral("No audio or video assets to rank"));
    const bool truncated = items.size() > kMcpMaxRankedAssets;
    if (truncated)
        items = items.mid(0, kMcpMaxRankedAssets);

    struct Ranked
    {
        Item item;
        bool audio = false;
        drift::ClapEmbedding embedding{};
        double peakDb = -120.0;
    };
    const QList<Ranked> ranked = blockingRun([&]() {
        QList<Ranked> out;
        for (const Item &item : items) {
            Ranked r;
            r.item = item;
            const std::vector<drift::ClapSpan> windows = drift::ClapTagger::clipSpans(
                static_cast<qint64>(item.seconds * kMcpAudioRate));
            std::vector<drift::ClapEmbedding> parts;
            float peak = 0.0f;
            for (const drift::ClapSpan &w : windows) {
                std::vector<float> pcm = drift::readMono(item.path, drift::secondsToUs(double(w.begin) / kMcpAudioRate),
                                                         drift::secondsToUs(double(w.end) / kMcpAudioRate), kMcpAudioRate);
                if (pcm.empty())
                    continue;
                for (float &v : pcm) {
                    v *= static_cast<float>(M_SQRT2);
                    peak = std::max(peak, std::abs(v));
                }
                drift::ClapEmbedding e;
                if (drift::ClapTagger::instance().embedClip(pcm.data(), static_cast<qint64>(pcm.size()), &e, 1))
                    parts.push_back(e);
            }
            if (!parts.empty()) {
                r.audio = true;
                r.embedding = drift::ClapTagger::meanOf(parts);
                r.peakDb = 20.0 * std::log10(std::max(peak, 1e-6f) / M_SQRT2);
            }
            out.append(r);
        }
        return out;
    });

    struct Row
    {
        QJsonObject json;
        float z;
        bool silent;
    };
    QList<Row> rows;
    QStringList skipped;
    for (const Ranked &r : ranked) {
        if (!r.audio) {
            skipped << r.item.name;
            continue;
        }
        const auto scores = clap.termScores(r.embedding);
        const float z = scores[static_cast<size_t>(termIndex)].z;
        const bool silent = r.peakDb < kMcpSilentDbfs;
        const QList<drift::ClapTag> like = clap.topTerms(r.embedding, 1, {QStringLiteral("quality"), QStringLiteral("production")});
        QJsonObject o{{QStringLiteral("asset"), r.item.id},
                      {QStringLiteral("name"), r.item.name},
                      {QStringLiteral("z"), round2(z)},
                      {QStringLiteral("cosine"), round3(scores[static_cast<size_t>(termIndex)].cosine)},
                      {QStringLiteral("verdict"), silent ? QStringLiteral("no match") : drift::clapVerdict(z)},
                      {QStringLiteral("near_silent"), silent}};
        if (!like.isEmpty())
            o.insert(QStringLiteral("sounds_most_like"), QStringLiteral("%1 (z %2)").arg(like.first().text).arg(double(like.first().z), 0, 'f', 1));
        rows.append({o, z, silent});
    }
    std::stable_sort(rows.begin(), rows.end(), [](const Row &a, const Row &b) {
        return a.silent != b.silent ? !a.silent : a.z > b.z;
    });
    QJsonArray results;
    int strong = 0, plausible = 0, weak = 0;
    for (const Row &r : rows) {
        if (results.size() < qBound(1, request.top, 100))
            results.append(r.json);
        if (r.silent)
            continue;
        const QString verdict = r.json.value(QStringLiteral("verdict")).toString();
        strong += verdict == QLatin1String("strong");
        plausible += verdict == QLatin1String("plausible");
        weak += verdict == QLatin1String("weak");
    }
    const QString canonical = clap.vocabularyTerms().at(termIndex);
    QString summary = QStringLiteral("Ranked %1 asset(s) for \"%2\": %3 strong, %4 plausible, %5 weak.")
                          .arg(rows.size()).arg(canonical).arg(strong).arg(plausible).arg(weak);
    if (!rows.isEmpty() && !rows.first().silent)
        summary += QStringLiteral(" Best: %1 (z %2, %3).").arg(rows.first().json.value(QStringLiteral("name")).toString())
                       .arg(double(rows.first().z), 0, 'f', 1).arg(rows.first().json.value(QStringLiteral("verdict")).toString());
    QJsonObject out{{QStringLiteral("term"), canonical},
                    {QStringLiteral("ranked"), rows.size()},
                    {QStringLiteral("results"), results},
                    {QStringLiteral("summary"), summary}};
    if (!skipped.isEmpty())
        out.insert(QStringLiteral("skipped_no_audio"), QJsonArray::fromStringList(skipped));
    if (truncated)
        out.insert(QStringLiteral("truncated"), true);
    return ok(out);
}

QJsonObject McpController::audioPresence(const AudioRequest &request)
{
    using namespace drift::mcp;
    if (const QJsonObject missing = astUnavailable("audio_presence"); !missing.isEmpty())
        return missing;
    ResolvedAudio audio;
    if (const QJsonObject failure = resolveAudio(request, kMcpMaxAnalysisSeconds, &audio); !failure.isEmpty())
        return failure;
    const MixedAudio mixed = blockingMixAnalysis(audio.snap, audio.start, audio.duration, true, false);
    const QVector<float> mono = modelGain(mixed.mono);
    const drift::AstPresence presence = blockingRun([&]() {
        const std::vector<float> pcm = astInput(mono);
        return drift::astPresence(drift::AstClassifier::instance(), pcm.data(), static_cast<qint64>(pcm.size()));
    });
    if (!presence.ok)
        return err("internal", presence.error);
    QJsonObject out = presenceJson(presence, audio.start);
    out.insert(QStringLiteral("source"), audio.source);
    out.insert(QStringLiteral("start"), round3(audio.start));
    out.insert(QStringLiteral("duration"), round3(audio.duration));
    out.insert(QStringLiteral("summary"),
               QStringLiteral("Speech %1 s, music %2 s, silence %3 s of %4 s.")
                   .arg(presence.speechSeconds, 0, 'f', 1).arg(presence.musicSeconds, 0, 'f', 1)
                   .arg(presence.silenceSeconds, 0, 'f', 1).arg(audio.duration, 0, 'f', 1));
    return ok(out);
}

struct McpController::AudioSheetJob
{
    ResolvedAudio audio;
    AudioRequest request;
    QList<double> cues;
    std::shared_ptr<const drift::Project> frameProject;
};

QJsonObject McpController::prepareAudioSheet(const AudioRequest &request, std::shared_ptr<AudioSheetJob> *out)
{
    using namespace drift::mcp;
    m_app->setPlaying(false);

    auto job = std::make_shared<AudioSheetJob>();
    job->request = request;
    ResolvedAudio &audio = job->audio;
    if (const QJsonObject failure = resolveAudio(request, kMcpMaxAnalysisSeconds, &audio); !failure.isEmpty())
        return failure;

    job->cues = audio.cuts;
    if (request.hasAt) {
        job->cues.clear();
        for (double t : request.at) {
            if (t > audio.start && t < audio.start + audio.duration)
                job->cues.append(t);
        }
        std::sort(job->cues.begin(), job->cues.end());
    }
    if (request.frames && (request.mode != QLatin1String("asset") || !audio.assetPath.isEmpty()))
        job->frameProject = std::make_shared<const drift::Project>(m_app->m_project.detachedCopy());
    *out = job;
    return {};
}

QJsonObject McpController::renderAudioSheet(const AudioSheetJob &job, QImage *sheet)
{
    using namespace drift::mcp;
    const AudioRequest &request = job.request;
    const ResolvedAudio &audio = job.audio;
    const QList<double> &cues = job.cues;

    const MixedAudio mixed = mixAnalysis(audio.snap, audio.start, audio.duration, true, true);
    if (!mixed.loudness.ok)
        return err("bad_args", QStringLiteral("No audio in range"));
    const drift::AudioEventsResult found = detectEvents(mixed.mono, audio.start, audio.leadIn);
    drift::audiosheet::Input in;
    in.title = audio.title;
    in.startSeconds = audio.start;
    in.durationSeconds = audio.duration;
    in.mono = mixed.mono.constData();
    in.monoFrames = mixed.mono.size();
    in.sampleRate = kMcpAudioRate;
    in.loudness = mixed.loudness;
    in.events = found.events;
    in.cuts = cues;
    if (!cues.isEmpty()) {
        drift::AudioSyncOptions options;
        options.levels = &found.levels;
        in.sync = drift::matchSync(cues, found.events, options);
        in.hasSync = true;
    }

    drift::AudioTags tagged;
    drift::AstPresence presence;
    if (request.tags && (drift::ClapTagger::modelPresent() || drift::AstClassifier::modelPresent())) {
        const QVector<float> modelMono = modelGain(mixed.mono);
        drift::AudioTagOptions options;
        options.astEvents = 1;
        for (double c : cues)
            options.near.append(c - audio.start);
        tagged = drift::tagAudio(modelMono.constData(), modelMono.size(), audio.start, found.events, options);
        if (tagged.ast) {
            const std::vector<float> pcm = astInput(modelMono);
            presence = drift::astPresence(drift::AstClassifier::instance(), pcm.data(), static_cast<qint64>(pcm.size()));
        }
        for (const drift::ClapSection &s : tagged.sections)
            in.sections.append({audio.start + s.start, audio.start + s.end, s.label});
        for (const drift::AstPresenceSpan &s : presence.spans) {
            if (s.kind.startsWith(QLatin1String("speech")) || s.kind == QLatin1String("music"))
                in.presence.append({audio.start + s.start, audio.start + s.end, s.kind});
        }
        if (!tagged.events.isEmpty()) {
            in.labels = drift::audiosheet::defaultEventLabels(in);
            for (const drift::TaggedEvent &t : std::as_const(tagged.events)) {
                if (t.label.isEmpty())
                    continue;
                in.labels[t.index].kind = t.kind;
                in.labels[t.index].text = t.label;
            }
        }
    }

    if (audio.duration >= AudioOnsets::kMinAnalysisSec) {
        const AudioBeatAnalysis beats = runBeatAnalysis(audio.snap, drift::secondsToUs(audio.start),
                                                        drift::secondsToUs(audio.duration), audio.start);
        if (beats.bpm > 0.0) {
            in.bpm = beats.bpm;
            in.beats = beats.beats;
            const int per = qMax(1, beats.beatsPerBar);
            for (int i = qBound(0, beats.firstDownbeat, qMax(0, beats.beats.size() - 1)); i < beats.beats.size(); i += per)
                in.downbeats.append(beats.beats.at(i));
        }
    }

    // A frame just after each cut, the way the frames tool renders its tiles, plus the first
    // moment of the span.
    QList<double> frameTimes;
    if (job.frameProject) {
        const double end = audio.start + audio.duration;
        QList<double> candidates;
        if (cues.isEmpty() || cues.first() - audio.start > kMcpSheetFrameLag)
            candidates.append(audio.start);
        for (double c : std::as_const(cues))
            candidates.append(qMin(c + kMcpSheetFrameLag, end - 0.05));
        const QList<int> pick = drift::framesheet::selectUniform(candidates.size(), qMin(candidates.size(), kMcpSheetMaxFrames));
        for (int i : pick)
            frameTimes.append(candidates.at(i));
    }
    QList<drift::audiosheet::FrameAt> frames;
    if (!frameTimes.isEmpty()) {
        FrameSource src;
        src.project = job.frameProject;
        if (request.mode == QLatin1String("asset"))
            src.path = audio.assetPath;
        FrameCompositor compositor;
        compositor.setProject(src.project.get());
        const int maxH = kMcpSheetFrameHeight;
        const int maxW = src.source() ? maxH * 2
                                      : qMax(1, int(std::lround(double(maxH) * src.longEdge()
                                                                / double(qMax(1, src.project->height())))));
        for (double t : frameTimes) {
            const QImage image = src.render(compositor, t, maxW, maxH);
            if (!image.isNull())
                frames.append({t, image});
        }
    }
    in.frames = frames;

    drift::audiosheet::Options opt;
    opt.width = request.width;
    *sheet = drift::audiosheet::render(in, opt);
    if (sheet->isNull())
        return err("capture_failed", QStringLiteral("Could not render the audio sheet"));

    const drift::LoudnessReport &lo = mixed.loudness;
    int swells = 0;
    for (const drift::AudioEvent &e : found.events)
        swells += e.kind == drift::AudioEvent::Kind::Swell;
    QStringList parts;
    if (lo.hasIntegrated)
        parts << QStringLiteral("%1 LUFS").arg(lo.integratedLufs, 0, 'f', 1);
    if (lo.hasTruePeak)
        parts << QStringLiteral("TP %1 dBTP").arg(lo.truePeakDb, 0, 'f', 1);
    if (lo.hasLra)
        parts << QStringLiteral("LRA %1 LU").arg(lo.lraLu, 0, 'f', 1);
    parts << QStringLiteral("%1 events (%2 swells)").arg(found.events.size()).arg(swells);
    if (in.bpm > 0.0)
        parts << QStringLiteral("~%1 BPM").arg(in.bpm, 0, 'f', 0);
    if (!lo.gaps.isEmpty())
        parts << QStringLiteral("%1 gaps").arg(lo.gaps.size());
    if (!tagged.sections.isEmpty())
        parts << QStringLiteral("%1 sections").arg(tagged.sections.size());
    if (presence.ok)
        parts << QStringLiteral("speech %1 s, music %2 s").arg(presence.speechSeconds, 0, 'f', 0).arg(presence.musicSeconds, 0, 'f', 0);
    if (in.hasSync) {
        const drift::AudioSyncStats &st = in.sync.stats;
        parts << QStringLiteral("%1 cuts, %2% with an onset within %3 ms").arg(st.n).arg(100.0 * st.hitWithinTolerance, 0, 'f', 0).arg(st.toleranceMs, 0, 'f', 0);
    }

    QJsonObject meta = ok({
        {QStringLiteral("source"), audio.source},
        {QStringLiteral("start"), round3(audio.start)},
        {QStringLiteral("duration"), round3(audio.duration)},
        {QStringLiteral("summary"), QStringLiteral("Audio sheet %1: %2.").arg(audio.title, parts.join(QStringLiteral(", ")))},
        {QStringLiteral("events"), found.events.size()},
        {QStringLiteral("swells"), swells},
        {QStringLiteral("cuts"), cues.size()},
        {QStringLiteral("frames"), frames.size()},
    });
    const QJsonObject loud = loudnessJson(lo, audio.start, false);
    for (auto it = loud.begin(); it != loud.end(); ++it)
        meta.insert(it.key(), it.value());
    if (in.bpm > 0.0)
        meta.insert(QStringLiteral("bpm"), round2(in.bpm));
    if (request.tags) {
        QJsonObject models{{QStringLiteral("clap"), tagged.clap}, {QStringLiteral("ast"), tagged.ast}};
        meta.insert(QStringLiteral("tags"), models);
        QJsonArray missing;
        if (!drift::ClapTagger::modelPresent())
            missing.append(QStringLiteral("audio.clap"));
        if (!drift::AstClassifier::modelPresent())
            missing.append(QStringLiteral("audio.ast"));
        if (!missing.isEmpty())
            meta.insert(QStringLiteral("tags_missing"), missing);
        if (!tagged.clapError.isEmpty() || !tagged.astError.isEmpty())
            meta.insert(QStringLiteral("tags_error"), tagged.clapError.isEmpty() ? tagged.astError : tagged.clapError);
        QJsonArray sections;
        for (const drift::ClapSection &s : std::as_const(tagged.sections))
            sections.append(QJsonObject{{QStringLiteral("start"), round3(audio.start + s.start)},
                                        {QStringLiteral("end"), round3(audio.start + s.end)},
                                        {QStringLiteral("label"), s.label}});
        meta.insert(QStringLiteral("sections"), sections);
        QJsonArray taggedEvents;
        for (const drift::TaggedEvent &t : std::as_const(tagged.events)) {
            taggedEvents.append(QJsonObject{{QStringLiteral("at"), round3(found.events.at(t.index).seconds)},
                                            {QStringLiteral("sound"), t.kind},
                                            {QStringLiteral("label"), t.label}});
        }
        meta.insert(QStringLiteral("tagged_events"), taggedEvents);
        if (presence.ok) {
            meta.insert(QStringLiteral("presence"), presenceJson(presence, audio.start));
        }
    }
    if (in.hasSync) {
        meta.insert(QStringLiteral("sync"), syncStatsJson(in.sync.stats));
        QJsonObject counts;
        for (const drift::AudioSyncItem &item : std::as_const(in.sync.items)) {
            const QString name = drift::syncStatusName(item.status);
            counts.insert(name, counts.value(name).toInt() + 1);
        }
        meta.insert(QStringLiteral("sync_counts"), counts);
        QJsonArray items;
        for (const drift::AudioSyncItem &item : std::as_const(in.sync.items)) {
            if (items.size() >= kMcpMaxSyncCues)
                break;
            items.append(syncItemJson(item));
        }
        meta.insert(QStringLiteral("sync_items"), items);
    }

    return meta;
}

QJsonObject McpController::audioSheet(const AudioRequest &request)
{
    using namespace drift::mcp;
    std::shared_ptr<AudioSheetJob> job;
    if (const QJsonObject failure = prepareAudioSheet(request, &job); !failure.isEmpty())
        return failure;

    auto sheet = std::make_shared<QImage>();
    auto meta = std::make_shared<QJsonObject>();
    QEventLoop loop;
    (void)QtConcurrent::run([job, sheet, meta, &loop]() {
        *meta = renderAudioSheet(*job, sheet.get());
        QMetaObject::invokeMethod(&loop, &QEventLoop::quit, Qt::QueuedConnection);
    });
    loop.exec();
    if (!meta->value(QStringLiteral("ok")).toBool())
        return *meta;

    if (request.toPath) {
        const QString outPath = newCapturePath(m_app->m_project.id(), QStringLiteral("audiosheet"), QStringLiteral("png"));
        if (outPath.isEmpty() || !sheet->save(outPath, "PNG"))
            return err("capture_failed", QStringLiteral("Could not write PNG"));
        meta->insert(QStringLiteral("path"), outPath);
        meta->insert(QStringLiteral("w"), sheet->width());
        meta->insert(QStringLiteral("h"), sheet->height());
        return *meta;
    }
    QImage shown = *sheet;
    if (qMax(shown.width(), shown.height()) > kMcpSheetLongEdge)
        shown = shown.scaled(kMcpSheetLongEdge, kMcpSheetLongEdge, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    meta->insert(QStringLiteral("w"), shown.width());
    meta->insert(QStringLiteral("h"), shown.height());
    return imageResult(*meta, encodeJpeg(shown, kMcpSheetJpegQuality), QStringLiteral("image/jpeg"));
}

void McpController::generateAudioSheet(const QVariantMap &options)
{
    using namespace drift::mcp;
    if (m_audioSheetBusy)
        return;
    AudioRequest request;
    request.mode = options.value(QStringLiteral("mode"), QStringLiteral("timeline")).toString();
    request.track = options.value(QStringLiteral("track"), -1).toInt();
    request.clip = options.value(QStringLiteral("clip"), -1).toInt();
    request.start = options.value(QStringLiteral("start")).toDouble();
    request.duration = options.value(QStringLiteral("duration")).toDouble();
    request.width = options.value(QStringLiteral("width"), request.width).toInt();
    request.frames = options.value(QStringLiteral("frames"), true).toBool();
    request.tags = options.value(QStringLiteral("tags"), true).toBool();

    std::shared_ptr<AudioSheetJob> job;
    if (const QJsonObject failure = prepareAudioSheet(request, &job); !failure.isEmpty()) {
        m_audioSheetResult = failure.toVariantMap();
        emit audioSheetChanged();
        return;
    }
    m_audioSheetBusy = true;
    emit audioSheetChanged();

    auto sheet = std::make_shared<QImage>();
    auto *watcher = new QFutureWatcher<QJsonObject>(this);
    connect(watcher, &QFutureWatcher<QJsonObject>::finished, this, [this, watcher, sheet]() {
        QJsonObject meta = watcher->result();
        watcher->deleteLater();
        if (meta.value(QStringLiteral("ok")).toBool()) {
            const QString dir = m_imageLog->sessionDir();
            const QString path = dir.isEmpty() ? QString()
                                               : QDir(dir).filePath(QStringLiteral("audio-sheet-%1.png").arg(++m_audioSheetSeq));
            if (path.isEmpty() || !sheet->save(path, "PNG")) {
                meta = err("capture_failed", QStringLiteral("Could not write PNG"));
            } else {
                if (!m_audioSheetPath.isEmpty())
                    QFile::remove(m_audioSheetPath);
                m_audioSheetPath = path;
                meta.insert(QStringLiteral("path"), path);
                meta.insert(QStringLiteral("url"), QUrl::fromLocalFile(path).toString());
                meta.insert(QStringLiteral("w"), sheet->width());
                meta.insert(QStringLiteral("h"), sheet->height());
            }
        }
        meta.remove(QStringLiteral("sync_items"));
        m_audioSheetResult = meta.toVariantMap();
        m_audioSheetBusy = false;
        emit audioSheetChanged();
    });
    watcher->setFuture(QtConcurrent::run([job, sheet]() { return renderAudioSheet(*job, sheet.get()); }));
}

struct McpController::AudioSearchIndex
{
    drift::MomentIndex moments;
};

QStringList McpController::audioVocabulary() const
{
    return drift::ClapTagger::modelPresent() ? drift::ClapTagger::instance().vocabularyTerms() : QStringList();
}

void McpController::searchAudioSheet(const QVariantMap &options)
{
    using namespace drift::mcp;
    if (m_audioSearchBusy)
        return;
    auto finish = [this](const QJsonObject &result) {
        m_audioSearchResult = result.toVariantMap();
        m_audioSearchBusy = false;
        emit audioSearchChanged();
    };
    AudioRequest request;
    request.mode = options.value(QStringLiteral("mode"), QStringLiteral("timeline")).toString();
    request.track = options.value(QStringLiteral("track"), -1).toInt();
    request.clip = options.value(QStringLiteral("clip"), -1).toInt();
    request.start = options.value(QStringLiteral("start")).toDouble();
    request.duration = options.value(QStringLiteral("duration")).toDouble();
    const QString term = options.value(QStringLiteral("term")).toString().trimmed();

    m_audioSearchBusy = true;
    emit audioSearchChanged();
    if (!drift::ClapTagger::modelPresent())
        return finish(err("addon_missing", QStringLiteral("Install the audio tagging add-on to search by sound")));
    const int termIndex = drift::ClapTagger::instance().termIndex(term);
    if (termIndex < 0)
        return finish(term.isEmpty() ? err("bad_args", QStringLiteral("Type a sound to look for")) : unknownTerm(term));
    auto audio = std::make_shared<ResolvedAudio>();
    if (const QJsonObject failure = resolveAudio(request, kMcpMaxAnalysisSeconds, audio.get()); !failure.isEmpty())
        return finish(failure);

    const QString key = QStringLiteral("%1|%2|%3|%4|%5|%6|%7")
                            .arg(request.mode).arg(request.track).arg(request.clip).arg(request.assetId)
                            .arg(audio->start).arg(audio->duration).arg(m_editRevision);
    const std::shared_ptr<AudioSearchIndex> cached = key == m_audioSearchKey ? m_audioSearchIndex : nullptr;

    struct Outcome
    {
        QJsonObject result;
        std::shared_ptr<AudioSearchIndex> index;
    };
    auto *watcher = new QFutureWatcher<Outcome>(this);
    connect(watcher, &QFutureWatcher<Outcome>::finished, this, [this, watcher, key, finish]() {
        const Outcome outcome = watcher->result();
        watcher->deleteLater();
        if (outcome.index) {
            m_audioSearchIndex = outcome.index;
            m_audioSearchKey = key;
        }
        finish(outcome.result);
    });
    watcher->setFuture(QtConcurrent::run([audio, cached, termIndex]() -> Outcome {
        drift::ClapTagger &clap = drift::ClapTagger::instance();
        if (!clap.available())
            return {err("addon_missing", clap.lastError()), nullptr};
        std::shared_ptr<AudioSearchIndex> index = cached;
        if (!index) {
            const MixedAudio mixed = mixAnalysis(audio->snap, audio->start, audio->duration, true, false);
            const drift::AudioEventsResult found = detectEvents(mixed.mono, audio->start, audio->leadIn);
            const QVector<float> mono = modelGain(mixed.mono);
            index = std::make_shared<AudioSearchIndex>();
            index->moments = drift::indexMoments(clap, mono.constData(), mono.size(), audio->start, found.events);
            if (!index->moments.valid())
                return {err("internal", clap.lastError()), nullptr};
        }
        const QList<drift::AudioMoment> moments = drift::findMoments(clap, index->moments, termIndex, 8);
        QJsonArray rows;
        for (const drift::AudioMoment &m : moments)
            rows.append(momentJson(m, audio->start));
        const QString canonical = clap.vocabularyTerms().at(termIndex);
        return {ok({{QStringLiteral("term"), canonical},
                    {QStringLiteral("moments"), rows},
                    {QStringLiteral("summary"), findSummary(canonical, moments)}}),
                index};
    }));
}

QJsonObject McpController::normalizeVolume(int trackIndex, int clipIndex, double targetLufs)
{
    using namespace drift::mcp;
    if (!m_app->isValidClipIndex(trackIndex, clipIndex))
        return err("not_found", QStringLiteral("Unknown clip"));
    const QJsonObject measured = analyzeLoudness(trackIndex, clipIndex, 0, 0);
    if (!measured.value(QStringLiteral("ok")).toBool())
        return measured;
    return applyNormalizeGain(trackIndex, clipIndex,
                              measured.value(QStringLiteral("lufs")).toDouble(),
                              measured.value(QStringLiteral("true_peak_db")).toDouble(),
                              targetLufs);
}

QJsonObject McpController::applyNormalizeGain(int trackIndex, int clipIndex, double lufs,
                                              double peakDb, double targetLufs)
{
    const double deltaDb = targetLufs - lufs;
    // The measurement was taken through the clip's current volume, so the correction is relative
    // to it. Writing the gain as an absolute value discards whatever level the clip was already
    // set to, which also made calling the op twice give two different answers.
    const drift::Clip &target = m_app->m_project.tracks().at(trackIndex).clips.at(clipIndex);
    const double currentVolume =
        m_app->propertyValueAt(trackIndex, clipIndex, QStringLiteral("volume"),
                        drift::usToSeconds(target.timelineStart), 1.0);
    const double gain = currentVolume * qPow(10.0, deltaDb / 20.0);
    const QJsonObject set = setClipVolume(trackIndex, clipIndex, gain, false, 0);
    QJsonObject out = set;
    out.insert(QStringLiteral("measured_lufs"), lufs);
    out.insert(QStringLiteral("target_lufs"), targetLufs);
    // Hitting a loudness target can still put the peaks over the top; say so rather than leaving
    // the caller to discover it in the render.
    const double projectedPeakDb = peakDb + deltaDb;
    if (projectedPeakDb > -1.0) {
        out.insert(QStringLiteral("clipping"), true);
        out.insert(QStringLiteral("projected_true_peak_db"), projectedPeakDb);
    }
    return out;
}

QJsonObject McpController::duckUnder(int musicTrack, int musicClip, int overTrack,
                                        const QStringList &overClips, double amount, double attack,
                                        double release)
{
    using namespace drift::mcp;
    if (!m_app->isValidClipIndex(musicTrack, musicClip))
        return err("not_found", QStringLiteral("Unknown music clip"));
    const double amt = qBound(0.0, amount, 1.0);

    QList<SilenceRange> speech;
    auto addSpeech = [&](int tr, int cl) {
        const QJsonObject det = detectSilence(tr, cl, 0, 0, 0.02, 0.12, 0.0);
        if (!det.value(QStringLiteral("ok")).toBool())
            return;
        const drift::Clip &clip = m_app->m_project.tracks().at(tr).clips.at(cl);
        const double cs = drift::usToSeconds(clip.timelineStart);
        const double ce = drift::usToSeconds(clip.timelineStart + clip.timelineDuration);
        // Invert silence → speech, clipped to the clip.
        double cursor = cs;
        const QJsonArray silences = det.value(QStringLiteral("ranges")).toArray();
        for (const QJsonValue &v : silences) {
            const QJsonObject r = v.toObject();
            const double s = r.value(QStringLiteral("start")).toDouble();
            const double e = r.value(QStringLiteral("end")).toDouble();
            if (s > cursor)
                speech.append({cursor, s});
            cursor = qMax(cursor, e);
        }
        if (ce > cursor)
            speech.append({cursor, ce});
    };

    if (!overClips.isEmpty()) {
        for (const QString &id : overClips) {
            int tr = -1, cl = -1;
            if (findClipById(m_app->m_project, id, &tr, &cl))
                addSpeech(tr, cl);
        }
    } else if (overTrack >= 0 && overTrack < m_app->m_project.tracks().size()) {
        const drift::Track &track = m_app->m_project.tracks().at(overTrack);
        for (int c = 0; c < track.clips.size(); ++c)
            addSpeech(overTrack, c);
    } else {
        return err("bad_args", QStringLiteral("over_track or over_clips required"));
    }

    // Read the level the music sits at going into each dip *before* touching anything. Sampling
    // one value at the playhead flattened whatever envelope the music already had, and sampling it
    // after a previous pass had written its own keys made each re-run collapse the rest level
    // toward the ducked one — the pumping between words.
    QList<SilenceRange> dips;
    QList<double> restLevels;
    for (const SilenceRange &span : speech) {
        if (span.end - span.start < 0.05)
            continue;
        dips.append(span);
        restLevels.append(m_app->propertyValueAt(musicTrack, musicClip, QStringLiteral("volume"),
                                          span.start - attack, 1.0));
    }

    beginBatch();

    // Writing is otherwise purely additive: keys are addressed by exact microsecond, so a re-run
    // with any changed argument interleaves a fresh set beside the old one. Clear the windows this
    // pass owns first, so running it twice leaves the same curve as running it once.
    if (m_app->isValidClipIndex(musicTrack, musicClip)) {
        const drift::Clip &music = m_app->m_project.tracks().at(musicTrack).clips.at(musicClip);
        const double clipStart = drift::usToSeconds(music.timelineStart);
        QList<double> doomed;
        const auto &existing = music.volume.keyframes();
        for (auto it = existing.constBegin(); it != existing.constEnd(); ++it) {
            const double at = clipStart + drift::usToSeconds(it.key());
            for (const SilenceRange &span : dips) {
                if (at >= span.start - attack - 1e-6 && at <= span.end + release + 1e-6) {
                    doomed.append(at);
                    break;
                }
            }
        }
        for (const double at : doomed)
            m_app->removeClipKeyframe(musicTrack, musicClip, QStringLiteral("volume"), at);
    }

    int keys = 0;
    for (int i = 0; i < dips.size(); ++i) {
        const SilenceRange &span = dips.at(i);
        const double rest = restLevels.at(i);
        const double ducked = rest * amt;
        m_app->setClipKeyframe(musicTrack, musicClip, QStringLiteral("volume"),
                        span.start - attack, rest);
        m_app->setClipKeyframe(musicTrack, musicClip, QStringLiteral("volume"), span.start, ducked);
        m_app->setClipKeyframe(musicTrack, musicClip, QStringLiteral("volume"), span.end, ducked);
        m_app->setClipKeyframe(musicTrack, musicClip, QStringLiteral("volume"),
                        span.end + release, rest);
        keys += 4;
    }
    endBatch(QStringLiteral("Duck under speech"), keys > 0);

    // The rest level is per dip now, so report the first one and say when they differ rather than
    // implying the whole clip was held at one level.
    const double firstRest = restLevels.isEmpty() ? 1.0 : restLevels.first();
    bool restVaries = false;
    for (const double level : restLevels)
        restVaries = restVaries || !qFuzzyCompare(level + 1.0, firstRest + 1.0);
    QJsonObject reply{{QStringLiteral("keys"), keys},
                      {QStringLiteral("speech"), speech.size()},
                      {QStringLiteral("rest"), round3(firstRest)},
                      {QStringLiteral("ducked"), round3(firstRest * amt)}};
    if (restVaries)
        reply.insert(QStringLiteral("rest_varies"), true);
    return ok(reply);
}

QJsonObject McpController::listFaceTrack(int trackIndex, int clipIndex) const
{
    using namespace drift::mcp;
    if (!m_app->isValidClipIndex(trackIndex, clipIndex))
        return err("not_found", QStringLiteral("Unknown clip"));
    const drift::Clip &clip = m_app->m_project.tracks().at(trackIndex).clips.at(clipIndex);
    if (clip.faceTrackPath.isEmpty())
        return err("not_found", QStringLiteral("No face track — call detect_faces first"));
    const std::shared_ptr<const drift::FaceTrack> track = drift::loadFaceTrackCached(clip.faceTrackPath);
    if (!track || track->isEmpty())
        return err("not_found", QStringLiteral("Face track file is missing or empty"));

    const int total = track->frames.size();
    const int cap = 200;
    const int step = qMax(1, (total + cap - 1) / cap);
    QJsonArray frames;
    for (int i = 0; i < total; i += step) {
        const drift::TimeUs relUs = track->fps > 0
                                        ? drift::TimeUs(i * drift::kUsPerSecond / track->fps)
                                        : 0;
        QJsonArray faces;
        for (const drift::FaceAnchors &face : track->frames.at(i).faces) {
            faces.append(QJsonObject{
                {QStringLiteral("cx"), face.faceCenter.x()},
                {QStringLiteral("cy"), face.faceCenter.y()},
                {QStringLiteral("rx"), face.faceRx},
                {QStringLiteral("ry"), face.faceRy},
                {QStringLiteral("valid"), face.valid},
            });
        }
        frames.append(QJsonObject{{QStringLiteral("t"), drift::usToSeconds(relUs)},
                                  {QStringLiteral("faces"), faces}});
    }
    return ok({{QStringLiteral("fps"), track->fps},
               {QStringLiteral("n"), total},
               {QStringLiteral("frames"), frames},
               {QStringLiteral("truncated"), step > 1}});
}

QJsonObject McpController::autoReframe(int trackIndex, int clipIndex, double aspect,
                                          const QString &mode)
{
    using namespace drift::mcp;
    if (!m_app->isValidClipIndex(trackIndex, clipIndex))
        return err("not_found", QStringLiteral("Unknown clip"));
    drift::Clip &clip = m_app->m_project.tracks()[trackIndex].clips[clipIndex];
    if (clip.type == drift::ClipType::Audio)
        return err("type_mismatch", QStringLiteral("Cannot reframe an audio clip"));

    const double canvasW = m_app->m_project.width();
    const double canvasH = m_app->m_project.height();
    double targetAspect = aspect > 0.01 ? aspect : (9.0 / 16.0);
    const QString m = mode.toLower();

    struct Sample {
        double t = 0.0;
        double cx = 0.5;
        double cy = 0.5;
        double rx = 0.2;
        double ry = 0.25;
    };
    QList<Sample> samples;
    if (m != QLatin1String("center") && !clip.faceTrackPath.isEmpty()) {
        const std::shared_ptr<const drift::FaceTrack> track =
            drift::loadFaceTrackCached(clip.faceTrackPath);
        if (track && !track->isEmpty() && track->fps > 0) {
            const int total = track->frames.size();
            const int cap = 120;
            const int step = qMax(1, (total + cap - 1) / cap);
            for (int i = 0; i < total; i += step) {
                Sample s;
                s.t = double(i) / double(track->fps);
                for (const drift::FaceAnchors &face : track->frames.at(i).faces) {
                    if (!face.valid)
                        continue;
                    s.cx = face.faceCenter.x();
                    s.cy = face.faceCenter.y();
                    s.rx = qMax(0.05, face.faceRx);
                    s.ry = qMax(0.05, face.faceRy);
                    break;
                }
                samples.append(s);
            }
        }
    }
    if (samples.isEmpty()) {
        Sample s;
        s.t = 0.0;
        samples.append(s);
        Sample e;
        e.t = drift::usToSeconds(clip.timelineDuration);
        samples.append(e);
    }

    const int radius = (m == QLatin1String("motion")) ? 4 : 2;
    QList<Sample> smoothed = samples;
    for (int i = 0; i < samples.size(); ++i) {
        double cx = 0, cy = 0, n = 0;
        for (int j = qMax(0, i - radius); j <= qMin(samples.size() - 1, i + radius); ++j) {
            cx += samples.at(j).cx;
            cy += samples.at(j).cy;
            n += 1;
        }
        smoothed[i].cx = cx / n;
        smoothed[i].cy = cy / n;
    }

    // The crop window is in normalised source coordinates, where the two axes have different
    // lengths in pixels. Relating them by the target aspect alone — as this did — treats them as
    // square, which both distorts the picture and zooms far past what was asked for: on a 4K 16:9
    // source a 9:16 request came out as a square box and about a 3x upscale. Dividing the source's
    // own display aspect out is what makes the window the shape the caller asked for.
    double sourceAspect = canvasW / canvasH;
    if (const drift::MediaAsset *asset = m_app->m_project.asset(clip.assetId)) {
        if (asset->width > 0 && asset->height > 0)
            sourceAspect = double(asset->width) / double(asset->height);
    }

    // How much of the source ends up across the canvas, at the tightest point. Above 1.0 the crop
    // is being blown up past its own resolution, which no amount of framing can make sharp.
    double tightestScale = 0.0;

    // Where the target-aspect window lands on the canvas. When the canvas already is that aspect
    // this is the whole canvas and the crop fills it. When it is not — a 9:16 crop asked for on a
    // 16:9 timeline, which is the documented use — the window is fitted inside instead. Filling the
    // canvas in that case would mean stretching the picture, and a reframe that distorts the
    // subject has not reframed anything.
    const double frameH = qMin(canvasH, canvasW / targetAspect);
    const double frameW = frameH * targetAspect;
    const double frameX = (canvasW - frameW) * 0.5;
    const double frameY = (canvasH - frameH) * 0.5;

    beginBatch();
    int keys = 0;
    for (const Sample &s : smoothed) {
        // Crop window of targetAspect centred on the face, in normalised source coords.
        double cropH = qMin(1.0, qMax(s.ry * 2.4, 0.35));
        double cropW = cropH * targetAspect / sourceAspect;
        if (cropW > 1.0) {
            cropW = 1.0;
            cropH = qMin(1.0, cropW * sourceAspect / targetAspect);
        }
        double cropX = qBound(0.0, s.cx - cropW * 0.5, 1.0 - cropW);
        double cropY = qBound(0.0, s.cy - cropH * 0.5, 1.0 - cropH);
        // The whole source, scaled so that its crop window covers exactly the framed area. w/h
        // comes out at the source's own aspect, so nothing is stretched.
        const double w = frameW / cropW;
        const double h = frameH / cropH;
        const double x = frameX - cropX * w;
        const double y = frameY - cropY * h;
        if (const drift::MediaAsset *asset = m_app->m_project.asset(clip.assetId)) {
            if (asset->width > 0)
                tightestScale = qMax(tightestScale, frameW / (cropW * asset->width));
        }
        const double at = drift::usToSeconds(clip.timelineStart) + s.t;
        m_app->setClipKeyframe(trackIndex, clipIndex, QStringLiteral("x"), at, x);
        m_app->setClipKeyframe(trackIndex, clipIndex, QStringLiteral("y"), at, y);
        m_app->setClipKeyframe(trackIndex, clipIndex, QStringLiteral("width"), at, w);
        m_app->setClipKeyframe(trackIndex, clipIndex, QStringLiteral("height"), at, h);
        keys += 4;
    }
    endBatch(QStringLiteral("Auto-reframe"), keys > 0);
    QJsonObject reply{{QStringLiteral("keys"), keys},
                      {QStringLiteral("aspect"), round3(targetAspect)},
                      {QStringLiteral("mode"), m.isEmpty() ? QStringLiteral("face") : m}};
    if (tightestScale > 0.0) {
        // Say how hard the crop is pushing the source, so a caller can see an upscale here rather
        // than discover it as a soft picture in a render.
        reply.insert(QStringLiteral("scale"), round3(tightestScale));
        if (tightestScale > 1.02)
            reply.insert(QStringLiteral("upscaled"), true);
    }
    return ok(reply);
}

QJsonObject McpController::listAddons() const
{
    using namespace drift::mcp;
    QJsonArray addons;
    QSet<QString> seen;
    if (m_app->m_addonManager) {
        for (const QVariant &v : m_app->m_addonManager->catalog()) {
            const QVariantMap row = v.toMap();
            const QString id = row.value(QStringLiteral("id")).toString();
            if (id.isEmpty())
                continue;
            seen.insert(id);
            const QString state = row.value(QStringLiteral("state")).toString();
            addons.append(QJsonObject{
                {QStringLiteral("id"), id},
                {QStringLiteral("name"), row.value(QStringLiteral("name")).toString()},
                {QStringLiteral("kind"), row.value(QStringLiteral("kind")).toString()},
                {QStringLiteral("version"), row.value(QStringLiteral("version")).toString()},
                {QStringLiteral("state"), state},
                {QStringLiteral("installed"),
                 state == QLatin1String("installed") || state == QLatin1String("update-available")},
            });
        }
    }
    for (const drift::addon::InstalledAddon &inst : drift::addon::installedAddons()) {
        if (seen.contains(inst.id))
            continue;
        addons.append(QJsonObject{
            {QStringLiteral("id"), inst.id},
            {QStringLiteral("name"), inst.name},
            {QStringLiteral("version"), inst.version},
            {QStringLiteral("state"), QStringLiteral("installed")},
            {QStringLiteral("installed"), true},
        });
    }
    return ok({{QStringLiteral("addons"), addons}});
}

QJsonObject McpController::installAddon(const QString &id)
{
    using namespace drift::mcp;
    if (!m_app->m_addonManager)
        return err("bad_args", QStringLiteral("Addon manager is only available in the running editor"));
    if (id.trimmed().isEmpty())
        return err("bad_args", QStringLiteral("id required"));
    m_app->m_addonManager->install(id);
    return ok({{QStringLiteral("started"), true}, {QStringLiteral("id"), id}});
}

QJsonObject McpController::cancelAddonInstall(const QString &id)
{
    using namespace drift::mcp;
    if (!m_app->m_addonManager)
        return err("bad_args", QStringLiteral("Addon manager is only available in the running editor"));
    m_app->m_addonManager->cancel(id);
    return ok({{QStringLiteral("cancelled"), true}, {QStringLiteral("id"), id}});
}

QJsonObject McpController::setAcceleration(const QString &variant)
{
    using namespace drift::mcp;
    if (!m_app->m_addonManager)
        return err("bad_args", QStringLiteral("Addon manager is only available in the running editor"));
    if (variant.trimmed().isEmpty())
        return err("bad_args", QStringLiteral("variant required"));
    m_app->m_addonManager->setAcceleration(variant);
    return ok({{QStringLiteral("variant"), m_app->m_addonManager->acceleration()}});
}

// ---------------------------------------------------------------------------------------------
// Transcripts

namespace {

QString speakerLabel(const drift::Transcript &t, int speaker)
{
    if (speaker < 0)
        return {};
    if (speaker < t.speakers.size() && !t.speakers.at(speaker).id.isEmpty())
        return t.speakers.at(speaker).id;
    return QStringLiteral("S%1").arg(speaker + 1);
}

QString clockLabel(double seconds)
{
    return QString::number(seconds, 'f', 2);
}

} // namespace

void McpController::storeTranscript(const QString &assetId, std::shared_ptr<drift::Transcript> transcript)
{
    const drift::MediaAsset *asset = m_app->m_project.asset(assetId);
    if (!asset || !transcript)
        return;
    if (!transcript->source.isEmpty() && !transcript->source.matches(asset->path))
        return;
    m_app->m_project.setTranscript(assetId, std::move(transcript));
    bumpEditRevision();
    m_app->projectFile()->setDirty(true);
}

QJsonObject McpController::transcribe(const QStringList &assetIds, const QJsonObject &options)
{
    using namespace drift::mcp;
    if (assetIds.isEmpty())
        return err("bad_args", QStringLiteral("asset, clip or clips required"));
    const QString engine = options.value(QStringLiteral("engine")).toString(QStringLiteral("local"));
    if (engine != QLatin1String("local") && engine != QLatin1String("elevenlabs"))
        return err("bad_args", QStringLiteral("engine must be local or elevenlabs"));
    const bool force = options.value(QStringLiteral("force")).toBool();

    drift::LocalTranscribeOptions local;
    local.language = options.value(QStringLiteral("language")).toString().trimmed().toLower();
    local.align = options.value(QStringLiteral("align")).toBool(true);
    local.diarize = options.value(QStringLiteral("diarize")).toBool(false);
    local.numSpeakers = options.value(QStringLiteral("num_speakers")).toInt(-1);

    QJsonArray started;
    QJsonArray cached;
    const QPointer<McpController> self(this);
    for (const QString &assetId : assetIds) {
        const drift::MediaAsset *asset = m_app->m_project.asset(assetId);
        if (!asset)
            return err("not_found", QStringLiteral("Unknown asset %1").arg(assetId));
        if (asset->path.isEmpty() || !asset->sequenceId.isEmpty()
            || (asset->kind != drift::MediaKind::Audio && asset->kind != drift::MediaKind::Video)
            || (asset->hasAudioKnown && !asset->hasAudio))
            return err("type_mismatch", QStringLiteral("%1 has no audio to transcribe").arg(asset->name));
        const drift::TranscriptPtr existing = m_app->m_project.transcript(assetId);
        if (!force && existing && existing->source.matches(asset->path)) {
            cached.append(assetId);
            continue;
        }
        if (m_app->m_jobs->hasActive(QStringLiteral("transcribe"), assetId))
            return err("busy", QStringLiteral("%1 is already being transcribed").arg(asset->name));
        if (engine == QLatin1String("local") && !drift::WhisperTranscriber::modelPresent())
            return err("addon_missing", QStringLiteral("Local transcription needs the whisper-model addon: "
                                                       "list_addons, then install_addon."));

        const QString path = asset->path;
        const drift::SourceFingerprint fingerprint = drift::SourceFingerprint::of(path);
        auto holder = std::make_shared<std::shared_ptr<drift::Transcript>>();
        if (engine == QLatin1String("elevenlabs")) {
            const QJsonObject unavailable = cloudUnavailable(QString::fromLatin1(CloudProviders::kElevenLabs));
            if (!unavailable.isEmpty())
                return unavailable;
            const QString key = m_app->m_cloud->apiKey(QString::fromLatin1(CloudProviders::kElevenLabs));
            const QString model = m_app->m_cloud->setting(QString::fromLatin1(CloudProviders::kElevenLabs), QStringLiteral("stt_model"));
            QStringList keyterms;
            for (const QJsonValue &v : options.value(QStringLiteral("keyterms")).toArray())
                keyterms.append(v.toString());
            const QString id = m_app->m_jobs->start(
                QStringLiteral("transcribe"), assetId, JobRegistry::Lane::Network,
                [path, key, model, local, keyterms, fingerprint, holder](JobContext &ctx) {
                    bool cancelled = false;
                    const std::vector<float> pcm = drift::readMono16k(
                        path, 0, -1,
                        [&ctx](double f) {
                            ctx.progress(0.1 * f, QStringLiteral("Reading audio…"));
                            return !ctx.cancelled();
                        },
                        &cancelled);
                    if (cancelled)
                        return;
                    if (pcm.empty())
                        return ctx.fail(QStringLiteral("no_audio"), QStringLiteral("No audio decoded"));
                    QString error;
                    const QString flac = drift::writeTempFlac16k(pcm, &error);
                    if (flac.isEmpty())
                        return ctx.fail(QStringLiteral("internal"), error);
                    ctx.progress(0.15, QStringLiteral("Uploading to ElevenLabs…"));
                    drift::cloud::CloudError cloudError;
                    auto transcript = drift::cloud::elevenlabs::speechToText(
                        key, model, flac, local.language, local.diarize, local.numSpeakers, keyterms,
                        [&ctx] { return ctx.cancelled(); },
                        [&ctx](double f) {
                            ctx.progress(0.15 + 0.5 * f, f < 1.0 ? QStringLiteral("Uploading to ElevenLabs…")
                                                                 : QStringLiteral("ElevenLabs is transcribing…"));
                        },
                        &cloudError);
                    QFile::remove(flac);
                    if (cloudError.isError())
                        return ctx.fail(cloudError.code, cloudError.message);
                    transcript->source = fingerprint;
                    *holder = transcript;
                    ctx.succeed({{QStringLiteral("words"), transcript->speechTokenCount()},
                                 {QStringLiteral("engine"), transcript->engine},
                                 {QStringLiteral("language"), transcript->language},
                                 {QStringLiteral("aligned"), true},
                                 {QStringLiteral("diarized"), transcript->diarized}});
                },
                [self, assetId, holder](const QJsonObject &job) {
                    if (!self)
                        return QJsonObject{};
                    if (job.value(QStringLiteral("ok")).toBool())
                        self->storeTranscript(assetId, *holder);
                    return QJsonObject{};
                });
            started.append(QJsonObject{{QStringLiteral("asset"), assetId}, {QStringLiteral("job_id"), id}});
            continue;
        }
        const QString id = m_app->m_jobs->start(
            QStringLiteral("transcribe"), assetId, JobRegistry::Lane::Model,
            [path, local, fingerprint, holder](JobContext &ctx) {
                bool cancelled = false;
                const std::vector<float> pcm = drift::readMono16k(
                    path, 0, -1,
                    [&ctx](double f) {
                        ctx.progress(0.05 * f, QStringLiteral("Reading audio…"));
                        return !ctx.cancelled();
                    },
                    &cancelled);
                if (cancelled)
                    return;
                if (pcm.empty())
                    return ctx.fail(QStringLiteral("no_audio"), QStringLiteral("No audio decoded"));
                const drift::LocalTranscribeResult r = drift::transcribeLocal(
                    pcm, 0, local, [&ctx](double f, const QString &status) {
                        ctx.progress(0.05 + 0.95 * f, status);
                        return !ctx.cancelled();
                    });
                if (r.cancelled)
                    return;
                if (!r.transcript)
                    return ctx.fail(QStringLiteral("model_error"), r.error);
                r.transcript->source = fingerprint;
                *holder = r.transcript;
                ctx.succeed({{QStringLiteral("words"), r.transcript->speechTokenCount()},
                             {QStringLiteral("engine"), r.transcript->engine},
                             {QStringLiteral("language"), r.transcript->language},
                             {QStringLiteral("aligned"), r.transcript->wordTimingsAligned}});
            },
            [self, assetId, holder](const QJsonObject &job) {
                if (!self)
                    return QJsonObject{};
                if (job.value(QStringLiteral("ok")).toBool())
                    self->storeTranscript(assetId, *holder);
                return QJsonObject{};
            });
        started.append(QJsonObject{{QStringLiteral("asset"), assetId}, {QStringLiteral("job_id"), id}});
    }
    return ok({{QStringLiteral("jobs"), started},
               {QStringLiteral("cached"), cached},
               {QStringLiteral("hint"), started.isEmpty()
                                            ? QStringLiteral("All cached; read with get_transcript.")
                                            : QStringLiteral("Poll get_job({id}) until active is false, "
                                                             "then get_transcript.")}});
}

QJsonObject McpController::getTranscript(const QString &assetId, int trackIndex, int clipIndex,
                                            double startSeconds, double endSeconds,
                                            const QJsonObject &options) const
{
    using namespace drift::mcp;
    const QString view = options.value(QStringLiteral("view")).toString(QStringLiteral("phrases"));
    const double breakOnSilence = options.value(QStringLiteral("break_on_silence")).toDouble(0.7);
    const int maxWords = options.value(QStringLiteral("max_words")).toInt(0);
    const int offset = qMax(0, options.value(QStringLiteral("offset")).toInt(0));
    const int limit = qBound(1, options.value(QStringLiteral("limit")).toInt(view == QLatin1String("words") ? 400 : 200), 2000);
    QStringList include;
    for (const QJsonValue &v : options.value(QStringLiteral("include")).toArray())
        include.append(v.toString());
    const bool withFillers = include.contains(QStringLiteral("fillers"));
    const bool withEvents = include.contains(QStringLiteral("events"));

    // A transcript to read, and the word index each of its entries came from. Asset mode reads
    // the asset's own transcript in source time; clip and range modes rebuild it on the timeline.
    drift::Transcript viewT;
    QList<int> sourceIndex;
    QList<QString> sourceAsset;
    QString mode;
    drift::TranscriptPtr primary;

    auto collect = [&](drift::TimeUs viewStart, drift::TimeUs viewEnd, const QString &onlyClipId) {
        for (const drift::TimelineWord &tw : drift::transcriptWordsOnTimeline(m_app->m_project, viewStart, viewEnd, onlyClipId)) {
            if (!primary)
                primary = m_app->m_project.transcript(tw.assetId);
            viewT.words.append(tw.word);
            sourceIndex.append(tw.index);
            sourceAsset.append(tw.assetId);
        }
    };

    if (!assetId.isEmpty()) {
        primary = m_app->m_project.transcript(assetId);
        if (!primary)
            return err("not_found", QStringLiteral("No transcript for this asset; run transcribe first"));
        mode = QStringLiteral("source");
        viewT = *primary;
        for (int i = 0; i < primary->words.size(); ++i) {
            sourceIndex.append(i);
            sourceAsset.append(assetId);
        }
    } else if (trackIndex >= 0 && clipIndex >= 0) {
        if (!m_app->isValidClipIndex(trackIndex, clipIndex))
            return err("not_found", QStringLiteral("Unknown clip"));
        const drift::Clip &clip = m_app->m_project.tracks().at(trackIndex).clips.at(clipIndex);
        primary = m_app->m_project.transcript(clip.assetId);
        if (!primary)
            return err("not_found", QStringLiteral("No transcript for this clip's media; run transcribe first"));
        mode = QStringLiteral("timeline");
        collect(clip.timelineStart, clip.timelineEnd(), clip.id);
    } else {
        if (endSeconds <= startSeconds)
            return err("bad_args", QStringLiteral("asset, clip, or start+end required"));
        mode = QStringLiteral("timeline");
        collect(drift::secondsToUs(startSeconds), drift::secondsToUs(endSeconds), {});
        if (!primary)
            return err("not_found", QStringLiteral("No transcribed media in that range"));
    }
    if (mode == QLatin1String("timeline"))
        viewT.speakers = primary->speakers;

    const drift::MediaAsset *primaryAsset = nullptr;
    for (auto it = m_app->m_project.assets().cbegin(); it != m_app->m_project.assets().cend(); ++it) {
        if (m_app->m_project.transcript(it.key()) == primary)
            primaryAsset = &it.value();
    }
    QJsonArray speakers;
    for (int sp = 0; sp < primary->speakers.size(); ++sp)
        speakers.append(QJsonObject{{QStringLiteral("id"), speakerLabel(*primary, sp)},
                                    {QStringLiteral("label"), primary->speakers.at(sp).label}});
    QJsonObject meta{
        {QStringLiteral("time"), mode},
        {QStringLiteral("engine"), primary->engine},
        {QStringLiteral("language"), primary->language},
        {QStringLiteral("aligned"), primary->wordTimingsAligned},
        {QStringLiteral("diarized"), primary->diarized},
        {QStringLiteral("stale"), primaryAsset ? !primary->source.matches(primaryAsset->path) : false},
        {QStringLiteral("speakers"), speakers},
    };

    const bool multiAsset = QSet<QString>(sourceAsset.cbegin(), sourceAsset.cend()).size() > 1;
    if (view == QLatin1String("words")) {
        QJsonArray words;
        int total = 0;
        for (int i = 0; i < viewT.words.size(); ++i) {
            const drift::TranscriptWord &w = viewT.words.at(i);
            if (w.type == drift::TranscriptTokenType::Spacing)
                continue;
            if ((w.type == drift::TranscriptTokenType::Filler && !withFillers && !include.isEmpty())
                || (w.type == drift::TranscriptTokenType::AudioEvent && !withEvents && !include.isEmpty()))
                continue;
            if (total++ < offset || words.size() >= limit)
                continue;
            QJsonObject o{{QStringLiteral("i"), sourceIndex.at(i)},
                          {QStringLiteral("start"), drift::usToSeconds(w.startUs)},
                          {QStringLiteral("end"), drift::usToSeconds(w.endUs)},
                          {QStringLiteral("text"), w.text}};
            if (w.type != drift::TranscriptTokenType::Word)
                o.insert(QStringLiteral("type"), drift::transcriptTokenTypeToString(w.type));
            if (w.speaker >= 0)
                o.insert(QStringLiteral("speaker"), speakerLabel(*primary, w.speaker));
            if (!std::isnan(w.confidence))
                o.insert(QStringLiteral("conf"), std::round(w.confidence * 100.0) / 100.0);
            if (w.interpolated)
                o.insert(QStringLiteral("estimated"), true);
            if (multiAsset)
                o.insert(QStringLiteral("asset"), sourceAsset.at(i));
            words.append(o);
        }
        meta.insert(QStringLiteral("words"), words);
        meta.insert(QStringLiteral("total"), total);
        if (offset + words.size() < total)
            meta.insert(QStringLiteral("next_offset"), offset + words.size());
        return ok(meta);
    }

    // Phrases break on silence or speaker change; fillers are shown (as they are editorial signal)
    // unless the caller narrowed `include`.
    const bool phraseFillers = include.isEmpty() || withFillers;
    const bool phraseEvents = include.isEmpty() || withEvents;
    const QList<drift::TranscriptPhrase> phrases = drift::packTranscriptPhrases(
        viewT, std::numeric_limits<drift::TimeUs>::min() / 2, std::numeric_limits<drift::TimeUs>::max() / 2,
        drift::secondsToUs(qMax(0.05, breakOnSilence)), maxWords, phraseFillers, phraseEvents);

    if (view == QLatin1String("text")) {
        QStringList parts;
        for (const drift::TranscriptPhrase &p : phrases)
            parts.append(p.text);
        meta.insert(QStringLiteral("text"), parts.join(QLatin1Char(' ')));
        return ok(meta);
    }

    QJsonArray out;
    QStringList compact;
    const int end = qMin(phrases.size(), offset + limit);
    for (int i = offset; i < end; ++i) {
        const drift::TranscriptPhrase &p = phrases.at(i);
        const QString who = speakerLabel(*primary, p.speaker);
        QJsonObject o{{QStringLiteral("start"), drift::usToSeconds(p.startUs)},
                      {QStringLiteral("end"), drift::usToSeconds(p.endUs)},
                      {QStringLiteral("text"), p.text},
                      {QStringLiteral("words"), QJsonArray{sourceIndex.at(p.firstWord), sourceIndex.at(p.lastWord)}}};
        if (!who.isEmpty())
            o.insert(QStringLiteral("speaker"), who);
        if (multiAsset)
            o.insert(QStringLiteral("asset"), sourceAsset.at(p.firstWord));
        out.append(o);
        compact.append(QStringLiteral("[%1-%2]%3 %4")
                           .arg(clockLabel(drift::usToSeconds(p.startUs)), clockLabel(drift::usToSeconds(p.endUs)),
                                who.isEmpty() ? QString() : QLatin1Char(' ') + who, p.text));
    }
    meta.insert(QStringLiteral("phrases"), out);
    meta.insert(QStringLiteral("compact"), compact.join(QLatin1Char('\n')));
    meta.insert(QStringLiteral("total"), phrases.size());
    if (end < phrases.size())
        meta.insert(QStringLiteral("next_offset"), end);
    return ok(meta);
}

QJsonObject McpController::diarize(const QString &assetId, const QJsonObject &options)
{
    using namespace drift::mcp;
    const drift::MediaAsset *asset = m_app->m_project.asset(assetId);
    if (!asset)
        return err("not_found", QStringLiteral("Unknown asset"));
    if (asset->path.isEmpty() || (asset->hasAudioKnown && !asset->hasAudio))
        return err("type_mismatch", QStringLiteral("%1 has no audio").arg(asset->name));
    if (!drift::SpeakerDiarizer::modelPresent())
        return err("addon_missing", QStringLiteral("Speaker labels need the diarize-model addon: "
                                                   "list_addons, then install_addon."));
    drift::DiarizeParams params;
    params.numSpeakers = options.value(QStringLiteral("num_speakers")).toInt(-1);
    params.threshold = static_cast<float>(options.value(QStringLiteral("threshold")).toDouble(0.5));
    params.minDurationOn = static_cast<float>(options.value(QStringLiteral("min_on")).toDouble(0.3));
    params.minDurationOff = static_cast<float>(options.value(QStringLiteral("min_off")).toDouble(0.5));

    const QString path = asset->path;
    auto turns = std::make_shared<QList<drift::DiarizeSegment>>();
    const QPointer<McpController> self(this);
    const QString id = m_app->m_jobs->start(
        QStringLiteral("diarize"), assetId, JobRegistry::Lane::Model,
        [path, params, turns](JobContext &ctx) {
            bool cancelled = false;
            const std::vector<float> pcm = drift::readMono16k(
                path, 0, -1,
                [&ctx](double f) {
                    ctx.progress(0.1 * f, QStringLiteral("Reading audio…"));
                    return !ctx.cancelled();
                },
                &cancelled);
            if (cancelled)
                return;
            if (pcm.empty())
                return ctx.fail(QStringLiteral("no_audio"), QStringLiteral("No audio decoded"));
            drift::SpeakerDiarizer &diarizer = drift::SpeakerDiarizer::instance();
            if (!diarizer.available())
                return ctx.fail(QStringLiteral("model_error"), diarizer.lastError());
            *turns = diarizer.diarize(
                pcm, params,
                [&ctx](double f) {
                    ctx.progress(0.1 + 0.9 * f, QStringLiteral("Telling speakers apart…"));
                    return !ctx.cancelled();
                },
                &cancelled);
            if (cancelled)
                return;
            QJsonArray segments;
            int speakers = 0;
            for (const drift::DiarizeSegment &t : std::as_const(*turns)) {
                segments.append(QJsonObject{{QStringLiteral("start"), drift::usToSeconds(t.startUs)},
                                            {QStringLiteral("end"), drift::usToSeconds(t.endUs)},
                                            {QStringLiteral("speaker"), QStringLiteral("S%1").arg(t.speaker + 1)}});
                speakers = qMax(speakers, t.speaker + 1);
            }
            ctx.succeed({{QStringLiteral("speakers"), speakers}, {QStringLiteral("segments"), segments}});
        },
        [self, assetId, turns](const QJsonObject &job) {
            if (!self)
                return QJsonObject{};
            if (!job.value(QStringLiteral("ok")).toBool())
                return QJsonObject{};
            // Label an existing transcript's words; the turns themselves are in the job result.
            if (const drift::TranscriptPtr t = self->m_app->m_project.transcript(assetId)) {
                auto labelled = std::make_shared<drift::Transcript>(*t);
                drift::assignSpeakers(*labelled, *turns);
                self->storeTranscript(assetId, labelled);
                return QJsonObject{{QStringLiteral("transcript_labelled"), true}};
            }
            return QJsonObject{};
        });
    return ok({{QStringLiteral("job_id"), id}});
}

QJsonObject McpController::cloudUnavailable(const QString &provider) const
{
    using namespace drift::mcp;
    const QString name = provider == QLatin1String(CloudProviders::kElevenLabs) ? QStringLiteral("ElevenLabs")
                                                                                : QStringLiteral("Fish Audio");
    if (!m_app->m_cloud->configured(provider))
        return err("not_configured", QStringLiteral("%1 has no API key. Ask the user to add one in Settings → "
                                                    "Cloud providers (or set %2).")
                                         .arg(name, provider == QLatin1String(CloudProviders::kElevenLabs)
                                                        ? QStringLiteral("ELEVENLABS_API_KEY")
                                                        : QStringLiteral("FISH_API_KEY")));
    if (!m_app->m_cloud->consent(provider))
        return err("consent_required", QStringLiteral("The user hasn't allowed Drift to send audio or text to %1 "
                                                      "yet. Ask them to allow it in Settings → Cloud providers.")
                                           .arg(name));
    return {};
}

QJsonObject McpController::importGeneratedAudio(const QString &path, const QJsonObject &generator,
                                                const QJsonValue &place)
{
    using namespace drift::mcp;
    const bool doPlace = !(place.isUndefined() || place.isNull() || (place.isBool() && !place.toBool()));
    const QJsonObject where = place.toObject();
    const double at = where.contains(QStringLiteral("at")) ? where.value(QStringLiteral("at")).toDouble() : m_app->playheadSeconds();
    const int track = where.contains(QStringLiteral("track")) ? where.value(QStringLiteral("track")).toInt() : -1;
    const AppController::GeneratedAudioImport imported = m_app->importGeneratedAudio(path, generator, doPlace, at, track);
    if (!imported.ok)
        return err(imported.errorCode.toUtf8().constData(), imported.error);
    QJsonObject result{{QStringLiteral("asset"), imported.assetId}, {QStringLiteral("path"), path}};
    if (imported.durationSeconds >= 0.0)
        result.insert(QStringLiteral("duration"), imported.durationSeconds);
    if (!imported.clipId.isEmpty())
        result.insert(QStringLiteral("clip"), imported.clipId);
    return ok(result);
}

namespace {

QString slugFor(const QString &text)
{
    QString slug;
    for (const QChar c : text.toLower()) {
        if (c.isLetterOrNumber())
            slug.append(c);
        else if (!slug.isEmpty() && !slug.endsWith(QLatin1Char('-')))
            slug.append(QLatin1Char('-'));
        if (slug.size() >= 24)
            break;
    }
    while (slug.endsWith(QLatin1Char('-')))
        slug.chop(1);
    return slug.isEmpty() ? QStringLiteral("audio") : slug;
}

QString generatedPath(const QString &kind, const QString &provider, const QString &text)
{
    return QDir(drift::generatedAudioDir())
        .filePath(QStringLiteral("%1-%2-%3-%4.mp3")
                      .arg(kind, provider, slugFor(text), QUuid::createUuid().toString(QUuid::Id128).left(8)));
}

} // namespace

QJsonObject McpController::ttsGenerate(const QJsonObject &args)
{
    using namespace drift::mcp;
    const QString provider = args.value(QStringLiteral("provider")).toString(QString::fromLatin1(CloudProviders::kElevenLabs));
    if (provider == QLatin1String("local"))
        return ttsGenerateLocal(args);
    if (provider != QLatin1String(CloudProviders::kElevenLabs) && provider != QLatin1String(CloudProviders::kFish))
        return err("bad_args", QStringLiteral("provider must be elevenlabs, fish or local"));
    const QString text = args.value(QStringLiteral("text")).toString().trimmed();
    if (text.isEmpty())
        return err("bad_args", QStringLiteral("text required"));
    if (text.size() > 5000)
        return err("bad_args", QStringLiteral("text is over 5000 characters; split it into several calls"));
    const QJsonObject unavailable = cloudUnavailable(provider);
    if (!unavailable.isEmpty())
        return unavailable;
    QString voice = args.value(QStringLiteral("voice")).toString().trimmed();
    if (voice.isEmpty())
        voice = m_app->m_cloud->setting(provider, QStringLiteral("voice"));
    if (voice.isEmpty() && provider == QLatin1String(CloudProviders::kElevenLabs))
        return err("bad_args", QStringLiteral("voice required (list_voices), or set a default voice in Settings"));
    QString model = args.value(QStringLiteral("model")).toString().trimmed();
    if (model.isEmpty())
        model = m_app->m_cloud->setting(provider, QStringLiteral("tts_model"));
    const QString key = m_app->m_cloud->apiKey(provider);
    const double speed = args.value(QStringLiteral("speed")).toDouble(1.0);
    const QString language = args.value(QStringLiteral("language")).toString();
    QJsonObject voiceSettings;
    for (const char *k : {"stability", "similarity_boost", "style"}) {
        if (args.contains(QLatin1String(k)))
            voiceSettings.insert(QLatin1String(k), args.value(QLatin1String(k)).toDouble());
    }
    if (provider == QLatin1String(CloudProviders::kElevenLabs) && !qFuzzyCompare(speed, 1.0))
        voiceSettings.insert(QStringLiteral("speed"), qBound(0.7, speed, 1.2));
    const QString path = generatedPath(QStringLiteral("tts"), provider, text);
    const QJsonObject generator{{QStringLiteral("provider"), provider}, {QStringLiteral("kind"), QStringLiteral("tts")},
                                {QStringLiteral("voice"), voice},     {QStringLiteral("model"), model},
                                {QStringLiteral("text"), text}};
    const QJsonValue place = args.value(QStringLiteral("place"));
    const QPointer<McpController> self(this);

    const QString id = m_app->m_jobs->start(
        QStringLiteral("tts"), provider, JobRegistry::Lane::Network,
        [provider, key, voice, model, text, voiceSettings, language, speed, path](JobContext &ctx) {
            ctx.progress(0.1, QStringLiteral("Generating voice…"));
            drift::cloud::CloudError error;
            const auto cancel = [&ctx] { return ctx.cancelled(); };
            const QByteArray audio = provider == QLatin1String(CloudProviders::kElevenLabs)
                                         ? drift::cloud::elevenlabs::textToSpeech(key, voice, model, text, voiceSettings,
                                                                                  language, cancel, &error)
                                         : drift::cloud::fish::textToSpeech(key, model, voice, text, speed, cancel, &error);
            if (error.isError())
                return ctx.fail(error.code, error.message);
            QFile file(path);
            if (audio.isEmpty() || !file.open(QIODevice::WriteOnly) || file.write(audio) != audio.size())
                return ctx.fail(QStringLiteral("internal"), QStringLiteral("Could not write %1").arg(path));
            ctx.succeed({{QStringLiteral("chars"), text.size()}});
        },
        [self, path, generator, place](const QJsonObject &job) {
            if (!self)
                return QJsonObject{};
            if (!job.value(QStringLiteral("ok")).toBool())
                return QJsonObject{};
            QJsonObject imported = self->importGeneratedAudio(path, generator, place);
            imported.remove(QStringLiteral("ok"));
            return imported;
        });
    return ok({{QStringLiteral("job_id"), id}});
}

QJsonObject McpController::localTtsUnavailable() const
{
    using namespace drift::mcp;
    if (!m_app->m_tts->available())
        return err("addon_missing", QStringLiteral("Local voices need the tts-model addon and ONNX Runtime: "
                                                   "list_addons, then install_addon."));
    if (m_app->m_tts->running())
        return err("busy", QStringLiteral("A voiceover is already being generated; poll get_job or cancel_job it first"));
    return {};
}

QJsonObject McpController::ttsGenerateLocal(const QJsonObject &args)
{
    using namespace drift::mcp;
    TtsController *tts = m_app->m_tts;
    const QString text = args.value(QStringLiteral("text")).toString().trimmed();
    if (text.isEmpty())
        return err("bad_args", QStringLiteral("text required"));
    if (text.size() > 5000)
        return err("bad_args", QStringLiteral("text is over 5000 characters; split it into several calls"));
    QString voice = args.value(QStringLiteral("voice")).toString().trimmed();
    if (voice.isEmpty())
        voice = tts->lastVoice();
    if (m_app->m_voices->indexOfId(voice) < 0)
        return err("not_found", QStringLiteral("Unknown voice %1 (list_local_voices)").arg(voice));
    QString language = args.value(QStringLiteral("language")).toString().trimmed();
    if (language.isEmpty())
        language = tts->lastLanguage();
    if (!drift::ChatterboxTts::supportedLanguages().contains(language))
        return err("bad_args", QStringLiteral("Unsupported language %1 (local_tts_status lists them)").arg(language));
    const double exaggeration = args.contains(QStringLiteral("exaggeration"))
                                    ? args.value(QStringLiteral("exaggeration")).toDouble()
                                    : tts->lastExaggeration();
    if (exaggeration < 0.25 || exaggeration > 2.0)
        return err("bad_args", QStringLiteral("exaggeration must be 0.25-2"));
    const QJsonValue place = args.value(QStringLiteral("place"));
    const bool doPlace = !(place.isUndefined() || place.isNull() || (place.isBool() && !place.toBool()));
    const QJsonObject where = place.toObject();
    const double at = where.contains(QStringLiteral("at")) ? where.value(QStringLiteral("at")).toDouble() : -1.0;
    const int track = where.contains(QStringLiteral("track")) ? where.value(QStringLiteral("track")).toInt() : -1;
    const QJsonObject unavailable = localTtsUnavailable();
    if (!unavailable.isEmpty())
        return unavailable;
    const QString id = tts->generate(text, voice, language, exaggeration, doPlace, at, track);
    if (id.isEmpty())
        return err("internal", QStringLiteral("Could not start the voiceover"));
    return ok({{QStringLiteral("job_id"), id}});
}

QJsonObject McpController::localTtsStatus(bool warmUp)
{
    using namespace drift::mcp;
    TtsController *tts = m_app->m_tts;
    if (warmUp)
        tts->warmUp();
    QJsonArray languages;
    for (const QVariant &v : tts->languageOptions()) {
        const QVariantMap m = v.toMap();
        languages.append(QJsonObject{{QStringLiteral("code"), m.value(QStringLiteral("code")).toString()},
                                     {QStringLiteral("label"), m.value(QStringLiteral("label")).toString()}});
    }
    return ok({{QStringLiteral("available"), tts->available()},
               {QStringLiteral("model_loaded"), tts->modelLoaded()},
               {QStringLiteral("loading"), tts->loading()},
               {QStringLiteral("running"), tts->running()},
               {QStringLiteral("addon_id"), QStringLiteral("tts-model")},
               {QStringLiteral("languages"), languages},
               {QStringLiteral("last_voice"), tts->lastVoice()},
               {QStringLiteral("last_language"), tts->lastLanguage()},
               {QStringLiteral("last_exaggeration"), tts->lastExaggeration()}});
}

QJsonObject McpController::listLocalVoices() const
{
    using namespace drift::mcp;
    const VoiceLibrary *voices = m_app->m_voices;
    QJsonArray list;
    for (int i = 0; i < voices->rowCount(); ++i) {
        const QModelIndex row = voices->index(i);
        list.append(QJsonObject{{QStringLiteral("id"), voices->data(row, VoiceLibrary::IdRole).toString()},
                                {QStringLiteral("name"), voices->data(row, VoiceLibrary::NameRole).toString()},
                                {QStringLiteral("built_in"), voices->data(row, VoiceLibrary::BuiltInRole).toBool()},
                                {QStringLiteral("source"), voices->data(row, VoiceLibrary::SourceRole).toString()}});
    }
    return ok({{QStringLiteral("voices"), list}});
}

QJsonObject McpController::addVoice(const QJsonObject &args, int trackIndex, int clipIndex)
{
    using namespace drift::mcp;
    const QString name = args.value(QStringLiteral("name")).toString().trimmed();
    if (name.isEmpty())
        return err("bad_args", QStringLiteral("name required"));
    QString path;
    double in = 0.0;
    double out = -1.0;
    const bool fromClip = trackIndex >= 0 && clipIndex >= 0;
    if (fromClip) {
        const drift::Clip &clip = m_app->m_project.tracks().at(trackIndex).clips.at(clipIndex);
        const drift::MediaAsset *asset = m_app->m_project.asset(clip.assetId);
        if (!asset || asset->path.isEmpty() || (asset->hasAudioKnown && !asset->hasAudio))
            return err("type_mismatch", QStringLiteral("That clip has no audio"));
        path = asset->path;
        in = drift::usToSeconds(clip.srcIn);
        out = drift::usToSeconds(clip.srcOut);
        if (args.contains(QStringLiteral("in")))
            in = args.value(QStringLiteral("in")).toDouble();
        if (args.contains(QStringLiteral("out")))
            out = args.value(QStringLiteral("out")).toDouble();
    } else {
        path = args.value(QStringLiteral("path")).toString().trimmed();
        if (path.isEmpty())
            return err("bad_args", QStringLiteral("path or clip required"));
        if (args.contains(QStringLiteral("in")))
            in = args.value(QStringLiteral("in")).toDouble();
        if (args.contains(QStringLiteral("out")))
            out = args.value(QStringLiteral("out")).toDouble();
    }
    const QUrl url(path);
    const QString local = url.isLocalFile() ? url.toLocalFile() : path;
    if (!QFileInfo::exists(local))
        return err("not_found", QStringLiteral("No such file: %1").arg(local));
    if (in < 0.0 || (out >= 0.0 && out - in < VoiceLibrary::kMinReferenceSeconds))
        return err("bad_args", QStringLiteral("The sample must be %1-%2 seconds of speech")
                                   .arg(VoiceLibrary::kMinReferenceSeconds).arg(VoiceLibrary::kMaxReferenceSeconds));
    const QJsonObject unavailable = localTtsUnavailable();
    if (!unavailable.isEmpty())
        return unavailable;
    VoiceLibrary *voices = m_app->m_voices;
    if (voices->encoding())
        return err("busy", QStringLiteral("Another voice is still being prepared"));
    QString failure;
    const QMetaObject::Connection conn = connect(voices, &VoiceLibrary::error, this,
                                                 [&failure](const QString &message) { failure = message; });
    const QString id = (fromClip || args.contains(QStringLiteral("in")) || args.contains(QStringLiteral("out")))
                           ? voices->addFromMedia(local, in, out, name)
                           : voices->addFromFile(local, name);
    disconnect(conn);
    if (id.isEmpty())
        return err("internal", failure.isEmpty() ? QStringLiteral("Could not start preparing the voice") : failure);
    return ok({{QStringLiteral("job_id"), id}});
}

QJsonObject McpController::renameVoice(const QString &id, const QString &name)
{
    using namespace drift::mcp;
    VoiceLibrary *voices = m_app->m_voices;
    if (voices->indexOfId(id) < 0)
        return err("not_found", QStringLiteral("Unknown voice %1 (list_local_voices)").arg(id));
    if (id == QLatin1String("default"))
        return err("bad_args", QStringLiteral("The built-in voice can't be renamed"));
    if (name.trimmed().isEmpty())
        return err("bad_args", QStringLiteral("name required"));
    voices->rename(id, name);
    return ok({{QStringLiteral("id"), id}, {QStringLiteral("name"), voices->nameFor(id)}});
}

QJsonObject McpController::removeVoice(const QString &id)
{
    using namespace drift::mcp;
    VoiceLibrary *voices = m_app->m_voices;
    if (voices->indexOfId(id) < 0)
        return err("not_found", QStringLiteral("Unknown voice %1 (list_local_voices)").arg(id));
    if (id == QLatin1String("default"))
        return err("bad_args", QStringLiteral("The built-in voice can't be removed"));
    voices->remove(id);
    return ok({{QStringLiteral("removed"), id}});
}

QJsonObject McpController::voiceoverFromSubtitles(int trackIndex, int clipIndex, const QJsonObject &args)
{
    using namespace drift::mcp;
    TtsController *tts = m_app->m_tts;
    if (m_app->m_project.tracks().at(trackIndex).clips.at(clipIndex).type != drift::ClipType::Subtitle)
        return err("type_mismatch", QStringLiteral("clip must be a subtitle clip"));
    QString voice = args.value(QStringLiteral("voice")).toString().trimmed();
    if (voice.isEmpty())
        voice = tts->lastVoice();
    if (m_app->m_voices->indexOfId(voice) < 0)
        return err("not_found", QStringLiteral("Unknown voice %1 (list_local_voices)").arg(voice));
    QString language = args.value(QStringLiteral("language")).toString().trimmed();
    if (language.isEmpty())
        language = tts->lastLanguage();
    if (!drift::ChatterboxTts::supportedLanguages().contains(language))
        return err("bad_args", QStringLiteral("Unsupported language %1 (local_tts_status lists them)").arg(language));
    const double exaggeration = args.contains(QStringLiteral("exaggeration"))
                                    ? args.value(QStringLiteral("exaggeration")).toDouble()
                                    : tts->lastExaggeration();
    if (exaggeration < 0.25 || exaggeration > 2.0)
        return err("bad_args", QStringLiteral("exaggeration must be 0.25-2"));
    bool hasText = false;
    for (const drift::SubtitleCue &cue : m_app->m_project.tracks().at(trackIndex).clips.at(clipIndex).subtitleCues)
        hasText = hasText || !cue.text.simplified().isEmpty();
    if (!hasText)
        return err("bad_args", QStringLiteral("This subtitle clip has no text to speak"));
    const QJsonObject unavailable = localTtsUnavailable();
    if (!unavailable.isEmpty())
        return unavailable;
    const QString id = tts->voiceoverFromSubtitles(trackIndex, clipIndex, voice, language, exaggeration);
    if (id.isEmpty())
        return err("internal", QStringLiteral("Could not start the voiceover"));
    return ok({{QStringLiteral("job_id"), id}});
}

QJsonObject McpController::sfxGenerate(const QJsonObject &args)
{
    using namespace drift::mcp;
    const QString provider = QString::fromLatin1(CloudProviders::kElevenLabs);
    const QString prompt = args.value(QStringLiteral("prompt")).toString().trimmed();
    if (prompt.isEmpty())
        return err("bad_args", QStringLiteral("prompt required"));
    const QJsonObject unavailable = cloudUnavailable(provider);
    if (!unavailable.isEmpty())
        return unavailable;
    const QString key = m_app->m_cloud->apiKey(provider);
    const double duration = args.value(QStringLiteral("duration")).toDouble(0.0);
    const double influence = args.value(QStringLiteral("prompt_influence")).toDouble(0.3);
    const bool loop = args.value(QStringLiteral("loop")).toBool(false);
    const QString path = generatedPath(QStringLiteral("sfx"), provider, prompt);
    const QJsonObject generator{{QStringLiteral("provider"), provider}, {QStringLiteral("kind"), QStringLiteral("sfx")},
                                {QStringLiteral("prompt"), prompt}, {QStringLiteral("duration"), duration}};
    const QJsonValue place = args.value(QStringLiteral("place"));
    const QPointer<McpController> self(this);
    const QString id = m_app->m_jobs->start(
        QStringLiteral("sfx"), provider, JobRegistry::Lane::Network,
        [key, prompt, duration, influence, loop, path](JobContext &ctx) {
            ctx.progress(0.1, QStringLiteral("Generating sound…"));
            drift::cloud::CloudError error;
            const QByteArray audio = drift::cloud::elevenlabs::soundEffect(
                key, prompt, duration, influence, loop, [&ctx] { return ctx.cancelled(); }, &error);
            if (error.isError())
                return ctx.fail(error.code, error.message);
            QFile file(path);
            if (audio.isEmpty() || !file.open(QIODevice::WriteOnly) || file.write(audio) != audio.size())
                return ctx.fail(QStringLiteral("internal"), QStringLiteral("Could not write %1").arg(path));
            ctx.succeed({});
        },
        [self, path, generator, place](const QJsonObject &job) {
            if (!self)
                return QJsonObject{};
            if (!job.value(QStringLiteral("ok")).toBool())
                return QJsonObject{};
            QJsonObject imported = self->importGeneratedAudio(path, generator, place);
            imported.remove(QStringLiteral("ok"));
            return imported;
        });
    return ok({{QStringLiteral("job_id"), id}});
}

QJsonObject McpController::listVoices(const QJsonObject &args) const
{
    using namespace drift::mcp;
    const QString provider = args.value(QStringLiteral("provider")).toString(QString::fromLatin1(CloudProviders::kElevenLabs));
    if (provider != QLatin1String(CloudProviders::kElevenLabs) && provider != QLatin1String(CloudProviders::kFish))
        return err("bad_args", QStringLiteral("provider must be elevenlabs or fish"));
    if (!m_app->m_cloud->configured(provider))
        return cloudUnavailable(provider);
    const QString key = m_app->m_cloud->apiKey(provider);
    const QString search = args.value(QStringLiteral("search")).toString();
    const int limit = qBound(1, args.value(QStringLiteral("limit")).toInt(30), 100);
    const QJsonValue page = args.value(QStringLiteral("page"));
    const bool mine = args.value(QStringLiteral("mine")).toBool(false);
    QJsonObject result;
    drift::cloud::CloudError error;
    QEventLoop loop;
    (void)QtConcurrent::run([&]() {
        result = provider == QLatin1String(CloudProviders::kElevenLabs)
                     ? drift::cloud::elevenlabs::voices(key, search, page.toString(), limit, &error)
                     : drift::cloud::fish::voices(key, search, mine, qMax(1, page.toInt(1)), limit, &error);
        QMetaObject::invokeMethod(&loop, &QEventLoop::quit, Qt::QueuedConnection);
    });
    loop.exec();
    if (error.isError())
        return err(error.code.toUtf8().constData(), error.message);
    result.insert(QStringLiteral("default_voice"), m_app->m_cloud->setting(provider, QStringLiteral("voice")));
    return ok(result);
}

QJsonObject McpController::cloudProviderStatus() const
{
    return drift::mcp::ok(m_app->m_cloud->statusJson());
}

QJsonObject McpController::keepRanges(int trackIndex, int clipIndex, const QJsonArray &ranges,
                                         double padding, double declick, bool ripple)
{
    using namespace drift::mcp;
    if (!m_app->isValidClipIndex(trackIndex, clipIndex))
        return err("not_found", QStringLiteral("Unknown clip"));
    const drift::Clip clip = m_app->m_project.tracks().at(trackIndex).clips.at(clipIndex);
    if (clip.path.isEmpty() && clip.sequenceId.isEmpty())
        return err("type_mismatch", QStringLiteral("keep_ranges works on media clips"));
    if (clip.hasSpeedCurve())
        return err("type_mismatch", QStringLiteral("This clip has a speed ramp; flatten it first"));
    QList<drift::TimeRangeUs> source;
    for (const QJsonValue &v : ranges) {
        const QJsonObject r = v.toObject();
        const double a = r.value(QStringLiteral("start")).toDouble(-1);
        const double b = r.value(QStringLiteral("end")).toDouble(-1);
        if (a < 0 || b <= a)
            return err("bad_args", QStringLiteral("each range needs start < end (source seconds)"));
        source.append({drift::secondsToUs(a), drift::secondsToUs(b)});
    }
    if (source.isEmpty())
        return err("bad_args", QStringLiteral("ranges required"));

    const drift::TimeUs mediaDur = m_app->sourceDurationForClip(clip);
    const drift::TimeUs padUs = drift::secondsToUs(qBound(0.0, padding, 1.0));
    const drift::TimeUs declickUs = drift::secondsToUs(qBound(0.0, declick, 0.5));
    QString buildError;
    const drift::Project before = m_app->m_project;
    AppController::SegmentReplaceResult replaced;
    QString error;
    if (!m_app->replaceClipGroupWithSegments(
            trackIndex, clipIndex,
            [&](const drift::Clip &member) {
                return drift::segmentsFromSourceRanges(member, source, mediaDur, padUs, declickUs, &buildError);
            },
            ripple, &replaced, &error))
        return err(error == QLatin1String("would_overlap") ? "would_overlap" : "bad_args",
                   error == QLatin1String("would_overlap")
                       ? QStringLiteral("The result is longer and would run into the next clip; pass ripple:true")
                       : error);
    if (replaced.ids.isEmpty())
        return err("bad_args", buildError.isEmpty() ? QStringLiteral("Nothing left to keep") : buildError);
    m_app->pushProjectEdit(before, AppController::tr("Keep ranges"));
    m_app->finishEdit(AppController::tr("Keep ranges"));

    QJsonArray applied;
    drift::TimeUs end = clip.timelineStart;
    for (const QString &id : std::as_const(replaced.ids)) {
        int t = -1, c = -1;
        if (!findClipById(m_app->m_project, id, &t, &c))
            continue;
        const drift::Clip &seg = m_app->m_project.tracks().at(t).clips.at(c);
        applied.append(QJsonObject{{QStringLiteral("id"), id},
                                   {QStringLiteral("src_start"), drift::usToSeconds(seg.srcIn)},
                                   {QStringLiteral("src_end"), drift::usToSeconds(seg.srcOut)},
                                   {QStringLiteral("start"), drift::usToSeconds(seg.timelineStart)},
                                   {QStringLiteral("end"), drift::usToSeconds(seg.timelineEnd())}});
        end = qMax(end, seg.timelineEnd());
    }
    return ok({{QStringLiteral("clips"), applied},
               {QStringLiteral("duration"), drift::usToSeconds(end - clip.timelineStart)},
               {QStringLiteral("delta"), drift::usToSeconds(replaced.deltaUs)}});
}

QJsonObject McpController::assemble(const QJsonArray &edl, int trackIndex, bool atGiven, double atSeconds,
                                       double padding, double declick)
{
    using namespace drift::mcp;
    struct Run
    {
        QString asset;
        QJsonArray ranges;
    };
    QList<Run> runs;
    for (const QJsonValue &v : edl) {
        const QJsonObject e = v.toObject();
        const QString asset = e.value(QStringLiteral("asset")).toString();
        if (!m_app->m_project.asset(asset))
            return err("not_found", QStringLiteral("Unknown asset %1").arg(asset));
        const QJsonObject range{{QStringLiteral("start"), e.value(QStringLiteral("start"))},
                                {QStringLiteral("end"), e.value(QStringLiteral("end"))}};
        if (runs.isEmpty() || runs.last().asset != asset)
            runs.append({asset, {}});
        runs.last().ranges.append(range);
    }
    if (runs.isEmpty())
        return err("bad_args", QStringLiteral("edl required: [{asset, start, end}]"));
    if (trackIndex >= m_app->m_project.tracks().size())
        return err("bad_args", QStringLiteral("track out of range"));

    beginBatch();
    const auto abort = [this](const QJsonObject &error) {
        // Nothing half-built stays behind: roll back to the start of the batch.
        const auto transcripts = m_app->m_project.transcripts();
        m_app->m_project = batchBefore();
        m_app->m_project.setTranscripts(transcripts);
        endBatch(QString(), false);
        return error;
    };

    int track = trackIndex;
    double cursor = atSeconds;
    QJsonArray placed;
    for (const Run &run : std::as_const(runs)) {
        const int assetIndex = m_app->m_project.assetIndex(run.asset);
        if (track < 0) {
            track = drift::defaultTrackForClipType(
                m_app->m_project, drift::clipTypeFromString(drift::mediaKindToString(m_app->m_project.asset(run.asset)->kind)));
        }
        if (!atGiven && placed.isEmpty()) {
            // Append after whatever the target track already holds.
            cursor = 0.0;
            if (track >= 0)
                for (const drift::Clip &c : m_app->m_project.tracks().at(track).clips)
                    cursor = qMax(cursor, drift::usToSeconds(c.timelineEnd()));
        }
        QSet<QString> before;
        for (const drift::Track &t : m_app->m_project.tracks())
            for (const drift::Clip &c : t.clips)
                before.insert(c.id);
        if (track >= 0 && m_app->trackAcceptsAsset(track, assetIndex))
            m_app->addClipFromAssetAt(assetIndex, track, cursor);
        else
            m_app->addClipFromAssetOnNewTrackAt(assetIndex, qMax(0, track), cursor);
        // The clip carrying the asset's picture (or sound, for audio); a linked audio companion
        // follows it through keep_ranges.
        QString mainId;
        for (int t = 0; t < m_app->m_project.tracks().size() && mainId.isEmpty(); ++t) {
            for (const drift::Clip &c : m_app->m_project.tracks().at(t).clips) {
                if (!before.contains(c.id) && c.assetId == run.asset
                    && (m_app->m_project.tracks().at(t).type != drift::TrackType::Audio
                        || m_app->m_project.asset(run.asset)->kind == drift::MediaKind::Audio)) {
                    mainId = c.id;
                    track = t;
                    break;
                }
            }
        }
        if (mainId.isEmpty())
            return abort(err("bad_args", QStringLiteral("Could not place asset %1 at %2 s").arg(run.asset).arg(cursor)));
        int t = -1, c = -1;
        findClipById(m_app->m_project, mainId, &t, &c);
        const QJsonObject kept = keepRanges(t, c, run.ranges, padding, declick, true);
        if (!kept.value(QStringLiteral("ok")).toBool())
            return abort(kept);
        for (const QJsonValue &seg : kept.value(QStringLiteral("clips")).toArray()) {
            placed.append(seg);
            cursor = qMax(cursor, seg.toObject().value(QStringLiteral("end")).toDouble());
        }
    }
    endBatch(AppController::tr("Assemble"), true);
    return ok({{QStringLiteral("clips"), placed},
               {QStringLiteral("track"), track},
               {QStringLiteral("end"), cursor}});
}

namespace {

QString matchKey(const QString &text)
{
    QString out;
    for (const QChar c : text.toLower()) {
        if (c.isLetterOrNumber() || c == QLatin1Char('\''))
            out.append(c);
    }
    return out;
}

// Timeline-free: the quietest 10 ms of the source between a and b, for a cut that lands in silence.
drift::TimeUs quietestPoint(const QString &path, drift::TimeUs a, drift::TimeUs b)
{
    if (b - a < 20'000)
        return (a + b) / 2;
    const std::vector<float> pcm = drift::readMono16k(path, a, b);
    constexpr int kBucket = drift::kSpeechSampleRate / 100;
    size_t best = 0;
    double bestEnergy = std::numeric_limits<double>::max();
    for (size_t off = 0; off + kBucket <= pcm.size(); off += kBucket / 2) {
        double e = 0.0;
        for (int i = 0; i < kBucket; ++i)
            e += double(pcm[off + i]) * pcm[off + i];
        if (e < bestEnergy) {
            bestEnergy = e;
            best = off + kBucket / 2;
        }
    }
    return qBound(a, a + drift::speechSamplesToUs(best), b);
}

} // namespace

QJsonObject McpController::cutWords(int trackIndex, int clipIndex, const QJsonObject &args)
{
    using namespace drift::mcp;
    if (!m_app->isValidClipIndex(trackIndex, clipIndex))
        return err("not_found", QStringLiteral("Unknown clip"));
    const drift::Clip clip = m_app->m_project.tracks().at(trackIndex).clips.at(clipIndex);
    const drift::TranscriptPtr t = m_app->m_project.transcript(clip.assetId);
    if (!t)
        return err("not_found", QStringLiteral("No transcript for this clip's media; run transcribe first"));
    const QString snap = args.value(QStringLiteral("snap")).toString(QStringLiteral("boundary"));
    const drift::TimeUs pad = drift::secondsToUs(qBound(0.03, args.value(QStringLiteral("padding")).toDouble(0.05), 0.2));
    const drift::TimeUs declickUs = drift::secondsToUs(qBound(0.0, args.value(QStringLiteral("declick")).toDouble(0.03), 0.5));
    const bool dryRun = args.value(QStringLiteral("dry_run")).toBool(false);

    // Speech tokens in order; runs are expressed as positions in this list.
    QList<int> speech;
    for (int i = 0; i < t->words.size(); ++i)
        if (drift::isSpeechToken(t->words.at(i)))
            speech.append(i);
    QHash<int, int> position;
    for (int p = 0; p < speech.size(); ++p)
        position.insert(speech.at(p), p);

    QSet<int> remove; // positions in `speech`
    for (const QJsonValue &v : args.value(QStringLiteral("words")).toArray()) {
        const QJsonArray pair = v.toArray();
        const int a = pair.size() > 0 ? pair.at(0).toInt(-1) : v.toInt(-1);
        const int b = pair.size() > 1 ? pair.at(1).toInt(a) : a;
        if (a < 0 || b < a || b >= t->words.size())
            return err("bad_args", QStringLiteral("words entries are word indices [i, j] from get_transcript"));
        for (int i = a; i <= b; ++i)
            if (position.contains(i))
                remove.insert(position.value(i));
    }
    QStringList texts;
    const QJsonValue textArg = args.value(QStringLiteral("text"));
    if (textArg.isString())
        texts.append(textArg.toString());
    for (const QJsonValue &v : textArg.toArray())
        texts.append(v.toString());
    const bool phrase = args.value(QStringLiteral("match")).toString(QStringLiteral("word")) == QLatin1String("phrase");
    for (const QString &text : std::as_const(texts)) {
        const QStringList needle = [&] {
            QStringList out;
            for (const QString &w : text.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts))
                if (!matchKey(w).isEmpty())
                    out.append(matchKey(w));
            return out;
        }();
        if (needle.isEmpty())
            continue;
        if (!phrase) {
            for (int p = 0; p < speech.size(); ++p)
                if (needle.contains(matchKey(t->words.at(speech.at(p)).text)))
                    remove.insert(p);
            continue;
        }
        for (int p = 0; p + needle.size() <= speech.size(); ++p) {
            bool hit = true;
            for (int k = 0; k < needle.size() && hit; ++k)
                hit = matchKey(t->words.at(speech.at(p + k)).text) == needle.at(k);
            if (hit)
                for (int k = 0; k < needle.size(); ++k)
                    remove.insert(p + k);
        }
    }
    if (remove.isEmpty())
        return err("not_found", QStringLiteral("No matching words"));

    // Only words this clip plays, grouped into runs of neighbouring speech.
    QList<int> positions(remove.cbegin(), remove.cend());
    std::sort(positions.begin(), positions.end());
    QList<QPair<int, int>> runs;
    for (const int p : std::as_const(positions)) {
        const drift::TranscriptWord &w = t->words.at(speech.at(p));
        if (w.endUs <= clip.srcIn || w.startUs >= clip.srcOut)
            continue;
        if (!runs.isEmpty() && runs.last().second + 1 == p)
            runs.last().second = p;
        else
            runs.append({p, p});
    }
    if (runs.isEmpty())
        return err("not_found", QStringLiteral("Those words aren't in the part of the media this clip plays"));

    QList<drift::TimeRangeUs> removedTimeline;
    QJsonArray plan;
    QJsonArray removedWords;
    for (const auto &[a, b] : std::as_const(runs)) {
        const drift::TranscriptWord &first = t->words.at(speech.at(a));
        const drift::TranscriptWord &last = t->words.at(speech.at(b));
        const drift::TimeUs L0 = a > 0 ? qMax(clip.srcIn, t->words.at(speech.at(a - 1)).endUs) : clip.srcIn;
        const drift::TimeUs L1 = qMax(L0, first.startUs);
        const drift::TimeUs R0 = qMin(clip.srcOut, last.endUs);
        const drift::TimeUs R1 = b + 1 < speech.size() ? qMax(R0, qMin(clip.srcOut, t->words.at(speech.at(b + 1)).startUs))
                                                       : clip.srcOut;
        drift::TimeUs cutS;
        drift::TimeUs cutE;
        if (snap == QLatin1String("silence")) {
            cutS = quietestPoint(clip.path, L0, L1);
            cutE = quietestPoint(clip.path, R0, R1);
        } else {
            // Take the pauses around the removed words too, leaving `pad` of air beside the
            // words that stay (less when the gap is shorter than that).
            cutS = L0 == clip.srcIn ? clip.srcIn : qMin(L0 + qMin(pad, (L1 - L0) / 2), L1);
            cutE = R1 == clip.srcOut ? clip.srcOut : qMax(R1 - qMin(pad, (R1 - R0) / 2), R0);
        }
        drift::TimeRangeUs tl;
        if (!drift::sourceRangeToTimeline(clip, {cutS, cutE}, tl))
            continue;
        removedTimeline.append(tl);
        QStringList said;
        for (int p = a; p <= b; ++p) {
            said.append(t->words.at(speech.at(p)).text);
            removedWords.append(QJsonObject{{QStringLiteral("i"), speech.at(p)},
                                            {QStringLiteral("text"), t->words.at(speech.at(p)).text}});
        }
        plan.append(QJsonObject{{QStringLiteral("start"), drift::usToSeconds(tl.startUs)},
                                {QStringLiteral("end"), drift::usToSeconds(tl.endUs)},
                                {QStringLiteral("text"), said.join(QLatin1Char(' '))}});
    }
    QJsonObject result{{QStringLiteral("removed"), plan}, {QStringLiteral("removed_words"), removedWords}};
    if (dryRun) {
        result.insert(QStringLiteral("dry_run"), true);
        return ok(result);
    }

    const QList<drift::TimeRangeUs> kept =
        drift::keptTimelineIntervals(clip, removedTimeline, qMax<drift::TimeUs>(drift::kCutMinEdgeUs, 2 * declickUs));
    const drift::Project before = m_app->m_project;
    AppController::SegmentReplaceResult replaced;
    QString error;
    if (!m_app->replaceClipGroupWithSegments(
            trackIndex, clipIndex,
            [&kept, declickUs](const drift::Clip &member) { return drift::packedSegments(member, kept, declickUs); },
            true, &replaced, &error))
        return err("bad_args", error);
    m_app->pushProjectEdit(before, AppController::tr("Cut words"));
    m_app->finishEdit(AppController::tr("Cut words"));
    result.insert(QStringLiteral("clips"), QJsonArray::fromStringList(replaced.ids));
    result.insert(QStringLiteral("delta"), drift::usToSeconds(replaced.deltaUs));
    return ok(result);
}

QJsonObject McpController::getJob(const QString &id) const
{
    using namespace drift::mcp;
    const QJsonObject job = m_app->m_jobs->job(id);
    if (job.isEmpty())
        return err("not_found", QStringLiteral("Unknown job"));
    return ok(job);
}

QJsonObject McpController::cancelJob(const QString &id)
{
    using namespace drift::mcp;
    if (m_app->m_jobs->job(id).isEmpty())
        return err("not_found", QStringLiteral("Unknown job"));
    return ok({{QStringLiteral("cancelled"), m_app->m_jobs->cancel(id)}});
}
