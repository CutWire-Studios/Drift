#include "TextAnimationPreset.h"

#include <QCache>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QMutex>
#include <QMutexLocker>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>

// Static-library resources are only pulled into the link when something references them.
static void initTextAnimationResources()
{
    Q_INIT_RESOURCE(drift_text_animations);
}

namespace drift {

namespace {

const QString kUserPrefix = QStringLiteral("user:");
QMutex g_catalogMutex;

// The standard stagger selector every reveal preset uses, spelled with the common param refs.
QJsonObject standardStaggerSelector()
{
    return QJsonObject{
        {QStringLiteral("driver"), QStringLiteral("stagger")},
        {QStringLiteral("domain"), QStringLiteral("{unit}")},
        {QStringLiteral("order"), QStringLiteral("{order}")},
        {QStringLiteral("staggerUs"), QJsonObject{{QStringLiteral("$"), QStringLiteral("stagger")}, {QStringLiteral("mul"), 1000000}}},
        {QStringLiteral("durationUs"), QJsonObject{{QStringLiteral("$"), QStringLiteral("duration")}, {QStringLiteral("mul"), 1000000}}},
        {QStringLiteral("ease"), QStringLiteral("{ease}")},
    };
}

// Walk the recipe replacing "{id}" strings and {"$": id, "mul": k, "add": c} objects.
QJsonValue substitute(const QJsonValue &v, const QMap<QString, VectorSlotValue> &params)
{
    if (v.isString()) {
        const QString s = v.toString();
        if (s.size() > 2 && s.startsWith(QLatin1Char('{')) && s.endsWith(QLatin1Char('}'))) {
            const auto it = params.constFind(s.mid(1, s.size() - 2));
            if (it != params.constEnd())
                return slotValueToJson(*it);
        }
        return v;
    }
    if (v.isArray()) {
        QJsonArray out;
        for (const QJsonValue &item : v.toArray())
            out.append(substitute(item, params));
        return out;
    }
    if (v.isObject()) {
        const QJsonObject o = v.toObject();
        if (o.contains(QStringLiteral("$"))) {
            const auto it = params.constFind(o.value(QStringLiteral("$")).toString());
            double value = it != params.constEnd() ? it->scalar : 0.0;
            value = value * o.value(QStringLiteral("mul")).toDouble(1.0) + o.value(QStringLiteral("add")).toDouble(0.0);
            return value;
        }
        QJsonObject out;
        for (auto it = o.constBegin(); it != o.constEnd(); ++it)
            out.insert(it.key(), substitute(it.value(), params));
        return out;
    }
    return v;
}

void negateParam(TextAnimParam &p)
{
    p.value = -p.value;
    if (p.curve.isEmpty())
        return;
    KeyframeTrack<double> flipped;
    flipped.setEnabled(p.curve.enabled());
    for (auto it = p.curve.keyframes().constBegin(); it != p.curve.keyframes().constEnd(); ++it) {
        Keyframe<double> k = it.value();
        k.value = -k.value;
        k.inDy = -k.inDy;
        k.outDy = -k.outDy;
        flipped.setKeyframe(it.key(), k);
    }
    p.curve = flipped;
}

bool hasStaggerSelector(const QList<TextAnimator> &animators)
{
    for (const TextAnimator &a : animators)
        for (const TextRangeSelector &s : a.selectors)
            if (s.driver == TextSelectorDriver::Stagger)
                return true;
    return false;
}

} // namespace

const QList<TextAnimParamSpec> &textAnimationCommonParams()
{
    static const QList<TextAnimParamSpec> common = [] {
        const auto scalar = [](const char *id, const char *label, const char *unit, double min, double max, double step,
                               double def) {
            TextAnimParamSpec s;
            s.id = QLatin1String(id);
            s.label = QLatin1String(label);
            s.unit = QLatin1String(unit);
            s.min = min;
            s.max = max;
            s.step = step;
            s.defaultValue = VectorSlotValue::fromScalar(def);
            return s;
        };
        const auto enumeration = [](const char *id, const char *label, const QStringList &values, const char *def) {
            TextAnimParamSpec s;
            s.id = QLatin1String(id);
            s.label = QLatin1String(label);
            s.type = TextAnimParamSpec::Type::Enum;
            s.enumValues = values;
            s.defaultValue = VectorSlotValue::fromText(QLatin1String(def));
            return s;
        };
        return QList<TextAnimParamSpec>{
            scalar("duration", "Duration", "s", 0.0, 5.0, 0.01, 0.4),
            scalar("stagger", "Stagger", "s", 0.0, 2.0, 0.01, 0.06),
            enumeration("unit", "By", {QStringLiteral("block"), QStringLiteral("character"), QStringLiteral("word"), QStringLiteral("line")}, "block"),
            enumeration("order", "Order", {QStringLiteral("forward"), QStringLiteral("backward"), QStringLiteral("centerOut"), QStringLiteral("random")}, "forward"),
            enumeration("ease", "Ease", {QStringLiteral("linear"), QStringLiteral("easeIn"), QStringLiteral("easeOut"), QStringLiteral("easeInOut"), QStringLiteral("back"), QStringLiteral("bounce"), QStringLiteral("smooth")}, "easeOut"),
        };
    }();
    return common;
}

const TextAnimParamSpec *TextAnimationPreset::param(const QString &paramId) const
{
    for (const TextAnimParamSpec &spec : params)
        if (spec.id == paramId)
            return &spec;
    return nullptr;
}

QMap<QString, VectorSlotValue> TextAnimationPreset::defaultParams() const
{
    QMap<QString, VectorSlotValue> out;
    for (const TextAnimParamSpec &spec : params)
        out.insert(spec.id, spec.defaultValue);
    return out;
}

bool TextAnimationPreset::flag(const QString &name, bool fallback) const
{
    return flags.value(name).toBool(fallback);
}

QJsonObject TextAnimationPreset::toJson() const
{
    QJsonArray slotsJson;
    for (TextAnimSlotKind kind : slotKinds)
        slotsJson.append(textAnimSlotKindToString(kind));
    QJsonArray paramsJson;
    for (const TextAnimParamSpec &spec : params)
        paramsJson.append(spec.toJson());
    QJsonObject o{
        {QStringLiteral("format"), 1},
        {QStringLiteral("commonParams"), false}, // params below are the complete list
        {QStringLiteral("id"), id},
        {QStringLiteral("label"), label},
        {QStringLiteral("category"), category},
        {QStringLiteral("sampleText"), sampleText},
        {QStringLiteral("slots"), slotsJson},
        {QStringLiteral("order"), order},
        {QStringLiteral("params"), paramsJson},
        {QStringLiteral("animators"), animators},
    };
    if (!caret.isEmpty())
        o.insert(QStringLiteral("caret"), caret);
    if (!flags.isEmpty())
        o.insert(QStringLiteral("flags"), flags);
    if (!report.isEmpty())
        o.insert(QStringLiteral("report"), QJsonArray::fromStringList(report));
    return o;
}

std::optional<TextAnimationPreset> TextAnimationPreset::fromJson(const QJsonObject &o, QString *error)
{
    TextAnimationPreset p;
    p.id = o.value(QStringLiteral("id")).toString();
    p.label = o.value(QStringLiteral("label")).toString(p.id);
    if (p.id.isEmpty()) {
        if (error)
            *error = QStringLiteral("preset has no id");
        return std::nullopt;
    }
    p.category = o.value(QStringLiteral("category")).toString(QStringLiteral("basic"));
    p.sampleText = o.value(QStringLiteral("sampleText")).toString(QStringLiteral("Your text"));
    for (const QJsonValue &v : o.value(QStringLiteral("slots")).toArray())
        p.slotKinds.append(textAnimSlotKindFromString(v.toString()));
    if (p.slotKinds.isEmpty())
        p.slotKinds.append(TextAnimSlotKind::In);
    p.order = o.value(QStringLiteral("order")).toInt(100);
    p.flags = o.value(QStringLiteral("flags")).toObject();
    p.caret = o.value(QStringLiteral("caret")).toObject();
    for (const QJsonValue &v : o.value(QStringLiteral("report")).toArray())
        p.report.append(v.toString());

    const bool reveal = p.slotKinds.contains(TextAnimSlotKind::In) || p.slotKinds.contains(TextAnimSlotKind::Out);
    if (o.value(QStringLiteral("commonParams")).toBool(reveal)) {
        const QJsonObject defaults = o.value(QStringLiteral("defaults")).toObject();
        for (TextAnimParamSpec spec : textAnimationCommonParams()) {
            if (defaults.contains(spec.id))
                spec.defaultValue = paramValueFromJson(spec.type, defaults.value(spec.id));
            p.params.append(spec);
        }
    }
    for (const QJsonValue &v : o.value(QStringLiteral("params")).toArray()) {
        const TextAnimParamSpec spec = TextAnimParamSpec::fromJson(v.toObject());
        if (spec.id.isEmpty())
            continue;
        bool replaced = false;
        for (TextAnimParamSpec &existing : p.params) {
            if (existing.id == spec.id) {
                existing = spec;
                replaced = true;
            }
        }
        if (!replaced)
            p.params.append(spec);
    }

    for (const QJsonValue &v : o.value(QStringLiteral("animators")).toArray()) {
        QJsonObject animator = v.toObject();
        QJsonArray selectors;
        for (const QJsonValue &s : animator.value(QStringLiteral("selectors")).toArray()) {
            if (s.isString() && s.toString() == QLatin1String("stagger"))
                selectors.append(standardStaggerSelector());
            else
                selectors.append(s);
        }
        animator.insert(QStringLiteral("selectors"), selectors);
        p.animators.append(animator);
    }
    if (p.animators.isEmpty()) {
        if (error)
            *error = QStringLiteral("preset '%1' has no animators").arg(p.id);
        return std::nullopt;
    }
    return p;
}

bool isUserTextAnimationPresetId(const QString &id)
{
    return id.startsWith(kUserPrefix);
}

TextAnimationPresetCatalog &TextAnimationPresetCatalog::instance()
{
    static TextAnimationPresetCatalog catalog;
    return catalog;
}

QString TextAnimationPresetCatalog::userDir()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return base.isEmpty() ? QString() : QDir(base).filePath(QStringLiteral("text-animations"));
}

