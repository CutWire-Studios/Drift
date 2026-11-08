#include "engine/platform/AndroidUri.h"
#include "app/editor/appcontroller/AppControllerInternal.h"
#include "engine/packages/effects/EffectCatalog.h"
#include "app/mcp/McpController.h"

#include "core/text/TextPresetStore.h"
#include "engine/packages/assets/FontCatalog.h"
// Engine-internal by its own header comment, and included here only for releaseCaches(): the GPU
// caches have no other owner outside src/engine to ask. A one-line forwarder on GpuCompositor
// would restore the boundary.
#include "engine/encode/proxies/ProxyNaming.h"
#include "engine/encode/proxies/ReverseProxyCache.h"
#include "engine/encode/proxies/ReverseRenderer.h"
#ifdef DRIFT_WITH_SKIA
#include "engine/render2d/text/SkiaTextPainter.h"
#endif


#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <QScopedValueRollback>
using namespace drift::appdetail;
using drift::mcpdetail::findClipById;
using drift::mcpdetail::runBeatAnalysis;
using namespace drift::appcontroller_detail;

namespace {

void applyLegacyTextAnimationPatch(drift::TextAnimationSet *set, const QString &which, const QVariantMap &m)
{
    if (m.isEmpty())
        return;
    drift::TextAnimationSlot *slot = which == QLatin1String("animIn") ? &set->in : &set->out;
    QVariantMap current = legacyTextAnimationMap(*slot);
    for (auto it = m.constBegin(); it != m.constEnd(); ++it)
        current.insert(it.key(), *it);
    const QString kind = current.value(QStringLiteral("kind")).toString();
    if (m.contains(QStringLiteral("kind")) || !slot->isActive()) {
        bool isLoop = false;
        drift::TextAnimationSlot rebuilt = drift::legacyTextAnimationSlot(
            kind, drift::secondsToUs(qBound(0.0, current.value(QStringLiteral("duration")).toDouble(), 10.0)),
            current.value(QStringLiteral("ease")).toString(), current.value(QStringLiteral("unit")).toString(),
            drift::secondsToUs(qBound(0.0, current.value(QStringLiteral("stagger")).toDouble(), 2.0)),
            current.value(QStringLiteral("order")).toString(), &isLoop);
        if (isLoop && which == QLatin1String("animIn")) {
            set->loop = rebuilt;
            *slot = drift::TextAnimationSlot{};
        } else {
            *slot = rebuilt;
        }
        return;
    }
    QVariantMap shortcuts;
    for (const char *key : {"duration", "stagger", "unit", "order", "ease"})
        if (m.contains(QLatin1String(key)))
            shortcuts.insert(QLatin1String(key), m.value(QLatin1String(key)));
    applyTextAnimationSlotPatch(slot, shortcuts);
}

// The well-known legacy layer, created with the legacy defaults when the style has none.
drift::TextShadingLayer *ensureLegacyLayer(drift::TextStyle &s, drift::TextLayerKind kind)
{
    if (drift::TextShadingLayer *existing = drift::firstTextLayerOfKind(s.layers, kind, false))
        return existing;
    drift::TextShadingLayer layer;
    switch (kind) {
    case drift::TextLayerKind::Fill:
        layer = drift::solidFillLayer(Qt::white);
        break;
    case drift::TextLayerKind::Stroke:
        layer = drift::strokeLayer(2.0, Qt::black);
        break;
    case drift::TextLayerKind::Shadow:
        layer = drift::shadowLayer(Qt::black, 0.0, 4.0, 8.0, 0.6);
        break;
    case drift::TextLayerKind::Glow:
        layer = drift::glowLayer(Qt::white, 18.0, 0.8);
        break;
    case drift::TextLayerKind::Extrude:
        layer = drift::solidFillLayer(QColor(40, 40, 40), QStringLiteral("extrude"));
        layer.kind = drift::TextLayerKind::Extrude;
        layer.width = 6.0;
        break;
    }
    layer.enabled = false;
    if (drift::findTextLayer(s.layers, layer.id))
        layer.id = drift::mintTextLayerId(s.layers);
    const int index = naturalLayerIndex(s.layers, kind);
    s.layers.insert(index, layer);
    return &s.layers[index];
}

void applyWordAccentPatch(drift::WordAccent *accent, const QVariantMap &m)
{
    if (m.contains(QStringLiteral("rule")))
        accent->rule = drift::wordAccentRuleFromString(m.value(QStringLiteral("rule")).toString());
    if (m.contains(QStringLiteral("n")))
        accent->n = qBound(1, m.value(QStringLiteral("n")).toInt(), 16);
    if (m.contains(QStringLiteral("phase")))
        accent->phase = qBound(0, m.value(QStringLiteral("phase")).toInt(), 16);
    if (m.contains(QStringLiteral("colorEnabled")))
        accent->colorEnabled = m.value(QStringLiteral("colorEnabled")).toBool();
    if (m.contains(QStringLiteral("color")))
        accent->color = QColor(m.value(QStringLiteral("color")).toString());
    if (m.contains(QStringLiteral("sizeScale")))
        accent->sizeScale = qBound(0.25, m.value(QStringLiteral("sizeScale")).toDouble(), 4.0);
    if (m.contains(QStringLiteral("outlineEnabled")))
        accent->outlineEnabled = m.value(QStringLiteral("outlineEnabled")).toBool();
    if (m.contains(QStringLiteral("outlineWidth")))
        accent->outlineWidth = qMax(0.0, m.value(QStringLiteral("outlineWidth")).toDouble());
    if (m.contains(QStringLiteral("outlineColor")))
        accent->outlineColor = QColor(m.value(QStringLiteral("outlineColor")).toString());
    applyTextHighlightPatch(&accent->highlight, m.value(QStringLiteral("highlight")).toMap());
}

QList<drift::SubtitleCue> subtitleCuesFromMap(const QVariantList &list)
{
    QList<drift::SubtitleCue> cues;
    cues.reserve(list.size());
    for (const QVariant &value : list) {
        const QVariantMap map = value.toMap();
        drift::SubtitleCue cue;
        cue.startUs = drift::secondsToUs(map.value(QStringLiteral("start")).toDouble());
        cue.endUs = drift::secondsToUs(map.value(QStringLiteral("end")).toDouble());
        cue.text = map.value(QStringLiteral("text")).toString();
        cue.source = map.value(QStringLiteral("source")).toString();
        cues.append(cue);
    }
    drift::sortSubtitleCues(cues);
    return cues;
}

constexpr drift::TimeUs kDefaultSubtitleCueDurationUs = 3 * drift::kUsPerSecond;
} // namespace

void AppController::beginPreviewDrag(const QString &undoText)
{
    // A press straight after keyboard nudges would otherwise take their result as its "before"
    // and the nudges would lose their undo step.
    if (m_previewDragAuto)
        commitPreviewDrag();
    m_previewDragBefore = m_project;
    m_previewDragActive = true;
    m_previewDragDirty = false;
    m_previewDragText = undoText.isEmpty() ? tr("Edit clip") : undoText;
    emit previewDragActiveChanged();
}

void AppController::beginImplicitPreviewDrag(const QString &undoText)
{
    if (!m_previewDragActive) {
        beginPreviewDrag(undoText);
        m_previewDragAuto = true;
    }
    if (m_previewDragAuto)
        m_previewAutoCommit->start();
}

void AppController::emitPreviewFrame()
{
    // notifyTracksChanged() also notifies selectedClipDataChanged via connection, and the
    // tracksChanged handler already schedules the composite. No setPlayheadUs here: its
    // refreshFrame() raced the handler's and rendered every edit twice.
    notifyTracksChanged();
}

void AppController::emitPreviewEdit(int trackIndex, int clipIndex, const QStringList &keys)
{
    m_previewDragDirty = true;
    // Multicam refreshes its tiles off tracksChanged, and a batch owes one at its end anyway.
    if (m_multicamActive || m_tracksBatchDepth > 0) {
        emitPreviewFrame();
        return;
    }
    // Only the lazily rebuilt caches are dropped; the timeline models, selectedClipData and
    // every inspector catch up once in commitPreviewDrag. Per-move fan-out of tracksChanged was
    // what made these drags lag.
    m_tracksCacheValid = false;
    m_tracksCache.clear();
    m_durationCacheValid = false;
    m_playback.notifyProjectEdited();
    emit clipPropertiesPreviewed(trackIndex, clipIndex, keys);
}

void AppController::previewSetClipPosition(int trackIndex, int clipIndex, double xPixels, double yPixels)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    drift::Clip &clip = track.clips[clipIndex];
    beginImplicitPreviewDrag(tr("Move clip"));
    const drift::TimeUs relative = qMax<drift::TimeUs>(0, m_playheadUs - clip.timelineStart);
    const bool wroteX = writeKeyframeValue(clip.transformX, relative, xPixels, m_autoKeyEnabled, false);
    const bool wroteY = writeKeyframeValue(clip.transformY, relative, yPixels, m_autoKeyEnabled, false);
    if (!wroteX && !wroteY) {
        emit transformBlocked(tr("Turn on Auto keyframes to move this"));
        return;
    }

    emitPreviewEdit(trackIndex, clipIndex, {QStringLiteral("x"), QStringLiteral("y")});
}

void AppController::previewSetClipSize(int trackIndex, int clipIndex, double widthPixels, double heightPixels)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    drift::Clip &clip = track.clips[clipIndex];
    beginImplicitPreviewDrag(tr("Resize clip"));
    const drift::TimeUs relative = qMax<drift::TimeUs>(0, m_playheadUs - clip.timelineStart);
    const bool wroteW =
        writeKeyframeValue(clip.transformW, relative, qMax(1.0, widthPixels), m_autoKeyEnabled, false);
    const bool wroteH =
        writeKeyframeValue(clip.transformH, relative, qMax(1.0, heightPixels), m_autoKeyEnabled, false);
    if (!wroteW && !wroteH) {
        emit transformBlocked(tr("Turn on Auto keyframes to resize this"));
        return;
    }

    emitPreviewEdit(trackIndex, clipIndex, {QStringLiteral("width"), QStringLiteral("height")});
}

