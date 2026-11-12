#include "app/editor/appcontroller/AppControllerInternal.h"
#include "engine/packages/effects/EffectCatalog.h"
#include "app/editor/sessions/MaskingController.h"
#include "app/mcp/McpController.h"
#include "app/editor/PreferencesController.h"

#include "engine/packages/assets/EmojiCatalog.h"
// Engine-internal by its own header comment, and included here only for releaseCaches(): the GPU
// caches have no other owner outside src/engine to ask. A one-line forwarder on GpuCompositor
// would restore the boundary.
#include "engine/ml/vision/detection/ObjectDetector.h"
#include "engine/packages/assets/StickerCatalog.h"


using namespace drift::appdetail;
using drift::mcpdetail::findClipById;
using drift::mcpdetail::runBeatAnalysis;
using namespace drift::appcontroller_detail;

namespace {


drift::ShapeStyle shapeStyleForKind(const QString &shapeId)
{
    if (const drift::ShapeCatalogEntry *entry = drift::shapeCatalogEntry(shapeId))
        return entry->style;

    drift::ShapeStyle style;
    style.kind = drift::shapeKindFromString(shapeId);
    return style;
}
} // namespace

// --- scene detection --------------------------------------------------------

bool AppController::objectDetectionAvailable() const
{
    return drift::ObjectDetector::modelPresent();
}

void AppController::finalizeGeneratedSubtitles(drift::TimeUs timelineStart,
                                               drift::TimeUs timelineDuration,
                                               const QList<drift::SubtitleCue> &cues)
{
    const drift::Project before = m_project;
    const int trackIndex = drift::ensureTrackForClipType(m_project, drift::ClipType::Subtitle, true);
    qWarning() << "[subtitles] finalize: trackIndex" << trackIndex << "cues" << cues.size()
               << "start" << timelineStart << "dur" << timelineDuration;
    if (trackIndex < 0)
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    drift::Clip clip;
    clip.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    clip.type = drift::ClipType::Subtitle;
    clip.timelineStart = timelineStart;
    clip.timelineDuration = timelineDuration;
    clip.srcIn = 0;
    clip.srcOut = timelineDuration;
    if (const std::optional<drift::TextStyle> preset = drift::textStyleForPresetId(QStringLiteral("subtitle")))
        clip.textStyle = *preset;
    applyDefaultVisualLayout(clip, m_project.width(), m_project.height());
    clip.subtitleCues = cues;
    clip.name = drift::subtitleClipName(cues);

    track.clips.append(clip);
    const int newClipIndex = track.clips.size() - 1;
    pushProjectEdit(before, tr("Subtitles generated"));
    finishEdit(tr("Subtitles generated"));
    selectClip(trackIndex, newClipIndex);
    setLastMessage(tr("Subtitles generated"), QStringLiteral("success"));
    emit subtitleGenerationFinished(true, QStringLiteral("Subtitles generated"));
}


QVariantList AppController::builtinStickers() const
{
    QVariantList out;
    for (const StickerEntry &entry : ::stickers()) {
        out.append(QVariantMap{
            {QStringLiteral("id"), entry.id},
            {QStringLiteral("label"), entry.label},
            {QStringLiteral("category"), entry.category},
            {QStringLiteral("path"), entry.path},
        });
    }
    return out;
}

QVariantList AppController::builtinStickerCategories() const
{
    QVariantList out;
    for (const StickerCategory &category : ::stickerCategories()) {
        out.append(QVariantMap{
            {QStringLiteral("id"), category.id},
            {QStringLiteral("label"), category.label},
        });
    }
    return out;
}

QVariantList AppController::builtinShapes() const
{
    QVariantList out;
    for (const drift::ShapeCatalogEntry &entry : drift::shapeCatalog()) {
        out.append(QVariantMap{
            {QStringLiteral("id"), entry.id},
            {QStringLiteral("label"), entry.label},
            {QStringLiteral("category"), entry.category},
            // Several ids share a kind, so the inspector matches a clip's stored kind on this.
            {QStringLiteral("kind"), drift::shapeKindToString(entry.style.kind)},
            {QStringLiteral("aspect"), entry.aspect},
        });
    }
    return out;
}

QVariantList AppController::builtinShapeCategories() const
{
    QVariantList out;
    for (const drift::ShapeCategory &category : drift::shapeCategories()) {
        out.append(QVariantMap{
            {QStringLiteral("id"), category.id},
            {QStringLiteral("label"), category.label},
        });
    }
    return out;
}

void AppController::addShapeClip(const QString &shapeKind, double atSeconds)
{
    addShapeClipAt(shapeKind, -1, atSeconds);
}

void AppController::addShapeClipAt(const QString &shapeId, int trackIndex, double atSeconds)
{
    const drift::ShapeCatalogEntry *entry = drift::shapeCatalogEntry(shapeId);
    const drift::ShapeStyle style = entry ? entry->style : shapeStyleForKind(shapeId);
    const drift::Project before = m_project;

    const drift::TimeUs wantStart = atSeconds < 0.0 ? m_playheadUs : drift::secondsToUs(atSeconds);
    int target = trackIndex;
    if (target < 0 || target >= m_project.tracks().size()
        || !m_project.tracks().at(target).allowsClipType(drift::ClipType::Shape)) {
        target = drift::ensureFreeTrackForClipType(m_project, drift::ClipType::Shape, wantStart,
                                                   drift::imageClipDurationUs(), true);
    }
    if (target < 0)
        return;

    drift::Track &track = m_project.tracks()[target];
    const drift::TimeUs startSeconds = wantStart;
    const drift::TimeUs start = drift::resolveClipStart(m_project, track, -1, startSeconds,
                                                        drift::imageClipDurationUs(), m_snapEnabled, m_playheadUs);

    drift::Clip clip;
    clip.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    clip.type = drift::ClipType::Shape;
    clip.name = entry ? entry->label : drift::shapeKindToString(style.kind);
    clip.shapeStyle = style;
    clip.timelineStart = start;
    clip.timelineDuration = drift::imageClipDurationUs();
    clip.srcIn = 0;
    clip.srcOut = drift::imageClipDurationUs();
    applyDefaultVisualLayout(clip, m_project.width(), m_project.height(),
                             entry ? entry->aspect : 1.6);

    track.clips.append(clip);
    const int newClipIndex = track.clips.size() - 1;
    pushProjectEdit(before, tr("Shape added"));
    finishEdit(tr("Shape added"));
    selectClip(target, newClipIndex);
}

int AppController::ensureAdjustmentLaneFor(int parentTrackIndex, drift::AdjustmentKind kind,
                                           drift::TimeUs startUs, drift::TimeUs durationUs)
{
    return drift::ensureAdjustmentLane(m_project, parentTrackIndex, kind, startUs, durationUs);
}

drift::ClipRef AppController::createLinkedAdjustment(int trackIndex, int clipIndex,
                                                     drift::AdjustmentKind kind)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return {};
    if (clipIndex < 0 || clipIndex >= m_project.tracks().at(trackIndex).clips.size())
        return {};

    // Copied, not referenced: ensureAdjustmentLaneFor may insert a track and invalidate it.
    const drift::Clip source = m_project.tracks().at(trackIndex).clips.at(clipIndex);

    drift::Clip adjustment;
    adjustment.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    adjustment.type = drift::ClipType::Adjustment;
    adjustment.adjustmentKind = kind;
    adjustment.linkedClipId = source.id;
    adjustment.hiddenInLane = kind == drift::AdjustmentKind::VideoEffects;
    adjustment.timelineStart = source.timelineStart;
    adjustment.timelineDuration = source.timelineDuration;
    adjustment.srcIn = 0;
    adjustment.srcOut = source.timelineDuration;

    const int laneIndex = ensureAdjustmentLaneFor(trackIndex, kind, adjustment.timelineStart,
                                                  adjustment.timelineDuration);
    if (laneIndex < 0)
        return {};

    drift::Track &lane = m_project.tracks()[laneIndex];
    lane.clips.append(adjustment);
    return {laneIndex, static_cast<int>(lane.clips.size()) - 1};
}

