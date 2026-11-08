#include "app/editor/appcontroller/AppControllerInternal.h"
#include "engine/packages/effects/EffectCatalog.h"
#include "engine/render3d/models/ModelAsset.h"
#include "engine/render2d/pdf/PdfiumRuntime.h"
#include "engine/render3d/splats/SplatAsset.h"
#include "engine/packages/transitions/TransitionCatalog.h"
#include "engine/render3d/models/ModelClipRenderer.h"

#include "engine/render2d/pdf/PdfClipRenderer.h"
#include "core/shapes/ShapePath.h"
#include "engine/compositor/layers/TransformLayer.h"
// Engine-internal by its own header comment, and included here only for releaseCaches(): the GPU
// caches have no other owner outside src/engine to ask. A one-line forwarder on GpuCompositor
// would restore the boundary.
#include "engine/compositor/layers/MaskApplier.h"
#include "engine/render2d/vector/VectorInspect.h"


using namespace drift::appdetail;
using drift::mcpdetail::findClipById;
using drift::mcpdetail::runBeatAnalysis;
using namespace drift::appcontroller_detail;

namespace {

// On an audio track only the sound of a transition exists, and "hold" has none.
bool transitionKindFitsTrack(drift::TrackType type, const QString &kindId)
{
    if (type != drift::TrackType::Audio)
        return true;
    const TransitionPresetEntry *def = transitionDefForId(kindId);
    return !def || def->audioCurve != QLatin1String("hold");
}

// Why `path` could not be loaded as a model clip source.
QString model3dLoadError(const QString &path)
{
    QString error;
    if (isSplatSourcePath(path))
        drift::model3d::loadSplatSceneCached(path, &error);
    else
        error = drift::modelAssetWarning(path);
    return error.isEmpty() ? QStringLiteral("could not load model") : error;
}

// The type a slot name takes on this document: a Lottie slot the animation declares, or one of
// the reserved svg.* override keys whose element the SVG carries. Empty error on success.
QString resolveVectorSlotType(const drift::vec::InspectReport &report, drift::VectorKind kind, const QString &name,
                              drift::VectorSlotValue::Type *type)
{
    if (kind == drift::VectorKind::Svg) {
        drift::SvgOverrideKey key;
        if (!drift::parseSvgOverrideKey(name, &key)) {
            return QStringLiteral("an SVG takes svg.<fill|stroke|strokeWidth|opacity> for the whole drawing or "
                                  "svg.<elementId>.<fill|stroke|strokeWidth|opacity|visible> for one element; "
                                  "not %1").arg(name);
        }
        if (!key.elementId.isEmpty()) {
            bool found = false;
            QStringList ids;
            for (const drift::vec::VectorSvgElement &element : report.svgElements) {
                ids.append(element.id);
                found = found || element.id == key.elementId;
            }
            if (!found) {
                return ids.isEmpty() ? QStringLiteral("no element has an id; only the whole-drawing svg.* keys apply")
                                     : QStringLiteral("no element with id %1; ids: %2").arg(key.elementId, ids.join(QStringLiteral(", ")));
            }
        }
        *type = drift::svgOverrideType(key.prop);
        return {};
    }
    for (const drift::vec::VectorSlotInfo &info : report.slotInfos) {
        if (info.id == name) {
            *type = info.type;
            return {};
        }
    }
    QStringList ids;
    for (const drift::vec::VectorSlotInfo &info : report.slotInfos)
        ids.append(info.id);
    return ids.isEmpty() ? QStringLiteral("the document declares no slots")
                         : QStringLiteral("no slot named %1; declared: %2").arg(name, ids.join(QStringLiteral(", ")));
}

// Drops the keyframe tracks riding on a slot (the slot itself or one of its colour channels).
void eraseVectorSlotKeyframes(drift::VectorSource &v, const QString &name)
{
    for (auto it = v.keyframes.begin(); it != v.keyframes.end();) {
        if (it.key() == name || it.key().startsWith(name + QLatin1Char('.')))
            it = v.keyframes.erase(it);
        else
            ++it;
    }
}

constexpr qint64 kMaxInlineVectorBytes = 8 * 1024 * 1024;

// `source` is either the document text or a file path. Fills kind/source/path; the probe fills
// the rest. Empty error on success.
QString resolveVectorInput(const QString &source, const QString &kindHint, drift::VectorSource *out)
{
    const QString trimmed = source.trimmed();
    if (trimmed.isEmpty())
        return QStringLiteral("source required: the document text or a path to a .json/.svg file");
    if (trimmed.startsWith(QLatin1Char('{')) || trimmed.startsWith(QLatin1Char('<'))) {
        if (trimmed.size() > kMaxInlineVectorBytes)
            return QStringLiteral("inline document exceeds 8 MB; import it as a file instead");
        out->source = trimmed;
        out->path.clear();
    } else {
        const QFileInfo info(trimmed);
        if (!info.isFile())
            return QStringLiteral("source is neither a document nor an existing file: ") + trimmed;
        out->path = info.absoluteFilePath();
        out->source.clear();
    }
    const QString kind = kindHint.trimmed().toLower();
    if (kind == QLatin1String("svg"))
        out->kind = drift::VectorKind::Svg;
    else if (kind == QLatin1String("lottie") || kind == QLatin1String("json"))
        out->kind = drift::VectorKind::Lottie;
    else
        out->kind = drift::vec::detectVectorKind(drift::vec::vectorSourceBytes(*out));
    return {};
}

// fit / loop / offset / name applied from an options map; only present keys change.
void applyVectorOptions(drift::Clip &clip, const QVariantMap &opts)
{
    drift::VectorSource &v = clip.vector;
    if (opts.contains(QStringLiteral("fit")))
        v.fit = drift::vectorFitFromString(opts.value(QStringLiteral("fit")).toString().toLower());
    if (opts.contains(QStringLiteral("loop")))
        v.loop = drift::vectorLoopFromString(opts.value(QStringLiteral("loop")).toString().toLower());
    if (opts.contains(QStringLiteral("offset")))
        v.startOffsetUs = drift::secondsToUs(opts.value(QStringLiteral("offset")).toDouble());
    if (opts.contains(QStringLiteral("name")) && !opts.value(QStringLiteral("name")).toString().isEmpty())
        clip.name = opts.value(QStringLiteral("name")).toString();
}

// Non-keyed model options plus the light statics as plain writes (keyframed ones go through
// setClipKeyframe); the legacy pose keys write the transform. Returns the keys it did not
// understand.
QStringList applyModel3dOptions(drift::Clip &clip, const QVariantMap &opts, double projectHeight)
{
    drift::Model3dSource &m = clip.model3d;
    QStringList unknown;
    for (auto it = opts.cbegin(); it != opts.cend(); ++it) {
        const QString &key = it.key();
        if (key == QStringLiteral("animation")) {
            m.animation = qBound(0, it->toInt(), qMax(0, m.animations.size() - 1));
        } else if (key == QStringLiteral("loop")) {
            m.loop = drift::vectorLoopFromString(it->toString().toLower());
        } else if (key == QStringLiteral("offset")) {
            m.startOffsetUs = drift::secondsToUs(it->toDouble());
        } else if (key == QStringLiteral("name")) {
            if (!it->toString().isEmpty())
                clip.name = it->toString();
        } else if (isLegacyModel3dPoseKey(key)) {
            for (const auto &write :
                 legacyModel3dPoseWrites(clip, key, it->toDouble(), 0, projectHeight)) {
                if (drift::KeyframeTrack<double> *track = transformTrackForProp(clip, write.first))
                    writeKeyframeValue(*track, 0, write.second, false, false);
            }
        } else if (!drift::setModel3dLightOption(m, key, *it) && !drift::setModel3dScalar(m, key, it->toDouble())) {
            unknown.append(key);
        }
    }
    return unknown;
}

QVariantMap inspectModel3dPath(const QString &path)
{
    if (isSplatSourcePath(path)) {
        QString error;
        const auto splat = drift::model3d::loadSplatSceneCached(path, &error);
        if (!splat)
            return {{QStringLiteral("ok"), false},
                    {QStringLiteral("error"), error.isEmpty() ? QStringLiteral("could not load splat") : error}};
        QVariantMap out{
            {QStringLiteral("ok"), true},
            {QStringLiteral("path"), path},
            {QStringLiteral("splat"), true},
            {QStringLiteral("animations"), QVariantList()},
            {QStringLiteral("splatCount"), splat->count},
        };
        if (splat->lodLevel >= 0) {
            out.insert(QStringLiteral("lodLevel"), splat->lodLevel);
            out.insert(QStringLiteral("lodLevels"), splat->lodLevels);
            out.insert(QStringLiteral("totalSplatCount"), splat->totalCount);
        }
        return out;
    }
    const auto asset = drift::loadModelAssetCached(path);
    if (!asset) {
        QString error = drift::modelAssetWarning(path);
        if (error.isEmpty())
            error = QStringLiteral("could not load model");
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), error}};
    }
    QVariantList animations;
    for (const drift::ModelAnimationInfo &info : asset->animations) {
        animations.append(QVariantMap{
            {QStringLiteral("name"), info.name},
            {QStringLiteral("durationSec"), drift::usToSeconds(info.durationUs)},
        });
    }
    QVariantMap out{
        {QStringLiteral("ok"), true},
        {QStringLiteral("path"), path},
        {QStringLiteral("animations"), animations},
        {QStringLiteral("vertexCount"), asset->vertexCount()},
        {QStringLiteral("primitiveCount"), asset->primitives.size()},
        {QStringLiteral("materialCount"), asset->materials.size()},
        {QStringLiteral("textureCount"), asset->images.size()},
    };
    if (!asset->warning.isEmpty())
        out.insert(QStringLiteral("warning"), asset->warning);
    return out;
}