void AppController::previewSetClipRect(int trackIndex, int clipIndex, double xPixels, double yPixels,
                                       double widthPixels, double heightPixels)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    drift::Clip &clip = track.clips[clipIndex];
    beginImplicitPreviewDrag(tr("Transform clip"));
    const drift::TimeUs relative = qMax<drift::TimeUs>(0, m_playheadUs - clip.timelineStart);
    bool wrote = false;
    wrote = writeKeyframeValue(clip.transformX, relative, xPixels, m_autoKeyEnabled, false) || wrote;
    wrote = writeKeyframeValue(clip.transformY, relative, yPixels, m_autoKeyEnabled, false) || wrote;
    wrote = writeKeyframeValue(clip.transformW, relative, qMax(1.0, widthPixels), m_autoKeyEnabled, false)
            || wrote;
    wrote = writeKeyframeValue(clip.transformH, relative, qMax(1.0, heightPixels), m_autoKeyEnabled, false)
            || wrote;
    if (!wrote) {
        emit transformBlocked(tr("Turn on Auto keyframes to change this"));
        return;
    }

    emitPreviewEdit(trackIndex, clipIndex,
                    {QStringLiteral("x"), QStringLiteral("y"), QStringLiteral("width"),
                     QStringLiteral("height")});
}

void AppController::previewSetClipRotation(int trackIndex, int clipIndex, double degrees)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    drift::Clip &clip = track.clips[clipIndex];
    beginImplicitPreviewDrag(tr("Rotate clip"));
    const drift::TimeUs relative = qMax<drift::TimeUs>(0, m_playheadUs - clip.timelineStart);
    if (!writeKeyframeValue(clip.rotation, relative, degrees, m_autoKeyEnabled, false)) {
        emit transformBlocked(tr("Turn on Auto keyframes to rotate this"));
        return;
    }

    emitPreviewEdit(trackIndex, clipIndex, {QStringLiteral("rotation")});
}

void AppController::previewSetClipKeyframe(int trackIndex, int clipIndex, const QString &prop,
                                           double atSeconds, double value)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;
    // Announced under the caller's indices: listeners address the clip they are showing, not
    // the adjustment the write lands on.
    const int announceTrack = trackIndex;
    const int announceClip = clipIndex;
    // "fx.<i>.<param>" addresses the effect stack, which now lives on the adjustment
    // linked to this clip. Transform props are untouched by this.
    redirectToKeyframeHost(&trackIndex, &clipIndex, prop);

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    drift::Clip &clip = track.clips[clipIndex];
    beginImplicitPreviewDrag(tr("Edit keyframe"));
    const drift::TimeUs rel = qMax<drift::TimeUs>(0, drift::secondsToUs(atSeconds) - clip.timelineStart);
    if (!writeClipPropValue(clip, prop, rel, value, m_autoKeyEnabled, /*force=*/false)) {
        emit transformBlocked(tr("Turn on Auto keyframes to edit this"));
        return;
    }

    QStringList affected = {prop};
    if (m_positionLinked && (prop == QLatin1String("x") || prop == QLatin1String("y"))) {
        const QString otherProp = (prop == QLatin1String("x")) ? QStringLiteral("y") : QStringLiteral("x");
        drift::KeyframeTrack<double> *otherKt = keyframeTrackForProp(clip, otherProp, false);
        if (otherKt && !otherKt->isEmpty()) {
            const drift::TimeUs otherNearest = otherKt->nearestKeyframe(rel, kKeyframeToleranceUs);
            if (otherNearest < 0 && m_autoKeyEnabled) {
                const double otherVal = otherKt->evaluateAt(rel);
                if (writeClipPropValue(clip, otherProp, rel, otherVal, m_autoKeyEnabled, /*force=*/false))
                    affected.append(otherProp);
            }
        }
    }

    emitPreviewEdit(announceTrack, announceClip, affected);
}

void AppController::previewSetEffectParam(int trackIndex, int clipIndex, int effectIndex,
                                          const QString &key, double value)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;
    const int announceTrack = trackIndex;
    const int announceClip = clipIndex;
    // The stack lives on the adjustment linked to this clip, not on the clip.
    if (!redirectToEffectHost(&trackIndex, &clipIndex, drift::AdjustmentKind::VideoEffects,
                              /*create=*/false)) {
        return;
    }

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    drift::Clip &clip = track.clips[clipIndex];
    if (effectIndex < 0 || effectIndex >= clip.effects.size())
        return;
    if (key.isEmpty())
        return;

    beginImplicitPreviewDrag(tr("Edit effect"));

    const EffectPresetEntry *def = effectDefForId(clip.effects[effectIndex].catalogId);
    bool asBoolean = false;
    if (def) {
        for (const drift::EffectParamSpec &param : def->meta.parameters) {
            if (param.key == key) {
                asBoolean = param.isBoolean();
                break;
            }
        }
    }
    if (asBoolean)
        clip.effects[effectIndex].parameters.insert(key, value > 0.5);
    else
        clip.effects[effectIndex].parameters.insert(key, value);
    emitPreviewEdit(announceTrack, announceClip,
                    {QStringLiteral("fx.%1.%2").arg(effectIndex).arg(key)});
}

void AppController::previewSetEffectEnabled(int trackIndex, int clipIndex, int effectIndex,
                                            bool enabled)
{
    if (!m_previewDragActive || trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;
    const int announceTrack = trackIndex;
    const int announceClip = clipIndex;
    if (!redirectToEffectHost(&trackIndex, &clipIndex, drift::AdjustmentKind::VideoEffects,
                              /*create=*/false)) {
        return;
    }

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;
    drift::Clip &clip = track.clips[clipIndex];
    if (effectIndex < 0 || effectIndex >= clip.effects.size()
        || clip.effects[effectIndex].enabled == enabled) {
        return;
    }

    clip.effects[effectIndex].enabled = enabled;
    emitPreviewEdit(announceTrack, announceClip,
                    {QStringLiteral("fx.%1.enabled").arg(effectIndex)});
}

QColor AppController::imagePixel(const QImage &image, int x, int y) const
{
    if (!image.valid(x, y))
        return Qt::transparent;
    return image.pixelColor(x, y);
}

void AppController::previewSetClipSpeed(int trackIndex, int clipIndex, double speed)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    drift::Clip &clip = track.clips[clipIndex];
    if (clip.type != drift::ClipType::Video && clip.type != drift::ClipType::Audio
        && clip.type != drift::ClipType::Composite)
        return;

    if (!m_previewDragActive)
        beginPreviewDrag(tr("Speed changed"));

    const drift::Project before = m_project;
    clip.speed = qBound(0.25, speed, 4.0);
    clip.syncSrcOutFromSpeed(sourceDurationForClip(clip));
    syncLinkedPartnersFrom(m_project, clip);
    drift::retimeLinkedSubtitles(before, m_project);
    emitPreviewFrame();
}

void AppController::previewSetClipFade(int trackIndex, int clipIndex, double fadeInSeconds, double fadeOutSeconds)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    if (!m_previewDragActive)
        beginPreviewDrag(tr("Adjust fade"));

    drift::Clip &clip = track.clips[clipIndex];
    drift::TimeUs fin = qMax<drift::TimeUs>(0, drift::secondsToUs(fadeInSeconds));
    drift::TimeUs fout = qMax<drift::TimeUs>(0, drift::secondsToUs(fadeOutSeconds));
    fin = qMin(fin, clip.timelineDuration);
    fout = qMin(fout, clip.timelineDuration - fin);
    clip.fadeInUs = fin;
    clip.fadeOutUs = fout;
    if (clip.type != drift::ClipType::Audio && clip.type != drift::ClipType::Subtitle) {
        setEdgeAnimationDuration(clip, true, fin);
        setEdgeAnimationDuration(clip, false, fout);
    }
    syncLinkedPartnersFrom(m_project, clip);
    emitPreviewFrame();
}

void AppController::previewSetClipMask(int trackIndex, int clipIndex, const QVariantMap &maskMap)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    beginImplicitPreviewDrag(tr("Mask changed"));

    writeClipMask(trackIndex, clipIndex, maskFromMap(maskMap));
    emitPreviewEdit(trackIndex, clipIndex, {QStringLiteral("mask.*")});
}

void AppController::commitPreviewDrag()
{
    if (!m_previewDragActive)
        return;

    m_previewAutoCommit->stop();
    m_previewDragAuto = false;
    m_previewDragActive = false;
    emit previewDragActiveChanged();
    // A press that never moved, or a drag whose every write was refused.
    if (!m_previewDragDirty)
        return;
    m_previewDragDirty = false;

    const QString text = m_previewDragText.isEmpty() ? tr("Edit clip") : m_previewDragText;
    // A drag edits the selected clip in place; when the clip under the selection is the one that
    // was there at the press, nothing about the selection needs announcing.
    const auto selectedId = [this](const drift::Project &project) {
        if (m_selectedTrack < 0 || m_selectedTrack >= project.tracks().size())
            return QString();
        const drift::Track &track = project.tracks().at(m_selectedTrack);
        return m_selectedClip >= 0 && m_selectedClip < track.clips.size() ? track.clips.at(m_selectedClip).id
                                                                          : QString();
    };
    const QString before = selectedId(m_previewDragBefore);
    const QScopedValueRollback quietSelection(m_quietSelection, !before.isEmpty() && before == selectedId(m_project));
    // Same reason as pushProjectEdit: a drag that moved or resized a graded clip must record its
    // pinned adjustments on the clip, or a redo restores them at the old span.
    syncLinkedAdjustments(m_project);
    if (!m_mcpController->undoSuspended()) {
        const QScopedValueRollback quietPush(m_quietUndoPush, true);
        m_undoStack.push(new drift::ProjectSnapshotCommand(&m_project, m_previewDragBefore, m_project, text));
    }
    finishEdit(text);
}