drift::ClipRef AppController::sourceClipRef(int trackIndex, int clipIndex) const
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return {};
    const drift::Track &track = m_project.tracks().at(trackIndex);
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return {};

    const drift::Clip &clip = track.clips.at(clipIndex);
    if (clip.type != drift::ClipType::Adjustment || clip.linkedClipId.isEmpty())
        return {trackIndex, clipIndex};

    int foundTrack = -1;
    int foundClip = -1;
    if (findClipById(m_project, clip.linkedClipId, &foundTrack, &foundClip))
        return {foundTrack, foundClip};
    // A link whose clip is gone; syncLinkedAdjustments clears these, so this is a transient.
    return {};
}

drift::ClipRef AppController::effectHostRef(int trackIndex, int clipIndex,
                                            drift::AdjustmentKind kind, bool create)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return {};
    const drift::Track &track = m_project.tracks().at(trackIndex);
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return {};

    const drift::Clip &clip = track.clips.at(clipIndex);
    if (clip.type == drift::ClipType::Adjustment) {
        // An adjustment hosts its own stack, but only the one it is for — an audio stack must not
        // land on a video adjustment.
        return clip.adjustmentKind == kind ? drift::ClipRef{trackIndex, clipIndex} : drift::ClipRef{};
    }

    const QString clipId = clip.id;
    for (const int laneIndex : drift::adjustmentLaneIndexes(m_project, trackIndex)) {
        const drift::Track &lane = m_project.tracks().at(laneIndex);
        for (int c = 0; c < lane.clips.size(); ++c) {
            const drift::Clip &adjustment = lane.clips.at(c);
            if (adjustment.adjustmentKind == kind && adjustment.linkedClipId == clipId)
                return {laneIndex, c};
        }
    }

    if (!create)
        return {};
    return createLinkedAdjustment(trackIndex, clipIndex, kind);
}

const drift::Clip *AppController::effectHostClip(int trackIndex, int clipIndex,
                                                 drift::AdjustmentKind kind) const
{
    const drift::ClipRef ref =
        const_cast<AppController *>(this)->effectHostRef(trackIndex, clipIndex, kind,
                                                         /*create=*/false);
    if (ref.trackIndex < 0)
        return nullptr;
    return &m_project.tracks().at(ref.trackIndex).clips.at(ref.clipIndex);
}

bool AppController::redirectToEffectHost(int *trackIndex, int *clipIndex,
                                         drift::AdjustmentKind kind, bool create)
{
    if (!trackIndex || !clipIndex)
        return false;
    const drift::ClipRef ref = effectHostRef(*trackIndex, *clipIndex, kind, create);
    if (ref.trackIndex < 0)
        return false;
    *trackIndex = ref.trackIndex;
    *clipIndex = ref.clipIndex;
    return true;
}

void AppController::redirectToKeyframeHost(int *trackIndex, int *clipIndex,
                                           const QString &prop) const
{
    if (!trackIndex || !clipIndex)
        return;
    // Three kinds of payload live on an adjustment rather than on the clip the user selected: a
    // video effect's params, an audio effect's params, and a mask's scalars.
    const bool isEffect = prop.startsWith(QLatin1String("fx."));
    const bool isAudioEffect = prop.startsWith(QLatin1String("afx."));
    const bool isMask = prop.startsWith(QLatin1String("mask."));
    if (!isEffect && !isAudioEffect && !isMask)
        return;
    const drift::ClipRef ref =
        const_cast<AppController *>(this)->effectHostRef(*trackIndex, *clipIndex,
                                                         isMask           ? drift::AdjustmentKind::Mask
                                                         : isAudioEffect ? drift::AdjustmentKind::AudioEffects
                                                                          : drift::AdjustmentKind::VideoEffects,
                                                         /*create=*/false);
    if (ref.trackIndex < 0)
        return;
    *trackIndex = ref.trackIndex;
    *clipIndex = ref.clipIndex;
}

void AppController::syncLinkedAdjustments(drift::Project &project) const
{
    // One pass over every clip to build the span table, then one over the adjustments. Scanning
    // per adjustment instead would be quadratic on a timeline with many effects.
    QHash<QString, QPair<drift::TimeUs, drift::TimeUs>> spans;
    for (const drift::Track &track : project.tracks()) {
        if (track.isAdjustment())
            continue;
        for (const drift::Clip &clip : track.clips)
            spans.insert(clip.id, {clip.timelineStart, clip.timelineDuration});
    }

    for (drift::Track &track : project.tracks()) {
        if (!track.isAdjustment())
            continue;
        for (drift::Clip &adjustment : track.clips) {
            if (adjustment.linkedClipId.isEmpty()
                || adjustment.adjustmentKind == drift::AdjustmentKind::Transform)
                continue;
            const auto it = spans.constFind(adjustment.linkedClipId);
            if (it == spans.constEnd()) {
                // The clip it was pinned to is gone. Unlink rather than delete: the effects are
                // the user's work, and a stranded adjustment is visible and recoverable.
                adjustment.linkedClipId.clear();
                continue;
            }
            adjustment.timelineStart = it->first;
            adjustment.timelineDuration = it->second;
            adjustment.srcIn = 0;
            adjustment.srcOut = it->second;
        }
    }
}

void AppController::normalizeProjectStructure(const drift::Project *before)
{
    m_project.ensureTrackIds();
    const QList<QPair<QString, int>> selection = captureSelectionByTrackId();
    const QString transitionTrackId = trackIdAt(m_selectedTransitionTrack);
    // Belt and braces for anything that hands the editor a project built the old way — an
    // importer, a tool, a test. Both passes are cheap no-ops once the shape is right.
    drift::liftAdjustmentClipsToOwnTracks(m_project);
    drift::hoistClipEffectsToAdjustmentLanes(m_project);
    normalizeAdjustmentLanes(m_project);
    drift::normalizeTransformLayers(m_project.tracks(), before ? &before->tracks() : nullptr);
    drift::normalizeCameraLayers(m_project.tracks());
    m_project.ensureTrackIds();
    clampStoredTransitionDurations(m_project);
    restoreSelectionByTrackId(selection);
    if (m_selectedTransitionTrack >= 0)
        m_selectedTransitionTrack = m_project.trackIndexById(transitionTrackId);
}

// Projects saved before overlap stopped minting crossfades can carry a transition whose stored
// duration is far longer than the clips it joins — one in the wild spanned 23 seconds across a cut.
// The window is clamped at render time now, but the stored number is what inspect reports and what
// the transition bar draws, so bring it back inside the clips on the way in.
void AppController::clampStoredTransitionDurations(drift::Project &project) const
{
    for (drift::Track &track : project.tracks()) {
        for (drift::Transition &transition : track.transitions) {
            if (transition.durationUs <= 0)
                continue;
            const drift::Clip *fromClip = drift::clipById(track, transition.fromClipId);
            const drift::Clip *toClip = drift::clipById(track, transition.toClipId);
            if (!fromClip || !toClip)
                continue;
            const drift::TimeUs longest =
                qMin(fromClip->timelineDuration, toClip->timelineDuration);
            if (longest > 0 && transition.durationUs > longest)
                transition.durationUs = longest;
        }
    }
}

