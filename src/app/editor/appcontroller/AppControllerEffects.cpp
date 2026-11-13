#include "engine/platform/AndroidUri.h"
#include "app/editor/appcontroller/AppControllerInternal.h"
#include "engine/packages/effects/EffectCatalog.h"
#include "core/effects/EffectStackStore.h"
#include "app/jobs/vision/FaceTrackingController.h"
#include "app/jobs/vision/SegmentationController.h"

#include "app/online/addons/AddonManager.h"
#include "engine/packages/audio/AudioEffectCatalog.h"
#include "engine/packages/effects/EffectTemplateCatalog.h"
// Engine-internal by its own header comment, and included here only for releaseCaches(): the GPU
// caches have no other owner outside src/engine to ask. A one-line forwarder on GpuCompositor
// would restore the boundary.
#include "engine/face/props/FacePropCatalog.h"
#include "engine/face/props/FacePropImport.h"

#include <QClipboard>
#include <QGuiApplication>

#include <QDir>
#include <QFile>
#include <QTemporaryFile>
#include <QByteArray>

using namespace drift::appdetail;
using drift::mcpdetail::findClipById;
using drift::mcpdetail::runBeatAnalysis;
using namespace drift::appcontroller_detail;

namespace {

template<typename T>
drift::KeyframeTrack<T> rescaleKeyframeTrackTimes(const drift::KeyframeTrack<T> &track,
                                                  drift::TimeUs fromDurationUs,
                                                  drift::TimeUs toDurationUs)
{
    if (track.isEmpty() || fromDurationUs <= 0 || toDurationUs <= 0 || fromDurationUs == toDurationUs)
        return track;

    const double scale = static_cast<double>(toDurationUs) / static_cast<double>(fromDurationUs);
    drift::KeyframeTrack<T> out;
    out.setEnabled(track.enabled());
    for (auto it = track.keyframes().constBegin(); it != track.keyframes().constEnd(); ++it) {
        drift::Keyframe<T> key = it.value();
        key.inDx *= scale;
        key.outDx *= scale;
        out.setKeyframe(static_cast<drift::TimeUs>(llround(static_cast<double>(it.key()) * scale)), key);
    }
    return out;
}
} // namespace

QVariantList AppController::effectCatalog() const
{
    QVariantList out;
    for (const EffectPresetEntry &def : ::effectCatalog()) {
        QVariantList params;
        for (const drift::EffectParamSpec &p : def.meta.parameters) {
            params.append(QVariantMap{
                {QStringLiteral("key"), p.key},
                {QStringLiteral("label"), p.label},
                {QStringLiteral("min"), p.min},
                {QStringLiteral("max"), p.max},
                {QStringLiteral("default"), p.defaultValue},
                {QStringLiteral("isBoolean"), p.isBoolean()},
                {QStringLiteral("type"), p.typeName()},
            });
        }
        out.append(QVariantMap{
            {QStringLiteral("id"), def.meta.id},
            {QStringLiteral("label"), def.meta.displayName},
            {QStringLiteral("displayName"), def.meta.displayName},
            {QStringLiteral("category"), def.meta.category},
            {QStringLiteral("categoryLabel"), effectCategoryLabel(def.meta.category)},
            {QStringLiteral("compositorOnly"), def.meta.compositorOnly},
            {QStringLiteral("thumbnailPath"), def.thumbnailPath},
            {QStringLiteral("user"), isUserPackageDir(def.gpu.packageDir, QStringLiteral("effects"))},
            {QStringLiteral("params"), params},
        });
    }
    return out;
}

QVariantList AppController::effectCategories() const
{
    QVariantList out;
    for (const auto &entry : ::effectCategories()) {
        out.append(QVariantMap{
            {QStringLiteral("id"), entry.first},
            {QStringLiteral("label"), entry.second},
        });
    }
    return out;
}
namespace {


// Every effect that follows baked face landmarks is named face_*. The catalog carries no other
// marker for it, and the inspector's own beauty/mesh checks already key off the same prefix.
bool isFaceEffectId(const QString &catalogId)
{
    return catalogId.startsWith(QLatin1String("face_"));
}

} // namespace

bool AppController::effectFitsTrack(int trackIndex, const QString &effectId, QString *why) const
{
    const EffectPresetEntry *def = effectDefForId(effectId);
    if (!def || !(def->needsFace || def->needsDepth || def->needsMask || def->isFaceSwap || def->isModel3d))
        return true;
    const bool standalone = trackIndex < 0
                            || (trackIndex < m_project.tracks().size()
                                && m_project.tracks().at(trackIndex).isAdjustment()
                                && !m_project.tracks().at(trackIndex).isAdjustmentLane());
    if (!standalone)
        return true;
    if (why) {
        *why = def->needsDepth
                   ? tr("Depth effects read one clip's depth, so they go on a clip, not on an adjustment layer.")
               : def->needsMask
                   ? tr("Mask effects read one clip's masks, so they go on a clip, not on an adjustment layer.")
                   : tr("Face effects follow one clip's faces, so they go on a clip, not on an adjustment layer.");
    }
    return false;
}

void AppController::addEffect(int trackIndex, int clipIndex, const QString &effectId)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;
    QString why;
    if (!effectFitsTrack(trackIndex, effectId, &why)) {
        setLastMessage(why, QStringLiteral("warning"));
        return;
    }

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    const EffectPresetEntry *def = effectDefForId(effectId);
    if (!def)
        return;

    drift::Effect effect;
    effect.name = def->filterName;
    effect.catalogId = def->meta.id;
    for (auto it = def->fixedParams.constBegin(); it != def->fixedParams.constEnd(); ++it)
        effect.parameters.insert(it.key(), it.value());
    for (const drift::EffectParamSpec &p : def->meta.parameters)
        p.insertDefault(effect.parameters);

    // The parent track is about to be addressed by id, so it needs one before the snapshot.
    m_project.ensureTrackIds();

    // A face effect follows baked landmarks and does nothing without them, so adding one starts
    // the scan rather than leaving the user to discover why the effect looks inert. Captured by
    // id before the edit, because minting a lane shifts every index after it.
    QString faceSourceId;
    if (isFaceEffectId(effectId)) {
        const drift::ClipRef source = sourceClipRef(trackIndex, clipIndex);
        if (source.trackIndex >= 0) {
            const drift::Clip &sourceClip =
                m_project.tracks().at(source.trackIndex).clips.at(source.clipIndex);
            if (sourceClip.faceTrackPath.isEmpty())
                faceSourceId = sourceClip.id;
        }
    }

    // Snapshot first: creating the lane is part of the edit, so undo must take it back out
    // along with the effect rather than leaving an empty row behind.
    const drift::Project before = m_project;

    const QString clipId = track.clips.at(clipIndex).id;
    int hostTrack = trackIndex;
    int hostClip = clipIndex;
    if (!redirectToEffectHost(&hostTrack, &hostClip, drift::AdjustmentKind::VideoEffects,
                              /*create=*/true)) {
        return;
    }
    m_project.tracks()[hostTrack].clips[hostClip].effects.append(effect);

    // The clip stays selected: its Effects tab edits the linked adjustment's stack. Minting a lane
    // can shift the clip's track, so the selection is re-pointed by id.
    if (!clipId.isEmpty()) {
        int selTrack = -1;
        int selClip = -1;
        if (findClipById(m_project, clipId, &selTrack, &selClip)) {
            m_selectedTrack = selTrack;
            m_selectedClip = selClip;
            m_selection = {qMakePair(selTrack, selClip)};
        }
    }

    pushProjectEdit(before, tr("Add effect"));
    finishEdit(tr("Effect added"));

    if (faceSourceId.isEmpty() || m_faces->detecting() || !m_faces->available())
        return;
    int faceTrack = -1;
    int faceClip = -1;
    if (findClipById(m_project, faceSourceId, &faceTrack, &faceClip))
        m_faces->detect(faceTrack, faceClip);
}
namespace {


drift::Effect effectFromCatalogEntry(const EffectPresetEntry &def,
                                     const QMap<QString, QVariant> &overrides)
{
    drift::Effect effect;
    effect.name = def.filterName;
    effect.catalogId = def.meta.id;
    for (auto it = def.fixedParams.constBegin(); it != def.fixedParams.constEnd(); ++it)
        effect.parameters.insert(it.key(), it.value());
    for (const drift::EffectParamSpec &p : def.meta.parameters) {
        const auto overrideIt = overrides.constFind(p.key);
        if (overrideIt != overrides.constEnd())
            effect.parameters.insert(p.key, overrideIt.value());
        else
            p.insertDefault(effect.parameters);
    }
    return effect;
}

// The audio twin of effectFromCatalogEntry. Audio presets have no fixedParams and no filter
// graph, so the name is the display name and the whole parameter map comes from the manifest.
drift::Effect audioEffectFromCatalogEntry(const AudioEffectEntry &def,
                                          const QMap<QString, QVariant> &overrides)
{
    drift::Effect effect;
    effect.name = def.displayName;
    effect.catalogId = def.id;
    for (const drift::EffectParamSpec &p : def.parameters) {
        const auto overrideIt = overrides.constFind(p.key);
        if (overrideIt != overrides.constEnd())
            effect.parameters.insert(p.key, overrideIt.value());
        else
            p.insertDefault(effect.parameters);
    }
    return effect;
}

bool templateSyncNeedsBeats(const QString &sync)
{
    return sync == QLatin1String("onset") || sync == QLatin1String("beat")
           || sync == QLatin1String("bar");
}

// The clip half only; the caller pins the matte with setLinkedMask once it knows where the clip
// landed, since that call needs a track index and inserts lanes of its own.
drift::Clip deriveMaskedClip(const drift::Clip &source, const QString &suffix)
{
    drift::Clip clip = source;
    clip.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    clip.linkId.clear();
    clip.effects.clear();
    clip.audioEffects.clear();
    clip.name = (source.name.isEmpty()
                     ? QCoreApplication::translate("AppController", "Clip")
                     : source.name)
                + suffix;
    return clip;
}

void applyTemplateLayersToClip(drift::Clip &clip, const QList<EffectTemplateLayer> &layers,
                               const QString &sync, const QList<drift::TimeUs> &syncPoints)
{
    const int baseEffectIndex = clip.effects.size();
    for (const EffectTemplateLayer &layer : layers) {
        const EffectPresetEntry *def = effectDefForId(layer.effectId);
        if (!def)
            continue;
        clip.effects.append(effectFromCatalogEntry(*def, layer.params));
    }

    for (int layerIndex = 0; layerIndex < layers.size(); ++layerIndex) {
        const EffectTemplateLayer &layer = layers.at(layerIndex);
        if (!layer.pulse.valid)
            continue;

        const int effectIndex = baseEffectIndex + layerIndex;
        if (effectIndex >= clip.effects.size())
            continue;

        const QString prop =
            QStringLiteral("fx.%1.%2").arg(effectIndex).arg(layer.pulse.param);
        const drift::TimeUs decayUs =
            static_cast<drift::TimeUs>(qMax(layer.pulse.decayMs, 0)) * 1000;
        if (sync == QLatin1String("clip") && syncPoints.size() >= 2) {
            writeClipPropValue(clip, prop, syncPoints.first(), layer.pulse.peak, false, true);
            writeClipPropValue(clip, prop, syncPoints.last(), layer.pulse.rest, false, true);
            continue;
        }
        for (drift::TimeUs t : syncPoints) {
            writeClipPropValue(clip, prop, t, layer.pulse.peak, false, true);
            writeClipPropValue(clip, prop, t + decayUs, layer.pulse.rest, false, true);
        }
    }
}

void applyTemplateSpeedPulse(drift::Clip &clip, const EffectTemplateSpeedPulse &pulse,
                             const QString &sync, const QList<drift::TimeUs> &syncPoints)
{
    if (!pulse.valid || clip.timelineDuration <= 0)
        return;

    const double baseSpeed = clip.effectiveSpeed();
    QList<drift::SpeedPoint> points;
    points.append({0.0, baseSpeed, 0.0, 0.0, 0.0, 0.0, false});
    points.append({1.0, baseSpeed, 0.0, 0.0, 0.0, 0.0, false});

    auto addPoint = [&](double pos, double speed) {
        pos = qBound(0.0, pos, 1.0);
        for (int i = 0; i < points.size(); ++i) {
            if (qFuzzyCompare(points[i].pos, pos)) {
                points[i].speed = speed;
                return;
            }
        }
        points.append({pos, speed, 0.0, 0.0, 0.0, 0.0, true});
    };

    const drift::TimeUs decayUs =
        static_cast<drift::TimeUs>(qMax(pulse.decayMs, 0)) * 1000;
    const double dur = static_cast<double>(clip.timelineDuration);

    if (sync == QLatin1String("clip") && syncPoints.size() >= 2) {
        addPoint(0.0, pulse.peak);
        addPoint(static_cast<double>(syncPoints.last()) / dur, pulse.rest);
    } else {
        for (drift::TimeUs t : syncPoints) {
            addPoint(static_cast<double>(t) / dur, pulse.peak);
            addPoint(static_cast<double>(t + decayUs) / dur, pulse.rest);
        }
    }

    clip.speedCurve.setPoints(points);
    clip.syncDurationFromSpeedCurve();
}

const EffectTemplateTrack *trackForRole(const EffectTemplateEntry &entry, const QString &role)
{
    for (const EffectTemplateTrack &track : entry.tracks) {
        if (track.role == role)
            return &track;
    }
    return nullptr;
}

bool clipsShareTemplateSource(const drift::Clip &a, const drift::Clip &b)
{
    return a.path == b.path && a.srcIn == b.srcIn && a.srcOut == b.srcOut
           && a.timelineStart == b.timelineStart && a.timelineDuration == b.timelineDuration;
}

bool isDerivedTemplateClipName(const QString &name)
{
    return name.endsWith(QStringLiteral(" (fg)")) || name.endsWith(QStringLiteral(" (bg)"))
           || name.endsWith(QStringLiteral(" (clone)"));
}

struct TemplateStackRefs
{
    int fgTrack = -1;
    int fgClip = -1;
    int bgTrack = -1;
    int bgClip = -1;
    QList<QPair<int, int>> clones;