void AppController::beginEditGroup()
{
    if (m_editGroupRevision >= 0)
        return;
    m_editGroupRevision = m_mcpController->revision();
    m_mcpController->beginBatch();
}

bool AppController::endEditGroup(const QString &undoText)
{
    if (m_editGroupRevision < 0)
        return false;
    const bool changed = m_mcpController->revision() != m_editGroupRevision;
    m_editGroupRevision = -1;
    m_mcpController->endBatch(undoText.isEmpty() ? tr("Edit value") : undoText, changed);
    return changed;
}

void AppController::cancelPreviewDrag()
{
    if (!m_previewDragActive)
        return;

    m_previewAutoCommit->stop();
    m_previewDragAuto = false;
    m_previewDragDirty = false;
    const auto transcripts = m_project.transcripts();
    m_project = m_previewDragBefore;
    m_project.setTranscripts(transcripts);
    m_previewDragActive = false;
    emit previewDragActiveChanged();
    emitPreviewFrame();
}

void AppController::setClipStart(int trackIndex, int clipIndex, double start)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    const drift::Project before = m_project;
    drift::Clip &clip = track.clips[clipIndex];
    const drift::TimeUs oldStart = clip.timelineStart;
    clip.timelineStart = drift::resolveClipStart(m_project, track, clipIndex, drift::secondsToUs(start),
                                                 clip.timelineDuration, m_snapEnabled, m_playheadUs,
                                                 extraSnapTargets());
    applyRippleShift(track, clipIndex, clip.timelineStart - oldStart);
    pushProjectEdit(before, tr("Start updated"));
    finishEdit(tr("Start updated"));
}

void AppController::setClipDuration(int trackIndex, int clipIndex, double duration)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    const drift::Project before = m_project;
    drift::Clip &clip = track.clips[clipIndex];
    const drift::TimeUs maxSource = sourceDurationForClip(clip);
    const bool syntheticVisual = isSyntheticTimelineClip(clip.type);
    const drift::TimeUs maxSourceSpan =
        syntheticVisual ? syntheticClipMaxDurationUs()
                        : (clip.reverse ? clip.srcOut : (maxSource > clip.srcIn ? maxSource - clip.srcIn : 0));
    const drift::TimeUs maxDuration =
        syntheticVisual
            ? maxSourceSpan
            : (clip.effectiveSpeed() > 0.0
                   ? static_cast<drift::TimeUs>(
                         llround(static_cast<double>(maxSourceSpan) / clip.effectiveSpeed()))
                   : maxSourceSpan);
    clip.timelineDuration = qBound(drift::minClipDurationUs(m_project.fps()), drift::secondsToUs(duration), maxDuration);
    if (syntheticVisual) {
        syncSyntheticSourceRange(clip);
    } else {
        const drift::TimeUs span = clip.sourceSpanUs();
        if (clip.reverse)
            clip.srcIn = qMax<drift::TimeUs>(0, clip.srcOut - span);
        else
            clip.srcOut = qMin(clip.srcIn + span, maxSource);
    }
    if (editCreatesCompleteOverlap(before, m_project)) {
        m_project = before;
        setLastMessage(tr("A clip can't completely cover another"), QStringLiteral("warning"));
        return;
    }
    pushProjectEdit(before, tr("Duration updated"));
    finishEdit(tr("Duration updated"));
}

void AppController::setClipTextContent(int trackIndex, int clipIndex, const QString &text)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    drift::Clip &clip = track.clips[clipIndex];
    if (clip.type != drift::ClipType::Text)
        return;
    const QString trimmed = text.trimmed();
    if (clip.textContent == trimmed && clip.name == trimmed.left(32))
        return;

    const drift::Project before = m_project;
    clip.textContent = trimmed;
    clip.name = clip.textContent.left(32);
    pushProjectEdit(before, tr("Text updated"));
    finishEdit(tr("Text updated"));
}

void AppController::setClipName(int trackIndex, int clipIndex, const QString &name)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty())
        return;

    drift::Clip &clip = track.clips[clipIndex];
    if (clip.name == trimmed)
        return;

    // Clip nests Qt-implicitly-shared maps (keyframes, effects). A plain Project
    // copy can still alias those payloads across undo snapshots; detach first.
    const drift::Project before = m_project.detachedCopy();
    clip.name = trimmed;
    // A composite is named after its sequence everywhere else (the bin, the timeline switcher),
    // so renaming its clip renames the composite itself.
    bool renamedComposite = false;
    if (clip.type == drift::ClipType::Composite && m_assetLibrary) {
        const int assetIndex = m_assetLibrary->indexOfId(clip.assetId);
        renamedComposite = assetIndex >= 0 && m_assetLibrary->setAssetName(assetIndex, trimmed);
    }
    pushProjectEdit(before, tr("Rename clip"));
    finishEdit(tr("Clip renamed"));
    if (renamedComposite)
        emit sequenceTabsChanged();
}

void AppController::commitTextEdit(int trackIndex, int clipIndex, const QString &text)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    drift::Clip &clip = track.clips[clipIndex];
    if (clip.type != drift::ClipType::Text)
        return;

    const QString trimmed = text.trimmed();
    if (m_previewDragActive) {
        clip.textContent = trimmed;
        clip.name = trimmed.left(32);
        m_previewDragDirty = true;
        commitPreviewDrag();
        return;
    }

    if (clip.textContent == trimmed && clip.name == trimmed.left(32))
        return;

    const drift::Project before = m_project;
    clip.textContent = trimmed;
    clip.name = trimmed.left(32);
    pushProjectEdit(before, tr("Text updated"));
    finishEdit(tr("Text updated"));
}

drift::Clip *AppController::inlineEditClip()
{
    return const_cast<drift::Clip *>(std::as_const(*this).inlineEditClip());
}

const drift::Clip *AppController::inlineEditClip() const
{
    if (!m_inlineEdit.active)
        return nullptr;
    const auto &tracks = m_project.tracks();
    if (m_inlineEdit.track >= 0 && m_inlineEdit.track < tracks.size()) {
        const drift::Track &track = tracks.at(m_inlineEdit.track);
        if (m_inlineEdit.clip >= 0 && m_inlineEdit.clip < track.clips.size()
            && track.clips.at(m_inlineEdit.clip).id == m_inlineEdit.clipId)
            return &track.clips.at(m_inlineEdit.clip);
    }
    return nullptr;
}

int AppController::inlineRenderedPos(int modelPos) const
{
    return modelPos >= m_inlineEdit.cursor ? modelPos + int(m_inlineEdit.preedit.size()) : modelPos;
}

int AppController::inlineModelPos(int renderedPos) const
{
    const int at = m_inlineEdit.cursor;
    const int length = int(m_inlineEdit.preedit.size());
    if (renderedPos <= at)
        return renderedPos;
    return renderedPos >= at + length ? renderedPos - length : at;
}

bool AppController::inlineEditLayout(FrameCompositor::TextEditGeometry *geometry, QTransform *toCanvas,
                                     double *scale) const
{
    const drift::Clip *clip = inlineEditClip();
    if (!clip)
        return false;
    // The preview's scale, so the layout is the one already cached for the render.
    *scale = m_playback.previewRenderScale();
    *geometry = FrameCompositor::textEditGeometry(*clip, clip->textContent, m_playheadUs, m_project.width(),
                                                  m_project.height(), *scale);
    if (!geometry->set)
        return false;
    // Block-local render px -> canvas project px: into the layout rect, turned about its centre
    // the way the layer is, then back from render scale.
    const QPointF centre = geometry->layoutRect.center();
    QTransform transform;
    transform.scale(1.0 / *scale, 1.0 / *scale);
    transform.translate(centre.x(), centre.y());
    transform.rotate(geometry->rotation);
    transform.translate(-centre.x(), -centre.y());
    transform.translate(geometry->layoutRect.x(), geometry->layoutRect.y());
    *toCanvas = transform;
    return true;
}

void AppController::beginInlineTextEdit(int trackIndex, int clipIndex)
{
    drift::Clip *clip = textClipAt(trackIndex, clipIndex);
    if (!clip || clip->type != drift::ClipType::Text)
        return;
    if (m_inlineEdit.active)
        endInlineTextEdit(true);

    if (!m_previewDragActive)
        beginPreviewDrag(tr("Edit text"));

    m_inlineEdit = InlineTextEdit{};
    m_inlineEdit.active = true;
    m_inlineEdit.track = trackIndex;
    m_inlineEdit.clip = clipIndex;
    m_inlineEdit.clipId = clip->id;
    m_inlineEdit.original = clip->textContent;
    m_inlineEdit.text = clip->textContent;
    m_inlineEdit.cursor = int(clip->textContent.size());
    if (!m_inlineTextEditing) {
        m_inlineTextEditing = true;
        emit inlineTextEditingChanged();
    }

    FrameCompositor::RenderOptions::TextEditDecor decor;
    decor.clipId = clip->id;
    decor.cursor = m_inlineEdit.cursor;
    m_playback.setTextEditDecor(decor);
    emit inlineEditGeometryChanged();
}

void AppController::updateInlineTextEdit(int trackIndex, int clipIndex, const QString &text, int cursor,
                                         int selStart, int selEnd, const QString &preeditText)
{
    drift::Clip *clip = inlineEditClip();
    if (!clip || trackIndex != m_inlineEdit.track || clipIndex != m_inlineEdit.clip)
        return;

    const int size = int(text.size());
    cursor = qBound(0, cursor, size);
    const bool selected = selStart >= 0 && selEnd >= 0 && selStart != selEnd;
    const int low = selected ? qBound(0, qMin(selStart, selEnd), size) : -1;
    const int high = selected ? qBound(0, qMax(selStart, selEnd), size) : -1;
    selStart = low;
    selEnd = high;
    m_inlineEdit.text = text;
    m_inlineEdit.cursor = cursor;
    m_inlineEdit.selStart = selStart;
    m_inlineEdit.selEnd = selEnd;
    m_inlineEdit.preedit = preeditText;

    // The preedit goes into the rendered text only; the model text stays the editor's.
    const QString rendered = text.left(cursor) + preeditText + text.mid(cursor);
    const bool textChanged = clip->textContent != rendered;
    if (textChanged) {
        clip->textContent = rendered;
        clip->name = rendered.left(32);
    }

    FrameCompositor::RenderOptions::TextEditDecor decor;
    decor.clipId = clip->id;
    decor.cursor = inlineRenderedPos(cursor);
    if (selected) {
        decor.selStart = inlineRenderedPos(selStart);
        decor.selEnd = inlineRenderedPos(selEnd);
    }
    if (!preeditText.isEmpty()) {
        decor.preeditStart = cursor;
        decor.preeditLength = int(preeditText.size());
    }
    // One render: a text change goes out with the project edit, a caret move on its own.
    m_playback.setTextEditDecor(decor, !textChanged);
    if (textChanged)
        emitPreviewFrame();
    emit inlineEditGeometryChanged();
}