void AppController::normalizeAdjustmentLanes(drift::Project &project) const
{
    bool hasLane = false;
    for (const drift::Track &track : project.tracks()) {
        if (track.type == drift::TrackType::Adjustment
            && track.adjustmentScope == drift::AdjustmentScope::ParentTrack) {
            hasLane = true;
            break;
        }
    }
    if (!hasLane)
        return;

    project.ensureTrackIds();

    // A lane whose parent became an adjustment track would have two scopes at once; demote it to
    // standalone rather than leave it ambiguous.
    for (drift::Track &track : project.tracks()) {
        if (!track.isAdjustmentLane())
            continue;
        const int parentIndex = project.trackIndexById(track.parentTrackId);
        if (parentIndex < 0 || project.tracks().at(parentIndex).isAdjustment()) {
            track.parentTrackId.clear();
            track.adjustmentScope = drift::AdjustmentScope::AllBelow;
        }
    }

    // A lane exists to hold effects; once its last clip is gone it is only clutter. Lanes are
    // empty only transiently while being created, and no normalize runs inside that window.
    for (int i = project.tracks().size() - 1; i >= 0; --i) {
        if (project.tracks().at(i).isAdjustmentLane() && project.tracks().at(i).clips.isEmpty())
            project.tracks().removeAt(i);
    }

    // Re-gather each parent's lanes directly below it. Lanes keep their relative order, so the
    // sequence their effects apply in survives a track move — and moving a track carries its
    // lanes along, which is what makes reordering behave.
    QList<drift::Track> ordered;
    ordered.reserve(project.tracks().size());
    QSet<QString> placed;
    for (const drift::Track &track : project.tracks()) {
        if (track.isAdjustmentLane())
            continue;
        ordered.append(track);
        placed.insert(track.id);
        for (const drift::Track &lane : project.tracks()) {
            if (lane.isAdjustmentLane() && lane.parentTrackId == track.id
                && !placed.contains(lane.id)) {
                ordered.append(lane);
                placed.insert(lane.id);
            }
        }
    }
    // Anything left is a lane whose parent vanished mid-pass; keep it rather than drop it.
    for (const drift::Track &track : project.tracks()) {
        if (!placed.contains(track.id))
            ordered.append(track);
    }
    project.tracks() = ordered;
}

QString AppController::trackIdAt(int trackIndex) const
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return {};
    return m_project.tracks().at(trackIndex).id;
}

QList<QPair<QString, int>> AppController::captureSelectionByTrackId() const
{
    QList<QPair<QString, int>> captured;
    captured.reserve(m_selection.size());
    m_capturedPrimaryClipId = isValidClipIndex(m_selectedTrack, m_selectedClip)
        ? m_project.tracks().at(m_selectedTrack).clips.at(m_selectedClip).id
        : QString();
    for (const QPair<int, int> &pair : m_selection) {
        const QString id = trackIdAt(pair.first);
        if (!id.isEmpty())
            captured.append({id, pair.second});
    }
    return captured;
}

void AppController::restoreSelectionByTrackId(const QList<QPair<QString, int>> &captured)
{
    m_selection.clear();
    for (const QPair<QString, int> &pair : captured) {
        const int trackIndex = m_project.trackIndexById(pair.first);
        if (trackIndex < 0)
            continue;
        if (pair.second < 0 || pair.second >= m_project.tracks().at(trackIndex).clips.size())
            continue;
        m_selection.append(qMakePair(trackIndex, pair.second));
    }
    if (m_selection.isEmpty()) {
        m_selectedTrack = -1;
        m_selectedClip = -1;
    } else {
        // The primary is not necessarily the last entry: a linked pair is [video, audio], and
        // falling back to the last would hand the inspector to the audio after every edit.
        const QPair<int, int> *primary = &m_selection.constLast();
        for (const QPair<int, int> &pair : std::as_const(m_selection)) {
            if (m_project.tracks().at(pair.first).clips.at(pair.second).id == m_capturedPrimaryClipId) {
                primary = &pair;
                break;
            }
        }
        m_selectedTrack = primary->first;
        m_selectedClip = primary->second;
    }
    m_capturedPrimaryClipId.clear();
}

void AppController::addAdjustmentClip(double atSeconds, double durationSeconds)
{
    addAdjustmentClipAt(-1, atSeconds, durationSeconds);
}

void AppController::addAdjustmentClipAt(int trackIndex, double atSeconds, double durationSeconds)
{
    addAdjustmentClipWithEffect(QString(), trackIndex, atSeconds, durationSeconds);
}

drift::Clip AppController::buildAdjustmentClip(const QString &effectId, drift::TimeUs start, drift::TimeUs durUs) const
{
    drift::Clip clip;
    clip.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    clip.type = drift::ClipType::Adjustment;
    clip.name = tr("Adjustment Layer");
    clip.timelineStart = start;
    clip.timelineDuration = durUs;
    clip.srcIn = 0;
    clip.srcOut = durUs;

    if (!effectId.isEmpty()) {
        if (const EffectPresetEntry *def = effectDefForId(effectId)) {
            drift::Effect effect;
            effect.name = def->filterName;
            effect.catalogId = def->meta.id;
            for (auto it = def->fixedParams.constBegin(); it != def->fixedParams.constEnd(); ++it)
                effect.parameters.insert(it.key(), it.value());
            for (const drift::EffectParamSpec &p : def->meta.parameters)
                p.insertDefault(effect.parameters);
            clip.effects.append(effect);
            clip.name = tr("Adjustment (%1)").arg(def->filterName);
        }
    }
    return clip;
}

void AppController::addAdjustmentClipWithEffect(const QString &effectId, int trackIndex, double atSeconds, double durationSeconds)
{
    QString why;
    if (!effectId.isEmpty() && !effectFitsTrack(-1, effectId, &why)) {
        setLastMessage(why, QStringLiteral("warning"));
        return;
    }
    const drift::Project before = m_project;

    int target = trackIndex;
    const drift::TimeUs durUs = durationSeconds > 0.0
        ? drift::secondsToUs(durationSeconds)
        : drift::imageClipDurationUs();
    const drift::TimeUs startSeconds = atSeconds < 0.0 ? m_playheadUs : drift::secondsToUs(atSeconds);

    // A free-standing adjustment goes on a standalone adjustment track — never into a nested
    // lane, which is scoped to somebody else's track and would silently change what it applies to.
    const auto usableTarget = [this](int index) {
        if (index < 0 || index >= m_project.tracks().size())
            return false;
        const drift::Track &t = m_project.tracks().at(index);
        return t.isAdjustment() && !t.isAdjustmentLane() && !t.isTransformLayer();
    };

    if (!usableTarget(target)) {
        int candidateTrack = -1;
        for (int i = 0; i < m_project.tracks().size(); ++i) {
            if (!usableTarget(i))
                continue;
            const drift::Track &t = m_project.tracks().at(i);
            bool hasOverlap = false;
            for (const drift::Clip &c : t.clips) {
                if (startSeconds < c.timelineEnd() && startSeconds + durUs > c.timelineStart) {
                    hasOverlap = true;
                    break;
                }
            }
            if (!hasOverlap) {
                candidateTrack = i;
                break;
            }
        }
        if (candidateTrack >= 0) {
            target = candidateTrack;
        } else {
            target = drift::insertTrackAtTopForClipType(m_project, drift::ClipType::Adjustment);
        }
    }
    if (target < 0)
        return;

    drift::Track &track = m_project.tracks()[target];
    const drift::TimeUs start = drift::resolveClipStart(m_project, track, -1, startSeconds,
                                                        durUs, m_snapEnabled, m_playheadUs);

    track.clips.append(buildAdjustmentClip(effectId, start, durUs));
    const int newClipIndex = track.clips.size() - 1;
    pushProjectEdit(before, tr("Add adjustment layer"));
    finishEdit(tr("Adjustment layer added"));
    selectClip(target, newClipIndex);
}
namespace {


drift::AdjustmentKind adjustmentKindFromArg(const QString &kind)
{
    return drift::adjustmentKindFromString(kind.trimmed());
}

} // namespace