// Parses an agent-supplied value against the slot's declared type. Colours take "#rrggbb",
// "#aarrggbb", a colour name or [r,g,b(,a)] in 0..1 (Lottie's own spelling); vec2 takes [x,y]
// or {x,y}.
QString parseVectorSlotValue(drift::VectorSlotValue::Type type, const QVariant &value,
                             drift::VectorSlotValue *out)
{
    switch (type) {
    case drift::VectorSlotValue::Type::Color: {
        QColor color;
        if (value.typeId() == QMetaType::QVariantList) {
            const QVariantList c = value.toList();
            if (c.size() < 3)
                return QStringLiteral("color needs [r,g,b] or [r,g,b,a] in 0..1");
            color = QColor::fromRgbF(qBound(0.0, c.at(0).toDouble(), 1.0), qBound(0.0, c.at(1).toDouble(), 1.0),
                                     qBound(0.0, c.at(2).toDouble(), 1.0),
                                     c.size() > 3 ? qBound(0.0, c.at(3).toDouble(), 1.0) : 1.0);
        } else {
            color = QColor(value.toString());
        }
        if (!color.isValid())
            return QStringLiteral("not a colour: ") + value.toString();
        *out = drift::VectorSlotValue::fromColor(color);
        return {};
    }
    case drift::VectorSlotValue::Type::Scalar: {
        bool ok = false;
        const double d = value.toDouble(&ok);
        if (!ok)
            return QStringLiteral("scalar slot needs a number");
        *out = drift::VectorSlotValue::fromScalar(d);
        return {};
    }
    case drift::VectorSlotValue::Type::Vec2: {
        if (value.typeId() == QMetaType::QVariantList && value.toList().size() >= 2) {
            const QVariantList p = value.toList();
            *out = drift::VectorSlotValue::fromVec2(QPointF(p.at(0).toDouble(), p.at(1).toDouble()));
            return {};
        }
        if (value.typeId() == QMetaType::QVariantMap) {
            const QVariantMap p = value.toMap();
            if (p.contains(QStringLiteral("x")) && p.contains(QStringLiteral("y"))) {
                *out = drift::VectorSlotValue::fromVec2(
                    QPointF(p.value(QStringLiteral("x")).toDouble(), p.value(QStringLiteral("y")).toDouble()));
                return {};
            }
        }
        return QStringLiteral("vec2 slot needs [x,y] or {x,y}");
    }
    case drift::VectorSlotValue::Type::Text:
        if (value.typeId() != QMetaType::QString)
            return QStringLiteral("text slot needs a string");
        *out = drift::VectorSlotValue::fromText(value.toString());
        return {};
    case drift::VectorSlotValue::Type::Image: {
        const QString path = value.toString();
        if (path.isEmpty() || !QFileInfo(path).isFile())
            return QStringLiteral("image slot needs the path of an existing image file");
        *out = drift::VectorSlotValue::fromImage(QFileInfo(path).absoluteFilePath());
        return {};
    }
    }
    return QStringLiteral("unknown slot type");
}

// `timelineStart` is the carrying adjustment's start: mask keys are stored relative to it, and
// the inspector reports every key time on the timeline.
// A freeform with no vertices rasterizes to an empty path, which blanks the clip with no way back
// except removing the mask. Seed the rect it would have had as a quad, so picking Freeform gives
// you something to drag instead of a hole.
void seedFreeformMask(drift::Mask &mask)
{
    if (mask.shape != drift::MaskShape::Freeform || !mask.points.isEmpty())
        return;
    const double left = mask.x - mask.w / 2.0;
    const double right = mask.x + mask.w / 2.0;
    const double top = mask.y - mask.h / 2.0;
    const double bottom = mask.y + mask.h / 2.0;
    mask.points = {QPointF(left, top), QPointF(right, top), QPointF(right, bottom),
                   QPointF(left, bottom)};
}

} // namespace

void AppController::setShapeStyle(int trackIndex, int clipIndex, const QVariantMap &m)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;
    if (clipIndex < 0 || clipIndex >= m_project.tracks().at(trackIndex).clips.size())
        return;
    if (m_project.tracks().at(trackIndex).clips.at(clipIndex).type != drift::ClipType::Shape)
        return;

    // Snapshot before taking any reference into m_project: Project holds an implicitly shared
    // QList, so writing through a reference obtained before the copy would mutate the snapshot too
    // and the undo step would be a no-op.
    const drift::Project before = m_project;
    drift::Clip &clip = m_project.tracks()[trackIndex].clips[clipIndex];
    drift::ShapeStyle &s = clip.shapeStyle;
    if (m.contains(QStringLiteral("kind"))) {
        // The inspector addresses shapes by catalog id, so "circle" resolves to Ellipse here.
        const QString id = m.value(QStringLiteral("kind")).toString();
        s.kind = drift::shapeKindFromString(id);
        if (const drift::ShapeCatalogEntry *entry = drift::shapeCatalogEntry(id))
            clip.name = entry->label;
    }
    applyShapeStylePatch(s, m);

    // Slider drags wrap their stream of updates in beginPreviewDrag/commitPreviewDrag, which
    // already holds the "before" snapshot — pushing here too would give one undo step per frame.
    if (m_previewDragActive) {
        emitPreviewFrame();
        return;
    }

    pushProjectEdit(before, tr("Shape style changed"));
    finishEdit(tr("Shape style updated"));
}

