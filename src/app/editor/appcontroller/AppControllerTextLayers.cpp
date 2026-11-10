#include "app/editor/appcontroller/AppControllerInternal.h"
#include "core/base/CacheRoot.h"
#include "engine/packages/procedurals/ProceduralCatalog.h"

#include "core/project/sources/DotLottie.h"
#include "core/text/animation/LottieTextImport.h"
#include "core/text/TextLook.h"
// Engine-internal by its own header comment, and included here only for releaseCaches(): the GPU
// caches have no other owner outside src/engine to ask. A one-line forwarder on GpuCompositor
// would restore the boundary.


#include <QStandardPaths>
#include <QDir>
using namespace drift::appdetail;
using drift::mcpdetail::findClipById;
using drift::mcpdetail::runBeatAnalysis;
using namespace drift::appcontroller_detail;

namespace {

QVariantMap paramSpecToMap(const drift::TextAnimParamSpec &spec)
{
    QVariantMap m{
        {QStringLiteral("id"), spec.id},
        {QStringLiteral("label"), spec.label},
        {QStringLiteral("type"), drift::textAnimParamTypeToString(spec.type)},
        {QStringLiteral("default"), slotValueToVariant(spec.defaultValue)},
    };
    if (!spec.unit.isEmpty())
        m.insert(QStringLiteral("unit"), spec.unit);
    if (!spec.group.isEmpty())
        m.insert(QStringLiteral("group"), spec.group);
    if (spec.type == drift::TextAnimParamSpec::Type::Scalar) {
        m.insert(QStringLiteral("min"), spec.min);
        m.insert(QStringLiteral("max"), spec.max);
        m.insert(QStringLiteral("step"), spec.step);
    }
    if (spec.type == drift::TextAnimParamSpec::Type::Enum)
        m.insert(QStringLiteral("values"), spec.enumValues);
    return m;
}

QVariantList paramSpecsToList(const QList<drift::TextAnimParamSpec> &specs)
{
    QVariantList out;
    for (const drift::TextAnimParamSpec &spec : specs)
        out.append(paramSpecToMap(spec));
    return out;
}
} // namespace

// ---------------------------------------------------------------------------------------------
// Text layers, looks and animation slots

drift::Clip *AppController::textClipAt(int trackIndex, int clipIndex)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return nullptr;
    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return nullptr;
    drift::Clip &clip = track.clips[clipIndex];
    if (clip.type != drift::ClipType::Text && clip.type != drift::ClipType::Subtitle)
        return nullptr;
    return &clip;
}