    bool valid() const { return fgTrack >= 0 && bgTrack >= 0; }
};

TemplateStackRefs findExistingTemplateStack(const drift::Project &project, const drift::Clip &source)
{
    TemplateStackRefs stack;
    for (int t = 0; t < project.tracks().size(); ++t) {
        const drift::Track &track = project.tracks().at(t);
        for (int c = 0; c < track.clips.size(); ++c) {
            const drift::Clip &clip = track.clips.at(c);
            if (!clipsShareTemplateSource(clip, source))
                continue;
            if (clip.name.endsWith(QStringLiteral(" (fg)"))) {
                stack.fgTrack = t;
                stack.fgClip = c;
            } else if (clip.name.endsWith(QStringLiteral(" (bg)"))) {
                stack.bgTrack = t;
                stack.bgClip = c;
            } else if (clip.name.endsWith(QStringLiteral(" (clone)"))) {
                stack.clones.append({t, c});
            }
        }
    }
    return stack;
}

void resetTemplateDerivedClip(drift::Clip &clip, double opacity)
{
    clip.effects.clear();
    clip.opacity.setKeyframe(0, opacity);
    clip.speedCurve = drift::SpeedCurve::flat(1.0);
    clip.syncDurationFromSpeedCurve();
}

} // namespace

QVariantList AppController::effectTemplateCatalog() const
{
    QVariantList out;
    for (const EffectTemplateEntry &entry : ::effectTemplateCatalog()) {
        QVariantList layers;
        for (const EffectTemplateLayer &layer : entry.layers) {
            layers.append(QVariantMap{
                {QStringLiteral("effectId"), layer.effectId},
            });
        }

        QVariantList effectThumbnails;
        QSet<QString> seenEffectIds;
        const auto appendEffectThumb = [&](const QString &effectId) {
            if (effectId.isEmpty() || seenEffectIds.contains(effectId))
                return;
            const EffectPresetEntry *def = effectDefForId(effectId);
            if (!def || def->thumbnailPath.isEmpty())
                return;
            seenEffectIds.insert(effectId);
            effectThumbnails.append(def->thumbnailPath);
        };
        for (const EffectTemplateLayer &layer : entry.layers)
            appendEffectThumb(layer.effectId);
        for (const EffectTemplateTrack &track : entry.tracks) {
            for (const EffectTemplateLayer &layer : track.layers)
                appendEffectThumb(layer.effectId);
        }

        out.append(QVariantMap{
            {QStringLiteral("id"), entry.id},
            {QStringLiteral("label"), entry.displayName},
            {QStringLiteral("displayName"), entry.displayName},
            {QStringLiteral("category"), entry.category},
            {QStringLiteral("categoryLabel"), effectTemplateCategoryLabel(entry.category)},
            {QStringLiteral("sync"), entry.sync},
            {QStringLiteral("thumbnailPath"), entry.thumbnailPath},
            {QStringLiteral("effectThumbnails"), effectThumbnails},
            {QStringLiteral("effectCount"), seenEffectIds.size()},
            {QStringLiteral("requiresSegmentation"), entry.requiresSegmentation},
            {QStringLiteral("layers"), layers},
        });
    }
    return out;
}

QVariantList AppController::effectTemplateCategories() const
{
    QVariantList out;
    for (const auto &entry : ::effectTemplateCategories()) {
        out.append(QVariantMap{
            {QStringLiteral("id"), entry.first},
            {QStringLiteral("label"), entry.second},
        });
    }
    return out;
}

bool AppController::beatAnalysisReadyForClip(const drift::Clip &clip, const QString &sync) const
{
    if (sync == QLatin1String("clip"))
        return true;
    if (m_beatAnalysis.isEmpty() || audioLayoutFingerprint() != m_beatAudioFingerprint)
        return false;

    const double rangeStart = m_beatAnalysis.value(QStringLiteral("rangeStart")).toDouble();
    const double rangeDur = m_beatAnalysis.value(QStringLiteral("rangeDuration")).toDouble();
    const double clipStart = drift::usToSeconds(clip.timelineStart);
    const double clipEnd =
        drift::usToSeconds(clip.timelineStart + clip.timelineDuration);
    if (clipStart < rangeStart - 0.001 || clipEnd > rangeStart + rangeDur + 0.001)
        return false;

    if (sync == QLatin1String("onset"))
        return !m_beatAnalysisRaw.onsets.isEmpty();
    return !m_beatAnalysisRaw.beats.isEmpty();
}

bool AppController::resolveTemplateApplyTarget(int *trackIndex, int *clipIndex) const
{
    if (!trackIndex || !clipIndex)
        return false;
    if (*trackIndex < 0 || *trackIndex >= m_project.tracks().size())
        return false;
    const drift::Track &track = m_project.tracks().at(*trackIndex);
    if (*clipIndex < 0 || *clipIndex >= track.clips.size())
        return false;

    const drift::Clip &selected = track.clips.at(*clipIndex);
    if (!isDerivedTemplateClipName(selected.name))
        return false;

    for (int t = 0; t < m_project.tracks().size(); ++t) {
        const drift::Track &candidateTrack = m_project.tracks().at(t);
        for (int c = 0; c < candidateTrack.clips.size(); ++c) {
            const drift::Clip &candidate = candidateTrack.clips.at(c);
            if (!clipsShareTemplateSource(candidate, selected))
                continue;
            if (isDerivedTemplateClipName(candidate.name))
                continue;
            *trackIndex = t;
            *clipIndex = c;
            return true;
        }
    }
    return false;
}

void AppController::applyEffectTemplateInternal(int trackIndex, int clipIndex,
                                                const EffectTemplateEntry &entry,
                                                const QString &mattePath,
                                                drift::TimeUs matteSrcOffsetUs)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    const drift::Clip sourceClip = track.clips[clipIndex];
    const drift::Project before = m_project;

    // Full-frame: the matte's own pixels place the subject, and the parametric defaults would
    // crop it to 60% of the frame.
    drift::Mask matte = mattePath.isEmpty() ? clipMatte(m_project, trackIndex, clipIndex)
                                            : drift::fullFrameMediaMask(mattePath, matteSrcOffsetUs);

    const bool segmented = entry.requiresSegmentation || entry.usesMultiTrack();
    const bool haveMatte = matte.isMedia();

    QList<drift::TimeUs> syncPoints;
    const drift::TimeUs clipStart = sourceClip.timelineStart;
    const drift::TimeUs clipEnd = clipStart + sourceClip.timelineDuration;
    if (entry.sync == QLatin1String("clip")) {
        syncPoints.append(0);
        if (sourceClip.timelineDuration > 0)
            syncPoints.append(sourceClip.timelineDuration);
    } else if (entry.sync == QLatin1String("onset")) {
        for (const AudioOnset &onset : std::as_const(m_beatAnalysisRaw.onsets)) {
            const drift::TimeUs at = drift::secondsToUs(onset.seconds);
            if (at >= clipStart && at < clipEnd)
                syncPoints.append(at - clipStart);
        }
    } else {
        int beatIndex = 0;
        for (double beatSeconds : std::as_const(m_beatAnalysisRaw.beats)) {
            const drift::TimeUs at = drift::secondsToUs(beatSeconds);
            if (at >= clipStart && at < clipEnd) {
                if (entry.sync == QLatin1String("bar")) {
                    const int rel = beatIndex - m_beatAnalysisRaw.firstDownbeat;
                    if (rel >= 0 && rel % m_beatAnalysisRaw.beatsPerBar == 0)
                        syncPoints.append(at - clipStart);
                } else {
                    syncPoints.append(at - clipStart);
                }
            }
            ++beatIndex;
        }
    }

    auto applyTrackLayers = [&](drift::Clip &clip, const EffectTemplateTrack &trackDef) {
        applyTemplateLayersToClip(clip, trackDef.layers, entry.sync, syncPoints);
        if (trackDef.opacity < 0.999)
            clip.opacity.setKeyframe(0, trackDef.opacity);
        applyTemplateSpeedPulse(clip, trackDef.speedPulse, entry.sync, syncPoints);
    };

    int selectTrack = trackIndex;
    int selectClip = clipIndex;

    // Pinning a mask mints a lane, which inserts a track and invalidates every reference and
    // index this block holds. So the matte assignments are queued by clip id and applied once the
    // structural work below is finished.
    QList<QPair<QString, drift::Mask>> pendingMasks;
    const auto pinMatte = [&pendingMasks, &matte](const drift::Clip &clip, bool invert) {
        drift::Mask copy = matte;
        copy.invert = invert;
        pendingMasks.append(qMakePair(clip.id, copy));
    };

    if (segmented && haveMatte) {
        pinMatte(track.clips[clipIndex], false);

        const bool sourceHidden = sourceClip.opacity.evaluateAt(0) < 0.05;
        const TemplateStackRefs existingStack = findExistingTemplateStack(m_project, sourceClip);
        const bool reuseStack =
            sourceHidden && existingStack.valid()
            && existingStack.clones.size() == entry.clones.count;

        if (reuseStack) {
            track.clips[clipIndex].opacity.setKeyframe(0, 0.0);

            drift::Clip &fgClip = m_project.tracks()[existingStack.fgTrack].clips[existingStack.fgClip];
            drift::Clip &bgClip = m_project.tracks()[existingStack.bgTrack].clips[existingStack.bgClip];
            resetTemplateDerivedClip(fgClip, 1.0);
            resetTemplateDerivedClip(bgClip, 1.0);
            pinMatte(fgClip, false);
            pinMatte(bgClip, true);

            selectTrack = existingStack.fgTrack;
            selectClip = existingStack.fgClip;

            if (const EffectTemplateTrack *bgDef = trackForRole(entry, QStringLiteral("background")))
                applyTrackLayers(bgClip, *bgDef);
            if (const EffectTemplateTrack *fgDef = trackForRole(entry, QStringLiteral("foreground")))
                applyTrackLayers(fgClip, *fgDef);

            for (int i = 0; i < existingStack.clones.size(); ++i) {
                const auto &ref = existingStack.clones.at(i);
                drift::Clip &clone = m_project.tracks()[ref.first].clips[ref.second];
                const double opacity = i < entry.clones.opacities.size()
                                           ? entry.clones.opacities.at(i)
                                           : 0.25;
                resetTemplateDerivedClip(clone, opacity);
                pinMatte(clone, false);
                if (i < entry.clones.scales.size()) {
                    const double scale = entry.clones.scales.at(i);
                    const double w = sourceClip.transformW.isEmpty()
                                           ? static_cast<double>(m_project.width())
                                           : sourceClip.transformW.evaluateAt(0);
                    const double h = sourceClip.transformH.isEmpty()
                                           ? static_cast<double>(m_project.height())
                                           : sourceClip.transformH.evaluateAt(0);
                    clone.transformW.setKeyframe(0, w * scale);
                    clone.transformH.setKeyframe(0, h * scale);
                }
                if (const EffectTemplateTrack *cloneDef =
                        trackForRole(entry, QStringLiteral("clone"))) {
                    applyTrackLayers(clone, *cloneDef);
                } else if (i == entry.clones.count - 1) {
                    const EffectPresetEntry *trail = effectDefForId(QStringLiteral("motion_trail"));
                    if (trail)
                        clone.effects.append(effectFromCatalogEntry(*trail, {}));
                }
            }
        } else {
            // Hide the untouched source; fg/bg/clone layers replace it visually.
            track.clips[clipIndex].opacity.setKeyframe(0, 0.0);

            const int fgTrack =
                drift::insertTrackAboveForClipType(m_project, trackIndex, drift::ClipType::Video);
            m_project.tracks()[fgTrack].clips.append(
                deriveMaskedClip(sourceClip, QStringLiteral(" (fg)")));
            pinMatte(m_project.tracks()[fgTrack].clips.constLast(), false);

            const int bgTrack =
                drift::insertTrackAboveForClipType(m_project, fgTrack + 1, drift::ClipType::Video);
            m_project.tracks()[bgTrack].clips.append(
                deriveMaskedClip(sourceClip, QStringLiteral(" (bg)")));
            pinMatte(m_project.tracks()[bgTrack].clips.constLast(), true);

            selectTrack = fgTrack;
            selectClip = 0;

            if (const EffectTemplateTrack *bgDef = trackForRole(entry, QStringLiteral("background")))
                applyTrackLayers(m_project.tracks()[bgTrack].clips[0], *bgDef);
            if (const EffectTemplateTrack *fgDef = trackForRole(entry, QStringLiteral("foreground")))
                applyTrackLayers(m_project.tracks()[fgTrack].clips[0], *fgDef);

            if (entry.clones.count > 0) {
                int insertAbove = fgTrack + 1;
                for (int i = 0; i < entry.clones.count; ++i) {
                    const int cloneTrack = drift::insertTrackAboveForClipType(
                        m_project, insertAbove, drift::ClipType::Video);
                    drift::Clip clone = deriveMaskedClip(sourceClip, QStringLiteral(" (clone)"));
                    pinMatte(clone, false);
                    const double opacity = i < entry.clones.opacities.size()
                                               ? entry.clones.opacities.at(i)
                                               : 0.25;
                    clone.opacity.setKeyframe(0, opacity);
                    if (i < entry.clones.scales.size()) {
                        const double scale = entry.clones.scales.at(i);
                        const double w = sourceClip.transformW.isEmpty()
                                               ? static_cast<double>(m_project.width())
                                               : sourceClip.transformW.evaluateAt(0);
                        const double h = sourceClip.transformH.isEmpty()
                                               ? static_cast<double>(m_project.height())
                                               : sourceClip.transformH.evaluateAt(0);
                        clone.transformW.setKeyframe(0, w * scale);
                        clone.transformH.setKeyframe(0, h * scale);
                    }
                    if (const EffectTemplateTrack *cloneDef =
                            trackForRole(entry, QStringLiteral("clone"))) {
                        applyTrackLayers(clone, *cloneDef);
                    } else if (i == entry.clones.count - 1) {
                        const EffectPresetEntry *trail = effectDefForId(QStringLiteral("motion_trail"));
                        if (trail)
                            clone.effects.append(effectFromCatalogEntry(*trail, {}));
                    }
                    m_project.tracks()[cloneTrack].clips.append(clone);
                    insertAbove = cloneTrack + 1;
                }
            }
        }
    } else if (entry.usesMultiTrack()) {
        if (const EffectTemplateTrack *fgDef = trackForRole(entry, QStringLiteral("foreground")))
            applyTrackLayers(track.clips[clipIndex], *fgDef);
    } else {
        applyTemplateLayersToClip(track.clips[clipIndex], entry.layers, entry.sync, syncPoints);
        if (entry.speedPulse.valid)
            applyTemplateSpeedPulse(track.clips[clipIndex], entry.speedPulse, entry.sync,
                                    syncPoints);
    }

    // Now that no reference into the track list is live, pin the mattes. Each one can insert a
    // lane, so every clip is re-resolved by id rather than trusting an index captured earlier.
    const QString selectId = selectTrack >= 0 && selectTrack < m_project.tracks().size()
                                     && selectClip >= 0
                                     && selectClip < m_project.tracks().at(selectTrack).clips.size()
                                 ? m_project.tracks().at(selectTrack).clips.at(selectClip).id
                                 : QString();
    for (const auto &pending : std::as_const(pendingMasks)) {
        int maskTrack = -1;
        int maskClip = -1;
        if (findClipById(m_project, pending.first, &maskTrack, &maskClip))
            drift::addLinkedMask(m_project, maskTrack, maskClip, pending.second);
    }
    if (!selectId.isEmpty()) {
        int foundTrack = -1;
        int foundClip = -1;
        if (findClipById(m_project, selectId, &foundTrack, &foundClip)) {
            selectTrack = foundTrack;
            selectClip = foundClip;
        }
    }

    m_selectedTrack = selectTrack;
    m_selectedClip = selectClip;
    m_selection = {qMakePair(selectTrack, selectClip)};
    pushProjectEdit(before, tr("Apply effect template"));
    finishEdit(tr("Template applied"));
}

