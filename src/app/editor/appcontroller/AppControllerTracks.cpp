#include "app/editor/appcontroller/AppControllerInternal.h"
#include "core/edit/BeatCut.h"
#include "app/editor/PreferencesController.h"
#include "app/editor/keymap/KeymapCatalog.h"
#include "app/jobs/audio/AudioAlignController.h"
#include "engine/decode/probe/MediaProbe.h"
#include "app/editor/sessions/MaskingController.h"
#include "app/mcp/McpController.h"
#include "app/jobs/vision/SegmentationController.h"
#include "app/editor/ProjectFileController.h"
#include "ui/preview/PreviewController.h"

#include "engine/decode/readers/ClipReaderPool.h"
#include "engine/packages/effects/EffectTemplateCatalog.h"
// Engine-internal by its own header comment, and included here only for releaseCaches(): the GPU
// caches have no other owner outside src/engine to ask. A one-line forwarder on GpuCompositor
// would restore the boundary.
#include "engine/encode/proxies/ReverseProxyCache.h"
#include "engine/render2d/vector/VectorClipRenderer.h"
#include "ui/timeline/ClipThumbnailStore.h"

#include "ui/timeline/TimelineCursors.h"
#include <QEventLoop>
#include <QGuiApplication>
#include <QScopedValueRollback>
#include <QScreen>

#include <QSettings>
#include <QStandardPaths>
#include <QCryptographicHash>
#include <QDir>
using namespace drift::appdetail;
using drift::mcpdetail::findClipById;
using drift::mcpdetail::runBeatAnalysis;
using namespace drift::appcontroller_detail;

void AppController::setTrackMuted(int trackIndex, bool muted)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;
    if (m_project.tracks()[trackIndex].muted == muted)
        return;

    const drift::Project before = m_project;
    m_project.tracks()[trackIndex].muted = muted;
    pushProjectEdit(before, tr("Track mute"));
    finishEdit(muted ? tr("Track muted") : tr("Track unmuted"));
}

void AppController::setTrackHidden(int trackIndex, bool hidden)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;
    if (m_project.tracks()[trackIndex].hidden == hidden)
        return;

    const drift::Project before = m_project;
    m_project.tracks()[trackIndex].hidden = hidden;
    pushProjectEdit(before, tr("Track visibility"));
    finishEdit(hidden ? tr("Track hidden") : tr("Track shown"));
}

bool AppController::renameTrack(int trackIndex, const QString &name)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return false;

    // Unlike renameAsset, an empty result is allowed through rather than refused — it clears
    // the custom name back to the type+position fallback ("Video 1"), which is a real,
    // reachable state (Track::name's own default) rather than an invalid one.
    const QString trimmed = name.trimmed();
    if (m_project.tracks()[trackIndex].name == trimmed)
        return false;

    const drift::Project before = m_project;
    m_project.tracks()[trackIndex].name = trimmed;
    pushProjectEdit(before, tr("Track renamed"));
    finishEdit(tr("Track renamed"));
    return true;
}

bool AppController::trackMuted(int trackIndex) const
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return false;
    return m_project.tracks().at(trackIndex).muted;
}

bool AppController::trackHidden(int trackIndex) const
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return false;
    return m_project.tracks().at(trackIndex).hidden;
}

void AppController::setTrackExcludedFromExport(int trackIndex, bool excluded)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;
    if (m_project.tracks()[trackIndex].excludeFromExport == excluded)
        return;

    const drift::Project before = m_project;
    m_project.tracks()[trackIndex].excludeFromExport = excluded;
    pushProjectEdit(before, tr("Exclude from export"));
    finishEdit(excluded ? tr("Track left out of export") : tr("Track included in export"));
}

bool AppController::trackExcludedFromExport(int trackIndex) const
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return false;
    return m_project.tracks().at(trackIndex).excludeFromExport;
}

void AppController::setTrackSolo(int trackIndex, bool solo)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;
    if (m_project.tracks()[trackIndex].solo == solo)
        return;

    const drift::Project before = m_project;
    m_project.tracks()[trackIndex].solo = solo;
    pushProjectEdit(before, tr("Track solo"));
    finishEdit(solo ? tr("Track soloed") : tr("Track unsoloed"));
}

bool AppController::trackSolo(int trackIndex) const
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return false;
    return m_project.tracks().at(trackIndex).solo;
}

void AppController::setTrackLocked(int trackIndex, bool locked)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;
    if (m_project.tracks()[trackIndex].locked == locked)
        return;

    const drift::Project before = m_project;
    m_project.tracks()[trackIndex].locked = locked;
    pushProjectEdit(before, tr("Track lock"));
    finishEdit(locked ? tr("Track locked") : tr("Track unlocked"));
}

bool AppController::trackLocked(int trackIndex) const
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return false;
    return m_project.tracks().at(trackIndex).locked;
}

void AppController::previewTrackVolume(int trackIndex, double volume)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;
    volume = qMax(0.0, volume);
    if (!m_previewDragActive)
        beginPreviewDrag(tr("Track volume"));

    m_project.tracks()[trackIndex].volume = volume;
    emitPreviewFrame();
}

void AppController::setTrackVolume(int trackIndex, double volume)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;
    volume = qMax(0.0, volume);
    if (m_previewDragActive) {
        m_project.tracks()[trackIndex].volume = volume;
        commitPreviewDrag();
        return;
    }
    if (qFuzzyCompare(m_project.tracks()[trackIndex].volume, volume))
        return;

    const drift::Project before = m_project;
    m_project.tracks()[trackIndex].volume = volume;
    pushProjectEdit(before, tr("Track volume"));
    finishEdit(tr("Track volume changed"));
}

double AppController::trackVolume(int trackIndex) const
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return 1.0;
    return m_project.tracks().at(trackIndex).volume;
}

void AppController::previewTrackPan(int trackIndex, double pan)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;
    pan = std::clamp(pan, -1.0, 1.0);
    if (!m_previewDragActive)
        beginPreviewDrag(tr("Track pan"));

    m_project.tracks()[trackIndex].pan = pan;
    emitPreviewFrame();
}

void AppController::setTrackPan(int trackIndex, double pan)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;
    pan = std::clamp(pan, -1.0, 1.0);
    if (m_previewDragActive) {
        m_project.tracks()[trackIndex].pan = pan;
        commitPreviewDrag();
        return;
    }
    if (qFuzzyCompare(m_project.tracks()[trackIndex].pan, pan))
        return;

    const drift::Project before = m_project;
    m_project.tracks()[trackIndex].pan = pan;
    pushProjectEdit(before, tr("Track pan"));
    finishEdit(tr("Track pan changed"));
}

double AppController::trackPan(int trackIndex) const
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return 0.0;
    return m_project.tracks().at(trackIndex).pan;
}

QVariantMap AppController::trackAudioLevels(int trackIndex) const
{
    float left = 0.0f;
    float right = 0.0f;
    if (m_audioRecorder.isRecording() && m_audioRecorder.recordingTrackIndex() == trackIndex) {
        left = m_audioRecorder.audioLevel();
        right = left;
    } else if (m_playback.isPlaying()) {
        const auto levels = m_playback.trackAudioLevels(trackIndex);
        left = levels.first;
        right = levels.second;
    }
    return {
        {QStringLiteral("left"), left},
        {QStringLiteral("right"), right}
    };
}

QList<float> AppController::meterLevels(const QList<int> &trackIndexes) const
{
    QList<float> out = m_playback.takeMeterPeaks(trackIndexes);
    if (!m_audioRecorder.isRecording())
        return out;
    const float level = m_audioRecorder.audioLevel();
    if (!m_playback.isPlaying()) {
        out[0] = level;
        out[1] = level;
    }
    const int recordingTrack = m_audioRecorder.recordingTrackIndex();
    for (int i = 0; i < trackIndexes.size(); ++i) {
        if (trackIndexes.at(i) == recordingTrack) {
            out[2 + i * 2] = level;
            out[3 + i * 2] = level;
        }
    }
    return out;
}

QVariantMap AppController::masterAudioLevels() const
{
    float left = 0.0f;
    float right = 0.0f;
    if (m_playback.isPlaying()) {
        const auto levels = m_playback.masterAudioLevels();
        left = levels.first;
        right = levels.second;
    } else if (m_audioRecorder.isRecording()) {
        left = m_audioRecorder.audioLevel();
        right = left;
    }
    return {
        {QStringLiteral("left"), left},
        {QStringLiteral("right"), right}
    };
}

void AppController::setTrackClipDisplay(int trackIndex, int mode)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;
    const auto display = static_cast<drift::Track::ClipDisplay>(qBound(0, mode, 2));
    if (m_project.tracks()[trackIndex].clipDisplay == display)
        return;

    // View-only preference: mutate and refresh without an undo entry.
    m_project.tracks()[trackIndex].clipDisplay = display;
    notifyTracksChanged();
}

int AppController::trackClipDisplay(int trackIndex) const
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return static_cast<int>(drift::Track::ClipDisplay::Both);
    return static_cast<int>(m_project.tracks().at(trackIndex).clipDisplay);
}

void AppController::setTrackShowChannelWaveforms(int trackIndex, bool show)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;
    if (m_project.tracks()[trackIndex].showChannelWaveforms == show)
        return;

    // View-only preference: mutate and refresh without an undo entry.
    m_project.tracks()[trackIndex].showChannelWaveforms = show;
    notifyTracksChanged();
}

bool AppController::trackShowChannelWaveforms(int trackIndex) const
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return false;
    return m_project.tracks().at(trackIndex).showChannelWaveforms;
}

void AppController::setTrackHeightScale(int trackIndex, double scale)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    const qreal clamped = qBound(trackHeightScaleMin(), scale, trackHeightScaleMax());
    if (qFuzzyCompare(m_project.tracks()[trackIndex].heightScale, clamped))
        return;

    // View-only preference, like clipDisplay: no undo entry.
    m_project.tracks()[trackIndex].heightScale = clamped;
    notifyTracksChanged();
}

int AppController::trackRowHeight(int trackIndex, const QVariantMap &metrics) const
{
    const auto metric = [&metrics](const char *key, double fallback) {
        const QVariant value = metrics.value(QLatin1String(key));
        return value.isValid() ? value.toDouble() : fallback;
    };
    const double baseVideo = metric("video", 65.0);

    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return qRound(baseVideo);

    const drift::Track &track = m_project.tracks().at(trackIndex);
    // A lane is drawn inside its parent's row, so it takes no row of its own. Returning 0 rather
    // than filtering lanes out of the model is what keeps the flat (trackIndex, clipIndex)
    // addressing the rest of the app is built on.
    if (track.isAdjustmentLane())
        return 0;

    double base = baseVideo;
    switch (track.type) {
    case drift::TrackType::Audio:
        base = metric("audio", 50.0);
        break;
    case drift::TrackType::Text:
        base = metric("text", 25.0);
        break;
    case drift::TrackType::Subtitle:
        base = metric("subtitle", 25.0);
        break;
    case drift::TrackType::Shape:
        base = metric("shape", 50.0);
        break;
    case drift::TrackType::Adjustment:
        // Nothing to show but the effects it carries, so it gets a label's worth of height
        // rather than a video track's.
        base = metric("adjustment", 28.0);
        break;
    case drift::TrackType::Video:
        break;
    }

    const double scale = track.heightScale > 0 ? track.heightScale : 1.0;
    const int laneCount = visibleAdjustmentLanes(trackIndex).size();
    const double lanes = laneCount * (track.lanesCollapsed ? metric("ribbon", 3.0) : metric("lane", 20.0));
    return qRound(qMax(20.0, base * scale + lanes));
}

QList<int> AppController::visibleAdjustmentLanes(int trackIndex) const
{
    return drift::adjustmentLaneIndexes(m_project, trackIndex);
}

int AppController::adjustmentLaneCount(int trackIndex) const
{
    return visibleAdjustmentLanes(trackIndex).size();
}

QVariantList AppController::adjustmentLanes(int trackIndex) const
{
    QVariantList out;
    for (const int laneIndex : visibleAdjustmentLanes(trackIndex))
        out.append(laneIndex);
    return out;
}

QString AppController::localizedTrackLabel(const drift::Track &track) const
{
    if (!track.name.isEmpty())
        return track.name;
    if (track.isCameraLayer())
        return tr("Camera");
    if (track.isAdjustmentLane()) {
        const drift::AdjustmentKind kind = track.clips.isEmpty() ? drift::AdjustmentKind::VideoEffects
                                                                 : track.clips.first().adjustmentKind;
        return kind == drift::AdjustmentKind::AudioEffects ? tr("Audio FX")
               : kind == drift::AdjustmentKind::Mask       ? tr("Mask")
                                                           : tr("Effects");
    }
    if (track.number <= 0)
        return drift::trackDefaultLabel(track);
    if (track.isTransformLayer())
        return tr("Transform %1").arg(track.number);
    switch (track.type) {
    case drift::TrackType::Video: return tr("Video %1").arg(track.number);
    case drift::TrackType::Audio: return tr("Audio %1").arg(track.number);
    case drift::TrackType::Text: return tr("Text %1").arg(track.number);
    case drift::TrackType::Subtitle: return tr("Subtitle %1").arg(track.number);
    case drift::TrackType::Shape: return tr("Graphic %1").arg(track.number);
    case drift::TrackType::Adjustment: return tr("Adjustment %1").arg(track.number);
    }
    return drift::trackDefaultLabel(track);
}

