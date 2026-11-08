#include "app/editor/appcontroller/AppControllerInternal.h"
#include "engine/packages/effects/EffectCatalog.h"
#include "engine/render3d/models/ModelAsset.h"
#include "engine/packages/transitions/TransitionCatalog.h"
#include "app/jobs/vision/StabilizeController.h"
#include "app/editor/ProjectFileController.h"
#include "ui/preview/PreviewController.h"

#include "engine/packages/audio/AudioEffectCatalog.h"
#include "engine/packages/looks/LookCatalog.h"
// Engine-internal by its own header comment, and included here only for releaseCaches(): the GPU
// caches have no other owner outside src/engine to ask. A one-line forwarder on GpuCompositor
// would restore the boundary.
#include "engine/color/CubeLut.h"
#include "engine/face/tracking/FaceTrack.h"
#include "playback/stats/PerfLog.h"

#include <QGuiApplication>
#include <QOpenGLContext>

#include <QSettings>
using namespace drift::appdetail;
using drift::mcpdetail::findClipById;
using drift::mcpdetail::runBeatAnalysis;
using namespace drift::appcontroller_detail;

namespace {



// Whether a baked sidecar carries the contour loops the makeup effects need. A track written
// before contours existed still drives every warp effect, so it cannot simply be treated as
// missing — but the beauty packages will pass straight through it, and saying so is what stops
// that reading as a broken effect.
//
// loadFaceTrackCached is a hash lookup once the file has been parsed, so this is cheap enough for
// the clip map; only the first valid frame is inspected, since a bake never mixes the two.
bool faceTrackHasContours(const QString &path)
{
    if (path.isEmpty())
        return false;
    const auto track = drift::loadFaceTrackCached(path);
    if (!track)
        return false;
    for (const drift::FaceTrackFrame &frame : track->frames) {
        for (const drift::FaceAnchors &face : frame.faces) {
            if (face.valid)
                return face.hasContours;
        }
    }
    return false;
}

// Same shape as faceTrackHasContours: a track baked before the 468-vertex mesh existed still
// drives warps and makeup, so it cannot be treated as missing — but the 3D face-mesh effect
// has nothing to warp, and saying so is what stops that reading as a broken effect.
bool faceTrackHasMesh(const QString &path)
{
    if (path.isEmpty())
        return false;
    const auto track = drift::loadFaceTrackCached(path);
    if (!track)
        return false;
    for (const drift::FaceTrackFrame &frame : track->frames) {
        for (const drift::FaceAnchors &face : frame.faces) {
            if (face.valid)
                return face.hasMesh;
        }
    }
    return false;
}

// The most faces any frame holds, which is how many slots a face effect's Face choice can
// usefully offer. Slots are positional, so this is the longest frame, not a count of people.
int faceTrackFaceCount(const QString &path)
{
    if (path.isEmpty())
        return 0;
    const auto track = drift::loadFaceTrackCached(path);
    if (!track)
        return 0;
    qsizetype most = 0;
    for (const drift::FaceTrackFrame &frame : track->frames)
        most = std::max(most, frame.faces.size());
    return int(most);
}

// What the properties panel may offer for a clip. Each inspector tab lists the caps it needs and
// is hidden without them, so a new clip kind only has to be described here.
QStringList inspectorCaps(const drift::Clip &clip, drift::TrackType trackType)
{
    using drift::ClipType;
    QStringList caps;
    if (clip.type == ClipType::Adjustment) {
        switch (clip.adjustmentKind) {
        case drift::AdjustmentKind::VideoEffects:
            caps << QStringLiteral("blendable") << QStringLiteral("maskable") << QStringLiteral("adj:videoEffects");
            break;
        case drift::AdjustmentKind::Mask:
            caps << QStringLiteral("blendable") << QStringLiteral("maskable") << QStringLiteral("adj:mask");
            break;
        case drift::AdjustmentKind::AudioEffects:
            caps << QStringLiteral("adj:audioEffects");
            break;
        case drift::AdjustmentKind::Transform:
            caps << QStringLiteral("transformable") << QStringLiteral("animatable") << QStringLiteral("adj:transform");
            break;
        case drift::AdjustmentKind::Camera:
            caps << QStringLiteral("adj:camera");
            break;
        }
        return caps;
    }

    if (trackAllowsTransitions(trackType))
        caps << QStringLiteral("transitionable");

    switch (clip.type) {
    case ClipType::Video:
        caps << QStringLiteral("transformable") << QStringLiteral("blendable") << QStringLiteral("animatable")
             << QStringLiteral("audible") << QStringLiteral("retimable") << QStringLiteral("stabilizable");
        break;
    case ClipType::Composite:
        caps << QStringLiteral("transformable") << QStringLiteral("blendable") << QStringLiteral("animatable")
             << QStringLiteral("audible") << QStringLiteral("retimable");
        break;
    case ClipType::Audio:
        caps << QStringLiteral("animatable") << QStringLiteral("audible") << QStringLiteral("retimable");
        break;
    case ClipType::Text:
        caps << QStringLiteral("transformable") << QStringLiteral("blendable") << QStringLiteral("animatable")
             << QStringLiteral("textStyled");
        break;
    case ClipType::Subtitle:
        caps << QStringLiteral("transformable") << QStringLiteral("blendable") << QStringLiteral("textStyled")
             << QStringLiteral("subtitle");
        break;
    case ClipType::Image:
        caps << QStringLiteral("transformable") << QStringLiteral("blendable") << QStringLiteral("animatable");
        break;
    case ClipType::Shape:
        caps << QStringLiteral("transformable") << QStringLiteral("blendable") << QStringLiteral("animatable")
             << QStringLiteral("shape");
        break;
    case ClipType::Vector:
        caps << QStringLiteral("transformable") << QStringLiteral("blendable") << QStringLiteral("animatable")
             << QStringLiteral("vector");
        break;
    case ClipType::Pdf:
        caps << QStringLiteral("transformable") << QStringLiteral("blendable") << QStringLiteral("animatable")
             << QStringLiteral("pdf");
        break;
    case ClipType::Model3d:
        caps << QStringLiteral("transformable") << QStringLiteral("blendable") << QStringLiteral("model3d");
        break;
    case ClipType::Adjustment:
        break;
    }
    return caps;
}

QVariantList subtitleCuesToMap(const QList<drift::SubtitleCue> &cues)
{
    QVariantList out;
    for (const drift::SubtitleCue &cue : cues) {
        out.append(QVariantMap{
            {QStringLiteral("start"), drift::usToSeconds(cue.startUs)},
            {QStringLiteral("end"), drift::usToSeconds(cue.endUs)},
            {QStringLiteral("text"), cue.text},
            {QStringLiteral("source"), cue.source},
        });
    }
    return out;
}

QVariantMap shapeStyleToMap(const drift::ShapeStyle &s, drift::TimeUs timelineStart)
{
    QVariantList layers;
    for (const drift::TextShadingLayer &layer : s.layers)
        layers.append(textLayerToMap(layer));
    QVariantMap map{
        {QStringLiteral("kind"), drift::shapeKindToString(s.kind)},
        {QStringLiteral("layers"), layers},
        {QStringLiteral("cornerRadius"), s.cornerRadius},
        {QStringLiteral("points"), s.points},
        {QStringLiteral("innerRatio"), s.innerRatio},
        {QStringLiteral("headSize"), s.headSize},
        {QStringLiteral("thickness"), s.thickness},
        {QStringLiteral("tailX"), s.tailX},
        {QStringLiteral("tailSize"), s.tailSize},
    };
    QVariantMap keyframes;
    for (auto it = s.keyframes.constBegin(); it != s.keyframes.constEnd(); ++it) {
        if (!it->isEmpty())
            keyframes.insert(it.key(), keyframeTrackToMap(it.value(), timelineStart));
    }
    if (!keyframes.isEmpty())
        map.insert(QStringLiteral("keyframes"), keyframes);
    return map;
}

// Metadata only: the document itself can run to megabytes and has its own getter.
QVariantMap vectorSourceToMap(const drift::VectorSource &v, drift::TimeUs timelineStart)
{
    QVariantMap overrides;
    for (auto it = v.slotValues.cbegin(); it != v.slotValues.cend(); ++it)
        overrides.insert(it.key(), vectorSlotValueToVariant(it.value()));
    QVariantMap keyframes;
    for (auto it = v.keyframes.cbegin(); it != v.keyframes.cend(); ++it) {
        if (!it->isEmpty())
            keyframes.insert(it.key(), keyframeTrackToMap(it.value(), timelineStart));
    }
    QVariantMap map{
        {QStringLiteral("kind"), drift::vectorKindToString(v.kind)},
        {QStringLiteral("inline"), v.isInline()},
        {QStringLiteral("path"), v.path},
        {QStringLiteral("hash"), v.hash},
        {QStringLiteral("width"), v.width},
        {QStringLiteral("height"), v.height},
        {QStringLiteral("fps"), v.fps},
        {QStringLiteral("durationSec"), drift::usToSeconds(v.durationUs)},
        {QStringLiteral("title"), v.title},
        {QStringLiteral("fit"), drift::vectorFitToString(v.fit)},
        {QStringLiteral("loop"), drift::vectorLoopToString(v.loop)},
        {QStringLiteral("offset"), drift::usToSeconds(v.startOffsetUs)},
        {QStringLiteral("slots"), overrides},
    };
    if (!keyframes.isEmpty())
        map.insert(QStringLiteral("keyframes"), keyframes);
    return map;
}
} // namespace
namespace drift::appdetail {


QVariantMap maskToMap(const drift::Mask &m, drift::TimeUs timelineStart)
{
    // {x, y} objects rather than [x, y] pairs: a Repeater delegate's `modelData` does not index
    // a nested array reliably, so a freeform's vertices came through as undefined and piled up in
    // the corner. Named fields are also what every other point-ish map in this file uses.
    QVariantList points;
    for (const QPointF &pt : m.points) {
        points.append(QVariantMap{{QStringLiteral("x"), pt.x()},
                                  {QStringLiteral("y"), pt.y()}});
    }

    QVariantMap maskKeyframes;
    for (auto it = m.keyframes.constBegin(); it != m.keyframes.constEnd(); ++it) {
        if (!it->isEmpty())
            maskKeyframes.insert(it.key(), keyframeTrackToMap(it.value(), timelineStart));
    }

    // Every field round-trips, media included. The inspector edits a mask by copying this map,
    // changing one key and handing it back to setClipMask, so anything missing here is silently
    // reset to its default — which is how toggling Invert used to erase a cutout's media path.
    return {
        {QStringLiteral("shape"), drift::maskShapeToString(m.shape)},
        {QStringLiteral("op"), drift::maskOpToString(m.op)},
        {QStringLiteral("enabled"), m.enabled},
        {QStringLiteral("name"), m.name},
        {QStringLiteral("x"), m.x},
        {QStringLiteral("y"), m.y},
        {QStringLiteral("w"), m.w},
        {QStringLiteral("h"), m.h},
        {QStringLiteral("rotation"), m.rotation},
        {QStringLiteral("feather"), m.feather},
        {QStringLiteral("invert"), m.invert},
        {QStringLiteral("points"), points},
        {QStringLiteral("mediaPath"), m.mediaPath},
        {QStringLiteral("mediaFgrPath"), m.mediaFgrPath},
        {QStringLiteral("mediaSrcOffsetUs"), qint64(m.mediaSrcOffsetUs)},
        {QStringLiteral("mediaFit"), drift::maskMediaFitToString(m.mediaFit)},
        {QStringLiteral("mediaChannel"), drift::maskMediaChannelToString(m.mediaChannel)},
        {QStringLiteral("mediaLoop"), m.mediaLoop},
        {QStringLiteral("mediaTimeBase"), drift::maskMediaTimeBaseToString(m.mediaTimeBase)},
        // Read-only, for the inspector and the preview overlay: keyframe edits go through the
        // generic keyframe invokables under "mask.<key>", never through setClipMask, so these are
        // deliberately not parsed back in maskFromMap.
        {QStringLiteral("animated"), m.isAnimated()},
        {QStringLiteral("pathKeyCount"), int(m.pathKeys.size())},
        {QStringLiteral("keyframes"), maskKeyframes},
    };
}



bool clipAcceptsPreviewTransform(const drift::Clip &clip)
{
    return clip.type == drift::ClipType::Shape || clip.type == drift::ClipType::Image
           || clip.type == drift::ClipType::Vector || clip.type == drift::ClipType::Model3d
           || clip.type == drift::ClipType::Pdf
           || clip.type == drift::ClipType::Text || clip.type == drift::ClipType::Subtitle
           || clip.type == drift::ClipType::Video || clip.type == drift::ClipType::Composite
           || (clip.type == drift::ClipType::Adjustment
               && clip.adjustmentKind == drift::AdjustmentKind::Transform);
}


double clipTransformValue(const drift::KeyframeTrack<double> &track, drift::TimeUs relative, double defaultValue)
{
    if (track.isEmpty())
        return defaultValue;
    return track.evaluateAt(relative);
}

} // namespace drift::appdetail
namespace {

// Shape-only check for callers without a clip: a text key is a flat name, a legacy alias or a
// layer path.
bool looksLikeTextProp(const QString &prop)
{
    if (!prop.startsWith(QLatin1String("text.")))
        return false;
    const QString suffix = prop.mid(5);
    if (suffix.startsWith(QLatin1String("layer.")))
        return true;
    drift::TextStyle probe;
    probe.layers = {drift::shadowLayer(Qt::black, 0, 4, 8, 0.6), drift::glowLayer(Qt::white, 18, 0.8),
                    drift::strokeLayer(2, Qt::black), drift::solidFillLayer(Qt::white)};
    return !drift::textKeyframeCanonicalKey(suffix, probe).isEmpty();
}

bool looksLikeShapeProp(const QString &prop)
{
    if (!prop.startsWith(QLatin1String("shape.")))
        return false;
    const QString suffix = prop.mid(6);
    if (suffix.startsWith(QLatin1String("layer.")))
        return true;
    drift::ShapeStyle probe;
    return !drift::shapeKeyframeCanonicalKey(suffix, probe).isEmpty();
}

bool looksLikeVectorProp(const QString &prop)
{
    if (!prop.startsWith(QLatin1String("vector.svg.")))
        return false;
    drift::VectorSource probe;
    double value = 0.0;
    return drift::vectorSlotScalar(probe, prop.mid(7), &value);
}

bool looksLikePdfProp(const QString &prop)
{
    double value = 0.0;
    return prop.startsWith(QLatin1String("pdf.")) && drift::pdfScalar({}, prop.mid(4), &value);
}

bool looksLikeModel3dProp(const QString &prop)
{
    if (!prop.startsWith(QLatin1String("model3d.")))
        return false;
    drift::Model3dSource probe;
    double value = 0.0;
    return drift::model3dScalar(probe, prop.mid(8), &value) || isLegacyModel3dPoseKey(prop.mid(8));
}

// Well-formed enough to keep in the keyframe-strip selection. Effect params can't be validated
// against a clip here — the selection outlives any particular clip — so only the shape is checked.
bool isKnownKeyframeProp(const QString &prop)
{
    int effectIndex = -1;
    QString paramKey;
    if (parseEffectProp(prop, &effectIndex, &paramKey) || parseAudioEffectProp(prop, &effectIndex, &paramKey))
        return true;
    QString maskKey;
    if (parseMaskProp(prop, &maskKey))
        return true;
    if (looksLikeTextProp(prop) || looksLikeShapeProp(prop) || looksLikeVectorProp(prop)
        || looksLikeModel3dProp(prop) || looksLikePdfProp(prop))
        return true;
    drift::Clip probe;
    return transformTrackForProp(probe, prop) != nullptr;
}

// Transform prop names are lower-case by convention, but effect param keys are verbatim
// identifiers from the effect manifest and are frequently camelCase ("u_blurRadius"), so the
// compound form must survive normalization untouched.
QString normalizeKeyframeProp(const QString &prop)
{
    // "mask.<key>" needs no exemption: every mask key is already lower-case. Text and shape keys
    // are camelCase members ("text.pixelSize", "shape.cornerRadius") and SVG element ids are
    // case-sensitive, so they pass through like effect params.
    const QString trimmed = prop.trimmed();
    return trimmed.startsWith(QLatin1String("fx.")) || trimmed.startsWith(QLatin1String("afx."))
                   || trimmed.startsWith(QLatin1String("text."))
                   || trimmed.startsWith(QLatin1String("shape.")) || trimmed.startsWith(QLatin1String("vector."))
                   || trimmed.startsWith(QLatin1String("model3d."))
                   || trimmed.startsWith(QLatin1String("pdf."))
               ? trimmed
               : trimmed.toLower();
}
// Deliberate writes snap far more tightly than that: just enough to absorb the rounding a time
// takes on its way out and back (replies carry 3 decimals, so up to 500 us), and not enough to
// swallow two keys an author actually meant to put close together.
constexpr drift::TimeUs kKeyframeWriteSnapUs = drift::kUsPerSecond / 1000;

} // namespace
namespace drift::appdetail {


// force=true (diamond click) always writes. Otherwise auto-key or an existing
// key at/near the playhead is required. Empty tracks get a constant key at 0
// when auto-key is off so static layout edits still work; a track with a single
// key retargets that key from anywhere so single-keyframe clips still edit freely.
bool writeKeyframeValue(drift::KeyframeTrack<double> &track, drift::TimeUs relative, double value,
                        bool autoKey, bool force)
{
    // With the animation switched off the property reads as its first key, so that is the key an
    // ordinary value edit has to land on — anything else would type into a value nothing shows.
    if (!track.enabled() && !track.isEmpty() && !force) {
        track.setKeyframe(track.keyframes().firstKey(), value);
        return true;
    }
    if (force || autoKey) {
        // Snap onto a key that is already within tolerance rather than minting a second one beside
        // it. MCP replies round times to 3 decimals, so a caller that reads a key's time back and
        // writes to it is up to 500 us off — enough to leave two keys where the caller meant one.
        const drift::TimeUs existing = track.nearestKeyframe(relative, kKeyframeWriteSnapUs);
        track.setKeyframe(existing >= 0 ? existing : relative, value);
        return true;
    }
    if (track.isEmpty()) {
        track.setKeyframe(0, value);
        return true;
    }
    if (track.keyframes().size() == 1) {
        track.setKeyframe(track.keyframes().firstKey(), value);
        return true;
    }
    const drift::TimeUs nearest = track.nearestKeyframe(relative, kKeyframeToleranceUs);
    if (nearest < 0)
        return false;
    track.setKeyframe(nearest, value);
    return true;
}

} // namespace drift::appdetail
namespace {

// Which GL flavour the app will run on. This is asked from the GUI thread, where no context is
// current, so it reads the module type rather than a context — the same signal GlRuntime uses to
// pick the surface format, and fixed for the life of the process.
bool runningOnGles()
{
    return QOpenGLContext::openGLModuleType() == QOpenGLContext::LibGLES;
}

// `effectIndex` and `timelineStart` are only needed to describe the params' keyframe tracks: the
// inspector addresses them as "fx.<index>.<key>", and key times are reported on the timeline.
QVariantMap effectToMap(const drift::Effect &effect, int effectIndex, drift::TimeUs timelineStart)
{
    const EffectPresetEntry *def = effectDefForId(effect.catalogId);
    QVariantList params;
    if (def) {
        for (const drift::EffectParamSpec &paramDef : def->meta.parameters) {
            if (paramDef.desktopGlOnly && runningOnGles())
                continue;
            QVariant value = effect.parameters.value(
                paramDef.isVec2() || paramDef.isRegion() ? paramDef.key + QLatin1String(".x") : paramDef.key);
            if (!value.isValid())
                value = paramDef.defaultVariant();

            // `value` stays the static value; the row falls back to it whenever the track is empty.
            QVariantMap param{
                {QStringLiteral("key"), paramDef.key},
                {QStringLiteral("label"), paramDef.label},
                {QStringLiteral("min"), paramDef.min},
                {QStringLiteral("max"), paramDef.max},
                {QStringLiteral("isBoolean"), paramDef.isBoolean()},
                {QStringLiteral("type"), paramDef.typeName()},
                {QStringLiteral("value"), value},
                {QStringLiteral("group"), paramDef.group},
                {QStringLiteral("groupCollapsed"), paramDef.groupCollapsed},
                {QStringLiteral("swatches"), paramDef.swatches},
                {QStringLiteral("enables"), paramDef.enables},
            };
            if (paramDef.isVec2()) {
                const QVariant y = effect.parameters.value(paramDef.key + QLatin1String(".y"),
                                                           paramDef.defaultY);
                param.insert(QStringLiteral("valueY"), y);
            }
            if (paramDef.isRegion()) {
                param.insert(QStringLiteral("valueY"),
                             effect.parameters.value(paramDef.key + QLatin1String(".y"), paramDef.defaultY));
                param.insert(QStringLiteral("valueW"),
                             effect.parameters.value(paramDef.key + QLatin1String(".w"), paramDef.defaultW));
                param.insert(QStringLiteral("valueH"),
                             effect.parameters.value(paramDef.key + QLatin1String(".h"), paramDef.defaultH));
                param.insert(QStringLiteral("regionShape"), paramDef.regionShape);
            }
            if (paramDef.isEnum())
                param.insert(QStringLiteral("options"), paramDef.options);
            if (paramDef.type == drift::EffectParamType::Lut) {
                const QString look = value.toString();
                const std::optional<drift::LookEntry> entry = drift::lookForId(look);
                param.insert(QStringLiteral("lookLabel"), entry ? entry->label : look);
                param.insert(QStringLiteral("missing"),
                             !look.isEmpty() && drift::resolveLutPath(look).isEmpty());
            }
            if (paramDef.step > 0.0)
                param.insert(QStringLiteral("step"), paramDef.step);
            if (paramDef.isColor())
                param.insert(QStringLiteral("alpha"), paramDef.alpha);
            if (paramDef.type == drift::EffectParamType::Curve)
                param.insert(QStringLiteral("curveMode"), paramDef.curveMode);
            if (paramDef.isFilePath()) {
                param.insert(QStringLiteral("fileFilters"), paramDef.fileFilters);
                const QString path = value.toString();
                param.insert(QStringLiteral("missing"),
                             !path.isEmpty() && !QFileInfo::exists(path));
                const QString warn = drift::modelAssetWarning(path);
                if (!warn.isEmpty())
                    param.insert(QStringLiteral("warning"), warn);
            }
            // A hue in degrees (chroma key's u_keyHue) is still a float on the keyframe stack, but
            // nobody thinks of a backdrop as "121°" — the inspector adds a swatch that maps a
            // picked colour onto it. Detected from the manifest's shape rather than a new param
            // type, so the shader contract and the addon manifests stay as they are.
            if (paramDef.type == drift::EffectParamType::Float && paramDef.min == 0.0
                && paramDef.max == 360.0
                && paramDef.key.endsWith(QLatin1String("hue"), Qt::CaseInsensitive)) {
                param.insert(QStringLiteral("hue"), true);
            }
            // File paths, clips and sampler inputs carry no `prop`: the keyframe stack is typed
            // double. A colour, vec2 and region animate as per-channel tracks ("<key>.r", "<key>.x"),
            // which share their key times, so the first channel's track stands in for the whole param.
            if (!paramDef.isFilePath() && !paramDef.isClip() && !paramDef.isSampler()) {
                const QString trackKey = paramDef.isColor()  ? paramDef.key + QLatin1String(".r")
                                         : paramDef.isVec2() || paramDef.isRegion()
                                             ? paramDef.key + QLatin1String(".x")
                                             : paramDef.key;
                param.insert(QStringLiteral("prop"),
                             QStringLiteral("fx.%1.%2").arg(effectIndex).arg(paramDef.key));
                param.insert(QStringLiteral("keyframes"),
                             keyframeTrackToMap(effect.paramKeyframes.value(trackKey),
                                                timelineStart));
            }
            params.append(param);
        }
    }
    return {
        {QStringLiteral("catalogId"), effect.catalogId},
        // Compositor-only effects carry no filter name, so an uninstalled one would label itself
        // with an empty string; the catalog id is at least the name the user has to go install.
        {QStringLiteral("label"),
         def ? def->meta.displayName : (effect.name.isEmpty() ? effect.catalogId : effect.name)},
        {QStringLiteral("params"), params},
        {QStringLiteral("compositorOnly"), def ? def->meta.compositorOnly : false},
        {QStringLiteral("needsDepth"), def ? def->needsDepth : false},
        {QStringLiteral("missing"), def == nullptr},
        {QStringLiteral("enabled"), effect.enabled},
    };
}

// What the timeline strip shows of an effect stack: a badge, and a tooltip listing the names.
// It reads `label` and `enabled` and nothing else, so it has no use for the full map above —
// which walks the catalog's parameter spec, stats every file-path value and builds a keyframe
// track per parameter, once per effect, once per clip, on every edit.
QVariantMap effectBadgeToMap(const drift::Effect &effect)
{
    const EffectPresetEntry *def = effectDefForId(effect.catalogId);
    return {
        {QStringLiteral("label"),
         def ? def->meta.displayName : (effect.name.isEmpty() ? effect.catalogId : effect.name)},
        {QStringLiteral("enabled"), effect.enabled},
    };
}

QVariantMap audioEffectBadgeToMap(const drift::Effect &effect)
{
    const AudioEffectEntry *def = audioEffectDefForId(effect.catalogId);
    return {
        {QStringLiteral("label"), def ? def->displayName : effect.name},
        {QStringLiteral("enabled"), effect.enabled},
    };
}

// Audio effects use the same per-instance shape as video effects, but read from the audio catalog.
// Params are addressed as "afx.<index>.<key>" for the keyframe API, key times on the timeline.
QVariantMap audioEffectToMap(const drift::Effect &effect, int effectIndex, drift::TimeUs timelineStart)
{
    const AudioEffectEntry *def = audioEffectDefForId(effect.catalogId);
    QVariantList params;
    if (def) {
        for (const drift::EffectParamSpec &paramDef : def->parameters) {
            QVariant value = effect.parameters.value(paramDef.key);
            if (!value.isValid()) {
                value = paramDef.defaultVariant();
            }
            params.append(QVariantMap{
                {QStringLiteral("key"), paramDef.key},
                {QStringLiteral("label"), paramDef.label},
                {QStringLiteral("min"), paramDef.min},
                {QStringLiteral("max"), paramDef.max},
                {QStringLiteral("isBoolean"), paramDef.isBoolean()},
                {QStringLiteral("type"), paramDef.typeName()},
                {QStringLiteral("value"), value},
                {QStringLiteral("prop"), QStringLiteral("afx.%1.%2").arg(effectIndex).arg(paramDef.key)},
                {QStringLiteral("keyframes"),
                 keyframeTrackToMap(effect.paramKeyframes.value(paramDef.key), timelineStart)},
            });
        }
    }
    return {
        {QStringLiteral("catalogId"), effect.catalogId},
        {QStringLiteral("label"), def ? def->displayName : effect.name},
        {QStringLiteral("icon"), def ? def->icon : QString()},
        {QStringLiteral("thumbnailPath"), def ? def->thumbnailPath : QString()},
        {QStringLiteral("params"), params},
        {QStringLiteral("missing"), def == nullptr},
        {QStringLiteral("enabled"), effect.enabled},
    };
}

QVariantMap keyframesToMap(const drift::Clip &clip)
{
    return {
        {QStringLiteral("opacity"), keyframeTrackToMap(clip.opacity, clip.timelineStart)},
        {QStringLiteral("x"), keyframeTrackToMap(clip.transformX, clip.timelineStart)},
        {QStringLiteral("y"), keyframeTrackToMap(clip.transformY, clip.timelineStart)},
        {QStringLiteral("width"), keyframeTrackToMap(clip.transformW, clip.timelineStart)},
        {QStringLiteral("height"), keyframeTrackToMap(clip.transformH, clip.timelineStart)},
        {QStringLiteral("rotation"), keyframeTrackToMap(clip.rotation, clip.timelineStart)},
        {QStringLiteral("rotationX"), keyframeTrackToMap(clip.rotationX, clip.timelineStart)},
        {QStringLiteral("rotationY"), keyframeTrackToMap(clip.rotationY, clip.timelineStart)},
        {QStringLiteral("z"), keyframeTrackToMap(clip.positionZ, clip.timelineStart)},
        {QStringLiteral("perspective"), keyframeTrackToMap(clip.perspective, clip.timelineStart)},
        {QStringLiteral("anchorX"), keyframeTrackToMap(clip.anchorX, clip.timelineStart)},
        {QStringLiteral("anchorY"), keyframeTrackToMap(clip.anchorY, clip.timelineStart)},
        {QStringLiteral("anchorZ"), keyframeTrackToMap(clip.anchorZ, clip.timelineStart)},
        {QStringLiteral("volume"), keyframeTrackToMap(clip.volume, clip.timelineStart)},
    };
}
} // namespace
namespace drift::appdetail {


drift::Transition *findTransition(drift::Track &track, const QString &transitionId)
{
    for (drift::Transition &transition : track.transitions) {
        if (transition.id == transitionId)
            return &transition;
    }
    return nullptr;
}

LinkedAudioPair linkedAudioPairFor(const drift::Project &project, const drift::Track &track,
                                   const QString &fromClipId, const QString &toClipId)
{
    if (track.type == drift::TrackType::Audio)
        return {};
    const drift::Clip *from = drift::clipById(track, fromClipId);
    const drift::Clip *to = drift::clipById(track, toClipId);
    if (!from || !to || from->linkId.isEmpty() || to->linkId.isEmpty())
        return {};
    for (const drift::ClipRef &pf : drift::linkedPartners(project, *from)) {
        if (project.tracks().at(pf.trackIndex).type != drift::TrackType::Audio)
            continue;
        const drift::Track &audio = project.tracks().at(pf.trackIndex);
        for (const drift::ClipRef &pt : drift::linkedPartners(project, *to)) {
            if (pt.trackIndex != pf.trackIndex)
                continue;
            const drift::Clip &a = audio.clips.at(pf.clipIndex);
            const drift::Clip &b = audio.clips.at(pt.clipIndex);
            if (drift::clipsEligibleForTransition(a, b))
                return {pf.trackIndex, a.id, b.id};
        }
    }
    return {};
}

drift::Transition *findTransitionBetween(drift::Track &track, const QString &fromId, const QString &toId)
{
    for (drift::Transition &t : track.transitions) {
        if (t.fromClipId == fromId && t.toClipId == toId)
            return &t;
    }
    return nullptr;
}

// The audio-track twin of a video transition, or null.
drift::Transition *mirroredAudioTransition(drift::Project &project, int trackIndex, const drift::Transition &t)
{
    const LinkedAudioPair pair = linkedAudioPairFor(project, project.tracks().at(trackIndex), t.fromClipId, t.toClipId);
    return pair.valid() ? findTransitionBetween(project.tracks()[pair.track], pair.fromId, pair.toId) : nullptr;
}

void syncLinkedPartnersFrom(drift::Project &project, const drift::Clip &source,
                            const QSet<QString> &skipClipIds)
{
    // Loose links only travel together; their timing stays independent.
    if (source.linkLoose)
        return;
    for (const drift::ClipRef &ref : drift::linkedPartners(project, source)) {
        const drift::Clip &partner = project.tracks().at(ref.trackIndex).clips.at(ref.clipIndex);
        if (skipClipIds.contains(partner.id) || partner.linkLoose)
            continue;
        drift::syncLinkedTiming(project.tracks()[ref.trackIndex].clips[ref.clipIndex], source);
    }
}



// Keyframe times are clip-relative, so a clip that changes duration would leave its animation
// sliding against the picture. Both clips cover the same source range, so each key is carried
// across through the moment of source it was sitting on.
template<typename T>
void remapKeyframeTrack(drift::KeyframeTrack<T> &dst, const drift::KeyframeTrack<T> &src,
                        const drift::Clip &from, const drift::Clip &to)
{
    if (src.isEmpty())
        return;

    const drift::TimeUs span = from.srcOut - from.srcIn;
    drift::KeyframeTrack<T> out;
    // A track switched off keeps its keys, and retiming must not switch it back on.
    out.setEnabled(src.enabled());
    // Tangents travel inside each key, so remapping the times carries the shape with them.
    for (auto it = src.keyframes().constBegin(); it != src.keyframes().constEnd(); ++it) {
        const drift::TimeUs sourceOffset =
            from.hasSpeedCurve() ? from.speedCurve.sourceOffsetForTimelineOffset(it.key(), span)
                                 : from.sourceDeltaForTimelineDelta(it.key());
        out.setKeyframe(to.speedCurve.timelineOffsetForSourceOffset(sourceOffset, span), it.value());
    }
    dst = out;
}

void remapKeyframesForRetime(drift::Clip &dst, const drift::Clip &src)
{
    remapKeyframeTrack(dst.opacity, src.opacity, src, dst);
    remapKeyframeTrack(dst.transformX, src.transformX, src, dst);
    remapKeyframeTrack(dst.transformY, src.transformY, src, dst);
    remapKeyframeTrack(dst.transformW, src.transformW, src, dst);
    remapKeyframeTrack(dst.transformH, src.transformH, src, dst);
    remapKeyframeTrack(dst.rotation, src.rotation, src, dst);
    remapKeyframeTrack(dst.rotationX, src.rotationX, src, dst);
    remapKeyframeTrack(dst.rotationY, src.rotationY, src, dst);
    remapKeyframeTrack(dst.positionZ, src.positionZ, src, dst);
    remapKeyframeTrack(dst.perspective, src.perspective, src, dst);
    remapKeyframeTrack(dst.anchorX, src.anchorX, src, dst);
    remapKeyframeTrack(dst.anchorY, src.anchorY, src, dst);
    remapKeyframeTrack(dst.anchorZ, src.anchorZ, src, dst);
    remapKeyframeTrack(dst.volume, src.volume, src, dst);

    for (int i = 0; i < dst.effects.size() && i < src.effects.size(); ++i) {
        const QMap<QString, drift::KeyframeTrack<double>> &srcParams = src.effects.at(i).paramKeyframes;
        for (auto it = srcParams.constBegin(); it != srcParams.constEnd(); ++it)
            remapKeyframeTrack(dst.effects[i].paramKeyframes[it.key()], it.value(), src, dst);
    }
    for (int i = 0; i < dst.audioEffects.size() && i < src.audioEffects.size(); ++i) {
        const QMap<QString, drift::KeyframeTrack<double>> &srcParams = src.audioEffects.at(i).paramKeyframes;
        for (auto it = srcParams.constBegin(); it != srcParams.constEnd(); ++it)
            remapKeyframeTrack(dst.audioEffects[i].paramKeyframes[it.key()], it.value(), src, dst);
    }
}

} // namespace drift::appdetail