void AppController::applyEffectTemplate(int trackIndex, int clipIndex, const QString &templateId)
{
    const EffectTemplateEntry *entry = effectTemplateForId(templateId);
    if (!entry)
        return;

    resolveTemplateApplyTarget(&trackIndex, &clipIndex);

    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    const drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    const drift::Clip &clip = track.clips[clipIndex];

    if (templateSyncNeedsBeats(entry->sync) && !beatAnalysisReadyForClip(clip, entry->sync)) {
        m_pendingEffectTemplate = PendingEffectTemplate{trackIndex, clipIndex, templateId};
        if (!m_beatAnalysisRunning) {
            analyzeBeats(drift::usToSeconds(clip.timelineStart),
                         drift::usToSeconds(clip.timelineDuration));
        }
        return;
    }

    const bool needsSegment = (entry->requiresSegmentation || entry->usesMultiTrack())
                              && !clipHasMatte(m_project, trackIndex, clipIndex);
    if (needsSegment) {
        if (!m_segmentation->available()) {
            setLastMessage(
                tr("This effect needs a subject cutout — open Extras to install it"),
                QStringLiteral("warning"));
            return;
        }
        if (m_segmentation->running()) {
            m_pendingEffectTemplate = PendingEffectTemplate{trackIndex, clipIndex, templateId};
            return;
        }
        m_pendingEffectTemplate = PendingEffectTemplate{trackIndex, clipIndex, templateId};
        m_segmentation->openSegmentationForTemplate(trackIndex, clipIndex);
        return;
    }

    m_pendingEffectTemplate.reset();
    applyEffectTemplateInternal(trackIndex, clipIndex, *entry);
}

void AppController::removeEffect(int trackIndex, int clipIndex, int effectIndex)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;
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

    const drift::Project before = m_project;
    clip.effects.removeAt(effectIndex);
    dropKeyframeGraphPropertiesForEffect(effectIndex);
    pushProjectEdit(before, tr("Remove effect"));
    finishEdit(tr("Effect removed"));
}

int AppController::removeAllEffects(int trackIndex, int clipIndex, bool includeMasks)
{
    if (!isValidClipIndex(trackIndex, clipIndex))
        return 0;
    // add_effect reports the adjustment as the stack's host, so an agent may well pass that id:
    // a pinned adjustment stands for the clip it is pinned to.
    const drift::ClipRef source = sourceClipRef(trackIndex, clipIndex);
    if (source.trackIndex < 0)
        return 0;
    const drift::Clip &host = m_project.tracks().at(source.trackIndex).clips.at(source.clipIndex);
    if (host.type == drift::ClipType::Adjustment)
        return 0;
    const QString hostId = host.id;

    const auto removable = [includeMasks](const drift::Clip &clip) {
        switch (clip.adjustmentKind) {
        case drift::AdjustmentKind::VideoEffects:
        case drift::AdjustmentKind::AudioEffects:
            return true;
        case drift::AdjustmentKind::Mask:
            return includeMasks;
        default:
            return false;
        }
    };

    const drift::Project before = m_project;
    // Removing adjustments and lanes shifts both clip and track indices, so the selection is
    // carried across by clip id; a selected adjustment that is removed simply drops out of it.
    const auto clipIdAt = [this](int t, int c) {
        return isValidClipIndex(t, c) ? m_project.tracks().at(t).clips.at(c).id : QString();
    };
    QStringList selectedIds;
    for (const QPair<int, int> &pair : std::as_const(m_selection))
        selectedIds.append(clipIdAt(pair.first, pair.second));
    const QString primaryId = clipIdAt(m_selectedTrack, m_selectedClip);
    const QString transitionTrackId = trackIdAt(m_selectedTransitionTrack);

    int removed = 0;
    bool removedVideo = false;
    bool removedAudio = false;
    QSet<QString> touchedLaneIds;
    for (drift::Track &track : m_project.tracks()) {
        if (!track.isAdjustment())
            continue;
        for (int c = track.clips.size() - 1; c >= 0; --c) {
            const drift::Clip &clip = track.clips.at(c);
            if (clip.type != drift::ClipType::Adjustment || clip.linkedClipId != hostId || !removable(clip))
                continue;
            removedVideo = removedVideo || clip.adjustmentKind == drift::AdjustmentKind::VideoEffects;
            removedAudio = removedAudio || clip.adjustmentKind == drift::AdjustmentKind::AudioEffects;
            track.clips.removeAt(c);
            touchedLaneIds.insert(track.id);
            ++removed;
        }
    }
    if (removed == 0)
        return 0;

    // A nested lane emptied here existed only to hold these effects.
    for (int t = m_project.tracks().size() - 1; t >= 0; --t) {
        const drift::Track &track = m_project.tracks().at(t);
        if (track.isAdjustmentLane() && track.clips.isEmpty() && !track.id.isEmpty()
            && touchedLaneIds.contains(track.id)) {
            m_project.tracks().removeAt(t);
        }
    }

    const auto locate = [this](const QString &id) {
        int t = -1;
        int c = -1;
        return findClipById(m_project, id, &t, &c) ? qMakePair(t, c) : qMakePair(-1, -1);
    };
    m_selection.clear();
    for (const QString &id : std::as_const(selectedIds)) {
        const QPair<int, int> at = id.isEmpty() ? qMakePair(-1, -1) : locate(id);
        if (at.first >= 0)
            m_selection.append(at);
    }
    const QPair<int, int> primary = primaryId.isEmpty() ? qMakePair(-1, -1) : locate(primaryId);
    if (primary.first >= 0) {
        m_selectedTrack = primary.first;
        m_selectedClip = primary.second;
    } else if (!m_selection.isEmpty()) {
        m_selectedTrack = m_selection.constFirst().first;
        m_selectedClip = m_selection.constFirst().second;
    } else {
        m_selectedTrack = -1;
        m_selectedClip = -1;
    }
    m_selectedTransitionTrack = m_project.trackIndexById(transitionTrackId);

    // The graph's hidden entries address effects by stack index; with the stack gone they would
    // hide parameters of whatever effect lands in that slot next.
    dropKeyframeGraphEffectProperties(removedVideo, removedAudio);
    pushProjectEdit(before, tr("Remove all effects"));
    finishEdit(tr("All effects removed"));
    return removed;
}

void AppController::setEffectEnabled(int trackIndex, int clipIndex, int effectIndex, bool enabled)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;
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
    if (clip.effects[effectIndex].enabled == enabled)
        return;

    const drift::Project before = m_project;
    clip.effects[effectIndex].enabled = enabled;
    pushProjectEdit(before, enabled ? tr("Enable effect")
                                    : tr("Disable effect"));
    finishEdit(enabled ? tr("Effect enabled") : tr("Effect disabled"));
}

void AppController::moveEffect(int trackIndex, int clipIndex, int fromIndex, int toIndex)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;
    // The stack lives on the adjustment linked to this clip, not on the clip.
    if (!redirectToEffectHost(&trackIndex, &clipIndex, drift::AdjustmentKind::VideoEffects,
                              /*create=*/false)) {
        return;
    }

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    drift::Clip &clip = track.clips[clipIndex];
    if (fromIndex < 0 || fromIndex >= clip.effects.size())
        return;
    toIndex = qBound(0, toIndex, clip.effects.size() - 1);
    if (fromIndex == toIndex)
        return;

    const drift::Project before = m_project;
    clip.effects.move(fromIndex, toIndex);
    remapKeyframeGraphPropertiesForEffectMove(fromIndex, toIndex);
    pushProjectEdit(before, tr("Reorder effect"));
    finishEdit(tr("Effect reordered"));
}