QString AppController::adjustmentLaneKind(int laneIndex) const
{
    if (laneIndex < 0 || laneIndex >= m_project.tracks().size())
        return {};
    const drift::Track &lane = m_project.tracks().at(laneIndex);
    if (!lane.isAdjustmentLane() || lane.clips.isEmpty())
        return {};
    switch (lane.clips.first().adjustmentKind) {
    case drift::AdjustmentKind::VideoEffects:
        return QStringLiteral("videoEffects");
    case drift::AdjustmentKind::AudioEffects:
        return QStringLiteral("audioEffects");
    case drift::AdjustmentKind::Mask:
        return QStringLiteral("mask");
    default:
        return {};
    }
}

QVariantList AppController::adjustmentLaneSpans(int parentIndex) const
{
    QVariantList out;
    for (const int laneIndex : visibleAdjustmentLanes(parentIndex)) {
        const drift::Track &lane = m_project.tracks().at(laneIndex);
        QVariantList spans;
        for (const drift::Clip &clip : lane.clips) {
            spans.append(QVariantMap{
                {QStringLiteral("start"), drift::usToSeconds(clip.timelineStart)},
                {QStringLiteral("end"), drift::usToSeconds(clip.timelineEnd())},
            });
        }
        out.append(QVariantMap{
            {QStringLiteral("laneIndex"), laneIndex},
            {QStringLiteral("kind"), adjustmentLaneKind(laneIndex)},
            {QStringLiteral("hidden"), lane.hidden},
            {QStringLiteral("spans"), spans},
        });
    }
    return out;
}

void AppController::setTrackLanesCollapsed(int trackIndex, bool collapsed)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;
    if (m_project.tracks()[trackIndex].lanesCollapsed == collapsed)
        return;
    m_project.tracks()[trackIndex].lanesCollapsed = collapsed;
    notifyTracksChanged();
}

double AppController::trackHeightScale(int trackIndex) const
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return 1.0;
    return m_project.tracks().at(trackIndex).heightScale;
}

void AppController::nudgeTrackHeightScale(int trackIndex, int steps)
{
    if (steps == 0)
        return;
    setTrackHeightScale(trackIndex,
                        trackHeightScale(trackIndex) * std::pow(1.18, steps));
}

void AppController::nudgeAllTrackHeightScales(int steps)
{
    if (steps == 0)
        return;

    // Multiplicative, and per lane rather than one shared multiplier, so lanes that already differ
    // keep their proportions and a lane sitting on a limit simply stops while the others carry on.
    // A bigger step than the per-lane nudge: this is a button pressed a couple of times to reframe
    // the whole timeline, not a wheel rolled under a pointer.
    const qreal factor = std::pow(1.35, steps);
    bool changed = false;
    for (drift::Track &track : m_project.tracks()) {
        const qreal clamped = qBound(trackHeightScaleMin(), track.heightScale * factor,
                                     trackHeightScaleMax());
        if (qFuzzyCompare(track.heightScale, clamped))
            continue;
        track.heightScale = clamped;
        changed = true;
    }

    // View-only preference, like setTrackHeightScale: no undo entry.
    if (changed)
        notifyTracksChanged();
}

bool AppController::canGrowTrackHeights() const
{
    // One lane still off the ceiling is enough: the sweep clamps per lane, so it does something
    // even when the tallest lane is already there.
    for (const drift::Track &track : m_project.tracks()) {
        if (!qFuzzyCompare(track.heightScale, trackHeightScaleMax()))
            return true;
    }
    return false;
}

bool AppController::canShrinkTrackHeights() const
{
    for (const drift::Track &track : m_project.tracks()) {
        if (!qFuzzyCompare(track.heightScale, trackHeightScaleMin()))
            return true;
    }
    return false;
}

void AppController::moveTrack(int fromIndex, int toIndex)
{
    const int trackCount = m_project.tracks().size();
    if (fromIndex < 0 || fromIndex >= trackCount || toIndex < 0 || toIndex >= trackCount)
        return;
    if (fromIndex == toIndex)
        return;

    const drift::Project before = m_project;
    m_project.ensureTrackIds();

    // Captured by id, not remapped by index: normalizeAdjustmentLanes re-gathers each track's
    // nested lanes around it afterwards, so the destination index no longer describes where any
    // particular track ended up. Moving a track takes its lanes with it, which is the point.
    const QList<QPair<QString, int>> selection = captureSelectionByTrackId();
    const QString transitionTrackId = trackIdAt(m_selectedTransitionTrack);

    m_project.tracks().move(fromIndex, toIndex);
    normalizeAdjustmentLanes(m_project);

    restoreSelectionByTrackId(selection);
    m_selectedTransitionTrack = m_project.trackIndexById(transitionTrackId);

    pushProjectEdit(before, tr("Move track"));
    finishEdit(tr("Track moved"));
}

void AppController::removeTrack(int trackIndex)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    const drift::Project before = m_project;
    m_project.ensureTrackIds();

    const QList<QPair<QString, int>> selection = captureSelectionByTrackId();
    const QString transitionTrackId = trackIdAt(m_selectedTransitionTrack);

    // Nested lanes go with the track. They exist only to modify it, so leaving them behind would
    // strand effects with nothing to apply to — and ensureTrackIds() would then quietly promote
    // them to standalone adjustments affecting the whole canvas.
    const QString removedId = m_project.tracks().at(trackIndex).id;
    for (int i = m_project.tracks().size() - 1; i >= 0; --i) {
        const drift::Track &track = m_project.tracks().at(i);
        if (track.id == removedId
            || (track.isAdjustmentLane() && track.parentTrackId == removedId)) {
            m_project.tracks().removeAt(i);
        }
    }
    normalizeAdjustmentLanes(m_project);

    restoreSelectionByTrackId(selection);
    m_selectedTransitionTrack = m_project.trackIndexById(transitionTrackId);

    pushProjectEdit(before, tr("Delete track"));
    finishEdit(tr("Track deleted"));
}

QList<int> AppController::removableEmptyTrackIndices() const
{
    const QList<drift::Track> &tracks = m_project.tracks();
    QList<int> result;
    int topLevelCount = 0;
    for (int i = 0; i < tracks.size(); ++i) {
        const drift::Track &track = tracks.at(i);
        if (track.isAdjustmentLane())
            continue;
        ++topLevelCount;
        bool empty = track.clips.isEmpty();
        for (int j = 0; empty && j < tracks.size(); ++j) {
            const drift::Track &lane = tracks.at(j);
            if (lane.isAdjustmentLane() && lane.parentTrackId == track.id && !lane.clips.isEmpty())
                empty = false;
        }
        if (empty)
            result.append(i);
    }
    // The project always keeps a track; when everything is empty the lowest one stays.
    if (!result.isEmpty() && result.size() == topLevelCount)
        result.removeFirst();
    return result;
}

bool AppController::hasEmptyTracks() const
{
    return !removableEmptyTrackIndices().isEmpty();
}

int AppController::removeEmptyTracks()
{
    if (removableEmptyTrackIndices().isEmpty())
        return 0;

    const drift::Project before = m_project;
    m_project.ensureTrackIds();

    const QList<QPair<QString, int>> selection = captureSelectionByTrackId();
    const QString transitionTrackId = trackIdAt(m_selectedTransitionTrack);

    const QList<int> indices = removableEmptyTrackIndices();
    QSet<QString> removedIds;
    for (const int index : indices)
        removedIds.insert(m_project.tracks().at(index).id);
    for (int i = m_project.tracks().size() - 1; i >= 0; --i) {
        const drift::Track &track = m_project.tracks().at(i);
        if (removedIds.contains(track.id)
            || (track.isAdjustmentLane() && removedIds.contains(track.parentTrackId))) {
            m_project.tracks().removeAt(i);
        }
    }
    normalizeAdjustmentLanes(m_project);

    restoreSelectionByTrackId(selection);
    m_selectedTransitionTrack = m_project.trackIndexById(transitionTrackId);

    pushProjectEdit(before, tr("Delete empty tracks"));
    finishEdit(tr("Empty tracks deleted"));
    return indices.size();
}

void AppController::addTrack(const QString &type)
{
    const QString normalized = type.trimmed().toLower();
    if (normalized != QLatin1String("video") && normalized != QLatin1String("audio")
        && normalized != QLatin1String("text") && normalized != QLatin1String("subtitle")
        && normalized != QLatin1String("shape")) {
        return;
    }

    const drift::Project before = m_project;
    m_project.tracks().prepend(drift::Track{.type = drift::trackTypeFromString(normalized)});
    pushProjectEdit(before, tr("Add track"));
    finishEdit(tr("Track added"));
}

void AppController::insertTrack(const QString &type, int index)
{
    const QString normalized = type.trimmed().toLower();
    if (normalized != QLatin1String("video") && normalized != QLatin1String("audio")
        && normalized != QLatin1String("text") && normalized != QLatin1String("subtitle")
        && normalized != QLatin1String("shape")) {
        return;
    }

    const drift::Project before = m_project;
    m_project.ensureTrackIds();
    const QList<QPair<QString, int>> selection = captureSelectionByTrackId();
    const QString transitionTrackId = trackIdAt(m_selectedTransitionTrack);

    m_project.tracks().insert(qBound(0, index, int(m_project.tracks().size())),
                              drift::Track{.type = drift::trackTypeFromString(normalized)});
    normalizeAdjustmentLanes(m_project);

    restoreSelectionByTrackId(selection);
    m_selectedTransitionTrack = m_project.trackIndexById(transitionTrackId);

    pushProjectEdit(before, tr("Add track"));
    finishEdit(tr("Track added"));
}

QVariantList AppController::bookmarks() const
{
    QVariantList result;
    for (const drift::Bookmark &bookmark : m_project.bookmarks()) {
        result.append(QVariantMap{
            {QStringLiteral("seconds"), drift::usToSeconds(bookmark.timeUs)},
            {QStringLiteral("label"), bookmark.label},
            // Empty when unset: the marker falls back to the theme colour rather than carrying
            // a resolved one, so a theme change recolours every uncoloured flag with it.
            {QStringLiteral("color"), bookmark.color.isValid() ? bookmark.color.name() : QString()},
        });
    }
    return result;
}

double AppController::workAreaInSeconds() const
{
    return m_project.workAreaInUs() >= 0 ? drift::usToSeconds(m_project.workAreaInUs()) : -1.0;
}

double AppController::workAreaOutSeconds() const
{
    return m_project.workAreaOutUs() >= 0 ? drift::usToSeconds(m_project.workAreaOutUs()) : -1.0;
}

void AppController::setLoopWorkAreaEnabled(bool enabled)
{
    if (m_loopWorkAreaEnabled == enabled)
        return;

    m_loopWorkAreaEnabled = enabled;
    m_playback.setLoopWorkArea(enabled);
    QSettings().setValue(QStringLiteral("playback/loopWorkArea"), enabled);
    emit loopWorkAreaEnabledChanged();
}

void AppController::markWorkAreaIn()
{
    const drift::Project before = m_project;
    const drift::TimeUs at = qBound<drift::TimeUs>(0, m_playheadUs, qMax(m_project.durationUs(), drift::TimeUs{0}));
    m_project.setWorkAreaInUs(at);
    if (m_project.workAreaOutUs() >= 0 && m_project.workAreaOutUs() <= at)
        m_project.setWorkAreaOutUs(-1);
    pushProjectEdit(before, tr("Mark work area in"));
    finishEdit(tr("Work area in marked"));
    emit workAreaChanged();
}

void AppController::markWorkAreaOut()
{
    const drift::Project before = m_project;
    const drift::TimeUs at = qBound<drift::TimeUs>(0, m_playheadUs, qMax(m_project.durationUs(), drift::TimeUs{0}));
    m_project.setWorkAreaOutUs(at);
    if (m_project.workAreaInUs() < 0 || m_project.workAreaInUs() >= at)
        m_project.setWorkAreaInUs(0);
    if (!m_project.hasWorkArea())
        m_project.clearWorkArea();
    pushProjectEdit(before, tr("Mark work area out"));
    finishEdit(tr("Work area out marked"));
    emit workAreaChanged();
}

void AppController::goToWorkAreaIn()
{
    if (!m_project.hasWorkArea())
        return;
    setPlayheadUs(m_project.workAreaInUs());
}

void AppController::goToWorkAreaOut()
{
    if (!m_project.hasWorkArea())
        return;
    setPlayheadUs(m_project.workAreaOutUs());
}

void AppController::clearWorkArea()
{
    if (!m_project.hasWorkArea() && m_project.workAreaInUs() < 0 && m_project.workAreaOutUs() < 0)
        return;

    const drift::Project before = m_project;
    m_project.clearWorkArea();
    pushProjectEdit(before, tr("Clear work area"));
    finishEdit(tr("Work area cleared"));
    emit workAreaChanged();
}

void AppController::toggleLoopWorkArea()
{
    setLoopWorkAreaEnabled(!m_loopWorkAreaEnabled);
}

void AppController::addBookmark(double seconds, const QString &label)
{
    const drift::Project before = m_project;
    m_project.bookmarks().append({
        .timeUs = qMax<drift::TimeUs>(0, drift::secondsToUs(seconds)),
        .label = label.isEmpty() ? QStringLiteral("Bookmark") : label,
    });
    pushProjectEdit(before, tr("Add bookmark"));
    finishEdit(tr("Bookmark added"));
}

void AppController::removeBookmark(int index)
{
    if (index < 0 || index >= m_project.bookmarks().size())
        return;

    const drift::Project before = m_project;
    m_project.bookmarks().removeAt(index);
    pushProjectEdit(before, tr("Remove bookmark"));
    finishEdit(tr("Bookmark removed"));
}