void TextAnimationPresetCatalog::loadDir(const QString &dir, bool builtIn, const QString &idPrefix) const
{
    const QDir d(dir);
    for (const QFileInfo &info : d.entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Name)) {
        QFile file(info.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly))
            continue;
        QString error;
        std::optional<TextAnimationPreset> preset =
            TextAnimationPreset::fromJson(QJsonDocument::fromJson(file.readAll()).object(), &error);
        if (!preset) {
            qWarning("text animation preset %s: %s", qPrintable(info.fileName()), qPrintable(error));
            continue;
        }
        preset->builtIn = builtIn;
        preset->source = builtIn ? QString() : info.absoluteFilePath();
        if (!idPrefix.isEmpty() && !preset->id.startsWith(idPrefix))
            preset->id = idPrefix + preset->id;
        bool replaced = false;
        for (TextAnimationPreset &existing : m_presets) {
            if (existing.id == preset->id) {
                existing = *preset;
                replaced = true;
            }
        }
        if (!replaced)
            m_presets.append(*preset);
    }
}

void TextAnimationPresetCatalog::ensureLoaded() const
{
    if (m_loaded)
        return;
    m_loaded = true;
    m_presets.clear();
    initTextAnimationResources();
    loadDir(QStringLiteral(":/text-animations"), true, QString());
    const QString extra = qEnvironmentVariable("DRIFT_TEXT_ANIMATIONS");
    for (const QString &dir : extra.split(QLatin1Char(':'), Qt::SkipEmptyParts))
        loadDir(dir, true, QString());
    if (!userDir().isEmpty())
        loadDir(userDir(), false, kUserPrefix);
    std::stable_sort(m_presets.begin(), m_presets.end(),
                     [](const TextAnimationPreset &a, const TextAnimationPreset &b) { return a.order < b.order; });
}