void AppController::addAdjustmentTrack(const QString &kind)
{
    if (kind.trimmed() == QLatin1String("transform")) {
        addTransformTrack();
        return;
    }
    const drift::Project before = m_project;

    drift::Track track;
    track.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    track.type = drift::TrackType::Adjustment;
    track.adjustmentScope = drift::AdjustmentScope::AllBelow;
    m_project.tracks().prepend(track);

    // Every stored index just moved down one.
    const QList<QPair<QString, int>> selection = captureSelectionByTrackId();
    restoreSelectionByTrackId(selection);
    if (m_selectedTransitionTrack >= 0)
        ++m_selectedTransitionTrack;

    // An audio adjustment track has nothing to show until it holds a clip, and nothing else
    // offers to make one, so it arrives with one at the playhead.
    QString newClipId;
    if (adjustmentKindFromArg(kind) == drift::AdjustmentKind::AudioEffects) {
        drift::Clip clip;
        clip.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        clip.type = drift::ClipType::Adjustment;
        clip.adjustmentKind = drift::AdjustmentKind::AudioEffects;
        clip.name = tr("Audio adjustment");
        clip.timelineStart = m_playheadUs;
        clip.timelineDuration = drift::imageClipDurationUs();
        clip.srcOut = clip.timelineDuration;
        m_project.tracks()[0].clips.append(clip);
        newClipId = clip.id;
    }
    pushProjectEdit(before, tr("Add adjustment track"));
    finishEdit(tr("Adjustment track added"));
    int trackIndex = -1;
    int clipIndex = -1;
    if (!newClipId.isEmpty() && findClipById(m_project, newClipId, &trackIndex, &clipIndex))
        selectClip(trackIndex, clipIndex);
}

void AppController::moveAdjustmentToLane(int fromTrack, int fromClip, int parentTrackIndex,
                                         double atSeconds)
{
    if (fromTrack < 0 || fromTrack >= m_project.tracks().size())
        return;
    if (fromClip < 0 || fromClip >= m_project.tracks().at(fromTrack).clips.size())
        return;
    const drift::Clip &source = m_project.tracks().at(fromTrack).clips.at(fromClip);
    if (source.type != drift::ClipType::Adjustment
        || source.adjustmentKind == drift::AdjustmentKind::Transform)
        return;
    if (parentTrackIndex < 0 || parentTrackIndex >= m_project.tracks().size())
        return;
    if (m_project.tracks().at(parentTrackIndex).isAdjustment())
        return;

    const drift::Project before = m_project;
    m_project.ensureTrackIds();

    // Copied before anything moves: creating the lane can insert a track and invalidate both the
    // reference and the indices the caller passed.
    drift::Clip adjustment = m_project.tracks().at(fromTrack).clips.at(fromClip);
    // Re-scoping breaks the pin: a linked adjustment belongs to its clip's track, so carrying the
    // link onto a different one would leave it following a clip it no longer applies to.
    adjustment.linkedClipId.clear();
    if (atSeconds >= 0.0)
        adjustment.timelineStart = drift::secondsToUs(atSeconds);

    const QString sourceTrackId = m_project.tracks().at(fromTrack).id;
    const QString parentTrackId = m_project.tracks().at(parentTrackIndex).id;

    const int laneIndex = ensureAdjustmentLaneFor(m_project.trackIndexById(parentTrackId),
                                                  adjustment.adjustmentKind,
                                                  adjustment.timelineStart,
                                                  adjustment.timelineDuration);
    if (laneIndex < 0)
        return;

    const int sourceIndex = m_project.trackIndexById(sourceTrackId);
    if (sourceIndex < 0)
        return;
    m_project.tracks()[sourceIndex].clips.removeAt(fromClip);
    m_project.tracks()[laneIndex].clips.append(adjustment);

    // A track that existed only to hold this adjustment is left empty; dropping it keeps the
    // timeline from accumulating dead rows every time one is dragged into a lane.
    if (m_project.tracks().at(sourceIndex).isAdjustment()
        && m_project.tracks().at(sourceIndex).clips.isEmpty()) {
        m_project.tracks().removeAt(sourceIndex);
    }

    normalizeAdjustmentLanes(m_project);
    clearSelection();
    pushProjectEdit(before, tr("Nest adjustment in track"));
    finishEdit(tr("Adjustment nested"));
}

void AppController::moveAdjustmentToOwnTrack(int fromTrack, int fromClip, double atSeconds)
{
    if (fromTrack < 0 || fromTrack >= m_project.tracks().size())
        return;
    if (fromClip < 0 || fromClip >= m_project.tracks().at(fromTrack).clips.size())
        return;
    if (m_project.tracks().at(fromTrack).clips.at(fromClip).type != drift::ClipType::Adjustment
        || m_project.tracks().at(fromTrack).clips.at(fromClip).adjustmentKind
               == drift::AdjustmentKind::Transform)
        return;

    const drift::Project before = m_project;
    m_project.ensureTrackIds();

    drift::Clip adjustment = m_project.tracks().at(fromTrack).clips.at(fromClip);
    // Standalone means "everything composited below", which is not something a clip can be
    // pinned to.
    adjustment.linkedClipId.clear();
    if (atSeconds >= 0.0)
        adjustment.timelineStart = drift::secondsToUs(atSeconds);

    const QString sourceTrackId = m_project.tracks().at(fromTrack).id;

    // Above the track it was nested in, so it keeps affecting that track and gains the ones
    // below it. A lane is stored *below* its parent, so the lane's own index is one slot too
    // low — landing there would put the new track under the one it came from, where it no
    // longer applies to it at all.
    const int laneParent = drift::adjustmentLaneParentIndex(m_project, fromTrack);
    const int insertAt = qMax(0, laneParent >= 0 ? laneParent : fromTrack);

    drift::Track track;
    track.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    track.type = drift::TrackType::Adjustment;
    track.adjustmentScope = drift::AdjustmentScope::AllBelow;
    track.clips.append(adjustment);
    m_project.tracks().insert(insertAt, track);

    const int sourceIndex = m_project.trackIndexById(sourceTrackId);
    if (sourceIndex >= 0) {
        m_project.tracks()[sourceIndex].clips.removeAt(fromClip);
        if (m_project.tracks().at(sourceIndex).isAdjustment()
            && m_project.tracks().at(sourceIndex).clips.isEmpty()) {
            m_project.tracks().removeAt(sourceIndex);
        }
    }

    normalizeAdjustmentLanes(m_project);
    clearSelection();
    pushProjectEdit(before, tr("Detach adjustment to its own track"));
    finishEdit(tr("Adjustment detached"));
}