bool AppController::vectorSupportAvailable() const
{
#ifdef DRIFT_WITH_SKIA
    return true;
#else
    return false;
#endif
}

QVariantMap AppController::inspectVector(const QString &source, const QString &kind) const
{
    drift::VectorSource probe;
    const QString error = resolveVectorInput(source, kind, &probe);
    if (!error.isEmpty())
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), error}};
    return drift::vec::inspectVector(drift::vec::vectorSourceBytes(probe), probe.kind).toJson().toVariantMap();
}

QVariantMap AppController::inspectVectorClip(int trackIndex, int clipIndex) const
{
    if (!isValidClipIndex(trackIndex, clipIndex)
        || m_project.tracks().at(trackIndex).clips.at(clipIndex).type != drift::ClipType::Vector) {
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("not a vector clip")}};
    }
    const drift::VectorSource &v = m_project.tracks().at(trackIndex).clips.at(clipIndex).vector;
    return drift::vec::inspectVector(drift::vec::vectorSourceBytes(v), v.kind).toJson().toVariantMap();
}

QString AppController::vectorSourceText(int trackIndex, int clipIndex) const
{
    if (!isValidClipIndex(trackIndex, clipIndex)
        || m_project.tracks().at(trackIndex).clips.at(clipIndex).type != drift::ClipType::Vector)
        return {};
    return QString::fromUtf8(
        drift::vec::vectorSourceBytes(m_project.tracks().at(trackIndex).clips.at(clipIndex).vector));
}

QVariantMap AppController::inspectModel3d(const QString &path) const
{
    if (path.isEmpty() || !QFileInfo::exists(path))
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("file not found")}};
    return inspectModel3dPath(path);
}

QVariantMap AppController::inspectModel3dClip(int trackIndex, int clipIndex) const
{
    if (!isValidClipIndex(trackIndex, clipIndex)
        || m_project.tracks().at(trackIndex).clips.at(clipIndex).type != drift::ClipType::Model3d) {
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("not a 3D model clip")}};
    }
    const drift::Clip &clip = m_project.tracks().at(trackIndex).clips.at(clipIndex);
    QVariantMap out = inspectModel3dPath(clip.model3d.path);
    out.insert(QStringLiteral("model3d"), model3dSourceToMap(clip.model3d, clip.timelineStart));
    return out;
}

QVariantMap AppController::inspectPdf(const QString &path) const
{
    if (path.isEmpty() || !QFileInfo::exists(path))
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("file not found")}};
    if (!AssetLibrary::isPdfPath(path))
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("not a .pdf file")}};
    drift::PdfSource source;
    source.path = QFileInfo(path).absoluteFilePath();
    drift::probePdfSource(source);
    return {{QStringLiteral("ok"), true},
            {QStringLiteral("path"), source.path},
            {QStringLiteral("pageCount"), source.pageCount},
            {QStringLiteral("addonAvailable"), drift::pdfium::available()},
            {QStringLiteral("addonId"), QString::fromLatin1(drift::pdfium::kPdfiumKind)}};
}

QVariantMap AppController::inspectPdfClip(int trackIndex, int clipIndex) const
{
    if (!isValidClipIndex(trackIndex, clipIndex)
        || m_project.tracks().at(trackIndex).clips.at(clipIndex).type != drift::ClipType::Pdf) {
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("not a PDF clip")}};
    }
    const drift::Clip &clip = m_project.tracks().at(trackIndex).clips.at(clipIndex);
    QVariantMap out = pdfSourceToMap(clip.pdf, clip.timelineStart);
    out.insert(QStringLiteral("ok"), true);
    out.insert(QStringLiteral("addonId"), QString::fromLatin1(drift::pdfium::kPdfiumKind));
    return out;
}

QVariantMap AppController::addModel3dClip(const QString &path, int trackIndex, double atSeconds,
                                          const QVariantMap &opts)
{
    if (path.isEmpty() || !QFileInfo::exists(path))
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("file not found")}};
    if (!AssetLibrary::isModelPath(path))
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("only .glb files and Gaussian splats (.sog, .ply, .zip, meta.json) are supported")}};
    drift::Model3dSource model;
    model.path = QFileInfo(path).absoluteFilePath();
    probeModel3dSource(model);
    if (!model.hasAabb()) {
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), model3dLoadError(model.path)}};
    }

    const drift::Project before = m_project;

    drift::TimeUs duration =
        model.animationDurationUs() > 0 ? model.animationDurationUs() : drift::imageClipDurationUs();
    if (opts.value(QStringLiteral("duration")).toDouble() > 0.0)
        duration = drift::secondsToUs(opts.value(QStringLiteral("duration")).toDouble());
    duration = qMax(duration, drift::minClipDurationUs(m_project.fps()));

    const drift::TimeUs startUs = atSeconds < 0.0 ? m_playheadUs : drift::secondsToUs(atSeconds);
    int target = trackIndex;
    if (target < 0 || target >= m_project.tracks().size()
        || !m_project.tracks().at(target).allowsClipType(drift::ClipType::Model3d)) {
        target = drift::ensureFreeTrackForClipType(m_project, drift::ClipType::Model3d, startUs,
                                                   duration, true);
    }
    if (target < 0)
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("no graphic track")}};

    drift::Track &track = m_project.tracks()[target];
    const drift::TimeUs start = drift::resolveClipStart(m_project, track, -1, startUs, duration,
                                                        m_snapEnabled, m_playheadUs);

    drift::Clip clip;
    clip.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    clip.type = drift::ClipType::Model3d;
    clip.path = model.path;
    clip.model3d = model;
    clip.name = QFileInfo(model.path).completeBaseName();
    clip.timelineStart = start;
    clip.timelineDuration = duration;
    clip.srcIn = 0;
    clip.srcOut = duration;
    QVariantMap options = opts;
    options.remove(QStringLiteral("duration"));
    placeModelClipLayout(clip, m_project.width(), m_project.height());
    const QStringList unknown = applyModel3dOptions(clip, options, m_project.height());

    track.clips.append(clip);
    const int newClipIndex = track.clips.size() - 1;
    pushProjectEdit(before, tr("3D model added"));
    finishEdit(tr("3D model added"));
    selectClip(target, newClipIndex);

    QVariantMap out = inspectModel3dPath(clip.model3d.path);
    out.insert(QStringLiteral("id"), clip.id);
    out.insert(QStringLiteral("track"), target);
    out.insert(QStringLiteral("index"), newClipIndex);
    out.insert(QStringLiteral("model3d"), model3dSourceToMap(clip.model3d, clip.timelineStart));
    if (!unknown.isEmpty())
        out.insert(QStringLiteral("unknownOptions"), unknown);
    return out;
}