void AppController::endInlineTextEdit(bool commit)
{
    if (!m_inlineEdit.active)
        return;

    drift::Clip *clip = inlineEditClip();
    const int track = m_inlineEdit.track;
    const int clipIndex = m_inlineEdit.clip;
    const QString text = m_inlineEdit.text;
    const QString original = m_inlineEdit.original;
    m_inlineEdit = InlineTextEdit{};

    m_playback.setTextEditDecor({}, false);
    if (m_inlineTextEditing) {
        m_inlineTextEditing = false;
        emit inlineTextEditingChanged();
    }

    if (clip && commit && text != original) {
        commitTextEdit(track, clipIndex, text);
    } else if (m_previewDragActive) {
        // Nothing changed or the edit was abandoned: the snapshot taken at the start is the
        // original, and no undo step is pushed.
        cancelPreviewDrag();
    } else if (clip) {
        clip->textContent = original;
        clip->name = original.left(32);
        emitPreviewFrame();
    }
    emit inlineEditGeometryChanged();
}

QVariantList AppController::inlineCaretQuad() const
{
    FrameCompositor::TextEditGeometry geometry;
    QTransform toCanvas;
    double scale = 1.0;
    if (!inlineEditLayout(&geometry, &toCanvas, &scale))
        return {};
    const QRectF caret = drift::text::caretRect(*geometry.set, inlineRenderedPos(m_inlineEdit.cursor));
    if (caret.height() <= 0.0)
        return {};
    // The bar the painter draws: 2 project px or 0.06 em, whichever is wider.
    const double width = qMax(2.0 * scale, 0.06 * geometry.set->paragraph->emPx);
    const QRectF bar(caret.x() - width * 0.5, caret.y(), width, caret.height());
    QVariantList quad;
    for (const QPointF &corner : {bar.topLeft(), bar.topRight(), bar.bottomRight(), bar.bottomLeft()})
        quad.append(toCanvas.map(corner));
    return quad;
}

QVariantList AppController::inlineSelectionQuads() const
{
    FrameCompositor::TextEditGeometry geometry;
    QTransform toCanvas;
    double scale = 1.0;
    if (m_inlineEdit.selStart < 0 || !inlineEditLayout(&geometry, &toCanvas, &scale))
        return {};
    QVariantList quads;
    for (const QRectF &rect : drift::text::selectionRects(*geometry.set, inlineRenderedPos(m_inlineEdit.selStart),
                                                          inlineRenderedPos(m_inlineEdit.selEnd))) {
        QVariantList quad;
        for (const QPointF &corner : {rect.topLeft(), rect.topRight(), rect.bottomRight(), rect.bottomLeft()})
            quad.append(toCanvas.map(corner));
        quads.append(QVariant(quad));
    }
    return quads;
}

int AppController::inlineHitTest(const QPointF &canvasPoint) const
{
    FrameCompositor::TextEditGeometry geometry;
    QTransform toCanvas;
    double scale = 1.0;
    if (!inlineEditLayout(&geometry, &toCanvas, &scale))
        return m_inlineEdit.cursor;
    bool invertible = false;
    const QTransform toLocal = toCanvas.inverted(&invertible);
    if (!invertible)
        return m_inlineEdit.cursor;
    return inlineModelPos(drift::text::hitTest(*geometry.set, toLocal.map(canvasPoint)));
}

QVariantMap AppController::inlineWordAt(int pos) const
{
    FrameCompositor::TextEditGeometry geometry;
    QTransform toCanvas;
    double scale = 1.0;
    QVariantMap out{{QStringLiteral("start"), pos}, {QStringLiteral("end"), pos}};
    if (!inlineEditLayout(&geometry, &toCanvas, &scale))
        return out;
    const std::pair<int, int> word = drift::text::wordAt(*geometry.set, inlineRenderedPos(pos));
    out.insert(QStringLiteral("start"), inlineModelPos(word.first));
    out.insert(QStringLiteral("end"), inlineModelPos(word.second));
    return out;
}

int AppController::inlineMoveCaretLines(int pos, int deltaLines)
{
    FrameCompositor::TextEditGeometry geometry;
    QTransform toCanvas;
    double scale = 1.0;
    if (!inlineEditLayout(&geometry, &toCanvas, &scale))
        return pos;
    const int rendered = inlineRenderedPos(pos);
    // Consecutive moves keep the column they started from, as a text field does.
    if (pos != m_inlineEdit.preferredFor)
        m_inlineEdit.preferredX = drift::text::caretRect(*geometry.set, rendered).x();
    const int moved = inlineModelPos(drift::text::lineMoveCaret(*geometry.set, rendered, deltaLines,
                                                                m_inlineEdit.preferredX));
    m_inlineEdit.preferredFor = moved;
    return moved;
}

int AppController::inlineLineEdge(int pos, bool toEnd) const
{
    FrameCompositor::TextEditGeometry geometry;
    QTransform toCanvas;
    double scale = 1.0;
    if (!inlineEditLayout(&geometry, &toCanvas, &scale))
        return pos;
    return inlineModelPos(drift::text::lineEdge(*geometry.set, inlineRenderedPos(pos), toEnd));
}

void AppController::setSubtitleCues(int trackIndex, int clipIndex, const QVariantList &cues)
{
    commitSubtitleCues(trackIndex, clipIndex, subtitleCuesFromMap(cues), tr("Subtitles updated"));
}

void AppController::commitSubtitleCues(int trackIndex, int clipIndex, QList<drift::SubtitleCue> cues,
                                       const QString &undoText)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    drift::Clip &clip = track.clips[clipIndex];
    if (clip.type != drift::ClipType::Subtitle)
        return;

    const drift::Project before = m_project;
    clip.subtitleCues = std::move(cues);
    drift::refreshSubtitleClipName(clip.name, clip.subtitleCues);
    pushProjectEdit(before, undoText);
    finishEdit(undoText);
}

void AppController::previewSetSubtitleCues(int trackIndex, int clipIndex, const QVariantList &cues)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    drift::Clip &clip = track.clips[clipIndex];
    if (clip.type != drift::ClipType::Subtitle)
        return;

    if (!m_previewDragActive)
        beginPreviewDrag(tr("Adjust subtitle timing"));

    clip.subtitleCues = subtitleCuesFromMap(cues);
    drift::refreshSubtitleClipName(clip.name, clip.subtitleCues);
    emitPreviewFrame();
}

double AppController::subtitleLocalPlayheadSeconds(int trackIndex, int clipIndex) const
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return -1.0;

    const drift::Track &track = m_project.tracks().at(trackIndex);
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return -1.0;

    const drift::Clip &clip = track.clips.at(clipIndex);
    if (clip.type != drift::ClipType::Subtitle || !clip.containsTime(m_playheadUs))
        return -1.0;

    return drift::usToSeconds(m_playheadUs - clip.timelineStart);
}

void AppController::upsertSubtitleCueAtPlayhead(int trackIndex, int clipIndex, const QString &text)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    drift::Clip &clip = track.clips[clipIndex];
    if (clip.type != drift::ClipType::Subtitle)
        return;

    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty())
        return;

    const drift::TimeUs localUs =
        qBound(drift::TimeUs{0}, m_playheadUs - clip.timelineStart, clip.timelineDuration);
    const int existingIndex = drift::subtitleCueIndexAt(clip.subtitleCues, localUs);

    const drift::Project before = m_project;
    if (existingIndex >= 0) {
        clip.subtitleCues[existingIndex].text = trimmed;
    } else {
        drift::SubtitleCue cue;
        cue.startUs = localUs;

        drift::TimeUs nextStart = clip.timelineDuration;
        for (const drift::SubtitleCue &existing : clip.subtitleCues) {
            if (existing.startUs > localUs)
                nextStart = qMin(nextStart, existing.startUs);
        }
        cue.endUs = qMin(clip.timelineDuration, qMax(localUs + kDefaultSubtitleCueDurationUs, localUs + 1));
        cue.endUs = qMin(cue.endUs, nextStart);
        if (cue.endUs <= cue.startUs)
            cue.endUs = qMin(clip.timelineDuration, cue.startUs + 1);
        cue.text = trimmed;
        clip.subtitleCues.append(cue);
        drift::sortSubtitleCues(clip.subtitleCues);
    }

    drift::refreshSubtitleClipName(clip.name, clip.subtitleCues);
    pushProjectEdit(before, tr("Subtitle cue updated"));
    finishEdit(tr("Subtitle cue updated"));
}

void AppController::seekToSubtitleCue(int trackIndex, int clipIndex, int cueIndex)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    const drift::Track &track = m_project.tracks().at(trackIndex);
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    const drift::Clip &clip = track.clips.at(clipIndex);
    if (clip.type != drift::ClipType::Subtitle || cueIndex < 0 || cueIndex >= clip.subtitleCues.size())
        return;

    setPlayheadSeconds(drift::usToSeconds(clip.timelineStart + clip.subtitleCues.at(cueIndex).startUs));
}