void TextAnimationPresetCatalog::reload()
{
    QMutexLocker lock(&g_catalogMutex);
    m_loaded = false;
    ensureLoaded();
    clearTextAnimationResolveCache();
}

QList<TextAnimationPreset> TextAnimationPresetCatalog::presets() const
{
    QMutexLocker lock(&g_catalogMutex);
    ensureLoaded();
    return m_presets;
}

QList<TextAnimationPreset> TextAnimationPresetCatalog::presetsFor(TextAnimSlotKind kind) const
{
    QList<TextAnimationPreset> out;
    for (const TextAnimationPreset &p : presets())
        if (p.supportsSlot(kind))
            out.append(p);
    return out;
}

std::optional<TextAnimationPreset> TextAnimationPresetCatalog::presetForId(const QString &id) const
{
    QMutexLocker lock(&g_catalogMutex);
    ensureLoaded();
    for (const TextAnimationPreset &p : m_presets)
        if (p.id == id)
            return p;
    return std::nullopt;
}

QString TextAnimationPresetCatalog::addUserPreset(TextAnimationPreset preset)
{
    const QString dir = userDir();
    if (dir.isEmpty())
        return {};
    QDir().mkpath(dir);
    if (!preset.id.startsWith(kUserPrefix))
        preset.id = kUserPrefix + QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    preset.builtIn = false;
    const QString path = QDir(dir).filePath(preset.id.mid(kUserPrefix.size()) + QStringLiteral(".json"));
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return {};
    QJsonObject json = preset.toJson();
    json.insert(QStringLiteral("id"), preset.id.mid(kUserPrefix.size()));
    file.write(QJsonDocument(json).toJson(QJsonDocument::Indented));
    if (!file.commit())
        return {};
    reload();
    return preset.id;
}