QVariantMap AppController::setModel3dSource(int trackIndex, int clipIndex, const QString &path,
                                            const QVariantMap &opts)
{
    if (!isValidClipIndex(trackIndex, clipIndex)
        || m_project.tracks().at(trackIndex).clips.at(clipIndex).type != drift::ClipType::Model3d)
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("not a 3D model clip")}};
    if (path.isEmpty() || !QFileInfo::exists(path))
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("file not found")}};
    if (!AssetLibrary::isModelPath(path))
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("only .glb files and Gaussian splats (.sog, .ply, .zip, meta.json) are supported")}};

    // Pose, light and keyframes carry over; the animation index is clamped to the new file.
    drift::Model3dSource model = m_project.tracks().at(trackIndex).clips.at(clipIndex).model3d;
    model.path = QFileInfo(path).absoluteFilePath();
    probeModel3dSource(model);
    if (!model.hasAabb()) {
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), model3dLoadError(model.path)}};
    }

    const drift::Project before = m_project;
    drift::Clip &clip = m_project.tracks()[trackIndex].clips[clipIndex];
    clip.path = model.path;
    clip.model3d = model;
    const QStringList unknown = applyModel3dOptions(clip, opts, m_project.height());
    pushProjectEdit(before, tr("3D model replaced"));
    finishEdit(tr("3D model replaced"));

    QVariantMap out = inspectModel3dPath(clip.model3d.path);
    out.insert(QStringLiteral("id"), clip.id);
    out.insert(QStringLiteral("model3d"), model3dSourceToMap(clip.model3d, clip.timelineStart));
    if (!unknown.isEmpty())
        out.insert(QStringLiteral("unknownOptions"), unknown);
    return out;
}

QString AppController::setModel3dOptions(int trackIndex, int clipIndex, const QVariantMap &opts)
{
    if (!isValidClipIndex(trackIndex, clipIndex)
        || m_project.tracks().at(trackIndex).clips.at(clipIndex).type != drift::ClipType::Model3d)
        return QStringLiteral("not a 3D model clip");
    if (opts.isEmpty())
        return QStringLiteral("nothing to change");
    const drift::Project before = m_project;
    const QStringList unknown = applyModel3dOptions(m_project.tracks()[trackIndex].clips[clipIndex], opts,
                                                   m_project.height());
    if (unknown.size() == opts.size()) {
        m_project = before;
        return QStringLiteral("unknown option: %1").arg(unknown.join(QStringLiteral(", ")));
    }
    pushProjectEdit(before, tr("3D model options"));
    finishEdit(tr("3D model options updated"));
    if (!unknown.isEmpty())
        return QStringLiteral("unknown option: %1").arg(unknown.join(QStringLiteral(", ")));
    return {};
}

QVariantMap AppController::addVectorClip(const QString &source, int trackIndex, double atSeconds,
                                         const QVariantMap &opts)
{
    drift::VectorSource vector;
    QString error = resolveVectorInput(source, opts.value(QStringLiteral("kind")).toString(), &vector);
    if (error.isEmpty() && !drift::vec::probeVectorSource(vector, &error))
        error = QStringLiteral("document did not parse: ") + error;
    if (!error.isEmpty())
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), error}};
    const drift::vec::InspectReport report =
        drift::vec::inspectVector(drift::vec::vectorSourceBytes(vector), vector.kind);

    const drift::Project before = m_project;

    // Plays once by default; a still gets the image default.
    drift::TimeUs duration = vector.durationUs > 0 ? vector.durationUs : drift::imageClipDurationUs();
    if (opts.value(QStringLiteral("duration")).toDouble() > 0.0)
        duration = drift::secondsToUs(opts.value(QStringLiteral("duration")).toDouble());
    duration = qMax(duration, drift::minClipDurationUs(m_project.fps()));

    const drift::TimeUs wantStart = atSeconds < 0.0 ? m_playheadUs : drift::secondsToUs(atSeconds);
    int target = trackIndex;
    if (target < 0 || target >= m_project.tracks().size()
        || !m_project.tracks().at(target).allowsClipType(drift::ClipType::Vector)) {
        target = drift::ensureFreeTrackForClipType(m_project, drift::ClipType::Vector, wantStart,
                                                   duration, true);
    }
    if (target < 0)
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("no graphic track")}};

    drift::Track &track = m_project.tracks()[target];
    const drift::TimeUs startUs = atSeconds < 0.0 ? m_playheadUs : drift::secondsToUs(atSeconds);
    const drift::TimeUs start = drift::resolveClipStart(m_project, track, -1, startUs, duration,
                                                        m_snapEnabled, m_playheadUs);

    drift::Clip clip;
    clip.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    clip.type = drift::ClipType::Vector;
    clip.vector = vector;
    clip.name = vector.title.isEmpty()
                    ? (vector.isInline() ? tr("Animation") : QFileInfo(vector.path).completeBaseName())
                    : vector.title;
    clip.timelineStart = start;
    clip.timelineDuration = duration;
    clip.srcIn = 0;
    clip.srcOut = duration;
    applyVectorOptions(clip, opts);
    clip.thumbnailPath = MediaThumbnail::generateVector(clip.vector);
    clip.filmstripPath = clip.thumbnailPath;
    const QVariantMap overrides = opts.value(QStringLiteral("slots")).toMap();
    QStringList slotErrors;
    for (auto it = overrides.cbegin(); it != overrides.cend(); ++it) {
        drift::VectorSlotValue::Type type;
        const QString typeError = resolveVectorSlotType(report, vector.kind, it.key(), &type);
        if (!typeError.isEmpty()) {
            slotErrors.append(QStringLiteral("%1: %2").arg(it.key(), typeError));
            continue;
        }
        drift::VectorSlotValue value;
        const QString slotError = parseVectorSlotValue(type, it.value(), &value);
        if (!slotError.isEmpty()) {
            slotErrors.append(QStringLiteral("%1: %2").arg(it.key(), slotError));
            continue;
        }
        clip.vector.slotValues.insert(it.key(), value);
    }
    fitClipLayoutToCanvas(clip, vector.width, vector.height, m_project.width(), m_project.height());

    track.clips.append(clip);
    const int newClipIndex = track.clips.size() - 1;
    pushProjectEdit(before, tr("Animation added"));
    finishEdit(tr("Animation added"));
    selectClip(target, newClipIndex);

    QVariantMap out = report.toJson().toVariantMap();
    out.insert(QStringLiteral("id"), clip.id);
    out.insert(QStringLiteral("track"), target);
    out.insert(QStringLiteral("index"), newClipIndex);
    if (!slotErrors.isEmpty())
        out.insert(QStringLiteral("slotErrors"), slotErrors);
    return out;
}

QVariantMap AppController::setVectorSource(int trackIndex, int clipIndex, const QString &source,
                                           const QVariantMap &opts)
{
    if (!isValidClipIndex(trackIndex, clipIndex)
        || m_project.tracks().at(trackIndex).clips.at(clipIndex).type != drift::ClipType::Vector)
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("not a vector clip")}};

    drift::VectorSource vector = m_project.tracks().at(trackIndex).clips.at(clipIndex).vector;
    QString error = resolveVectorInput(source, opts.value(QStringLiteral("kind")).toString(), &vector);
    if (error.isEmpty() && !drift::vec::probeVectorSource(vector, &error))
        error = QStringLiteral("document did not parse: ") + error;
    if (!error.isEmpty())
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), error}};
    const drift::vec::InspectReport report =
        drift::vec::inspectVector(drift::vec::vectorSourceBytes(vector), vector.kind);

    // Overrides for slots the new document does not declare are dropped rather than carried,
    // along with the keyframes riding on them.
    QMap<QString, drift::VectorSlotValue> kept;
    for (auto it = vector.slotValues.cbegin(); it != vector.slotValues.cend(); ++it) {
        drift::VectorSlotValue::Type type;
        if (resolveVectorSlotType(report, vector.kind, it.key(), &type).isEmpty() && it->type == type)
            kept.insert(it.key(), *it);
        else
            eraseVectorSlotKeyframes(vector, it.key());
    }
    vector.slotValues = kept;
    vector.title = report.title;

    const drift::Project before = m_project;
    drift::Clip &clip = m_project.tracks()[trackIndex].clips[clipIndex];
    clip.vector = vector;
    applyVectorOptions(clip, opts);
    clip.thumbnailPath = MediaThumbnail::generateVector(clip.vector);
    clip.filmstripPath = clip.thumbnailPath;
    pushProjectEdit(before, tr("Animation replaced"));
    finishEdit(tr("Animation replaced"));

    QVariantMap out = report.toJson().toVariantMap();
    out.insert(QStringLiteral("id"), clip.id);
    return out;
}