void AppController::updateBookmark(int index, double seconds, const QString &label)
{
    if (index < 0 || index >= m_project.bookmarks().size())
        return;

    const drift::TimeUs timeUs = qMax<drift::TimeUs>(0, drift::secondsToUs(seconds));
    const QString resolvedLabel = label.isEmpty() ? QStringLiteral("Bookmark") : label;
    // Snapshot before binding the reference: bookmarks are a plain QList, so a later copy would
    // share the buffer the reference writes into.
    const drift::Project before = m_project;
    drift::Bookmark &bookmark = m_project.bookmarks()[index];
    if (bookmark.timeUs == timeUs && bookmark.label == resolvedLabel)
        return;

    bookmark.timeUs = timeUs;
    bookmark.label = resolvedLabel;
    pushProjectEdit(before, tr("Edit bookmark"));
    finishEdit(tr("Bookmark updated"));
}

void AppController::setBookmarkColor(int index, const QString &color)
{
    if (index < 0 || index >= m_project.bookmarks().size())
        return;

    const QColor resolved = color.isEmpty() || !QColor::isValidColorName(color) ? QColor()
                                                                               : QColor(color);
    // Snapshot before binding the reference: bookmarks are a plain QList, so a later copy would
    // share the buffer the reference writes into.
    const drift::Project before = m_project;
    drift::Bookmark &bookmark = m_project.bookmarks()[index];
    if (bookmark.color == resolved)
        return;

    bookmark.color = resolved;
    pushProjectEdit(before, tr("Colour bookmark"));
    finishEdit(tr("Bookmark colour changed"));
}

void AppController::goToBookmark(int index)
{
    if (index < 0 || index >= m_project.bookmarks().size())
        return;
    setPlayheadUs(m_project.bookmarks().at(index).timeUs);
}
namespace {


int nearestBookmarkIndex(const QList<drift::Bookmark> &bookmarks, drift::TimeUs timeUs,
                         drift::TimeUs maxDistanceUs)
{
    int bestIndex = -1;
    drift::TimeUs bestDistance = maxDistanceUs;
    for (int i = 0; i < bookmarks.size(); ++i) {
        const drift::TimeUs distance = qAbs(bookmarks.at(i).timeUs - timeUs);
        if (distance <= bestDistance) {
            bestDistance = distance;
            bestIndex = i;
        }
    }
    return bestIndex;
}

int nextBookmarkIndex(const QList<drift::Bookmark> &bookmarks, drift::TimeUs playheadUs)
{
    int bestIndex = -1;
    drift::TimeUs bestTime = 0;
    int earliestIndex = -1;
    drift::TimeUs earliestTime = 0;
    for (int i = 0; i < bookmarks.size(); ++i) {
        const drift::TimeUs timeUs = bookmarks.at(i).timeUs;
        if (earliestIndex < 0 || timeUs < earliestTime) {
            earliestIndex = i;
            earliestTime = timeUs;
        }
        if (timeUs <= playheadUs)
            continue;
        if (bestIndex < 0 || timeUs < bestTime) {
            bestIndex = i;
            bestTime = timeUs;
        }
    }
    return bestIndex >= 0 ? bestIndex : earliestIndex;
}

int previousBookmarkIndex(const QList<drift::Bookmark> &bookmarks, drift::TimeUs playheadUs)
{
    int bestIndex = -1;
    drift::TimeUs bestTime = 0;
    int latestIndex = -1;
    drift::TimeUs latestTime = 0;
    for (int i = 0; i < bookmarks.size(); ++i) {
        const drift::TimeUs timeUs = bookmarks.at(i).timeUs;
        if (latestIndex < 0 || timeUs > latestTime) {
            latestIndex = i;
            latestTime = timeUs;
        }
        if (timeUs >= playheadUs)
            continue;
        if (bestIndex < 0 || timeUs > bestTime) {
            bestIndex = i;
            bestTime = timeUs;
        }
    }
    return bestIndex >= 0 ? bestIndex : latestIndex;
}

// Nearest cut point strictly after (direction > 0) or strictly before (direction < 0)
// `fromUs`, or `fromUs` itself when there is none. The candidates are every clip edge on
// every track plus both ends of the timeline — the same set snapTime() collects, so
// walking the cuts by keyboard stops exactly where a dragged clip would snap. Clips are
// not stored in time order, so this compares values rather than trusting indices.
drift::TimeUs adjacentEditPoint(const drift::Project &project, drift::TimeUs fromUs, int direction)
{
    drift::TimeUs best = fromUs;
    bool found = false;

    auto consider = [&](drift::TimeUs candidate) {
        if (direction > 0 ? candidate <= fromUs : candidate >= fromUs)
            return;
        if (!found || (direction > 0 ? candidate < best : candidate > best)) {
            best = candidate;
            found = true;
        }
    };

    consider(0);
    consider(project.durationUs());
    for (const drift::Track &track : project.tracks()) {
        for (const drift::Clip &clip : track.clips) {
            consider(clip.timelineStart);
            consider(clip.timelineEnd());
        }
    }
    return found ? best : fromUs;
}

} // namespace

void AppController::goToNextBookmark()
{
    const int index = nextBookmarkIndex(m_project.bookmarks(), m_playheadUs);
    if (index >= 0)
        goToBookmark(index);
}

void AppController::goToPreviousBookmark()
{
    const int index = previousBookmarkIndex(m_project.bookmarks(), m_playheadUs);
    if (index >= 0)
        goToBookmark(index);
}

void AppController::goToNextEdit()
{
    setPlayheadUs(adjacentEditPoint(m_project, m_playheadUs, 1));
}

void AppController::goToPreviousEdit()
{
    setPlayheadUs(adjacentEditPoint(m_project, m_playheadUs, -1));
}

bool AppController::crossesEditPoint(double fromSeconds, double toSeconds) const
{
    const drift::TimeUs from = drift::secondsToUs(std::min(fromSeconds, toSeconds));
    const drift::TimeUs to = drift::secondsToUs(std::max(fromSeconds, toSeconds));
    if (to <= from)
        return false;
    const drift::TimeUs next = adjacentEditPoint(m_project, from, 1);
    if (next > from && next <= to)
        return true;
    for (const auto &mark : m_project.bookmarks()) {
        if (mark.timeUs > from && mark.timeUs <= to)
            return true;
    }
    return false;
}

void AppController::toggleBookmarkAtPlayhead()
{
    // The timeline's 150 ms snapping radius is much wider than a frame. Using it here
    // deletes a nearby, distinct bookmark when the user is trying to add another one.
    // Toggle only a bookmark on the same project frame as the playhead.
    const int fps = m_projectFile->projectFps();
    const qint64 playheadFrame = qRound64(drift::usToSeconds(m_playheadUs) * fps);
    const auto &marks = m_project.bookmarks();
    for (int i = 0; i < marks.size(); ++i) {
        if (qRound64(drift::usToSeconds(marks.at(i).timeUs) * fps) == playheadFrame) {
            removeBookmark(i);
            return;
        }
    }

    const int markNumber = m_project.bookmarks().size() + 1;
    addBookmark(playheadSeconds(), QStringLiteral("Mark %1").arg(markNumber));
}

void AppController::removeBookmarkNearPlayhead()
{
    const int near = nearestBookmarkIndex(m_project.bookmarks(), m_playheadUs,
                                          drift::kSnapThresholdUs);
    if (near >= 0)
        removeBookmark(near);
}

// Freeze frames are project media, not cache: they live under the project's own media dir so a
// save bundles them and a cache sweep can't delete the file a saved clip points at.
QString AppController::projectMediaDir(const QString &projectId)
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString key = drift::projectPathKey(projectId);
    if (base.isEmpty() || key.isEmpty())
        return {};
    const QString dir = QDir(base).filePath(QStringLiteral("projects/%1/media").arg(key));
    return QDir().mkpath(dir) ? dir : QString();
}

QString AppController::newFreezeFramePath(const QString &projectId)
{
    const QString dir = projectMediaDir(projectId);
    if (dir.isEmpty())
        return {};

    return QDir(dir).filePath(QStringLiteral("freeze-%1.png")
                                  .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
}

void AppController::previewClipRange(int trackIndex, int clipIndex)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;
    const drift::Track &track = m_project.tracks().at(trackIndex);
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;
    const drift::Clip &clip = track.clips.at(clipIndex);
    const drift::TimeUs startUs = clip.timelineStart;
    const drift::TimeUs endUs = clip.timelineStart + clip.timelineDuration;
    m_playback.setPreviewRange(startUs, endUs);
    if (m_playheadUs < startUs || m_playheadUs >= endUs)
        setPlayheadUs(startUs);
}

void AppController::clearPreviewRange()
{
    m_playback.clearPreviewRange();
}

QVariantMap AppController::clipTimelineRange(int track, int clip) const
{
    if (track < 0 || track >= m_project.tracks().size())
        return {};
    const drift::Track &t = m_project.tracks().at(track);
    if (clip < 0 || clip >= t.clips.size())
        return {};
    const drift::Clip &c = t.clips.at(clip);
    return {{QStringLiteral("startSeconds"), drift::usToSeconds(c.timelineStart)},
            {QStringLiteral("endSeconds"), drift::usToSeconds(c.timelineStart + c.timelineDuration)}};
}

void AppController::freezeFrameAtPlayhead()
{
    if (!m_assetLibrary)
        return;

    if (activeVideoClipAtPlayhead().isEmpty()) {
        setLastMessage(tr("No video at the current time"), QStringLiteral("warning"));
        return;
    }

    const QString outPath = newFreezeFramePath(m_project.id());
    if (outPath.isEmpty()) {
        setLastMessage(tr("Couldn’t capture a still frame"), QStringLiteral("error"));
        return;
    }

    // Compositing here drives the shared decoder pool, which playback is also driving with its
    // own set of retained paths and its own read-ahead. Same reason export stops playback first.
    setPlaying(false);

    setLastMessage(tr("Capturing freeze frame…"));
    const drift::TimeUs playheadUs = m_playheadUs;
    // The worker composites off the GUI thread while editing continues, so it gets a detached
    // copy rather than a pointer into the live project.
    const auto snapshot = std::make_shared<const drift::Project>(m_project.detachedCopy());

    (void)QtConcurrent::run([this, snapshot, playheadUs, outPath]() {
        FrameCompositor compositor;
        compositor.setProject(snapshot.get());

        // Full canvas scale: this is the finished frame — every track, effect, transition and
        // overlay — not a preview of one clip's source.
        const QImage frame = compositor.compositeAt(playheadUs);
        const bool ok = !frame.isNull() && frame.save(outPath, "PNG");
        const QString thumb =
            ok ? MediaThumbnail::generate(outPath, drift::mediaKindToString(drift::MediaKind::Image))
               : QString();
        const QSize size = frame.size();

        QMetaObject::invokeMethod(
            this,
            [this, ok, outPath, thumb, size, playheadUs]() {
                if (!ok) {
                    setLastMessage(tr("Couldn’t capture a still frame"),
                                   QStringLiteral("error"));
                    return;
                }

                const drift::Project before = m_project;

                drift::MediaAsset asset;
                asset.name = QStringLiteral("Freeze frame");
                asset.path = outPath;
                asset.kind = drift::MediaKind::Image;
                asset.width = size.width();
                asset.height = size.height();
                asset.thumbnailPath = thumb;
                asset.filmstripPath = thumb;
                asset.hasAudio = false;
                asset.hasAudioKnown = true;
                const QString assetId = m_assetLibrary->addGeneratedAsset(asset);

                // Inserted at the top: track 0 composites in front, so a freeze frame on a freshly
                // appended track would sit behind the video it was captured from and show nothing.
                const int trackIndex = drift::ensureTrackForClipType(m_project, drift::ClipType::Image, true);

                drift::Track &track = m_project.tracks()[trackIndex];
                const drift::TimeUs start = drift::resolveClipStart(m_project, track, -1, playheadUs,
                                                                    drift::imageClipDurationUs(), m_snapEnabled,
                                                                    playheadUs);

                drift::Clip freezeClip;
                freezeClip.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
                freezeClip.assetId = assetId;
                freezeClip.type = drift::ClipType::Image;
                freezeClip.name = QStringLiteral("Freeze frame");
                freezeClip.path = outPath;
                freezeClip.thumbnailPath = thumb;
                freezeClip.filmstripPath = thumb;
                freezeClip.timelineStart = start;
                freezeClip.timelineDuration = drift::imageClipDurationUs();
                freezeClip.srcIn = 0;
                freezeClip.srcOut = drift::imageClipDurationUs();
                fitClipLayoutToCanvas(freezeClip, size.width(), size.height(), m_project.width(),
                                      m_project.height());

                track.clips.append(freezeClip);
                const int newClipIndex = track.clips.size() - 1;
                pushProjectEdit(before, tr("Freeze frame added"));
                finishEdit(tr("Freeze frame added"));
                selectClip(trackIndex, newClipIndex);
            },
            Qt::QueuedConnection);
    });
}

void AppController::copySelection()
{
    m_clipboard.clear();
    QList<QPair<int, int>> pairs = m_selection;
    if (pairs.isEmpty() && m_selectedTrack >= 0 && m_selectedClip >= 0)
        pairs.append(qMakePair(m_selectedTrack, m_selectedClip));
    for (const QPair<int, int> &pair : pairs) {
        if (!isValidClipIndex(pair.first, pair.second))
            continue;
        ClipboardItem item;
        item.clip = m_project.tracks().at(pair.first).clips.at(pair.second);
        item.trackType = m_project.tracks().at(pair.first).type;
        for (const drift::Transition &tr : m_project.tracks().at(pair.first).transitions) {
            if (tr.fromClipId == item.clip.id || tr.toClipId == item.clip.id)
                item.transitions.append(tr);
        }
        for (const drift::ClipRef &ref :
             drift::linkedMaskAdjustments(m_project, pair.first, pair.second)) {
            item.masks.append(m_project.tracks().at(ref.trackIndex).clips.at(ref.clipIndex).mask);
        }
        m_clipboard.append(item);
    }
}