bool AppController::setEffectParam(int trackIndex, int clipIndex, int effectIndex, const QString &key,
                                   double value)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return false;
    // The stack lives on the adjustment linked to this clip, not on the clip.
    if (!redirectToEffectHost(&trackIndex, &clipIndex, drift::AdjustmentKind::VideoEffects,
                              /*create=*/false)) {
        return false;
    }

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return false;

    drift::Clip &clip = track.clips[clipIndex];
    if (effectIndex < 0 || effectIndex >= clip.effects.size())
        return false;

    const EffectPresetEntry *def = effectDefForId(clip.effects[effectIndex].catalogId);
    if (!def || !declaresParam(def->meta, key))
        return false;

    bool asBoolean = false;
    for (const drift::EffectParamSpec &param : def->meta.parameters) {
        if (param.key == key) {
            asBoolean = param.isBoolean();
            break;
        }
    }
    const QVariant newValue = asBoolean ? QVariant(value > 0.5) : QVariant(value);
    // Already set: succeed without an edit, so a batch repeating it leaves no empty undo step.
    if (clip.effects[effectIndex].parameters.value(key) == newValue)
        return true;

    const drift::Project before = m_project;
    clip.effects[effectIndex].parameters.insert(key, newValue);
    pushProjectEdit(before, tr("Edit effect"));
    finishEdit(tr("Effect updated"));
    return true;
}

bool AppController::setEffectVec2Param(int trackIndex, int clipIndex, int effectIndex,
                                       const QString &key, double x, double y)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return false;
    if (!redirectToEffectHost(&trackIndex, &clipIndex, drift::AdjustmentKind::VideoEffects,
                              /*create=*/false)) {
        return false;
    }
    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return false;
    drift::Clip &clip = track.clips[clipIndex];
    if (effectIndex < 0 || effectIndex >= clip.effects.size())
        return false;
    const EffectPresetEntry *def = effectDefForId(clip.effects[effectIndex].catalogId);
    if (!def)
        return false;
    const auto specIt = std::find_if(def->meta.parameters.cbegin(), def->meta.parameters.cend(),
                                     [&](const drift::EffectParamSpec &p) { return p.key == key; });
    if (specIt == def->meta.parameters.cend() || !specIt->isVec2())
        return false;

    const drift::Project before = m_project;
    clip.effects[effectIndex].parameters.insert(key + QLatin1String(".x"),
                                                std::clamp(x, specIt->min, specIt->max));
    clip.effects[effectIndex].parameters.insert(key + QLatin1String(".y"),
                                                std::clamp(y, specIt->min, specIt->max));
    pushProjectEdit(before, tr("Edit effect"));
    finishEdit(tr("Effect updated"));
    return true;
}

void AppController::previewSetEffectVec2Param(int trackIndex, int clipIndex, int effectIndex,
                                              const QString &key, double x, double y)
{
    previewSetEffectParam(trackIndex, clipIndex, effectIndex, key + QLatin1String(".x"), x);
    previewSetEffectParam(trackIndex, clipIndex, effectIndex, key + QLatin1String(".y"), y);
}

void AppController::previewSetEffectTextParam(int trackIndex, int clipIndex, int effectIndex,
                                              const QString &key, const QString &value)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
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
    if (effectIndex < 0 || effectIndex >= clip.effects.size())
        return;
    if (key.isEmpty())
        return;

    beginImplicitPreviewDrag(tr("Edit effect"));
    clip.effects[effectIndex].parameters.insert(key, value);
    emitPreviewEdit(announceTrack, announceClip,
                    {QStringLiteral("fx.%1.%2").arg(effectIndex).arg(key)});
}

// Colour params take this path rather than widening setEffectParam, which every existing QML call
// site passes a double to. There is no preview variant on purpose: a swatch commits once, so there
// is no drag stream to coalesce the way a slider needs.
bool AppController::setEffectColorParam(int trackIndex, int clipIndex, int effectIndex,
                                        const QString &key, const QString &value)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return false;
    // The stack lives on the adjustment linked to this clip, not on the clip.
    if (!redirectToEffectHost(&trackIndex, &clipIndex, drift::AdjustmentKind::VideoEffects,
                              /*create=*/false)) {
        return false;
    }

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return false;

    drift::Clip &clip = track.clips[clipIndex];
    if (effectIndex < 0 || effectIndex >= clip.effects.size())
        return false;

    const EffectPresetEntry *def = effectDefForId(clip.effects[effectIndex].catalogId);
    if (!def)
        return false;
    const auto specIt = std::find_if(def->meta.parameters.cbegin(), def->meta.parameters.cend(),
                                     [&](const drift::EffectParamSpec &p) { return p.key == key; });
    if (specIt == def->meta.parameters.cend() || !specIt->isColor())
        return false;

    // Normalized to the same six-digit form the catalog default carries, so what lands in the
    // project matches what the parser would have produced.
    // An alpha param stores #rrggbbaa, and an eight-digit value is always read that way (Qt
    // would read it as #aarrggbb). A six-digit pick keeps the opacity already stored.
    QString stored;
    if (specIt->alpha && value.size() == 9 && value.startsWith(QLatin1Char('#'))) {
        const QColor rgb(value.left(7));
        bool alphaOk = false;
        value.right(2).toInt(&alphaOk, 16);
        if (!rgb.isValid() || !alphaOk)
            return false;
        stored = rgb.name(QColor::HexRgb) + value.right(2).toLower();
    } else {
        const QColor color(value);
        if (!color.isValid())
            return false;
        stored = color.name(QColor::HexRgb);
        if (specIt->alpha) {
            const QString old = clip.effects[effectIndex].parameters.value(key).toString();
            stored += old.size() == 9 ? old.right(2) : QStringLiteral("ff");
        }
    }

    const QVariantMap &params = clip.effects[effectIndex].parameters;
    if (params.value(key).toString() == stored
        && (specIt->enables.isEmpty() || params.value(specIt->enables).toBool()))
        return true; // already set: no edit, so a repeating batch leaves no empty undo step

    const drift::Project before = m_project;
    clip.effects[effectIndex].parameters.insert(key, stored);
    // One undo step: a picked shade that left its "custom colour" switch off would change nothing.
    if (!specIt->enables.isEmpty())
        clip.effects[effectIndex].parameters.insert(specIt->enables, true);
    pushProjectEdit(before, tr("Edit effect"));
    finishEdit(tr("Effect updated"));
    return true;
}

bool AppController::setEffectClipParam(int trackIndex, int clipIndex, int effectIndex,
                                       const QString &key, const QString &clipId)
{
    if (!redirectToEffectHost(&trackIndex, &clipIndex, drift::AdjustmentKind::VideoEffects,
                              /*create=*/false)) {
        return false;
    }
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return false;
    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return false;
    drift::Clip &clip = track.clips[clipIndex];
    if (effectIndex < 0 || effectIndex >= clip.effects.size())
        return false;
    const EffectPresetEntry *def = effectDefForId(clip.effects[effectIndex].catalogId);
    if (!def)
        return false;
    const auto spec = std::find_if(def->meta.parameters.cbegin(), def->meta.parameters.cend(),
                                   [&](const drift::EffectParamSpec &p) { return p.key == key; });
    if (spec == def->meta.parameters.cend() || !spec->isClip())
        return false;
    if (clip.effects[effectIndex].parameters.value(key).toString() == clipId)
        return true;

    const drift::Project before = m_project;
    clip.effects[effectIndex].parameters.insert(key, clipId);
    pushProjectEdit(before, tr("Edit effect"));
    finishEdit(tr("Effect updated"));
    return true;
}
namespace {


QString clipDisplayName(const drift::Clip &clip)
{
    if (!clip.name.isEmpty())
        return clip.name;
    return QFileInfo(clip.path).completeBaseName();
}

} // namespace

QVariantList AppController::effectClipCandidates(int trackIndex, int clipIndex) const
{
    QVariantList out;
    const drift::ClipRef host = sourceClipRef(trackIndex, clipIndex);
    if (host.trackIndex < 0 || host.trackIndex >= m_project.tracks().size())
        return out;
    const drift::Clip &self = m_project.tracks().at(host.trackIndex).clips.at(host.clipIndex);
    const drift::TimeUs start = self.timelineStart;
    const drift::TimeUs end = self.timelineEnd();

    // Track 0 is topmost, so "beneath" is every higher index, nearest first.
    const QList<drift::Track> &tracks = m_project.tracks();
    for (int t = host.trackIndex + 1; t < tracks.size(); ++t) {
        const drift::Track &track = tracks.at(t);
        if (track.hidden || track.type == drift::TrackType::Audio || track.isAdjustmentLane())
            continue;
        for (int c = 0; c < track.clips.size(); ++c) {
            const drift::Clip &clip = track.clips.at(c);
            if (clip.type != drift::ClipType::Video && clip.type != drift::ClipType::Image)
                continue;
            if (clip.timelineEnd() <= start || clip.timelineStart >= end)
                continue;
            out.append(QVariantMap{{QStringLiteral("id"), clip.id},
                                   {QStringLiteral("name"), clipDisplayName(clip)},
                                   {QStringLiteral("track"), t},
                                   {QStringLiteral("clip"), c},
                                   {QStringLiteral("hasDepth"), !clip.depthPath.isEmpty()}});
        }
    }
    return out;
}

QVariantMap AppController::effectClipTarget(int trackIndex, int clipIndex, int effectIndex,
                                            const QString &key) const
{
    const drift::ClipRef host = sourceClipRef(trackIndex, clipIndex);
    if (host.trackIndex < 0 || host.trackIndex >= m_project.tracks().size())
        return {};
    const drift::Clip *stack =
        effectHostClip(host.trackIndex, host.clipIndex, drift::AdjustmentKind::VideoEffects);
    if (!stack)
        stack = &m_project.tracks().at(host.trackIndex).clips.at(host.clipIndex);
    if (effectIndex < 0 || effectIndex >= stack->effects.size())
        return {};
    const QString chosen = stack->effects.at(effectIndex).parameters.value(key).toString();

    const auto describe = [&](int t, int c, bool isExplicit) {
        const drift::Clip &clip = m_project.tracks().at(t).clips.at(c);
        return QVariantMap{{QStringLiteral("explicit"), isExplicit},
                           {QStringLiteral("id"), clip.id},
                           {QStringLiteral("name"), clipDisplayName(clip)},
                           {QStringLiteral("track"), t},
                           {QStringLiteral("clip"), c},
                           {QStringLiteral("hasDepth"), !clip.depthPath.isEmpty()}};
    };

    // A chosen clip that no longer exists (deleted, or split into new ids) falls back to
    // automatic, exactly as the renderer does.
    int t = -1;
    int c = -1;
    if (!chosen.isEmpty() && findClipById(m_project, chosen, &t, &c))
        return describe(t, c, true);

    // Automatic: the nearest clip beneath at the playhead, or at the start when the playhead is
    // outside this clip.
    const drift::Clip &self = m_project.tracks().at(host.trackIndex).clips.at(host.clipIndex);
    const drift::TimeUs at = self.containsTime(m_playheadUs) ? m_playheadUs : self.timelineStart;
    const QVariantList candidates = effectClipCandidates(host.trackIndex, host.clipIndex);
    for (const QVariant &v : candidates) {
        const QVariantMap m = v.toMap();
        const drift::Clip &clip =
            m_project.tracks().at(m.value(QStringLiteral("track")).toInt())
                .clips.at(m.value(QStringLiteral("clip")).toInt());
        if (clip.containsTime(at))
            return describe(m.value(QStringLiteral("track")).toInt(),
                            m.value(QStringLiteral("clip")).toInt(), false);
    }
    return {};
}

bool AppController::setEffectTextParam(int trackIndex, int clipIndex, int effectIndex,
                                      const QString &key, const QString &value)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return false;
    if (!redirectToEffectHost(&trackIndex, &clipIndex, drift::AdjustmentKind::VideoEffects,
                              /*create=*/false)) {
        return false;
    }

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return false;

    drift::Clip &clip = track.clips[clipIndex];
    if (effectIndex < 0 || effectIndex >= clip.effects.size())
        return false;

    const EffectPresetEntry *def = effectDefForId(clip.effects[effectIndex].catalogId);
    if (!def)
        return false;
    const auto specIt = std::find_if(def->meta.parameters.cbegin(), def->meta.parameters.cend(),
                                     [&](const drift::EffectParamSpec &p) { return p.key == key; });
    if (specIt == def->meta.parameters.cend()
        || (specIt->type != drift::EffectParamType::Curve && specIt->type != drift::EffectParamType::Gradient
            && specIt->type != drift::EffectParamType::Lut)) {
        return false;
    }
    if (specIt->type == drift::EffectParamType::Curve) {
        QJsonParseError parseError;
        const QJsonDocument keys = QJsonDocument::fromJson(value.toUtf8(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !keys.isArray())
            return false;
    }

    const QString undoText = specIt->type == drift::EffectParamType::Lut     ? tr("Change LUT")
                             : specIt->type == drift::EffectParamType::Curve ? tr("Edit curve")
                                                                             : tr("Edit gradient");
    const drift::Project before = m_project;
    clip.effects[effectIndex].parameters.insert(key, value);
    pushProjectEdit(before, undoText);
    finishEdit(tr("Effect updated"));
    return true;
}

