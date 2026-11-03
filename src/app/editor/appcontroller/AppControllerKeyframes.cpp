#include "app/editor/appcontroller/AppControllerInternal.h"
#include "engine/packages/effects/EffectCatalog.h"

#include "core/animation/EasingPresetStore.h"
// Engine-internal by its own header comment, and included here only for releaseCaches(): the GPU
// caches have no other owner outside src/engine to ask. A one-line forwarder on GpuCompositor
// would restore the boundary.

#include <QClipboard>
#include <QGuiApplication>

using namespace drift::appdetail;
using drift::mcpdetail::findClipById;
using drift::mcpdetail::runBeatAnalysis;
using namespace drift::appcontroller_detail;

namespace {

// A colour animates as "<key>.r/.g/.b/.a" in 0..1, but its static value is one "#rrggbb[aa]" hex
// on the base key. This is the static channel the unkeyed curve starts from.
bool staticColorChannel(const QMap<QString, QVariant> &params, const QString &paramKey, double *out)
{
    if (paramKey.size() < 3 || paramKey.at(paramKey.size() - 2) != QLatin1Char('.'))
        return false;
    const QString hex = params.value(paramKey.chopped(2)).toString();
    if (!hex.startsWith(QLatin1Char('#')) || (hex.size() != 7 && hex.size() != 9))
        return false;
    const QColor color(hex.left(7));
    switch (paramKey.back().toLatin1()) {
    case 'r':
        *out = color.redF();
        return true;
    case 'g':
        *out = color.greenF();
        return true;
    case 'b':
        *out = color.blueF();
        return true;
    case 'a':
        *out = hex.size() == 9 ? hex.right(2).toInt(nullptr, 16) / 255.0 : 1.0;
        return true;
    }
    return false;
}

static QVariantMap easingSpecToVariant(const drift::EasingSpec &spec)
{
    return spec.toJson().toVariantMap();
}

static QVariantList easingSamplesToVariant(const drift::EasingSpec &spec, int n)
{
    QVariantList out;
    for (const QPointF &point : drift::sampleEasing(spec, n))
        out.append(point);
    return out;
}
} // namespace

void AppController::setClipKeyframe(int trackIndex, int clipIndex, const QString &prop, double atSeconds,
                                    double value)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;
    // "fx.<i>.<param>" addresses the effect stack, which now lives on the adjustment
    // linked to this clip. Transform props are untouched by this.
    redirectToKeyframeHost(&trackIndex, &clipIndex, prop);

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    drift::Clip &clip = track.clips[clipIndex];
    const drift::Project before = m_project;
    // Clamp at both ends. Past the clip's end a key can never render, but it still shapes the
    // curve up to it; before the start, the write lands on relative 0 and silently overwrites the
    // clip's base layout key. Neither is ever what the caller meant.
    const drift::TimeUs rel = qBound<drift::TimeUs>(
        0, drift::secondsToUs(atSeconds) - clip.timelineStart, clip.timelineDuration);
    if (prop.startsWith(QLatin1String("model3d.")) && isLegacyModel3dPoseKey(prop.mid(8))) {
        bool wrote = false;
        for (const auto &write : legacyModel3dPoseWrites(clip, prop.mid(8), value, rel, m_project.height()))
            wrote = writeClipPropValue(clip, write.first, rel, write.second, m_autoKeyEnabled, true) || wrote;
        if (!wrote)
            return;
    } else if (!writeClipPropValue(clip, prop, rel, value, m_autoKeyEnabled, /*force=*/true)) {
        return;
    }

    if (m_positionLinked && (prop == QLatin1String("x") || prop == QLatin1String("y"))) {
        const QString otherProp = (prop == QLatin1String("x")) ? QStringLiteral("y") : QStringLiteral("x");
        drift::KeyframeTrack<double> *otherKt = keyframeTrackForProp(clip, otherProp, /*createIfMissing=*/true);
        if (otherKt) {
            const drift::TimeUs otherNearest = otherKt->nearestKeyframe(rel, kKeyframeToleranceUs);
            if (otherNearest < 0) {
                const double otherVal = otherKt->evaluateAt(rel);
                writeClipPropValue(clip, otherProp, rel, otherVal, m_autoKeyEnabled, /*force=*/true);
                drift::KeyframeTrack<double> *srcKt = keyframeTrackForProp(clip, prop, false);
                if (srcKt)
                    otherKt->setEasing(rel, srcKt->easingAt(rel));
            }
        }
    }
    pushProjectEdit(before, tr("Add keyframe"));
    finishEdit(tr("Keyframe set"));
}

void AppController::setClipColorKeyframe(int trackIndex, int clipIndex, const QString &prop,
                                         double atSeconds, const QColor &color)
{
    if (!isValidClipIndex(trackIndex, clipIndex) || !color.isValid())
        return;
    // "fx.<i>.<param>" lives on the adjustment linked to this clip; other props are untouched.
    redirectToKeyframeHost(&trackIndex, &clipIndex, prop);
    if (!isValidClipIndex(trackIndex, clipIndex))
        return;
    drift::Clip &clip = m_project.tracks()[trackIndex].clips[clipIndex];
    const drift::Project before = m_project;
    const drift::TimeUs rel = qMax<drift::TimeUs>(0, drift::secondsToUs(atSeconds) - clip.timelineStart);
    const double channels[4] = {color.redF(), color.greenF(), color.blueF(), color.alphaF()};
    const char *suffixes[4] = {".r", ".g", ".b", ".a"};
    bool any = false;
    for (int i = 0; i < 4; ++i)
        any = writeClipPropValue(clip, prop + QLatin1String(suffixes[i]), rel, channels[i], m_autoKeyEnabled, /*force=*/true) || any;
    if (!any)
        return;
    pushProjectEdit(before, tr("Add keyframe"));
    finishEdit(tr("Keyframe set"));
}