void AppController::cutSelection()
{
    copySelection();
    deleteSelectedClip();
}

void AppController::pasteAtPlayhead()
{
    if (m_clipboard.isEmpty())
        return;
    const drift::Project before = m_project;
    drift::TimeUs anchor = LLONG_MAX;
    for (const ClipboardItem &item : m_clipboard)
        anchor = qMin(anchor, item.clip.timelineStart);
    const drift::TimeUs shift = m_playheadUs - anchor;
    // By id: a later clip can insert a track above an earlier one, and normalizing the edit can
    // reorder tracks, so positions taken here go stale.
    QStringList inserted;
    // Pasted clips stay linked to each other under a fresh id, never to the clips they were
    // copied from; one pasted without any of its partners comes in unlinked.
    QHash<QString, int> linkCounts;
    for (const ClipboardItem &item : m_clipboard) {
        if (!item.clip.linkId.isEmpty())
            ++linkCounts[item.clip.linkId];
    }
    QHash<QString, QString> pastedLinkIds;
    // An extracted audio clip follows its video to the pasted copy; one pasted without it loses
    // the reference rather than pointing at the original.
    QHash<QString, QString> pastedClipIds;

    for (const ClipboardItem &item : m_clipboard) {
        // Composites live on the main timeline only, and never outlive their sequence.
        if (!item.clip.sequenceId.isEmpty()
            && (!m_project.activeSequenceId().isEmpty() || !m_project.hasSequence(item.clip.sequenceId)))
            continue;
        drift::Clip clip = item.clip;
        clip.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        pastedClipIds.insert(item.clip.id, clip.id);
        clip.timelineStart = qMax<drift::TimeUs>(0, clip.timelineStart + shift);
        if (linkCounts.value(clip.linkId) > 1) {
            auto it = pastedLinkIds.find(clip.linkId);
            if (it == pastedLinkIds.end())
                it = pastedLinkIds.insert(clip.linkId, newClipId());
            clip.linkId = *it;
        } else {
            clip.linkId.clear();
            clip.linkLoose = false;
        }

        int targetTrack = -1;
        for (int i = 0; i < m_project.tracks().size(); ++i) {
            if (m_project.tracks().at(i).type == item.trackType && m_project.tracks().at(i).acceptsClip(clip)) {
                targetTrack = i;
                break;
            }
        }
        if (targetTrack < 0 && clip.type == drift::ClipType::Adjustment
            && clip.adjustmentKind == drift::AdjustmentKind::Transform) {
            QString lowest;
            for (const drift::Track &t : m_project.tracks()) {
                if (drift::isTransformableTrack(t))
                    lowest = t.id;
            }
            targetTrack = drift::insertTransformTrack(m_project.tracks(), 0, lowest);
        }
        if (targetTrack < 0)
            targetTrack = drift::ensureTrackForClipType(m_project, clip.type, true);
        if (targetTrack < 0 || !m_project.tracks()[targetTrack].acceptsClip(clip))
            continue;
        m_project.tracks()[targetTrack].clips.append(clip);
        inserted.append(clip.id);
    }

    if (inserted.isEmpty())
        return;
    // Second pass: the audio can sit before its video in the clipboard, and a video that was
    // skipped above has no copy to point at.
    const QSet<QString> insertedIds(inserted.begin(), inserted.end());
    for (const QString &id : std::as_const(inserted)) {
        int trackIndex = -1;
        int clipIndex = -1;
        if (!findClipById(m_project, id, &trackIndex, &clipIndex))
            continue;
        QString &source = m_project.tracks()[trackIndex].clips[clipIndex].sourceVideoClipId;
        if (source.isEmpty())
            continue;
        const QString mapped = pastedClipIds.value(source);
        source = insertedIds.contains(mapped) ? mapped : QString();
    }
    pushProjectEdit(before, tr("Paste"));
    m_selection.clear();
    for (const QString &id : inserted) {
        int trackIndex = -1;
        int clipIndex = -1;
        if (findClipById(m_project, id, &trackIndex, &clipIndex))
            m_selection.append(qMakePair(trackIndex, clipIndex));
    }
    if (!m_selection.isEmpty()) {
        m_selectedTrack = m_selection.constLast().first;
        m_selectedClip = m_selection.constLast().second;
    }
    finishEdit(tr("Pasted %n clips", "", int(inserted.size())));
}

void AppController::nudgeSelection(double deltaSeconds)
{
    if (qFuzzyIsNull(deltaSeconds))
        return;
    QList<QPair<int, int>> pairs = m_selection;
    if (pairs.isEmpty() && m_selectedTrack >= 0 && m_selectedClip >= 0)
        pairs.append(qMakePair(m_selectedTrack, m_selectedClip));
    if (pairs.isEmpty())
        return;
    const drift::Project before = m_project;
    const drift::TimeUs deltaUs = drift::secondsToUs(deltaSeconds);

    drift::TimeUs minGroupStartUs = -1;
    for (const QPair<int, int> &pair : pairs) {
        if (isValidClipIndex(pair.first, pair.second)) {
            const drift::TimeUs s = m_project.tracks().at(pair.first).clips.at(pair.second).timelineStart;
            if (minGroupStartUs < 0 || s < minGroupStartUs)
                minGroupStartUs = s;
        }
    }
    drift::TimeUs clampedDeltaUs = deltaUs;
    if (clampedDeltaUs < 0 && minGroupStartUs >= 0 && -clampedDeltaUs > minGroupStartUs) {
        clampedDeltaUs = -minGroupStartUs;
    }

    QSet<QString> movedIds;
    for (const QPair<int, int> &pair : pairs) {
        if (!isValidClipIndex(pair.first, pair.second))
            continue;
        drift::Clip &clip = m_project.tracks()[pair.first].clips[pair.second];
        clip.timelineStart = qMax<drift::TimeUs>(0, clip.timelineStart + clampedDeltaUs);
        movedIds.insert(clip.id);
    }
    if (!m_allowClipOverlap) {
        QList<drift::PlacedSpan> spans;
        for (const QPair<int, int> &pair : pairs) {
            if (!isValidClipIndex(pair.first, pair.second))
                continue;
            const drift::Track &track = m_project.tracks().at(pair.first);
            const drift::Clip &clip = track.clips.at(pair.second);
            spans.append({&track, clip.timelineStart, clip.timelineDuration});
        }
        const std::optional<drift::TimeUs> shift = drift::groupShiftNoOverlap(spans, movedIds);
        if (!shift) {
            m_project = before;
            setLastMessage(tr("No space to place the clip there"), QStringLiteral("warning"));
            return;
        }
        if (*shift != 0) {
            for (const QPair<int, int> &pair : pairs) {
                if (!isValidClipIndex(pair.first, pair.second))
                    continue;
                m_project.tracks()[pair.first].clips[pair.second].timelineStart += *shift;
            }
        }
    }
    if (editCreatesCompleteOverlap(before, m_project)) {
        m_project = before;
        setLastMessage(tr("A clip can't completely cover another"), QStringLiteral("warning"));
        return;
    }
    pushProjectEdit(before, tr("Nudge selection"));
    finishEdit(tr("Selection nudged"));
}

void AppController::syncSelectionMirror() const
{
    if (m_selectionMirrorSource.constData() == std::as_const(m_selection).constData()
        && m_selectionMirrorSource.size() == m_selection.size())
        return;
    m_selectionMirrorSource = m_selection;
    m_selectionSet.clear();
    m_selectionTracks.clear();
    for (const QPair<int, int> &pair : std::as_const(m_selectionMirrorSource)) {
        m_selectionSet.insert(pair);
        m_selectionTracks.insert(pair.first);
    }
}

bool AppController::selectionContains(int trackIndex, int clipIndex) const
{
    syncSelectionMirror();
    return m_selectionSet.contains(qMakePair(trackIndex, clipIndex));
}

bool AppController::selectionContainsTrack(int trackIndex) const
{
    syncSelectionMirror();
    return m_selectionTracks.contains(trackIndex);
}

double AppController::selectionEarliestStartSeconds() const
{
    drift::TimeUs minStart = -1;
    for (const QPair<int, int> &pair : m_selection) {
        if (!isValidClipIndex(pair.first, pair.second))
            continue;
        const drift::TimeUs s = m_project.tracks().at(pair.first).clips.at(pair.second).timelineStart;
        if (minStart < 0 || s < minStart)
            minStart = s;
    }
    return minStart >= 0 ? drift::usToSeconds(minStart) : 0.0;
}

void AppController::setTimelineCursor(const QString &kindName, int heightPx, int owner)
{
    heightPx = qMax(0, heightPx);
    TimelineCursorKind kind = TimelineCursorKind::Move;
    const bool clear = kindName.isEmpty() || !timelineCursorKindFromName(kindName, &kind);

    if (clear) {
        // Only whoever put the cursor up may take it down. Clip delegates sit edge to edge, so
        // crossing from one handle to its neighbour raises the new one before the old one clears
        // — and without this that stale clear would strip the cursor that had just been set.
        if (owner != 0 && m_timelineCursorOwner != 0 && owner != m_timelineCursorOwner)
            return;
        if (m_timelineCursorActive)
            QGuiApplication::restoreOverrideCursor();
        m_timelineCursorActive = false;
        m_timelineCursorKind.clear();
        m_timelineCursorHeight = 0;
        m_timelineCursorOwner = 0;
        return;
    }

    if (m_timelineCursorActive && kindName == m_timelineCursorKind
        && heightPx == m_timelineCursorHeight && owner == m_timelineCursorOwner) {
        return;
    }

    // The override stack stays exactly one deep: pop whatever is up before pushing.
    if (m_timelineCursorActive)
        QGuiApplication::restoreOverrideCursor();

    const QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    m_timelineCursorActive = true;
    m_timelineCursorKind = kindName;
    m_timelineCursorHeight = heightPx;
    m_timelineCursorOwner = owner;
    QGuiApplication::setOverrideCursor(
        timelineCursor(kind, heightPx > 0 ? heightPx : 28, screen ? screen->devicePixelRatio() : 1.0));
}

QString AppController::shortcutFor(const QString &actionId) const
{
    return m_keymap->bindings(actionId).value(0);
}

QString AppController::setShortcut(const QString &actionId, const QString &keys)
{
    if (keys.isEmpty())
        return m_keymap->setBindings(actionId, {});
    if (drift::Keymap::normalizeChord(keys).isEmpty())
        return tr("Not a valid key combination");
    return m_keymap->setBindings(actionId, {keys});
}

void AppController::resetShortcuts()
{
    m_keymap->resetAll();
}

QString AppController::shortcutChord(int key, int modifiers) const
{
    return m_keymap->chordFromKey(key, modifiers);
}

QString AppController::actionForArrowChord(int key, int modifiers) const
{
    if (key != Qt::Key_Left && key != Qt::Key_Right && key != Qt::Key_Up && key != Qt::Key_Down)
        return {};
    return m_keymap->actionFor(QLatin1String(drift::keymap::kGlobalScope), shortcutChord(key, modifiers));
}

void AppController::loadAssetFavorites()
{
    m_assetFavorites.clear();
    QSettings settings;
    settings.beginGroup(QStringLiteral("assetFavorites"));
    const QStringList keys = settings.allKeys();
    for (const QString &tabId : keys) {
        const QStringList ids = settings.value(tabId).toStringList();
        if (!ids.isEmpty())
            m_assetFavorites.insert(tabId, QSet<QString>(ids.cbegin(), ids.cend()));
    }
    settings.endGroup();
}

void AppController::saveAssetFavorites(const QString &tabId)
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("assetFavorites"));
    const QSet<QString> ids = m_assetFavorites.value(tabId);
    if (ids.isEmpty())
        settings.remove(tabId);
    else
        settings.setValue(tabId, QStringList(ids.cbegin(), ids.cend()));
    settings.endGroup();
}

bool AppController::isAssetFavorite(const QString &tabId, const QString &itemId) const
{
    return m_assetFavorites.value(tabId).contains(itemId);
}

void AppController::toggleAssetFavorite(const QString &tabId, const QString &itemId)
{
    if (tabId.isEmpty() || itemId.isEmpty())
        return;
    QSet<QString> &ids = m_assetFavorites[tabId];
    if (ids.contains(itemId))
        ids.remove(itemId);
    else
        ids.insert(itemId);
    if (ids.isEmpty())
        m_assetFavorites.remove(tabId);
    saveAssetFavorites(tabId);
    emit assetFavoritesChanged();
}