QString AppController::setVectorOptions(int trackIndex, int clipIndex, const QVariantMap &opts)
{
    if (!isValidClipIndex(trackIndex, clipIndex)
        || m_project.tracks().at(trackIndex).clips.at(clipIndex).type != drift::ClipType::Vector)
        return QStringLiteral("not a vector clip");
    static const QStringList kKeys = {QStringLiteral("fit"), QStringLiteral("loop"),
                                      QStringLiteral("offset"), QStringLiteral("name")};
    bool any = false;
    for (const QString &key : kKeys)
        any = any || opts.contains(key);
    if (!any)
        return QStringLiteral("nothing to change: fit, loop, offset or name required");
    const drift::Project before = m_project;
    applyVectorOptions(m_project.tracks()[trackIndex].clips[clipIndex], opts);
    pushProjectEdit(before, tr("Animation options"));
    finishEdit(tr("Animation options updated"));
    return {};
}

QString AppController::setPdfOptions(int trackIndex, int clipIndex, const QVariantMap &opts)
{
    if (!isValidClipIndex(trackIndex, clipIndex)
        || m_project.tracks().at(trackIndex).clips.at(clipIndex).type != drift::ClipType::Pdf)
        return QStringLiteral("not a PDF clip");
    static const QStringList kKeys = {QStringLiteral("firstPage"), QStringLiteral("lastPage"),
                                      QStringLiteral("layout"), QStringLiteral("gridColumns"),
                                      QStringLiteral("gap")};
    bool any = false;
    for (const QString &key : kKeys)
        any = any || opts.contains(key);
    if (!any)
        return QStringLiteral("nothing to change: firstPage, lastPage, layout, gridColumns or gap required");

    const drift::Project before = m_project;
    drift::PdfSource &pdf = m_project.tracks()[trackIndex].clips[clipIndex].pdf;
    const int maxPage = pdf.pageCount > 0 ? pdf.pageCount : 9999;
    if (opts.contains(QStringLiteral("firstPage")))
        pdf.firstPage = qBound(1, opts.value(QStringLiteral("firstPage")).toInt(), maxPage);
    if (opts.contains(QStringLiteral("lastPage"))) {
        const int last = opts.value(QStringLiteral("lastPage")).toInt();
        pdf.lastPage = last <= 0 ? 0 : qMin(last, maxPage);
    }
    if (pdf.lastPage > 0 && pdf.lastPage < pdf.firstPage) {
        if (opts.contains(QStringLiteral("lastPage")))
            pdf.firstPage = pdf.lastPage;
        else
            pdf.lastPage = pdf.firstPage;
    }
    if (opts.contains(QStringLiteral("layout")))
        pdf.layout = drift::pdfLayoutFromString(opts.value(QStringLiteral("layout")).toString());
    if (opts.contains(QStringLiteral("gridColumns")))
        pdf.gridColumns = qBound(1, opts.value(QStringLiteral("gridColumns")).toInt(), 20);
    if (opts.contains(QStringLiteral("gap")))
        pdf.gap = qBound(0.0, opts.value(QStringLiteral("gap")).toDouble(), 200.0);
    pushProjectEdit(before, tr("PDF options"));
    finishEdit(tr("PDF options updated"));
    return {};
}

QString AppController::setVectorSlot(int trackIndex, int clipIndex, const QString &name,
                                     const QVariant &value)
{
    if (!isValidClipIndex(trackIndex, clipIndex)
        || m_project.tracks().at(trackIndex).clips.at(clipIndex).type != drift::ClipType::Vector)
        return QStringLiteral("not a vector clip");
    if (name.isEmpty())
        return QStringLiteral("slot name required");

    const drift::VectorSource &current = m_project.tracks().at(trackIndex).clips.at(clipIndex).vector;
    const drift::vec::InspectReport report =
        drift::vec::inspectVector(drift::vec::vectorSourceBytes(current), current.kind);
    drift::VectorSlotValue::Type type;
    const QString typeError = resolveVectorSlotType(report, current.kind, name, &type);
    if (!typeError.isEmpty())
        return typeError;

    const bool clear = !value.isValid() || value.isNull();
    drift::VectorSlotValue parsed;
    if (!clear) {
        const QString error = parseVectorSlotValue(type, value, &parsed);
        if (!error.isEmpty())
            return QStringLiteral("%1 (%2 slot): %3").arg(name, drift::vectorSlotTypeToString(type), error);
    }

    const drift::Project before = m_project;
    drift::VectorSource &v = m_project.tracks()[trackIndex].clips[clipIndex].vector;
    if (clear) {
        v.slotValues.remove(name);
        eraseVectorSlotKeyframes(v, name);
    } else {
        v.slotValues.insert(name, parsed);
    }
    pushProjectEdit(before, tr("Animation slot"));
    finishEdit(tr("Animation slot updated"));
    return {};
}

QVariantList AppController::vectorSlots(int trackIndex, int clipIndex) const
{
    if (!isValidClipIndex(trackIndex, clipIndex)
        || m_project.tracks().at(trackIndex).clips.at(clipIndex).type != drift::ClipType::Vector)
        return {};
    const drift::VectorSource &v = m_project.tracks().at(trackIndex).clips.at(clipIndex).vector;
    const drift::vec::InspectReport report =
        drift::vec::inspectVector(drift::vec::vectorSourceBytes(v), v.kind);
    QVariantList out;
    const auto row = [&](const QString &id, drift::VectorSlotValue::Type type) {
        QVariantMap entry{{QStringLiteral("id"), id}, {QStringLiteral("type"), drift::vectorSlotTypeToString(type)}};
        auto it = v.slotValues.constFind(id);
        if (it != v.slotValues.constEnd())
            entry.insert(QStringLiteral("value"), vectorSlotValueToVariant(*it));
        out.append(entry);
    };
    if (v.kind == drift::VectorKind::Svg) {
        // The whole-drawing keys always, then whichever element overrides are set.
        for (const char *prop : {"fill", "stroke", "strokeWidth", "opacity"})
            row(QStringLiteral("svg.") + QLatin1String(prop), drift::svgOverrideType(QLatin1String(prop)));
        for (auto it = v.slotValues.cbegin(); it != v.slotValues.cend(); ++it) {
            drift::SvgOverrideKey key;
            if (drift::parseSvgOverrideKey(it.key(), &key) && !key.elementId.isEmpty())
                row(it.key(), drift::svgOverrideType(key.prop));
        }
        return out;
    }
    for (const drift::vec::VectorSlotInfo &info : report.slotInfos)
        row(info.id, info.type);
    return out;
}

void AppController::setClipMask(int trackIndex, int clipIndex, const QVariantMap &maskMap)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    const drift::Project before = m_project;
    writeClipMask(trackIndex, clipIndex, maskFromMap(maskMap));
    pushProjectEdit(before, tr("Mask changed"));
    finishEdit(tr("Clip mask updated"));
}