bool TextAnimationPresetCatalog::removeUserPreset(const QString &id)
{
    const std::optional<TextAnimationPreset> preset = presetForId(id);
    if (!preset || preset->builtIn || preset->source.isEmpty())
        return false;
    if (!QFile::remove(preset->source))
        return false;
    reload();
    return true;
}

bool TextAnimationPresetCatalog::renameUserPreset(const QString &id, const QString &label)
{
    std::optional<TextAnimationPreset> preset = presetForId(id);
    if (!preset || preset->builtIn || preset->source.isEmpty())
        return false;
    preset->label = label;
    QSaveFile file(preset->source);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    QJsonObject json = preset->toJson();
    json.insert(QStringLiteral("id"), preset->id.mid(kUserPrefix.size()));
    file.write(QJsonDocument(json).toJson(QJsonDocument::Indented));
    if (!file.commit())
        return false;
    reload();
    return true;
}

ResolvedPresetSlot resolvePresetSlot(const TextAnimationPreset &preset, const QMap<QString, VectorSlotValue> &overrides,
                                     TextAnimSlotKind kind)
{
    ResolvedPresetSlot out;
    QMap<QString, VectorSlotValue> params = preset.defaultParams();
    for (auto it = overrides.constBegin(); it != overrides.constEnd(); ++it) {
        if (params.contains(it.key()))
            params.insert(it.key(), *it);
    }
    for (const QJsonValue &v : substitute(preset.animators, params).toArray())
        out.animators.append(textAnimatorFromJson(v.toObject()));
    if (out.animators.isEmpty())
        return out;

    const bool mirror = preset.flags.value(QStringLiteral("mirrorForOut"))
                            .toBool(preset.supportsSlot(TextAnimSlotKind::In) && preset.supportsSlot(TextAnimSlotKind::Out));
    if (kind == TextAnimSlotKind::Out && mirror) {
        for (TextAnimator &a : out.animators) {
            negateParam(a.props.position.x);
            negateParam(a.props.position.y);
        }
    }
    if (!preset.caret.isEmpty()) {
        const TextCaret caret = textCaretFromJson(substitute(preset.caret, params).toObject());
        out.caret = caret;
        if (caret.enabled && kind == TextAnimSlotKind::In)
            out.delayUs = caret.leadUs;
    }
    if (kind == TextAnimSlotKind::Loop)
        out.periodUs = secondsToUs(scalarParam(params, QStringLiteral("period"), 0.0));
    else if (!hasStaggerSelector(out.animators))
        out.durationUs = secondsToUs(scalarParam(params, QStringLiteral("duration"), 0.4));
    out.valid = true;
    return out;
}