void AppController::setTextStyle(int trackIndex, int clipIndex, const QVariantMap &m)
{
    drift::Clip *clip = textClipAt(trackIndex, clipIndex);
    if (!clip)
        return;

    const drift::Project before = m_project;
    drift::TextStyle &s = clip->textStyle;
    applyTextStylePatch(s, m);
    pushProjectEdit(before, tr("Edit text style"));
    finishEdit(tr("Text style updated"));
}

// The partial patch both the inspector and set_text send: the canonical keys (layers, layer,
// animation, lookId) and, for one release, the flat v6 keys routed onto the well-known layers.
void AppController::applyTextStylePatch(drift::TextStyle &s, const QVariantMap &m)
{
    if (m.contains(QStringLiteral("fontFamily")))
        s.fontFamily = m.value(QStringLiteral("fontFamily")).toString();
    if (m.contains(QStringLiteral("pixelSize")))
        s.pixelSize = qBound(8, m.value(QStringLiteral("pixelSize")).toInt(), 800);
    if (m.contains(QStringLiteral("fontWeight")))
        s.fontWeight = qBound(100, m.value(QStringLiteral("fontWeight")).toInt(), 900);
    if (m.contains(QStringLiteral("italic")))
        s.italic = m.value(QStringLiteral("italic")).toBool();
    if (m.contains(QStringLiteral("pathBend")))
        s.pathBend = qBound(-100.0, m.value(QStringLiteral("pathBend")).toDouble(), 100.0);
    if (m.contains(QStringLiteral("align")))
        s.align = drift::textAlignFromString(m.value(QStringLiteral("align")).toString());
    if (m.contains(QStringLiteral("valign")))
        s.valign = drift::textVAlignFromString(m.value(QStringLiteral("valign")).toString());
    if (m.contains(QStringLiteral("wordWrap")))
        s.wordWrap = m.value(QStringLiteral("wordWrap")).toBool();
    if (m.contains(QStringLiteral("lineHeight")))
        s.lineHeight = qBound(0.5, m.value(QStringLiteral("lineHeight")).toDouble(), 4.0);
    if (m.contains(QStringLiteral("letterSpacing")))
        s.letterSpacing = m.value(QStringLiteral("letterSpacing")).toDouble();
    if (m.contains(QStringLiteral("textCase")))
        s.textCase = drift::textCaseFromString(m.value(QStringLiteral("textCase")).toString());
    if (m.contains(QStringLiteral("boxEnabled")))
        s.boxEnabled = m.value(QStringLiteral("boxEnabled")).toBool();
    if (m.contains(QStringLiteral("boxColor")))
        s.boxColor = QColor(m.value(QStringLiteral("boxColor")).toString());
    if (m.contains(QStringLiteral("boxPadding")))
        s.boxPadding = qMax(0.0, m.value(QStringLiteral("boxPadding")).toDouble());
    if (m.contains(QStringLiteral("boxRadius")))
        s.boxRadius = qMax(0.0, m.value(QStringLiteral("boxRadius")).toDouble());
    if (m.contains(QStringLiteral("underlineEnabled")))
        s.underlineEnabled = m.value(QStringLiteral("underlineEnabled")).toBool();
    if (m.contains(QStringLiteral("underlineColor")))
        s.underlineColor = QColor(m.value(QStringLiteral("underlineColor")).toString());
    if (m.contains(QStringLiteral("underlineWidth")))
        s.underlineWidth = qMax(0.0, m.value(QStringLiteral("underlineWidth")).toDouble());
    if (m.contains(QStringLiteral("underlineOffset")))
        s.underlineOffset = m.value(QStringLiteral("underlineOffset")).toDouble();
    if (m.contains(QStringLiteral("strikethroughEnabled")))
        s.strikethroughEnabled = m.value(QStringLiteral("strikethroughEnabled")).toBool();
    applyTextHighlightPatch(&s.wordHighlight, m.value(QStringLiteral("wordHighlight")).toMap());
    applyWordAccentPatch(&s.accent, m.value(QStringLiteral("accent")).toMap());

    // The look: canonical layers first, then the flat v6 keys onto their well-known layers.
    bool lookEdited = applyLayerStackPatch(s.layers, m, drift::solidFillLayer(Qt::white));
    if (m.contains(QStringLiteral("color"))) {
        s.setPrimaryColor(QColor(m.value(QStringLiteral("color")).toString()));
        lookEdited = true;
    }
    if (m.contains(QStringLiteral("fillKind")) || m.contains(QStringLiteral("colorSecondary"))
        || m.contains(QStringLiteral("gradientAngle"))) {
        drift::TextShadingLayer *fill = ensureLegacyLayer(s, drift::TextLayerKind::Fill);
        fill->enabled = true;
        const QString kind = m.value(QStringLiteral("fillKind")).toString();
        if (kind == QLatin1String("solid")) {
            fill->paint.color = s.primaryColor();
            fill->paint.kind = drift::TextPaintKind::Solid;
        } else if (kind == QLatin1String("linearGradient") || kind == QLatin1String("radialGradient")
                   || (kind.isEmpty() && fill->paint.kind == drift::TextPaintKind::Gradient)) {
            const QColor primary = s.primaryColor();
            if (fill->paint.kind != drift::TextPaintKind::Gradient) {
                fill->paint.kind = drift::TextPaintKind::Gradient;
                fill->paint.gradient.stops = {{0.0, primary}, {1.0, QColor(255, 120, 0)}};
            }
            if (!kind.isEmpty())
                fill->paint.gradient.kind = kind == QLatin1String("radialGradient") ? drift::TextGradientKind::Radial
                                                                                    : drift::TextGradientKind::Linear;
            if (m.contains(QStringLiteral("colorSecondary")) && fill->paint.gradient.stops.size() >= 2)
                fill->paint.gradient.stops.last().color = QColor(m.value(QStringLiteral("colorSecondary")).toString());
            if (m.contains(QStringLiteral("gradientAngle")))
                fill->paint.gradient.angle = m.value(QStringLiteral("gradientAngle")).toDouble();
        }
        lookEdited = true;
    }
    if (m.contains(QStringLiteral("outlineEnabled")) || m.contains(QStringLiteral("outlineWidth"))
        || m.contains(QStringLiteral("outlineColor"))) {
        drift::TextShadingLayer *stroke = ensureLegacyLayer(s, drift::TextLayerKind::Stroke);
        if (m.contains(QStringLiteral("outlineEnabled")))
            stroke->enabled = m.value(QStringLiteral("outlineEnabled")).toBool();
        if (m.contains(QStringLiteral("outlineWidth")))
            stroke->width = qMax(0.0, m.value(QStringLiteral("outlineWidth")).toDouble());
        if (m.contains(QStringLiteral("outlineColor")))
            stroke->paint.color = QColor(m.value(QStringLiteral("outlineColor")).toString());
        lookEdited = true;
    }
    if (m.contains(QStringLiteral("shadowEnabled")) || m.contains(QStringLiteral("shadowOffsetX"))
        || m.contains(QStringLiteral("shadowOffsetY")) || m.contains(QStringLiteral("shadowBlur"))
        || m.contains(QStringLiteral("shadowOpacity")) || m.contains(QStringLiteral("shadowColor"))) {
        drift::TextShadingLayer *shadow = ensureLegacyLayer(s, drift::TextLayerKind::Shadow);
        if (m.contains(QStringLiteral("shadowEnabled")))
            shadow->enabled = m.value(QStringLiteral("shadowEnabled")).toBool();
        if (m.contains(QStringLiteral("shadowOffsetX")))
            shadow->offsetX = m.value(QStringLiteral("shadowOffsetX")).toDouble();
        if (m.contains(QStringLiteral("shadowOffsetY")))
            shadow->offsetY = m.value(QStringLiteral("shadowOffsetY")).toDouble();
        if (m.contains(QStringLiteral("shadowBlur")))
            shadow->blur = qMax(0.0, m.value(QStringLiteral("shadowBlur")).toDouble());
        if (m.contains(QStringLiteral("shadowOpacity")))
            shadow->opacity = qBound(0.0, m.value(QStringLiteral("shadowOpacity")).toDouble(), 1.0);
        if (m.contains(QStringLiteral("shadowColor")))
            shadow->paint.color = QColor(m.value(QStringLiteral("shadowColor")).toString());
        lookEdited = true;
    }
    if (m.contains(QStringLiteral("glowEnabled")) || m.contains(QStringLiteral("glowColor"))
        || m.contains(QStringLiteral("glowRadius")) || m.contains(QStringLiteral("glowOpacity"))) {
        drift::TextShadingLayer *glow = ensureLegacyLayer(s, drift::TextLayerKind::Glow);
        if (m.contains(QStringLiteral("glowEnabled")))
            glow->enabled = m.value(QStringLiteral("glowEnabled")).toBool();
        if (m.contains(QStringLiteral("glowColor")))
            glow->paint.color = QColor(m.value(QStringLiteral("glowColor")).toString());
        if (m.contains(QStringLiteral("glowRadius")))
            glow->blur = qMax(0.0, m.value(QStringLiteral("glowRadius")).toDouble());
        if (m.contains(QStringLiteral("glowOpacity")))
            glow->opacity = qBound(0.0, m.value(QStringLiteral("glowOpacity")).toDouble(), 1.0);
        lookEdited = true;
    }
    if (m.contains(QStringLiteral("lookId")) && m.value(QStringLiteral("lookId")).toString().isEmpty()) {
        s.lookId.clear();
        s.lookParams.clear();
    } else if (lookEdited) {
        s.lookId.clear();
        s.lookParams.clear();
    }
    // Layer keyframes whose layer is gone.
    for (auto it = s.keyframes.begin(); it != s.keyframes.end();) {
        if (drift::textKeyframeCanonicalKey(it.key(), s).isEmpty())
            it = s.keyframes.erase(it);
        else
            ++it;
    }

    if (m.contains(QStringLiteral("animation")))
        applyTextAnimationSetPatch(&s.animation, m.value(QStringLiteral("animation")).toMap());
    applyLegacyTextAnimationPatch(&s.animation, QStringLiteral("animIn"), m.value(QStringLiteral("animIn")).toMap());
    applyLegacyTextAnimationPatch(&s.animation, QStringLiteral("animOut"), m.value(QStringLiteral("animOut")).toMap());

    // A hand-edited style is no longer the pack it came from, so the picker stops showing one as
    // selected. Alignment and wrapping are layout, not look, and leave the pack intact.
    static const QSet<QString> kLayoutOnlyKeys = {QStringLiteral("align"), QStringLiteral("valign"),
                                                  QStringLiteral("wordWrap")};
    for (auto it = m.constBegin(); it != m.constEnd(); ++it) {
        if (!kLayoutOnlyKeys.contains(it.key())) {
            s.packId.clear();
            break;
        }
    }
}