void AppController::unlinkAdjustment(int trackIndex, int clipIndex)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;
    if (clipIndex < 0 || clipIndex >= m_project.tracks().at(trackIndex).clips.size())
        return;
    const drift::Clip &clip = m_project.tracks().at(trackIndex).clips.at(clipIndex);
    if (clip.type != drift::ClipType::Adjustment || clip.linkedClipId.isEmpty())
        return;

    const drift::Project before = m_project;
    m_project.tracks()[trackIndex].clips[clipIndex].linkedClipId.clear();
    pushProjectEdit(before, tr("Unlink adjustment"));
    finishEdit(tr("Adjustment unlinked"));
}

void AppController::toggleEffectsLayerRevealed(int trackIndex, int clipIndex)
{
    const drift::ClipRef ref = effectHostRef(trackIndex, clipIndex, drift::AdjustmentKind::VideoEffects,
                                             /*create=*/false);
    if (ref.trackIndex < 0)
        return;
    const drift::Project before = m_project;
    drift::Clip &layer = m_project.tracks()[ref.trackIndex].clips[ref.clipIndex];
    layer.hiddenInLane = !layer.hiddenInLane;
    pushProjectEdit(before, layer.hiddenInLane ? tr("Hide effects layer") : tr("Show effects layer"));
    finishEdit(layer.hiddenInLane ? tr("Effects layer hidden") : tr("Effects layer shown"));
}

void AppController::hideEffectsLayer(int trackIndex, int clipIndex)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;
    if (clipIndex < 0 || clipIndex >= m_project.tracks().at(trackIndex).clips.size())
        return;
    if (m_project.tracks().at(trackIndex).clips.at(clipIndex).hiddenInLane)
        return;
    const drift::Project before = m_project;
    m_project.tracks()[trackIndex].clips[clipIndex].hiddenInLane = true;
    pushProjectEdit(before, tr("Hide effects layer"));
    finishEdit(tr("Effects layer hidden"));
}

int AppController::effectsLayerHostClip(int fromTrack, int fromClip, int targetTrack, double atSeconds) const
{
    if (fromTrack < 0 || fromTrack >= m_project.tracks().size() || targetTrack < 0
        || targetTrack >= m_project.tracks().size())
        return -1;
    const QList<drift::Clip> &clips = m_project.tracks().at(fromTrack).clips;
    if (fromClip < 0 || fromClip >= clips.size())
        return -1;
    const drift::Clip &layer = clips.at(fromClip);
    if (layer.type != drift::ClipType::Adjustment || layer.adjustmentKind != drift::AdjustmentKind::VideoEffects
        || layer.linkedClipId.isEmpty())
        return -1;
    const QList<drift::Clip> &hostClips = m_project.tracks().at(targetTrack).clips;
    const drift::TimeUs at = drift::secondsToUs(atSeconds);
    for (int i = 0; i < hostClips.size(); ++i) {
        const drift::Clip &host = hostClips.at(i);
        if (host.id == layer.linkedClipId && at >= host.timelineStart && at < host.timelineEnd())
            return i;
    }
    return -1;
}

void AppController::relinkAdjustment(int trackIndex, int clipIndex, int mediaTrack, int mediaClip)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;
    if (clipIndex < 0 || clipIndex >= m_project.tracks().at(trackIndex).clips.size())
        return;
    if (m_project.tracks().at(trackIndex).clips.at(clipIndex).type != drift::ClipType::Adjustment
        || m_project.tracks().at(trackIndex).clips.at(clipIndex).adjustmentKind
               == drift::AdjustmentKind::Transform)
        return;
    if (mediaTrack < 0 || mediaTrack >= m_project.tracks().size())
        return;
    if (mediaClip < 0 || mediaClip >= m_project.tracks().at(mediaTrack).clips.size())
        return;

    const drift::Clip &target = m_project.tracks().at(mediaTrack).clips.at(mediaClip);
    if (target.type == drift::ClipType::Adjustment)
        return;
    // Pinning only means something for a lane nested in the target's own track — anywhere else
    // the adjustment would follow a clip it does not apply to.
    if (drift::adjustmentLaneParentIndex(m_project, trackIndex) != mediaTrack)
        return;

    const drift::Project before = m_project;
    m_project.tracks()[trackIndex].clips[clipIndex].linkedClipId = target.id;
    // finishEdit's sync pass is what actually snaps the span onto the clip.
    pushProjectEdit(before, tr("Link adjustment to clip"));
    finishEdit(tr("Adjustment linked"));
}

void AppController::addStickerClip(const QString &stickerId, double atSeconds, int trackIndex)
{
    QString path;
    QString label;
    for (const QVariant &item : builtinStickers()) {
        const QVariantMap sticker = item.toMap();
        if (sticker.value(QStringLiteral("id")).toString() == stickerId) {
            path = sticker.value(QStringLiteral("path")).toString();
            label = sticker.value(QStringLiteral("label")).toString();
            break;
        }
    }
    if (path.isEmpty())
        return;

    addImageOverlayClip(path, label.isEmpty() ? stickerId : label, QString(), atSeconds,
                        QStringLiteral("Sticker added"), trackIndex);
}

QVariantList AppController::emojiCatalog() const
{
    QVariantList out;
    for (const EmojiEntry &entry : ::emojiCatalog()) {
        out.append(QVariantMap{
            {QStringLiteral("emoji"), entry.emoji},
            {QStringLiteral("name"), entry.name},
            {QStringLiteral("group"), entry.group},
            {QStringLiteral("keywords"), entry.keywords},
        });
    }
    return out;
}

QStringList AppController::emojiGroups() const
{
    return ::emojiGroups();
}

QString AppController::emojiFontFamily() const
{
    return ::emojiFontFamily();
}

void AppController::addEmojiClip(const QString &emoji, const QString &name, double atSeconds,
                                 int trackIndex)
{
    const QString path = emojiImagePath(emoji);
    if (path.isEmpty()) {
        setLastMessage(tr("Install the emoji sticker pack to add emoji"), QStringLiteral("warning"));
        return;
    }
    addImageOverlayClip(path, name.isEmpty() ? emoji : name, emoji, atSeconds,
                        QStringLiteral("Emoji added"), trackIndex);
}