bool AppController::setEffectStringParam(int trackIndex, int clipIndex, int effectIndex,
                                         const QString &key, const QUrl &url)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return false;
    // The stack lives on the adjustment linked to this clip, not on the clip.
    if (!redirectToEffectHost(&trackIndex, &clipIndex, drift::AdjustmentKind::VideoEffects,
                              /*create=*/false)) {
        return false;
    }

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return false;

    drift::Clip &clip = track.clips[clipIndex];
    if (effectIndex < 0 || effectIndex >= clip.effects.size())
        return false;

    const EffectPresetEntry *def = effectDefForId(clip.effects[effectIndex].catalogId);
    if (!def)
        return false;
    const auto specIt = std::find_if(def->meta.parameters.cbegin(), def->meta.parameters.cend(),
                                     [&](const drift::EffectParamSpec &p) { return p.key == key; });
    if (specIt == def->meta.parameters.cend() || !specIt->isFilePath())
        return false;

    // Empty URL clears the path (the inspector's clear button). Anything else must resolve to a
    // local file — portal picks, SAF content:// URIs, and plain file:// all come through as QUrl.
    // cgltf / the rest-mesh loader fopen the path, so a content:// URI is copied into app storage
    // first rather than stored as-is.
    QString path;
    if (!url.isEmpty()) {
        if (url.isLocalFile()) {
            path = url.toLocalFile();
#ifdef Q_OS_ANDROID
        } else if (AndroidUri::isContentUri(url)) {
            // cgltf / the rest-mesh loader fopen the path; Qt can QFile a SAF URI but
            // those loaders cannot, so copy into app storage once.
            path = materializeContentUrl(url);
#endif
        } else {
            path = url.toString(QUrl::PreferLocalFile);
        }
        if (path.isEmpty())
            return false;
    }

    const drift::Project before = m_project;
    clip.effects[effectIndex].parameters.insert(key, path);
    pushProjectEdit(before, QStringLiteral("Edit effect"));
    finishEdit(QStringLiteral("Effect updated"));

    if (def->isFaceSwap && key == QLatin1String("sourceImage"))
        m_faces->ingestSwapSource(path);
    return true;
}

QVariantList AppController::facePropLibrary() const
{
    QVariantList out;
    for (const FacePropEntry &entry : facePropsSnapshot()) {
        out.append(QVariantMap{
            {QStringLiteral("id"), entry.id},
            {QStringLiteral("name"), entry.label},
            {QStringLiteral("thumbnailPath"), entry.thumbnailPath},
            {QStringLiteral("description"), entry.description},
            {QStringLiteral("license"), entry.license},
            {QStringLiteral("modelPath"), entry.path},
            {QStringLiteral("removable"), entry.userInstalled},
        });
    }
    return out;
}

QVariantMap AppController::importFaceProps(const QUrl &url)
{
    QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
#ifdef Q_OS_ANDROID
    // The zip reader seeks around the archive, so a SAF document is copied to a real file first.
    QTemporaryFile copy(QDir::temp().filePath(QStringLiteral("drift-face-props-XXXXXX.zip")));
    if (AndroidUri::isContentUri(url)) {
        std::unique_ptr<QFile> src = AndroidUri::openForRead(url);
        if (!src || !copy.open()) {
            setLastMessage(tr("Could not read the selected file"), QStringLiteral("error"));
            return {};
        }
        for (;;) {
            const QByteArray chunk = src->read(1 << 20);
            if (chunk.isEmpty())
                break;
            if (copy.write(chunk) != chunk.size()) {
                setLastMessage(tr("Could not read the selected file"), QStringLiteral("error"));
                return {};
            }
        }
        copy.close();
        path = copy.fileName();
    }
#endif
    if (path.isEmpty())
        return {};

    const FacePropImportResult result = QFileInfo(path).isDir()
        ? importFacePropsFromDirectory(path, userFacePropsDir())
        : importFacePropsFromZip(path, userFacePropsDir());

    const int installed = int(result.installedIds.size());
    if (installed > 0 && result.errors.isEmpty())
        setLastMessage(tr("Imported %n face prop(s)", "", installed), QStringLiteral("success"));
    else if (installed > 0)
        setLastMessage(tr("Imported %n face prop(s); %1 skipped: %2", "", installed)
                           .arg(result.errors.size())
                           .arg(result.errors.first()),
                       QStringLiteral("warning"));
    else
        setLastMessage(result.errors.isEmpty() ? tr("No face props were imported") : result.errors.first(),
                       QStringLiteral("error"));

    if (installed > 0) {
        reloadFacePropCatalog();
        emit facePropsChanged();
        // A re-import replaces files an effect may already be drawing.
        emitPreviewFrame();
    }
    return {
        {QStringLiteral("installed"), result.installedIds},
        {QStringLiteral("errors"), result.errors},
    };
}

bool AppController::removeFaceProp(const QString &propId)
{
    const QList<FacePropEntry> props = facePropsSnapshot();
    const auto prop = std::find_if(props.cbegin(), props.cend(),
                                   [&](const FacePropEntry &e) { return e.id == propId; });
    int sharingDir = 0;
    if (prop != props.cend()) {
        for (const FacePropEntry &e : props)
            sharingDir += !e.dir.isEmpty() && e.dir == prop->dir;
    }
    QString error;
    // Designs expanded out of one prop.json share a folder. Deleting one would delete the rest.
    if (sharingDir > 1) {
        setLastMessage(tr("That style shares its folder with other styles"), QStringLiteral("error"));
        return false;
    }
    if (prop == props.cend() || !prop->userInstalled
        || !removeUserFaceProp(QFileInfo(prop->dir).fileName(), &error, userFacePropsDir())) {
        setLastMessage(error.isEmpty() ? tr("Could not delete the face prop") : error,
                       QStringLiteral("error"));
        return false;
    }
    reloadFacePropCatalog();
    emit facePropsChanged();
    setLastMessage(tr("Face prop deleted"), QStringLiteral("success"));
    emitPreviewFrame();
    return true;
}

bool AppController::addFaceProp(int trackIndex, int clipIndex, const QString &propId)
{
    const int steps = m_undoStack.count();
    m_undoStack.beginMacro(tr("Add face prop"));
    addEffect(trackIndex, clipIndex, QStringLiteral("face_props"));
    bool ok = false;
    // addEffect leaves the adjustment carrying the stack selected, with the new effect last.
    if (m_selectedTrack >= 0 && m_selectedTrack < m_project.tracks().size()) {
        const auto &clips = m_project.tracks().at(m_selectedTrack).clips;
        if (m_selectedClip >= 0 && m_selectedClip < clips.size() && !clips.at(m_selectedClip).effects.isEmpty())
            ok = applyFaceProp(m_selectedTrack, m_selectedClip,
                               int(clips.at(m_selectedClip).effects.size()) - 1, propId);
    }
    m_undoStack.endMacro();
    // An empty macro still lands on the stack; a failed add must not leave a no-op undo entry.
    if (!ok && m_undoStack.count() > steps)
        m_undoStack.undo();
    return ok;
}

bool AppController::applyFaceProp(int trackIndex, int clipIndex, int effectIndex,
                                  const QString &propId)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return false;
    // The stack lives on the adjustment linked to this clip, not on the clip.
    if (!redirectToEffectHost(&trackIndex, &clipIndex, drift::AdjustmentKind::VideoEffects,
                              /*create=*/false)) {
        return false;
    }

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return false;

    drift::Clip &clip = track.clips[clipIndex];
    if (effectIndex < 0 || effectIndex >= clip.effects.size())
        return false;
    if (clip.effects[effectIndex].catalogId != QLatin1String("face_props"))
        return false;
    const EffectPresetEntry *def = effectDefForId(clip.effects[effectIndex].catalogId);
    if (!def)
        return false;

    const QList<FacePropEntry> props = facePropsSnapshot();
    const auto prop = std::find_if(props.cbegin(), props.cend(),
                                   [&](const FacePropEntry &e) { return e.id == propId; });
    if (prop == props.cend())
        return false;

    // What a prop is fitted with. A key the prop leaves out goes back to the package default
    // rather than keeping the previous prop's placement.
    static const QStringList placementKeys{
        QStringLiteral("scale"),         QStringLiteral("offsetX"),
        QStringLiteral("offsetY"),       QStringLiteral("offsetZ"),
        QStringLiteral("rotX"),          QStringLiteral("rotY"),
        QStringLiteral("rotZ"),          QStringLiteral("occlusion"),
        QStringLiteral("occlusionSize"), QStringLiteral("occlusionOffset"),
        QStringLiteral("occlusionDepth"),
    };

    const drift::Project before = m_project;
    drift::Effect &effect = clip.effects[effectIndex];
    effect.parameters.insert(QStringLiteral("model"), prop->path);
    for (const drift::EffectParamSpec &spec : def->meta.parameters) {
        if (!placementKeys.contains(spec.key))
            continue;
        const QVariant value = prop->params.value(spec.key, spec.defaultVariant());
        if (spec.isBoolean())
            effect.parameters.insert(spec.key, value.toBool());
        else
            effect.parameters.insert(spec.key, std::clamp(value.toDouble(), spec.min, spec.max));
        // Keys would override the placement just set, so the prop would not appear to apply.
        effect.paramKeyframes.remove(spec.key);
    }
    pushProjectEdit(before, tr("Apply face prop"));
    finishEdit(tr("Face prop applied"));
    return true;
}

QVariantList AppController::audioEffectCatalog() const
{
    QVariantList out;
    for (const AudioEffectEntry &def : ::audioEffectCatalog()) {
        QVariantList params;
        for (const drift::EffectParamSpec &p : def.parameters) {
            params.append(QVariantMap{
                {QStringLiteral("key"), p.key},
                {QStringLiteral("label"), p.label},
                {QStringLiteral("min"), p.min},
                {QStringLiteral("max"), p.max},
                {QStringLiteral("default"), p.defaultValue},
                {QStringLiteral("isBoolean"), p.isBoolean()},
                {QStringLiteral("type"), p.typeName()},
            });
        }
        out.append(QVariantMap{
            {QStringLiteral("id"), def.id},
            {QStringLiteral("label"), def.displayName},
            {QStringLiteral("displayName"), def.displayName},
            {QStringLiteral("category"), def.category},
            {QStringLiteral("categoryLabel"), audioEffectCategoryLabel(def.category)},
            {QStringLiteral("icon"), def.icon},
            {QStringLiteral("thumbnailPath"), def.thumbnailPath},
            {QStringLiteral("user"), isUserPackageDir(def.packageDir, QStringLiteral("audio-effects"))},
            {QStringLiteral("params"), params},
        });
    }
    return out;
}

QVariantList AppController::audioEffectCategories() const
{
    QVariantList out;
    for (const auto &entry : ::audioEffectCategories()) {
        out.append(QVariantMap{
            {QStringLiteral("id"), entry.first},
            {QStringLiteral("label"), entry.second},
        });
    }
    return out;
}

void AppController::addAudioEffect(int trackIndex, int clipIndex, const QString &effectId)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    const AudioEffectEntry *def = audioEffectDefForId(effectId);
    if (!def)
        return;

    const drift::Effect effect = audioEffectFromCatalogEntry(*def, {});

    // The parent track is about to be addressed by id, so it needs one before the snapshot.
    m_project.ensureTrackIds();
    // Snapshot first: creating the lane is part of the edit, so undo must take it back out
    // along with the effect rather than leaving an empty row behind.
    const drift::Project before = m_project;

    int hostTrack = trackIndex;
    int hostClip = clipIndex;
    if (!redirectToEffectHost(&hostTrack, &hostClip, drift::AdjustmentKind::AudioEffects,
                              /*create=*/true)) {
        return;
    }
    m_project.tracks()[hostTrack].clips[hostClip].audioEffects.append(effect);

    // Selection moves to the adjustment carrying the stack — the Audio FX tab is no longer
    // offered for the clip the effect was aimed at, so nothing else would show it.
    // redirectToEffectHost already returns post-insert indices, and normalizeProjectStructure
    // carries the selection across any reorder by track id.
    m_selectedTrack = hostTrack;
    m_selectedClip = hostClip;
    m_selection = {qMakePair(hostTrack, hostClip)};

    pushProjectEdit(before, tr("Add audio effect"));
    finishEdit(tr("Audio effect added"));
}

void AppController::removeAudioEffect(int trackIndex, int clipIndex, int effectIndex)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;
    // The stack lives on the adjustment linked to this clip, not on the clip.
    if (!redirectToEffectHost(&trackIndex, &clipIndex, drift::AdjustmentKind::AudioEffects,
                              /*create=*/false)) {
        return;
    }

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    drift::Clip &clip = track.clips[clipIndex];
    if (effectIndex < 0 || effectIndex >= clip.audioEffects.size())
        return;

    const drift::Project before = m_project;
    clip.audioEffects.removeAt(effectIndex);
    dropKeyframeGraphPropertiesForEffect(effectIndex, /*audio=*/true);
    pushProjectEdit(before, tr("Remove audio effect"));
    finishEdit(tr("Audio effect removed"));
}