drift::Clip *AppController::styledClipAt(int trackIndex, int clipIndex, bool textOnly)
{
    if (drift::Clip *text = textClipAt(trackIndex, clipIndex))
        return text;
    if (textOnly || trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return nullptr;
    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return nullptr;
    drift::Clip &clip = track.clips[clipIndex];
    return clip.type == drift::ClipType::Shape ? &clip : nullptr;
}
namespace {


// The stack, its keyframes and the colour a new fill or glow borrows, whichever style the clip
// carries. A text edit also drops the look/preset the stack came from.
struct LayerStack
{
    QList<drift::TextShadingLayer> *layers = nullptr;
    QMap<QString, drift::KeyframeTrack<double>> *keyframes = nullptr;
    QColor primaryColor;
    drift::TextStyle *text = nullptr;
};

LayerStack layerStackFor(drift::Clip &clip)
{
    LayerStack stack;
    if (clip.type == drift::ClipType::Shape) {
        stack.layers = &clip.shapeStyle.layers;
        stack.keyframes = &clip.shapeStyle.keyframes;
        stack.primaryColor = clip.shapeStyle.primaryColor();
    } else {
        stack.layers = &clip.textStyle.layers;
        stack.keyframes = &clip.textStyle.keyframes;
        stack.primaryColor = clip.textStyle.primaryColor();
        stack.text = &clip.textStyle;
    }
    return stack;
}

void detachFromLook(const LayerStack &stack)
{
    if (!stack.text)
        return;
    stack.text->lookId.clear();
    stack.text->lookParams.clear();
    stack.text->packId.clear();
}

int layerIndexOf(const QList<drift::TextShadingLayer> &layers, const QString &layerId)
{
    for (int i = 0; i < layers.size(); ++i)
        if (layers.at(i).id == layerId)
            return i;
    return -1;
}

} // namespace

QString AppController::addStyleLayer(int trackIndex, int clipIndex, const QString &kind, int atIndex)
{
    drift::Clip *clip = styledClipAt(trackIndex, clipIndex);
    if (!clip)
        return {};
    const drift::Project before = m_project;
    const LayerStack stack = layerStackFor(*clip);
    QList<drift::TextShadingLayer> &layers = *stack.layers;
    const bool shape = clip->type == drift::ClipType::Shape;
    const drift::TextLayerKind layerKind = drift::textLayerKindFromString(kind);
    drift::TextShadingLayer layer;
    switch (layerKind) {
    case drift::TextLayerKind::Fill:
        layer = drift::solidFillLayer(stack.primaryColor, drift::mintTextLayerId(layers));
        break;
    case drift::TextLayerKind::Stroke:
        layer = drift::strokeLayer(shape ? 4.0 : 3.0, shape ? QColor(Qt::white) : QColor(Qt::black),
                                   drift::mintTextLayerId(layers));
        if (shape)
            layer.strokeAlign = drift::StrokeAlign::Inside;
        break;
    case drift::TextLayerKind::Shadow:
        layer = drift::shadowLayer(Qt::black, 0.0, 4.0, 8.0, 0.6, drift::mintTextLayerId(layers));
        break;
    case drift::TextLayerKind::Glow:
        layer = drift::glowLayer(stack.primaryColor, 18.0, 0.8, drift::mintTextLayerId(layers));
        break;
    case drift::TextLayerKind::Extrude:
        layer = drift::solidFillLayer(QColor(40, 40, 40), drift::mintTextLayerId(layers));
        layer.kind = drift::TextLayerKind::Extrude;
        layer.width = 6.0;
        break;
    }
    const int index = atIndex < 0 || atIndex > layers.size() ? naturalLayerIndex(layers, layerKind) : atIndex;
    layers.insert(index, layer);
    detachFromLook(stack);
    pushProjectEdit(before, tr("Add layer"));
    finishEdit(tr("Layer added"));
    return layer.id;
}

bool AppController::removeStyleLayer(int trackIndex, int clipIndex, const QString &layerId)
{
    drift::Clip *clip = styledClipAt(trackIndex, clipIndex);
    if (!clip)
        return false;
    const LayerStack stack = layerStackFor(*clip);
    const int index = layerIndexOf(*stack.layers, layerId);
    if (index < 0)
        return false;
    const drift::Project before = m_project;
    stack.layers->removeAt(index);
    drift::eraseLayerKeyframes(*stack.keyframes, layerId);
    detachFromLook(stack);
    pushProjectEdit(before, tr("Remove layer"));
    finishEdit(tr("Layer removed"));
    return true;
}

QString AppController::duplicateStyleLayer(int trackIndex, int clipIndex, const QString &layerId)
{
    drift::Clip *clip = styledClipAt(trackIndex, clipIndex);
    if (!clip)
        return {};
    const LayerStack stack = layerStackFor(*clip);
    const int index = layerIndexOf(*stack.layers, layerId);
    if (index < 0)
        return {};
    const drift::Project before = m_project;
    drift::TextShadingLayer copy = stack.layers->at(index);
    copy.id = drift::mintTextLayerId(*stack.layers);
    stack.layers->insert(index + 1, copy);
    detachFromLook(stack);
    pushProjectEdit(before, tr("Duplicate layer"));
    finishEdit(tr("Layer duplicated"));
    return copy.id;
}

bool AppController::moveStyleLayer(int trackIndex, int clipIndex, const QString &layerId, int toIndex)
{
    drift::Clip *clip = styledClipAt(trackIndex, clipIndex);
    if (!clip)
        return false;
    const LayerStack stack = layerStackFor(*clip);
    const int index = layerIndexOf(*stack.layers, layerId);
    toIndex = qBound(0, toIndex, stack.layers->size() - 1);
    if (index < 0 || index == toIndex)
        return false;
    const drift::Project before = m_project;
    stack.layers->move(index, toIndex);
    detachFromLook(stack);
    pushProjectEdit(before, tr("Reorder layers"));
    finishEdit(tr("Layer moved"));
    return true;
}

void AppController::setStyleLayer(int trackIndex, int clipIndex, const QString &layerId, const QVariantMap &patch)
{
    drift::Clip *clip = styledClipAt(trackIndex, clipIndex);
    if (!clip)
        return;
    QVariantMap layer = patch;
    layer.insert(QStringLiteral("id"), layerId);
    if (clip->type == drift::ClipType::Shape)
        setShapeStyle(trackIndex, clipIndex, QVariantMap{{QStringLiteral("layer"), layer}});
    else
        setTextStyle(trackIndex, clipIndex, QVariantMap{{QStringLiteral("layer"), layer}});
}

void AppController::previewSetStyleLayer(int trackIndex, int clipIndex, const QString &layerId, const QVariantMap &patch)
{
    drift::Clip *clip = styledClipAt(trackIndex, clipIndex);
    if (!clip)
        return;
    beginImplicitPreviewDrag(tr("Edit layer"));
    QVariantMap layer = patch;
    layer.insert(QStringLiteral("id"), layerId);
    if (clip->type == drift::ClipType::Shape)
        applyShapeStylePatch(clip->shapeStyle, QVariantMap{{QStringLiteral("layer"), layer}});
    else
        applyTextStylePatch(clip->textStyle, QVariantMap{{QStringLiteral("layer"), layer}});
    emitPreviewEdit(trackIndex, clipIndex, {QStringLiteral("style.%1.*").arg(layerId)});
}

QString AppController::addTextLayer(int trackIndex, int clipIndex, const QString &kind, int atIndex)
{
    return textClipAt(trackIndex, clipIndex) ? addStyleLayer(trackIndex, clipIndex, kind, atIndex) : QString();
}

bool AppController::removeTextLayer(int trackIndex, int clipIndex, const QString &layerId)
{
    return textClipAt(trackIndex, clipIndex) && removeStyleLayer(trackIndex, clipIndex, layerId);
}

QString AppController::duplicateTextLayer(int trackIndex, int clipIndex, const QString &layerId)
{
    return textClipAt(trackIndex, clipIndex) ? duplicateStyleLayer(trackIndex, clipIndex, layerId) : QString();
}

bool AppController::moveTextLayer(int trackIndex, int clipIndex, const QString &layerId, int toIndex)
{
    return textClipAt(trackIndex, clipIndex) && moveStyleLayer(trackIndex, clipIndex, layerId, toIndex);
}

void AppController::setTextLayer(int trackIndex, int clipIndex, const QString &layerId, const QVariantMap &patch)
{
    if (textClipAt(trackIndex, clipIndex))
        setStyleLayer(trackIndex, clipIndex, layerId, patch);
}

void AppController::previewSetTextLayer(int trackIndex, int clipIndex, const QString &layerId, const QVariantMap &patch)
{
    if (textClipAt(trackIndex, clipIndex))
        previewSetStyleLayer(trackIndex, clipIndex, layerId, patch);
}

void AppController::setTextAnimationSlot(int trackIndex, int clipIndex, const QString &slot, const QVariantMap &patch)
{
    drift::Clip *clip = textClipAt(trackIndex, clipIndex);
    if (!clip)
        return;
    if (slot != QLatin1String("in") && slot != QLatin1String("out") && slot != QLatin1String("loop"))
        return;
    const drift::Project before = m_project;
    applyTextAnimationSetPatch(&clip->textStyle.animation, QVariantMap{{slot, patch}});
    clip->textStyle.packId.clear();
    pushProjectEdit(before, tr("Edit text animation"));
    finishEdit(tr("Text animation updated"));
}

void AppController::previewSetTextAnimationSlot(int trackIndex, int clipIndex, const QString &slot,
                                                const QVariantMap &patch)
{
    drift::Clip *clip = textClipAt(trackIndex, clipIndex);
    if (!clip)
        return;
    if (slot != QLatin1String("in") && slot != QLatin1String("out") && slot != QLatin1String("loop"))
        return;
    beginImplicitPreviewDrag(tr("Edit text animation"));
    applyTextAnimationSetPatch(&clip->textStyle.animation, QVariantMap{{slot, patch}});
    emitPreviewEdit(trackIndex, clipIndex, {QStringLiteral("textAnim.%1").arg(slot)});
}

void AppController::clearTextAnimationSlot(int trackIndex, int clipIndex, const QString &slot)
{
    setTextAnimationSlot(trackIndex, clipIndex, slot, QVariantMap{{QStringLiteral("preset"), QString()}});
}

QVariantMap AppController::textAnimationPresetToMap(const drift::TextAnimationPreset &preset)
{
    QVariantList slots_;
    for (drift::TextAnimSlotKind kind : preset.slotKinds)
        slots_.append(drift::textAnimSlotKindToString(kind));
    return {
        {QStringLiteral("id"), preset.id},
        {QStringLiteral("label"), preset.label},
        {QStringLiteral("category"), preset.category},
        {QStringLiteral("sampleText"), preset.sampleText},
        {QStringLiteral("slots"), slots_},
        {QStringLiteral("order"), preset.order},
        {QStringLiteral("builtIn"), preset.builtIn},
        {QStringLiteral("params"), paramSpecsToList(preset.params)},
        {QStringLiteral("flags"), preset.flags.toVariantMap()},
        {QStringLiteral("report"), preset.report},
    };
}

QVariantList AppController::textAnimationPresets(const QString &slot) const
{
    QVariantList out;
    const QList<drift::TextAnimationPreset> presets =
        slot.isEmpty() ? drift::TextAnimationPresetCatalog::instance().presets()
                       : drift::TextAnimationPresetCatalog::instance().presetsFor(drift::textAnimSlotKindFromString(slot));
    for (const drift::TextAnimationPreset &preset : presets)
        out.append(textAnimationPresetToMap(preset));
    return out;
}

QVariantList AppController::textAnimationCategories(const QString &slot) const
{
    QStringList seen;
    for (const QVariant &v : textAnimationPresets(slot)) {
        const QString category = v.toMap().value(QStringLiteral("category")).toString();
        if (!seen.contains(category))
            seen.append(category);
    }
    static const QMap<QString, QString> labels{
        {QStringLiteral("basic"), tr("Basic")},         {QStringLiteral("character"), tr("By character")},
        {QStringLiteral("word"), tr("By word")},        {QStringLiteral("kinetic"), tr("Kinetic")},
        {QStringLiteral("light"), tr("Light")},         {QStringLiteral("colour"), tr("Colour")},
        {QStringLiteral("hold"), tr("Hold")},           {QStringLiteral("imported"), tr("Imported")},
    };
    QVariantList out;
    for (const QString &id : seen)
        out.append(QVariantMap{{QStringLiteral("id"), id}, {QStringLiteral("label"), labels.value(id, id)}});
    return out;
}

double AppController::textAnimationSlotStartSeconds(int trackIndex, int clipIndex, const QString &slot) const
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return playheadSeconds();
    const drift::Track &track = m_project.tracks().at(trackIndex);
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return playheadSeconds();
    const drift::Clip &clip = track.clips.at(clipIndex);
    drift::TimeUs start = clip.timelineStart;
    drift::TimeUs end = clip.timelineStart + clip.timelineDuration;
    if (clip.type == drift::ClipType::Subtitle) {
        const drift::TimeUs localUs = m_playheadUs - clip.timelineStart;
        if (const drift::SubtitleCue *cue = drift::activeSubtitleCueAt(clip.subtitleCues, localUs)) {
            start = clip.timelineStart + cue->startUs;
            end = clip.timelineStart + cue->endUs;
        }
    }
    if (slot == QLatin1String("out")) {
        const QVariantMap m = textAnimationSlotToMap(clip.textStyle.animation.out);
        const double seconds = m.value(QStringLiteral("duration"), 0.4).toDouble() + m.value(QStringLiteral("stagger"), 0.0).toDouble() * 4.0;
        return qMax(drift::usToSeconds(start), drift::usToSeconds(end) - seconds - 0.2);
    }
    if (slot == QLatin1String("loop"))
        return playheadSeconds();
    return drift::usToSeconds(start);
}