namespace {

QMutex g_resolveMutex;
QCache<quint64, ResolvedTextAnimation> g_resolveCache(64);

void resolveSlot(const TextAnimationSlot &slot, TextAnimSlotKind kind, QList<TextAnimator> *animators,
                 TextAnimationSlot *effective, std::optional<TextCaret> *caret)
{
    *effective = slot;
    if (!slot.enabled)
        return;
    if (slot.presetId.isEmpty()) {
        *animators = slot.animators;
        return;
    }
    const std::optional<TextAnimationPreset> preset = TextAnimationPresetCatalog::instance().presetForId(slot.presetId);
    if (!preset)
        return;
    const ResolvedPresetSlot resolved = resolvePresetSlot(*preset, slot.params, kind);
    if (!resolved.valid)
        return;
    *animators = resolved.animators;
    if (resolved.caret)
        *caret = resolved.caret;
    if (effective->durationUs == 0)
        effective->durationUs = resolved.durationUs;
    if (effective->delayUs == 0)
        effective->delayUs = resolved.delayUs;
    if (effective->periodUs == 0)
        effective->periodUs = resolved.periodUs;
}

} // namespace

ResolvedTextAnimation resolveTextAnimation(const TextAnimationSet &set)
{
    const quint64 key = textAnimationSetHash(set);
    {
        QMutexLocker lock(&g_resolveMutex);
        if (const ResolvedTextAnimation *hit = g_resolveCache.object(key))
            return *hit;
    }
    ResolvedTextAnimation out;
    out.set = set;
    std::optional<TextCaret> caret;
    resolveSlot(set.in, TextAnimSlotKind::In, &out.resolved.in, &out.set.in, &caret);
    resolveSlot(set.out, TextAnimSlotKind::Out, &out.resolved.out, &out.set.out, &caret);
    resolveSlot(set.loop, TextAnimSlotKind::Loop, &out.resolved.loop, &out.set.loop, &caret);
    // A preset's caret applies unless the style switched its own caret on explicitly.
    if (caret && !set.caret.enabled)
        out.set.caret = *caret;
    QMutexLocker lock(&g_resolveMutex);
    g_resolveCache.insert(key, new ResolvedTextAnimation(out));
    return out;
}

void clearTextAnimationResolveCache()
{
    QMutexLocker lock(&g_resolveMutex);
    g_resolveCache.clear();
}

TextAnimationSlot legacyTextAnimationSlot(const QString &kind, TimeUs durationUs, const QString &ease,
                                          const QString &unit, TimeUs staggerUs, const QString &order, bool *isLoop)
{
    static const QMap<QString, QString> kPresetForKind{
        {QStringLiteral("fade"), QStringLiteral("fade")},
        {QStringLiteral("slideUp"), QStringLiteral("slide-up")},
        {QStringLiteral("slideDown"), QStringLiteral("slide-down")},
        {QStringLiteral("slideLeft"), QStringLiteral("slide-left")},
        {QStringLiteral("slideRight"), QStringLiteral("slide-right")},
        {QStringLiteral("pop"), QStringLiteral("pop")},
        {QStringLiteral("blur"), QStringLiteral("blur-in")},
        {QStringLiteral("typewriter"), QStringLiteral("typewriter")},
        {QStringLiteral("rise"), QStringLiteral("rise")},
        {QStringLiteral("bounce"), QStringLiteral("bounce")},
        {QStringLiteral("wave"), QStringLiteral("wave")},
    };
    TextAnimationSlot slot;
    if (isLoop)
        *isLoop = kind == QLatin1String("wave");
    const auto it = kPresetForKind.constFind(kind);
    if (it == kPresetForKind.constEnd())
        return slot; // "none" and unknown spellings: no animation
    slot.presetId = *it;
    if (kind == QLatin1String("wave"))
        return slot; // the loop preset has its own params
    slot.params.insert(QStringLiteral("duration"), VectorSlotValue::fromScalar(usToSeconds(durationUs)));
    slot.params.insert(QStringLiteral("stagger"), VectorSlotValue::fromScalar(usToSeconds(staggerUs)));
    slot.params.insert(QStringLiteral("unit"), VectorSlotValue::fromText(unit.isEmpty() ? QStringLiteral("block") : unit));
    slot.params.insert(QStringLiteral("order"), VectorSlotValue::fromText(order.isEmpty() ? QStringLiteral("forward") : order));
    slot.params.insert(QStringLiteral("ease"), VectorSlotValue::fromText(ease.isEmpty() ? QStringLiteral("easeOut") : ease));
    return slot;
}

} // namespace drift