void AppController::addImageOverlayClip(const QString &path, const QString &name,
                                        const QString &emoji, double atSeconds,
                                        const QString &undoText, int requestedTrack)
{
    const drift::Project before = m_project;
    const drift::TimeUs wantStart = atSeconds < 0.0 ? m_playheadUs : drift::secondsToUs(atSeconds);
    int trackIndex = requestedTrack;
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size()
        || !m_project.tracks().at(trackIndex).allowsClipType(drift::ClipType::Image)) {
        trackIndex = drift::ensureFreeTrackForClipType(
            m_project, drift::ClipType::Image, wantStart, drift::imageClipDurationUs(), true);
    }
    if (trackIndex < 0)
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    const drift::TimeUs startSeconds = wantStart;
    const drift::TimeUs start = drift::resolveClipStart(m_project, track, -1, startSeconds,
                                                        drift::imageClipDurationUs(), m_snapEnabled, m_playheadUs);

    drift::Clip clip;
    clip.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    clip.type = drift::ClipType::Image;
    clip.name = name;
    clip.path = path;
    clip.thumbnailPath = path;
    clip.filmstripPath = path;
    clip.emoji = emoji;
    clip.timelineStart = start;
    clip.timelineDuration = drift::imageClipDurationUs();
    clip.srcIn = 0;
    clip.srcOut = drift::imageClipDurationUs();
    applyDefaultVisualLayout(clip, m_project.width(), m_project.height());

    track.clips.append(clip);
    const int newClipIndex = track.clips.size() - 1;
    pushProjectEdit(before, undoText);
    finishEdit(undoText);
    selectClip(trackIndex, newClipIndex);
}
namespace drift::appdetail {


std::optional<drift::ClipType> placeableClipType(const QString &kind)
{
    if (kind == QLatin1String("shape"))
        return drift::ClipType::Shape;
    if (kind == QLatin1String("sticker") || kind == QLatin1String("emoji"))
        return drift::ClipType::Image;
    if (kind == QLatin1String("textStyle"))
        return drift::ClipType::Text;
    if (kind == QLatin1String("adjustment"))
        return drift::ClipType::Adjustment;
    return std::nullopt;
}

bool isClipTargetedKind(const QString &kind)
{
    return kind == QLatin1String("effect") || kind == QLatin1String("audioEffect")
           || kind == QLatin1String("template") || kind == QLatin1String("effectStack")
           || kind == QLatin1String("mask") || kind == QLatin1String("maskMedia");
}

} // namespace drift::appdetail
namespace {


int clipIndexAtTime(const drift::Track &track, drift::TimeUs t)
{
    // Last match wins: with overlap allowed, the later clip is the one drawn on top.
    int found = -1;
    for (int i = 0; i < track.clips.size(); ++i) {
        const drift::Clip &clip = track.clips.at(i);
        if (t >= clip.timelineStart && t < clip.timelineEnd())
            found = i;
    }
    return found;
}

} // namespace
namespace drift::appdetail {


QVariantMap rejectDrop(const QString &message)
{
    return {{QStringLiteral("accepted"), false}, {QStringLiteral("mode"), QStringLiteral("none")},
            {QStringLiteral("message"), message}};
}

} // namespace drift::appdetail

bool AppController::isPlaceableDropKind(const QString &kind) const
{
    return placeableClipType(kind).has_value();
}

bool AppController::trackAcceptsDropKind(int trackIndex, const QString &kind) const
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return false;
    const drift::Track &track = m_project.tracks().at(trackIndex);
    if (const std::optional<drift::ClipType> type = placeableClipType(kind))
        return track.allowsClipType(*type) && !track.isAdjustmentLane() && !track.isTransformLayer();
    return true;
}

int AppController::transitionJunctionAt(int trackIndex, double seconds) const
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return -1;
    const drift::Track &track = m_project.tracks().at(trackIndex);
    if (track.type != drift::TrackType::Video && track.type != drift::TrackType::Shape
        && track.type != drift::TrackType::Text && track.type != drift::TrackType::Audio)
        return -1;
    // A cut is a target for a quarter second either side of it; an overlap for its whole length.
    constexpr double kCutReach = 0.25;
    constexpr double kTouching = 0.001;
    int best = -1;
    double bestDistance = 1e9;
    for (int i = 0; i < track.clips.size(); ++i) {
        const drift::Clip &left = track.clips.at(i);
        const double leftEnd = drift::usToSeconds(left.timelineEnd());
        for (int j = 0; j < track.clips.size(); ++j) {
            const drift::Clip &right = track.clips.at(j);
            if (i == j || right.timelineStart < left.timelineStart)
                continue;
            const double rightStart = drift::usToSeconds(right.timelineStart);
            if (rightStart - leftEnd > kTouching)
                continue;
            const double regionStart = rightStart < leftEnd ? rightStart : leftEnd - kCutReach;
            const double regionEnd = rightStart < leftEnd ? leftEnd : leftEnd + kCutReach;
            if (seconds < regionStart || seconds > regionEnd)
                continue;
            const double distance = qAbs(seconds - (regionStart + regionEnd) / 2.0);
            if (distance < bestDistance) {
                bestDistance = distance;
                best = i;
            }
        }
    }
    return best;
}

QVariantMap AppController::planAssetDrop(const QString &kind, const QString &payload, int trackIndex,
                                         double seconds, int newTrackIndex) const
{
    const double at = qMax(0.0, seconds);
    const int trackCount = int(m_project.tracks().size());
    const bool onTrack = trackIndex >= 0 && trackIndex < trackCount;

    if (const std::optional<drift::ClipType> type = placeableClipType(kind)) {
        const double duration = drift::usToSeconds(*type == drift::ClipType::Text
                                                       ? drift::kTextClipDurationUs
                                                       : drift::imageClipDurationUs());
        QVariantMap plan{{QStringLiteral("accepted"), true},
                         {QStringLiteral("landingStart"), at},
                         {QStringLiteral("landingDuration"), duration},
                         {QStringLiteral("track"), -1},
                         {QStringLiteral("newTrackIndex"), -1}};
        if (newTrackIndex >= 0 || !onTrack || !trackAcceptsDropKind(trackIndex, kind)) {
            plan.insert(QStringLiteral("mode"), QStringLiteral("newTrack"));
            plan.insert(QStringLiteral("newTrackIndex"),
                        newTrackIndex >= 0 ? qMin(newTrackIndex, trackCount) : 0);
            return plan;
        }
        plan.insert(QStringLiteral("mode"), QStringLiteral("gap"));
        plan.insert(QStringLiteral("track"), trackIndex);
        return plan;
    }

    if (!onTrack)
        return rejectDrop();
    const drift::Track &track = m_project.tracks().at(trackIndex);

    if (kind == QLatin1String("transition")) {
        const int left = transitionJunctionAt(trackIndex, at);
        if (left < 0)
            return rejectDrop(tr("Drop a transition where two clips meet."));
        return {{QStringLiteral("accepted"), true}, {QStringLiteral("mode"), QStringLiteral("junction")},
                {QStringLiteral("track"), trackIndex}, {QStringLiteral("clip"), left}};
    }

    if (!isClipTargetedKind(kind))
        return rejectDrop();
    // A transform layer carries a transform and nothing else; effects and masks belong on the
    // clips it moves, or on an adjustment layer.
    if (track.isTransformLayer())
        return rejectDrop(tr("Transform layers take no effects or masks."));

    const bool audioTrack = track.type == drift::TrackType::Audio;
    const bool maskKind = kind == QLatin1String("mask") || kind == QLatin1String("maskMedia");
    const int clipIndex = clipIndexAtTime(track, drift::secondsToUs(at));
    if (clipIndex >= 0) {
        const bool fits = kind == QLatin1String("audioEffect")
                              ? (audioTrack || track.type == drift::TrackType::Video)
                              : !audioTrack && !(maskKind && track.isAdjustmentLane());
        if (!fits) {
            return rejectDrop(kind == QLatin1String("audioEffect")
                                  ? tr("Audio effects go on clips with sound.")
                                  : tr("That goes on a video, image, shape or text clip."));
        }
        return {{QStringLiteral("accepted"), true}, {QStringLiteral("mode"), QStringLiteral("clip")},
                {QStringLiteral("track"), trackIndex}, {QStringLiteral("clip"), clipIndex}};
    }

    // Over empty track. An effect there becomes an adjustment layer over whatever is below it,
    // and a mask a mask lane — both only make sense over pictures.
    const bool pictureTrack = track.type == drift::TrackType::Video;
    QString why;
    if (pictureTrack && kind == QLatin1String("effect") && !effectFitsTrack(-1, payload, &why))
        return rejectDrop(why);
    if (pictureTrack && (kind == QLatin1String("effect") || kind == QLatin1String("effectStack")
                         || maskKind)) {
        const drift::TimeUs duration = kind == QLatin1String("maskMedia")
                                           ? m_masking->assetMaskDurationUs(payload.toInt())
                                           : drift::imageClipDurationUs();
        return {{QStringLiteral("accepted"), true}, {QStringLiteral("mode"), QStringLiteral("gap")},
                {QStringLiteral("track"), trackIndex},
                {QStringLiteral("landingStart"), at},
                {QStringLiteral("landingDuration"), drift::usToSeconds(duration)}};
    }
    return rejectDrop(tr("Drop that onto a clip to apply it."));
}