QVariantList AppController::textLooks() const
{
    QVariantList out;
    for (const drift::TextLook &look : drift::textLooks()) {
        out.append(QVariantMap{{QStringLiteral("id"), look.id},
                               {QStringLiteral("label"), look.label},
                               {QStringLiteral("params"), paramSpecsToList(look.params)}});
    }
    return out;
}

void AppController::applyTextLook(int trackIndex, int clipIndex, const QString &lookId, const QVariantMap &params)
{
    drift::Clip *clip = textClipAt(trackIndex, clipIndex);
    if (!clip)
        return;
    const drift::TextLook *look = drift::textLookForId(lookId);
    if (!look)
        return;
    const drift::Project before = m_project;
    QMap<QString, drift::VectorSlotValue> typed;
    mergeParamsFromMap(&typed, params, look->params);
    drift::applyTextLook(clip->textStyle, lookId, typed);
    clip->textStyle.packId.clear();
    pushProjectEdit(before, tr("Apply text look"));
    finishEdit(tr("Look applied"));
}

void AppController::setTextLookParam(int trackIndex, int clipIndex, const QString &key, const QVariant &value)
{
    drift::Clip *clip = textClipAt(trackIndex, clipIndex);
    if (!clip || clip->textStyle.lookId.isEmpty())
        return;
    const drift::TextLook *look = drift::textLookForId(clip->textStyle.lookId);
    if (!look)
        return;
    const drift::Project before = m_project;
    QMap<QString, drift::VectorSlotValue> params = clip->textStyle.lookParams;
    mergeParamsFromMap(&params, QVariantMap{{key, value}}, look->params);
    drift::applyTextLook(clip->textStyle, look->id, params);
    pushProjectEdit(before, tr("Adjust text look"));
    finishEdit(tr("Look updated"));
}