bool AppController::writeClipProp(drift::Clip &clip, const QString &prop, drift::TimeUs relative,
                                  double value, bool autoKey, bool force)
{
    return writeClipPropValue(clip, prop, relative, value, autoKey, force);
}

drift::KeyframeTrack<double> *AppController::transformTrack(drift::Clip &clip, const QString &prop)
{
    return transformTrackForProp(clip, prop);
}

drift::KeyframeTrack<double> *AppController::propKeyframeTrack(drift::Clip &clip, const QString &prop,
                                                               bool create)
{
    return keyframeTrackForProp(clip, prop, create);
}

const drift::KeyframeTrack<double> *AppController::propKeyframeTrack(const drift::Clip &clip,
                                                                     const QString &prop) const
{
    return keyframeTrackForProp(clip, prop);
}

drift::TimeUs AppController::keyframeToleranceUs()
{
    return kKeyframeToleranceUs;
}

drift::TimeUs AppController::keyframeWriteSnapUs()
{
    return kKeyframeWriteSnapUs;
}

drift::EasingSpec AppController::easingSpecFromMap(const QVariantMap &spec)
{
    return easingSpecFromVariant(spec);
}

void AppController::clearPose3d(drift::Clip &clip)
{
    clearClipPose3d(clip);
}
namespace drift::appdetail {


void setClipLayoutPixels(drift::Clip &clip, double x, double y, double w, double h)
{
    clip.transformX = {};
    clip.transformY = {};
    clip.transformW = {};
    clip.transformH = {};
    clip.transformX.setKeyframe(0, x);
    clip.transformY.setKeyframe(0, y);
    clip.transformW.setKeyframe(0, qMax(1.0, w));
    clip.transformH.setKeyframe(0, qMax(1.0, h));
}

} // namespace drift::appdetail
namespace {

// Both halves of a split keep the same effects, but only one keeps the original clip id. Where the
// surviving half is given a fresh id, the adjustment would be left pinned to an id nothing answers
// to and syncLinkedAdjustments would quietly unlink it.
void repointLinkedAdjustments(drift::Project &project, int parentTrackIndex, const QString &fromClipId,
                              const QString &toClipId)
{
    if (fromClipId.isEmpty() || toClipId.isEmpty() || fromClipId == toClipId)
        return;

    for (const int laneIndex : drift::adjustmentLaneIndexes(project, parentTrackIndex)) {
        drift::Track &lane = project.tracks()[laneIndex];
        for (drift::Clip &adjustment : lane.clips) {
            if (adjustment.linkedClipId == fromClipId)
                adjustment.linkedClipId = toClipId;
        }
    }
}
} // namespace