void AppController::setClipVec2Keyframe(int trackIndex, int clipIndex, const QString &prop,
                                        double atSeconds, double x, double y)
{
    if (!isValidClipIndex(trackIndex, clipIndex))
        return;
    redirectToKeyframeHost(&trackIndex, &clipIndex, prop);
    if (!isValidClipIndex(trackIndex, clipIndex))
        return;
    drift::Clip &clip = m_project.tracks()[trackIndex].clips[clipIndex];
    const drift::Project before = m_project;
    const drift::TimeUs rel = qBound<drift::TimeUs>(
        0, drift::secondsToUs(atSeconds) - clip.timelineStart, clip.timelineDuration);
    const bool wroteX = writeClipPropValue(clip, prop + QLatin1String(".x"), rel, x, m_autoKeyEnabled, /*force=*/true);
    const bool wroteY = writeClipPropValue(clip, prop + QLatin1String(".y"), rel, y, m_autoKeyEnabled, /*force=*/true);
    if (!wroteX && !wroteY)
        return;
    pushProjectEdit(before, tr("Add keyframe"));
    finishEdit(tr("Keyframe set"));
}

void AppController::removeClipChannelKeyframes(int trackIndex, int clipIndex, const QString &prop,
                                               double atSeconds, const QStringList &suffixes)
{
    if (!isValidClipIndex(trackIndex, clipIndex))
        return;
    redirectToKeyframeHost(&trackIndex, &clipIndex, prop);
    if (!isValidClipIndex(trackIndex, clipIndex))
        return;
    drift::Clip &clip = m_project.tracks()[trackIndex].clips[clipIndex];
    const drift::Project before = m_project;
    const drift::TimeUs rel = qMax<drift::TimeUs>(0, drift::secondsToUs(atSeconds) - clip.timelineStart);
    bool any = false;
    for (const QString &suffix : suffixes) {
        drift::KeyframeTrack<double> *kt =
            keyframeTrackForProp(clip, prop + suffix, /*createIfMissing=*/false);
        if (!kt)
            continue;
        const drift::TimeUs nearest = kt->nearestKeyframe(rel, kKeyframeToleranceUs);
        if (nearest < 0)
            continue;
        kt->removeKeyframe(nearest);
        any = true;
    }
    if (!any)
        return;
    pushProjectEdit(before, tr("Remove keyframe"));
    finishEdit(tr("Keyframe removed"));
}

void AppController::removeClipKeyframe(int trackIndex, int clipIndex, const QString &prop, double atSeconds)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;
    // "fx.<i>.<param>" addresses the effect stack, which now lives on the adjustment
    // linked to this clip. Transform props are untouched by this.
    redirectToKeyframeHost(&trackIndex, &clipIndex, prop);

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    drift::Clip &clip = track.clips[clipIndex];
    drift::KeyframeTrack<double> *kt = keyframeTrackForProp(clip, prop, /*createIfMissing=*/false);
    if (!kt)
        return;

    const drift::Project before = m_project;
    const drift::TimeUs rel = qMax<drift::TimeUs>(0, drift::secondsToUs(atSeconds) - clip.timelineStart);
    const drift::TimeUs nearest = kt->nearestKeyframe(rel, kKeyframeToleranceUs);
    if (nearest < 0)
        return;
    kt->removeKeyframe(nearest);
    if (m_positionLinked && (prop == QLatin1String("x") || prop == QLatin1String("y"))) {
        const QString otherProp = (prop == QLatin1String("x")) ? QStringLiteral("y") : QStringLiteral("x");
        drift::KeyframeTrack<double> *otherKt = keyframeTrackForProp(clip, otherProp, false);
        if (otherKt) {
            const drift::TimeUs otherNearest = otherKt->nearestKeyframe(rel, kKeyframeToleranceUs);
            if (otherNearest >= 0)
                otherKt->removeKeyframe(otherNearest);
        }
    }
    pushProjectEdit(before, tr("Remove keyframe"));
    finishEdit(tr("Keyframe removed"));
}

void AppController::previewMoveClipKeyframe(int trackIndex, int clipIndex, const QString &prop,
                                            double fromSeconds, double toSeconds, double value)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;
    // "fx.<i>.<param>" addresses the effect stack, which now lives on the adjustment
    // linked to this clip. Transform props are untouched by this.
    redirectToKeyframeHost(&trackIndex, &clipIndex, prop);

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    drift::Clip &clip = track.clips[clipIndex];
    drift::KeyframeTrack<double> *kt = keyframeTrackForProp(clip, prop, /*createIfMissing=*/false);
    if (!kt)
        return;

    if (!m_previewDragActive)
        beginPreviewDrag(tr("Move keyframe"));

    const drift::TimeUs fromRel = qMax<drift::TimeUs>(0, drift::secondsToUs(fromSeconds) - clip.timelineStart);
    const drift::TimeUs toRel = qMax<drift::TimeUs>(0, drift::secondsToUs(toSeconds) - clip.timelineStart);
    const drift::TimeUs nearest = kt->nearestKeyframe(fromRel, kKeyframeToleranceUs);
    if (nearest >= 0)
        kt->removeKeyframe(nearest);
    kt->setKeyframe(toRel, value);

    if (m_positionLinked && (prop == QLatin1String("x") || prop == QLatin1String("y"))) {
        const QString otherProp = (prop == QLatin1String("x")) ? QStringLiteral("y") : QStringLiteral("x");
        drift::KeyframeTrack<double> *otherKt = keyframeTrackForProp(clip, otherProp, false);
        if (otherKt && !otherKt->isEmpty()) {
            const drift::TimeUs otherNearest = otherKt->nearestKeyframe(fromRel, kKeyframeToleranceUs);
            if (otherNearest >= 0) {
                const auto otherKey = otherKt->keyframes().value(otherNearest);
                otherKt->removeKeyframe(otherNearest);
                otherKt->setKeyframe(toRel, otherKey);
            }
        }
    }
    emitPreviewFrame();
}