// Selecting a mask adjustment and selecting the clip it masks are both ways of reaching the same
// mask, so both write here. On the adjustment the mask is the payload and is written in place; on
// a media clip it is pinned through a lane, which is the only place a media clip's mask can live.
void AppController::writeClipMask(int trackIndex, int clipIndex, const drift::Mask &mask)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;
    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    drift::Mask seeded = mask;
    seedFreeformMask(seeded);

    drift::Clip &clip = track.clips[clipIndex];
    // maskFromMap never carries animation, so keep what is already there.
    std::optional<drift::Mask> existing;
    if (clip.type == drift::ClipType::Adjustment) {
        existing = clip.mask;
    } else {
        const QList<drift::ClipRef> pinned =
            drift::linkedMaskAdjustments(m_project, trackIndex, clipIndex);
        if (!pinned.isEmpty()) {
            const drift::ClipRef &first = pinned.constFirst();
            existing = m_project.tracks().at(first.trackIndex).clips.at(first.clipIndex).mask;
        }
    }
    // maskFromMap never carries frame fixes either, and a cutout edited from the inspector must keep them.
    if (existing && seeded.mediaPatches.isEmpty() && seeded.isMedia())
        seeded.mediaPatches = existing->mediaPatches;
    if (existing && mask.keyframes.isEmpty() && mask.pathKeys.isEmpty()) {
        // An edit of one property keeps the rest of the animation; a caller that supplies a new
        // value or new geometry has replaced that property, so its old keys must not override it.
        const auto same = [](double a, double b) { return qAbs(a - b) <= 1e-9; };
        const std::pair<const char *, std::pair<double, double>> scalars[] = {
            {"x", {seeded.x, existing->x}}, {"y", {seeded.y, existing->y}},
            {"w", {seeded.w, existing->w}}, {"h", {seeded.h, existing->h}},
            {"rotation", {seeded.rotation, existing->rotation}},
            {"feather", {seeded.feather, existing->feather}}};
        seeded.keyframes = existing->keyframes;
        for (const auto &[key, values] : scalars) {
            if (!same(values.first, values.second))
                seeded.keyframes.remove(QString::fromLatin1(key));
        }

        bool samePoints = seeded.points.size() == existing->points.size();
        for (qsizetype i = 0; samePoints && i < seeded.points.size(); ++i) {
            samePoints = same(seeded.points.at(i).x(), existing->points.at(i).x())
                         && same(seeded.points.at(i).y(), existing->points.at(i).y());
        }
        if (existing->shape == seeded.shape && samePoints)
            seeded.pathKeys = existing->pathKeys;
    }

    if (clip.type == drift::ClipType::Adjustment) {
        // The kind is deliberately left alone. The Masks tab is also offered for a video-effects
        // adjustment, where a mask scopes where the chain lands; flipping it to Mask there would
        // stop its effect stack rendering.
        clip.mask = seeded;
        return;
    }
    drift::setLinkedMask(m_project, trackIndex, clipIndex, seeded);
}

drift::Mask AppController::maskFromCatalogId(const QString &shape) const
{
    drift::Mask mask;
    mask.shape = drift::maskShapeFromString(shape);
    // maskShapeFromString falls back to None for anything it does not know, and Media needs a
    // path it cannot invent — either way the mask contributes nothing and the caller does nothing.
    if (mask.shape == drift::MaskShape::Media)
        mask.shape = drift::MaskShape::None;
    seedFreeformMask(mask);
    return mask;
}

void AppController::selectClipById(const QString &clipId)
{
    int foundTrack = -1;
    int foundClip = -1;
    if (!clipId.isEmpty() && findClipById(m_project, clipId, &foundTrack, &foundClip))
        selectClip(foundTrack, foundClip);
}

QVariantList AppController::maskCatalog() const
{
    // Ordered as the inspector's shape combo lists them, so the two read alike.
    const QList<QPair<QString, QString>> entries = {
        {QStringLiteral("rectangle"), tr("Rectangle")}, {QStringLiteral("ellipse"), tr("Ellipse")},
        {QStringLiteral("star"), tr("Star")},           {QStringLiteral("heart"), tr("Heart")},
        {QStringLiteral("bars"), tr("Bars")},           {QStringLiteral("freeform"), tr("Freeform")},
    };

    QVariantList out;
    for (const auto &entry : entries) {
        out.append(QVariantMap{{QStringLiteral("id"), entry.first},
                               {QStringLiteral("label"), entry.second}});
    }
    return out;
}

QString AppController::maskShapeSvgPath(const QString &shape) const
{
    drift::Mask mask = maskFromCatalogId(shape);
    if (mask.shape == drift::MaskShape::None)
        return {};

    // Thumbnails are authored on the 0..100 grid MasksTab.qml scales from. The mask rect is
    // normalized, so widening it here is what fills the card rather than sitting at the 60% the
    // timeline default would give. Bars spans the full width by construction and is left alone.
    if (mask.shape != drift::MaskShape::Bars) {
        mask.w = 0.94;
        mask.h = 0.94;
        mask.points.clear();
        seedFreeformMask(mask);
    }
    return drift::painterPathToSvg(drift::maskPath(mask, 100, 100));
}

void AppController::addMaskToClip(int trackIndex, int clipIndex, const QString &shape)
{
    const drift::Mask mask = maskFromCatalogId(shape);
    if (!mask.contributes())
        return;

    const drift::Project before = m_project;
    const drift::ClipRef added = drift::addLinkedMask(m_project, trackIndex, clipIndex, mask);
    if (added.trackIndex < 0)
        return;
    const QString addedId = m_project.tracks().at(added.trackIndex).clips.at(added.clipIndex).id;

    pushProjectEdit(before, tr("Add mask"));
    finishEdit(tr("Mask added"));
    selectClipById(addedId);
}

void AppController::addMaskLaneClip(int trackIndex, const QString &shape, double atSeconds,
                                    double durationSeconds)
{
    const drift::Mask mask = maskFromCatalogId(shape);
    if (!mask.contributes())
        return;

    const drift::TimeUs startUs =
        atSeconds < 0.0 ? m_playheadUs : drift::secondsToUs(atSeconds);
    const drift::TimeUs durationUs = durationSeconds > 0.0
                                         ? drift::secondsToUs(durationSeconds)
                                         : drift::imageClipDurationUs();

    const drift::Project before = m_project;
    const drift::ClipRef added =
        drift::addLaneMask(m_project, trackIndex, mask, startUs, durationUs);
    if (added.trackIndex < 0)
        return;
    const QString addedId = m_project.tracks().at(added.trackIndex).clips.at(added.clipIndex).id;

    pushProjectEdit(before, tr("Add mask"));
    finishEdit(tr("Mask added"));
    selectClipById(addedId);
}

void AppController::addMediaMaskToClip(int trackIndex, int clipIndex, const QUrl &url)
{
    // The compositor decodes the coverage with FFmpeg, which cannot open a content:// URI, so the
    // document is staged to a real file first on the platforms that hand one back.
    const QString path = readTargetPath(url);
    if (path.isEmpty())
        return;

    // Full-frame, for the same reason a segmentation matte is: the media's own pixels place the
    // coverage, so the parametric default rect would crop it.
    drift::Mask mask = drift::fullFrameMediaMask(path);
    mask.name = QFileInfo(path).completeBaseName();

    const drift::Project before = m_project;
    const drift::ClipRef added = drift::addLinkedMask(m_project, trackIndex, clipIndex, mask);
    if (added.trackIndex < 0)
        return;
    const QString addedId = m_project.tracks().at(added.trackIndex).clips.at(added.clipIndex).id;

    pushProjectEdit(before, tr("Add mask"));
    finishEdit(tr("Mask added"));
    selectClipById(addedId);
}