void AppController::previewSetTextLookParam(int trackIndex, int clipIndex, const QString &key, const QVariant &value)
{
    drift::Clip *clip = textClipAt(trackIndex, clipIndex);
    if (!clip || clip->textStyle.lookId.isEmpty())
        return;
    const drift::TextLook *look = drift::textLookForId(clip->textStyle.lookId);
    if (!look)
        return;
    beginImplicitPreviewDrag(tr("Adjust text look"));
    QMap<QString, drift::VectorSlotValue> params = clip->textStyle.lookParams;
    mergeParamsFromMap(&params, QVariantMap{{key, value}}, look->params);
    drift::applyTextLook(clip->textStyle, look->id, params);
    emitPreviewEdit(trackIndex, clipIndex, {QStringLiteral("textLook.%1").arg(key)});
}

QVariantList AppController::textPaintEffects() const
{
    QVariantList out;
    for (const drift::TextEffectSpec &spec : drift::textShaderEffectSpecs()) {
        out.append(QVariantMap{{QStringLiteral("id"), spec.id},
                               {QStringLiteral("label"), spec.label},
                               {QStringLiteral("params"), paramSpecsToList(spec.params)}});
    }
    return out;
}

QVariantList AppController::textGradientPresets() const
{
    QVariantList out;
    for (const drift::TextGradientPreset &preset : drift::textGradientPresets()) {
        QVariantList stops;
        for (const drift::TextGradientStop &stop : preset.gradient.stops)
            stops.append(QVariantMap{{QStringLiteral("pos"), stop.pos}, {QStringLiteral("color"), stop.color.name(QColor::HexArgb)}});
        out.append(QVariantMap{{QStringLiteral("id"), preset.id},
                               {QStringLiteral("label"), preset.label},
                               {QStringLiteral("kind"), drift::textGradientKindToString(preset.gradient.kind)},
                               {QStringLiteral("angle"), preset.gradient.angle},
                               {QStringLiteral("stops"), stops}});
    }
    return out;
}