QVariantMap AppController::clipToMap(const drift::Clip &clip, const drift::Clip *videoEffectHost,
                                     const drift::Clip *audioEffectHost,
                                     const drift::Clip *maskHost,
                                     const drift::Clip *faceSource) const
{
    // Face landmarks live on the media clip, so a linked adjustment reports its host's.
    const drift::Clip &face = faceSource ? *faceSource : clip;
    // A media clip's stack physically lives on the adjustment linked to it, but the inspector
    // and the MCP tools still ask the clip for "its" effects — so report the host's list here.
    // Indices line up 1:1 with what the effect invokables take, because a clip has at most one
    // linked adjustment per kind. The two kinds are separate adjustments, hence two hosts. An
    // adjustment clip passes neither and reports its own lists.
    const drift::Clip &videoHost = videoEffectHost ? *videoEffectHost : clip;
    const drift::Clip &audioHost = audioEffectHost ? *audioEffectHost : clip;

    QVariantList effects;
    for (int i = 0; i < videoHost.effects.size(); ++i)
        effects.append(effectToMap(videoHost.effects.at(i), i, clip.timelineStart));

    QVariantList audioEffects;
    for (int i = 0; i < audioHost.audioEffects.size(); ++i)
        audioEffects.append(audioEffectToMap(audioHost.audioEffects.at(i), i, clip.timelineStart));

    QVariantList fadeShape;
    for (const QPointF &pt : clip.fadeShape.points()) {
        fadeShape.append(QVariantMap{
            {QStringLiteral("t"), pt.x()},
            {QStringLiteral("g"), pt.y()},
        });
    }

    // Full source length backs the filmstrip's timestamp mapping (the strip is sampled across
    // the whole source, so tiles need the total to place srcIn/srcOut within it).
    const drift::MediaAsset *sourceAsset = m_project.asset(clip.assetId);

    QVariantMap map{
        {QStringLiteral("id"), clip.id},
        {QStringLiteral("name"), clip.name},
        {QStringLiteral("path"), clip.path},
        {QStringLiteral("sourceFrame"), clip.sourceFrame},
        {QStringLiteral("sourceFrameAngle"), clip.sourceFrameAngle},
        {QStringLiteral("sourceWidth"), sourceAsset ? sourceAsset->width : 0},
        {QStringLiteral("sourceHeight"), sourceAsset ? sourceAsset->height : 0},
        {QStringLiteral("sourceRotation"), sourceAsset ? sourceAsset->rotationDegrees : 0},
        {QStringLiteral("kind"), drift::clipTypeToString(clip.type)},
        // The nested timeline a composite (or the audio separated from one) plays.
        {QStringLiteral("sequenceId"), clip.sequenceId},
        // Adjustment clips only: which inspector they get, and whether the timeline should
        // treat their edges as pinned.
        {QStringLiteral("adjustmentKind"), drift::adjustmentKindToString(clip.adjustmentKind)},
        {QStringLiteral("linkedClipId"), clip.linkedClipId},
        {QStringLiteral("thumbnailPath"), clip.thumbnailPath},
        {QStringLiteral("filmstripPath"), clip.filmstripPath},
        {QStringLiteral("textContent"), clip.textContent},
        {QStringLiteral("textStyle"), textStyleToMap(clip.textStyle, clip.timelineStart)},
        {QStringLiteral("subtitleCues"), subtitleCuesToMap(clip.subtitleCues)},
        {QStringLiteral("shapeStyle"), shapeStyleToMap(clip.shapeStyle, clip.timelineStart)},
        {QStringLiteral("blendMode"), drift::blendModeToString(clip.blendMode)},
        {QStringLiteral("speed"), clip.speed},
        {QStringLiteral("hasSpeedCurve"), clip.hasSpeedCurve()},
        {QStringLiteral("reverse"), clip.reverse},
        {QStringLiteral("flipH"), clip.flipH},
        {QStringLiteral("layer3d"), clip.layer3d},
        {QStringLiteral("depthOcclude"), clip.depthOcclude},
        {QStringLiteral("aimAtTarget"), clip.aimAtTarget},
        {QStringLiteral("flipV"), clip.flipV},
        // Discrete lossless orientation fix, distinct from the free "rotation" keyframe track
        // below. "orientation" is the absolute result (what the inspector shows), the correction
        // is what is stored — see drift::Clip::rotationCorrection.
        {QStringLiteral("rotationCorrection"), clip.rotationCorrection},
        {QStringLiteral("orientation"), clipOrientation(clip)},
        // Same redirect as the effect stacks above: a media clip's mask lives on the adjustment
        // pinned to it, but the inspector and the MCP tools still ask the clip for "its" mask.
        {QStringLiteral("mask"), maskToMap(maskHost ? maskHost->mask : clip.mask,
                                           maskHost ? maskHost->timelineStart : clip.timelineStart)},
        {QStringLiteral("hasFaceTrack"), !face.faceTrackPath.isEmpty()},
        {QStringLiteral("faceTrackHasContours"), faceTrackHasContours(face.faceTrackPath)},
        {QStringLiteral("faceTrackHasMesh"), faceTrackHasMesh(face.faceTrackPath)},
        {QStringLiteral("faceTrackFaceCount"), faceTrackFaceCount(face.faceTrackPath)},
        // Whether there is anything a face scan could run on at all: an unlinked adjustment or an
        // audio clip has no source, and the inspector must not offer to scan one.
        {QStringLiteral("canFaceTrack"), face.type == drift::ClipType::Video
             || face.type == drift::ClipType::Image},
        {QStringLiteral("hasDepth"), !face.depthPath.isEmpty()},
        // Depth lives on the media clip an effect adjustment is pinned to, and so does its job;
        // the inspector needs that clip's id to follow the job's progress.
        {QStringLiteral("depthClipId"), face.id},
        {QStringLiteral("canDepth"), face.type == drift::ClipType::Video
             || face.type == drift::ClipType::Image},
        {QStringLiteral("stabilized"), clip.stabilizeAppliedSmoothing >= 0},
        {QStringLiteral("stabilizing"), clip.stabilizing},
        {QStringLiteral("stabilizeMode"), drift::stabilizeModeToString(clip.stabilizeMode)},
        {QStringLiteral("stabilizeAppliedMode"), drift::stabilizeModeToString(clip.stabilizeAppliedMode)},
        {QStringLiteral("stabilizeSmoothing"), clip.stabilizeSmoothing},
        {QStringLiteral("stabilizeTripod"), clip.stabilizeTripod},
        {QStringLiteral("stabilizeStale"), clip.stabilizeAppliedSmoothing >= 0
             && (clip.stabilizeSmoothing != clip.stabilizeAppliedSmoothing
                 || clip.stabilizeTripod != clip.stabilizeAppliedTripod
                 || clip.stabilizeMode != clip.stabilizeAppliedMode)},
        {QStringLiteral("stabilizeProgress"), m_stabilize->progressFor(clip.id)},
        {QStringLiteral("stabilizeStatus"), m_stabilize->statusFor(clip.id)},
        {QStringLiteral("start"), drift::usToSeconds(clip.timelineStart)},
        {QStringLiteral("duration"), drift::usToSeconds(clip.timelineDuration)},
        {QStringLiteral("inPoint"), drift::usToSeconds(clip.srcIn)},
        {QStringLiteral("outPoint"), drift::usToSeconds(clip.srcOut)},
        {QStringLiteral("sourceDuration"), sourceAsset ? drift::usToSeconds(sourceAsset->durationUs) : 0.0},
        {QStringLiteral("assetId"), clip.assetId},
        {QStringLiteral("assetIndex"), assetIndexForClip(clip)},
        {QStringLiteral("linked"), !clip.linkId.isEmpty()},
        // The pairing key itself, so a trim drag can carry its A/V companion on screen without
        // waiting for the commit to sync it.
        {QStringLiteral("linkId"), clip.linkId},
        {QStringLiteral("audioStreamIndex"), clip.audioStreamIndex},
        {QStringLiteral("pan"), clip.pan},
        {QStringLiteral("volume"), clip.volume.isEmpty() ? 1.0 : clip.volume.evaluateAt(0)},
        {QStringLiteral("fadeIn"), drift::usToSeconds(edgeHandleUs(clip, true))},
        {QStringLiteral("fadeOut"), drift::usToSeconds(edgeHandleUs(clip, false))},
        {QStringLiteral("fadeCurve"), drift::fadeCurveToString(clip.fadeCurve)},
        {QStringLiteral("fadeShape"), fadeShape},
        {QStringLiteral("fadeHandles"),
         QVariantList{clip.fadeShape.handle1().x(), clip.fadeShape.handle1().y(),
                      clip.fadeShape.handle2().x(), clip.fadeShape.handle2().y()}},
        {QStringLiteral("animIn"), QVariantMap{
             {QStringLiteral("kind"), drift::clipAnimKindToString(clip.animIn.kind)},
             {QStringLiteral("duration"), drift::usToSeconds(clip.animIn.durationUs)},
             {QStringLiteral("ease"), drift::clipAnimEaseToString(clip.animIn.ease)},
             {QStringLiteral("curve"), drift::fadeCurveToString(clip.animIn.curve)},
             {QStringLiteral("handles"), handlesToVariant(clip.animIn.shape)},
         }},
        {QStringLiteral("animOut"), QVariantMap{
             {QStringLiteral("kind"), drift::clipAnimKindToString(clip.animOut.kind)},
             {QStringLiteral("duration"), drift::usToSeconds(clip.animOut.durationUs)},
             {QStringLiteral("ease"), drift::clipAnimEaseToString(clip.animOut.ease)},
             {QStringLiteral("curve"), drift::fadeCurveToString(clip.animOut.curve)},
             {QStringLiteral("handles"), handlesToVariant(clip.animOut.shape)},
         }},
        {QStringLiteral("effects"), effects},
        {QStringLiteral("audioEffects"), audioEffects},
        {QStringLiteral("keyframes"), keyframesToMap(clip)},
    };
    if (clip.type == drift::ClipType::Vector)
        map.insert(QStringLiteral("vector"), vectorSourceToMap(clip.vector, clip.timelineStart));
    if (clip.type == drift::ClipType::Model3d)
        map.insert(QStringLiteral("model3d"), model3dSourceToMap(clip.model3d, clip.timelineStart));
    if (clip.type == drift::ClipType::Pdf)
        map.insert(QStringLiteral("pdf"), pdfSourceToMap(clip.pdf, clip.timelineStart));
    return map;
}