// Locates a single key for the tangent editors. `atSeconds` is a timeline position; keys are
// stored clip-relative, and the strip reports them on the timeline, so it converts back here.
drift::Keyframe<double> *AppController::keyframeAt(int trackIndex, int clipIndex,
                                                   const QString &prop, double atSeconds)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return nullptr;
    // Covers the tangent and hold entry points too — they all resolve their key through here.
    redirectToKeyframeHost(&trackIndex, &clipIndex, prop);

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return nullptr;

    drift::Clip &clip = track.clips[clipIndex];
    drift::KeyframeTrack<double> *kt = keyframeTrackForProp(clip, prop, /*createIfMissing=*/false);
    if (!kt || kt->isEmpty())
        return nullptr;

    const drift::TimeUs local = drift::secondsToUs(atSeconds) - clip.timelineStart;
    const drift::TimeUs at = kt->nearestKeyframe(local, drift::kUsPerSecond / 60);
    if (at < 0)
        return nullptr;
    return kt->keyframeRef(at);
}

// Handles are authored in seconds on the QML side and stored in µs. Directions are enforced
// here rather than trusted from the caller: an out-handle reaching backwards (or an in-handle
// forwards) would fold the segment and make the curve multi-valued in time.
void AppController::applyTangents(drift::Keyframe<double> &key, double inDx, double inDy,
                                  double outDx, double outDy, bool corner)
{
    key.inDx = qMin(0.0, static_cast<double>(drift::secondsToUs(inDx)));
    key.outDx = qMax(0.0, static_cast<double>(drift::secondsToUs(outDx)));
    key.inDy = inDy;
    key.outDy = outDy;
    key.corner = corner;
    // A key can be shaped by hand and holding at the same time in the data, but the hold wins
    // when evaluated, which would silently discard the drag. Dropping it is the honest move.
    key.hold = false;
}

// The inspector's live readout. Exposed so QML does not have to carry its own copy of the
// interpolation math — which is no longer a two-line lerp now that keys have tangents.
double AppController::propertyValueAt(int trackIndex, int clipIndex, const QString &prop,
                                      double atSeconds, double fallback) const
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return fallback;
    // "fx.<i>.<param>" addresses the effect stack, which now lives on the adjustment
    // linked to this clip. Transform props are untouched by this.
    redirectToKeyframeHost(&trackIndex, &clipIndex, prop);

    const drift::Track &track = m_project.tracks().at(trackIndex);
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return fallback;

    const drift::Clip &clip = track.clips.at(clipIndex);
    const drift::KeyframeTrack<double> *kt = keyframeTrackForProp(clip, prop);
    if (!kt || kt->isEmpty())
        return propertyBaseValue(trackIndex, clipIndex, prop, fallback);

    return kt->evaluateAt(drift::secondsToUs(atSeconds) - clip.timelineStart);
}

// What an unkeyed property evaluates to. These mirror the defaults the compositor passes to
// transformValue (FrameCompositor.cpp) — an unkeyed clip fills the canvas at full opacity —
// so a curve drawn from these sits where the clip actually is rather than at zero.
double AppController::propertyBaseValue(int trackIndex, int clipIndex, const QString &prop,
                                        double fallback) const
{
    if (prop == QLatin1String("width"))
        return m_project.width();
    if (prop == QLatin1String("height"))
        return m_project.height();
    if (prop == QLatin1String("opacity") || prop == QLatin1String("volume"))
        return 1.0;
    if (prop == QLatin1String("x") || prop == QLatin1String("y")
        || prop == QLatin1String("rotation") || prop == QLatin1String("rotationX")
        || prop == QLatin1String("rotationY") || prop == QLatin1String("z")) {
        return 0.0;
    }
    if (prop == QLatin1String("perspective"))
        return drift::kDefaultClipPerspective;

    // Effect params fall back to the effect's own static value, which is what the compositor
    // reads for an unkeyed param — and that value now sits on the adjustment linked to the clip.
    redirectToKeyframeHost(&trackIndex, &clipIndex, prop);
    if (trackIndex >= 0 && trackIndex < m_project.tracks().size()) {
        const drift::Track &track = m_project.tracks().at(trackIndex);
        if (clipIndex >= 0 && clipIndex < track.clips.size()) {
            const drift::Clip &clip = track.clips.at(clipIndex);
            int effectIndex = -1;
            QString paramKey;
            if (parseEffectProp(prop, &effectIndex, &paramKey)
                && effectIndex >= 0 && effectIndex < clip.effects.size()) {
                const drift::Effect &effect = clip.effects.at(effectIndex);
                const QVariant value = effect.parameters.value(paramKey);
                if (value.isValid())
                    return value.toDouble();
                // Channel and vec2 keys have no stored entry of their own, so read the catalog-resolved
                // map that the compositor builds for the same effect.
                const EffectPresetEntry *def = effectDefForId(effect.catalogId);
                const QMap<QString, QVariant> params = def ? resolvedEffectParameters(effect, *def) : effect.parameters;
                if (params.value(paramKey).isValid())
                    return params.value(paramKey).toDouble();
                double channel = 0.0;
                if (staticColorChannel(params, paramKey, &channel))
                    return channel;
            }
            if (parseAudioEffectProp(prop, &effectIndex, &paramKey)
                && effectIndex >= 0 && effectIndex < clip.audioEffects.size()) {
                const drift::Effect &effect = clip.audioEffects.at(effectIndex);
                const QVariant value = effect.parameters.value(paramKey);
                if (value.isValid())
                    return value.toDouble();
                double channel = 0.0;
                if (staticColorChannel(effect.parameters, paramKey, &channel))
                    return channel;
            }
            // Same rule for a mask scalar: the static member is what the rasterizer reads when
            // the track is empty, so it is the curve's baseline.
            QString maskKey;
            if (parseMaskProp(prop, &maskKey)) {
                drift::Mask flat = clip.mask;
                flat.keyframes.clear();
                return flat.valueAt(maskKey, 0);
            }
            QString textKey;
            double scalar = 0.0;
            if (parseTextProp(clip.textStyle, prop, &textKey) && drift::textStyleScalar(clip.textStyle, textKey, &scalar))
                return scalar;
            QString shapeKey;
            if (parseShapeProp(clip, prop, &shapeKey) && drift::shapeStyleScalar(clip.shapeStyle, shapeKey, &scalar))
                return scalar;
            QString vectorKey;
            if (parseVectorProp(clip, prop, &vectorKey) && drift::vectorSlotScalar(clip.vector, vectorKey, &scalar))
                return scalar;
            QString modelKey;
            if (parseModel3dProp(clip, prop, &modelKey) && drift::model3dScalar(clip.model3d, modelKey, &scalar))
                return scalar;
            QString pdfKey;
            if (parsePdfProp(clip, prop, &pdfKey) && drift::pdfScalar(clip.pdf, pdfKey, &scalar))
                return scalar;
        }
    }
    return fallback;
}