void AppController::insertMaskPoint(int trackIndex, int clipIndex, int pointIndex, double x,
                                    double y)
{
    const drift::Clip *host =
        effectHostClip(trackIndex, clipIndex, drift::AdjustmentKind::Mask);
    if (!host || host->mask.shape != drift::MaskShape::Freeform)
        return;

    drift::Mask mask = host->mask;
    const int index = qBound(0, pointIndex, mask.points.size());
    // Each shape key gains the vertex at the same slot, placed on that key's own edge, so the
    // animation keeps its shape. The count check has to run before the static insert below.
    for (auto it = mask.pathKeys.begin(); it != mask.pathKeys.end(); ++it) {
        QVector<QPointF> &vertices = it.value();
        const int n = vertices.size();
        if (n != mask.points.size() || n == 0)
            continue;
        const QPointF prev = vertices.at((index - 1 + n) % n);
        const QPointF next = vertices.at(index % n);
        vertices.insert(index, (prev + next) / 2.0);
    }
    mask.points.insert(index, QPointF(x, y));

    const drift::Project before = m_project;
    writeClipMask(trackIndex, clipIndex, mask);
    pushProjectEdit(before, tr("Add mask point"));
    finishEdit(tr("Mask point added"));
}

void AppController::removeMaskPoint(int trackIndex, int clipIndex, int pointIndex)
{
    const drift::Clip *host =
        effectHostClip(trackIndex, clipIndex, drift::AdjustmentKind::Mask);
    if (!host || host->mask.shape != drift::MaskShape::Freeform)
        return;
    // A polygon needs three vertices to enclose anything; removing past that would silently
    // blank the clip with no way back except deleting the mask.
    if (host->mask.points.size() <= 3 || pointIndex < 0 || pointIndex >= host->mask.points.size())
        return;

    drift::Mask mask = host->mask;
    mask.points.remove(pointIndex);
    // The shape keys are vertex lists of their own, so they have to lose the same slot or the
    // counts stop matching and pointsAt() falls back to holding the earlier key.
    for (auto it = mask.pathKeys.begin(); it != mask.pathKeys.end(); ++it) {
        if (pointIndex < it.value().size())
            it.value().remove(pointIndex);
    }

    const drift::Project before = m_project;
    writeClipMask(trackIndex, clipIndex, mask);
    pushProjectEdit(before, tr("Remove mask point"));
    finishEdit(tr("Mask point removed"));
}

drift::TransformParent AppController::transformParentFor(int trackIndex) const
{
    const QList<drift::TransformParent> parents =
        drift::transformParentsAt(m_project, m_playheadUs, 1.0);
    if (trackIndex < 0 || trackIndex >= parents.size())
        return {};
    return parents.at(trackIndex);
}

void AppController::addTransition(int trackIndex, int clipIndex, const QString &kind, double durationSeconds,
                                  bool linkedAudio)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (!trackAllowsTransitions(track.type))
        return;
    if (!transitionKindFitsTrack(track.type, kind)) {
        setLastMessage(tr("That transition has no sound; audio tracks take crossfade or dip"),
                       QStringLiteral("warning"));
        return;
    }

    const int partnerIndex = findTransitionPartnerIndex(track, clipIndex);
    if (partnerIndex < 0)
        return;

    const drift::Clip &fromClip = track.clips.at(clipIndex);
    const drift::Clip &toClip = track.clips.at(partnerIndex);
    const drift::TimeUs overlapUs = drift::physicalOverlapDurationUs(fromClip, toClip);
    const drift::TimeUs requestedUs = qMax<drift::TimeUs>(drift::secondsToUs(0.1), drift::secondsToUs(durationSeconds));
    const drift::TimeUs durationUs = overlapUs > 0 ? overlapUs : requestedUs;
    const QString kindId = transitionDefForId(kind) ? kind : QStringLiteral("crossfade");

    const drift::Project before = m_project;
    const QString fromId = fromClip.id;
    const QString toId = toClip.id;
    // Adds or replaces the transition between two clips on a track; returns whether it replaced.
    const auto upsert = [&](int t, const QString &a, const QString &b, const QString &kindForTrack) {
        drift::Track &target = m_project.tracks()[t];
        if (drift::Transition *existing = findTransitionBetween(target, a, b)) {
            existing->kindId = kindForTrack;
            existing->parameters.clear(); // overrides belong to the old package
            existing->durationUs = durationUs;
            return true;
        }
        drift::Transition transition;
        transition.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        transition.fromClipId = a;
        transition.toClipId = b;
        transition.kindId = kindForTrack;
        transition.durationUs = durationUs;
        target.transitions.append(transition);
        return false;
    };
    const bool replaced = upsert(trackIndex, fromId, toId, kindId);
    if (linkedAudio) {
        const LinkedAudioPair pair = linkedAudioPairFor(m_project, m_project.tracks().at(trackIndex), fromId, toId);
        if (pair.valid())
            upsert(pair.track, pair.fromId, pair.toId,
                   transitionKindFitsTrack(drift::TrackType::Audio, kindId) ? kindId : QStringLiteral("crossfade"));
    }
    pushProjectEdit(before, replaced ? tr("Replace transition") : tr("Add transition"));
    finishEdit(replaced ? tr("Transition updated") : tr("Transition added"));
    selectTransition(trackIndex, clipIndex);
}

void AppController::removeTransition(int trackIndex, const QString &transitionId)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    const drift::Project before = m_project;
    for (int i = 0; i < track.transitions.size(); ++i) {
        if (track.transitions.at(i).id != transitionId)
            continue;

        const drift::Transition transition = track.transitions.at(i);
        if (m_selectedTransitionTrack == trackIndex && m_selectedTransitionLeftClip >= 0) {
            const QString fromId = track.clips.value(m_selectedTransitionLeftClip).id;
            if (transition.fromClipId == fromId)
                clearTransitionSelection();
        }

        track.transitions.removeAt(i);
        const LinkedAudioPair pair =
            linkedAudioPairFor(m_project, m_project.tracks().at(trackIndex), transition.fromClipId, transition.toClipId);
        if (pair.valid()) {
            drift::Track &audio = m_project.tracks()[pair.track];
            for (int k = audio.transitions.size() - 1; k >= 0; --k)
                if (audio.transitions.at(k).fromClipId == pair.fromId && audio.transitions.at(k).toClipId == pair.toId)
                    audio.transitions.removeAt(k);
        }
        pushProjectEdit(before, tr("Remove transition"));
        finishEdit(tr("Transition removed"));
        return;
    }
}