QVariantMap AppController::dropAsset(const QString &kind, const QString &payload, const QString &label,
                                     int trackIndex, double seconds, int newTrackIndex)
{
    const QVariantMap plan = planAssetDrop(kind, payload, trackIndex, seconds, newTrackIndex);
    // A refusal's message is the caller's to show: it knows whether a toast fits.
    if (!plan.value(QStringLiteral("accepted")).toBool())
        return plan;
    const QString mode = plan.value(QStringLiteral("mode")).toString();
    const int track = plan.value(QStringLiteral("track")).toInt();
    const int clip = plan.value(QStringLiteral("clip"), -1).toInt();
    const double at = plan.value(QStringLiteral("landingStart"), qMax(0.0, seconds)).toDouble();

    if (const std::optional<drift::ClipType> type = placeableClipType(kind)) {
        // A new track and the clip on it are one edit, so one undo takes both away.
        const bool newTrack = mode == QLatin1String("newTrack");
        int target = track;
        if (newTrack) {
            m_mcpController->beginBatch();
            target = drift::insertTrackAboveForClipType(
                m_project, plan.value(QStringLiteral("newTrackIndex")).toInt(), *type);
        }
        if (kind == QLatin1String("shape"))
            addShapeClipAt(payload, target, at);
        else if (kind == QLatin1String("sticker"))
            addStickerClip(payload, at, target);
        else if (kind == QLatin1String("emoji"))
            addEmojiClip(payload, label, at, target);
        else if (kind == QLatin1String("textStyle"))
            addTextClip(QString(), at, payload, target);
        else if (kind == QLatin1String("adjustment"))
            addAdjustmentClipAt(target, at);
        if (newTrack)
            m_mcpController->endBatch(tr("Add to new track"), true);
        return plan;
    }

    if (mode == QLatin1String("junction")) {
        QString transition = QStringLiteral("crossfade");
        for (const QVariant &entry : transitionKindsForTrack(track)) {
            if (entry.toMap().value(QStringLiteral("kind")).toString() == payload)
                transition = payload;
        }
        addTransition(track, clip, transition, m_preferences->transitionDurationSeconds());
        return plan;
    }

    if (mode == QLatin1String("clip")) {
        if (kind == QLatin1String("effect"))
            addEffect(track, clip, payload);
        else if (kind == QLatin1String("audioEffect"))
            addAudioEffect(track, clip, payload);
        else if (kind == QLatin1String("template"))
            applyEffectTemplate(track, clip, payload);
        else if (kind == QLatin1String("effectStack"))
            applyEffectPreset(track, clip, payload);
        else if (kind == QLatin1String("mask"))
            addMaskToClip(track, clip, payload);
        else if (kind == QLatin1String("maskMedia"))
            m_masking->addAssetMaskToClip(track, clip, payload.toInt());
        // Effects and masks select what they made (the stack's host, the mask clip), which is
        // where their inspector lives; the rest leave the target selected.
        if (kind != QLatin1String("effect") && kind != QLatin1String("mask")
            && kind != QLatin1String("maskMedia"))
            selectClip(track, clip);
        return plan;
    }

    // mode == "gap". An effect dropped on a track's empty space grades what is under that track,
    // so its adjustment goes directly above it: reusing a free standalone adjustment track that
    // already sits there, else a new one. Any other placement changes what it applies to.
    const auto adjustmentTrackAbove = [this, at](int trackIndex) {
        const drift::TimeUs start = drift::secondsToUs(at);
        const drift::TimeUs end = start + drift::imageClipDurationUs();
        if (trackIndex > 0) {
            const drift::Track &above = m_project.tracks().at(trackIndex - 1);
            bool free = above.isAdjustment() && !above.isAdjustmentLane() && !above.isTransformLayer();
            for (const drift::Clip &clip : above.clips)
                free = free && (clip.timelineEnd() <= start || clip.timelineStart >= end);
            if (free)
                return trackIndex - 1;
        }
        return drift::insertTrackAboveForClipType(m_project, trackIndex, drift::ClipType::Adjustment);
    };
    if (kind == QLatin1String("effect")) {
        m_mcpController->beginBatch();
        addAdjustmentClipWithEffect(payload, adjustmentTrackAbove(track), at);
        m_mcpController->endBatch(tr("Add adjustment layer"), true);
    } else if (kind == QLatin1String("mask")) {
        addMaskLaneClip(track, payload, at);
    } else if (kind == QLatin1String("maskMedia")) {
        m_masking->addAssetLaneMask(track, payload.toInt(), at);
    } else if (kind == QLatin1String("effectStack")) {
        m_mcpController->beginBatch();
        addAdjustmentClipWithEffect(QString(), adjustmentTrackAbove(track), at);
        if (m_selectedTrack >= 0 && m_selectedClip >= 0)
            applyEffectPreset(m_selectedTrack, m_selectedClip, payload);
        m_mcpController->endBatch(tr("Add adjustment layer"), true);
    }
    return plan;
}

QVariantMap AppController::cameraStateAtPlayhead() const
{
    return cameraStateAt(drift::usToSeconds(m_playheadUs));
}

QVariantMap AppController::cameraStateAt(double seconds, int trackIndex, int clipIndex) const
{
    const QList<drift::Track> &tracks = m_project.tracks();
    const drift::TimeUs timeUs = drift::secondsToUs(seconds);
    const drift::Clip *found = nullptr;
    if (trackIndex >= 0 && isValidClipIndex(trackIndex, clipIndex)) {
        const drift::Clip &clip = tracks.at(trackIndex).clips.at(clipIndex);
        if (drift::isCameraClip(clip)) {
            found = &clip;
        }
    } else {
        trackIndex = drift::cameraTrackIndex(tracks);
        if (trackIndex >= 0 && !tracks.at(trackIndex).hidden) {
            for (int c = 0; c < tracks.at(trackIndex).clips.size(); ++c) {
                const drift::Clip &clip = tracks.at(trackIndex).clips.at(c);
                if (drift::isCameraClip(clip) && clip.containsTime(timeUs)) {
                    found = &clip;
                    clipIndex = c;
                    break;
                }
            }
        }
    }
    if (!found)
        return QVariantMap{{QStringLiteral("active"), false}};
    const QSizeF project(m_project.width(), m_project.height());
    const drift::SceneCamera3d camera = drift::sceneCameraFromClip(*found, timeUs, 1.0, project);
    const QVector3D eye = drift::sceneCameraEye(camera);
    return QVariantMap{
        {QStringLiteral("active"), true},
        {QStringLiteral("track"), trackIndex},
        {QStringLiteral("clip"), clipIndex},
        {QStringLiteral("start"), drift::usToSeconds(found->timelineStart)},
        {QStringLiteral("dur"), drift::usToSeconds(found->timelineDuration)},
        {QStringLiteral("x"), camera.positionX},
        {QStringLiteral("y"), camera.positionY},
        {QStringLiteral("z"), camera.positionZ},
        {QStringLiteral("rotationX"), camera.rotationX},
        {QStringLiteral("rotationY"), camera.rotationY},
        {QStringLiteral("rotation"), camera.rotationZ},
        {QStringLiteral("aim"), camera.aimAtTarget},
        {QStringLiteral("poiX"), camera.targetX},
        {QStringLiteral("poiY"), camera.targetY},
        {QStringLiteral("poiZ"), camera.targetZ},
        {QStringLiteral("zoom"), camera.zoom},
        {QStringLiteral("focalLength"), drift::focalLengthFromZoom(camera.zoom, project.height())},
        {QStringLiteral("fov"), drift::fovFromZoom(camera.zoom, project.height())},
        {QStringLiteral("eye"), QVariantList{eye.x(), eye.y(), eye.z()}},
    };
}