int AppController::applyTextStyleToCaptions(int trackIndex, int clipIndex, const QString &scope)
{
    const drift::Clip *source = textClipAt(trackIndex, clipIndex);
    if (!source)
        return 0;
    drift::TextStyle style = source->textStyle;
    style.keyframes.clear();
    const drift::Project before = m_project;
    int changed = 0;
    for (int t = 0; t < m_project.tracks().size(); ++t) {
        if (scope != QLatin1String("project") && t != trackIndex)
            continue;
        drift::Track &track = m_project.tracks()[t];
        for (int c = 0; c < track.clips.size(); ++c) {
            drift::Clip &clip = track.clips[c];
            if (clip.type != drift::ClipType::Subtitle || (t == trackIndex && c == clipIndex))
                continue;
            const QMap<QString, drift::KeyframeTrack<double>> keep = clip.textStyle.keyframes;
            clip.textStyle = style;
            clip.textStyle.keyframes = keep;
            ++changed;
        }
    }
    if (changed == 0)
        return 0;
    pushProjectEdit(before, tr("Apply caption style"));
    finishEdit(tr("Applied to %n caption clip(s)", "", changed));
    return changed;
}

QString AppController::keyframePropertyLabel(int trackIndex, int clipIndex, const QString &prop) const
{
    const drift::Clip *clip = nullptr;
    if (trackIndex >= 0 && trackIndex < m_project.tracks().size()) {
        const drift::Track &track = m_project.tracks().at(trackIndex);
        if (clipIndex >= 0 && clipIndex < track.clips.size())
            clip = &track.clips.at(clipIndex);
    }
    if (prop.startsWith(QLatin1String("text.")))
        return drift::textKeyframeLabel(prop.mid(5), clip ? clip->textStyle : drift::TextStyle{});
    if (prop.startsWith(QLatin1String("shape.")))
        return drift::shapeKeyframeLabel(prop.mid(6), clip ? clip->shapeStyle : drift::ShapeStyle{});
    if (prop.startsWith(QLatin1String("model3d.")))
        return drift::model3dKeyframeLabel(prop.mid(8));
    if (prop.startsWith(QLatin1String("pdf.")))
        return drift::pdfKeyframeLabel(prop.mid(4));
    if (prop.startsWith(QLatin1String("proc."))) {
        const QString key = prop.mid(5);
        if (clip) {
            for (const drift::EffectParamSpec &spec : proceduralForId(clip->procedural.packageId).parameters) {
                if (spec.key == key)
                    return spec.label.isEmpty() ? key : spec.label;
            }
        }
        return key;
    }
    if (prop.startsWith(QLatin1String("vector."))) {
        QString key = prop.mid(7);
        QString channel;
        drift::SvgOverrideKey parsed;
        if (!drift::parseSvgOverrideKey(key, &parsed)) {
            const int dot = key.lastIndexOf(QLatin1Char('.'));
            if (dot < 0 || !drift::parseSvgOverrideKey(key.left(dot), &parsed))
                return {};
            static const QMap<QString, QString> channels{
                {QStringLiteral("r"), tr("Red")}, {QStringLiteral("g"), tr("Green")},
                {QStringLiteral("b"), tr("Blue")}, {QStringLiteral("a"), tr("Alpha")}};
            channel = channels.value(key.mid(dot + 1));
        }
        const QString label = drift::svgOverrideLabel(parsed);
        return channel.isEmpty() ? label : label + QStringLiteral(" · ") + channel;
    }
    return {};
}