// The timeline strip's view of a clip. Same source fields as the inspector map above, minus
// everything only an inspector opens — textStyle alone is 45 keys and ten colour-to-string
// conversions. Built as a value rather than a QVariantMap because this one runs for every clip
// in the project on every edit, and the model it feeds compares rows to find what changed.
TimelineClipsModel::Row AppController::clipRow(const drift::Clip &clip,
                                               const drift::Clip *videoEffectHost,
                                               const drift::Clip *audioEffectHost) const
{
    const drift::Clip &videoHost = videoEffectHost ? *videoEffectHost : clip;
    const drift::Clip &audioHost = audioEffectHost ? *audioEffectHost : clip;
    const drift::MediaAsset *sourceAsset = m_project.asset(clip.assetId);

    TimelineClipsModel::Row row;
    row.id = clip.id;
    row.name = clip.name;
    row.path = clip.path;
    row.kind = drift::clipTypeToString(clip.type);
    row.adjustmentKind = drift::adjustmentKindToString(clip.adjustmentKind);
    row.linkedClipId = clip.linkedClipId;
    row.linkId = clip.linkId;
    row.linked = !clip.linkId.isEmpty();
    row.filmstripPath = clip.filmstripPath;
    row.textContent = clip.textContent;
    row.rotationCorrection = clip.rotationCorrection;
    row.start = drift::usToSeconds(clip.timelineStart);
    row.duration = drift::usToSeconds(clip.timelineDuration);
    row.inPoint = drift::usToSeconds(clip.srcIn);
    row.outPoint = drift::usToSeconds(clip.srcOut);
    row.sourceDuration = sourceAsset ? drift::usToSeconds(sourceAsset->durationUs) : 0.0;
    row.audioStreamIndex = clip.audioStreamIndex;
    // For the waveform bar: not once the audio has been separated, nor when the source has no
    // audio stream. An unprobed source gets the benefit of the doubt; its waveform stays empty.
    row.hasEmbeddedAudio = clip.type == drift::ClipType::Video && !clip.suppressEmbeddedAudio
        && (!sourceAsset || !sourceAsset->hasAudioKnown || sourceAsset->hasAudio);
    row.fadeIn = drift::usToSeconds(edgeHandleUs(clip, true));
    row.fadeOut = drift::usToSeconds(edgeHandleUs(clip, false));
    row.fadeCurve = drift::fadeCurveToString(clip.fadeCurve);
    // Only a Custom curve is drawn from these points, and the strip keys its fade canvases on
    // the list — so it has to be present, not populated.
    if (clip.fadeCurve == drift::FadeCurve::Custom) {
        for (const QPointF &pt : clip.fadeShape.points()) {
            row.fadeShape.append(QVariantMap{
                {QStringLiteral("t"), pt.x()},
                {QStringLiteral("g"), pt.y()},
            });
        }
    }
    row.fadeHandles = QVariantList{clip.fadeShape.handle1().x(), clip.fadeShape.handle1().y(),
                                   clip.fadeShape.handle2().x(), clip.fadeShape.handle2().y()};
    for (const drift::Effect &effect : videoHost.effects)
        row.effects.append(effectBadgeToMap(effect));
    for (const drift::Effect &effect : audioHost.audioEffects)
        row.audioEffects.append(audioEffectBadgeToMap(effect));
    row.fadeCurveTyped = clip.fadeCurve;
    row.fadeShapeTyped = clip.fadeShape;
    QStringList effectNames;
    for (const QVariantList *list : {&row.effects, &row.audioEffects}) {
        for (const QVariant &value : *list) {
            const QVariantMap fx = value.toMap();
            QString label = fx.value(QStringLiteral("label")).toString();
            if (label.isEmpty())
                label = tr("Effect");
            effectNames.append(fx.value(QStringLiteral("enabled"), true).toBool()
                                   ? label
                                   : tr("%1 (off)").arg(label));
        }
    }
    row.effectsLabel = effectNames.join(QStringLiteral(" · "));
    for (const auto &kv : clip.volume.keyframes().asKeyValueRange())
        row.volumeKeys.append(QVariantMap{
            {QStringLiteral("t"), drift::usToSeconds(kv.first)},
            {QStringLiteral("v"), kv.second.value},
        });
    row.volumeTyped = clip.volume;
    const bool carriesAudio = clip.type == drift::ClipType::Audio
                              || (clip.type == drift::ClipType::Video && row.hasEmbeddedAudio);
    if (carriesAudio && clip.volume.keyframes().size() <= 1) {
        const double gain = clip.volume.isEmpty() ? 1.0 : clip.volume.keyframes().constBegin()->value;
        if (gain <= 0.0) {
            row.volumeLabel = tr("-inf dB");
        } else if (const double db = 20.0 * std::log10(gain); std::abs(db) >= 0.05) {
            row.volumeLabel = tr("%1 dB").arg((db > 0.0 ? QStringLiteral("+") : QString())
                                              + QString::number(db, 'f', 1));
        }
    }
    return row;
}

