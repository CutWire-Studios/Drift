#include "app/editor/appcontroller/AppControllerInternal.h"
#include "app/editor/AppControllerDetail.h"
#include "app/bin/AssetLibrary.h"
#include "core/text/animation/TextAnimationPreset.h"
#include "engine/render2d/pdf/PdfiumRuntime.h"
#include "engine/packages/effects/EffectCatalog.h"
#include "engine/packages/procedurals/ProceduralCatalog.h"
#include "engine/decode/probe/MediaProbe.h"
#include "engine/render3d/models/ModelAsset.h"
#include "engine/render3d/models/ModelClipRenderer.h"
#include "engine/render3d/splats/SplatAsset.h"
#include "engine/packages/transitions/TransitionCatalog.h"
#include "app/mcp/McpDetail.h"

#include <QStandardPaths>
#include <QDir>

namespace drift::appcontroller_detail {

bool isUserPackageDir(const QString &packageDir, const QString &subdir)
{
    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (packageDir.isEmpty() || appData.isEmpty())
        return false;
    const QString root = QDir::cleanPath(QDir(appData).filePath(subdir)) + QLatin1Char('/');
    return QDir::cleanPath(packageDir).startsWith(root);
}

bool clipsOverlapCompletely(const drift::Clip &a, const drift::Clip &b)
{
    const drift::TimeUs overlap = qMin(a.timelineEnd(), b.timelineEnd()) - qMax(a.timelineStart, b.timelineStart);
    return overlap > 0 && overlap >= qMin(a.timelineDuration, b.timelineDuration);
}

bool editCreatesCompleteOverlap(const drift::Project &before, const drift::Project &after)
{
    QSet<QPair<QString, QString>> existing;
    for (const drift::Track &track : before.tracks()) {
        for (int i = 0; i < track.clips.size(); ++i)
            for (int j = i + 1; j < track.clips.size(); ++j)
                if (clipsOverlapCompletely(track.clips.at(i), track.clips.at(j))) {
                    existing.insert(qMakePair(track.clips.at(i).id, track.clips.at(j).id));
                    existing.insert(qMakePair(track.clips.at(j).id, track.clips.at(i).id));
                }
    }
    for (const drift::Track &track : after.tracks()) {
        for (int i = 0; i < track.clips.size(); ++i)
            for (int j = i + 1; j < track.clips.size(); ++j)
                if (clipsOverlapCompletely(track.clips.at(i), track.clips.at(j))
                    && !existing.contains(qMakePair(track.clips.at(i).id, track.clips.at(j).id)))
                    return true;
    }
    return false;
}

int findTransitionPartnerIndex(const drift::Track &track, int fromIndex)
{
    if (fromIndex < 0 || fromIndex >= track.clips.size())
        return -1;

    const drift::Clip &fromClip = track.clips.at(fromIndex);
    int best = -1;
    drift::TimeUs bestStart = std::numeric_limits<drift::TimeUs>::min();
    for (int i = 0; i < track.clips.size(); ++i) {
        if (i == fromIndex)
            continue;
        const drift::Clip &candidate = track.clips.at(i);
        if (!drift::clipsEligibleForTransition(fromClip, candidate))
            continue;
        // Eligible clips are everything starting between this clip's own start and just past its
        // end, so with overlap on that can include one that covers it almost entirely. The partner
        // meant by "the next clip" is the one starting nearest the cut, not the earliest of them.
        if (candidate.timelineStart > bestStart) {
            bestStart = candidate.timelineStart;
            best = i;
        }
    }
    return best;
}

bool trackAllowsTransitions(drift::TrackType type)
{
    return type == drift::TrackType::Video || type == drift::TrackType::Shape
           || type == drift::TrackType::Text || type == drift::TrackType::Audio;
}

void syncOverlapTransitionsOnTrack(drift::Track &track)
{
    constexpr drift::TimeUs kDefaultAdjacentDurationUs = drift::secondsToUs(0.5);

    {
        if (!trackAllowsTransitions(track.type))
            return;

        // Overlap is stacking, not an implicit fade. Only keep and retune transitions the
        // user (or MCP) actually added.
        for (int i = track.transitions.size() - 1; i >= 0; --i) {
            drift::Transition &transition = track.transitions[i];
            const drift::Clip *fromClip = drift::clipById(track, transition.fromClipId);
            const drift::Clip *toClip = drift::clipById(track, transition.toClipId);
            if (!fromClip || !toClip || !drift::clipsEligibleForTransition(*fromClip, *toClip)) {
                track.transitions.removeAt(i);
                continue;
            }

            if (drift::clipsPhysicallyOverlap(*fromClip, *toClip)) {
                const drift::TimeUs overlapUs = drift::physicalOverlapDurationUs(*fromClip, *toClip);
                if (overlapUs > 0)
                    transition.durationUs = overlapUs;
                continue;
            }

            // Adjacent: a leftover overlap duration can be longer than either clip, which
            // would paint a virtual window from before t=0. Explicit 0.75s fades stay put.
            const drift::TimeUs shorter =
                qMin(fromClip->timelineDuration, toClip->timelineDuration);
            if (transition.durationUs > shorter)
                transition.durationUs = qMin(kDefaultAdjacentDurationUs, shorter);
        }
    }
}

QVariant slotValueToVariant(const drift::VectorSlotValue &v)
{
    switch (v.type) {
    case drift::VectorSlotValue::Type::Color:
        return v.color.name(QColor::HexArgb);
    case drift::VectorSlotValue::Type::Scalar:
        return v.scalar;
    case drift::VectorSlotValue::Type::Vec2:
        return QVariantList{v.vec2.x(), v.vec2.y()};
    case drift::VectorSlotValue::Type::Text:
        return v.text;
    case drift::VectorSlotValue::Type::Image:
        return v.image;
    }
    return {};
}

drift::VectorSlotValue slotValueFromVariant(const QVariant &v, drift::VectorSlotValue::Type hint)
{
    if (v.userType() == QMetaType::QVariantMap) {
        const QVariantMap m = v.toMap();
        if (m.contains(QStringLiteral("type")))
            return drift::VectorSlotValue::fromJson(QJsonObject::fromVariantMap(m));
    }
    switch (hint) {
    case drift::VectorSlotValue::Type::Color:
        return drift::VectorSlotValue::fromColor(QColor(v.toString()));
    case drift::VectorSlotValue::Type::Scalar:
        if (v.userType() == QMetaType::Bool)
            return drift::VectorSlotValue::fromScalar(v.toBool() ? 1.0 : 0.0);
        return drift::VectorSlotValue::fromScalar(v.toDouble());
    case drift::VectorSlotValue::Type::Vec2: {
        const QVariantList l = v.toList();
        return drift::VectorSlotValue::fromVec2(QPointF(l.value(0).toDouble(), l.value(1).toDouble()));
    }
    case drift::VectorSlotValue::Type::Text:
        return drift::VectorSlotValue::fromText(v.toString());
    case drift::VectorSlotValue::Type::Image:
        return drift::VectorSlotValue::fromImage(v.toString());
    }
    return drift::VectorSlotValue::fromScalar(v.toDouble());
}

drift::VectorSlotValue::Type slotTypeForSpec(const drift::TextAnimParamSpec &spec)
{
    switch (spec.type) {
    case drift::TextAnimParamSpec::Type::Color:
        return drift::VectorSlotValue::Type::Color;
    case drift::TextAnimParamSpec::Type::Vec2:
        return drift::VectorSlotValue::Type::Vec2;
    case drift::TextAnimParamSpec::Type::Text:
    case drift::TextAnimParamSpec::Type::Enum:
        return drift::VectorSlotValue::Type::Text;
    case drift::TextAnimParamSpec::Type::Scalar:
    case drift::TextAnimParamSpec::Type::Bool:
        return drift::VectorSlotValue::Type::Scalar;
    }
    return drift::VectorSlotValue::Type::Scalar;
}

drift::VectorSlotValue::Type guessSlotType(const QVariant &v)
{
    if (v.userType() == QMetaType::QString) {
        const QString s = v.toString();
        return s.startsWith(QLatin1Char('#')) ? drift::VectorSlotValue::Type::Color : drift::VectorSlotValue::Type::Text;
    }
    if (v.userType() == QMetaType::QVariantList)
        return drift::VectorSlotValue::Type::Vec2;
    return drift::VectorSlotValue::Type::Scalar;
}

QVariantMap paramsToMap(const QMap<QString, drift::VectorSlotValue> &params)
{
    QVariantMap out;
    for (auto it = params.constBegin(); it != params.constEnd(); ++it)
        out.insert(it.key(), slotValueToVariant(*it));
    return out;
}

void mergeParamsFromMap(QMap<QString, drift::VectorSlotValue> *params, const QVariantMap &m,
                        const QList<drift::TextAnimParamSpec> &specs)
{
    for (auto it = m.constBegin(); it != m.constEnd(); ++it) {
        drift::VectorSlotValue::Type hint = guessSlotType(*it);
        for (const drift::TextAnimParamSpec &spec : specs) {
            if (spec.id == it.key()) {
                hint = slotTypeForSpec(spec);
                break;
            }
        }
        params->insert(it.key(), slotValueFromVariant(*it, hint));
    }
}

QList<drift::TextAnimParamSpec> specsForSlot(const drift::TextAnimationSlot &slot)
{
    if (!slot.presetId.isEmpty()) {
        if (const std::optional<drift::TextAnimationPreset> preset =
                drift::TextAnimationPresetCatalog::instance().presetForId(slot.presetId))
            return preset->params;
    }
    return drift::textAnimationCommonParams();
}

extern const char *const kSlotShortcuts[] = {"duration", "stagger", "unit", "order", "ease", "period", "amount"};

QVariantMap textAnimationSlotToMap(const drift::TextAnimationSlot &slot)
{
    QVariantMap m{
        {QStringLiteral("preset"), slot.presetId},
        {QStringLiteral("enabled"), slot.enabled},
        {QStringLiteral("custom"), slot.presetId.isEmpty() && !slot.animators.isEmpty()},
    };
    // An empty slot is just that; its defaults would only bloat every clip row.
    if (slot.presetId.isEmpty() && slot.animators.isEmpty())
        return m;
    m.insert(QStringLiteral("delay"), drift::usToSeconds(slot.delayUs));
    m.insert(QStringLiteral("period"), drift::usToSeconds(slot.periodUs));
    QMap<QString, drift::VectorSlotValue> params;
    for (const drift::TextAnimParamSpec &spec : specsForSlot(slot))
        params.insert(spec.id, spec.defaultValue);
    for (auto it = slot.params.constBegin(); it != slot.params.constEnd(); ++it)
        params.insert(it.key(), *it);
    m.insert(QStringLiteral("params"), paramsToMap(params));
    for (const char *key : kSlotShortcuts) {
        const auto it = params.constFind(QLatin1String(key));
        if (it != params.constEnd())
            m.insert(QLatin1String(key), slotValueToVariant(*it));
    }
    if (slot.presetId.isEmpty() && !slot.animators.isEmpty()) {
        QVariantList animators;
        for (const drift::TextAnimator &a : slot.animators)
            animators.append(drift::textAnimatorToJson(a).toVariantMap());
        m.insert(QStringLiteral("animators"), animators);
    }
    return m;
}

QVariantMap textAnimationSetToMap(const drift::TextAnimationSet &set)
{
    const drift::TextCaret &c = set.caret;
    return {
        {QStringLiteral("in"), textAnimationSlotToMap(set.in)},
        {QStringLiteral("out"), textAnimationSlotToMap(set.out)},
        {QStringLiteral("loop"), textAnimationSlotToMap(set.loop)},
        {QStringLiteral("caret"), QVariantMap{{QStringLiteral("enabled"), c.enabled},
                                              {QStringLiteral("lead"), drift::usToSeconds(c.leadUs)},
                                              {QStringLiteral("blinkOn"), drift::usToSeconds(c.blinkOnUs)},
                                              {QStringLiteral("blinkOff"), drift::usToSeconds(c.blinkOffUs)},
                                              {QStringLiteral("holdAfter"), c.holdAfterUs < 0 ? -1.0 : drift::usToSeconds(c.holdAfterUs)},
                                              {QStringLiteral("widthEm"), c.widthEm},
                                              {QStringLiteral("heightEm"), c.heightEm},
                                              {QStringLiteral("shape"), drift::textCaretShapeToString(c.shape)},
                                              {QStringLiteral("color"), c.color.isValid() ? c.color.name(QColor::HexArgb) : QString()}}},
        {QStringLiteral("anchorGrouping"), drift::textAnchorGroupingToString(set.anchorGrouping)},
        {QStringLiteral("anchorAlignment"), QVariantList{set.anchorAlignment.x(), set.anchorAlignment.y()}},
        {QStringLiteral("custom"), (set.in.presetId.isEmpty() && !set.in.animators.isEmpty())
                                       || (set.out.presetId.isEmpty() && !set.out.animators.isEmpty())
                                       || (set.loop.presetId.isEmpty() && !set.loop.animators.isEmpty())},
    };
}

void applyTextAnimationSlotPatch(drift::TextAnimationSlot *slot, const QVariantMap &m)
{
    if (m.contains(QStringLiteral("preset"))) {
        const QString id = m.value(QStringLiteral("preset")).toString();
        if (id.isEmpty() || id == QLatin1String("none")) {
            *slot = drift::TextAnimationSlot{};
        } else if (id != slot->presetId) {
            QMap<QString, drift::VectorSlotValue> kept;
            if (m.value(QStringLiteral("keepControls")).toBool()) {
                for (const char *key : kSlotShortcuts) {
                    const auto it = slot->params.constFind(QLatin1String(key));
                    if (it != slot->params.constEnd())
                        kept.insert(QLatin1String(key), *it);
                }
            }
            slot->presetId = id;
            slot->params = kept;
            slot->animators.clear();
            slot->durationUs = 0;
            slot->delayUs = 0;
            slot->periodUs = 0;
            slot->enabled = true;
        }
    }
    if (m.contains(QStringLiteral("animators"))) {
        slot->animators.clear();
        for (const QVariant &v : m.value(QStringLiteral("animators")).toList())
            slot->animators.append(drift::textAnimatorFromJson(QJsonObject::fromVariantMap(v.toMap())));
        slot->presetId.clear();
        slot->params.clear();
        slot->enabled = true;
    }
    const QList<drift::TextAnimParamSpec> specs = specsForSlot(*slot);
    mergeParamsFromMap(&slot->params, m.value(QStringLiteral("params")).toMap(), specs);
    QVariantMap shortcuts;
    for (const char *key : kSlotShortcuts) {
        if (m.contains(QLatin1String(key)))
            shortcuts.insert(QLatin1String(key), m.value(QLatin1String(key)));
    }
    mergeParamsFromMap(&slot->params, shortcuts, specs);
    if (m.contains(QStringLiteral("delay")))
        slot->delayUs = drift::secondsToUs(qMax(0.0, m.value(QStringLiteral("delay")).toDouble()));
    if (m.contains(QStringLiteral("durationOverride")))
        slot->durationUs = drift::secondsToUs(qMax(0.0, m.value(QStringLiteral("durationOverride")).toDouble()));
    if (m.contains(QStringLiteral("period")))
        slot->periodUs = drift::secondsToUs(qMax(0.0, m.value(QStringLiteral("period")).toDouble()));
    if (m.contains(QStringLiteral("enabled")))
        slot->enabled = m.value(QStringLiteral("enabled")).toBool();
}

void applyTextCaretPatch(drift::TextCaret *c, const QVariantMap &m)
{
    if (m.contains(QStringLiteral("enabled")))
        c->enabled = m.value(QStringLiteral("enabled")).toBool();
    if (m.contains(QStringLiteral("lead")))
        c->leadUs = drift::secondsToUs(qMax(0.0, m.value(QStringLiteral("lead")).toDouble()));
    if (m.contains(QStringLiteral("blinkOn")))
        c->blinkOnUs = drift::secondsToUs(qMax(0.0, m.value(QStringLiteral("blinkOn")).toDouble()));
    if (m.contains(QStringLiteral("blinkOff")))
        c->blinkOffUs = drift::secondsToUs(qMax(0.0, m.value(QStringLiteral("blinkOff")).toDouble()));
    if (m.contains(QStringLiteral("holdAfter"))) {
        const double v = m.value(QStringLiteral("holdAfter")).toDouble();
        c->holdAfterUs = v < 0.0 ? -1 : drift::secondsToUs(v);
    }
    if (m.contains(QStringLiteral("widthEm")))
        c->widthEm = qBound(0.01, m.value(QStringLiteral("widthEm")).toDouble(), 2.0);
    if (m.contains(QStringLiteral("heightEm")))
        c->heightEm = qBound(0.1, m.value(QStringLiteral("heightEm")).toDouble(), 2.0);
    if (m.contains(QStringLiteral("shape")))
        c->shape = drift::textCaretShapeFromString(m.value(QStringLiteral("shape")).toString());
    if (m.contains(QStringLiteral("color"))) {
        const QString color = m.value(QStringLiteral("color")).toString();
        c->color = color.isEmpty() ? QColor() : QColor(color);
    }
}

void applyTextAnimationSetPatch(drift::TextAnimationSet *set, const QVariantMap &m)
{
    if (m.contains(QStringLiteral("in")))
        applyTextAnimationSlotPatch(&set->in, m.value(QStringLiteral("in")).toMap());
    if (m.contains(QStringLiteral("out")))
        applyTextAnimationSlotPatch(&set->out, m.value(QStringLiteral("out")).toMap());
    if (m.contains(QStringLiteral("loop")))
        applyTextAnimationSlotPatch(&set->loop, m.value(QStringLiteral("loop")).toMap());
    if (m.contains(QStringLiteral("caret")))
        applyTextCaretPatch(&set->caret, m.value(QStringLiteral("caret")).toMap());
    if (m.contains(QStringLiteral("anchorGrouping")))
        set->anchorGrouping = drift::textAnchorGroupingFromString(m.value(QStringLiteral("anchorGrouping")).toString());
    if (m.contains(QStringLiteral("anchorAlignment"))) {
        const QVariantList l = m.value(QStringLiteral("anchorAlignment")).toList();
        set->anchorAlignment = QPointF(qBound(-1.0, l.value(0).toDouble(), 1.0), qBound(-1.0, l.value(1).toDouble(), 1.0));
    }
}

const QMap<QString, QString> &legacyKindForPreset()
{
    static const QMap<QString, QString> kinds{
        {QStringLiteral("fade"), QStringLiteral("fade")},       {QStringLiteral("slide-up"), QStringLiteral("slideUp")},
        {QStringLiteral("slide-down"), QStringLiteral("slideDown")}, {QStringLiteral("slide-left"), QStringLiteral("slideLeft")},
        {QStringLiteral("slide-right"), QStringLiteral("slideRight")}, {QStringLiteral("pop"), QStringLiteral("pop")},
        {QStringLiteral("blur-in"), QStringLiteral("blur")},     {QStringLiteral("typewriter"), QStringLiteral("typewriter")},
        {QStringLiteral("rise"), QStringLiteral("rise")},       {QStringLiteral("bounce"), QStringLiteral("bounce")},
        {QStringLiteral("wave"), QStringLiteral("wave")},
    };
    return kinds;
}

QVariantMap legacyTextAnimationMap(const drift::TextAnimationSlot &slot)
{
    const QVariantMap full = textAnimationSlotToMap(slot);
    QString kind = QStringLiteral("none");
    if (slot.isActive())
        kind = legacyKindForPreset().value(slot.presetId, slot.presetId.isEmpty() ? QStringLiteral("custom") : slot.presetId);
    return {
        {QStringLiteral("kind"), kind},
        {QStringLiteral("duration"), full.value(QStringLiteral("duration"), 0.4)},
        {QStringLiteral("ease"), full.value(QStringLiteral("ease"), QStringLiteral("easeOut"))},
        {QStringLiteral("unit"), full.value(QStringLiteral("unit"), QStringLiteral("block"))},
        {QStringLiteral("stagger"), full.value(QStringLiteral("stagger"), 0.06)},
        {QStringLiteral("order"), full.value(QStringLiteral("order"), QStringLiteral("forward"))},
    };
}

QVariantMap textLayerToMap(const drift::TextShadingLayer &layer)
{
    return drift::textShadingLayerToJson(layer).toVariantMap();
}

QJsonObject deepMergeJson(QJsonObject base, const QJsonObject &patch)
{
    for (auto it = patch.constBegin(); it != patch.constEnd(); ++it) {
        if (it->isObject() && base.value(it.key()).isObject())
            base.insert(it.key(), deepMergeJson(base.value(it.key()).toObject(), it->toObject()));
        else
            base.insert(it.key(), *it);
    }
    return base;
}

void applyTextLayerPatch(drift::TextShadingLayer *layer, const QVariantMap &patch)
{
    const QString id = layer->id;
    *layer = drift::textShadingLayerFromJson(
        deepMergeJson(drift::textShadingLayerToJson(*layer), QJsonObject::fromVariantMap(patch)));
    layer->id = id;
}

bool applyLayerStackPatch(QList<drift::TextShadingLayer> &stack, const QVariantMap &m,
                          const drift::TextShadingLayer &fallback)
{
    bool edited = false;
    if (m.contains(QStringLiteral("layers"))) {
        QList<drift::TextShadingLayer> layers;
        for (const QVariant &v : m.value(QStringLiteral("layers")).toList()) {
            drift::TextShadingLayer layer = drift::textShadingLayerFromJson(QJsonObject::fromVariantMap(v.toMap()));
            if (layer.id.isEmpty() || drift::findTextLayer(layers, layer.id))
                layer.id = drift::mintTextLayerId(layers);
            layers.append(layer);
        }
        if (layers.isEmpty())
            layers = {fallback};
        stack = layers;
        edited = true;
    }
    if (m.contains(QStringLiteral("layer"))) {
        const QVariantMap patch = m.value(QStringLiteral("layer")).toMap();
        drift::TextShadingLayer *layer = nullptr;
        if (patch.contains(QStringLiteral("id")))
            layer = drift::findTextLayer(stack, patch.value(QStringLiteral("id")).toString().toLower());
        else if (patch.contains(QStringLiteral("index"))) {
            const int index = patch.value(QStringLiteral("index")).toInt();
            if (index >= 0 && index < stack.size())
                layer = &stack[index];
        }
        if (layer) {
            QVariantMap fields = patch;
            fields.remove(QStringLiteral("id"));
            fields.remove(QStringLiteral("index"));
            applyTextLayerPatch(layer, fields);
            edited = true;
        }
    }
    return edited;
}

int naturalLayerIndex(const QList<drift::TextShadingLayer> &layers, drift::TextLayerKind kind)
{
    const auto rank = [](drift::TextLayerKind k) {
        switch (k) {
        case drift::TextLayerKind::Extrude:
            return 0;
        case drift::TextLayerKind::Shadow:
            return 1;
        case drift::TextLayerKind::Glow:
            return 2;
        case drift::TextLayerKind::Stroke:
            return 3;
        case drift::TextLayerKind::Fill:
            return 4;
        }
        return 4;
    };
    int index = 0;
    for (int i = 0; i < layers.size(); ++i) {
        if (rank(layers.at(i).kind) <= rank(kind))
            index = i + 1;
    }
    return index;
}

QVariantMap textHighlightToMap(const drift::TextHighlight &h)
{
    return {
        {QStringLiteral("enabled"), h.enabled},
        {QStringLiteral("color"), h.color.name(QColor::HexArgb)},
        {QStringLiteral("padding"), h.padding},
        {QStringLiteral("radius"), h.radius},
    };
}

void applyTextHighlightPatch(drift::TextHighlight *highlight, const QVariantMap &m)
{
    if (m.contains(QStringLiteral("enabled")))
        highlight->enabled = m.value(QStringLiteral("enabled")).toBool();
    if (m.contains(QStringLiteral("color")))
        highlight->color = QColor(m.value(QStringLiteral("color")).toString());
    if (m.contains(QStringLiteral("padding")))
        highlight->padding = qMax(0.0, m.value(QStringLiteral("padding")).toDouble());
    if (m.contains(QStringLiteral("radius")))
        highlight->radius = qMax(0.0, m.value(QStringLiteral("radius")).toDouble());
}

QVariantMap wordAccentToMap(const drift::WordAccent &a)
{
    return {
        {QStringLiteral("rule"), drift::wordAccentRuleToString(a.rule)},
        {QStringLiteral("n"), a.n},
        {QStringLiteral("phase"), a.phase},
        {QStringLiteral("colorEnabled"), a.colorEnabled},
        {QStringLiteral("color"), a.color.name(QColor::HexArgb)},
        {QStringLiteral("sizeScale"), a.sizeScale},
        {QStringLiteral("outlineEnabled"), a.outlineEnabled},
        {QStringLiteral("outlineWidth"), a.outlineWidth},
        {QStringLiteral("outlineColor"), a.outlineColor.name(QColor::HexArgb)},
        {QStringLiteral("highlight"), textHighlightToMap(a.highlight)},
    };
}

QVariantMap textStyleToMap(const drift::TextStyle &s, drift::TimeUs timelineStart)
{
    QVariantList layers;
    for (const drift::TextShadingLayer &layer : s.layers)
        layers.append(textLayerToMap(layer));
    QVariantMap map{
        {QStringLiteral("packId"), s.packId},
        {QStringLiteral("fontFamily"), s.fontFamily},
        {QStringLiteral("pixelSize"), s.pixelSize},
        {QStringLiteral("fontWeight"), s.fontWeight},
        {QStringLiteral("italic"), s.italic},
        {QStringLiteral("layers"), layers},
        {QStringLiteral("lookId"), s.lookId},
        {QStringLiteral("lookParams"), paramsToMap(s.lookParams)},
        {QStringLiteral("pathBend"), s.pathBend},
        {QStringLiteral("align"), drift::textAlignToString(s.align)},
        {QStringLiteral("valign"), drift::textVAlignToString(s.valign)},
        {QStringLiteral("wordWrap"), s.wordWrap},
        {QStringLiteral("lineHeight"), s.lineHeight},
        {QStringLiteral("letterSpacing"), s.letterSpacing},
        {QStringLiteral("textCase"), drift::textCaseToString(s.textCase)},
        {QStringLiteral("boxEnabled"), s.boxEnabled},
        {QStringLiteral("boxColor"), s.boxColor.name(QColor::HexArgb)},
        {QStringLiteral("boxPadding"), s.boxPadding},
        {QStringLiteral("boxRadius"), s.boxRadius},
        {QStringLiteral("wordHighlight"), textHighlightToMap(s.wordHighlight)},
        {QStringLiteral("underlineEnabled"), s.underlineEnabled},
        {QStringLiteral("underlineColor"), s.underlineColor.name(QColor::HexArgb)},
        {QStringLiteral("underlineWidth"), s.underlineWidth},
        {QStringLiteral("underlineOffset"), s.underlineOffset},
        {QStringLiteral("strikethroughEnabled"), s.strikethroughEnabled},
        {QStringLiteral("accent"), wordAccentToMap(s.accent)},
        {QStringLiteral("animation"), textAnimationSetToMap(s.animation)},
    };

    // Read-only mirrors of the flat v6 look, kept one release for the inspector rows and agents
    // that still read them.
    map.insert(QStringLiteral("color"), s.primaryColor().name(QColor::HexArgb));
    const drift::TextShadingLayer *fill = drift::firstTextLayerOfKind(s.layers, drift::TextLayerKind::Fill, true);
    if (!fill)
        fill = drift::firstTextLayerOfKind(s.layers, drift::TextLayerKind::Fill, false);
    QString fillKind = QStringLiteral("solid");
    QColor secondary(255, 120, 0);
    double gradientAngle = 90.0;
    if (fill && fill->paint.kind == drift::TextPaintKind::Gradient) {
        fillKind = fill->paint.gradient.kind == drift::TextGradientKind::Radial ? QStringLiteral("radialGradient")
                                                                               : QStringLiteral("linearGradient");
        if (!fill->paint.gradient.stops.isEmpty())
            secondary = fill->paint.gradient.stops.last().color;
        gradientAngle = fill->paint.gradient.angle;
    }
    map.insert(QStringLiteral("fillKind"), fillKind);
    map.insert(QStringLiteral("colorSecondary"), secondary.name(QColor::HexArgb));
    map.insert(QStringLiteral("gradientAngle"), gradientAngle);
    const drift::TextShadingLayer *stroke = drift::firstTextLayerOfKind(s.layers, drift::TextLayerKind::Stroke, false);
    map.insert(QStringLiteral("outlineEnabled"), stroke && stroke->enabled);
    map.insert(QStringLiteral("outlineWidth"), stroke ? stroke->width : 2.0);
    map.insert(QStringLiteral("outlineColor"), (stroke ? stroke->paint.color : QColor(Qt::black)).name(QColor::HexArgb));
    const drift::TextShadingLayer *shadow = drift::firstTextLayerOfKind(s.layers, drift::TextLayerKind::Shadow, false);
    map.insert(QStringLiteral("shadowEnabled"), shadow && shadow->enabled);
    map.insert(QStringLiteral("shadowOffsetX"), shadow ? shadow->offsetX : 0.0);
    map.insert(QStringLiteral("shadowOffsetY"), shadow ? shadow->offsetY : 4.0);
    map.insert(QStringLiteral("shadowBlur"), shadow ? shadow->blur : 8.0);
    map.insert(QStringLiteral("shadowOpacity"), shadow ? shadow->opacity : 0.6);
    map.insert(QStringLiteral("shadowColor"), (shadow ? shadow->paint.color : QColor(Qt::black)).name(QColor::HexArgb));
    const drift::TextShadingLayer *glow = drift::firstTextLayerOfKind(s.layers, drift::TextLayerKind::Glow, false);
    map.insert(QStringLiteral("glowEnabled"), glow && glow->enabled);
    map.insert(QStringLiteral("glowColor"), (glow ? glow->paint.color : QColor(Qt::white)).name(QColor::HexArgb));
    map.insert(QStringLiteral("glowRadius"), glow ? glow->blur : 18.0);
    map.insert(QStringLiteral("glowOpacity"), glow ? glow->opacity : 0.8);
    map.insert(QStringLiteral("animIn"), legacyTextAnimationMap(s.animation.in));
    map.insert(QStringLiteral("animOut"), legacyTextAnimationMap(s.animation.out));

    QVariantMap keyframes;
    for (auto it = s.keyframes.constBegin(); it != s.keyframes.constEnd(); ++it) {
        if (!it->isEmpty())
            keyframes.insert(it.key(), keyframeTrackToMap(it.value(), timelineStart));
    }
    if (!keyframes.isEmpty())
        map.insert(QStringLiteral("keyframes"), keyframes);
    return map;
}

QVariant vectorSlotValueToVariant(const drift::VectorSlotValue &v)
{
    switch (v.type) {
    case drift::VectorSlotValue::Type::Color:
        return v.color.name(QColor::HexArgb);
    case drift::VectorSlotValue::Type::Scalar:
        return v.scalar;
    case drift::VectorSlotValue::Type::Vec2:
        return QVariantList{v.vec2.x(), v.vec2.y()};
    case drift::VectorSlotValue::Type::Text:
        return v.text;
    case drift::VectorSlotValue::Type::Image:
        return v.image;
    }
    return {};
}

QVariantMap pdfSourceToMap(const drift::PdfSource &s, drift::TimeUs timelineStart)
{
    QVariantMap keyframes;
    for (auto it = s.keyframes.cbegin(); it != s.keyframes.cend(); ++it) {
        if (!it->isEmpty())
            keyframes.insert(it.key(), keyframeTrackToMap(it.value(), timelineStart));
    }
    QVariantMap map{
        {QStringLiteral("path"), s.path},
        {QStringLiteral("pageCount"), s.pageCount},
        {QStringLiteral("firstPage"), s.firstPage},
        {QStringLiteral("lastPage"), s.lastPage},
        {QStringLiteral("layout"), drift::pdfLayoutToString(s.layout)},
        {QStringLiteral("gridColumns"), s.gridColumns},
        {QStringLiteral("gap"), s.gap},
        {QStringLiteral("addonAvailable"), drift::pdfium::available()},
    };
    for (const QString &key : drift::pdfKeyframeProperties()) {
        double v = 0.0;
        drift::pdfScalar(s, key, &v);
        map.insert(key, v);
    }
    if (!keyframes.isEmpty())
        map.insert(QStringLiteral("keyframes"), keyframes);
    return map;
}

bool isSplatSourcePath(const QString &path)
{
    return QFileInfo(path).suffix().compare(QLatin1String("glb"), Qt::CaseInsensitive) != 0;
}

QVariantMap model3dSourceToMap(const drift::Model3dSource &m, drift::TimeUs timelineStart)
{
    QVariantList animations;
    for (const drift::Model3dAnimationRef &a : m.animations) {
        animations.append(QVariantMap{
            {QStringLiteral("name"), a.name},
            {QStringLiteral("durationSec"), drift::usToSeconds(a.durationUs)},
        });
    }
    QVariantMap keyframes;
    for (auto it = m.keyframes.cbegin(); it != m.keyframes.cend(); ++it) {
        if (!it->isEmpty())
            keyframes.insert(it.key(), keyframeTrackToMap(it.value(), timelineStart));
    }
    QVariantMap map{
        {QStringLiteral("path"), m.path},
        {QStringLiteral("animations"), animations},
        {QStringLiteral("animation"), m.animation},
        {QStringLiteral("loop"), drift::vectorLoopToString(m.loop)},
        {QStringLiteral("offset"), drift::usToSeconds(m.startOffsetUs)},
        {QStringLiteral("isSplat"), isSplatSourcePath(m.path)},
    };
    for (const QString &key : drift::model3dKeyframeProperties()) {
        double v = 0.0;
        drift::model3dScalar(m, key, &v);
        map.insert(key, v);
    }
    for (int n = 0; n < drift::kModelLightCount; ++n) {
        const QString prefix = QStringLiteral("light%1_").arg(n + 1);
        map.insert(prefix + QLatin1String("enabled"), m.lights[size_t(n)].enabled);
        map.insert(prefix + QLatin1String("color"), m.lights[size_t(n)].color.name());
    }
    if (!keyframes.isEmpty())
        map.insert(QStringLiteral("keyframes"), keyframes);
    const QString warning = drift::modelAssetWarning(m.path);
    if (!warning.isEmpty())
        map.insert(QStringLiteral("warning"), warning);
    return map;
}

QList<QPair<QString, double>> legacyModel3dPoseWrites(const drift::Clip &clip, const QString &key,
                                                      double value, drift::TimeUs relative,
                                                      double projectHeight)
{
    if (clip.type != drift::ClipType::Model3d)
        return {};
    // Old rotations were about glTF's y-up, z-toward-the-viewer axes; the clip's are about the world's
    // y-down, z-away ones, a half turn about X from glTF's: X is shared, Y and Z turn the other way.
    if (key == QLatin1String("rotX"))
        return {{QStringLiteral("rotationX"), value}};
    if (key == QLatin1String("rotY"))
        return {{QStringLiteral("rotationY"), -value}};
    if (key == QLatin1String("rotZ"))
        return {{QStringLiteral("rotation"), -value}};
    if (key == QLatin1String("depth"))
        return {{QStringLiteral("perspective"), projectHeight / (2.0 * qBound(0.01, value, 1.0))}};
    if (key == QLatin1String("scale")) {
        const double w = clipTransformValue(clip.transformW, relative, projectHeight);
        const double h = clipTransformValue(clip.transformH, relative, projectHeight);
        const double cx = clipTransformValue(clip.transformX, relative, 0.0) + w / 2.0;
        const double cy = clipTransformValue(clip.transformY, relative, 0.0) + h / 2.0;
        const QSizeF face = drift::modelClipFaceSize(clip.model3d.aabbMin, clip.model3d.aabbMax,
                                                     qMax(0.01, value) * projectHeight);
        return {{QStringLiteral("x"), cx - face.width() / 2.0},
                {QStringLiteral("y"), cy - face.height() / 2.0},
                {QStringLiteral("width"), face.width()},
                {QStringLiteral("height"), face.height()}};
    }
    return {};
}

bool isLegacyModel3dPoseKey(const QString &key)
{
    return key == QLatin1String("scale") || key == QLatin1String("depth")
           || key == QLatin1String("rotX") || key == QLatin1String("rotY")
           || key == QLatin1String("rotZ");
}

drift::Mask maskFromMap(const QVariantMap &m)
{
    drift::Mask mask;
    mask.shape = drift::maskShapeFromString(m.value(QStringLiteral("shape")).toString());
    mask.x = m.value(QStringLiteral("x"), mask.x).toDouble();
    mask.y = m.value(QStringLiteral("y"), mask.y).toDouble();
    mask.w = m.value(QStringLiteral("w"), mask.w).toDouble();
    mask.h = m.value(QStringLiteral("h"), mask.h).toDouble();
    mask.rotation = m.value(QStringLiteral("rotation"), mask.rotation).toDouble();
    mask.feather = m.value(QStringLiteral("feather"), mask.feather).toDouble();
    mask.roundness = m.value(QStringLiteral("roundness"), mask.roundness).toDouble();
    mask.invert = m.value(QStringLiteral("invert"), mask.invert).toBool();
    mask.op = drift::maskOpFromString(m.value(QStringLiteral("op")).toString());
    mask.enabled = m.value(QStringLiteral("enabled"), mask.enabled).toBool();
    mask.name = m.value(QStringLiteral("name"), mask.name).toString();
    mask.mediaPath = m.value(QStringLiteral("mediaPath"), mask.mediaPath).toString();
    mask.mediaFgrPath = m.value(QStringLiteral("mediaFgrPath"), mask.mediaFgrPath).toString();
    mask.mediaSrcOffsetUs =
        drift::TimeUs(m.value(QStringLiteral("mediaSrcOffsetUs"), qint64(0)).toLongLong());
    mask.mediaFit = drift::maskMediaFitFromString(m.value(QStringLiteral("mediaFit")).toString());
    mask.mediaChannel =
        drift::maskMediaChannelFromString(m.value(QStringLiteral("mediaChannel")).toString());
    mask.mediaLoop = m.value(QStringLiteral("mediaLoop"), mask.mediaLoop).toBool();
    if (m.contains(QStringLiteral("mediaTimeBase")))
        mask.mediaTimeBase =
            drift::maskMediaTimeBaseFromString(m.value(QStringLiteral("mediaTimeBase")).toString());
    const QVariantList points = m.value(QStringLiteral("points")).toList();
    for (const QVariant &value : points) {
        // The [x, y] pair form is what this map used to emit, so anything already sending it
        // over MCP keeps working.
        if (value.canConvert<QVariantMap>() && !value.canConvert<QVariantList>()) {
            const QVariantMap point = value.toMap();
            mask.points.append(QPointF(point.value(QStringLiteral("x")).toDouble(),
                                       point.value(QStringLiteral("y")).toDouble()));
            continue;
        }
        const QVariantList pair = value.toList();
        if (pair.size() >= 2)
            mask.points.append(QPointF(pair.at(0).toDouble(), pair.at(1).toDouble()));
    }
    return mask;
}

QVariantMap transitionToMap(const drift::Project &project, const drift::Track &track, const drift::Transition &t)
{
    drift::TimeUs startUs = 0;
    drift::TimeUs endUs = 0;
    const bool hasWindow = drift::transitionWindow(track, t, startUs, endUs);
    const drift::Clip *fromClip = drift::clipById(track, t.fromClipId);
    const drift::Clip *toClip = drift::clipById(track, t.toClipId);
    const bool overlapping = fromClip && toClip && drift::clipsPhysicallyOverlap(*fromClip, *toClip);
    const drift::TimeUs durationUs = hasWindow ? (endUs - startUs) : t.durationUs;

    const TransitionPresetEntry *def = transitionDefForId(t.kindId);

    // Whether each side has media past the cut: without it, that side dips instead of crossfading.
    QVariantMap handles;
    if (fromClip && toClip && !overlapping && hasWindow) {
        handles.insert(QStringLiteral("out"),
                       drift::clipHandleAfterUs(*fromClip, drift::sourceDurationForClip(project, *fromClip))
                           >= endUs - fromClip->timelineEnd());
        handles.insert(QStringLiteral("in"),
                       drift::clipHandleBeforeUs(*toClip, drift::sourceDurationForClip(project, *toClip))
                           >= toClip->timelineStart - startUs);
    }

    // Current value per parameter, so the properties panel can build its sliders.
    QVariantList params;
    if (def) {
        const QMap<QString, QVariant> resolved = resolvedTransitionParameters(t, *def);
        for (const drift::EffectParamSpec &p : def->meta.parameters) {
            params.append(QVariantMap{
                {QStringLiteral("key"), p.key},
                {QStringLiteral("label"), p.label},
                {QStringLiteral("min"), p.min},
                {QStringLiteral("max"), p.max},
                {QStringLiteral("default"), p.defaultValue},
                {QStringLiteral("isBoolean"), p.isBoolean()},
                {QStringLiteral("type"), p.typeName()},
                {QStringLiteral("value"), resolved.value(p.key, p.defaultValue)},
            });
        }
    }

    return {
        {QStringLiteral("id"), t.id},
        {QStringLiteral("fromClipId"), t.fromClipId},
        {QStringLiteral("toClipId"), t.toClipId},
        {QStringLiteral("kind"), t.kindId},
        {QStringLiteral("duration"), drift::usToSeconds(durationUs)},
        {QStringLiteral("start"), hasWindow ? drift::usToSeconds(startUs) : 0.0},
        {QStringLiteral("end"), hasWindow ? drift::usToSeconds(endUs) : 0.0},
        {QStringLiteral("overlapping"), overlapping},
        {QStringLiteral("label"), def ? def->meta.displayName : t.kindId},
        {QStringLiteral("params"), params},
        {QStringLiteral("easingCurve"), drift::fadeCurveToString(t.easingCurve)},
        {QStringLiteral("easingHandles"), handlesToVariant(t.easingShape)},
        {QStringLiteral("audioOnly"), track.type == drift::TrackType::Audio},
        {QStringLiteral("audioCurve"), def && !def->audioCurve.isEmpty() ? def->audioCurve : QStringLiteral("crossfade")},
        {QStringLiteral("handles"), handles},
    };
}

bool isSyntheticTimelineClip(drift::ClipType type)
{
    return type == drift::ClipType::Text || type == drift::ClipType::Subtitle
           || type == drift::ClipType::Shape || type == drift::ClipType::Image
           || type == drift::ClipType::Vector || type == drift::ClipType::Model3d
           || type == drift::ClipType::Pdf || type == drift::ClipType::Procedural
           || type == drift::ClipType::Adjustment;
}

drift::TimeUs syntheticClipMaxDurationUs()
{
    return drift::secondsToUs(86400.0);
}

void syncSyntheticSourceRange(drift::Clip &clip)
{
    // A mask adjustment's srcIn is where its matte media starts playing, so a head-trim or split
    // moves it; everything else synthetic always starts at 0.
    const bool keepsSrcIn = clip.type == drift::ClipType::Adjustment
                            && clip.adjustmentKind == drift::AdjustmentKind::Mask;
    clip.srcIn = keepsSrcIn ? qMax<drift::TimeUs>(0, clip.srcIn) : 0;
    clip.srcOut = clip.srcIn + qMin(clip.sourceSpanUs(), syntheticClipMaxDurationUs());
}

bool parseIndexedProp(const QString &prop, QLatin1String prefix, int *effectIndex, QString *paramKey)
{
    if (!prop.startsWith(prefix))
        return false;
    const int start = int(prefix.size());
    const int dot = prop.indexOf(QLatin1Char('.'), start);
    if (dot < 0 || dot == start || dot + 1 >= prop.size())
        return false;
    bool ok = false;
    const int index = QStringView(prop).mid(start, dot - start).toInt(&ok);
    if (!ok || index < 0)
        return false;
    *effectIndex = index;
    *paramKey = prop.mid(dot + 1);
    return true;
}

bool parseEffectProp(const QString &prop, int *effectIndex, QString *paramKey)
{
    return parseIndexedProp(prop, QLatin1String("fx."), effectIndex, paramKey);
}

bool parseAudioEffectProp(const QString &prop, int *effectIndex, QString *paramKey)
{
    return parseIndexedProp(prop, QLatin1String("afx."), effectIndex, paramKey);
}

bool parseMaskProp(const QString &prop, QString *key)
{
    if (!prop.startsWith(QLatin1String("mask.")))
        return false;
    const QString suffix = prop.mid(5);
    if (!drift::maskKeyframeProperties().contains(suffix))
        return false;
    *key = suffix;
    return true;
}

bool parseTextProp(const drift::TextStyle &style, const QString &prop, QString *key)
{
    if (!prop.startsWith(QLatin1String("text.")))
        return false;
    const QString canonical = drift::textKeyframeCanonicalKey(prop.mid(5), style);
    if (canonical.isEmpty())
        return false;
    *key = canonical;
    return true;
}

bool parseShapeProp(const drift::Clip &clip, const QString &prop, QString *key)
{
    if (clip.type != drift::ClipType::Shape || !prop.startsWith(QLatin1String("shape.")))
        return false;
    const QString canonical = drift::shapeKeyframeCanonicalKey(prop.mid(6), clip.shapeStyle);
    if (canonical.isEmpty())
        return false;
    *key = canonical;
    return true;
}

bool parseVectorProp(const drift::Clip &clip, const QString &prop, QString *key)
{
    if (clip.type != drift::ClipType::Vector || !prop.startsWith(QLatin1String("vector.svg.")))
        return false;
    const QString candidate = prop.mid(7);
    double probe = 0.0;
    if (!drift::vectorSlotScalar(clip.vector, candidate, &probe))
        return false;
    *key = candidate;
    return true;
}

bool parseModel3dProp(const drift::Clip &clip, const QString &prop, QString *key)
{
    if (clip.type != drift::ClipType::Model3d || !prop.startsWith(QLatin1String("model3d.")))
        return false;
    const QString candidate = prop.mid(8);
    double probe = 0.0;
    if (!drift::model3dScalar(clip.model3d, candidate, &probe))
        return false;
    *key = candidate;
    return true;
}

bool parsePdfProp(const drift::Clip &clip, const QString &prop, QString *key)
{
    if (clip.type != drift::ClipType::Pdf || !prop.startsWith(QLatin1String("pdf.")))
        return false;
    const QString candidate = prop.mid(4);
    double probe = 0.0;
    if (!drift::pdfScalar(clip.pdf, candidate, &probe))
        return false;
    *key = candidate;
    return true;
}

bool parseProceduralProp(const drift::Clip &clip, const QString &prop, QString *key)
{
    if (clip.type != drift::ClipType::Procedural || !prop.startsWith(QLatin1String("proc.")))
        return false;
    const QString candidate = prop.mid(5);
    const drift::ProceduralPackage package = proceduralForId(clip.procedural.packageId);
    for (const drift::EffectParamSpec &spec : package.parameters) {
        if (spec.key == candidate) {
            if (spec.isText() || spec.isVec2() || spec.isRegion())
                return false;
            *key = candidate;
            return true;
        }
        if (!candidate.startsWith(spec.key + QLatin1Char('.')))
            continue;
        const QString channel = candidate.mid(spec.key.size() + 1);
        const bool valid = (spec.isColor() && QStringLiteral("rgb").contains(channel) && channel.size() == 1)
            || (spec.isColor() && spec.alpha && channel == QLatin1String("a"))
            || (spec.isVec2() && (channel == QLatin1String("x") || channel == QLatin1String("y")))
            || (spec.isRegion() && QStringList{QStringLiteral("x"), QStringLiteral("y"), QStringLiteral("w"),
                                                QStringLiteral("h")}.contains(channel));
        if (!valid)
            return false;
        *key = candidate;
        return true;
    }
    return false;
}

void clearClipPose3d(drift::Clip &clip)
{
    clip.rotationX = {};
    clip.rotationY = {};
    clip.positionZ = {};
    clip.perspective = {};
    clip.anchorX = {};
    clip.anchorY = {};
    clip.anchorZ = {};
    clip.layer3d = false;
    // Depth sorting only means anything with a pose, so switching 3D off takes it with it.
    clip.depthOcclude = false;
}

drift::KeyframeTrack<double> *transformTrackForProp(drift::Clip &clip, const QString &prop)
{
    if (prop == QStringLiteral("opacity"))
        return &clip.opacity;
    if (prop == QStringLiteral("x") || prop == QStringLiteral("posX"))
        return &clip.transformX;
    if (prop == QStringLiteral("y") || prop == QStringLiteral("posY"))
        return &clip.transformY;
    if (prop == QStringLiteral("width") || prop == QStringLiteral("scale"))
        return &clip.transformW;
    if (prop == QStringLiteral("height"))
        return &clip.transformH;
    if (prop == QStringLiteral("rotation"))
        return &clip.rotation;
    if (prop == QStringLiteral("rotationX"))
        return &clip.rotationX;
    if (prop == QStringLiteral("rotationY"))
        return &clip.rotationY;
    if (prop == QStringLiteral("z"))
        return &clip.positionZ;
    if (prop == QStringLiteral("perspective"))
        return &clip.perspective;
    if (prop == QStringLiteral("anchorX"))
        return &clip.anchorX;
    if (prop == QStringLiteral("anchorY"))
        return &clip.anchorY;
    if (prop == QStringLiteral("anchorZ"))
        return &clip.anchorZ;
    if (prop == QStringLiteral("volume"))
        return &clip.volume;
    return nullptr;
}

drift::KeyframeTrack<double> *keyframeTrackForProp(drift::Clip &clip, const QString &prop,
                                                   bool createIfMissing)
{
    QString maskKey;
    if (parseMaskProp(prop, &maskKey)) {
        if (createIfMissing)
            return &clip.mask.keyframes[maskKey];
        const auto it = clip.mask.keyframes.find(maskKey);
        return it == clip.mask.keyframes.end() ? nullptr : &it.value();
    }
    QString textKey;
    if (parseTextProp(clip.textStyle, prop, &textKey)) {
        if (createIfMissing)
            return &clip.textStyle.keyframes[textKey];
        const auto it = clip.textStyle.keyframes.find(textKey);
        return it == clip.textStyle.keyframes.end() ? nullptr : &it.value();
    }
    QString shapeKey;
    if (parseShapeProp(clip, prop, &shapeKey)) {
        if (createIfMissing)
            return &clip.shapeStyle.keyframes[shapeKey];
        const auto it = clip.shapeStyle.keyframes.find(shapeKey);
        return it == clip.shapeStyle.keyframes.end() ? nullptr : &it.value();
    }
    QString vectorKey;
    if (parseVectorProp(clip, prop, &vectorKey)) {
        if (createIfMissing)
            return &clip.vector.keyframes[vectorKey];
        const auto it = clip.vector.keyframes.find(vectorKey);
        return it == clip.vector.keyframes.end() ? nullptr : &it.value();
    }
    QString modelKey;
    if (parseModel3dProp(clip, prop, &modelKey)) {
        if (createIfMissing)
            return &clip.model3d.keyframes[modelKey];
        const auto it = clip.model3d.keyframes.find(modelKey);
        return it == clip.model3d.keyframes.end() ? nullptr : &it.value();
    }
    QString procKey;
    if (parseProceduralProp(clip, prop, &procKey)) {
        if (createIfMissing)
            return &clip.procedural.paramKeyframes[procKey];
        const auto it = clip.procedural.paramKeyframes.find(procKey);
        return it == clip.procedural.paramKeyframes.end() ? nullptr : &it.value();
    }
    QString pdfKey;
    if (parsePdfProp(clip, prop, &pdfKey)) {
        if (createIfMissing)
            return &clip.pdf.keyframes[pdfKey];
        const auto it = clip.pdf.keyframes.find(pdfKey);
        return it == clip.pdf.keyframes.end() ? nullptr : &it.value();
    }

    int effectIndex = -1;
    QString paramKey;
    // Audio params are all scalars (floats and switches), so any declared one can carry a track.
    if (parseAudioEffectProp(prop, &effectIndex, &paramKey)) {
        if (effectIndex >= clip.audioEffects.size())
            return nullptr;
        drift::Effect &effect = clip.audioEffects[effectIndex];
        if (createIfMissing)
            return &effect.paramKeyframes[paramKey];
        const auto it = effect.paramKeyframes.find(paramKey);
        return it == effect.paramKeyframes.end() ? nullptr : &it.value();
    }
    if (!parseEffectProp(prop, &effectIndex, &paramKey))
        return transformTrackForProp(clip, prop);

    if (effectIndex >= clip.effects.size())
        return nullptr;

    drift::Effect &effect = clip.effects[effectIndex];

    // File and clip params are not animatable, and a colour or vec2 only through its channel
    // tracks ("<key>.r", "<key>.x"): the track type is double all the way down.
    if (const EffectPresetEntry *def = effectDefForId(effect.catalogId)) {
        for (const drift::EffectParamSpec &spec : def->meta.parameters) {
            if (spec.key == paramKey && spec.isText())
                return nullptr;
        }
    }

    if (createIfMissing)
        return &effect.paramKeyframes[paramKey];
    const auto it = effect.paramKeyframes.find(paramKey);
    return it == effect.paramKeyframes.end() ? nullptr : &it.value();
}

const drift::KeyframeTrack<double> *keyframeTrackForProp(const drift::Clip &clip, const QString &prop)
{
    return keyframeTrackForProp(const_cast<drift::Clip &>(clip), prop, /*createIfMissing=*/false);
}

bool writeMaskScalar(drift::Mask &mask, const QString &key, double value)
{
    if (key == QStringLiteral("x"))
        mask.x = value;
    else if (key == QStringLiteral("y"))
        mask.y = value;
    else if (key == QStringLiteral("w"))
        mask.w = value;
    else if (key == QStringLiteral("h"))
        mask.h = value;
    else if (key == QStringLiteral("rotation"))
        mask.rotation = value;
    else if (key == QStringLiteral("feather"))
        mask.feather = value;
    else if (key == QStringLiteral("roundness"))
        mask.roundness = value;
    else
        return false;
    return true;
}

namespace {
// "<colour>.r/.g/.b/.a" is a keyframe track only; the static value is the hex under the colour's
// own key, so a channel write updates that hex instead of adding a "<colour>.r" entry.
void storeParamValue(QMap<QString, QVariant> &parameters, const QString &key, double value)
{
    const bool colorChannel = key.size() > 2 && key.at(key.size() - 2) == QLatin1Char('.')
                              && QStringLiteral("rgba").contains(key.back());
    const QString base = colorChannel ? key.chopped(2) : QString();
    const QString hex = colorChannel ? parameters.value(base).toString() : QString();
    if (!colorChannel || !hex.startsWith(QLatin1Char('#'))) {
        parameters.insert(key, value);
        return;
    }
    QColor color(hex.left(7));
    const int v = qBound(0, int(std::lround(value * 255.0)), 255);
    const QChar channel = key.back();
    if (channel == QLatin1Char('r'))
        color.setRed(v);
    else if (channel == QLatin1Char('g'))
        color.setGreen(v);
    else if (channel == QLatin1Char('b'))
        color.setBlue(v);
    QString stored = color.name(QColor::HexRgb);
    if (channel == QLatin1Char('a'))
        stored += QStringLiteral("%1").arg(v, 2, 16, QLatin1Char('0'));
    else if (hex.size() == 9)
        stored += hex.right(2);
    parameters.insert(base, stored);
}
} // namespace

bool writeClipPropValue(drift::Clip &clip, const QString &prop, drift::TimeUs relative, double value,
                        bool autoKey, bool force)
{
    // A mask scalar mirrors an effect param: the static member holds the last value so clearing
    // the track leaves the mask where the user put it, rather than snapping to the struct default.
    QString maskKey;
    if (parseMaskProp(prop, &maskKey)) {
        const auto existing = clip.mask.keyframes.constFind(maskKey);
        const bool keyed = existing != clip.mask.keyframes.constEnd() && !existing->isEmpty();
        if (!keyed && !force && !autoKey)
            return writeMaskScalar(clip.mask, maskKey, value);
        if (!writeKeyframeValue(clip.mask.keyframes[maskKey], relative, value, autoKey, force))
            return false;
        writeMaskScalar(clip.mask, maskKey, value);
        return true;
    }

    // Same again for a text scalar: the style member is the static home.
    QString textKey;
    if (parseTextProp(clip.textStyle, prop, &textKey)) {
        const auto existing = clip.textStyle.keyframes.constFind(textKey);
        const bool keyed = existing != clip.textStyle.keyframes.constEnd() && !existing->isEmpty();
        if (!keyed && !force && !autoKey)
            return drift::setTextStyleScalar(clip.textStyle, textKey, value);
        if (!writeKeyframeValue(clip.textStyle.keyframes[textKey], relative, value, autoKey, force))
            return false;
        drift::setTextStyleScalar(clip.textStyle, textKey, value);
        return true;
    }

    QString shapeKey;
    if (parseShapeProp(clip, prop, &shapeKey)) {
        const auto existing = clip.shapeStyle.keyframes.constFind(shapeKey);
        const bool keyed = existing != clip.shapeStyle.keyframes.constEnd() && !existing->isEmpty();
        if (!keyed && !force && !autoKey)
            return drift::setShapeStyleScalar(clip.shapeStyle, shapeKey, value);
        if (!writeKeyframeValue(clip.shapeStyle.keyframes[shapeKey], relative, value, autoKey, force))
            return false;
        drift::setShapeStyleScalar(clip.shapeStyle, shapeKey, value);
        return true;
    }
    QString vectorKey;
    if (parseVectorProp(clip, prop, &vectorKey)) {
        const auto existing = clip.vector.keyframes.constFind(vectorKey);
        const bool keyed = existing != clip.vector.keyframes.constEnd() && !existing->isEmpty();
        if (!keyed && !force && !autoKey)
            return drift::setVectorSlotScalar(clip.vector, vectorKey, value);
        if (!writeKeyframeValue(clip.vector.keyframes[vectorKey], relative, value, autoKey, force))
            return false;
        drift::setVectorSlotScalar(clip.vector, vectorKey, value);
        return true;
    }
    QString modelKey;
    if (parseModel3dProp(clip, prop, &modelKey)) {
        const auto existing = clip.model3d.keyframes.constFind(modelKey);
        const bool keyed = existing != clip.model3d.keyframes.constEnd() && !existing->isEmpty();
        if (!keyed && !force && !autoKey)
            return drift::setModel3dScalar(clip.model3d, modelKey, value);
        if (!writeKeyframeValue(clip.model3d.keyframes[modelKey], relative, value, autoKey, force))
            return false;
        drift::setModel3dScalar(clip.model3d, modelKey, value);
        return true;
    }
    QString procKey;
    if (parseProceduralProp(clip, prop, &procKey)) {
        const auto store = [&] { storeParamValue(clip.procedural.parameters, procKey, value); };
        const auto existing = clip.procedural.paramKeyframes.constFind(procKey);
        const bool keyed = existing != clip.procedural.paramKeyframes.constEnd() && !existing->isEmpty();
        if (!keyed && !force && !autoKey) {
            store();
            return true;
        }
        if (!writeKeyframeValue(clip.procedural.paramKeyframes[procKey], relative, value, autoKey, force))
            return false;
        store();
        return true;
    }
    QString pdfKey;
    if (parsePdfProp(clip, prop, &pdfKey)) {
        const auto existing = clip.pdf.keyframes.constFind(pdfKey);
        const bool keyed = existing != clip.pdf.keyframes.constEnd() && !existing->isEmpty();
        if (!keyed && !force && !autoKey)
            return drift::setPdfScalar(clip.pdf, pdfKey, value);
        if (!writeKeyframeValue(clip.pdf.keyframes[pdfKey], relative, value, autoKey, force))
            return false;
        drift::setPdfScalar(clip.pdf, pdfKey, value);
        return true;
    }

    int effectIndex = -1;
    QString paramKey;
    if (parseAudioEffectProp(prop, &effectIndex, &paramKey)) {
        if (effectIndex >= clip.audioEffects.size())
            return false;
        drift::Effect &effect = clip.audioEffects[effectIndex];
        const auto existing = effect.paramKeyframes.constFind(paramKey);
        const bool keyed = existing != effect.paramKeyframes.constEnd() && !existing->isEmpty();
        if ((keyed || force || autoKey)
            && !writeKeyframeValue(effect.paramKeyframes[paramKey], relative, value, autoKey, force))
            return false;
        effect.parameters.insert(paramKey, value);
        return true;
    }
    if (!parseEffectProp(prop, &effectIndex, &paramKey)) {
        drift::KeyframeTrack<double> *kt = transformTrackForProp(clip, prop);
        if (!kt || !writeKeyframeValue(*kt, relative, value, autoKey, force))
            return false;
        // A 3D value written to a flat clip would do nothing, so writing one makes it a 3D layer.
        if (kt == &clip.rotationX || kt == &clip.rotationY || kt == &clip.positionZ
            || kt == &clip.perspective || kt == &clip.anchorX || kt == &clip.anchorY
            || kt == &clip.anchorZ) {
            clip.layer3d = true;
        }
        return true;
    }

    if (effectIndex >= clip.effects.size())
        return false;

    drift::Effect &effect = clip.effects[effectIndex];
    const auto existing = effect.paramKeyframes.constFind(paramKey);
    const bool keyed = existing != effect.paramKeyframes.constEnd() && !existing->isEmpty();
    if (!keyed && !force && !autoKey) {
        storeParamValue(effect.parameters, paramKey, value);
        return true;
    }
    if (!writeKeyframeValue(effect.paramKeyframes[paramKey], relative, value, autoKey, force))
        return false;
    storeParamValue(effect.parameters, paramKey, value);
    return true;
}

drift::EasingSpec easingSpecFromVariant(const QVariantMap &spec)
{
    return drift::EasingSpec::fromJson(QJsonObject::fromVariantMap(spec));
}

QVariantList keyframeListToVariant(const drift::KeyframeTrack<double> &track, drift::TimeUs timelineStart)
{
    QVariantList out;
    for (auto it = track.keyframes().constBegin(); it != track.keyframes().constEnd(); ++it) {
        const drift::Keyframe<double> &key = it.value();
        // Handle dx reaches QML in seconds, matching `seconds`, so the curve editor can work
        // in one unit throughout instead of converting on every drag.
        out.append(QVariantMap{
            {QStringLiteral("seconds"), drift::usToSeconds(timelineStart + it.key())},
            {QStringLiteral("value"), key.value},
            {QStringLiteral("inDx"), drift::usToSeconds(static_cast<drift::TimeUs>(key.inDx))},
            {QStringLiteral("inDy"), key.inDy},
            {QStringLiteral("outDx"), drift::usToSeconds(static_cast<drift::TimeUs>(key.outDx))},
            {QStringLiteral("outDy"), key.outDy},
            {QStringLiteral("corner"), key.corner},
            {QStringLiteral("hold"), key.hold},
            {QStringLiteral("easing"), drift::interpolationToString(track.easingAt(it.key()))},
            {QStringLiteral("custom"), track.hasCustomTangents(it.key())},
            {QStringLiteral("fn"), drift::easingFnToString(key.fn)},
            {QStringLiteral("fnAmp"), key.fnAmplitude},
            {QStringLiteral("fnPeriod"), key.fnPeriod},
        });
    }
    return out;
}

QVariantMap keyframeTrackToMap(const drift::KeyframeTrack<double> &track, drift::TimeUs timelineStart)
{
    // `interpolation` is per-key now and travels inside each point; the map keeps its shape so
    // the inspector's bindings do not all have to change at once.
    return {
        {QStringLiteral("points"), keyframeListToVariant(track, timelineStart)},
    };
}

void fitClipLayoutToCanvas(drift::Clip &clip, int mediaW, int mediaH, int canvasW, int canvasH)
{
    canvasW = qMax(1, canvasW);
    canvasH = qMax(1, canvasH);
    if (mediaW <= 0 || mediaH <= 0) {
        setClipLayoutPixels(clip, 0, 0, canvasW, canvasH);
        return;
    }
    const double scale = qMin(static_cast<double>(canvasW) / mediaW, static_cast<double>(canvasH) / mediaH);
    const double w = mediaW * scale;
    const double h = mediaH * scale;
    setClipLayoutPixels(clip, (canvasW - w) / 2.0, (canvasH - h) / 2.0, w, h);
}

void placeModelClipLayout(drift::Clip &clip, int canvasW, int canvasH)
{
    const QSizeF face =
        drift::modelClipFaceSize(clip.model3d.aabbMin, clip.model3d.aabbMax, 0.5 * canvasH);
    setClipLayoutPixels(clip, (canvasW - face.width()) / 2.0, (canvasH - face.height()) / 2.0,
                        face.width(), face.height());
    clip.layer3d = true;
}

void probeModel3dSource(drift::Model3dSource &source)
{
    source.animations.clear();
    source.aabbMin = {};
    source.aabbMax = {};
    if (isSplatSourcePath(source.path)) {
        source.animation = 0;
        if (const auto splat = drift::model3d::loadSplatSceneCached(source.path)) {
            source.aabbMin = splat->fitMin;
            source.aabbMax = splat->fitMax;
        }
        return;
    }
    const auto asset = drift::loadModelAssetCached(source.path);
    if (!asset)
        return;
    for (const drift::ModelAnimationInfo &info : asset->animations)
        source.animations.append({info.name, info.durationUs});
    drift::modelClipBounds(*asset, &source.aabbMin, &source.aabbMax);
    source.animation = qBound(0, source.animation, qMax(0, source.animations.size() - 1));
}

bool assetHasTrim(const QVariantMap &asset)
{
    return asset.value(QStringLiteral("trimInSeconds"), 0.0).toDouble() > 0.0
           || asset.value(QStringLiteral("trimOutSeconds"), -1.0).toDouble() >= 0.0;
}

drift::TimeUs trimmedClipDurationUs(const QVariantMap &asset, drift::TimeUs fullDurationUs)
{
    if (!assetHasTrim(asset))
        return fullDurationUs;
    const drift::TimeUs trimIn = qBound<drift::TimeUs>(
        0, drift::secondsToUs(asset.value(QStringLiteral("trimInSeconds"), 0.0).toDouble()), fullDurationUs);
    const double trimOutSeconds = asset.value(QStringLiteral("trimOutSeconds"), -1.0).toDouble();
    const drift::TimeUs trimOut =
        trimOutSeconds < 0.0
            ? fullDurationUs
            : qBound<drift::TimeUs>(trimIn + 1, drift::secondsToUs(trimOutSeconds), fullDurationUs);
    return trimOut - trimIn;
}

void applyAssetLayout(drift::Clip &clip, const QVariantMap &asset, int canvasW, int canvasH)
{
    int mediaW = asset.value(QStringLiteral("width")).toInt();
    int mediaH = asset.value(QStringLiteral("height")).toInt();
    // "effectiveRotation" folds in any bin-preview override over the probed rotationDegrees
    // (AssetLibrary::assetAt). The clip carries the bin's correction as its own from here on, so
    // its orientation stays put even if the bin asset's override changes later.
    const int rotation = asset.value(QStringLiteral("effectiveRotation")).toInt();
    if (rotation == 90 || rotation == 270)
        std::swap(mediaW, mediaH);
    if (clip.type == drift::ClipType::Video || clip.type == drift::ClipType::Image) {
        clip.sourceFrame = asset.value(QStringLiteral("sourceFrame"), QRectF(0, 0, 1, 1)).toRectF();
        if (mediaW > 0 && mediaH > 0) {
            mediaW = qMax(1, qRound(mediaW * clip.sourceFrame.width()));
            mediaH = qMax(1, qRound(mediaH * clip.sourceFrame.height()));
        }
    }
    if (clip.type == drift::ClipType::Model3d)
        placeModelClipLayout(clip, canvasW, canvasH);
    else
        fitClipLayoutToCanvas(clip, mediaW, mediaH, canvasW, canvasH);
    clip.rotationCorrection = asset.value(QStringLiteral("rotationCorrection")).toInt();

    // A bin-preview trim is non-destructive (AssetLibrary::setAssetTrim never touches the file),
    // so a freshly placed clip has to start at that saved range itself rather than the caller's
    // default full-source srcIn/srcOut — the same idea as the rotation bake-in just above.
    if (assetHasTrim(asset)) {
        const drift::TimeUs durationUs =
            drift::secondsToUs(asset.value(QStringLiteral("durationSeconds")).toDouble());
        const drift::TimeUs trimIn = qBound<drift::TimeUs>(
            0, drift::secondsToUs(asset.value(QStringLiteral("trimInSeconds"), 0.0).toDouble()), durationUs);
        clip.timelineDuration = trimmedClipDurationUs(asset, durationUs);
        clip.srcIn = trimIn;
        clip.srcOut = trimIn + clip.timelineDuration;
    }
}

void applyDefaultVisualLayout(drift::Clip &clip, int canvasW, int canvasH, double shapeAspect)
{
    canvasW = qMax(1, canvasW);
    canvasH = qMax(1, canvasH);
    if (clip.type == drift::ClipType::Shape) {
        const double aspect = shapeAspect > 0.01 ? shapeAspect : 1.6;
        double h = canvasH * 0.30;
        double w = h * aspect;
        if (w > canvasW * 0.6) {
            w = canvasW * 0.6;
            h = w / aspect;
        }
        setClipLayoutPixels(clip, 0, 0, w, h);
        return;
    }
    if (clip.type == drift::ClipType::Text) {
        setClipLayoutPixels(clip, 0, canvasH * 0.35, canvasW, canvasH * 0.30);
        return;
    }
    if (clip.type == drift::ClipType::Subtitle) {
        setClipLayoutPixels(clip, 0, canvasH * 0.78, canvasW, canvasH * 0.18);
        return;
    }
    // Stickers / generic images without metadata: modest top-left box.
    const double side = qMin(canvasW, canvasH) * 0.25;
    setClipLayoutPixels(clip, 0, 0, side, side);
}

bool assetHasAudioStreams(const drift::Project &project, AssetLibrary *library, const QString &assetId)
{
    const drift::MediaAsset *asset = project.asset(assetId);
    if (!asset)
        return false;

    if (asset->hasAudioKnown)
        return asset->hasAudio;
    if (asset->channels > 0 || asset->sampleRate > 0)
        return true;
    if (library)
        library->ensureAudioPresence(assetId);
    return false;
}

QList<StreamInfo> cachedAudioStreams(const QString &path)
{
    if (path.isEmpty())
        return {};

    struct Entry {
        qint64 size = -1;
        qint64 mtimeMs = -1;
        QList<StreamInfo> streams;
    };
    static QHash<QString, Entry> cache;
    static QMutex mutex;
    // A project references a bounded set of media, but one-shot probes of files that never land
    // on a timeline would otherwise accumulate for the life of the process.
    constexpr int kMaxEntries = 256;

    const QFileInfo info(path);
    const qint64 size = info.size();
    const qint64 mtimeMs = info.lastModified().toMSecsSinceEpoch();

    {
        QMutexLocker locker(&mutex);
        const auto it = cache.constFind(path);
        if (it != cache.constEnd() && it->size == size && it->mtimeMs == mtimeMs)
            return it->streams;
    }

    const QList<StreamInfo> streams = MediaProbe::audioStreams(path);

    QMutexLocker locker(&mutex);
    if (cache.size() >= kMaxEntries)
        cache.clear();
    cache.insert(path, Entry{size, mtimeMs, streams});
    return streams;
}

bool clipHasEmbeddedAudio(const drift::Project &project, AssetLibrary *library, const drift::Clip &clip)
{
    // A composite plays its nested mix, which is there to separate even while it is silent.
    if (clip.type == drift::ClipType::Composite)
        return !clip.suppressEmbeddedAudio;
    if (clip.type != drift::ClipType::Video || clip.suppressEmbeddedAudio || clip.path.isEmpty())
        return false;
    if (!clip.assetId.isEmpty())
        return assetHasAudioStreams(project, library, clip.assetId);
    return !cachedAudioStreams(clip.path).isEmpty();
}

drift::Clip makeAudioCompanionFromVideo(const drift::Clip &videoClip, const QString &linkId,
                                        int audioStreamIndex, const QString &customName)
{
    drift::Clip audio;
    audio.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    audio.linkId = linkId;
    audio.sourceVideoClipId = videoClip.id;
    audio.assetId = videoClip.assetId;
    audio.type = drift::ClipType::Audio;
    audio.audioStreamIndex = audioStreamIndex;
    audio.name = customName.isEmpty() ? videoClip.name : customName;
    audio.path = videoClip.path;
    audio.sequenceId = videoClip.sequenceId;
    audio.timelineStart = videoClip.timelineStart;
    audio.timelineDuration = videoClip.timelineDuration;
    audio.srcIn = videoClip.srcIn;
    audio.srcOut = videoClip.srcOut;
    audio.speed = videoClip.speed;
    // Mirror the whole retiming, not just the scalar: a curve supersedes `speed` outright, so a
    // companion without it plays the ramped source at a constant rate and drifts against the
    // picture until the next edit runs it through syncLinkedTiming.
    audio.speedCurve = videoClip.speedCurve;
    audio.reverse = videoClip.reverse;
    audio.fadeInUs = videoClip.fadeInUs;
    audio.fadeOutUs = videoClip.fadeOutUs;
    audio.fadeCurve = videoClip.fadeCurve;
    audio.fadeShape = videoClip.fadeShape;
    audio.volume = videoClip.volume;
    audio.pan = videoClip.pan;
    return audio;
}

void migrateAudioEffectsToCompanion(drift::Project &project, const QString &videoClipId,
                                    const QString &audioClipId)
{
    int videoTrack = -1;
    int videoClipIdx = -1;
    if (!findClipById(project, videoClipId, &videoTrack, &videoClipIdx))
        return;

    QList<drift::Clip> moved;
    for (const int laneIndex : drift::adjustmentLaneIndexes(project, videoTrack)) {
        drift::Track &lane = project.tracks()[laneIndex];
        for (int c = lane.clips.size() - 1; c >= 0; --c) {
            const drift::Clip &adjustment = lane.clips.at(c);
            if (adjustment.adjustmentKind == drift::AdjustmentKind::AudioEffects
                && adjustment.linkedClipId == videoClipId) {
                moved.prepend(lane.clips.takeAt(c));
            }
        }
    }
    if (moved.isEmpty())
        return;

    for (drift::Clip &adjustment : moved)
        adjustment.linkedClipId = audioClipId;

    // One at a time, re-resolving the audio track each round: ensureAdjustmentLane mints ids and
    // may insert a lane, and it is also what puts two overlapping adjustments on separate lanes
    // instead of stacking them into one.
    for (const drift::Clip &adjustment : moved) {
        int audioTrack = -1;
        int audioClipIdx = -1;
        if (!findClipById(project, audioClipId, &audioTrack, &audioClipIdx))
            return;
        const int laneIndex =
            drift::ensureAdjustmentLane(project, audioTrack, drift::AdjustmentKind::AudioEffects,
                                        adjustment.timelineStart, adjustment.timelineDuration);
        if (laneIndex < 0)
            return;
        project.tracks()[laneIndex].clips.append(adjustment);
    }
}

int videoTrackOrdinal(const drift::Project &project, int trackIndex)
{
    if (trackIndex < 0 || trackIndex >= project.tracks().size())
        return -1;
    if (project.tracks().at(trackIndex).type != drift::TrackType::Video)
        return -1;

    int ordinal = 0;
    for (int t = 0; t < trackIndex; ++t) {
        if (project.tracks().at(t).type == drift::TrackType::Video)
            ++ordinal;
    }
    return ordinal;
}

int ownerVideoOrdinalForAudioTrack(const drift::Project &project, int audioTrackIndex)
{
    if (audioTrackIndex < 0 || audioTrackIndex >= project.tracks().size())
        return -1;

    const drift::Track &audioTrack = project.tracks().at(audioTrackIndex);
    if (audioTrack.type != drift::TrackType::Audio)
        return -1;

    for (const drift::Clip &audioClip : audioTrack.clips) {
        if (audioClip.sourceVideoClipId.isEmpty() && audioClip.linkId.isEmpty())
            continue;

        for (int t = 0; t < project.tracks().size(); ++t) {
            const drift::Track &videoTrack = project.tracks().at(t);
            if (videoTrack.type != drift::TrackType::Video)
                continue;

            for (const drift::Clip &videoClip : videoTrack.clips) {
                if ((!audioClip.sourceVideoClipId.isEmpty()
                     && videoClip.id == audioClip.sourceVideoClipId)
                    || (!audioClip.linkId.isEmpty() && !videoClip.linkId.isEmpty()
                        && videoClip.linkId == audioClip.linkId)) {
                    return videoTrackOrdinal(project, t);
                }
            }
        }
    }

    // Ordinary/imported audio tracks do not have a linked video owner.
    return -1;
}

int audioTrackInsertIndexForVideoTrack(const drift::Project &project,
                                       int sourceVideoTrackIndex)
{
    const int sourceOrdinal =
        videoTrackOrdinal(project, sourceVideoTrackIndex);

    if (sourceOrdinal < 0)
        return project.tracks().size();

    int lastVideoTrack = -1;
    int firstAudioTrack = -1;
    int lastAudioTrack = -1;

    for (int t = 0; t < project.tracks().size(); ++t) {
        const drift::TrackType type = project.tracks().at(t).type;

        if (type == drift::TrackType::Video)
            lastVideoTrack = t;

        if (type != drift::TrackType::Audio)
            continue;

        if (firstAudioTrack < 0)
            firstAudioTrack = t;

        lastAudioTrack = t;

        const int ownerOrdinal =
            ownerVideoOrdinalForAudioTrack(project, t);

        // An unrelated audio track marks the end of the linked-companion block.
        if (ownerOrdinal < 0)
            return t;

        // Insert before the first companion belonging to a lower video track.
        if (ownerOrdinal > sourceOrdinal)
            return t;
    }

    // No audio tracks yet: begin the audio group immediately below all videos.
    if (firstAudioTrack < 0)
        return qBound(0, lastVideoTrack + 1, project.tracks().size());

    // All existing companion tracks belong to videos above this one.
    return lastAudioTrack + 1;
}

bool detachEmbeddedAudioFromVideo(drift::Project &project,
                                  AssetLibrary *library,
                                  int videoTrackIndex,
                                  int videoClipIndex)
{
    if (videoTrackIndex < 0
        || videoTrackIndex >= project.tracks().size())
        return false;

    if (videoClipIndex < 0
        || videoClipIndex >= project.tracks().at(videoTrackIndex).clips.size())
        return false;

    drift::Clip &videoClip =
        project.tracks()[videoTrackIndex].clips[videoClipIndex];

    if (!clipHasEmbeddedAudio(project, library, videoClip))
        return false;

    const QString linkId =
        videoClip.linkId.isEmpty()
            ? QUuid::createUuid().toString(QUuid::WithoutBraces)
            : videoClip.linkId;

    videoClip.linkId = linkId;
    videoClip.suppressEmbeddedAudio = true;

    // Build the companion before inserting into project.tracks(); QList insertion
    // may relocate Track objects and invalidate references into that container.
    const drift::Clip audioClip =
        makeAudioCompanionFromVideo(
            videoClip,
            linkId,
            videoClip.audioStreamIndex);
    const QString videoClipId = videoClip.id;

    const int insertAt =
        audioTrackInsertIndexForVideoTrack(
            project,
            videoTrackIndex);

    drift::Track audioTrack;
    audioTrack.type = drift::TrackType::Audio;
    audioTrack.clips.append(audioClip);

    project.tracks().insert(insertAt, audioTrack);

    migrateAudioEffectsToCompanion(project, videoClipId, audioClip.id);

    return true;
}

QList<QPair<int, int>> selectionWithLinkedPartners(const drift::Project &project, int trackIndex, int clipIndex)
{
    QList<QPair<int, int>> pairs;
    if (trackIndex < 0 || trackIndex >= project.tracks().size())
        return pairs;
    const drift::Track &track = project.tracks().at(trackIndex);
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return pairs;

    pairs.append(qMakePair(trackIndex, clipIndex));
    for (const drift::ClipRef &ref : drift::linkedPartners(project, track.clips.at(clipIndex))) {
        const QPair<int, int> linked(ref.trackIndex, ref.clipIndex);
        if (!pairs.contains(linked))
            pairs.append(linked);
    }
    return pairs;
}

bool declaresParam(const QList<drift::EffectParamSpec> &parameters, const QString &key)
{
    for (const drift::EffectParamSpec &param : parameters) {
        if (param.key == key)
            return true;
        // A vec2 is stored as "<key>.x" / "<key>.y", so each axis is writable on its own.
        if (param.isVec2() && (key == param.key + QLatin1String(".x") || key == param.key + QLatin1String(".y")))
            return true;
        if (param.isRegion()) {
            for (const char *axis : {".x", ".y", ".w", ".h"}) {
                if (key == param.key + QLatin1String(axis))
                    return true;
            }
        }
    }
    return false;
}

bool declaresParam(const drift::EffectPresetMeta &meta, const QString &key)
{
    return declaresParam(meta.parameters, key);
}

int clipIndexById(const drift::Track &track, const QString &id)
{
    for (int i = 0; i < track.clips.size(); ++i) {
        if (track.clips.at(i).id == id)
            return i;
    }
    return -1;
}

QString newClipId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

void applyShapeStylePatch(drift::ShapeStyle &s, const QVariantMap &m)
{
    applyLayerStackPatch(s.layers, m, drift::solidFillLayer(QColor(0, 180, 255), QStringLiteral("fill")));

    const auto fillLayer = [&]() -> drift::TextShadingLayer * {
        if (drift::TextShadingLayer *fill = drift::firstTextLayerOfKind(s.layers, drift::TextLayerKind::Fill, false))
            return fill;
        s.layers.prepend(drift::solidFillLayer(s.primaryColor(), QStringLiteral("fill")));
        return &s.layers.first();
    };
    const auto strokeLayer = [&]() -> drift::TextShadingLayer * {
        if (drift::TextShadingLayer *stroke = drift::firstTextLayerOfKind(s.layers, drift::TextLayerKind::Stroke, false))
            return stroke;
        s.setStroke(0.0);
        return drift::firstTextLayerOfKind(s.layers, drift::TextLayerKind::Stroke, false);
    };

    if (m.contains(QStringLiteral("fill"))) {
        drift::TextShadingLayer *fill = fillLayer();
        const QColor color(m.value(QStringLiteral("fill")).toString());
        fill->paint.color = color;
        if (!fill->paint.gradient.stops.isEmpty())
            fill->paint.gradient.stops.first().color = color;
    }
    if (m.contains(QStringLiteral("fillSecondary"))) {
        drift::TextShadingLayer *fill = fillLayer();
        const QColor color(m.value(QStringLiteral("fillSecondary")).toString());
        if (fill->paint.gradient.stops.size() < 2)
            fill->paint.gradient.stops = {{0.0, fill->paint.color}, {1.0, color}};
        else
            fill->paint.gradient.stops.last().color = color;
    }
    if (m.contains(QStringLiteral("gradientAngle")))
        fillLayer()->paint.gradient.angle = m.value(QStringLiteral("gradientAngle")).toDouble();
    if (m.contains(QStringLiteral("fillKind"))) {
        drift::TextShadingLayer *fill = fillLayer();
        const QString kind = m.value(QStringLiteral("fillKind")).toString();
        fill->enabled = kind != QLatin1String("none");
        if (kind == QLatin1String("solid")) {
            fill->paint.kind = drift::TextPaintKind::Solid;
        } else if (kind == QLatin1String("linear") || kind == QLatin1String("radial")) {
            fill->paint.kind = drift::TextPaintKind::Gradient;
            fill->paint.gradient.kind = kind == QLatin1String("radial") ? drift::TextGradientKind::Radial
                                                                        : drift::TextGradientKind::Linear;
        }
    }
    if (m.contains(QStringLiteral("stroke")))
        strokeLayer()->paint.color = QColor(m.value(QStringLiteral("stroke")).toString());
    if (m.contains(QStringLiteral("strokeWidth"))) {
        drift::TextShadingLayer *stroke = strokeLayer();
        stroke->width = qBound(0.0, m.value(QStringLiteral("strokeWidth")).toDouble(), 200.0);
        stroke->enabled = stroke->width > 0.0;
    }
    if (m.contains(QStringLiteral("strokeStyle"))) {
        drift::TextShadingLayer *stroke = strokeLayer();
        const QString style = m.value(QStringLiteral("strokeStyle")).toString();
        stroke->enabled = style != QLatin1String("none") && stroke->width > 0.0;
        if (style != QLatin1String("none"))
            stroke->dash = drift::strokeDashFromString(style);
    }

    if (m.contains(QStringLiteral("cornerRadius")))
        s.cornerRadius = qBound(0.0, m.value(QStringLiteral("cornerRadius")).toDouble(), 2000.0);
    if (m.contains(QStringLiteral("points")))
        s.points = qBound(3, m.value(QStringLiteral("points")).toInt(), 60);
    if (m.contains(QStringLiteral("innerRatio")))
        s.innerRatio = qBound(0.05, m.value(QStringLiteral("innerRatio")).toDouble(), 0.95);
    if (m.contains(QStringLiteral("headSize")))
        s.headSize = qBound(0.05, m.value(QStringLiteral("headSize")).toDouble(), 0.9);
    if (m.contains(QStringLiteral("thickness")))
        s.thickness = qBound(0.05, m.value(QStringLiteral("thickness")).toDouble(), 1.0);
    if (m.contains(QStringLiteral("tailX")))
        s.tailX = qBound(0.08, m.value(QStringLiteral("tailX")).toDouble(), 0.92);
    if (m.contains(QStringLiteral("tailSize")))
        s.tailSize = qBound(0.05, m.value(QStringLiteral("tailSize")).toDouble(), 0.5);
}

drift::Mask clipMatte(const drift::Project &project, int trackIndex, int clipIndex)
{
    for (const drift::ClipRef &ref : drift::linkedMaskAdjustments(project, trackIndex, clipIndex)) {
        const drift::Mask &mask = project.tracks().at(ref.trackIndex).clips.at(ref.clipIndex).mask;
        if (mask.isMedia())
            return mask;
    }
    return {};
}

bool clipHasMatte(const drift::Project &project, int trackIndex, int clipIndex)
{
    return clipMatte(project, trackIndex, clipIndex).isMedia();
}
} // namespace drift::appcontroller_detail