QVariantMap AppController::importTextAnimationPreset(const QUrl &fileUrl, const QString &slot)
{
    const QString path = fileUrl.isLocalFile() ? fileUrl.toLocalFile() : fileUrl.toString();
    QVariantMap result{{QStringLiteral("ok"), false}};
    QStringList files{path};
    if (drift::isDotLottiePath(path)) {
        QString unpackError;
        files = drift::unpackDotLottie(path, QDir(drift::cacheRoot())
                                                .filePath(QStringLiteral("dotlottie")), &unpackError);
        if (files.isEmpty()) {
            result.insert(QStringLiteral("error"), unpackError.isEmpty() ? tr("Could not unpack the bundle") : unpackError);
            return result;
        }
    }
    QFile file(files.first());
    if (!file.open(QIODevice::ReadOnly)) {
        result.insert(QStringLiteral("error"), tr("Could not read %1").arg(path));
        return result;
    }
    const QByteArray json = file.readAll();
    std::optional<drift::TextAnimationPreset> preset;
    QStringList warnings;
    QString error;
    const QJsonObject doc = QJsonDocument::fromJson(json).object();
    if (doc.contains(QStringLiteral("animators")) && doc.contains(QStringLiteral("id"))) {
        // One of our own exported presets.
        preset = drift::TextAnimationPreset::fromJson(doc, &error);
    } else {
        drift::lottie::ImportOptions options;
        options.category = slot;
        preset = drift::lottie::importLottieTextPreset(json, options, &warnings, &error);
    }
    if (!preset) {
        result.insert(QStringLiteral("error"), error.isEmpty() ? tr("Nothing to import") : error);
        return result;
    }
    if (!slot.isEmpty())
        preset->slotKinds = {drift::textAnimSlotKindFromString(slot)};
    preset->category = QStringLiteral("imported");
    const QString id = drift::TextAnimationPresetCatalog::instance().addUserPreset(*preset);
    if (id.isEmpty()) {
        result.insert(QStringLiteral("error"), tr("Could not save the preset"));
        return result;
    }
    result.insert(QStringLiteral("ok"), true);
    result.insert(QStringLiteral("id"), id);
    result.insert(QStringLiteral("unsupported"), warnings);
    emit userTextAnimationPresetsChanged();
    return result;
}