void AppController::syncClipModels()
{
    const QList<drift::Track> &tracks = m_project.tracks();
    while (m_clipModels.size() < tracks.size())
        m_clipModels.append(new TimelineClipsModel(this));

    for (int ti = 0; ti < tracks.size(); ++ti) {
        const drift::Track &track = tracks.at(ti);

        // Each clip's effect stack lives on the adjustment linked to it in one of this track's
        // lanes. Gathered once per track rather than resolved per clip, which would be quadratic.
        QHash<QString, const drift::Clip *> videoHosts;
        QHash<QString, const drift::Clip *> audioHosts;
        const QList<int> laneIndexes = drift::adjustmentLaneIndexes(m_project, ti);
        if (!track.isAdjustment()) {
            for (const int laneIndex : laneIndexes) {
                for (const drift::Clip &adjustment : tracks.at(laneIndex).clips) {
                    if (adjustment.linkedClipId.isEmpty())
                        continue;
                    if (adjustment.adjustmentKind == drift::AdjustmentKind::VideoEffects)
                        videoHosts.insert(adjustment.linkedClipId, &adjustment);
                    else if (adjustment.adjustmentKind == drift::AdjustmentKind::AudioEffects)
                        audioHosts.insert(adjustment.linkedClipId, &adjustment);
                }
            }
        }

        QList<TimelineClipsModel::Row> rows;
        rows.reserve(track.clips.size());
        for (const drift::Clip &clip : track.clips) {
            rows.append(clipRow(clip, videoHosts.value(clip.id, nullptr),
                                audioHosts.value(clip.id, nullptr)));
        }
        m_clipModels[ti]->setRows(std::move(rows));
        m_clipModels[ti]->setDecorations(trackDecorations(track, laneIndexes));
    }

    // Slots past the end belong to tracks that are gone. Emptied rather than deleted — see the
    // note on m_clipModels.
    for (int ti = tracks.size(); ti < m_clipModels.size(); ++ti) {
        m_clipModels[ti]->setRows({});
        m_clipModels[ti]->setDecorations({});
    }
}

TimelineClipsModel::Decorations AppController::trackDecorations(const drift::Track &track,
                                                                const QList<int> &laneIndexes) const
{
    TimelineClipsModel::Decorations out;
    out.adjustmentLanes = laneIndexes;

    // Gaps are whatever time no clip covers between two neighbours in start order, plus the
    // stretch before the first clip when it does not start at zero — closeGap() at 0 pulls the
    // track to the start of the timeline. They are never stored, only derived.
    QList<const drift::Clip *> sorted;
    sorted.reserve(track.clips.size());
    for (const drift::Clip &clip : track.clips)
        sorted.append(&clip);
    std::stable_sort(sorted.begin(), sorted.end(), [](const drift::Clip *a, const drift::Clip *b) {
        return a->timelineStart < b->timelineStart;
    });
    if (!sorted.isEmpty() && sorted.first()->timelineStart > 0)
        out.gaps.append({0.0, drift::usToSeconds(sorted.first()->timelineStart)});
    for (int i = 0; i + 1 < sorted.size(); ++i) {
        const drift::TimeUs gapStart = sorted.at(i)->timelineEnd();
        const drift::TimeUs gapEnd = sorted.at(i + 1)->timelineStart;
        if (gapEnd > gapStart)
            out.gaps.append({drift::usToSeconds(gapStart), drift::usToSeconds(gapEnd)});
    }

    if (!trackAllowsTransitions(track.type))
        return out;

    // One region per clip that overlaps its transition partner or carries a transition to it —
    // the same partner the transition commands resolve, so a click lands on what is drawn.
    for (int left = 0; left < track.clips.size(); ++left) {
        const int right = findTransitionPartnerIndex(track, left);
        if (right < 0)
            continue;
        const drift::Clip &fromClip = track.clips.at(left);
        const drift::Clip &toClip = track.clips.at(right);
        const drift::Transition *transition = nullptr;
        for (const drift::Transition &candidate : track.transitions) {
            if (candidate.fromClipId == fromClip.id && candidate.toClipId == toClip.id) {
                transition = &candidate;
                break;
            }
        }

        TimelineClipsModel::TransitionRegion region;
        region.leftClip = left;
        region.rightClip = right;
        drift::TimeUs startUs = 0;
        drift::TimeUs endUs = 0;
        if (transition) {
            if (!drift::transitionWindow(track, *transition, startUs, endUs))
                continue;
            region.hasTransition = true;
            const TransitionPresetEntry *def = transitionDefForId(transition->kindId);
            region.label = def ? def->meta.displayName : transition->kindId;
            region.kind = transition->kindId;
        } else if (drift::clipsPhysicallyOverlap(fromClip, toClip)) {
            startUs = toClip.timelineStart;
            endUs = fromClip.timelineEnd();
        } else {
            continue;
        }
        if (endUs <= startUs)
            continue;
        region.start = drift::usToSeconds(startUs);
        region.end = drift::usToSeconds(endUs);
        region.overlap = drift::clipsPhysicallyOverlap(fromClip, toClip);
        const drift::TimeUs shorterUs = qMin(fromClip.timelineDuration, toClip.timelineDuration);
        region.maxDuration =
            drift::usToSeconds(region.overlap ? shorterUs - drift::secondsToUs(0.05) : shorterUs);
        if (transition)
            region.transitionId = transition->id;
        out.transitions.append(region);
    }
    return out;
}

QVariantList AppController::timelineOverviewBlocks() const
{
    QVariantList out;
    int lane = 0;
    for (const drift::Track &track : m_project.tracks()) {
        // An adjustment lane sits inside its parent's row, so at minimap scale a band of its own
        // would only repeat the clip it is pinned to, one row down.
        if (track.isAdjustmentLane())
            continue;
        for (const drift::Clip &clip : track.clips) {
            if (clip.timelineDuration <= 0)
                continue;
            out.append(lane);
            out.append(static_cast<int>(clip.type));
            out.append(drift::usToSeconds(clip.timelineStart));
            out.append(drift::usToSeconds(clip.timelineDuration));
        }
        ++lane;
    }
    return out;
}

int AppController::timelineOverviewLaneCount() const
{
    int lanes = 0;
    for (const drift::Track &track : m_project.tracks()) {
        if (!track.isAdjustmentLane())
            ++lanes;
    }
    return lanes;
}

QObject *AppController::clipsModel(int trackIndex) const
{
    if (trackIndex < 0)
        return nullptr;
    // A Repeater asks for its model as the delegate is built, which can be a turn ahead of the
    // sync that fills it; minting the slot here keeps the pointer stable either way.
    auto *self = const_cast<AppController *>(this);
    while (self->m_clipModels.size() <= trackIndex)
        self->m_clipModels.append(new TimelineClipsModel(self));
    return m_clipModels.at(trackIndex);
}

int AppController::clipCountForAsset(int assetIndex) const
{
    if (!m_assetLibrary)
        return 0;
    const QString assetId = m_assetLibrary->assetIdAt(assetIndex);
    if (assetId.isEmpty())
        return 0;

    // Inside composites too: removing media a composite still plays would orphan its clips.
    int count = 0;
    m_project.forEachTrackList([&](const QList<drift::Track> &tracks) {
        for (const drift::Track &track : tracks) {
            for (const drift::Clip &clip : track.clips) {
                if (clip.assetId == assetId)
                    ++count;
            }
        }
    });
    return count;
}

QHash<QString, int> AppController::clipCountsByAsset() const
{
    QHash<QString, int> counts;
    m_project.forEachTrackList([&](const QList<drift::Track> &tracks) {
        for (const drift::Track &track : tracks) {
            for (const drift::Clip &clip : track.clips) {
                if (!clip.assetId.isEmpty())
                    ++counts[clip.assetId];
            }
        }
    });
    return counts;
}

bool AppController::removeAsset(int assetIndex)
{
    // Clips keep their own copy of the source path, so an orphaned assetId
    // would still play but silently lose trim-past-the-cut, merge and
    // separate-audio. Refuse instead; the caller reports the usage count.
    if (!m_assetLibrary || clipCountForAsset(assetIndex) > 0)
        return false;

    const QString removedPath =
        m_assetLibrary->assetAt(assetIndex).value(QStringLiteral("path")).toString();

    const drift::Project before = m_project;
    if (!m_assetLibrary->removeAssetAt(assetIndex))
        return false;

    if (!removedPath.isEmpty())
        m_waveformBlocks.forgetSource(removedPath);
    pruneOrphanSequences();

    // Rows after the removed one shift down, so any index captured at drag
    // start now points at the wrong asset.
    setDraggingAssetIndex(-1);
    pushProjectEdit(before, tr("Media removed"));
    settleUndoRefresh();
    return true;
}