void AppController::triggerAction(const QString &actionId)
{
    // File ops need QML (dialogs + unsaved-change confirm). Emit so the header
    // can gate them the same way the project menu does.
    if (actionId == QStringLiteral("newProject"))
        emit m_projectFile->newProjectRequested();
    else if (actionId == QStringLiteral("open"))
        emit m_projectFile->openRequested();
    else if (actionId == QStringLiteral("save"))
        emit m_projectFile->saveRequested();
    else if (actionId == QStringLiteral("saveAs"))
        emit m_projectFile->saveAsRequested();
    else if (actionId == QStringLiteral("playPause"))
        togglePlayback();
    else if (actionId == QStringLiteral("multicam"))
        emit openMulticamWindowRequested();
    else if (actionId == QStringLiteral("delete"))
        deleteSelectedClip();
    else if (actionId == QStringLiteral("deleteWithEffects"))
        deleteSelectedClipWithEffects();
    else if (actionId == QStringLiteral("undo"))
        undo();
    else if (actionId == QStringLiteral("redo"))
        redo();
    else if (actionId == QStringLiteral("clearSelection"))
        clearSelection();
    else if (actionId == QStringLiteral("selectAll"))
        selectAllClips();
    else if (actionId == QStringLiteral("duplicate"))
        duplicateSelectedClip();
    else if (actionId == QStringLiteral("transformTogether"))
        addTransformLayerForSelection();
    else if (actionId == QStringLiteral("selectTransformLayer"))
        selectTransformParent();
    else if (actionId == QStringLiteral("copyEffects"))
        copyClipEffectsToClipboard(m_selectedTrack, m_selectedClip);
    else if (actionId == QStringLiteral("pasteEffects"))
        pasteEffectsFromClipboard(m_selectedTrack, m_selectedClip);
    else if (actionId == QStringLiteral("pasteAttributes"))
        requestPasteAttributes();
    else if (actionId == QStringLiteral("split"))
        splitAtPlayheadForSelection();
    else if (actionId == QStringLiteral("splitAllTracks"))
        splitAtPlayhead();
    else if (actionId == QStringLiteral("toggleClipEnabled"))
        toggleSelectedClipsEnabled();
    else if (actionId == QStringLiteral("splitSelection"))
        splitSelectionAtPlayhead();
    else if (actionId == QStringLiteral("addTransitionAtPlayhead"))
        addTransitionAtPlayhead();
    else if (actionId == QStringLiteral("merge"))
        mergeSelectedClips();
    else if (actionId == QStringLiteral("separateAudio"))
        separateAudioFromSelection();
    else if (actionId == QStringLiteral("unlink"))
        unlinkSelectedClips();
    else if (actionId == QStringLiteral("link"))
        linkSelectedClips();
    else if (actionId == QStringLiteral("copy"))
        copySelection();
    else if (actionId == QStringLiteral("cut"))
        cutSelection();
    else if (actionId == QStringLiteral("paste"))
        pasteAtPlayhead();
    else if (actionId == QStringLiteral("nudgeLeft"))
        nudgeSelection(-0.1);
    else if (actionId == QStringLiteral("nudgeRight"))
        nudgeSelection(0.1);
    else if (actionId == QStringLiteral("toggleGuides"))
        m_preview->setGuidesEnabled(!m_preview->guidesEnabled());
    else if (actionId == QStringLiteral("toggleTrackHidden"))
        setTrackHidden(m_selectedTrack, !trackHidden(m_selectedTrack));
    else if (actionId == QStringLiteral("addSpeedPoint"))
        addSpeedPointAtPlayhead();
    else if (actionId == QStringLiteral("toggleBookmark"))
        toggleBookmarkAtPlayhead();
    else if (actionId == QStringLiteral("nextBookmark"))
        goToNextBookmark();
    else if (actionId == QStringLiteral("previousBookmark"))
        goToPreviousBookmark();
    else if (actionId == QStringLiteral("markIn"))
        markWorkAreaIn();
    else if (actionId == QStringLiteral("markOut"))
        markWorkAreaOut();
    else if (actionId == QStringLiteral("goToIn"))
        goToWorkAreaIn();
    else if (actionId == QStringLiteral("goToOut"))
        goToWorkAreaOut();
    else if (actionId == QStringLiteral("clearInOut"))
        clearWorkArea();
    else if (actionId == QStringLiteral("toggleLoop"))
        toggleLoopWorkArea();
    else if (actionId == QStringLiteral("previousEdit"))
        goToPreviousEdit();
    else if (actionId == QStringLiteral("nextEdit"))
        goToNextEdit();
    else if (actionId == QStringLiteral("stepBack"))
        stepFrames(-1);
    else if (actionId == QStringLiteral("stepForward"))
        stepFrames(1);
    else if (actionId == QStringLiteral("jumpBack"))
        jumpSeconds(-1.0);
    else if (actionId == QStringLiteral("jumpForward"))
        jumpSeconds(1.0);
    else if (actionId == QStringLiteral("jumpBackFar"))
        jumpSeconds(-10.0);
    else if (actionId == QStringLiteral("jumpForwardFar"))
        jumpSeconds(10.0);
    else if (actionId == QStringLiteral("goToStart")) {
        setPlayheadUs(0);
        emit playheadJumped();
    } else if (actionId == QStringLiteral("goToEnd")) {
        setPlayheadUs(m_project.durationUs());
        emit playheadJumped();
    }
    else if (actionId == QStringLiteral("deleteLeft"))
        splitSelectedClipLeft();
    else if (actionId == QStringLiteral("deleteRight"))
        splitSelectedClipRight();
    else if (actionId == QStringLiteral("speedUp")) {
        m_playback.stepPlaybackRate(1);
        // Reviewing long footage is the whole point of the key, so from a stopped transport
        // the first press starts rolling rather than only arming a rate for later. This has
        // to follow the step: setPlaybackRate restarts the transport itself when it was
        // already playing, and would otherwise undo the resume.
        setPlaying(true);
    } else if (actionId == QStringLiteral("speedDown"))
        m_playback.stepPlaybackRate(-1);
    else if (actionId == QStringLiteral("stop")) {
        setPlaying(false);
        m_playback.setPlaybackRate(1.0);
    } else if (actionId == QStringLiteral("stepBack5"))
        stepFrames(-5);
    else if (actionId == QStringLiteral("stepForward5"))
        stepFrames(5);
    else if (actionId == QStringLiteral("rippleDelete"))
        rippleDeleteSelection();
    else if (actionId == QStringLiteral("deselectAll"))
        clearSelection();
    else if (actionId == QStringLiteral("selectClipAtPlayhead"))
        selectClipUnderPlayhead();
    else if (actionId == QStringLiteral("selectNextClip"))
        selectAdjacentClip(true);
    else if (actionId == QStringLiteral("selectPreviousClip"))
        selectAdjacentClip(false);
    else if (actionId == QStringLiteral("removeAllEffects"))
        removeAllEffects(m_selectedTrack, m_selectedClip);
    else if (actionId == QStringLiteral("moveToOwnTrack"))
        moveAdjustmentToOwnTrack(m_selectedTrack, m_selectedClip);
    else if (actionId == QStringLiteral("swapWithPrevious"))
        swapWithNeighbour(m_selectedTrack, m_selectedClip, false);
    else if (actionId == QStringLiteral("swapWithNext"))
        swapWithNeighbour(m_selectedTrack, m_selectedClip, true);
    else if (actionId == QStringLiteral("makeComposite")) {
        if (canMakeCompositeFromSelection())
            makeCompositeFromSelection();
    } else if (actionId == QStringLiteral("openComposite")) {
        if (isValidClipIndex(m_selectedTrack, m_selectedClip)
            && m_project.tracks().at(m_selectedTrack).clips.at(m_selectedClip).type == drift::ClipType::Composite)
            openCompositeClip(m_selectedTrack, m_selectedClip);
    } else if (actionId == QStringLiteral("flattenComposite")) {
        if (isValidClipIndex(m_selectedTrack, m_selectedClip) && !exportInProgress()
            && m_project.tracks().at(m_selectedTrack).clips.at(m_selectedClip).type == drift::ClipType::Composite)
            flattenComposite(m_selectedTrack, m_selectedClip);
    } else if (actionId == QStringLiteral("freezeFrame"))
        freezeFrameAtPlayhead();
    else if (actionId == QStringLiteral("nudgeLeftFar"))
        nudgeSelection(-1.0);
    else if (actionId == QStringLiteral("nudgeRightFar"))
        nudgeSelection(1.0);
    else if (actionId == QStringLiteral("reverseClip")) {
        if (isValidClipIndex(m_selectedTrack, m_selectedClip)) {
            if (m_project.tracks().at(m_selectedTrack).clips.at(m_selectedClip).reverse)
                setClipReverse(m_selectedTrack, m_selectedClip, false);
            else
                requestClipReverse(m_selectedTrack, m_selectedClip);
        }
    } else if (actionId == QStringLiteral("splitOnBeats")) {
        if (beatsAvailable() && isValidClipIndex(m_selectedTrack, m_selectedClip)
            && m_project.tracks().at(m_selectedTrack).clips.at(m_selectedClip).type != drift::ClipType::Subtitle)
            splitSelectionOnBeats(m_selectedTrack, m_selectedClip, QStringLiteral("beat"));
    } else if (actionId == QStringLiteral("removeSilence")) {
        if (m_audioAlign->canRemoveSilence(m_selectedTrack, m_selectedClip) && !m_audioAlign->running())
            m_audioAlign->removeSilence(m_selectedTrack, m_selectedClip);
    } else if (actionId == QStringLiteral("syncByAudio")) {
        if (m_audioAlign->canSyncByAudio() && !m_audioAlign->running())
            m_audioAlign->syncByAudio();
    } else if (actionId == QStringLiteral("markClip"))
        markWorkAreaToClip();
    else if (actionId == QStringLiteral("clearIn"))
        clearWorkAreaIn();
    else if (actionId == QStringLiteral("clearOut"))
        clearWorkAreaOut();
    else if (actionId == QStringLiteral("deleteBookmark"))
        removeBookmarkNearPlayhead();
    else if (actionId == QStringLiteral("deleteAllBookmarks"))
        clearBookmarks();
    else if (actionId == QStringLiteral("toggleSnapping"))
        setSnapEnabled(!m_snapEnabled);
    else if (actionId == QStringLiteral("toggleRipple"))
        setRippleEnabled(!m_rippleEnabled);
    else if (actionId == QStringLiteral("toggleOverlap"))
        setAllowClipOverlap(!m_allowClipOverlap);
    else if (actionId == QStringLiteral("toggleAutoSelect"))
        m_preferences->setAutoSelectUnderPlayhead(!m_preferences->autoSelectUnderPlayhead());
    else if (actionId == QStringLiteral("toggleMixerStrip"))
        m_preferences->setAudioMixerVisible(!m_preferences->audioMixerVisible());
    else if (actionId == QStringLiteral("toggleKeyframeStrip"))
        m_preferences->setKeyframeStripVisible(!m_preferences->keyframeStripVisible());
    else if (actionId == QStringLiteral("toggleOverview"))
        m_preferences->setTimelineOverviewVisible(!m_preferences->timelineOverviewVisible());
    else if (actionId == QStringLiteral("toggleTrackMuted"))
        setTrackMuted(m_selectedTrack, !trackMuted(m_selectedTrack));
    else if (actionId == QStringLiteral("toggleTrackSolo"))
        setTrackSolo(m_selectedTrack, !trackSolo(m_selectedTrack));
    else if (actionId == QStringLiteral("toggleTrackLocked"))
        setTrackLocked(m_selectedTrack, !trackLocked(m_selectedTrack));
    else if (actionId == QStringLiteral("addVideoTrack"))
        addTrack(QStringLiteral("video"));
    else if (actionId == QStringLiteral("addAudioTrack"))
        addTrack(QStringLiteral("audio"));
    else if (actionId == QStringLiteral("trackTaller"))
        nudgeTrackHeightScale(m_selectedTrack, 1);
    else if (actionId == QStringLiteral("trackShorter"))
        nudgeTrackHeightScale(m_selectedTrack, -1);
    else if (actionId == QStringLiteral("addAdjustmentLayer"))
        addAdjustmentClip(-1, -1);
    else if (actionId == QStringLiteral("addCamera"))
        addCameraTrack();
    else if (actionId == QStringLiteral("addText"))
        addTextClip(QString(), -1);
    else if (actionId == QStringLiteral("recordVoiceover")) {
        if (isRecordingAudio())
            stopAudioRecording();
        else
            startAudioRecording();
    }
}

void AppController::markWorkAreaToClip()
{
    if (!isValidClipIndex(m_selectedTrack, m_selectedClip)) {
        selectClipUnderPlayhead();
        if (!isValidClipIndex(m_selectedTrack, m_selectedClip))
            return;
    }
    const drift::Clip &clip = m_project.tracks().at(m_selectedTrack).clips.at(m_selectedClip);
    const drift::TimeUs start = clip.timelineStart;
    const drift::TimeUs end = clip.timelineEnd();
    if (end <= start)
        return;
    const drift::Project before = m_project;
    m_project.setWorkAreaInUs(start);
    m_project.setWorkAreaOutUs(end);
    pushProjectEdit(before, tr("Mark work area to clip"));
    finishEdit(tr("Work area set to clip"));
    emit workAreaChanged();
}

void AppController::clearWorkAreaIn()
{
    if (m_project.workAreaInUs() < 0)
        return;
    const drift::Project before = m_project;
    m_project.setWorkAreaInUs(-1);
    pushProjectEdit(before, tr("Clear work area in"));
    finishEdit(tr("Work area in cleared"));
    emit workAreaChanged();
}

void AppController::clearWorkAreaOut()
{
    if (m_project.workAreaOutUs() < 0)
        return;
    const drift::Project before = m_project;
    m_project.setWorkAreaOutUs(-1);
    pushProjectEdit(before, tr("Clear work area out"));
    finishEdit(tr("Work area out cleared"));
    emit workAreaChanged();
}

void AppController::clearBookmarks()
{
    if (m_project.bookmarks().isEmpty())
        return;
    const drift::Project before = m_project;
    m_project.bookmarks().clear();
    pushProjectEdit(before, tr("Remove all bookmarks"));
    finishEdit(tr("Bookmarks removed"));
}

void AppController::rippleDeleteSelection()
{
    const bool ripple = m_rippleEnabled;
    m_rippleEnabled = true;
    deleteSelection(/*withPinnedAdjustments=*/false);
    m_rippleEnabled = ripple;
}

void AppController::selectAdjacentClip(bool next)
{
    if (!isValidClipIndex(m_selectedTrack, m_selectedClip)) {
        selectClipUnderPlayhead();
        return;
    }
    const QString id = neighbourClipId(m_selectedTrack, m_selectedClip, next);
    if (!id.isEmpty())
        selectClipById(id);
}