void AppController::setAudioEffectEnabled(int trackIndex, int clipIndex, int effectIndex, bool enabled)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;
    // The stack lives on the adjustment linked to this clip, not on the clip.
    if (!redirectToEffectHost(&trackIndex, &clipIndex, drift::AdjustmentKind::AudioEffects,
                              /*create=*/false)) {
        return;
    }

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    drift::Clip &clip = track.clips[clipIndex];
    if (effectIndex < 0 || effectIndex >= clip.audioEffects.size())
        return;
    if (clip.audioEffects[effectIndex].enabled == enabled)
        return;

    const drift::Project before = m_project;
    clip.audioEffects[effectIndex].enabled = enabled;
    pushProjectEdit(before, enabled ? tr("Enable audio effect")
                                    : tr("Disable audio effect"));
    finishEdit(enabled ? tr("Audio effect enabled")
                       : tr("Audio effect disabled"));
}

void AppController::moveAudioEffect(int trackIndex, int clipIndex, int fromIndex, int toIndex)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;
    // The stack lives on the adjustment linked to this clip, not on the clip.
    if (!redirectToEffectHost(&trackIndex, &clipIndex, drift::AdjustmentKind::AudioEffects,
                              /*create=*/false)) {
        return;
    }

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    drift::Clip &clip = track.clips[clipIndex];
    if (fromIndex < 0 || fromIndex >= clip.audioEffects.size())
        return;
    toIndex = qBound(0, toIndex, clip.audioEffects.size() - 1);
    if (fromIndex == toIndex)
        return;

    const drift::Project before = m_project;
    clip.audioEffects.move(fromIndex, toIndex);
    remapKeyframeGraphPropertiesForEffectMove(fromIndex, toIndex, /*audio=*/true);
    pushProjectEdit(before, tr("Reorder audio effect"));
    finishEdit(tr("Audio effect reordered"));
}

void AppController::previewSetAudioEffectParam(int trackIndex, int clipIndex, int effectIndex,
                                               const QString &key, double value)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;
    const int announceTrack = trackIndex;
    const int announceClip = clipIndex;
    // The stack lives on the adjustment linked to this clip, not on the clip.
    if (!redirectToEffectHost(&trackIndex, &clipIndex, drift::AdjustmentKind::AudioEffects,
                              /*create=*/false)) {
        return;
    }

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    drift::Clip &clip = track.clips[clipIndex];
    if (effectIndex < 0 || effectIndex >= clip.audioEffects.size())
        return;
    if (key.isEmpty())
        return;

    beginImplicitPreviewDrag(tr("Edit audio effect"));

    clip.audioEffects[effectIndex].parameters.insert(key, value);
    // Audio effects are heard, not seen: a preview frame won't reflect the change, but keeping the
    // project mutated live means the next playback buffer picks it up without a commit.
    emitPreviewEdit(announceTrack, announceClip,
                    {QStringLiteral("afx.%1.%2").arg(effectIndex).arg(key)});
}

bool AppController::setAudioEffectParam(int trackIndex, int clipIndex, int effectIndex,
                                        const QString &key, double value)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return false;
    // The stack lives on the adjustment linked to this clip, not on the clip.
    if (!redirectToEffectHost(&trackIndex, &clipIndex, drift::AdjustmentKind::AudioEffects,
                              /*create=*/false)) {
        return false;
    }

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return false;

    drift::Clip &clip = track.clips[clipIndex];
    if (effectIndex < 0 || effectIndex >= clip.audioEffects.size())
        return false;

    const AudioEffectEntry *def = audioEffectDefForId(clip.audioEffects[effectIndex].catalogId);
    if (!def || !declaresParam(def->parameters, key))
        return false;

    // Already set: succeed without an edit, so a batch repeating it leaves no empty undo step.
    if (clip.audioEffects[effectIndex].parameters.value(key) == QVariant(value))
        return true;

    const drift::Project before = m_project;
    clip.audioEffects[effectIndex].parameters.insert(key, value);
    pushProjectEdit(before, tr("Edit audio effect"));
    finishEdit(tr("Audio effect updated"));
    return true;
}

// --- effect stacks: copy/paste and user presets ------------------------------

drift::EffectStackPreset AppController::effectStackFor(int trackIndex, int clipIndex,
                                                       int effectIndex, int audioEffectIndex) const
{
    drift::EffectStackPreset stack;
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return stack;

    const drift::Track &track = m_project.tracks().at(trackIndex);
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return stack;

    const drift::Clip &clip = track.clips.at(clipIndex);
    stack.label = clip.name;
    // Keyframes on either half are stored against this duration, and rescaled onto the target's.
    stack.sourceDurationUs = clip.timelineDuration;

    // The stacks live on the adjustments linked to this clip. Indices match what the inspector
    // shows, because a clip has at most one linked adjustment per kind.
    const drift::Clip *videoHost =
        effectHostClip(trackIndex, clipIndex, drift::AdjustmentKind::VideoEffects);
    const drift::Clip *audioHost =
        effectHostClip(trackIndex, clipIndex, drift::AdjustmentKind::AudioEffects);
    const QList<drift::Effect> videoEffects = videoHost ? videoHost->effects : clip.effects;
    const QList<drift::Effect> audioEffects = audioHost ? audioHost->audioEffects
                                                        : clip.audioEffects;

    // Both indices unset means the whole clip; otherwise exactly one effect, on its own side.
    const bool wholeClip = effectIndex < 0 && audioEffectIndex < 0;
    if (wholeClip) {
        stack.effects = videoEffects;
        stack.audioEffects = audioEffects;
        return stack;
    }
    if (effectIndex >= 0 && effectIndex < videoEffects.size())
        stack.effects.append(videoEffects.at(effectIndex));
    if (audioEffectIndex >= 0 && audioEffectIndex < audioEffects.size())
        stack.audioEffects.append(audioEffects.at(audioEffectIndex));
    return stack;
}

void AppController::applyEffectStack(int trackIndex, int clipIndex,
                                     const drift::EffectStackPreset &stack,
                                     const QString &undoLabel)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    if (clipIndex < 0 || clipIndex >= m_project.tracks().at(trackIndex).clips.size())
        return;
    if (stack.isEmpty())
        return;

    // Read-only for now: the snapshot below has to be taken before anything mutable is held, or
    // copying the project re-shares the container this reference points into and the append lands
    // in the undo snapshot too.
    const drift::Clip &source = m_project.tracks().at(trackIndex).clips.at(clipIndex);
    const drift::TimeUs targetDurationUs = source.timelineDuration;
    // Audio effects only run in the mixer, and the audio inspector is hidden for clips with no
    // audio, so pasting them onto an image or a title would leave them invisible and inert.
    const bool clipHasAudio =
        source.type == drift::ClipType::Video || source.type == drift::ClipType::Audio;

    QStringList missing;
    QList<drift::Effect> video;
    video.reserve(stack.effects.size());
    for (const drift::Effect &incoming : stack.effects) {
        // Rebuilding from the catalog re-derives the filter name and the manifest's fixed params,
        // so a preset written against an older build of an effect applies as that effect is
        // today, carrying across only what the user actually set.
        if (const EffectPresetEntry *def = effectDefForId(incoming.catalogId)) {
            drift::Effect effect = effectFromCatalogEntry(*def, incoming.parameters);
            effect.paramKeyframes = incoming.paramKeyframes;
            effect.enabled = incoming.enabled;
            video.append(effect);
        } else {
            // Uninstalled addon: kept, exactly as project load keeps it, so the stack survives the
            // round-trip and starts working the moment the pack is installed.
            video.append(incoming);
            if (!incoming.catalogId.isEmpty())
                missing.append(incoming.catalogId);
        }
    }

    QList<drift::Effect> audio;
    if (clipHasAudio) {
        audio.reserve(stack.audioEffects.size());
        for (const drift::Effect &incoming : stack.audioEffects) {
            if (const AudioEffectEntry *def = audioEffectDefForId(incoming.catalogId)) {
                drift::Effect effect = audioEffectFromCatalogEntry(*def, incoming.parameters);
                effect.paramKeyframes = incoming.paramKeyframes;
                effect.enabled = incoming.enabled;
                audio.append(effect);
            } else {
                audio.append(incoming);
                if (!incoming.catalogId.isEmpty())
                    missing.append(incoming.catalogId);
            }
        }
    }

    drift::rescaleEffectKeyframes(video, stack.sourceDurationUs, targetDurationUs);
    drift::rescaleEffectKeyframes(audio, stack.sourceDurationUs, targetDurationUs);

    m_project.ensureTrackIds();
    const QString targetTrackId = m_project.tracks().at(trackIndex).id;
    const QString targetClipId = m_project.tracks().at(trackIndex).clips.at(clipIndex).id;

    const drift::Project before = m_project;

    // Append, never replace. Appending is also what lets the keyframe graph's hidden-property set
    // stand: every existing "fx.<n>.<key>" still addresses the effect it did before. Each half
    // goes to the adjustment linked to this clip for that kind, minted here if there is none.
    // Where the selection lands afterwards: the stack is only visible in the inspector of the
    // adjustment holding it, and the video half is the one the user is usually after.
    drift::ClipRef selectHost;
    if (!video.isEmpty()) {
        int hostTrack = trackIndex;
        int hostClip = clipIndex;
        if (redirectToEffectHost(&hostTrack, &hostClip, drift::AdjustmentKind::VideoEffects,
                                 /*create=*/true)) {
            m_project.tracks()[hostTrack].clips[hostClip].effects.append(video);
            selectHost = {hostTrack, hostClip};
        }
    }
    if (!audio.isEmpty()) {
        // Re-resolved from ids: minting the video host above may have inserted a track.
        const int audioTrackIndex = m_project.trackIndexById(targetTrackId);
        int audioClipIndex = -1;
        if (audioTrackIndex >= 0) {
            const drift::Track &owner = m_project.tracks().at(audioTrackIndex);
            for (int c = 0; c < owner.clips.size(); ++c) {
                if (owner.clips.at(c).id == targetClipId) {
                    audioClipIndex = c;
                    break;
                }
            }
        }
        if (audioClipIndex >= 0) {
            int hostTrack = audioTrackIndex;
            int hostClip = audioClipIndex;
            if (redirectToEffectHost(&hostTrack, &hostClip, drift::AdjustmentKind::AudioEffects,
                                     /*create=*/true)) {
                m_project.tracks()[hostTrack].clips[hostClip].audioEffects.append(audio);
                // Minting the video host above can shift indices, so the video ref is re-resolved
                // by id below rather than trusted here. An audio-only stack has nowhere else to go.
                if (selectHost.trackIndex < 0)
                    selectHost = {hostTrack, hostClip};
            }
        }
    }

    // Re-resolved by id: the audio host's lane insert may have moved the video host.
    int selectedTrack = -1;
    int selectedClip = -1;
    if (selectHost.trackIndex >= 0) {
        const QString hostId = m_project.tracks()
                                   .at(selectHost.trackIndex)
                                   .clips.at(selectHost.clipIndex)
                                   .id;
        findClipById(m_project, hostId, &selectedTrack, &selectedClip);
    }
    if (selectedClip < 0) {
        // Nothing applied, or the host vanished: leave the selection on the target clip.
        selectedTrack = m_project.trackIndexById(targetTrackId);
        if (selectedTrack >= 0) {
            const drift::Track &owner = m_project.tracks().at(selectedTrack);
            for (int c = 0; c < owner.clips.size(); ++c) {
                if (owner.clips.at(c).id == targetClipId) {
                    selectedClip = c;
                    break;
                }
            }
        }
    }
    if (selectedClip >= 0) {
        m_selectedTrack = selectedTrack;
        m_selectedClip = selectedClip;
        m_selection = {qMakePair(selectedTrack, selectedClip)};
    }
    pushProjectEdit(before, undoLabel);
    finishEdit(undoLabel);

    // finishEdit clears lastMessage, so the warning has to come after it.
    if (!missing.isEmpty()) {
        missing.removeDuplicates();
        missing.sort();
        const QString sample = missing.mid(0, 3).join(QStringLiteral(", "));
        setLastMessage(missing.size() == 1
                           ? tr("This stack uses “%1”, which isn’t installed — it "
                                "won’t show. Open Extras to install it.").arg(sample)
                           : tr("This stack uses %1 effects that aren’t installed — they "
                                "won’t show. Open Extras to install them.")
                                 .arg(missing.size()),
                       QStringLiteral("warning"));
    }
}