QVariantList AppController::clipKeyframes(int trackIndex, int clipIndex, const QString &prop) const
{
    QVariantList out;
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return out;
    // "fx.<i>.<param>" addresses the effect stack, which now lives on the adjustment
    // linked to this clip. Transform props are untouched by this.
    redirectToKeyframeHost(&trackIndex, &clipIndex, prop);

    const drift::Track &track = m_project.tracks().at(trackIndex);
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return out;
    // A legacy model pose key reads back the transform track it writes.
    if (track.clips.at(clipIndex).type == drift::ClipType::Model3d
        && prop.startsWith(QLatin1String("model3d.")) && isLegacyModel3dPoseKey(prop.mid(8))) {
        const QList<QPair<QString, double>> writes =
            legacyModel3dPoseWrites(track.clips.at(clipIndex), prop.mid(8), 1.0, 0, 1.0);
        return clipKeyframes(trackIndex, clipIndex, writes.size() == 1 ? writes.first().first
                                                                       : QStringLiteral("width"));
    }

    const drift::Clip &clip = track.clips.at(clipIndex);
    const drift::KeyframeTrack<double> *kt = keyframeTrackForProp(clip, prop);
    if (!kt)
        return out;

    return keyframeListToVariant(*kt, clip.timelineStart);
}

bool AppController::clipPropertyKeyframesEnabled(int trackIndex, int clipIndex,
                                                 const QString &prop) const
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return true;
    // "fx.<i>.<param>" addresses the effect stack, which now lives on the adjustment
    // linked to this clip. Transform props are untouched by this.
    redirectToKeyframeHost(&trackIndex, &clipIndex, prop);

    const drift::Track &track = m_project.tracks().at(trackIndex);
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return true;

    const drift::KeyframeTrack<double> *kt =
        keyframeTrackForProp(track.clips.at(clipIndex), prop);
    return kt ? kt->enabled() : true;
}

void AppController::setClipPropertyKeyframesEnabled(int trackIndex, int clipIndex,
                                                    const QString &prop, bool enabled)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;
    // "fx.<i>.<param>" addresses the effect stack, which now lives on the adjustment
    // linked to this clip. Transform props are untouched by this.
    redirectToKeyframeHost(&trackIndex, &clipIndex, prop);

    if (clipIndex < 0 || clipIndex >= m_project.tracks().at(trackIndex).clips.size())
        return;

    {
        // With no keys there is no animation to switch off, and minting an empty track just to
        // hold the flag would make an untouched property look animated.
        const drift::KeyframeTrack<double> *kt =
            keyframeTrackForProp(m_project.tracks().at(trackIndex).clips.at(clipIndex), prop);
        if (!kt || kt->isEmpty() || kt->enabled() == enabled)
            return;
    }

    // The snapshot is taken before any mutable reference into the project: QList is implicitly
    // shared, so writing through a reference obtained earlier would land in the copy as well and
    // leave undo with nothing to restore.
    const drift::Project before = m_project;
    drift::Clip &clip = m_project.tracks()[trackIndex].clips[clipIndex];
    drift::KeyframeTrack<double> *kt = keyframeTrackForProp(clip, prop, /*createIfMissing=*/false);
    if (kt)
        kt->setEnabled(enabled);
    if (m_positionLinked && (prop == QLatin1String("x") || prop == QLatin1String("y"))) {
        const QString otherProp = (prop == QLatin1String("x")) ? QStringLiteral("y") : QStringLiteral("x");
        drift::KeyframeTrack<double> *otherKt = keyframeTrackForProp(clip, otherProp, /*createIfMissing=*/false);
        if (otherKt)
            otherKt->setEnabled(enabled);
    }
    pushProjectEdit(before, enabled ? tr("Enable keyframes")
                                    : tr("Disable keyframes"));
    finishEdit(enabled ? tr("Keyframes enabled") : tr("Keyframes disabled"));
}

void AppController::toggleClipPropertyKeyframesEnabled(int trackIndex, int clipIndex,
                                                       const QString &prop)
{
    setClipPropertyKeyframesEnabled(trackIndex, clipIndex, prop,
                                    !clipPropertyKeyframesEnabled(trackIndex, clipIndex, prop));
}