bool AppController::renameUserTextAnimationPreset(const QString &presetId, const QString &label)
{
    if (label.trimmed().isEmpty())
        return false;
    const bool ok = drift::TextAnimationPresetCatalog::instance().renameUserPreset(presetId, label.trimmed());
    if (ok)
        emit userTextAnimationPresetsChanged();
    return ok;
}

bool AppController::deleteUserTextAnimationPreset(const QString &presetId)
{
    const bool ok = drift::TextAnimationPresetCatalog::instance().removeUserPreset(presetId);
    if (ok)
        emit userTextAnimationPresetsChanged();
    return ok;
}

bool AppController::exportUserTextAnimationPreset(const QString &presetId, const QUrl &fileUrl)
{
    const std::optional<drift::TextAnimationPreset> preset =
        drift::TextAnimationPresetCatalog::instance().presetForId(presetId);
    if (!preset)
        return false;
    QFile file(fileUrl.isLocalFile() ? fileUrl.toLocalFile() : fileUrl.toString());
    if (!file.open(QIODevice::WriteOnly))
        return false;
    QJsonObject json = preset->toJson();
    json.insert(QStringLiteral("id"), drift::isUserTextAnimationPresetId(preset->id) ? preset->id.mid(5) : preset->id);
    file.write(QJsonDocument(json).toJson(QJsonDocument::Indented));
    return true;
}