void AppController::copyEffectToClipboard(int trackIndex, int clipIndex, int effectIndex)
{
    copyEffectStack(effectStackFor(trackIndex, clipIndex, effectIndex, -1), tr("Effect copied"));
}

void AppController::copyAudioEffectToClipboard(int trackIndex, int clipIndex, int effectIndex)
{
    copyEffectStack(effectStackFor(trackIndex, clipIndex, -1, effectIndex),
                    tr("Audio effect copied"));
}

void AppController::copyClipEffectsToClipboard(int trackIndex, int clipIndex)
{
    copyEffectStack(effectStackFor(trackIndex, clipIndex, -1, -1), tr("Effects copied"));
}

void AppController::copyEffectStack(const drift::EffectStackPreset &stack, const QString &message)
{
    if (stack.isEmpty()) {
        setLastMessage(tr("This clip has no effects to copy"), QStringLiteral("error"));
        return;
    }
    QClipboard *board = QGuiApplication::clipboard();
    if (!board)
        return;
    // Text rather than a custom MIME type: text/plain is the one format that survives portals,
    // Wayland bridging and clipboard managers between two running instances — and it lets the
    // payload be pasted into a file by hand, since a copy and a .drifteffects file are one format.
    board->setText(QString::fromUtf8(
        QJsonDocument(drift::effectStackToJson(stack)).toJson(QJsonDocument::Indented)));
    setLastMessage(message, QStringLiteral("success"));
}

drift::EffectStackPreset AppController::effectStackOnClipboard()
{
    const QClipboard *board = QGuiApplication::clipboard();
    if (!board)
        return {};
    const QString text = board->text();
    // A substring test before the parse: this runs when a context menu opens, and the clipboard
    // may well be holding a page of prose from another application.
    if (!text.contains(QLatin1String("\"drift\"")))
        return {};
    return drift::effectStackFromJson(QJsonDocument::fromJson(text.toUtf8()).object());
}

bool AppController::clipboardHasEffects() const
{
    return !effectStackOnClipboard().isEmpty();
}

void AppController::pasteEffectsFromClipboard(int trackIndex, int clipIndex)
{
    const drift::EffectStackPreset stack = effectStackOnClipboard();
    if (stack.isEmpty()) {
        setLastMessage(tr("No effects on the clipboard"), QStringLiteral("error"));
        return;
    }
    applyEffectStack(trackIndex, clipIndex, stack, tr("Paste effects"));
}

bool AppController::canPasteAttributes() const
{
    if (m_clipboard.isEmpty())
        return false;
    if (!m_selection.isEmpty())
        return true;
    return isValidClipIndex(m_selectedTrack, m_selectedClip);
}

QVariantMap AppController::clipboardAttributes() const
{
    QVariantMap out;
    if (m_clipboard.isEmpty()) {
        out.insert(QStringLiteral("hasClip"), false);
        return out;
    }

    const ClipboardItem &item = m_clipboard.constFirst();
    const drift::Clip &c = item.clip;
    out.insert(QStringLiteral("hasClip"), true);
    out.insert(QStringLiteral("clipName"), c.name.isEmpty() ? tr("Clip") : c.name);

    const bool isVisual = (c.type != drift::ClipType::Audio);
    out.insert(QStringLiteral("isVisual"), isVisual);

    const bool hasTransform = isVisual && (!c.transformX.isEmpty() || !c.transformY.isEmpty()
                                           || !c.transformW.isEmpty() || !c.transformH.isEmpty()
                                           || !c.rotation.isEmpty() || !c.opacity.isEmpty()
                                           || c.layer3d
                                           || c.blendMode != drift::BlendMode::Normal
                                           || c.flipH || c.flipV
                                           || !item.masks.isEmpty()
                                           || c.animIn.kind != drift::ClipAnimKind::None
                                           || c.animOut.kind != drift::ClipAnimKind::None);
    out.insert(QStringLiteral("hasTransform"), hasTransform);

    const bool isAudioOrVideo = (c.type == drift::ClipType::Video || c.type == drift::ClipType::Audio);
    const bool hasSpeed = isAudioOrVideo && ((std::abs(c.speed - 1.0) > 0.001) || c.hasSpeedCurve() || c.reverse);
    out.insert(QStringLiteral("isAudioOrVideo"), isAudioOrVideo);
    out.insert(QStringLiteral("hasSpeed"), hasSpeed);
    out.insert(QStringLiteral("speed"), c.speed);
    out.insert(QStringLiteral("hasSpeedCurve"), c.hasSpeedCurve());
    out.insert(QStringLiteral("reverse"), c.reverse);

    const bool hasVolume = isAudioOrVideo && (!c.volume.isEmpty() || c.fadeInUs > 0 || c.fadeOutUs > 0);
    out.insert(QStringLiteral("hasVolume"), hasVolume);

    out.insert(QStringLiteral("hasEffects"), !c.effects.isEmpty());
    out.insert(QStringLiteral("effectCount"), c.effects.size());

    out.insert(QStringLiteral("hasAudioEffects"), !c.audioEffects.isEmpty());
    out.insert(QStringLiteral("audioEffectCount"), c.audioEffects.size());

    out.insert(QStringLiteral("hasTransitions"), !item.transitions.isEmpty());
    out.insert(QStringLiteral("transitionCount"), item.transitions.size());

    int count = 0;
    if (!m_selection.isEmpty()) {
        for (const auto &pair : m_selection) {
            if (isValidClipIndex(pair.first, pair.second))
                ++count;
        }
    } else if (isValidClipIndex(m_selectedTrack, m_selectedClip)) {
        count = 1;
    }
    out.insert(QStringLiteral("targetClipCount"), count);

    return out;
}

void AppController::requestPasteAttributes()
{
    if (!canPasteAttributes()) {
        setLastMessage(tr("Copy a clip and select target clips first"), QStringLiteral("warning"));
        return;
    }
    emit openPasteAttributesRequested();
}