// Every property of the clip that carries an animation — the keyframe strip's series list.
//
// A lone key at the clip's start is how a *static* transform value is stored (every clip gets one
// for x/y/w/h when it is added), so that alone is not an animation. Effect params keep their static
// value in Effect::parameters, so for those any key at all means animated.
QStringList AppController::clipAnimatedProperties(int trackIndex, int clipIndex) const
{
    QStringList out;
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return out;

    const drift::Track &track = m_project.tracks().at(trackIndex);
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return out;

    const drift::Clip &clip = track.clips.at(clipIndex);

    static const QStringList transformProps = {
        QStringLiteral("x"),       QStringLiteral("y"),       QStringLiteral("width"),
        QStringLiteral("height"),  QStringLiteral("rotation"), QStringLiteral("rotationX"),
        QStringLiteral("rotationY"), QStringLiteral("z"),     QStringLiteral("perspective"),
        QStringLiteral("opacity"), QStringLiteral("volume"),
    };
    for (const QString &prop : transformProps) {
        const drift::KeyframeTrack<double> *kt = keyframeTrackForProp(clip, prop);
        if (!kt || kt->isEmpty())
            continue;
        if (kt->keyframes().size() == 1 && kt->keyframes().firstKey() == 0)
            continue;
        out.append(prop);
    }

    // Transform props stay on the clip, but its effect stack lives on the adjustment linked to
    // it — so the fx half of the series list is enumerated from there.
    const drift::Clip *host =
        effectHostClip(trackIndex, clipIndex, drift::AdjustmentKind::VideoEffects);
    if (host) {
        for (int i = 0; i < host->effects.size(); ++i) {
            const QMap<QString, drift::KeyframeTrack<double>> &params =
                host->effects.at(i).paramKeyframes;
            for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
                if (!it.value().isEmpty())
                    out.append(QStringLiteral("fx.%1.%2").arg(i).arg(it.key()));
            }
        }
    }

    const drift::Clip *audioHost =
        effectHostClip(trackIndex, clipIndex, drift::AdjustmentKind::AudioEffects);
    if (audioHost) {
        for (int i = 0; i < audioHost->audioEffects.size(); ++i) {
            const QMap<QString, drift::KeyframeTrack<double>> &params =
                audioHost->audioEffects.at(i).paramKeyframes;
            for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
                if (!it.value().isEmpty())
                    out.append(QStringLiteral("afx.%1.%2").arg(i).arg(it.key()));
            }
        }
    }

    // A mask lives on its own adjustment for the same reason, so its scalars come from there.
    // Listed in maskKeyframeProperties() order rather than the map's, so the strip's rows do not
    // reshuffle as tracks are created.
    const drift::Clip *maskHost =
        effectHostClip(trackIndex, clipIndex, drift::AdjustmentKind::Mask);
    if (maskHost) {
        for (const QString &key : drift::maskKeyframeProperties()) {
            const auto it = maskHost->mask.keyframes.constFind(key);
            if (it != maskHost->mask.keyframes.constEnd() && !it->isEmpty())
                out.append(QStringLiteral("mask.%1").arg(key));
        }
    }
    // Text and shape scalars live on the clip itself.
    for (const QString &key : drift::textKeyframeProperties(clip.textStyle)) {
        const auto it = clip.textStyle.keyframes.constFind(key);
        if (it != clip.textStyle.keyframes.constEnd() && !it->isEmpty())
            out.append(QStringLiteral("text.%1").arg(key));
    }
    if (clip.type == drift::ClipType::Shape) {
        for (const QString &key : drift::shapeKeyframeProperties(clip.shapeStyle)) {
            const auto it = clip.shapeStyle.keyframes.constFind(key);
            if (it != clip.shapeStyle.keyframes.constEnd() && !it->isEmpty())
                out.append(QStringLiteral("shape.%1").arg(key));
        }
    }
    if (clip.type == drift::ClipType::Vector) {
        for (auto it = clip.vector.keyframes.constBegin(); it != clip.vector.keyframes.constEnd(); ++it) {
            if (!it->isEmpty())
                out.append(QStringLiteral("vector.%1").arg(it.key()));
        }
    }
    if (clip.type == drift::ClipType::Model3d) {
        for (const QString &key : drift::model3dKeyframeProperties()) {
            const auto it = clip.model3d.keyframes.constFind(key);
            if (it != clip.model3d.keyframes.constEnd() && !it->isEmpty())
                out.append(QStringLiteral("model3d.%1").arg(key));
        }
    }
    if (clip.type == drift::ClipType::Pdf) {
        for (const QString &key : drift::pdfKeyframeProperties()) {
            const auto it = clip.pdf.keyframes.constFind(key);
            if (it != clip.pdf.keyframes.constEnd() && !it->isEmpty())
                out.append(QStringLiteral("pdf.%1").arg(key));
        }
    }
    return out;
}

void AppController::setKeyframeInterpolation(int trackIndex, int clipIndex, const QString &prop,
                                             const QString &mode)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;
    // "fx.<i>.<param>" addresses the effect stack, which now lives on the adjustment
    // linked to this clip. Transform props are untouched by this.
    redirectToKeyframeHost(&trackIndex, &clipIndex, prop);

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    drift::Clip &clip = track.clips[clipIndex];
    drift::KeyframeTrack<double> *kt = keyframeTrackForProp(clip, prop, /*createIfMissing=*/true);
    if (!kt || kt->isEmpty())
        return;

    // Presets act on the key at the playhead. Without one there is nothing to shape — the mode
    // is no longer a property-wide setting that can be armed ahead of the first key.
    const drift::TimeUs local = m_playheadUs - clip.timelineStart;
    const drift::TimeUs at = kt->nearestKeyframe(local, drift::kUsPerSecond / 30);
    if (at < 0)
        return;

    const drift::Project before = m_project;
    kt->setEasing(at, drift::interpolationFromString(mode));

    if (m_positionLinked && (prop == QLatin1String("x") || prop == QLatin1String("y"))) {
        const QString otherProp = (prop == QLatin1String("x")) ? QStringLiteral("y") : QStringLiteral("x");
        drift::KeyframeTrack<double> *otherKt = keyframeTrackForProp(clip, otherProp, /*createIfMissing=*/false);
        if (otherKt && !otherKt->isEmpty()) {
            const drift::TimeUs otherAt = otherKt->nearestKeyframe(local, drift::kUsPerSecond / 30);
            if (otherAt >= 0)
                otherKt->setEasing(otherAt, drift::interpolationFromString(mode));
        }
    }

    pushProjectEdit(before, tr("Keyframe easing changed"));
    finishEdit(tr("Keyframe easing updated"));
}