void AppController::undo()
{
    // A stroke still being written would push its edit after the undo and clear the redo stack.
    if (m_masking)
        m_masking->waitForBrushSaves(2000);
    if (!m_undoStack.canUndo())
        return;
    captureSelectionClipIds();
    m_undoStack.undo();
    m_undoSelectionCaptured = false;
    m_mcpController->bumpEditRevision(McpController::ChangeKind::Undo);
}

QStringList AppController::undoHistory() const
{
    QStringList texts;
    for (int i = 0; i < m_undoStack.count(); ++i)
        texts.append(m_undoStack.text(i));
    return texts;
}

void AppController::setUndoIndex(int index)
{
    index = qBound(0, index, m_undoStack.count());
    while (m_undoStack.index() > index && m_undoStack.canUndo())
        undo();
    while (m_undoStack.index() < index && m_undoStack.canRedo())
        redo();
}

void AppController::redo()
{
    if (m_masking)
        m_masking->waitForBrushSaves(2000);
    if (!m_undoStack.canRedo())
        return;
    captureSelectionClipIds();
    m_undoStack.redo();
    m_undoSelectionCaptured = false;
    m_mcpController->bumpEditRevision(McpController::ChangeKind::Redo);
}
namespace {


// Raw max-abs amplitude wastes almost all of the lane: ordinary dialogue peaks around
// -18 dBFS, i.e. 0.12 linear, which is a 4px sliver in a 40px lane. Plot dB over this window
// instead — the scale audio is actually read on — so quiet material is legible without
// clipping loud material. Full scale still maps to 1.0.
constexpr double kWaveformFloorDb = 48.0;

double waveformDisplayLevel(float peak)
{
    if (peak <= 0.0f)
        return 0.0;
    const double db = 20.0 * std::log10(static_cast<double>(peak));
    return qBound(0.0, 1.0 + db / kWaveformFloorDb, 1.0);
}

// Max-reduce dense peaks over [first, last) into `buckets` display values. The 0.05 floor
// keeps silent stretches drawn as a hairline rather than vanishing; a negative input marks a
// stretch that hasn't been decoded yet and comes back as 0 so nothing is drawn for it.
QVariantList reduceDensePeaks(const QVector<float> &dense, int first, int last, int buckets)
{
    QVariantList result;
    if (dense.isEmpty() || buckets <= 0)
        return result;

    first = qBound(0, first, dense.size());
    last = qBound(first, last, dense.size());
    if (last <= first)
        return result;

    const int span = last - first;
    result.reserve(buckets);
    for (int b = 0; b < buckets; ++b) {
        int i0 = first + static_cast<int>((static_cast<int64_t>(b) * span) / buckets);
        int i1 = first + static_cast<int>((static_cast<int64_t>(b + 1) * span) / buckets);
        if (i1 <= i0)
            i1 = qMin(last, i0 + 1);
        float peak = -1.0f;
        for (int i = i0; i < i1; ++i)
            peak = qMax(peak, dense[i]);
        result.append(peak < 0.0f ? 0.0 : qMax(0.05, waveformDisplayLevel(peak)));
    }
    return result;
}


} // namespace

const MediaWaveform::Dense *AppController::densePeaksFor(const QString &path) const
{
    if (path.isEmpty())
        return nullptr;

    const auto cached = m_waveformCache.constFind(path);
    if (cached != m_waveformCache.constEnd())
        return &cached.value();

    if (!m_waveformPending.contains(path)) {
        m_waveformPending.insert(path);
        AppController *self = const_cast<AppController *>(this);
        (void)QtConcurrent::run([self, path] {
            const MediaWaveform::Dense dense = MediaWaveform::densePeaks(path);
            QMetaObject::invokeMethod(
                self,
                [self, path, dense] {
                    self->m_waveformPending.remove(path);
                    // Decoded from the file this path held before it changed; the next ask
                    // decodes it again.
                    if (!self->m_waveformDiscard.remove(path))
                        self->m_waveformCache.insert(path, dense);
                    emit self->waveformReady(path);
                },
                Qt::QueuedConnection);
        });
    }

    return nullptr;
}

void AppController::forgetChangedMediaSource(const QString &path, bool beforeReprobe)
{
    if (path.isEmpty())
        return;
    // Skips a decoder that is mid-read; the second call, after the re-probe, catches it.
    ClipReaderPool::instance().releasePath(path);
    m_waveformBlocks.forgetSource(path);
    m_filmstripTiles.forgetSource(path);
    m_waveformCache.remove(path);
    if (m_waveformPending.contains(path))
        m_waveformDiscard.insert(path);
    // Cached files are named by path, so the regenerated ones keep their names.
    ClipThumbnailStore::instance().forgetPrefix(MediaThumbnail::cachePrefixFor(path));
    if (!beforeReprobe)
        return;
    // These would hand the re-probe the old pictures back, so they go before it runs.
    MediaThumbnail::forgetSource(path);
    drift::ReverseProxyCache::instance().removePreview(path);
    // Keyed by the content hash recorded at the last probe, which a clip still carries; all or
    // nothing, and cheap to rebuild — parsed documents only.
    drift::vec::clearVectorDocumentCache();
}

QVariantList AppController::waveformPeaks(const QString &path) const
{
    const MediaWaveform::Dense *dense = densePeaksFor(path);
    if (!dense)
        return {};

    // Whole file at dialog resolution (Denoise / Speed Curve canvases).
    const int buckets = qMin(2000, dense->peaks.size());
    return reduceDensePeaks(dense->peaks, 0, dense->peaks.size(), buckets);
}

QVariantList AppController::waveformPeaksForSourceRange(const QString &path, double startSeconds,
                                                        double durSeconds) const
{
    const MediaWaveform::Dense *dense = densePeaksFor(path);
    if (!dense || dense->durationSeconds <= 0.0 || durSeconds <= 0.0)
        return {};

    const double peaksPerSecond = dense->peaks.size() / dense->durationSeconds;
    const int first = static_cast<int>(startSeconds * peaksPerSecond);
    const int last = static_cast<int>((startSeconds + durSeconds) * peaksPerSecond);
    // reduceDensePeaks clamps the range, so a window running past the decoded end is safe.
    return reduceDensePeaks(dense->peaks, first, last, qMin(2000, qMax(1, last - first)));
}

QVariantList AppController::waveformPeaksRange(const QString &path, double startSeconds,
                                               double durSeconds, int buckets, int audioStreamIndex) const
{
    if (path.isEmpty() || durSeconds <= 0.0 || buckets <= 0)
        return {};

    // Block-backed: only the source span the clip's visible pixels cover is ever decoded, so
    // a three-hour file costs a viewport's worth of audio instead of the whole timeline.
    const int outCount = qBound(1, buckets, 4096);
    const QVector<float> span = m_waveformBlocks.range(path, startSeconds, durSeconds, outCount, audioStreamIndex);
    if (span.isEmpty())
        return {};

    return reduceDensePeaks(span, 0, span.size(), span.size());
}

QVariantMap AppController::waveformChannelPeaksRange(const QString &path, double startSeconds,
                                                     double durSeconds, int buckets,
                                                     int audioStreamIndex) const
{
    QVariantMap result;
    result.insert(QStringLiteral("channels"), 0);
    result.insert(QStringLiteral("buckets"), 0);
    if (path.isEmpty() || durSeconds <= 0.0 || buckets <= 0)
        return result;

    const int channels = m_waveformBlocks.channelCount(path, audioStreamIndex);
    if (channels <= 0) {
        // Nothing decoded yet. Ask for the merged envelope anyway so the block gets queued and
        // rangeReady() brings the caller back once the layout is known.
        m_waveformBlocks.range(path, startSeconds, durSeconds, qBound(1, buckets, 4096),
                               audioStreamIndex);
        return result;
    }

    const int outCount = qBound(1, buckets, 4096);
    QVariantList peaks;
    peaks.reserve(channels * outCount);
    int decodedBuckets = -1;
    for (int c = 0; c < channels; ++c) {
        const QVector<float> span =
            m_waveformBlocks.range(path, startSeconds, durSeconds, outCount, audioStreamIndex, c);
        // Same 48 dB display curve and visibility floor as the merged lane, deliberately not
        // renormalized per channel: a silent surround has to read as quieter than the dialogue
        // rather than being stretched to look like it.
        const QVariantList reduced = reduceDensePeaks(span, 0, span.size(), span.size());
        // The caller indexes this as peaks[c * buckets + b], which is only meaningful if every
        // channel came back the same length. They always do — range() returns exactly outCount
        // or nothing — but a ragged result would be read as garbage rather than as missing data.
        if (decodedBuckets >= 0 && reduced.size() != decodedBuckets)
            return result;
        decodedBuckets = reduced.size();
        peaks.append(reduced);
    }

    if (decodedBuckets <= 0)
        return result;

    result.insert(QStringLiteral("channels"), channels);
    result.insert(QStringLiteral("buckets"), decodedBuckets);
    result.insert(QStringLiteral("names"), m_waveformBlocks.channelNames(path, audioStreamIndex));
    result.insert(QStringLiteral("peaks"), peaks);
    return result;
}

QVector<float> AppController::waveformDisplayPeaks(const QString &path, double startSeconds,
                                                   double durSeconds, int buckets,
                                                   int audioStreamIndex, int channel) const
{
    QVector<float> out;
    if (path.isEmpty() || durSeconds <= 0.0 || buckets <= 0)
        return out;
    const QVector<float> span = m_waveformBlocks.range(path, startSeconds, durSeconds,
                                                       qBound(1, buckets, 4096), audioStreamIndex,
                                                       channel);
    out.reserve(span.size());
    for (float peak : span)
        out.append(peak < 0.0f ? 0.0f : float(qMax(0.05, waveformDisplayLevel(peak))));
    return out;
}

QStringList AppController::waveformChannelNames(const QString &path, int audioStreamIndex) const
{
    return m_waveformBlocks.channelNames(path, audioStreamIndex);
}

QString AppController::filmstripTilePath(const QString &path, int level, qint64 index,
                                         int rotationCorrection) const
{
    if (path.isEmpty())
        return {};
    return m_filmstripTiles.tile(path, level, index, rotationCorrection);
}

int AppController::waveformChannelCount(const QString &path, int audioStreamIndex) const
{
    if (path.isEmpty())
        return 0;
    return m_waveformBlocks.channelCount(path, audioStreamIndex);
}

int AppController::trackMaxChannelCount(int trackIndex) const
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return 0;

    int widest = 0;
    for (const drift::Clip &clip : m_project.tracks().at(trackIndex).clips) {
        if (clip.path.isEmpty())
            continue;
        // From the container's own metadata, not from the block cache: the cache only learns the
        // layout once a block has decoded, which is after the header has already been built and
        // is signalled by waveformRangeReady rather than tracksChanged — so a control bound to
        // this would stay hidden until some unrelated edit happened to re-evaluate it. The probe
        // is memoized, so asking per clip costs one container open per distinct file.
        const QList<StreamInfo> streams = cachedAudioStreams(clip.path);
        if (clip.audioStreamIndex >= 0 && clip.audioStreamIndex < streams.size())
            widest = qMax(widest, streams.at(clip.audioStreamIndex).channels);
    }
    return widest;
}

QList<float> AppController::subtitleWaveformColumns(double startSeconds, double durSeconds,
                                                    int sampleCount, int columns) const
{
    const QVariantList peaks = subtitleWaveformPeaks(startSeconds, durSeconds, sampleCount);
    const int n = static_cast<int>(peaks.size());
    if (n == 0 || columns <= 0)
        return {};
    QList<float> out;
    out.reserve(columns);
    for (int x = 0; x < columns; ++x) {
        const int i0 = static_cast<int>(static_cast<qint64>(x) * n / columns);
        int i1 = static_cast<int>(static_cast<qint64>(x + 1) * n / columns);
        if (i1 <= i0)
            i1 = qMin(n, i0 + 1);
        float peak = 0.0f;
        for (int i = i0; i < i1; ++i)
            peak = qMax(peak, peaks.at(i).toFloat());
        out.append(peak);
    }
    return out;
}

QVariantList AppController::subtitleWaveformPeaks(double startSeconds, double durSeconds,
                                                  int sampleCount) const
{
    if (durSeconds <= 0.0 || sampleCount <= 0)
        return {};

    // Cap so extreme zoom doesn't spawn multi-megabyte peak lists / mix jobs.
    const int buckets = qBound(1, sampleCount, 8192);

    const drift::TimeUs startUs = drift::secondsToUs(startSeconds);
    const drift::TimeUs durUs = drift::secondsToUs(durSeconds);
    const QString key = QStringLiteral("%1:%2:%3").arg(startUs).arg(durUs).arg(buckets);

    const auto cached = m_subtitleWaveformCache.constFind(key);
    if (cached != m_subtitleWaveformCache.constEnd())
        return cached.value();

    if (!m_subtitleWaveformPending.contains(key)) {
        m_subtitleWaveformPending.insert(key);
        AppController *self = const_cast<AppController *>(this);
        // Snapshot the project so the off-thread mixer never races the live one.
        const drift::Project snap = m_project;
        (void)QtConcurrent::run([self, snap, startUs, durUs, buckets, key, startSeconds, durSeconds] {
            const int rate = 8000; // enough for voice; keeps the render cheap
            const qint64 frames =
                static_cast<qint64>((static_cast<double>(durUs) / 1'000'000.0) * rate);
            QVariantList peaks;
            if (frames > 0) {
                AudioMixer mixer;
                mixer.setProject(&snap);
                // Never ask for more buckets than PCM frames — extras would be empty.
                const int peakBuckets = static_cast<int>(qMin<qint64>(buckets, frames));
                // Mixed a window at a time. The lane spans every A/V clip on the timeline, so
                // mixing it in one go meant holding the whole thing as PCM — ~400 MB for a
                // feature-length movie, to draw at most 8192 columns. The windows are
                // contiguous and in order, which is the pattern playback already uses, so the
                // readers decode forward instead of re-seeking.
                peaks = MediaWaveform::voicePeaks(
                    frames, rate, peakBuckets,
                    [&mixer, startUs, rate](float *out, qint64 frameOffset, int maxFrames) {
                        const drift::TimeUs at =
                            startUs + frameOffset * drift::kUsPerSecond / rate;
                        mixer.mix(at, maxFrames, rate, out);
                        return maxFrames;
                    });
            }
            QMetaObject::invokeMethod(
                self,
                [self, key, peaks, startSeconds, durSeconds, buckets] {
                    self->m_subtitleWaveformCache.insert(key, peaks);
                    self->m_subtitleWaveformPending.remove(key);
                    emit self->subtitleWaveformReady(startSeconds, durSeconds, buckets);
                },
                Qt::QueuedConnection);
        });
    }

    return {};
}