void AppController::pasteAttributes(const QVariantMap &options)
{
    if (m_clipboard.isEmpty())
        return;

    const bool pasteTransform = options.value(QStringLiteral("transform"), false).toBool();
    const bool pasteSpeed = options.value(QStringLiteral("speed"), false).toBool();
    const bool pasteVolume = options.value(QStringLiteral("volume"), false).toBool();
    const bool pasteEffects = options.value(QStringLiteral("effects"), false).toBool();
    const bool pasteAudioEffects = options.value(QStringLiteral("audioEffects"), false).toBool();
    const bool pasteTransitions = options.value(QStringLiteral("transitions"), false).toBool();
    const bool replaceEffects = options.value(QStringLiteral("replaceEffects"), false).toBool();

    if (!pasteTransform && !pasteSpeed && !pasteVolume && !pasteEffects && !pasteAudioEffects && !pasteTransitions)
        return;

    QList<QPair<int, int>> pairs;
    if (!m_selection.isEmpty()) {
        for (const auto &pair : m_selection) {
            if (isValidClipIndex(pair.first, pair.second))
                pairs.append(pair);
        }
    } else if (isValidClipIndex(m_selectedTrack, m_selectedClip)) {
        pairs.append(qMakePair(m_selectedTrack, m_selectedClip));
    }
    if (pairs.isEmpty())
        return;

    const ClipboardItem &sourceItem = m_clipboard.constFirst();
    const drift::Clip &sourceClip = sourceItem.clip;
    const drift::TimeUs srcDurationUs = sourceClip.timelineDuration;

    const drift::Project before = m_project;
    bool anyModified = false;
    int modifiedCount = 0;
    QStringList missingEffectPacks;
    QList<QPair<QString, QList<drift::Mask>>> pendingMasks;

    for (const auto &pair : pairs) {
        const int trackIdx = pair.first;
        const int clipIdx = pair.second;
        if (!isValidClipIndex(trackIdx, clipIdx))
            continue;

        drift::Track &track = m_project.tracks()[trackIdx];
        drift::Clip &targetClip = track.clips[clipIdx];
        bool clipModified = false;

        // 1. Speed / Retime (applied first so target duration adjusts before keyframe scaling)
        if (pasteSpeed && (targetClip.type == drift::ClipType::Video || targetClip.type == drift::ClipType::Audio)) {
            if (sourceClip.hasSpeedCurve()) {
                targetClip.speedCurve = sourceClip.speedCurve;
                targetClip.reverse = sourceClip.reverse;
                targetClip.syncDurationFromSpeedCurve();
            } else {
                targetClip.speed = sourceClip.speed;
                targetClip.reverse = sourceClip.reverse;
                targetClip.speedCurve.clear();
                targetClip.syncSrcOutFromSpeed(sourceDurationForClip(targetClip));
            }
            syncLinkedPartnersFrom(m_project, targetClip);
            clipModified = true;
        }

        const drift::TimeUs targetDurationUs = targetClip.timelineDuration;
        const bool targetIsVisual = (targetClip.type != drift::ClipType::Audio);
        const bool targetHasAudio = (targetClip.type == drift::ClipType::Video || targetClip.type == drift::ClipType::Audio);

        // 2. Transform / Motion
        if (pasteTransform && targetIsVisual) {
            targetClip.transformX = rescaleKeyframeTrackTimes(sourceClip.transformX, srcDurationUs, targetDurationUs);
            targetClip.transformY = rescaleKeyframeTrackTimes(sourceClip.transformY, srcDurationUs, targetDurationUs);
            targetClip.transformW = rescaleKeyframeTrackTimes(sourceClip.transformW, srcDurationUs, targetDurationUs);
            targetClip.transformH = rescaleKeyframeTrackTimes(sourceClip.transformH, srcDurationUs, targetDurationUs);
            targetClip.rotation = rescaleKeyframeTrackTimes(sourceClip.rotation, srcDurationUs, targetDurationUs);
            targetClip.rotationX = rescaleKeyframeTrackTimes(sourceClip.rotationX, srcDurationUs, targetDurationUs);
            targetClip.rotationY = rescaleKeyframeTrackTimes(sourceClip.rotationY, srcDurationUs, targetDurationUs);
            targetClip.positionZ = rescaleKeyframeTrackTimes(sourceClip.positionZ, srcDurationUs, targetDurationUs);
            targetClip.perspective = rescaleKeyframeTrackTimes(sourceClip.perspective, srcDurationUs, targetDurationUs);
            targetClip.anchorX = rescaleKeyframeTrackTimes(sourceClip.anchorX, srcDurationUs, targetDurationUs);
            targetClip.anchorY = rescaleKeyframeTrackTimes(sourceClip.anchorY, srcDurationUs, targetDurationUs);
            targetClip.anchorZ = rescaleKeyframeTrackTimes(sourceClip.anchorZ, srcDurationUs, targetDurationUs);
            targetClip.layer3d = sourceClip.layer3d && targetClip.type != drift::ClipType::Model3d;
            targetClip.opacity = rescaleKeyframeTrackTimes(sourceClip.opacity, srcDurationUs, targetDurationUs);
            targetClip.blendMode = sourceClip.blendMode;
            targetClip.flipH = sourceClip.flipH;
            targetClip.flipV = sourceClip.flipV;
            // Keeps the pasted box paired with the orientation it was fit for. The stored value
            // is relative to each clip's own file, so match the visible result, not the number.
            if (targetClip.type == drift::ClipType::Video)
                setClipOrientationTo(targetClip, clipOrientation(sourceClip));
            // Pinning a mask mints a lane, which inserts a track and invalidates `track` and
            // `targetClip`. Queued by id and applied once the loop is done.
            pendingMasks.append(qMakePair(targetClip.id, sourceItem.masks));
            targetClip.animIn = sourceClip.animIn;
            targetClip.animOut = sourceClip.animOut;
            if (targetClip.animIn.durationUs > targetDurationUs)
                targetClip.animIn.durationUs = targetDurationUs;
            if (targetClip.animOut.durationUs > targetDurationUs)
                targetClip.animOut.durationUs = targetDurationUs;
            clipModified = true;
        }

        // 3. Audio / Volume & Fades
        if (pasteVolume && targetHasAudio) {
            targetClip.volume = rescaleKeyframeTrackTimes(sourceClip.volume, srcDurationUs, targetDurationUs);
            targetClip.fadeInUs = qMin(sourceClip.fadeInUs, targetDurationUs);
            targetClip.fadeOutUs = qMin(sourceClip.fadeOutUs, targetDurationUs);
            targetClip.fadeCurve = sourceClip.fadeCurve;
            targetClip.fadeShape = sourceClip.fadeShape;
            clipModified = true;
        }

        // 4. Video Effects
        if (pasteEffects && targetIsVisual && !sourceClip.effects.isEmpty()) {
            QList<drift::Effect> video;
            video.reserve(sourceClip.effects.size());
            for (const drift::Effect &incoming : sourceClip.effects) {
                if (const EffectPresetEntry *def = effectDefForId(incoming.catalogId)) {
                    drift::Effect effect = effectFromCatalogEntry(*def, incoming.parameters);
                    effect.paramKeyframes = incoming.paramKeyframes;
                    effect.enabled = incoming.enabled;
                    video.append(effect);
                } else {
                    video.append(incoming);
                    if (!incoming.catalogId.isEmpty())
                        missingEffectPacks.append(incoming.catalogId);
                }
            }
            drift::rescaleEffectKeyframes(video, srcDurationUs, targetDurationUs);
            if (replaceEffects) {
                targetClip.effects = video;
            } else {
                targetClip.effects.append(video);
            }
            clipModified = true;
        } else if (pasteEffects && targetIsVisual && replaceEffects && sourceClip.effects.isEmpty()) {
            if (!targetClip.effects.isEmpty()) {
                targetClip.effects.clear();
                clipModified = true;
            }
        }

        // 5. Audio Effects
        if (pasteAudioEffects && targetHasAudio && !sourceClip.audioEffects.isEmpty()) {
            QList<drift::Effect> audio;
            audio.reserve(sourceClip.audioEffects.size());
            for (const drift::Effect &incoming : sourceClip.audioEffects) {
                if (const AudioEffectEntry *def = audioEffectDefForId(incoming.catalogId)) {
                    drift::Effect effect = audioEffectFromCatalogEntry(*def, incoming.parameters);
                    effect.paramKeyframes = incoming.paramKeyframes;
                    effect.enabled = incoming.enabled;
                    audio.append(effect);
                } else {
                    audio.append(incoming);
                    if (!incoming.catalogId.isEmpty())
                        missingEffectPacks.append(incoming.catalogId);
                }
            }
            drift::rescaleEffectKeyframes(audio, srcDurationUs, targetDurationUs);
            if (replaceEffects) {
                targetClip.audioEffects = audio;
            } else {
                targetClip.audioEffects.append(audio);
            }
            clipModified = true;
        } else if (pasteAudioEffects && targetHasAudio && replaceEffects && sourceClip.audioEffects.isEmpty()) {
            if (!targetClip.audioEffects.isEmpty()) {
                targetClip.audioEffects.clear();
                clipModified = true;
            }
        }

        // 6. Transitions
        if (pasteTransitions && !sourceItem.transitions.isEmpty() && trackAllowsTransitions(track.type)) {
            const int partnerIndex = findTransitionPartnerIndex(track, clipIdx);
            if (partnerIndex >= 0) {
                const drift::Clip &fromClip = track.clips.at(clipIdx);
                const drift::Clip &toClip = track.clips.at(partnerIndex);
                for (const drift::Transition &srcTrans : sourceItem.transitions) {
                    bool found = false;
                    for (drift::Transition &existing : track.transitions) {
                        if (existing.fromClipId == fromClip.id && existing.toClipId == toClip.id) {
                            existing.kindId = srcTrans.kindId;
                            existing.durationUs = srcTrans.durationUs;
                            existing.parameters = srcTrans.parameters;
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        drift::Transition transition;
                        transition.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
                        transition.fromClipId = fromClip.id;
                        transition.toClipId = toClip.id;
                        transition.kindId = srcTrans.kindId;
                        transition.durationUs = srcTrans.durationUs;
                        transition.parameters = srcTrans.parameters;
                        track.transitions.append(transition);
                    }
                    clipModified = true;
                }
            }
        }

        if (clipModified) {
            anyModified = true;
            ++modifiedCount;
        }
    }

    if (!anyModified)
        return;

    // Paste replaces the target's whole stack with the source's, rather than merging the two:
    // an empty list clears what the target had, which is what "paste transform from a clip with
    // no mask" should mean.
    for (const auto &pending : std::as_const(pendingMasks)) {
        int maskTrack = -1;
        int maskClip = -1;
        if (!findClipById(m_project, pending.first, &maskTrack, &maskClip))
            continue;
        drift::clearLinkedMasks(m_project, maskTrack, maskClip);
        for (const drift::Mask &mask : pending.second) {
            // Each pin can insert a lane, which moves the clip; re-resolve before the next one.
            if (findClipById(m_project, pending.first, &maskTrack, &maskClip))
                drift::addLinkedMask(m_project, maskTrack, maskClip, mask);
        }
    }

    pushProjectEdit(before, tr("Paste attributes"));
    finishEdit(tr("Pasted attributes onto %n clip(s)", "", modifiedCount));

    if (!missingEffectPacks.isEmpty()) {
        missingEffectPacks.removeDuplicates();
        missingEffectPacks.sort();
        const QString sample = missingEffectPacks.mid(0, 3).join(QStringLiteral(", "));
        setLastMessage(missingEffectPacks.size() == 1
                           ? tr("Pasted effects use “%1”, which isn’t installed — it "
                                "won’t show. Open Extras to install it.").arg(sample)
                           : tr("Pasted effects use %1 packs that aren’t installed — they "
                                "won’t show. Open Extras to install them.")
                                 .arg(missingEffectPacks.size()),
                       QStringLiteral("warning"));
    }
}

QVariantList AppController::userEffectPresets() const
{
    QVariantList out;
    for (const drift::EffectStackPreset &preset : drift::EffectStackStore::instance().presets()) {
        // The card has no thumbnail to draw, so it lists what is in the stack instead.
        QStringList labels;
        for (const drift::Effect &effect : preset.effects) {
            const EffectPresetEntry *def = effectDefForId(effect.catalogId);
            labels.append(def ? def->meta.displayName : effect.catalogId);
        }
        for (const drift::Effect &effect : preset.audioEffects) {
            const AudioEffectEntry *def = audioEffectDefForId(effect.catalogId);
            labels.append(def ? def->displayName : effect.catalogId);
        }
        out.append(QVariantMap{
            {QStringLiteral("id"), preset.id},
            {QStringLiteral("label"), preset.label},
            {QStringLiteral("effectCount"), preset.effects.size()},
            {QStringLiteral("audioEffectCount"), preset.audioEffects.size()},
            {QStringLiteral("labels"), labels},
        });
    }
    return out;
}

QString AppController::saveEffectStack(const drift::EffectStackPreset &stack, const QString &label)
{
    if (stack.isEmpty()) {
        setLastMessage(tr("There are no effects to save"), QStringLiteral("error"));
        return {};
    }
    const QString presetId = drift::EffectStackStore::instance().add(label, stack);
    if (presetId.isEmpty()) {
        setLastMessage(tr("Could not save the effect preset"), QStringLiteral("error"));
        return {};
    }
    emit userEffectPresetsChanged();
    setLastMessage(tr("Effect preset saved"), QStringLiteral("success"));
    return presetId;
}

QString AppController::saveEffectAsPreset(int trackIndex, int clipIndex, int effectIndex,
                                          const QString &label)
{
    return saveEffectStack(effectStackFor(trackIndex, clipIndex, effectIndex, -1), label);
}

QString AppController::saveAudioEffectAsPreset(int trackIndex, int clipIndex, int effectIndex,
                                               const QString &label)
{
    return saveEffectStack(effectStackFor(trackIndex, clipIndex, -1, effectIndex), label);
}

QString AppController::saveClipEffectsAsPreset(int trackIndex, int clipIndex, const QString &label)
{
    return saveEffectStack(effectStackFor(trackIndex, clipIndex, -1, -1), label);
}

void AppController::applyEffectPreset(int trackIndex, int clipIndex, const QString &presetId)
{
    const std::optional<drift::EffectStackPreset> preset =
        drift::EffectStackStore::instance().presetForId(presetId);
    if (!preset)
        return;
    applyEffectStack(trackIndex, clipIndex, *preset, tr("Apply effect preset"));
}

bool AppController::renameUserEffectPreset(const QString &presetId, const QString &label)
{
    if (!drift::EffectStackStore::instance().rename(presetId, label)) {
        setLastMessage(tr("Could not rename the effect preset"), QStringLiteral("error"));
        return false;
    }
    emit userEffectPresetsChanged();
    setLastMessage(tr("Effect preset renamed"), QStringLiteral("success"));
    return true;
}

bool AppController::deleteUserEffectPreset(const QString &presetId)
{
    // Clips keep the effects they were given; only the library entry goes.
    if (!drift::EffectStackStore::instance().remove(presetId)) {
        setLastMessage(tr("Could not delete the effect preset"), QStringLiteral("error"));
        return false;
    }
    emit userEffectPresetsChanged();
    setLastMessage(tr("Effect preset deleted"), QStringLiteral("success"));
    return true;
}

int AppController::userEffectUses(const QString &effectId) const
{
    int count = 0;
    m_project.forEachTrackList([&](const QList<drift::Track> &tracks) {
        for (const drift::Track &track : tracks) {
            for (const drift::Clip &clip : track.clips) {
                for (const drift::Effect &effect : clip.effects)
                    count += effect.catalogId == effectId ? 1 : 0;
            }
        }
    });
    return count;
}

bool AppController::deleteUserEffect(const QString &effectId)
{
    const EffectPresetEntry *def = effectDefForId(effectId);
    if (!def || !isUserPackageDir(def->gpu.packageDir, QStringLiteral("effects"))) {
        setLastMessage(tr("Only effects you imported can be deleted"), QStringLiteral("error"));
        return false;
    }
    const QString name = def->meta.displayName;
    // The catalog entry dies with the reload below, so nothing may read `def` after this.
    if (!QDir(def->gpu.packageDir).removeRecursively()) {
        setLastMessage(tr("Could not delete %1").arg(name), QStringLiteral("error"));
        return false;
    }
    // Clips that used it keep the entry and render without it, like any missing effect.
    if (m_addonManager)
        m_addonManager->reloadForKinds({QStringLiteral("effects")});
    else
        reloadEffectCatalog();
    setLastMessage(tr("Deleted %1").arg(name), QStringLiteral("success"));
    return true;
}

bool AppController::exportUserEffectPreset(const QString &presetId, const QUrl &fileUrl)
{
#ifdef Q_OS_ANDROID
    if (AndroidUri::isContentUri(fileUrl)) {
        const QString tmp = QDir::temp().filePath(QStringLiteral("drift-effect-preset.json"));
        if (!drift::EffectStackStore::instance().exportToFile(presetId, tmp)) {
            setLastMessage(tr("Could not export the effect preset"), QStringLiteral("error"));
            return false;
        }
        QFile src(tmp);
        std::unique_ptr<QFile> sink = AndroidUri::openForWrite(fileUrl);
        if (!sink || !src.open(QIODevice::ReadOnly) || sink->write(src.readAll()) < 0) {
            setLastMessage(tr("Could not export the effect preset"), QStringLiteral("error"));
            return false;
        }
        QFile::remove(tmp);
        setLastMessage(tr("Effect preset exported"), QStringLiteral("success"));
        return true;
    }
#endif
    const QString path = fileUrl.isLocalFile() ? fileUrl.toLocalFile() : fileUrl.toString();
    if (path.isEmpty())
        return false;
    if (!drift::EffectStackStore::instance().exportToFile(presetId, path)) {
        setLastMessage(tr("Could not export the effect preset"), QStringLiteral("error"));
        return false;
    }
    setLastMessage(tr("Effect preset exported"), QStringLiteral("success"));
    return true;
}

bool AppController::importUserEffectPreset(const QUrl &fileUrl)
{
#ifdef Q_OS_ANDROID
    if (AndroidUri::isContentUri(fileUrl)) {
        std::unique_ptr<QFile> src = AndroidUri::openForRead(fileUrl);
        if (!src) {
            setLastMessage(tr("Could not import the effect preset"), QStringLiteral("error"));
            return false;
        }
        const QString tmp = QDir::temp().filePath(QStringLiteral("drift-effect-preset-import.json"));
        QFile dst(tmp);
        if (!dst.open(QIODevice::WriteOnly) || dst.write(src->readAll()) < 0) {
            setLastMessage(tr("Could not import the effect preset"), QStringLiteral("error"));
            return false;
        }
        dst.close();
        const QString id = drift::EffectStackStore::instance().importFromFile(tmp);
        QFile::remove(tmp);
        if (id.isEmpty()) {
            setLastMessage(tr("Could not import the effect preset"), QStringLiteral("error"));
            return false;
        }
        emit userEffectPresetsChanged();
        setLastMessage(tr("Effect preset imported"), QStringLiteral("success"));
        return true;
    }
#endif
    const QString path = fileUrl.isLocalFile() ? fileUrl.toLocalFile() : fileUrl.toString();
    if (path.isEmpty())
        return false;
    if (drift::EffectStackStore::instance().importFromFile(path).isEmpty()) {
        setLastMessage(tr("Could not import the effect preset"), QStringLiteral("error"));
        return false;
    }
    emit userEffectPresetsChanged();
    setLastMessage(tr("Effect preset imported"), QStringLiteral("success"));
    return true;
}