void AppController::setKeyframeEasingAt(int trackIndex, int clipIndex, const QString &prop,
                                        double atSeconds, const QString &mode)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;
    redirectToKeyframeHost(&trackIndex, &clipIndex, prop);

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    drift::Clip &clip = track.clips[clipIndex];
    drift::KeyframeTrack<double> *kt = keyframeTrackForProp(clip, prop, /*createIfMissing=*/false);
    if (!kt || kt->isEmpty())
        return;

    const drift::TimeUs local = drift::secondsToUs(atSeconds) - clip.timelineStart;
    const drift::TimeUs at = kt->nearestKeyframe(local, drift::kUsPerSecond / 60);
    if (at < 0)
        return;

    const drift::Project before = m_project;
    kt->setEasing(at, drift::interpolationFromString(mode));

    if (m_positionLinked && (prop == QLatin1String("x") || prop == QLatin1String("y"))) {
        const QString otherProp = (prop == QLatin1String("x")) ? QStringLiteral("y") : QStringLiteral("x");
        drift::KeyframeTrack<double> *otherKt = keyframeTrackForProp(clip, otherProp, /*createIfMissing=*/false);
        if (otherKt && !otherKt->isEmpty()) {
            const drift::TimeUs otherAt = otherKt->nearestKeyframe(local, drift::kUsPerSecond / 60);
            if (otherAt >= 0)
                otherKt->setEasing(otherAt, drift::interpolationFromString(mode));
        }
    }

    pushProjectEdit(before, tr("Keyframe easing changed"));
    finishEdit(tr("Keyframe easing updated"));
}

QVariantList AppController::easingPresets() const
{
    constexpr int kSamples = 24;
    QVariantList out;
    for (const drift::EasingPreset &preset : drift::builtinEasingPresets()) {
        out.append(QVariantMap{
            {QStringLiteral("id"), preset.id},
            {QStringLiteral("label"),
             QCoreApplication::translate("EasingPresets", preset.label.toUtf8().constData())},
            {QStringLiteral("family"), preset.family},
            {QStringLiteral("user"), false},
            {QStringLiteral("spec"), easingSpecToVariant(preset.spec)},
            {QStringLiteral("samples"), easingSamplesToVariant(preset.spec, kSamples)},
        });
    }
    for (const drift::UserEasingPreset &preset : drift::EasingPresetStore::instance().all()) {
        out.append(QVariantMap{
            {QStringLiteral("id"), preset.id},
            {QStringLiteral("label"), preset.label},
            {QStringLiteral("family"), QStringLiteral("user")},
            {QStringLiteral("user"), true},
            {QStringLiteral("spec"), easingSpecToVariant(preset.spec)},
            {QStringLiteral("samples"), easingSamplesToVariant(preset.spec, kSamples)},
        });
    }
    return out;
}

QVariantList AppController::easingSamples(const QVariantMap &spec, int n) const
{
    return easingSamplesToVariant(easingSpecFromVariant(spec), n);
}

QVariantMap AppController::keyframeSegmentEasing(int trackIndex, int clipIndex, const QString &prop,
                                                 double atSeconds) const
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return {};
    redirectToKeyframeHost(&trackIndex, &clipIndex, prop);

    const drift::Track &track = m_project.tracks().at(trackIndex);
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return {};

    const drift::Clip &clip = track.clips.at(clipIndex);
    const drift::KeyframeTrack<double> *kt = keyframeTrackForProp(clip, prop);
    if (!kt || kt->isEmpty())
        return {};

    const drift::TimeUs local = drift::secondsToUs(atSeconds) - clip.timelineStart;
    const drift::TimeUs at = kt->nearestKeyframe(local, drift::kUsPerSecond / 60);
    if (at < 0)
        return {};

    const drift::EasingSpec spec = kt->segmentEasing(at);
    QString presetId = drift::matchBuiltinEasingPreset(spec);
    if (presetId.isEmpty()) {
        for (const drift::UserEasingPreset &preset : drift::EasingPresetStore::instance().all()) {
            if (preset.spec.approxEquals(spec)) {
                presetId = preset.id;
                break;
            }
        }
    }

    return {
        {QStringLiteral("spec"), easingSpecToVariant(spec)},
        {QStringLiteral("presetId"), presetId},
        {QStringLiteral("samples"), easingSamplesToVariant(spec, 24)},
        {QStringLiteral("hasSegment"), kt->keyframes().size() >= 2},
    };
}