// Everything the AudioMixer reads, and nothing else — so a transform keyframe edit does not
// invalidate a beat grid while moving, trimming, retiming, or muting, hiding or soloing the music
// does.
QByteArray AppController::audioLayoutFingerprint() const
{
    QCryptographicHash hash(QCryptographicHash::Sha1);
    const bool anySolo = drift::anyTrackSoloed(m_project.tracks());
    for (const drift::Track &track : m_project.tracks()) {
        if (track.type != drift::TrackType::Audio && track.type != drift::TrackType::Video)
            continue;
        hash.addData(drift::trackAudible(track, anySolo) ? "a" : "-");
        // A plain cut leaves the mix untouched, so consecutive pieces that continue one another
        // hash as the single clip they came from — otherwise split_on_beats drops its own grid.
        const auto plain = [](const drift::Clip &c) {
            return !c.reverse && !c.loops() && !c.hasSpeedCurve() && c.volume.keyframes().isEmpty();
        };
        for (qsizetype i = 0; i < track.clips.size(); ++i) {
            const drift::Clip &clip = track.clips.at(i);
            drift::TimeUs runDuration = clip.timelineDuration;
            drift::TimeUs runSrcOut = clip.srcOut;
            while (plain(clip) && i + 1 < track.clips.size()) {
                const drift::Clip &next = track.clips.at(i + 1);
                if (!plain(next) || next.assetId != clip.assetId
                    || next.timelineStart != clip.timelineStart + runDuration
                    || next.srcIn != runSrcOut || next.speed != clip.speed
                    || next.suppressEmbeddedAudio != clip.suppressEmbeddedAudio
                    || next.audioEffects.size() != clip.audioEffects.size() || next.pan != clip.pan) {
                    break;
                }
                runDuration += next.timelineDuration;
                runSrcOut = next.srcOut;
                ++i;
            }
            const QString row = QStringLiteral("%1|%2|%3|%4|%5|%6|%7|%8|%9|%10")
                                    .arg(clip.assetId)
                                    .arg(clip.timelineStart)
                                    .arg(runDuration)
                                    .arg(clip.srcIn)
                                    .arg(runSrcOut)
                                    .arg(clip.speed)
                                    .arg(clip.reverse ? 1 : 0)
                                    .arg(clip.suppressEmbeddedAudio ? 1 : 0)
                                    .arg(clip.audioEffects.size())
                                    .arg(clip.pan);
            hash.addData(row.toUtf8());
            // Tangents shape the volume ramp, so they belong in the digest alongside the values.
            for (const auto &kv : clip.volume.keyframes().asKeyValueRange()) {
                hash.addData(QStringLiteral("v%1:%2:%3:%4:%5:%6:%7")
                                 .arg(kv.first)
                                 .arg(kv.second.value)
                                 .arg(kv.second.inDx)
                                 .arg(kv.second.inDy)
                                 .arg(kv.second.outDx)
                                 .arg(kv.second.outDy)
                                 .arg(kv.second.hold ? 1 : 0)
                                 .toUtf8());
            }
            if (clip.hasSpeedCurve())
                hash.addData("c");
        }
    }
    return hash.result();
}

void AppController::analyzeBeats(double startSeconds, double durSeconds)
{
    if (durSeconds <= 0.0 || m_beatAnalysisRunning)
        return;

    // Already showing this exact range — nothing to redo.
    if (!m_beatAnalysis.isEmpty()
        && qFuzzyCompare(m_beatAnalysis.value(QStringLiteral("rangeStart")).toDouble() + 1.0,
                         startSeconds + 1.0)
        && qFuzzyCompare(m_beatAnalysis.value(QStringLiteral("rangeDuration")).toDouble() + 1.0,
                         durSeconds + 1.0)) {
        return;
    }

    const drift::TimeUs startUs = drift::secondsToUs(startSeconds);
    const drift::TimeUs durUs = drift::secondsToUs(durSeconds);
    const quint64 generation = ++m_beatAnalysisGeneration;

    m_beatAnalysisRunning = true;
    emit beatAnalysisChanged();

    // Snapshot the project so the off-thread mixer never races the live one — same
    // contract as subtitleWaveformPeaks. The fingerprint is taken from that same snapshot,
    // so an edit landing mid-analysis is caught by the staleness check rather than being
    // baked in as the state the result supposedly describes.
    const drift::Project snap = m_project;
    const QByteArray fingerprint = audioLayoutFingerprint();
    (void)QtConcurrent::run([this, snap, startUs, durUs, startSeconds, durSeconds, generation,
                             fingerprint] {
        const AudioBeatAnalysis analysis = runBeatAnalysis(snap, startUs, durUs, startSeconds);
        QMetaObject::invokeMethod(
            this,
            [this, analysis, startSeconds, durSeconds, generation, fingerprint] {
                if (generation != m_beatAnalysisGeneration)
                    return; // the user moved on; this result is for a range nobody is looking at
                applyBeatAnalysis(analysis, startSeconds, durSeconds, fingerprint);
            },
            Qt::QueuedConnection);
    });
}

namespace {

QVariantMap beatFailure(const char *code, const QString &message)
{
    return QVariantMap{{QStringLiteral("error"), QString::fromLatin1(code)},
                       {QStringLiteral("message"), message}};
}

} // namespace

// Mixes the clip on its own, so other music or dialogue on the timeline cannot move the tempo.
// Blocks in a nested event loop like detect_beats does; a few seconds for a song.
AudioBeatAnalysis AppController::analyzeClipBeats(const QString &clipId, QVariantMap *failure)
{
    const QPair<int, int> at = m_mcpController->locateClip(clipId);
    if (at.first < 0 || at.second < 0 || !isValidClipIndex(at.first, at.second)) {
        *failure = beatFailure("not_found", tr("Unknown clip"));
        return {};
    }
    const drift::Clip &clip = m_project.tracks().at(at.first).clips.at(at.second);
    if (clip.type != drift::ClipType::Audio && clip.type != drift::ClipType::Video) {
        *failure = beatFailure("bad_args", tr("Beats come from an audio or video clip"));
        return {};
    }
    const double startSeconds = drift::usToSeconds(clip.timelineStart);
    const double durSeconds = qMin(drift::usToSeconds(clip.timelineDuration), 600.0);
    if (durSeconds < AudioOnsets::kMinAnalysisSec) {
        *failure = beatFailure("bad_args", tr("The clip is too short to find a tempo"));
        return {};
    }

    drift::Project snap = m_project;
    drift::Track only = m_project.tracks().at(at.first);
    only.clips = {clip};
    only.transitions.clear();
    only.muted = false;
    only.solo = false;
    snap.tracks() = {only};

    const drift::TimeUs startUs = clip.timelineStart;
    const drift::TimeUs durUs = drift::secondsToUs(durSeconds);
    auto result = std::make_shared<AudioBeatAnalysis>();
    QEventLoop loop;
    (void)QtConcurrent::run([snap, startUs, durUs, startSeconds, result, &loop]() {
        *result = runBeatAnalysis(snap, startUs, durUs, startSeconds);
        QMetaObject::invokeMethod(&loop, &QEventLoop::quit, Qt::QueuedConnection);
    });
    loop.exec();

    if (result->beats.size() < 2)
        *failure = beatFailure("not_found", tr("No steady beat found in that clip"));
    return *result;
}

QVariantMap AppController::addBeatMarkersForClip(const QString &clipId, int every)
{
    QVariantMap failure;
    const AudioBeatAnalysis analysis = analyzeClipBeats(clipId, &failure);
    if (!failure.isEmpty())
        return failure;

    const int step = every <= 0 ? analysis.beatsPerBar : every;
    const QList<double> times = drift::beatsEvery(analysis.beats, step, analysis.firstDownbeat);
    const int added = addBookmarksAtTimes(times, 0.0, 0.0, false, tr("Beat"), tr("Add beat markers"));
    return QVariantMap{{QStringLiteral("added"), added},
                       {QStringLiteral("beats"), static_cast<int>(times.size())},
                       {QStringLiteral("bpm"), analysis.bpm},
                       {QStringLiteral("confidence"), analysis.confidence}};
}

QVariantMap AppController::autoCutToBeat(const QString &musicClipId, const QStringList &clipIds,
                                         int beatsPerCut, bool fromPlayhead, bool syncDownbeats)
{
    struct Target {
        QString id;
        drift::TimeUs start = 0;
        int track = 0;
        double available = 0.0;
    };
    QList<Target> targets;
    QSet<QString> seen;
    for (const QString &raw : clipIds) {
        const QString id = raw.trimmed();
        if (id.isEmpty() || id == musicClipId || seen.contains(id))
            continue;
        seen.insert(id);
        const QPair<int, int> at = m_mcpController->locateClip(id);
        if (at.first < 0 || at.second < 0 || !isValidClipIndex(at.first, at.second))
            continue;
        const drift::Clip &clip = m_project.tracks().at(at.first).clips.at(at.second);
        if (clip.type != drift::ClipType::Video && clip.type != drift::ClipType::Image)
            continue;
        // A still has no footage to run out of; a video can only be trimmed, never stretched.
        const double available = clip.type == drift::ClipType::Image
                                     ? 3600.0
                                     : drift::usToSeconds(clip.timelineDuration);
        targets.append({id, clip.timelineStart, at.first, available});
    }
    if (targets.isEmpty())
        return beatFailure("bad_args", tr("Select the video or image clips to cut together"));
    std::stable_sort(targets.begin(), targets.end(), [](const Target &a, const Target &b) {
        return a.start != b.start ? a.start < b.start : a.track < b.track;
    });

    QVariantMap failure;
    const AudioBeatAnalysis analysis = analyzeClipBeats(musicClipId, &failure);
    if (!failure.isEmpty())
        return failure;

    const QPair<int, int> music = m_mcpController->locateClip(musicClipId);
    if (music.first < 0 || music.second < 0 || !isValidClipIndex(music.first, music.second))
        return beatFailure("not_found", tr("The music clip is gone"));
    const drift::Clip &musicClip = m_project.tracks().at(music.first).clips.at(music.second);
    const double musicStart = drift::usToSeconds(musicClip.timelineStart);
    const double musicEnd = drift::usToSeconds(musicClip.timelineEnd());

    drift::BeatCutOptions options;
    options.beatsPerCut = beatsPerCut;
    options.syncToDownbeats = syncDownbeats;
    options.startSeconds = fromPlayhead ? qMax(musicStart, playheadSeconds()) : musicStart;
    options.endSeconds = qMin(musicEnd, musicStart + 600.0);
    options.firstDownbeat = analysis.firstDownbeat;
    options.beatsPerBar = analysis.beatsPerBar;

    QList<double> available;
    for (const Target &t : std::as_const(targets))
        available.append(t.available);
    const QList<drift::BeatCutSlot> cuts = drift::planBeatCut(analysis.beats, available, options);
    if (cuts.isEmpty())
        return beatFailure("bad_args", tr("No beat to cut on between the start point and the end of the music"));

    int insertAt = targets.first().track;
    for (const Target &t : std::as_const(targets))
        insertAt = qMin(insertAt, t.track);

    // Placement is exact: snapping to the playhead, another clip's edge or the beat grid would
    // each move a cut off the beat it was planned for.
    QScopedValueRollback<bool> noSnap(m_snapEnabled, false);
    clearSelection();
    m_mcpController->beginBatch();
    m_project.ensureTrackIds();
    QSet<QString> idsBefore;
    for (const drift::Track &track : m_project.tracks())
        idsBefore.insert(track.id);
    insertTrack(QStringLiteral("video"), insertAt);
    int newTrack = -1;
    for (int i = 0; i < m_project.tracks().size(); ++i) {
        if (!idsBefore.contains(m_project.tracks().at(i).id)) {
            newTrack = i;
            break;
        }
    }
    if (newTrack < 0) {
        m_mcpController->endBatch(tr("Auto-cut to beat"), true);
        return beatFailure("apply_failed", tr("Could not add a track"));
    }
    QVariantList placed;
    int shortClips = 0;
    for (const drift::BeatCutSlot &slot : cuts) {
        const QString &id = targets.at(slot.clip).id;
        QPair<int, int> at = m_mcpController->locateClip(id);
        if (at.first < 0)
            continue;
        const drift::Clip &before = m_project.tracks().at(at.first).clips.at(at.second);
        if (qAbs(drift::usToSeconds(before.timelineDuration) - slot.duration) > 0.0005)
            setClipDuration(at.first, at.second, slot.duration);
        at = m_mcpController->locateClip(id);
        if (at.first < 0)
            continue;
        moveClipToTrack(at.first, at.second, newTrack, slot.start);
        at = m_mcpController->locateClip(id);
        if (at.first < 0)
            continue;
        if (qAbs(drift::usToSeconds(m_project.tracks().at(at.first).clips.at(at.second).timelineStart) - slot.start) > 0.0005)
            setClipStart(at.first, at.second, slot.start);
        if (slot.shorterThanSlot)
            ++shortClips;
        placed.append(QVariantMap{{QStringLiteral("clip"), id},
                                  {QStringLiteral("start"), slot.start},
                                  {QStringLiteral("duration"), slot.duration}});
    }
    m_mcpController->endBatch(tr("Auto-cut to beat"), !placed.isEmpty());

    return QVariantMap{{QStringLiteral("placed"), placed},
                       {QStringLiteral("track"), newTrack},
                       {QStringLiteral("bpm"), analysis.bpm},
                       {QStringLiteral("short"), shortClips},
                       {QStringLiteral("unused"), static_cast<int>(targets.size() - cuts.size())}};
}