void AppController::applyTextPreset(int trackIndex, int clipIndex, const QString &presetId)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    drift::Clip &clip = track.clips[clipIndex];
    if (clip.type != drift::ClipType::Text && clip.type != drift::ClipType::Subtitle)
        return;

    const std::optional<drift::TextStyle> preset = drift::textStyleForPresetId(presetId);
    if (!preset)
        return;

    const drift::Project before = m_project;
    clip.textStyle = *preset;
    clip.textStyle.packId = presetId;
    pushProjectEdit(before, tr("Apply text preset"));
    finishEdit(tr("Text preset applied"));
}

QVariantList AppController::textPresets() const
{
    QVariantList out;
    for (const drift::TextPreset &preset : drift::textPresets()) {
        out.append(QVariantMap{
            {QStringLiteral("id"), preset.id},
            {QStringLiteral("label"), preset.label},
            {QStringLiteral("sampleText"), preset.sampleText},
            {QStringLiteral("style"), textStyleToMap(preset.style, 0)},
        });
    }
    return out;
}

QVariantList AppController::userTextPresets() const
{
    QVariantList out;
    for (const drift::TextPreset &preset : drift::TextPresetStore::instance().presets()) {
        out.append(QVariantMap{
            {QStringLiteral("id"), preset.id},
            {QStringLiteral("label"), preset.label},
            {QStringLiteral("style"), textStyleToMap(preset.style, 0)},
        });
    }
    return out;
}

QString AppController::saveTextStyleAsPreset(int trackIndex, int clipIndex, const QString &label)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return {};

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return {};

    drift::Clip &clip = track.clips[clipIndex];
    if (clip.type != drift::ClipType::Text && clip.type != drift::ClipType::Subtitle)
        return {};

    // The card's thumbnail draws this, so the user's own words make the preset recognisable at a
    // glance. Long lines would render as a wall of tiny glyphs, hence the clamp.
    QString sample = clip.textContent.simplified();
    const QStringList words = sample.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (words.size() > 4)
        sample = words.mid(0, 4).join(QLatin1Char(' '));

    const QString presetId =
        drift::TextPresetStore::instance().add(label, clip.textStyle, sample);
    if (presetId.isEmpty()) {
        setLastMessage(tr("Could not save the text style"), QStringLiteral("error"));
        return {};
    }
    emit userTextPresetsChanged();

    // Point the clip at the style it just minted, so the inspector's picker shows the new name
    // instead of continuing to read "Custom".
    const drift::Project before = m_project;
    clip.textStyle.packId = presetId;
    pushProjectEdit(before, tr("Save text style"));
    finishEdit(tr("Text style saved"));
    return presetId;
}

bool AppController::renameUserTextPreset(const QString &presetId, const QString &label)
{
    if (!drift::TextPresetStore::instance().rename(presetId, label)) {
        setLastMessage(tr("Could not rename the text style"), QStringLiteral("error"));
        return false;
    }
    emit userTextPresetsChanged();
    setLastMessage(tr("Text style renamed"), QStringLiteral("success"));
    return true;
}

bool AppController::deleteUserTextPreset(const QString &presetId)
{
    // Clips keep their style; only the library entry goes, so a dangling packId just reads as
    // "Custom" in the picker.
    if (!drift::TextPresetStore::instance().remove(presetId)) {
        setLastMessage(tr("Could not delete the text style"), QStringLiteral("error"));
        return false;
    }
    emit userTextPresetsChanged();
    setLastMessage(tr("Text style deleted"), QStringLiteral("success"));
    return true;
}

bool AppController::exportUserTextPreset(const QString &presetId, const QUrl &fileUrl)
{
#ifdef Q_OS_ANDROID
    if (AndroidUri::isContentUri(fileUrl)) {
        const QString tmp = QDir::temp().filePath(QStringLiteral("drift-text-preset.json"));
        if (!drift::TextPresetStore::instance().exportToFile(presetId, tmp)) {
            setLastMessage(tr("Could not export the text style"), QStringLiteral("error"));
            return false;
        }
        QFile src(tmp);
        std::unique_ptr<QFile> sink = AndroidUri::openForWrite(fileUrl);
        if (!sink || !src.open(QIODevice::ReadOnly) || sink->write(src.readAll()) < 0) {
            setLastMessage(tr("Could not export the text style"), QStringLiteral("error"));
            return false;
        }
        QFile::remove(tmp);
        setLastMessage(tr("Text style exported"), QStringLiteral("success"));
        return true;
    }
#endif
    const QString path = fileUrl.isLocalFile() ? fileUrl.toLocalFile() : fileUrl.toString();
    if (path.isEmpty())
        return false;
    if (!drift::TextPresetStore::instance().exportToFile(presetId, path)) {
        setLastMessage(tr("Could not export the text style"), QStringLiteral("error"));
        return false;
    }
    setLastMessage(tr("Text style exported"), QStringLiteral("success"));
    return true;
}

bool AppController::importUserTextPreset(const QUrl &fileUrl)
{
#ifdef Q_OS_ANDROID
    if (AndroidUri::isContentUri(fileUrl)) {
        std::unique_ptr<QFile> src = AndroidUri::openForRead(fileUrl);
        if (!src) {
            setLastMessage(tr("Could not import the text style"), QStringLiteral("error"));
            return false;
        }
        const QString tmp = QDir::temp().filePath(QStringLiteral("drift-text-preset-import.json"));
        QFile dst(tmp);
        if (!dst.open(QIODevice::WriteOnly) || dst.write(src->readAll()) < 0) {
            setLastMessage(tr("Could not import the text style"), QStringLiteral("error"));
            return false;
        }
        dst.close();
        const QString id = drift::TextPresetStore::instance().importFromFile(tmp);
        QFile::remove(tmp);
        if (id.isEmpty()) {
            setLastMessage(tr("Could not import the text style"), QStringLiteral("error"));
            return false;
        }
        emit userTextPresetsChanged();
        setLastMessage(tr("Text style imported"), QStringLiteral("success"));
        return true;
    }
#endif
    const QString path = fileUrl.isLocalFile() ? fileUrl.toLocalFile() : fileUrl.toString();
    if (path.isEmpty())
        return false;
    if (drift::TextPresetStore::instance().importFromFile(path).isEmpty()) {
        setLastMessage(tr("Could not import the text style"), QStringLiteral("error"));
        return false;
    }
    emit userTextPresetsChanged();
    setLastMessage(tr("Text style imported"), QStringLiteral("success"));
    return true;
}

QVariantList AppController::fontCategories() const
{
    QVariantList out;
    for (const auto &category : ::fontCategories()) {
        out.append(QVariantMap{
            {QStringLiteral("id"), category.first},
            {QStringLiteral("label"), category.second},
        });
    }
    return out;
}

QVariantList AppController::fontCatalog() const
{
    QVariantList out;
    QMap<QString, QString> labels;
    for (const auto &category : ::fontCategories())
        labels.insert(category.first, category.second);

    for (const FontFamilyEntry &entry : ::fontCatalog()) {
        QVariantList weights;
        for (int weight : entry.weights())
            weights.append(weight);

        out.append(QVariantMap{
            {QStringLiteral("id"), entry.id},
            {QStringLiteral("family"), entry.family},
            {QStringLiteral("qtFamily"), entry.qtFamily},
            {QStringLiteral("category"), entry.category},
            {QStringLiteral("categoryLabel"), labels.value(entry.category, entry.category)},
            {QStringLiteral("weights"), weights},
            {QStringLiteral("hasItalic"), entry.hasItalic()},
        });
    }
    return out;
}

QStringList AppController::systemFontFamilies() const
{
    static QStringList cached;
    static bool ready = false;
    if (!ready) {
        for (const QString &family : ::systemFontFamilies()) {
            if (!::fontFamilyForName(family))
                cached.append(family);
        }
        ready = true;
    }
    return cached;
}

QStringList AppController::userFonts() const
{
    return ::userFontFamilies();
}

QStringList AppController::importFontFile(const QUrl &fileUrl)
{
    QString path;
    std::unique_ptr<QTemporaryDir> staging;
#ifdef Q_OS_ANDROID
    if (AndroidUri::isContentUri(fileUrl)) {
        std::unique_ptr<QFile> src = AndroidUri::openForRead(fileUrl);
        staging = std::make_unique<QTemporaryDir>();
        const QString name = QFileInfo(AndroidUri::displayName(fileUrl)).fileName();
        QFile dst(staging->filePath(name));
        if (!src || !staging->isValid() || name.isEmpty() || !dst.open(QIODevice::WriteOnly)
            || dst.write(src->readAll()) < 0) {
            setLastMessage(tr("Could not import the font"), QStringLiteral("error"));
            return {};
        }
        dst.close();
        path = dst.fileName();
    } else
#endif
    {
        path = fileUrl.isLocalFile() ? fileUrl.toLocalFile() : fileUrl.toString();
    }
    if (path.isEmpty())
        return {};

    QString error;
    const QStringList families = ::importUserFontFile(path, &error);
    if (families.isEmpty()) {
        setLastMessage(error.isEmpty() ? tr("Could not import the font") : error, QStringLiteral("error"));
        return {};
    }
    drift::text::clearLayoutCache();
#ifdef DRIFT_WITH_SKIA
    drift::skia::clearTextGeometryCache();
#endif
    emitPreviewFrame();
    emit userFontsChanged();
    setLastMessage(tr("Added %1").arg(families.join(QStringLiteral(", "))), QStringLiteral("success"));
    return families;
}