double AppController::cameraZoomFromFocalLength(double millimetres) const
{
    return drift::zoomFromFocalLength(millimetres, m_project.height());
}

double AppController::cameraZoomFromFov(double degrees) const
{
    return drift::zoomFromFov(degrees, m_project.height());
}

void AppController::setCameraAimAtTarget(int trackIndex, int clipIndex, bool enabled)
{
    if (!isValidClipIndex(trackIndex, clipIndex)
        || !drift::isCameraClip(m_project.tracks().at(trackIndex).clips.at(clipIndex))
        || m_project.tracks().at(trackIndex).clips.at(clipIndex).aimAtTarget == enabled) {
        return;
    }
    const drift::Project before = m_project;
    m_project.tracks()[trackIndex].clips[clipIndex].aimAtTarget = enabled;
    pushProjectEdit(before, enabled ? tr("Aim camera at target") : tr("Free camera"));
    finishEdit(enabled ? tr("Camera aims at its point of interest") : tr("Camera uses its own rotation"));
}

QVariantList AppController::clipWorldCentreAt(int trackIndex, int clipIndex, double seconds) const
{
    if (!isValidClipIndex(trackIndex, clipIndex))
        return {};
    const drift::Clip &clip = m_project.tracks().at(trackIndex).clips.at(clipIndex);
    const drift::TimeUs relative = qBound<drift::TimeUs>(0, drift::secondsToUs(seconds) - clip.timelineStart,
                                                          clip.timelineDuration);
    const auto value = [&](const drift::KeyframeTrack<double> &kt, double fallback) {
        return kt.isEmpty() ? fallback : kt.evaluateAt(relative);
    };
    const double w = value(clip.transformW, m_project.width());
    const double h = value(clip.transformH, m_project.height());
    return QVariantList{value(clip.transformX, 0.0) + w / 2.0, value(clip.transformY, 0.0) + h / 2.0,
                        value(clip.positionZ, 0.0)};
}

// Crop rect is given in current-canvas pixels; it may extend outside the canvas
// (negative origin / oversized extent) to grow the frame on that side.
void AppController::applyCanvasCrop(double x, double y, double width, double height)
{
    const int newWidth = qBound(16, qRound(width), 7680);
    const int newHeight = qBound(16, qRound(height), 4320);
    if (newWidth == m_project.width() && newHeight == m_project.height()
        && qFuzzyIsNull(x) && qFuzzyIsNull(y))
        return;

    const drift::Project before = m_project;
    drift::rebaseClipLayout(m_project, m_project.width(), m_project.height(), newWidth, newHeight, x, y);
    m_project.setResolution(newWidth, newHeight);
    pushProjectEdit(before, tr("Crop canvas"));
    finishEdit(tr("Video size cropped to %1×%2").arg(newWidth).arg(newHeight));
}

bool AppController::timelineHasVisualClips() const
{
    for (const drift::Track &track : m_project.tracks()) {
        for (const drift::Clip &clip : track.clips) {
            if (clip.type == drift::ClipType::Video || clip.type == drift::ClipType::Image
                || clip.type == drift::ClipType::Text || clip.type == drift::ClipType::Subtitle
                || clip.type == drift::ClipType::Shape) {
                return true;
            }
        }
    }
    return false;
}

bool AppController::shouldConfigureProjectForAsset(int assetIndex) const
{
    if (m_projectLayoutChosen)
        return false;
    if (!m_assetLibrary)
        return false;
    const QVariantMap asset = m_assetLibrary->assetAt(assetIndex);
    if (asset.isEmpty())
        return false;
    const QString kind = asset.value(QStringLiteral("kind")).toString();
    if (kind != QStringLiteral("video") && kind != QStringLiteral("image"))
        return false;

    // Offer setup only for the first video/image clip (text/shapes alone don't count).
    for (const drift::Track &track : m_project.tracks()) {
        for (const drift::Clip &clip : track.clips) {
            if (clip.type == drift::ClipType::Video || clip.type == drift::ClipType::Image)
                return false;
        }
    }
    return true;
}

void AppController::markProjectLayoutChosen()
{
    setProjectLayoutChosen(true);
}

void AppController::setProjectLayoutChosen(bool chosen)
{
    if (m_projectLayoutChosen == chosen)
        return;
    m_projectLayoutChosen = chosen;
    emit projectLayoutChosenChanged();
}

QVariantMap AppController::suggestedProjectSetupForAsset(int assetIndex) const
{
    QVariantMap out{
        {QStringLiteral("width"), m_project.width()},
        {QStringLiteral("height"), m_project.height()},
        {QStringLiteral("fps"), m_project.fps()},
        {QStringLiteral("aspect"), QStringLiteral("custom")},
    };
    if (!m_assetLibrary)
        return out;

    const QVariantMap asset = m_assetLibrary->assetAt(assetIndex);
    if (asset.isEmpty())
        return out;

    int w = asset.value(QStringLiteral("width")).toInt();
    int h = asset.value(QStringLiteral("height")).toInt();
    const int rotation = asset.value(QStringLiteral("effectiveRotation")).toInt();
    if (rotation == 90 || rotation == 270)
        std::swap(w, h);
    if (w > 0 && h > 0) {
        out.insert(QStringLiteral("width"), w);
        out.insert(QStringLiteral("height"), h);
        const double ratio = static_cast<double>(w) / static_cast<double>(h);
        if (qAbs(ratio - 16.0 / 9.0) < 0.02)
            out.insert(QStringLiteral("aspect"), QStringLiteral("16:9"));
        else if (qAbs(ratio - 9.0 / 16.0) < 0.02)
            out.insert(QStringLiteral("aspect"), QStringLiteral("9:16"));
        else if (qAbs(ratio - 4.0 / 3.0) < 0.02)
            out.insert(QStringLiteral("aspect"), QStringLiteral("4:3"));
        else if (qAbs(ratio - 1.0) < 0.02)
            out.insert(QStringLiteral("aspect"), QStringLiteral("1:1"));
        else
            out.insert(QStringLiteral("aspect"), QStringLiteral("source"));
    }
    const double fps = asset.value(QStringLiteral("fps")).toDouble();
    if (fps >= 1.0)
        out.insert(QStringLiteral("fps"), qRound(fps));
    out.insert(QStringLiteral("name"), asset.value(QStringLiteral("name")).toString());
    out.insert(QStringLiteral("kind"), asset.value(QStringLiteral("kind")).toString());
    return out;
}