int AppController::beatCutTargetCount(int trackIndex, int clipIndex) const
{
    int n = 0;
    for (const QString &id : clipIdsForAction(trackIndex, clipIndex)) {
        const QPair<int, int> at = m_mcpController->locateClip(id);
        if (at.first < 0 || (at.first == trackIndex && at.second == clipIndex))
            continue;
        const drift::ClipType type = m_project.tracks().at(at.first).clips.at(at.second).type;
        if (type == drift::ClipType::Video || type == drift::ClipType::Image)
            ++n;
    }
    return n;
}

void AppController::addBeatMarkersForSelection(int trackIndex, int clipIndex, int every)
{
    if (!isValidClipIndex(trackIndex, clipIndex))
        return;
    const QVariantMap result =
        addBeatMarkersForClip(m_project.tracks().at(trackIndex).clips.at(clipIndex).id, every);
    if (result.contains(QStringLiteral("error"))) {
        setLastMessage(result.value(QStringLiteral("message")).toString(), QStringLiteral("warning"));
        return;
    }
    const int added = result.value(QStringLiteral("added")).toInt();
    if (added > 0)
        setLastMessage(tr("Added %n beat marker(s)", nullptr, added));
    else
        setLastMessage(tr("No new markers to add"), QStringLiteral("warning"));
}

void AppController::autoCutSelectionToBeat(int trackIndex, int clipIndex, int beatsPerCut, bool fromPlayhead,
                                           bool syncDownbeats)
{
    if (!isValidClipIndex(trackIndex, clipIndex))
        return;
    const QString musicId = m_project.tracks().at(trackIndex).clips.at(clipIndex).id;
    const QVariantMap result =
        autoCutToBeat(musicId, clipIdsForAction(trackIndex, clipIndex), beatsPerCut, fromPlayhead, syncDownbeats);
    if (result.contains(QStringLiteral("error"))) {
        setLastMessage(result.value(QStringLiteral("message")).toString(), QStringLiteral("warning"));
        return;
    }
    const int placed = result.value(QStringLiteral("placed")).toList().size();
    const int unused = result.value(QStringLiteral("unused")).toInt();
    setLastMessage(unused > 0 ? tr("Cut %n clip(s) to the beat; %1 did not fit the music", nullptr, placed).arg(unused)
                              : tr("Cut %n clip(s) to the beat", nullptr, placed));
}

void AppController::applyBeatAnalysis(const AudioBeatAnalysis &analysis, double startSeconds,
                                      double durSeconds, const QByteArray &fingerprint)
{
    QVariantList beats;
    for (double b : analysis.beats)
        beats.append(b);

    QVariantList onsets;
    for (const AudioOnset &o : analysis.onsets) {
        const auto next = std::lower_bound(analysis.beats.cbegin(), analysis.beats.cend(),
                                           o.seconds - 0.06);
        const bool nearBeat = next != analysis.beats.cend() && std::abs(*next - o.seconds) < 0.06;
        onsets.append(QVariantMap{{QStringLiteral("seconds"), o.seconds},
                                  {QStringLiteral("strength"), o.strength},
                                  {QStringLiteral("nearBeat"), nearBeat}});
    }

    m_beatAnalysis = QVariantMap{
        {QStringLiteral("rangeStart"), startSeconds},
        {QStringLiteral("rangeDuration"), durSeconds},
        {QStringLiteral("bpm"), analysis.bpm},
        {QStringLiteral("confidence"), analysis.confidence},
        {QStringLiteral("beatsPerBar"), analysis.beatsPerBar},
        {QStringLiteral("firstDownbeat"), analysis.firstDownbeat},
        {QStringLiteral("beats"), beats},
        {QStringLiteral("onsets"), onsets},
    };

    m_beatAnalysisRaw = analysis;
    rebuildBeatSnapTargets();

    m_beatAudioFingerprint = fingerprint;
    m_beatAnalysisRunning = false;
    emit beatAnalysisChanged();

    if (m_pendingEffectTemplate && m_pendingEffectTemplate->valid()) {
        const PendingEffectTemplate pending = *m_pendingEffectTemplate;
        const EffectTemplateEntry *entry = effectTemplateForId(pending.templateId);
        if (entry && pending.trackIndex >= 0 && pending.trackIndex < m_project.tracks().size()) {
            const drift::Track &track = m_project.tracks()[pending.trackIndex];
            if (pending.clipIndex >= 0 && pending.clipIndex < track.clips.size()
                && beatAnalysisReadyForClip(track.clips[pending.clipIndex], entry->sync)) {
                const bool needsSegment =
                    (entry->requiresSegmentation || entry->usesMultiTrack())
                    && !clipHasMatte(m_project, pending.trackIndex, pending.clipIndex);
                if (needsSegment) {
                    if (m_segmentation->available() && !m_segmentation->running())
                        m_segmentation->openSegmentationForTemplate(pending.trackIndex, pending.clipIndex);
                } else {
                    m_pendingEffectTemplate.reset();
                    applyEffectTemplateInternal(pending.trackIndex, pending.clipIndex, *entry);
                }
            }
        }
    }
}

// Beats first — they are the musically meaningful grid — then onsets that do not already
// sit within a snap threshold of an accepted target. Without the thinning, a dense onset
// list would leave no un-snapped position on the timeline.
void AppController::rebuildBeatSnapTargets()
{
    m_beatSnapTargets.clear();

    if (m_beatGridVisible) {
        for (double b : std::as_const(m_beatAnalysisRaw.beats))
            m_beatSnapTargets.append(drift::secondsToUs(b));
    }
    if (!m_onsetsVisible)
        return;

    for (const AudioOnset &o : std::as_const(m_beatAnalysisRaw.onsets)) {
        const drift::TimeUs at = drift::secondsToUs(o.seconds);
        bool crowded = false;
        for (drift::TimeUs existing : std::as_const(m_beatSnapTargets)) {
            if (qAbs(existing - at) < drift::kSnapThresholdUs) {
                crowded = true;
                break;
            }
        }
        if (!crowded)
            m_beatSnapTargets.append(at);
    }
}

QList<drift::TimeUs> AppController::extraSnapTargets() const
{
    QList<drift::TimeUs> targets = m_beatSnapTargets;
    for (const drift::Bookmark &bookmark : m_project.bookmarks())
        targets.append(bookmark.timeUs);
    if (m_project.hasWorkArea()) {
        targets.append(m_project.workAreaInUs());
        targets.append(m_project.workAreaOutUs());
    }
    return targets;
}

void AppController::setBeatGridVisible(bool visible)
{
    if (m_beatGridVisible == visible)
        return;
    m_beatGridVisible = visible;
    rebuildBeatSnapTargets();
    emit beatAnalysisChanged();
}

void AppController::setOnsetsVisible(bool visible)
{
    if (m_onsetsVisible == visible)
        return;
    m_onsetsVisible = visible;
    rebuildBeatSnapTargets();
    emit beatAnalysisChanged();
}

void AppController::clearBeatAnalysis()
{
    // Bump the generation so an in-flight job cannot repopulate what we just cleared.
    ++m_beatAnalysisGeneration;
    if (m_beatAnalysis.isEmpty() && !m_beatAnalysisRunning && !m_beatGridVisible
        && !m_onsetsVisible) {
        return;
    }
    m_beatAnalysis.clear();
    m_beatAnalysisRaw = {};
    m_beatSnapTargets.clear();
    m_beatAudioFingerprint.clear();
    m_beatAnalysisRunning = false;
    // The layer toggles come down with the data. Leaving them lit over an empty result
    // would show two active buttons and nothing drawn, and re-analyzing automatically
    // would mean a full mix render on every clip nudge.
    m_beatGridVisible = false;
    m_onsetsVisible = false;
    emit beatAnalysisChanged();
}

AppController::BeatState AppController::beatState() const
{
    return {m_beatAnalysisRaw, m_beatAnalysis, m_beatSnapTargets, m_beatAudioFingerprint, m_beatGridVisible,
            m_onsetsVisible};
}

void AppController::restoreBeatState(const BeatState &state)
{
    // A newer analysis started since wins; so does one still running.
    if (m_beatAnalysisRunning || !m_beatAnalysis.isEmpty() || state.analysis.isEmpty())
        return;
    ++m_beatAnalysisGeneration;
    m_beatAnalysisRaw = state.raw;
    m_beatAnalysis = state.analysis;
    m_beatSnapTargets = state.snapTargets;
    m_beatAudioFingerprint = state.fingerprint;
    m_beatGridVisible = state.gridVisible;
    m_onsetsVisible = state.onsetsVisible;
    emit beatAnalysisChanged();
}

void AppController::restoreFilmstripsAfterLoad()
{
    if (!m_assetLibrary)
        return;

    for (drift::Track &track : m_project.tracks()) {
        for (drift::Clip &clip : track.clips) {
            if (!clip.filmstripPath.isEmpty())
                continue;
            const int assetIndex = assetIndexForClip(clip);
            if (assetIndex >= 0) {
                m_assetLibrary->ensureMedia(assetIndex);
                clip.filmstripPath = m_assetLibrary->filmstripAt(assetIndex);
            }
            if (clip.filmstripPath.isEmpty())
                clip.filmstripPath = clip.thumbnailPath;
        }
    }
}

bool AppController::isValidClipIndex(int trackIndex, int clipIndex) const
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return false;
    return clipIndex >= 0 && clipIndex < m_project.tracks().at(trackIndex).clips.size();
}

void AppController::captureSelectionClipIds()
{
    m_undoSelectionIds.clear();
    for (const QPair<int, int> &pair : std::as_const(m_selection)) {
        if (isValidClipIndex(pair.first, pair.second))
            m_undoSelectionIds.append(m_project.tracks().at(pair.first).clips.at(pair.second).id);
    }
    m_undoPrimaryId = isValidClipIndex(m_selectedTrack, m_selectedClip)
        ? m_project.tracks().at(m_selectedTrack).clips.at(m_selectedClip).id
        : QString();
    const bool transition = isValidClipIndex(m_selectedTransitionTrack, m_selectedTransitionLeftClip);
    m_undoTransitionLeftClipId = transition
        ? m_project.tracks().at(m_selectedTransitionTrack).clips.at(m_selectedTransitionLeftClip).id
        : QString();
    m_undoSelectionCaptured = true;
}

void AppController::restoreSelectionClipIds()
{
    if (!m_undoSelectionCaptured)
        return;
    QList<QPair<int, int>> restored;
    for (const QString &id : std::as_const(m_undoSelectionIds)) {
        int t = -1;
        int c = -1;
        if (findClipById(m_project, id, &t, &c))
            restored.append(qMakePair(t, c));
    }
    int t = -1;
    int c = -1;
    const bool primaryFound = !m_undoPrimaryId.isEmpty() && findClipById(m_project, m_undoPrimaryId, &t, &c);
    if (restored.isEmpty() && primaryFound)
        restored.append(qMakePair(t, c));
    // Nothing selected survives: clear it. Keeping the old indices would leave them pointing at
    // whatever clip now sits there -- a neighbour's grade, say, after undoing a removal before it.
    m_selection = restored;
    if (primaryFound) {
        m_selectedTrack = t;
        m_selectedClip = c;
    } else if (!restored.isEmpty()) {
        m_selectedTrack = restored.constLast().first;
        m_selectedClip = restored.constLast().second;
    } else {
        m_selectedTrack = -1;
        m_selectedClip = -1;
    }

    // Before normalizeSelection, which would otherwise judge the transition by stale indices.
    if (!m_undoTransitionLeftClipId.isEmpty()) {
        int track = -1;
        int left = -1;
        if (!findClipById(m_project, m_undoTransitionLeftClipId, &track, &left))
            track = left = -1;
        m_selectedTransitionTrack = track;
        m_selectedTransitionLeftClip = left;
    }
}

void AppController::normalizeSelection()
{
    const QList<QPair<int, int>> selection = m_selection;
    QList<QPair<int, int>> kept;
    kept.reserve(selection.size());
    for (const QPair<int, int> &pair : selection) {
        if (isValidClipIndex(pair.first, pair.second) && !m_project.tracks().at(pair.first).locked
            && !kept.contains(pair))
            kept.append(pair);
    }
    m_selection = kept;
    if (m_selectedTransitionTrack >= 0) {
        const QVariantMap selected = selectedTransitionData();
        if (selected.isEmpty())
            clearTransitionSelection();
    }
    if (m_selection.isEmpty()) {
        m_selectedTrack = -1;
        m_selectedClip = -1;
        return;
    }
    if (!isValidClipIndex(m_selectedTrack, m_selectedClip)) {
        m_selectedTrack = m_selection.constLast().first;
        m_selectedClip = m_selection.constLast().second;
    }
}