bool AppController::shapeKeyframeSegments(int trackIndex, int clipIndex, const QString &prop,
                                          double atSeconds, const drift::EasingSpec &spec,
                                          bool allSegments)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return false;
    redirectToKeyframeHost(&trackIndex, &clipIndex, prop);

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return false;

    drift::Clip &clip = track.clips[clipIndex];
    drift::KeyframeTrack<double> *kt = keyframeTrackForProp(clip, prop, /*createIfMissing=*/false);
    if (!kt || kt->isEmpty())
        return false;

    const drift::TimeUs local = drift::secondsToUs(atSeconds) - clip.timelineStart;
    drift::TimeUs at = -1;
    if (!allSegments) {
        at = kt->nearestKeyframe(local, drift::kUsPerSecond / 60);
        if (at < 0)
            return false;
    }

    auto shape = [&](drift::KeyframeTrack<double> *target) {
        if (allSegments)
            target->setSegmentEasingAll(spec);
        else
            target->setSegmentEasing(at, spec);
    };
    shape(kt);

    if (m_positionLinked && (prop == QLatin1String("x") || prop == QLatin1String("y"))) {
        const QString otherProp = (prop == QLatin1String("x")) ? QStringLiteral("y") : QStringLiteral("x");
        drift::KeyframeTrack<double> *otherKt = keyframeTrackForProp(clip, otherProp, /*createIfMissing=*/false);
        if (otherKt && !otherKt->isEmpty()) {
            if (allSegments) {
                shape(otherKt);
            } else {
                const drift::TimeUs otherAt = otherKt->nearestKeyframe(local, drift::kUsPerSecond / 60);
                if (otherAt >= 0)
                    otherKt->setSegmentEasing(otherAt, spec);
            }
        }
    }
    return true;
}

void AppController::setKeyframeSegmentEasing(int trackIndex, int clipIndex, const QString &prop,
                                             double atSeconds, const QVariantMap &spec)
{
    // Snapshot first, for the same reason as setKeyframeTangents.
    const drift::Project before = m_project;
    if (!shapeKeyframeSegments(trackIndex, clipIndex, prop, atSeconds, easingSpecFromVariant(spec), false))
        return;
    pushProjectEdit(before, tr("Keyframe easing changed"));
    finishEdit(tr("Keyframe easing updated"));
}

void AppController::previewSetKeyframeSegmentEasing(int trackIndex, int clipIndex, const QString &prop,
                                                    double atSeconds, const QVariantMap &spec)
{
    if (!shapeKeyframeSegments(trackIndex, clipIndex, prop, atSeconds, easingSpecFromVariant(spec), false))
        return;
    notifyTracksChanged();
    emit selectedClipDataChanged();
    emit projectMutated();
}

void AppController::applyEasingToAllSegments(int trackIndex, int clipIndex, const QString &prop,
                                             const QVariantMap &spec)
{
    const drift::Project before = m_project;
    if (!shapeKeyframeSegments(trackIndex, clipIndex, prop, 0.0, easingSpecFromVariant(spec), true))
        return;
    pushProjectEdit(before, tr("Keyframe easing changed"));
    finishEdit(tr("Keyframe easing updated"));
}

QString AppController::saveEasingPreset(const QString &label, const QVariantMap &spec)
{
    const QString id = drift::EasingPresetStore::instance().add(label, easingSpecFromVariant(spec));
    if (!id.isEmpty())
        emit easingPresetsChanged();
    return id;
}

bool AppController::renameEasingPreset(const QString &id, const QString &label)
{
    if (!drift::EasingPresetStore::instance().rename(id, label))
        return false;
    emit easingPresetsChanged();
    return true;
}

bool AppController::deleteEasingPreset(const QString &id)
{
    if (!drift::EasingPresetStore::instance().remove(id))
        return false;
    emit easingPresetsChanged();
    return true;
}

void AppController::copyEasingToClipboard(const QVariantMap &spec) const
{
    QGuiApplication::clipboard()->setText(easingSpecFromVariant(spec).toCssString());
}

QVariantMap AppController::easingFromClipboard() const
{
    const std::optional<drift::EasingSpec> spec =
        drift::EasingSpec::parseCss(QGuiApplication::clipboard()->text());
    if (!spec)
        return {};
    return easingSpecToVariant(*spec);
}

void AppController::syncLinkedTangents(int trackIndex, int clipIndex, const QString &prop,
                                       double atSeconds, double inDx, double inDy,
                                       double outDx, double outDy, bool corner)
{
    if (!m_positionLinked || (prop != QLatin1String("x") && prop != QLatin1String("y")))
        return;

    const QString otherProp = (prop == QLatin1String("x")) ? QStringLiteral("y") : QStringLiteral("x");
    drift::Keyframe<double> *otherKey = keyframeAt(trackIndex, clipIndex, otherProp, atSeconds);
    if (!otherKey)
        return;

    redirectToKeyframeHost(&trackIndex, &clipIndex, prop);
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;
    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;
    drift::Clip &clip = track.clips[clipIndex];

    const drift::KeyframeTrack<double> *masterTrack = keyframeTrackForProp(clip, prop);
    const drift::KeyframeTrack<double> *slaveTrack = keyframeTrackForProp(clip, otherProp);
    if (!masterTrack || !slaveTrack)
        return;

    const drift::TimeUs local = drift::secondsToUs(atSeconds) - clip.timelineStart;
    const drift::TimeUs masterTime = masterTrack->nearestKeyframe(local, drift::kUsPerSecond / 60);
    const drift::TimeUs slaveTime = slaveTrack->nearestKeyframe(local, drift::kUsPerSecond / 60);
    if (masterTime < 0 || slaveTime < 0)
        return;

    const auto &masterKeys = masterTrack->keyframes();
    const auto &slaveKeys = slaveTrack->keyframes();
    auto masterIt = masterKeys.constFind(masterTime);
    auto slaveIt = slaveKeys.constFind(slaveTime);
    if (masterIt == masterKeys.constEnd() || slaveIt == slaveKeys.constEnd())
        return;

    double otherInDx = inDx;
    double otherOutDx = outDx;

    double otherOutDy = 0.0;
    auto nextMaster = std::next(masterIt);
    auto nextSlave = std::next(slaveIt);
    if (nextMaster != masterKeys.constEnd() && nextSlave != slaveKeys.constEnd()) {
        const double spanMaster = nextMaster.value().value - masterIt.value().value;
        const double spanSlave = nextSlave.value().value - slaveIt.value().value;
        if (std::abs(spanMaster) > 1e-6)
            otherOutDy = outDy * (spanSlave / spanMaster);
        else if (std::abs(outDy) < 1e-6)
            otherOutDy = 0.0;
    }

    double otherInDy = 0.0;
    if (masterIt != masterKeys.constBegin() && slaveIt != slaveKeys.constBegin()) {
        auto prevMaster = std::prev(masterIt);
        auto prevSlave = std::prev(slaveIt);
        const double spanMaster = masterIt.value().value - prevMaster.value().value;
        const double spanSlave = slaveIt.value().value - prevSlave.value().value;
        if (std::abs(spanMaster) > 1e-6)
            otherInDy = inDy * (spanSlave / spanMaster);
        else if (std::abs(inDy) < 1e-6)
            otherInDy = 0.0;
    }

    applyTangents(*otherKey, otherInDx, otherInDy, otherOutDx, otherOutDy, corner);
}