bool AppController::removeUserFont(const QString &family)
{
    if (!::removeUserFont(family))
        return false;
    drift::text::clearLayoutCache();
#ifdef DRIFT_WITH_SKIA
    drift::skia::clearTextGeometryCache();
#endif
    emitPreviewFrame();
    emit userFontsChanged();
    setLastMessage(tr("Removed %1").arg(family), QStringLiteral("success"));
    return true;
}

void AppController::previewSetTextRect(int trackIndex, int clipIndex, double xPixels, double yPixels,
                                       double widthPixels, double heightPixels, int pixelSize)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    drift::Clip &clip = track.clips[clipIndex];
    if (clip.type != drift::ClipType::Text && clip.type != drift::ClipType::Subtitle)
        return;

    // The rect follows the same auto-key rules as previewSetClipRect. The glyph size is a plain
    // style field rather than a keyframed track, so it is always applied — the two move together
    // under one undo entry, because resizing a text clip should scale what you see, not just the
    // invisible wrap container.
    beginImplicitPreviewDrag(tr("Resize text"));
    const drift::TimeUs relative = qMax<drift::TimeUs>(0, m_playheadUs - clip.timelineStart);
    bool wrote = false;
    wrote = writeKeyframeValue(clip.transformX, relative, xPixels, m_autoKeyEnabled, false) || wrote;
    wrote = writeKeyframeValue(clip.transformY, relative, yPixels, m_autoKeyEnabled, false) || wrote;
    wrote = writeKeyframeValue(clip.transformW, relative, qMax(1.0, widthPixels), m_autoKeyEnabled, false)
            || wrote;
    wrote = writeKeyframeValue(clip.transformH, relative, qMax(1.0, heightPixels), m_autoKeyEnabled, false)
            || wrote;

    const int clamped = qBound(8, pixelSize, 800);
    if (clip.textStyle.pixelSize != clamped) {
        clip.textStyle.pixelSize = clamped;
        wrote = true;
    }
    if (!wrote)
        return;

    emitPreviewEdit(trackIndex, clipIndex,
                    {QStringLiteral("x"), QStringLiteral("y"), QStringLiteral("width"),
                     QStringLiteral("height"), QStringLiteral("text.pixelSize")});
}

void AppController::setClipBlendMode(int trackIndex, int clipIndex, const QString &mode)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    const drift::BlendMode next = drift::blendModeFromString(mode);
    if (track.clips[clipIndex].blendMode == next)
        return;

    const drift::Project before = m_project;
    track.clips[clipIndex].blendMode = next;
    pushProjectEdit(before, tr("Blend mode changed"));
    finishEdit(tr("Blend mode updated"));
}

bool AppController::setClipSpeed(int trackIndex, int clipIndex, double speed)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return false;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return false;

    drift::Clip &clip = track.clips[clipIndex];
    if (clip.type != drift::ClipType::Video && clip.type != drift::ClipType::Audio
        && clip.type != drift::ClipType::Composite)
        return false;

    const drift::Project before = m_project;
    clip.speed = qBound(0.25, speed, 4.0);
    clip.syncSrcOutFromSpeed(sourceDurationForClip(clip));
    syncLinkedPartnersFrom(m_project, clip);
    drift::retimeLinkedSubtitles(before, m_project);
    pushProjectEdit(before, tr("Speed changed"));
    finishEdit(tr("Clip speed updated"));
    return true;
}

void AppController::setClipReverse(int trackIndex, int clipIndex, bool reverse)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    drift::Clip &clip = track.clips[clipIndex];
    if (clip.type != drift::ClipType::Video && clip.type != drift::ClipType::Audio)
        return;
    if (clip.reverse == reverse)
        return;

    const drift::Project before = m_project;
    clip.reverse = reverse;
    drift::retimeLinkedSubtitles(before, m_project);
    pushProjectEdit(before, reverse ? tr("Reverse on") : tr("Reverse off"));
    finishEdit(reverse ? tr("Clip reversed") : tr("Clip forward"));
}

void AppController::requestClipReverse(int trackIndex, int clipIndex)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;
    const drift::Track &track = m_project.tracks().at(trackIndex);
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    const drift::Clip clip = track.clips.at(clipIndex);
    const bool needsRender =
        clip.type == drift::ClipType::Video && !clip.path.isEmpty() && clip.srcOut > clip.srcIn
        && drift::ReverseProxyCache::instance()
               .lookup(clip.path, clip.srcIn, clip.srcOut, nullptr)
               .isEmpty();
    if (!needsRender) {
        setClipReverse(trackIndex, clipIndex, true);
        return;
    }

    emit reverseConfirmRequested(trackIndex, clipIndex,
                                 drift::usToSeconds(clip.srcOut - clip.srcIn));
}

void AppController::applyClipReverse(int trackIndex, int clipIndex)
{
    if (m_reverseRendering) {
        setLastMessage(tr("A clip is already being reversed"), QStringLiteral("warning"));
        return;
    }
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;
    const drift::Track &track = m_project.tracks().at(trackIndex);
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    // By value: setClipReverse below runs a full edit cycle, and reading the clip back out of the
    // project afterwards would mean trusting indices across it.
    const drift::Clip clip = track.clips.at(clipIndex);
    const drift::TimeUs sourceDuration = sourceDurationForClip(clip);

    setClipReverse(trackIndex, clipIndex, true);

    // Audio reverses by flipping decoded blocks, which is already cheap. Only video pays the
    // per-frame keyframe seek a proxy exists to avoid.
    if (clip.type != drift::ClipType::Video || clip.path.isEmpty() || clip.srcOut <= clip.srcIn)
        return;
    if (!drift::ReverseProxyCache::instance()
             .lookup(clip.path, clip.srcIn, clip.srcOut, nullptr)
             .isEmpty())
        return;

    // Padding either side so ordinary trim-handle nudges stay inside the rendered range instead
    // of dropping the clip back onto the live path.
    constexpr drift::TimeUs kPadUs = 2 * drift::kUsPerSecond;
    drift::TimeUs coverIn = qMax<drift::TimeUs>(0, clip.srcIn - kPadUs);
    drift::TimeUs coverOut = clip.srcOut + kPadUs;
    if (sourceDuration > 0)
        coverOut = qMin(coverOut, sourceDuration);
    coverOut = qMax(coverOut, clip.srcOut);

    // Whole milliseconds, rounded outward, because the range is the rendered file's name and the
    // render must match it exactly for the file to be found again by name alone.
    coverIn = coverIn / 1000 * 1000;
    coverOut = (coverOut + 999) / 1000 * 1000;

    startReverseRender(clip.path, coverIn, coverOut);
}

void AppController::startReverseRender(const QString &sourcePath, drift::TimeUs coverInUs,
                                       drift::TimeUs coverOutUs)
{
    m_reverseCancel.storeRelaxed(0);
    m_reverseProgress = 0.0;
    emit reverseRenderProgressChanged();
    m_reverseStatus = QStringLiteral("Getting ready…");
    emit reverseRenderStatusChanged();
    m_reverseRendering = true;
    emit reverseRenderingChanged();

    // Playback deliberately keeps running: renderReversed opens its own demuxer rather than
    // driving the shared decode pool, so the clip stays watchable (on the slow path) meanwhile.
    (void)QtConcurrent::run([this, sourcePath, coverInUs, coverOutUs]() {
        auto setProgress = [this](double fraction, const QString &status) {
            QMetaObject::invokeMethod(
                this,
                [this, fraction, status]() {
                    m_reverseProgress = fraction;
                    emit reverseRenderProgressChanged();
                    if (!status.isEmpty() && status != m_reverseStatus) {
                        m_reverseStatus = status;
                        emit reverseRenderStatusChanged();
                    }
                },
                Qt::QueuedConnection);
        };

        auto finish = [this, sourcePath, coverInUs, coverOutUs](bool ok, const QString &message,
                                                                const QString &proxyPath,
                                                                bool beside) {
            QMetaObject::invokeMethod(
                this,
                [this, ok, message, proxyPath, beside, sourcePath, coverInUs, coverOutUs]() {
                    m_reverseRendering = false;
                    emit reverseRenderingChanged();
                    m_reverseProgress = ok ? 1.0 : 0.0;
                    emit reverseRenderProgressChanged();
                    m_reverseStatus = ok ? QStringLiteral("Done") : message;
                    emit reverseRenderStatusChanged();
                    if (ok && beside) {
                        drift::ReverseProxyCache::instance().addReversedSidecar(
                            sourcePath, coverInUs, coverOutUs, proxyPath);
                    } else if (ok) {
                        drift::ReverseProxyCache::instance().insert(sourcePath, coverInUs,
                                                                    coverOutUs, proxyPath);
                        if (m_assetLibrary && drift::isLocalSourcePath(sourcePath))
                            m_assetLibrary->reportProxyFallback(sourcePath);
                    }
                    setLastMessage(message, ok ? QStringLiteral("info")
                                              : QStringLiteral("error"));
                    // No pushProjectEdit: the proxy is a cache, not project content, and it must
                    // not land in the undo stack. This only asks the compositor to re-read, which
                    // now resolves to the proxy.
                    emitPreviewFrame();
                    emit reverseRenderFinished(ok, message);
                },
                Qt::QueuedConnection);
        };

        // Beside the source when that folder takes a write, else the app cache.
        QString proxyPath;
        bool beside = false;
        if (drift::isLocalSourcePath(sourcePath)) {
            const QString target = drift::reversedSidecarPath(sourcePath, coverInUs, coverOutUs);
            if (drift::canWriteBeside(target)) {
                proxyPath = target;
                beside = true;
            }
        }
        if (!beside)
            proxyPath = drift::newReversePath();
        if (proxyPath.isEmpty()) {
            finish(false, tr("Could not create a reversed file"), {}, false);
            return;
        }

        QString error;
        const bool ok = drift::renderReversed(
            sourcePath, coverInUs, coverOutUs, proxyPath, &error, [&](double fraction) {
                setProgress(fraction, tr("Reversing video…"));
                return m_reverseCancel.loadRelaxed() == 0;
            });
        finish(ok, ok ? QStringLiteral("Reversed clip ready") : error, proxyPath, beside);
    });
}