int AppController::removeAssets(const QStringList &assetIds)
{
    if (!m_assetLibrary || assetIds.isEmpty())
        return 0;

    // Refuse the whole batch if any id is still referenced by a clip — checked up front so
    // this stays atomic (no partial removal) and holds even for a caller that bypasses the
    // QML confirmation flow's own in-use check (AssetsPanel.qml's requestRemoveAsset).
    for (const QString &id : assetIds) {
        const int index = m_assetLibrary->indexOfId(id);
        if (index >= 0 && clipCountForAsset(index) > 0)
            return 0;
    }

    const drift::Project before = m_project;
    int removed = 0;
    for (const QString &id : assetIds) {
        // Resolved fresh per id rather than upfront: removeAssetAt shifts every row after the
        // one it removes, so an index captured before the loop started would drift out from
        // under the ids that come after it.
        const int index = m_assetLibrary->indexOfId(id);
        if (index < 0)
            continue;
        const QString removedPath =
            m_assetLibrary->assetAt(index).value(QStringLiteral("path")).toString();
        if (m_assetLibrary->removeAssetAt(index)) {
            ++removed;
            if (!removedPath.isEmpty())
                m_waveformBlocks.forgetSource(removedPath);
        }
    }
    if (removed == 0)
        return 0;
    pruneOrphanSequences();

    setDraggingAssetIndex(-1);
    pushProjectEdit(before, removed == 1 ? tr("Media removed") : tr("%n items removed", "", removed));
    settleUndoRefresh();
    return removed;
}

int AppController::removeAssetsAndClips(const QStringList &assetIds)
{
    if (!m_assetLibrary || assetIds.isEmpty())
        return 0;

    QSet<QString> targetAssetIds;
    for (const QString &id : assetIds) {
        if (m_assetLibrary->indexOfId(id) >= 0)
            targetAssetIds.insert(id);
    }

    if (targetAssetIds.isEmpty())
        return 0;

    // Snapshot before touching either the timeline or the asset library. AssetLibrary
    // mirrors the project's asset collection, so the ProjectSnapshotCommand restores
    // the complete operation in one undo step.
    const drift::Project before = m_project.detachedCopy();

    QSet<QString> removedClipIds;
    int removedClips = 0;

    // Remove backwards so QList indices remain valid. Every timeline, composites included.
    m_project.forEachTrackList([&](QList<drift::Track> &tracks) {
        for (drift::Track &track : tracks) {
            for (int i = track.clips.size() - 1; i >= 0; --i) {
                const drift::Clip &clip = track.clips.at(i);
                if (!targetAssetIds.contains(clip.assetId))
                    continue;

                removedClipIds.insert(clip.id);
                track.clips.removeAt(i);
                ++removedClips;
            }
        }

        // A transition may reference a clip that has just disappeared. Keep the same
        // invariant enforced by deleteSelectedClip().
        if (removedClipIds.isEmpty())
            return;
        for (drift::Track &track : tracks) {
            for (int i = track.transitions.size() - 1; i >= 0; --i) {
                const drift::Transition &transition = track.transitions.at(i);
                if (removedClipIds.contains(transition.fromClipId)
                    || removedClipIds.contains(transition.toClipId)) {
                    track.transitions.removeAt(i);
                }
            }
        }
    });

    int removedAssets = 0;
    for (const QString &id : targetAssetIds) {
        const int index = m_assetLibrary->indexOfId(id);
        if (index >= 0 && m_assetLibrary->removeAssetAt(index))
            ++removedAssets;
    }

    if (removedAssets == 0) {
        // Defensive rollback. Normally impossible because all ids were resolved before
        // mutation, but do not leave timeline edits behind if the asset operation fails.
        m_project = before;
        m_assetLibrary->syncToProject();
        return 0;
    }

    pruneOrphanSequences();
    setDraggingAssetIndex(-1);
    clearSelection();

    pushProjectEdit(
        before,
        removedAssets == 1
            ? tr("Media and referenced clips removed")
            : tr("%n media items and referenced clips removed", "", removedAssets));

    finishEdit(
        removedClips == 1
            ? tr("Media and referenced clip removed")
            : tr("Media and referenced clips removed"));

    return removedAssets;
}

bool AppController::renameAsset(int assetIndex, const QString &name)
{
    if (!m_assetLibrary)
        return false;

    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty())
        return false;

    const QVariantMap current = m_assetLibrary->assetAt(assetIndex);
    if (current.isEmpty() || current.value(QStringLiteral("name")).toString() == trimmed)
        return false;

    const drift::Project before = m_project.detachedCopy();
    if (!m_assetLibrary->setAssetName(assetIndex, trimmed))
        return false;

    pushProjectEdit(before, tr("Rename media"));
    finishEdit(tr("Media renamed"));
    // Composite names also label the timeline switcher.
    emit sequenceTabsChanged();
    return true;
}

bool AppController::setAssetRotation(int assetIndex, int degrees)
{
    if (!m_assetLibrary)
        return false;

    const drift::Project before = m_project;
    if (!m_assetLibrary->setAssetRotation(assetIndex, degrees))
        return false;

    pushProjectEdit(before, tr("Media rotated"));
    settleUndoRefresh();
    return true;
}

void AppController::writeClipSourceFrame(int t, int c, const QRectF &frame, double turn)
{
    const QRectF previousFrame = m_project.tracks().at(t).clips.at(c).sourceFrame;
    auto &clip = m_project.tracks()[t].clips[c];
    clip.sourceFrame = frame;
    clip.sourceFrameAngle = turn;
    if (const auto *asset = m_project.asset(clip.assetId)) {
        // Displayed orientation: the file's own tag plus this clip's correction, as decoded.
        int sw = asset->width, sh = asset->height;
        if (qAbs(asset->rotationDegrees + clip.rotationCorrection) % 180 == 90)
            std::swap(sw, sh);
        if (sw > 0 && sh > 0) {
            const auto fittedSize = [&](const QRectF &r) {
                QSizeF size(sw * r.width(), sh * r.height());
                size.scale(QSizeF(m_project.width(), m_project.height()), Qt::KeepAspectRatio);
                return size;
            };
            const QSizeF previous = fittedSize(previousFrame), next = fittedSize(frame);
            const auto scaleTrack = [](drift::KeyframeTrack<double> &track, double ratio) {
                const auto keys = track.keyframes();
                for (auto it = keys.cbegin(); it != keys.cend(); ++it) {
                    auto key = it.value();
                    key.value *= ratio;
                    key.inDy *= ratio;
                    key.outDy *= ratio;
                    track.setKeyframe(it.key(), key);
                }
            };
            scaleTrack(clip.transformW, next.width() / previous.width());
            scaleTrack(clip.transformH, next.height() / previous.height());
        }
    }
}

bool AppController::setClipSourceFrame(const QString &clipId, double x, double y, double w, double h,
                                       double angle)
{
    const QRectF frame = drift::normalizedSourceFrame(x, y, w, h);
    for (int t = 0; t < m_project.tracks().size(); ++t) {
        for (int c = 0; c < m_project.tracks().at(t).clips.size(); ++c) {
            const auto &old = m_project.tracks().at(t).clips.at(c);
            if (old.id != clipId
                || (old.type != drift::ClipType::Video && old.type != drift::ClipType::Image))
                continue;
            const double turn = std::isnan(angle) ? old.sourceFrameAngle : drift::normalizedSourceAngle(angle);
            if (old.sourceFrame == frame && old.sourceFrameAngle == turn)
                return true;
            const drift::Project before = m_project.detachedCopy();
            writeClipSourceFrame(t, c, frame, turn);
            pushProjectEdit(before, tr("Crop clip"));
            finishEdit(tr("Crop saved"));
            return true;
        }
    }
    return false;
}

int AppController::assetIndexForClip(const drift::Clip &clip) const
{
    if (clip.assetId.isEmpty())
        return -1;
    return m_project.assetIndex(clip.assetId);
}

double AppController::playheadSeconds() const
{
    return drift::usToSeconds(m_playheadUs);
}

double AppController::durationSeconds() const
{
    if (!m_durationCacheValid) {
        m_durationSecondsCache = drift::usToSeconds(m_project.durationUs());
        m_durationCacheValid = true;
    }
    return m_durationSecondsCache;
}

QVariantMap AppController::mediaExtentSeconds() const
{
    drift::TimeUs start = std::numeric_limits<drift::TimeUs>::max();
    drift::TimeUs end = 0;
    for (const drift::Track &track : m_project.tracks()) {
        if (track.type != drift::TrackType::Video && track.type != drift::TrackType::Audio)
            continue;
        for (const drift::Clip &clip : track.clips) {
            if (clip.timelineDuration <= 0)
                continue;
            start = qMin(start, clip.timelineStart);
            end = qMax(end, clip.timelineStart + clip.timelineDuration);
        }
    }
    if (end <= start) {
        return {{QStringLiteral("start"), 0.0}, {QStringLiteral("duration"), 0.0}};
    }
    return {
        {QStringLiteral("start"), drift::usToSeconds(start)},
        {QStringLiteral("duration"), drift::usToSeconds(end - start)},
    };
}


QVariantMap AppController::selectedClipData() const
{
    if (m_selectedTrack < 0 || m_selectedClip < 0)
        return {};

    auto enrich = [this](QVariantMap data, int trackIndex, int clipIndex) -> QVariantMap {
        if (data.isEmpty() || !isValidClipIndex(trackIndex, clipIndex))
            return data;
        const drift::Clip &clip = m_project.tracks().at(trackIndex).clips.at(clipIndex);
        data.insert(QStringLiteral("caps"),
                    inspectorCaps(clip, m_project.tracks().at(trackIndex).type));
        return data;
    };

    QVariantMap data = enrich(clipAt(m_selectedTrack, m_selectedClip), m_selectedTrack, m_selectedClip);
    if (!data.isEmpty())
        return data;

    // Primary indices can lag m_selection after structural edits; fall back.
    for (const QPair<int, int> &pair : m_selection) {
        data = enrich(clipAt(pair.first, pair.second), pair.first, pair.second);
        if (!data.isEmpty())
            return data;
    }
    return {};
}

double AppController::selectedClipRotationAtPlayhead() const
{
    auto rotation = [this](int trackIndex, int clipIndex) {
        const drift::Clip &clip = m_project.tracks().at(trackIndex).clips.at(clipIndex);
        const drift::TimeUs relative = qMax<drift::TimeUs>(0, m_playheadUs - clip.timelineStart);
        return clipTransformValue(clip.rotation, relative, 0.0);
    };
    if (isValidClipIndex(m_selectedTrack, m_selectedClip))
        return rotation(m_selectedTrack, m_selectedClip);
    for (const QPair<int, int> &pair : m_selection) {
        if (isValidClipIndex(pair.first, pair.second))
            return rotation(pair.first, pair.second);
    }
    return 0.0;
}

QVariantList AppController::selectedClipEffects() const
{
    const QVariantMap clip = selectedClipData();
    return clip.value(QStringLiteral("effects")).toList();
}

QVariantList AppController::selectedClipAudioEffects() const
{
    const QVariantMap clip = selectedClipData();
    return clip.value(QStringLiteral("audioEffects")).toList();
}

QVariantList AppController::selection() const
{
    QVariantList out;
    for (const QPair<int, int> &pair : m_selection) {
        out.append(QVariantMap{
            {QStringLiteral("track"), pair.first},
            {QStringLiteral("clip"), pair.second},
        });
    }
    return out;
}