void AppController::setTransitionDuration(int trackIndex, const QString &transitionId, double durationSeconds)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    const drift::Project before = m_project;
    for (drift::Transition &transition : track.transitions) {
        if (transition.id != transitionId)
            continue;

        const drift::TimeUs durationUs =
            qMax<drift::TimeUs>(drift::secondsToUs(0.1), drift::secondsToUs(durationSeconds));
        drift::Clip *fromClip = nullptr;
        drift::Clip *toClip = nullptr;
        for (drift::Clip &clip : track.clips) {
            if (clip.id == transition.fromClipId)
                fromClip = &clip;
            else if (clip.id == transition.toClipId)
                toClip = &clip;
        }

        if (fromClip && toClip && drift::clipsPhysicallyOverlap(*fromClip, *toClip)) {
            const drift::TimeUs maxOverlap =
                qMin(fromClip->timelineDuration, toClip->timelineDuration) - drift::secondsToUs(0.05);
            const drift::TimeUs clamped = qBound(drift::secondsToUs(0.1), durationUs, qMax(drift::secondsToUs(0.1), maxOverlap));
            toClip->timelineStart = fromClip->timelineEnd() - clamped;
            transition.durationUs = clamped;
        } else {
            transition.durationUs = durationUs;
        }
        if (drift::Transition *mirror = mirroredAudioTransition(m_project, trackIndex, transition))
            mirror->durationUs = transition.durationUs;

        pushProjectEdit(before, tr("Transition duration"));
        finishEdit(tr("Transition duration updated"));
        emit selectedTransitionDataChanged();
        return;
    }
}

void AppController::setTransitionKind(int trackIndex, const QString &transitionId, const QString &kind)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;
    if (!transitionDefForId(kind))
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (!transitionKindFitsTrack(track.type, kind))
        return;
    const drift::Project before = m_project;
    for (drift::Transition &transition : track.transitions) {
        if (transition.id == transitionId) {
            if (transition.kindId == kind)
                return;
            transition.kindId = kind;
            transition.parameters.clear(); // overrides belong to the old package
            if (drift::Transition *mirror = mirroredAudioTransition(m_project, trackIndex, transition)) {
                mirror->kindId = transitionKindFitsTrack(drift::TrackType::Audio, kind) ? kind : QStringLiteral("crossfade");
                mirror->parameters.clear();
            }
            pushProjectEdit(before, tr("Transition kind"));
            finishEdit(tr("Transition kind updated"));
            emit selectedTransitionDataChanged();
            return;
        }
    }
}
namespace {


// Transition parameters are declared floats or bools, matching the effect parameter UI.
QVariant coerceTransitionParam(const TransitionPresetEntry *def, const QString &key, double value)
{
    if (def) {
        for (const drift::EffectParamSpec &param : def->meta.parameters) {
            if (param.key == key)
                return param.isBoolean() ? QVariant(value > 0.5) : QVariant(value);
        }
    }
    return value;
}

} // namespace

void AppController::previewSetTransitionParam(int trackIndex, const QString &transitionId,
                                              const QString &key, double value)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size() || key.isEmpty())
        return;

    drift::Transition *transition = findTransition(m_project.tracks()[trackIndex], transitionId);
    if (!transition)
        return;

    beginImplicitPreviewDrag(tr("Edit transition"));

    transition->parameters.insert(
        key, coerceTransitionParam(transitionDefForId(transition->kindId), key, value));
    emitPreviewEdit(trackIndex, -1, {QStringLiteral("transition.%1.%2").arg(transitionId, key)});
}

bool AppController::setTransitionParam(int trackIndex, const QString &transitionId, const QString &key,
                                       double value)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size() || key.isEmpty())
        return false;

    drift::Track &track = m_project.tracks()[trackIndex];
    drift::Transition *transition = findTransition(track, transitionId);
    if (!transition)
        return false;

    const TransitionPresetEntry *def = transitionDefForId(transition->kindId);
    if (!def || !declaresParam(def->meta, key))
        return false;

    const QVariant coerced = coerceTransitionParam(def, key, value);
    // Already set: succeed without an edit, so a batch repeating it leaves no empty undo step.
    if (transition->parameters.value(key) == coerced)
        return true;

    const drift::Project before = m_project;
    transition->parameters.insert(key, coerced);
    pushProjectEdit(before, tr("Edit transition"));
    finishEdit(tr("Transition updated"));
    emit selectedTransitionDataChanged();
    return true;
}

QVariantMap AppController::transitionBetweenClips(int trackIndex, int clipIndex) const
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return {};

    const drift::Track &track = m_project.tracks().at(trackIndex);
    const int partnerIndex = findTransitionPartnerIndex(track, clipIndex);
    if (partnerIndex < 0)
        return {};

    const QString fromId = track.clips.at(clipIndex).id;
    const QString toId = track.clips.at(partnerIndex).id;
    for (const drift::Transition &transition : track.transitions) {
        if (transition.fromClipId == fromId && transition.toClipId == toId)
            return transitionToMap(m_project, track, transition);
    }
    return {};
}

bool AppController::hasTransitionPartner(int trackIndex, int clipIndex) const
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return false;
    const drift::Track &track = m_project.tracks().at(trackIndex);
    return trackAllowsTransitions(track.type) && findTransitionPartnerIndex(track, clipIndex) >= 0;
}

QVariantList AppController::transitionKindsForTrack(int trackIndex) const
{
    const QVariantList all = transitionKinds();
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size()
        || m_project.tracks().at(trackIndex).type != drift::TrackType::Audio)
        return all;
    QVariantList audible;
    for (const QVariant &kind : all) {
        if (transitionKindFitsTrack(drift::TrackType::Audio, kind.toMap().value(QStringLiteral("kind")).toString()))
            audible.append(kind);
    }
    return audible;
}

QVariantList AppController::transitionKinds() const
{
    const QList<TransitionPresetEntry> &catalog = transitionCatalog();

    QVariantList result;
    result.reserve(catalog.size());
    for (const TransitionPresetEntry &def : catalog) {
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
        result.append(QVariantMap{
            {QStringLiteral("kind"), def.meta.id},
            {QStringLiteral("label"), def.meta.displayName},
            {QStringLiteral("audioCurve"), def.audioCurve.isEmpty() ? QStringLiteral("crossfade") : def.audioCurve},
            {QStringLiteral("category"), def.meta.category},
            {QStringLiteral("previewStripPath"), def.previewStripPath},
            {QStringLiteral("previewFrames"), def.previewFrames},
            {QStringLiteral("user"), isUserPackageDir(def.gpu.packageDir, QStringLiteral("transitions"))},
            {QStringLiteral("params"), params},
        });
    }
    return result;
}

QVariantList AppController::transitionCategories() const
{
    QVariantList out;
    for (const auto &entry : ::transitionCategories()) {
        out.append(QVariantMap{
            {QStringLiteral("id"), entry.first},
            {QStringLiteral("label"), entry.second},
        });
    }
    return out;
}

QVariantMap AppController::selectedTransitionData() const
{
    if (m_selectedTransitionTrack < 0 || m_selectedTransitionLeftClip < 0)
        return {};
    return transitionBetweenClips(m_selectedTransitionTrack, m_selectedTransitionLeftClip);
}

void AppController::selectTransition(int trackIndex, int leftClipIndex)
{
    if (transitionBetweenClips(trackIndex, leftClipIndex).isEmpty())
        return;

    m_selectedTransitionTrack = trackIndex;
    m_selectedTransitionLeftClip = leftClipIndex;
    m_selectedTrack = trackIndex;
    m_selectedClip = leftClipIndex;
    m_selection = {qMakePair(trackIndex, leftClipIndex)};
    notifySelectionChanged();
    emit selectedTransitionDataChanged();
}

void AppController::clearTransitionSelection()
{
    if (m_selectedTransitionTrack < 0 && m_selectedTransitionLeftClip < 0)
        return;

    m_selectedTransitionTrack = -1;
    m_selectedTransitionLeftClip = -1;
    emit selectedTransitionDataChanged();
}