void AppController::cancelReverseRender()
{
    if (m_reverseRendering)
        m_reverseCancel.storeRelaxed(1);
}

bool AppController::clipHasReverseProxy(int trackIndex, int clipIndex) const
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return true;
    const drift::Track &track = m_project.tracks().at(trackIndex);
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return true;

    // Anything with nothing to render reports true, so the "not rendered" hint stays hidden for
    // clips a proxy would do nothing for.
    const drift::Clip &clip = track.clips.at(clipIndex);
    if (!clip.reverse || clip.type != drift::ClipType::Video || clip.path.isEmpty())
        return true;
    return !drift::ReverseProxyCache::instance()
                .lookup(clip.path, clip.srcIn, clip.srcOut, nullptr)
                .isEmpty();
}

void AppController::setClipLayer3d(int trackIndex, int clipIndex, bool enabled)
{
    if (!isValidClipIndex(trackIndex, clipIndex))
        return;
    drift::Clip &clip = m_project.tracks()[trackIndex].clips[clipIndex];
    if (clip.type == drift::ClipType::Audio || clip.type == drift::ClipType::Model3d
        || clip.layer3d == enabled)
        return;
    const drift::Project before = m_project;
    clip.layer3d = enabled;
    if (!enabled)
        clearClipPose3d(clip);
    pushProjectEdit(before, enabled ? tr("Enable 3D") : tr("Disable 3D"));
    finishEdit(enabled ? tr("Clip is a 3D layer") : tr("Clip is flat"));
}

void AppController::setClipDepthOcclude(int trackIndex, int clipIndex, bool enabled)
{
    if (!isValidClipIndex(trackIndex, clipIndex))
        return;
    drift::Clip &clip = m_project.tracks()[trackIndex].clips[clipIndex];
    // Needs a pose to sort by, and the dest-reading blend modes copy the canvas aside before they
    // draw, which a depth test cannot see through — so those are refused rather than silently
    // ignored. The inspector greys the control for the same two reasons.
    if (!clip.layer3d || clip.blendMode != drift::BlendMode::Normal || clip.depthOcclude == enabled)
        return;
    const drift::Project before = m_project;
    clip.depthOcclude = enabled;
    pushProjectEdit(before, enabled ? tr("Enable depth occlusion") : tr("Disable depth occlusion"));
    finishEdit(enabled ? tr("Clip is occluded by depth") : tr("Clip uses track order"));
}

void AppController::setClipFlip(int trackIndex, int clipIndex, bool flipH, bool flipV)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    drift::Clip &clip = track.clips[clipIndex];
    if (clip.type == drift::ClipType::Audio)
        return;
    if (clip.flipH == flipH && clip.flipV == flipV)
        return;

    const drift::Project before = m_project;
    clip.flipH = flipH;
    clip.flipV = flipV;
    pushProjectEdit(before, tr("Flip changed"));
    finishEdit(tr("Clip flip updated"));
}

// Snapshot first, then index: Project's tracks deep-detach on copy (see drift::TrackList), so
// aliasing a snapshot is no longer possible either way, but taking the reference after the copy
// keeps that independent of the container's copy semantics.
void AppController::previewSetClipPan(int trackIndex, int clipIndex, double pan)
{
    if (!isValidClipIndex(trackIndex, clipIndex))
        return;
    const drift::ClipType type = m_project.tracks().at(trackIndex).clips.at(clipIndex).type;
    if (type != drift::ClipType::Video && type != drift::ClipType::Audio)
        return;

    beginImplicitPreviewDrag(tr("Pan changed"));

    m_project.tracks()[trackIndex].clips[clipIndex].pan = qBound(-1.0, pan, 1.0);
    emitPreviewEdit(trackIndex, clipIndex, {QStringLiteral("pan")});
}

void AppController::setClipPan(int trackIndex, int clipIndex, double pan)
{
    if (!isValidClipIndex(trackIndex, clipIndex))
        return;

    const drift::Clip &existing = m_project.tracks().at(trackIndex).clips.at(clipIndex);
    if (existing.type != drift::ClipType::Video && existing.type != drift::ClipType::Audio)
        return;

    const double clamped = qBound(-1.0, pan, 1.0);
    if (qFuzzyCompare(existing.pan + 2.0, clamped + 2.0))
        return;

    const drift::Project before = m_project;
    m_project.tracks()[trackIndex].clips[clipIndex].pan = clamped;
    pushProjectEdit(before, tr("Pan changed"));
    finishEdit(tr("Clip pan updated"));
}

void AppController::setClipRotationSnap(int trackIndex, int clipIndex, double degrees)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    drift::Clip &clip = track.clips[clipIndex];
    if (clip.type == drift::ClipType::Audio)
        return;

    // Normalize to (-180, 180]
    double snapped = degrees;
    while (snapped > 180.0)
        snapped -= 360.0;
    while (snapped <= -180.0)
        snapped += 360.0;

    const drift::Project before = m_project;
    // Snap replaces the rotation curve with a single constant key so inspector
    // chips and preview stay in lockstep (no leftover mid-curve keys).
    clip.rotation = {};
    clip.rotation.setKeyframe(0, snapped);
    pushProjectEdit(before, tr("Rotation snapped"));
    finishEdit(tr("Rotation set to %1°").arg(snapped, 0, 'f', 0));
}

int AppController::clipOrientation(const drift::Clip &clip) const
{
    const drift::MediaAsset *asset = m_project.asset(clip.assetId);
    const int probed = asset ? asset->rotationDegrees : 0;
    return ((probed + clip.rotationCorrection) % 360 + 360) % 360;
}

void AppController::setClipOrientationTo(drift::Clip &clip, int degrees)
{
    const drift::MediaAsset *asset = m_project.asset(clip.assetId);
    const int probed = asset ? asset->rotationDegrees : 0;
    const int correction = ((degrees - probed) % 360 + 360) % 360;
    // Depth is estimated on upright frames; turned, it would no longer line up with them.
    if (correction != clip.rotationCorrection)
        clip.depthPath.clear();
    clip.rotationCorrection = correction;
}

void AppController::setClipOrientation(int trackIndex, int clipIndex, int degrees)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    drift::Clip &clip = track.clips[clipIndex];
    // Images and text/shape clips do not carry a source display-matrix rotation to correct — only
    // a decoded video frame does.
    if (clip.type != drift::ClipType::Video)
        return;

    int normalized = ((degrees % 360) + 360) % 360;
    normalized = ((normalized + 45) / 90 * 90) % 360;

    const int oldRotation = clipOrientation(clip);
    if (oldRotation == normalized)
        return;

    const drift::Project before = m_project;

    // Only the aspect category (portrait-swapped vs not) needs the box to change — going from,
    // say, 90 to 270 spins the same content inside the same box. Transposing whatever box the
    // clip currently has (not re-deriving one from the source media) preserves any manual
    // resize/reposition the user already made, and needs no canvas or media-size lookup at all.
    const bool oldSwapped = (oldRotation == 90 || oldRotation == 270);
    const bool newSwapped = (normalized == 90 || normalized == 270);
    if (oldSwapped != newSwapped) {
        // setClipLayoutPixels would collapse every track to one key sampled at time 0, destroying
        // any existing position/size animation. Width and height instead trade places outright —
        // new width is exactly the old height track and vice versa, so swapping the two
        // KeyframeTrack objects wholesale carries every keyframe across untouched.
        const drift::KeyframeTrack<double> oldW = clip.transformW;
        const drift::KeyframeTrack<double> oldH = clip.transformH;
        std::swap(clip.transformW, clip.transformH);

        // X/Y shift so the box's centre stays put under the transposed size: by (w - h)/2 for X,
        // the mirror for Y. Each existing key moves by the offset at its own time, which keeps
        // its tangents, hold flag and the track's enabled state exactly as they were. When the
        // size itself animates the offset is a curve, so keys are also added at width/height's
        // key times to pin it there; between keys it is only approximated, which is the trade
        // for not resampling the user's curve into something they no longer recognise.
        const bool sizeIsAnimated = oldW.keyframes().size() > 1 || oldH.keyframes().size() > 1;
        auto offsetAt = [&](drift::TimeUs t, bool axisIsX) {
            const double w = oldW.isEmpty() ? double(m_project.width()) : oldW.evaluateAt(t);
            const double h = oldH.isEmpty() ? double(m_project.height()) : oldH.evaluateAt(t);
            return axisIsX ? (w - h) * 0.5 : (h - w) * 0.5;
        };
        auto shiftAxis = [&](drift::KeyframeTrack<double> &axis, bool axisIsX) {
            if (axis.isEmpty())
                return;
            const drift::KeyframeTrack<double> original = axis;
            if (sizeIsAnimated && axis.enabled()) {
                for (auto it = oldW.keyframes().constBegin(); it != oldW.keyframes().constEnd(); ++it)
                    if (!original.keyframes().contains(it.key()))
                        axis.setKeyframe(it.key(), original.evaluateAt(it.key()));
                for (auto it = oldH.keyframes().constBegin(); it != oldH.keyframes().constEnd(); ++it)
                    if (!original.keyframes().contains(it.key()))
                        axis.setKeyframe(it.key(), original.evaluateAt(it.key()));
            }
            for (const drift::TimeUs t : axis.keyframes().keys())
                axis.keyframeRef(t)->value += offsetAt(t, axisIsX);
        };
        shiftAxis(clip.transformX, true);
        shiftAxis(clip.transformY, false);
    }

    setClipOrientationTo(clip, normalized);
    pushProjectEdit(before, tr("Orientation changed"));
    finishEdit(tr("Clip orientation set to %1°").arg(normalized));
}