QVariantList AppController::actions() const
{
    auto action = [this](const QString &id, const QString &label) {
        return QVariantMap{
            {QStringLiteral("id"), id},
            {QStringLiteral("label"), label},
            {QStringLiteral("shortcut"), m_shortcuts.value(id)},
        };
    };

    return {
        action(QStringLiteral("newProject"), tr("New project")),
        action(QStringLiteral("open"), tr("Open project")),
        action(QStringLiteral("save"), tr("Save project")),
        action(QStringLiteral("saveAs"), tr("Save project as…")),
        action(QStringLiteral("playPause"), tr("Play/Pause")),
        action(QStringLiteral("delete"), tr("Delete selection")),
        action(QStringLiteral("undo"), tr("Undo")),
        action(QStringLiteral("redo"), tr("Redo")),
        action(QStringLiteral("copy"), tr("Copy selection")),
        action(QStringLiteral("cut"), tr("Cut selection")),
        action(QStringLiteral("paste"), tr("Paste at current time")),
        action(QStringLiteral("duplicate"), tr("Duplicate selected clip")),
        action(QStringLiteral("copyEffects"), tr("Copy effects from clip")),
        action(QStringLiteral("pasteEffects"), tr("Paste effects onto clip")),
        action(QStringLiteral("pasteAttributes"), tr("Paste attributes…")),
        action(QStringLiteral("split"), tr("Split at current time")),
        action(QStringLiteral("merge"), tr("Merge adjacent clips")),
        action(QStringLiteral("transformTogether"), tr("Transform selection together")),
        action(QStringLiteral("selectTransformLayer"), tr("Select transform layer")),
        action(QStringLiteral("separateAudio"), tr("Separate audio")),
        action(QStringLiteral("unlink"), tr("Unlink audio")),
        action(QStringLiteral("link"), tr("Link clips")),
        action(QStringLiteral("clearSelection"), tr("Clear selection")),
        action(QStringLiteral("selectAll"), tr("Select all clips")),
        action(QStringLiteral("nudgeLeft"), tr("Move selection left a little")),
        action(QStringLiteral("nudgeRight"), tr("Move selection right a little")),
        action(QStringLiteral("previousEdit"), tr("Go to previous cut point")),
        action(QStringLiteral("nextEdit"), tr("Go to next cut point")),
        action(QStringLiteral("stepBack"), tr("Step back one frame")),
        action(QStringLiteral("stepForward"), tr("Step forward one frame")),
        action(QStringLiteral("jumpBack"), tr("Jump back 1 second")),
        action(QStringLiteral("jumpForward"), tr("Jump forward 1 second")),
        action(QStringLiteral("jumpBackFar"), tr("Jump back 10 seconds")),
        action(QStringLiteral("jumpForwardFar"), tr("Jump forward 10 seconds")),
        action(QStringLiteral("goToStart"), tr("Go to start of timeline")),
        action(QStringLiteral("goToEnd"), tr("Go to end of timeline")),
        action(QStringLiteral("deleteLeft"), tr("Delete left of the playhead")),
        action(QStringLiteral("deleteRight"), tr("Delete right of the playhead")),
        action(QStringLiteral("speedUp"), tr("Increase playback speed")),
        action(QStringLiteral("speedDown"), tr("Decrease playback speed")),
        action(QStringLiteral("toggleGuides"), tr("Toggle guides")),
        action(QStringLiteral("gizmoMove"), tr("3D gizmo: move / Tracker tool")),
        action(QStringLiteral("gizmoRotate"), tr("3D gizmo: rotate / Rectangle mask")),
        action(QStringLiteral("gizmoScale"), tr("3D gizmo: scale / Ellipse mask")),
        action(QStringLiteral("gizmoOrientation"), tr("3D gizmo: cycle world/local/view axes")),
        action(QStringLiteral("toggleBookmark"), tr("Add/remove bookmark at current time")),
        action(QStringLiteral("nextBookmark"), tr("Go to next bookmark")),
        action(QStringLiteral("previousBookmark"), tr("Go to previous bookmark")),
        action(QStringLiteral("markIn"), tr("Mark work area in")),
        action(QStringLiteral("markOut"), tr("Mark work area out")),
        action(QStringLiteral("goToIn"), tr("Go to work area in")),
        action(QStringLiteral("goToOut"), tr("Go to work area out")),
        action(QStringLiteral("clearInOut"), tr("Clear work area")),
        action(QStringLiteral("toggleLoop"), tr("Loop work area playback")),
        action(QStringLiteral("selectTool"), tr("Select tool")),
        action(QStringLiteral("bladeTool"), tr("Cut tool / Brush mask")),
        action(QStringLiteral("maskingPolygonTool"), tr("Polygon mask tool")),
        action(QStringLiteral("maskingAiTool"), tr("AI object mask tool")),
        action(QStringLiteral("maskingBrushSmaller"), tr("Decrease brush size")),
        action(QStringLiteral("maskingBrushLarger"), tr("Increase brush size")),
        action(QStringLiteral("maskingBrushToggleErase"), tr("Brush: switch paint / erase")),
        action(QStringLiteral("trackForward"), tr("Track forward")),
        action(QStringLiteral("trackBackward"), tr("Track backward")),
        action(QStringLiteral("trackFrameForward"), tr("Track one frame forward")),
        action(QStringLiteral("trackFrameBackward"), tr("Track one frame backward")),
        action(QStringLiteral("trackBoth"), tr("Track whole clip")),
        action(QStringLiteral("zoomIn"), tr("Zoom in")),
        action(QStringLiteral("zoomOut"), tr("Zoom out")),
        action(QStringLiteral("multicam"), tr("Multicam window")),
        action(QStringLiteral("search"), tr("Search…")),
        action(QStringLiteral("workspace.restoreWindows"), tr("Restore Workspace Windows")),
        action(QStringLiteral("workspace.next"), tr("Next workspace")),
        action(QStringLiteral("workspace.prev"), tr("Previous workspace")),
        action(QStringLiteral("workspace.1"), tr("Workspace 1")),
        action(QStringLiteral("workspace.2"), tr("Workspace 2")),
        action(QStringLiteral("workspace.3"), tr("Workspace 3")),
        action(QStringLiteral("workspace.4"), tr("Workspace 4")),
        action(QStringLiteral("workspace.5"), tr("Workspace 5")),
        action(QStringLiteral("workspace.6"), tr("Workspace 6")),
        action(QStringLiteral("workspace.7"), tr("Workspace 7")),
        action(QStringLiteral("workspace.8"), tr("Workspace 8")),
        action(QStringLiteral("workspace.9"), tr("Workspace 9")),
    };
}

void AppController::setPlayheadUs(drift::TimeUs us)
{
    const drift::perf::Scope perfScope("seek.gui");
    const drift::TimeUs clamped = qBound<drift::TimeUs>(0, us, qMax(m_project.durationUs(), drift::TimeUs{0}));
    if (m_playheadUs == clamped)
        return;

    m_playheadUs = clamped;
    {
        const drift::perf::Scope engineScope("seek.engine");
        m_playback.setPlayheadUs(clamped);
    }
    {
        const drift::perf::Scope emitScope("seek.emit");
        emit playheadSecondsChanged();
    }
}

void AppController::setPlayheadSeconds(double seconds)
{
    setPlayheadUs(drift::secondsToUs(seconds));
}

void AppController::setPlaying(bool playing)
{
    if (m_playing == playing)
        return;

    // Sliders and preview handles lock while playing; land the drag in progress while the
    // commit's seek is still legal.
    if (playing && m_previewDragActive)
        commitPreviewDrag();

    if (playing)
        endScrub();
    m_playing = playing;
    if (m_playing) {
        const drift::TimeUs durationUs = m_project.durationUs();
        if (m_playback.hasPreviewRange()) {
            if (m_playheadUs >= m_playback.previewRangeEndUs() || m_playheadUs < m_playback.previewRangeStartUs())
                setPlayheadUs(m_playback.previewRangeStartUs());
        } else if (m_loopWorkAreaEnabled && m_project.hasWorkArea()) {
            const drift::TimeUs loopIn = m_project.workAreaInUs();
            const drift::TimeUs loopOut = m_project.workAreaOutUs();
            if (m_playheadUs >= loopOut || m_playheadUs < loopIn)
                setPlayheadUs(loopIn);
        } else if (!m_playback.isVoiceoverRecording() && m_playheadUs >= durationUs && durationUs > 0) {
            setPlayheadUs(0);
        }
        m_playback.setPlayheadUs(m_playheadUs);
        m_playback.play();
    } else {
        if (m_audioRecorder.isRecording()) {
            stopAudioRecording();
            return;
        }
        m_playback.pause();
    }
    emit playingChanged();
}

void AppController::beginScrub()
{
    m_playback.beginScrub();
    if (m_scrubbing)
        return;
    m_scrubbing = true;
    emit scrubbingChanged();
}

void AppController::endScrub()
{
    m_playback.endScrub();
    if (!m_scrubbing)
        return;
    m_scrubbing = false;
    emit scrubbingChanged();
    // The throttle may have swallowed the last position of the gesture.
    emit inspectorPlayheadChanged();
}

void AppController::togglePlayback()
{
    setPlaying(!m_playback.isPlaying());
}

void AppController::stepFrames(int frames)
{
    if (frames == 0)
        return;

    // Stepping is a paused-only operation: leaving playback running would have the clock overwrite
    // the stepped position on its next tick.
    setPlaying(false);

    const drift::TimeUs step = drift::frameDurationUs(m_projectFile->projectFps());
    const int64_t frame = (m_playheadUs + step / 2) / step;
    setPlayheadUs((frame + frames) * step);
}

void AppController::jumpSeconds(double seconds)
{
    setPlayheadUs(m_playheadUs + drift::secondsToUs(seconds));
}

int AppController::keyboardModifiers() const
{
    return static_cast<int>(QGuiApplication::keyboardModifiers());
}

void AppController::setSnapEnabled(bool enabled)
{
    if (m_snapEnabled == enabled)
        return;

    m_snapEnabled = enabled;
    emit snapEnabledChanged();
}

void AppController::setRippleEnabled(bool enabled)
{
    if (m_rippleEnabled == enabled)
        return;
    m_rippleEnabled = enabled;
    emit rippleEnabledChanged();
}

void AppController::setAllowClipOverlap(bool enabled)
{
    if (m_allowClipOverlap == enabled)
        return;
    m_allowClipOverlap = enabled;
    emit allowClipOverlapChanged();
}

void AppController::setMediaGridMode(bool enabled)
{
    setMediaViewMode(enabled ? QStringLiteral("grid") : QStringLiteral("list"));
}

void AppController::setMediaViewMode(const QString &mode)
{
    // An unknown mode (an older build's project, a bad MCP argument) falls back to the
    // grid rather than leaving the bin with nothing to render.
    const QString next = (mode == QLatin1String("list") || mode == QLatin1String("tree"))
                             ? mode
                             : QStringLiteral("grid");
    if (m_mediaViewMode == next)
        return;
    m_mediaViewMode = next;
    emit mediaViewModeChanged();
    emit mediaGridModeChanged();
}

void AppController::setAutoKeyEnabled(bool enabled)
{
    if (m_autoKeyEnabled == enabled)
        return;
    m_autoKeyEnabled = enabled;
    QSettings settings;
    settings.setValue(QStringLiteral("editor/autoKeyEnabled"), m_autoKeyEnabled);
    emit autoKeyEnabledChanged();
}

void AppController::setPositionLinked(bool linked)
{
    if (m_positionLinked == linked)
        return;
    m_positionLinked = linked;
    emit positionLinkedChanged();
}

void AppController::setSizeLinked(bool linked)
{
    if (m_sizeLinked == linked)
        return;
    m_sizeLinked = linked;
    emit sizeLinkedChanged();
}

double AppController::masterVolume() const
{
    return m_playback.masterVolume();
}

void AppController::setMasterVolume(double volume)
{
    m_playback.setMasterVolume(qMax(0.0, volume));
    emit masterVolumeChanged();
}

bool AppController::masterMuted() const
{
    return m_playback.masterMuted();
}

void AppController::setMasterMuted(bool muted)
{
    m_playback.setMasterMuted(muted);
    emit masterMutedChanged();
}

void AppController::toggleKeyframeGraphPropertyVisible(const QString &prop)
{
    const QString key = normalizeKeyframeProp(prop);
    if (!isKnownKeyframeProp(key))
        return;
    if (m_keyframeGraphHiddenProperties.contains(key))
        m_keyframeGraphHiddenProperties.removeAll(key);
    else
        m_keyframeGraphHiddenProperties.append(key);
    emit keyframeGraphVisibilityChanged();
}

void AppController::showKeyframeGraphProperty(const QString &prop)
{
    const QString key = normalizeKeyframeProp(prop);
    if (!m_keyframeGraphHiddenProperties.removeAll(key))
        return;
    emit keyframeGraphVisibilityChanged();
}

// Effect props are addressed by index, so removing an effect would leave a hidden flag attached to
// some other effect's parameter. Drop the removed effect's entries and renumber everything above it.
// `audio` picks the stack: "afx." props for the audio one, "fx." for the video one.
void AppController::dropKeyframeGraphPropertiesForEffect(int removedIndex, bool audio)
{
    const auto parse = audio ? parseAudioEffectProp : parseEffectProp;
    const QString format = audio ? QStringLiteral("afx.%1.%2") : QStringLiteral("fx.%1.%2");
    QStringList next;
    for (const QString &prop : std::as_const(m_keyframeGraphHiddenProperties)) {
        int effectIndex = -1;
        QString paramKey;
        if (!parse(prop, &effectIndex, &paramKey)) {
            next.append(prop);
            continue;
        }
        if (effectIndex == removedIndex)
            continue;
        next.append(effectIndex > removedIndex ? format.arg(effectIndex - 1).arg(paramKey) : prop);
    }
    if (next == m_keyframeGraphHiddenProperties)
        return;
    m_keyframeGraphHiddenProperties = next;
    emit keyframeGraphVisibilityChanged();
}