void AppController::setKeyframeTangents(int trackIndex, int clipIndex, const QString &prop,
                                        double atSeconds, double inDx, double inDy, double outDx,
                                        double outDy, bool corner)
{
    // Snapshot first: the key pointer points into the keyframe map, which TrackList's copy does
    // not detach, so a snapshot taken after it would share the map and record the edit as "before".
    const drift::Project before = m_project;
    drift::Keyframe<double> *key = keyframeAt(trackIndex, clipIndex, prop, atSeconds);
    if (!key)
        return;

    applyTangents(*key, inDx, inDy, outDx, outDy, corner);
    syncLinkedTangents(trackIndex, clipIndex, prop, atSeconds, inDx, inDy, outDx, outDy, corner);
    pushProjectEdit(before, tr("Keyframe curve changed"));
    finishEdit(tr("Keyframe curve updated"));
}

void AppController::previewSetKeyframeTangents(int trackIndex, int clipIndex, const QString &prop,
                                               double atSeconds, double inDx, double inDy,
                                               double outDx, double outDy, bool corner)
{
    drift::Keyframe<double> *key = keyframeAt(trackIndex, clipIndex, prop, atSeconds);
    if (!key)
        return;

    applyTangents(*key, inDx, inDy, outDx, outDy, corner);
    syncLinkedTangents(trackIndex, clipIndex, prop, atSeconds, inDx, inDy, outDx, outDy, corner);
    notifyTracksChanged();
    emit selectedClipDataChanged();
    emit projectMutated();
}

void AppController::setKeyframeHold(int trackIndex, int clipIndex, const QString &prop,
                                    double atSeconds, bool hold)
{
    // Snapshot before resolving the key, for the same reason as setKeyframeTangents.
    const drift::Project before = m_project;
    drift::Keyframe<double> *key = keyframeAt(trackIndex, clipIndex, prop, atSeconds);
    if (!key || key->hold == hold)
        return;

    key->hold = hold;
    if (m_positionLinked && (prop == QLatin1String("x") || prop == QLatin1String("y"))) {
        const QString otherProp = (prop == QLatin1String("x")) ? QStringLiteral("y") : QStringLiteral("x");
        drift::Keyframe<double> *otherKey = keyframeAt(trackIndex, clipIndex, otherProp, atSeconds);
        if (otherKey)
            otherKey->hold = hold;
    }
    pushProjectEdit(before, tr("Keyframe hold changed"));
    finishEdit(hold ? tr("Keyframe holds") : tr("Keyframe interpolates"));
}

void AppController::resetClipTransform(int trackIndex, int clipIndex)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    drift::Clip &clip = track.clips[clipIndex];
    if (clip.type == drift::ClipType::Audio)
        return;

    const drift::Project before = m_project;
    clip.opacity = {};
    clip.transformX = {};
    clip.transformY = {};
    clip.transformW = {};
    clip.transformH = {};
    clip.rotation = {};
    clearClipPose3d(clip);
    clip.flipH = false;
    clip.flipV = false;
    // Media resets to how it first landed: its own shape, fitted and centred. Everything else
    // (text, shapes, composites, transform layers) resets to the full canvas.
    int mediaW = 0;
    int mediaH = 0;
    if (clip.type == drift::ClipType::Vector) {
        mediaW = clip.vector.width;
        mediaH = clip.vector.height;
    } else if (clip.type == drift::ClipType::Video || clip.type == drift::ClipType::Image
               || clip.type == drift::ClipType::Pdf) {
        if (const drift::MediaAsset *asset = m_project.asset(clip.assetId)) {
            mediaW = asset->width;
            mediaH = asset->height;
            if ((asset->rotationDegrees + clip.rotationCorrection) % 180 == 90)
                std::swap(mediaW, mediaH);
            if (clip.type == drift::ClipType::Video && mediaW > 0 && mediaH > 0) {
                mediaW = qMax(1, qRound(mediaW * clip.sourceFrame.width()));
                mediaH = qMax(1, qRound(mediaH * clip.sourceFrame.height()));
            }
        }
    }
    if (clip.type == drift::ClipType::Model3d)
        placeModelClipLayout(clip, m_project.width(), m_project.height());
    else if (mediaW > 0 && mediaH > 0)
        fitClipLayoutToCanvas(clip, mediaW, mediaH, m_project.width(), m_project.height());
    else
        setClipLayoutPixels(clip, 0, 0, m_project.width(), m_project.height());
    pushProjectEdit(before, tr("Reset transform"));
    finishEdit(tr("Transform reset"));
}