// Moving an effect between slots is the same addressing problem as remove: rewrite every
// fx.N.* entry so the hidden set still points at the same parameters after the swap.
void AppController::remapKeyframeGraphPropertiesForEffectMove(int fromIndex, int toIndex, bool audio)
{
    if (fromIndex == toIndex)
        return;
    const auto parse = audio ? parseAudioEffectProp : parseEffectProp;
    const QString format = audio ? QStringLiteral("afx.%1.%2") : QStringLiteral("fx.%1.%2");
    QStringList next;
    next.reserve(m_keyframeGraphHiddenProperties.size());
    for (const QString &prop : std::as_const(m_keyframeGraphHiddenProperties)) {
        int effectIndex = -1;
        QString paramKey;
        if (!parse(prop, &effectIndex, &paramKey)) {
            next.append(prop);
            continue;
        }
        int mapped = effectIndex;
        if (effectIndex == fromIndex) {
            mapped = toIndex;
        } else if (fromIndex < toIndex) {
            // Shifted left toward the vacated slot.
            if (effectIndex > fromIndex && effectIndex <= toIndex)
                mapped = effectIndex - 1;
        } else {
            // Shifted right toward the vacated slot.
            if (effectIndex >= toIndex && effectIndex < fromIndex)
                mapped = effectIndex + 1;
        }
        next.append(mapped == effectIndex ? prop : format.arg(mapped).arg(paramKey));
    }
    if (next == m_keyframeGraphHiddenProperties)
        return;
    m_keyframeGraphHiddenProperties = next;
    emit keyframeGraphVisibilityChanged();
}

void AppController::setSubtitleEditing(bool editing)
{
    if (m_subtitleEditing == editing)
        return;
    m_subtitleEditing = editing;
    emit subtitleEditingChanged();
}

void AppController::setSelectedSubtitleCue(int index)
{
    if (m_selectedSubtitleCue == index)
        return;
    m_selectedSubtitleCue = index;
    emit selectedSubtitleCueChanged();
}

void AppController::setDraggingAssetIndex(int index)
{
    if (m_draggingAssetIndex == index)
        return;
    m_draggingAssetIndex = index;
    emit draggingAssetIndexChanged();
}

const drift::GuideSet *AppController::findGuideSet(const QString &id) const
{
    for (const QList<drift::GuideSet> *list : {&drift::builtInGuideSets(), &m_guideLibrary, &m_projectGuideSets}) {
        for (const drift::GuideSet &set : *list) {
            if (set.id == id)
                return &set;
        }
    }
    return nullptr;
}

static QVariantMap guideItemToVariant(const drift::GuideItem &item)
{
    return {
        {QStringLiteral("id"), item.id},
        {QStringLiteral("kind"), drift::guideKindToString(item.kind)},
        {QStringLiteral("pos"), item.pos},
        {QStringLiteral("left"), item.left},
        {QStringLiteral("top"), item.top},
        {QStringLiteral("right"), item.right},
        {QStringLiteral("bottom"), item.bottom},
        {QStringLiteral("aspectW"), item.aspectW},
        {QStringLiteral("aspectH"), item.aspectH},
        {QStringLiteral("aspect"), item.aspectW / item.aspectH},
        {QStringLiteral("color"), item.color.name(QColor::HexRgb)},
        {QStringLiteral("opacity"), item.opacity},
        {QStringLiteral("locked"), item.locked},
    };
}

QVariantList AppController::guideSets() const
{
    QVariantList out;
    QSet<QString> seen;
    for (const QList<drift::GuideSet> *list : {&drift::builtInGuideSets(), &m_guideLibrary, &m_projectGuideSets}) {
        for (const drift::GuideSet &set : *list) {
            if (seen.contains(set.id))
                continue;
            seen.insert(set.id);
            QVariantList items;
            for (const drift::GuideItem &item : set.items)
                items.append(guideItemToVariant(item));
            out.append(QVariantMap{
                {QStringLiteral("id"), set.id},
                {QStringLiteral("name"), set.builtIn ? QCoreApplication::translate("GuideSet", set.name.toUtf8().constData())
                                                     : set.name},
                {QStringLiteral("builtIn"), set.builtIn},
                {QStringLiteral("active"), m_activeGuideSets.contains(set.id)},
                {QStringLiteral("inLibrary"), list == &m_guideLibrary},
                {QStringLiteral("items"), items},
            });
        }
    }
    return out;
}

QVariantList AppController::guideItems() const
{
    QVariantList out;
    for (const QString &id : m_activeGuideSets) {
        const drift::GuideSet *set = findGuideSet(id);
        if (!set)
            continue;
        for (const drift::GuideItem &item : set->items)
            out.append(guideItemToVariant(item));
    }
    return out;
}

void AppController::setGuideSetActive(const QString &id, bool active)
{
    if (!findGuideSet(id) || m_activeGuideSets.contains(id) == active)
        return;
    if (active)
        m_activeGuideSets.append(id);
    else
        m_activeGuideSets.removeAll(id);
    if (!active && m_preview->guideEditSetId() == id)
        m_preview->setGuideEditSetId(QString());
    QSettings().setValue(QStringLiteral("preview/activeGuideSets"), m_activeGuideSets);
    m_projectFile->setDirty(true);
    emit guidesChanged();
}

drift::GuideSet *AppController::libraryGuideSet(const QString &id)
{
    for (drift::GuideSet &set : m_guideLibrary) {
        if (set.id == id)
            return &set;
    }
    return nullptr;
}

void AppController::guideLibraryEdited(const QString &id)
{
    QJsonArray sets;
    for (const drift::GuideSet &set : m_guideLibrary)
        sets.append(drift::guideSetToJson(set));
    QSettings().setValue(QStringLiteral("preview/guideSets"), QJsonDocument(sets).toJson(QJsonDocument::Compact));
    if (m_activeGuideSets.contains(id))
        m_projectFile->setDirty(true);
    emit guidesChanged();
}

static QString newGuideId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString AppController::createGuideSet(const QString &name)
{
    drift::GuideSet set;
    set.id = newGuideId();
    set.name = name.trimmed().isEmpty() ? tr("Custom guides") : name.trimmed();
    m_guideLibrary.append(set);
    m_activeGuideSets.append(set.id);
    QSettings().setValue(QStringLiteral("preview/activeGuideSets"), m_activeGuideSets);
    guideLibraryEdited(set.id);
    return set.id;
}

QString AppController::duplicateGuideSet(const QString &id)
{
    const drift::GuideSet *source = findGuideSet(id);
    if (!source)
        return {};
    drift::GuideSet copy = *source;
    copy.id = newGuideId();
    copy.name = tr("%1 copy").arg(source->builtIn ? QCoreApplication::translate("GuideSet", source->name.toUtf8().constData())
                                                  : source->name);
    copy.builtIn = false;
    m_guideLibrary.append(copy);
    // The copy takes the original's place, so duplicating to edit leaves the preview unchanged.
    const int at = m_activeGuideSets.indexOf(id);
    if (at >= 0)
        m_activeGuideSets[at] = copy.id;
    else
        m_activeGuideSets.append(copy.id);
    QSettings().setValue(QStringLiteral("preview/activeGuideSets"), m_activeGuideSets);
    guideLibraryEdited(copy.id);
    return copy.id;
}

void AppController::renameGuideSet(const QString &id, const QString &name)
{
    drift::GuideSet *set = libraryGuideSet(id);
    if (!set || name.trimmed().isEmpty() || set->name == name.trimmed())
        return;
    set->name = name.trimmed();
    guideLibraryEdited(id);
}

void AppController::deleteGuideSet(const QString &id)
{
    const auto removed = std::remove_if(m_guideLibrary.begin(), m_guideLibrary.end(),
                                        [&](const drift::GuideSet &set) { return set.id == id; });
    if (removed == m_guideLibrary.end())
        return;
    m_guideLibrary.erase(removed, m_guideLibrary.end());
    if (m_preview->guideEditSetId() == id)
        m_preview->setGuideEditSetId(QString());
    // Otherwise the project's copy would bring it straight back.
    m_projectGuideSets.removeIf([&](const drift::GuideSet &set) { return set.id == id; });
    if (m_activeGuideSets.removeAll(id) > 0) {
        QSettings().setValue(QStringLiteral("preview/activeGuideSets"), m_activeGuideSets);
        m_projectFile->setDirty(true);
    }
    guideLibraryEdited(id);
}

void AppController::saveGuideSetToLibrary(const QString &id)
{
    if (libraryGuideSet(id))
        return;
    for (int i = 0; i < m_projectGuideSets.size(); ++i) {
        if (m_projectGuideSets.at(i).id == id) {
            m_guideLibrary.append(m_projectGuideSets.takeAt(i));
            guideLibraryEdited(id);
            return;
        }
    }
}

QString AppController::addGuideItem(const QString &setId, const QString &kind)
{
    drift::GuideSet *set = libraryGuideSet(setId);
    if (!set)
        return {};
    drift::GuideItem item;
    item.id = newGuideId();
    item.kind = drift::guideKindFromString(kind);
    if (item.kind == drift::GuideKind::Rect)
        item.left = item.top = item.right = item.bottom = 0.05;
    if (item.kind == drift::GuideKind::Aspect) {
        item.aspectW = 9;
        item.aspectH = 16;
    }
    set->items.append(item);
    guideLibraryEdited(setId);
    return item.id;
}

void AppController::setGuideItemProperty(const QString &setId, const QString &itemId,
                                         const QString &key, const QVariant &value)
{
    drift::GuideSet *set = libraryGuideSet(setId);
    if (!set)
        return;
    const auto it = std::find_if(set->items.begin(), set->items.end(),
                                 [&](const drift::GuideItem &item) { return item.id == itemId; });
    if (it == set->items.end())
        return;
    const double number = value.toDouble();
    if (key == QLatin1String("pos"))
        it->pos = qBound(0.0, number, 1.0);
    else if (key == QLatin1String("left"))
        it->left = qBound(0.0, number, 0.5);
    else if (key == QLatin1String("top"))
        it->top = qBound(0.0, number, 0.5);
    else if (key == QLatin1String("right"))
        it->right = qBound(0.0, number, 0.5);
    else if (key == QLatin1String("bottom"))
        it->bottom = qBound(0.0, number, 0.5);
    else if (key == QLatin1String("aspectW") && number > 0)
        it->aspectW = number;
    else if (key == QLatin1String("aspectH") && number > 0)
        it->aspectH = number;
    else if (key == QLatin1String("opacity"))
        it->opacity = qBound(0.0, number, 1.0);
    else if (key == QLatin1String("locked"))
        it->locked = value.toBool();
    else if (key == QLatin1String("color") && QColor(value.toString()).isValid())
        it->color = QColor(value.toString()).toRgb();
    else
        return;
    // The picker hands back #AARRGGBB; opacity has its own control.
    it->color.setAlpha(255);
    guideLibraryEdited(setId);
}

void AppController::removeGuideItem(const QString &setId, const QString &itemId)
{
    drift::GuideSet *set = libraryGuideSet(setId);
    if (!set || set->items.removeIf([&](const drift::GuideItem &item) { return item.id == itemId; }) == 0)
        return;
    guideLibraryEdited(setId);
}

QVariantMap AppController::guideSnapTargets(double width, double height) const
{
    QVariantList xs;
    QVariantList ys;
    for (const QString &id : m_activeGuideSets) {
        const drift::GuideSet *set = findGuideSet(id);
        if (!set)
            continue;
        for (const drift::GuideItem &item : set->items) {
            switch (item.kind) {
            case drift::GuideKind::Vertical:
                xs << item.pos * width;
                break;
            case drift::GuideKind::Horizontal:
                ys << item.pos * height;
                break;
            case drift::GuideKind::Rect:
                xs << item.left * width << (1 - item.right) * width;
                ys << item.top * height << (1 - item.bottom) * height;
                break;
            case drift::GuideKind::Aspect: {
                const double ratio = item.aspectW / item.aspectH;
                const double frameW = qMin(width, height * ratio);
                const double frameH = qMin(height, width / ratio);
                xs << (width - frameW) / 2 << (width + frameW) / 2;
                ys << (height - frameH) / 2 << (height + frameH) / 2;
                break;
            }
            }
        }
    }
    return {{QStringLiteral("x"), xs}, {QStringLiteral("y"), ys}};
}
