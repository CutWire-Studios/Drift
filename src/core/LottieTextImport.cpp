#include "LottieTextImport.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

#include <cmath>

namespace drift::lottie {

namespace {

struct Comp
{
    double ip = 0.0;
    double op = 1.0;
    double fr = 30.0;
    double span() const { return std::max(1.0, op - ip); }
};

// A Lottie animatable value's component `c`: a constant, or a curve keyed over the comp's
// [ip, op] mapped onto the slot's progress.
TextAnimParam paramFrom(const QJsonValue &v, int c, const Comp &comp, double fallback)
{
    TextAnimParam out(fallback);
    if (!v.isObject())
        return out;
    const QJsonObject o = v.toObject();
    const QJsonValue k = o.value(QStringLiteral("k"));
    const auto component = [c](const QJsonValue &value, double def) {
        if (value.isArray())
            return value.toArray().at(c).toDouble(def);
        return value.toDouble(def);
    };
    if (o.value(QStringLiteral("a")).toInt(0) == 0 || !k.isArray()) {
        out.value = component(k, fallback);
        return out;
    }
    const QJsonArray keys = k.toArray();
    struct Key
    {
        double p;
        double value;
        bool hold;
        QPointF in, outHandle;
    };
    QList<Key> parsed;
    for (int i = 0; i < keys.size(); ++i) {
        const QJsonObject key = keys.at(i).toObject();
        Key entry;
        entry.p = (key.value(QStringLiteral("t")).toDouble() - comp.ip) / comp.span();
        // The last key may carry no value of its own: it ends the previous segment.
        if (key.contains(QStringLiteral("s")))
            entry.value = component(key.value(QStringLiteral("s")), fallback);
        else if (!parsed.isEmpty() && keys.at(i - 1).toObject().contains(QStringLiteral("e")))
            entry.value = component(keys.at(i - 1).toObject().value(QStringLiteral("e")), fallback);
        else
            entry.value = parsed.isEmpty() ? fallback : parsed.last().value;
        entry.hold = key.value(QStringLiteral("h")).toInt(0) == 1;
        const QJsonObject o_ = key.value(QStringLiteral("o")).toObject();
        const QJsonObject i_ = key.value(QStringLiteral("i")).toObject();
        entry.outHandle = QPointF(component(o_.value(QStringLiteral("x")), 0.0), component(o_.value(QStringLiteral("y")), 0.0));
        entry.in = QPointF(component(i_.value(QStringLiteral("x")), 1.0), component(i_.value(QStringLiteral("y")), 1.0));
        parsed.append(entry);
    }
    if (parsed.isEmpty())
        return out;
    out.value = parsed.first().value;
    for (int i = 0; i < parsed.size(); ++i) {
        const Key &key = parsed.at(i);
        Keyframe<double> kf;
        kf.value = key.value;
        kf.hold = key.hold;
        // Lottie handles are fractions of the segment; ours are relative offsets in curve space.
        if (i + 1 < parsed.size()) {
            const Key &next = parsed.at(i + 1);
            const double dt = (next.p - key.p) * kProgressScale;
            const double dv = next.value - key.value;
            kf.outDx = key.outHandle.x() * dt;
            kf.outDy = key.outHandle.y() * dv;
        }
        if (i > 0) {
            const Key &prev = parsed.at(i - 1);
            const double dt = (key.p - prev.p) * kProgressScale;
            const double dv = key.value - prev.value;
            kf.inDx = -(1.0 - prev.in.x()) * dt;
            kf.inDy = -(1.0 - prev.in.y()) * dv;
        }
        out.curve.setKeyframe(static_cast<TimeUs>(std::round(std::clamp(key.p, 0.0, 1.0) * kProgressScale)), kf);
    }
    return out;
}

bool isAnimated(const QJsonValue &v)
{
    return v.isObject() && v.toObject().value(QStringLiteral("a")).toInt(0) == 1;
}

QColor colorFrom(const QJsonValue &v)
{
    QJsonValue k = v.toObject().value(QStringLiteral("k"));
    if (k.isArray() && !k.toArray().isEmpty() && k.toArray().first().isObject())
        k = k.toArray().first().toObject().value(QStringLiteral("s")); // first key of an animated colour
    const QJsonArray a = k.toArray();
    if (a.size() < 3)
        return QColor();
    return QColor::fromRgbF(std::clamp(a.at(0).toDouble(), 0.0, 1.0), std::clamp(a.at(1).toDouble(), 0.0, 1.0),
                            std::clamp(a.at(2).toDouble(), 0.0, 1.0), a.size() > 3 ? std::clamp(a.at(3).toDouble(), 0.0, 1.0) : 1.0);
}

std::optional<TextRangeSelector> selectorFrom(const QJsonObject &s, const Comp &comp, QStringList *warnings)
{
    if (s.isEmpty())
        return std::nullopt;
    if (s.value(QStringLiteral("t")).toInt(0) != 0) {
        warnings->append(QStringLiteral("expression selector skipped (Drift evaluates no expressions)"));
        return std::nullopt;
    }
    TextRangeSelector sel;
    sel.driver = TextSelectorDriver::Curves;
    sel.units = s.value(QStringLiteral("r")).toInt(1) == 2 ? TextSelectorUnits::Index : TextSelectorUnits::Percent;
    static const TextSelectorDomain domains[] = {TextSelectorDomain::Chars, TextSelectorDomain::CharsExcludingSpaces,
                                                 TextSelectorDomain::Words, TextSelectorDomain::Lines};
    sel.domain = domains[std::clamp(s.value(QStringLiteral("b")).toInt(1), 1, 4) - 1];
    static const TextSelectorMode modes[] = {TextSelectorMode::Add, TextSelectorMode::Subtract, TextSelectorMode::Intersect,
                                             TextSelectorMode::Min, TextSelectorMode::Max, TextSelectorMode::Difference};
    sel.mode = modes[std::clamp(s.value(QStringLiteral("m")).toInt(1), 1, 6) - 1];
    static const TextSelectorShape shapes[] = {TextSelectorShape::Square, TextSelectorShape::RampUp, TextSelectorShape::RampDown,
                                               TextSelectorShape::Triangle, TextSelectorShape::Round, TextSelectorShape::Smooth};
    sel.shape = shapes[std::clamp(s.value(QStringLiteral("sh")).toInt(1), 1, 6) - 1];
    const double endDefault = sel.units == TextSelectorUnits::Percent ? 100.0 : kTextSelectorIndexEnd;
    sel.start = paramFrom(s.value(QStringLiteral("s")), 0, comp, 0.0);
    sel.end = paramFrom(s.value(QStringLiteral("e")), 0, comp, endDefault);
    sel.offset = paramFrom(s.value(QStringLiteral("o")), 0, comp, 0.0);
    sel.amount = paramFrom(s.value(QStringLiteral("a")), 0, comp, 100.0);
    sel.easeLo = paramFrom(s.value(QStringLiteral("ne")), 0, comp, 0.0);
    sel.easeHi = paramFrom(s.value(QStringLiteral("xe")), 0, comp, 0.0);
    sel.smoothness = paramFrom(s.value(QStringLiteral("sm")), 0, comp, 100.0);
    if (s.value(QStringLiteral("rn")).toInt(0) == 1)
        warnings->append(QStringLiteral("randomized order is not applied to range selectors"));
    return sel;
}

std::optional<TextAnimator> animatorFrom(const QJsonObject &janimator, const Comp &comp, QStringList *warnings)
{
    const QJsonObject props = janimator.value(QStringLiteral("a")).toObject();
    if (props.isEmpty())
        return std::nullopt;
    TextAnimator animator;
    animator.name = janimator.value(QStringLiteral("nm")).toString();
    const QJsonValue jsel = janimator.value(QStringLiteral("s"));
    if (jsel.isArray()) {
        for (const QJsonValue &v : jsel.toArray())
            if (std::optional<TextRangeSelector> sel = selectorFrom(v.toObject(), comp, warnings))
                animator.selectors.append(*sel);
    } else if (std::optional<TextRangeSelector> sel = selectorFrom(jsel.toObject(), comp, warnings)) {
        animator.selectors.append(*sel);
    }
    // A selector we could not read would otherwise turn into "everything, always" — drop the
    // animator instead; the warning says why.
    if ((jsel.isObject() && !jsel.toObject().isEmpty()) || (jsel.isArray() && !jsel.toArray().isEmpty())) {
        if (animator.selectors.isEmpty())
            return std::nullopt;
    }
    TextAnimatorProps &p = animator.props;
    bool any = false;
    if (props.contains(QStringLiteral("p"))) {
        p.position.x = paramFrom(props.value(QStringLiteral("p")), 0, comp, 0.0);
        p.position.y = paramFrom(props.value(QStringLiteral("p")), 1, comp, 0.0);
        p.position.unit = TextLengthUnit::Px;
        any = true;
    }
    if (props.contains(QStringLiteral("s"))) {
        p.scaleX = paramFrom(props.value(QStringLiteral("s")), 0, comp, 100.0);
        p.scaleY = paramFrom(props.value(QStringLiteral("s")), 1, comp, 100.0);
        any = true;
    }
    if (props.contains(QStringLiteral("r"))) {
        p.rotation = paramFrom(props.value(QStringLiteral("r")), 0, comp, 0.0);
        any = true;
    }
    if (props.contains(QStringLiteral("rx")) || props.contains(QStringLiteral("ry")))
        warnings->append(QStringLiteral("3D rotation (rx/ry) is not supported; only z rotation was kept"));
    if (props.contains(QStringLiteral("sk"))) {
        p.skew = paramFrom(props.value(QStringLiteral("sk")), 0, comp, 0.0);
        any = true;
    }
    if (props.contains(QStringLiteral("sa")) && !qFuzzyIsNull(paramFrom(props.value(QStringLiteral("sa")), 0, comp, 0.0).value))
        warnings->append(QStringLiteral("skew axis is not supported; skew is applied along x"));
    if (props.contains(QStringLiteral("o"))) {
        p.opacity = paramFrom(props.value(QStringLiteral("o")), 0, comp, 100.0);
        p.hasOpacity = true;
        any = true;
    }
    if (props.contains(QStringLiteral("fo"))) {
        p.fillOpacity = paramFrom(props.value(QStringLiteral("fo")), 0, comp, 100.0);
        p.hasFillOpacity = true;
        any = true;
    }
    if (props.contains(QStringLiteral("so"))) {
        p.strokeOpacity = paramFrom(props.value(QStringLiteral("so")), 0, comp, 100.0);
        p.hasStrokeOpacity = true;
        any = true;
    }
    if (props.contains(QStringLiteral("fc"))) {
        p.fillColor = colorFrom(props.value(QStringLiteral("fc")));
        p.hasFillColor = p.fillColor.isValid();
        if (isAnimated(props.value(QStringLiteral("fc"))))
            warnings->append(QStringLiteral("animated fill colour keeps only its first key"));
        any = any || p.hasFillColor;
    }
    if (props.contains(QStringLiteral("sc"))) {
        p.strokeColor = colorFrom(props.value(QStringLiteral("sc")));
        p.hasStrokeColor = p.strokeColor.isValid();
        any = any || p.hasStrokeColor;
    }
    if (props.contains(QStringLiteral("t"))) {
        p.tracking = paramFrom(props.value(QStringLiteral("t")), 0, comp, 0.0);
        any = true;
    }
    if (props.contains(QStringLiteral("ls"))) {
        p.lineSpacing = paramFrom(props.value(QStringLiteral("ls")), 0, comp, 0.0);
        any = true;
    }
    if (props.contains(QStringLiteral("bl"))) {
        p.blur = paramFrom(props.value(QStringLiteral("bl")), 0, comp, 0.0);
        any = true;
    }
    if (props.contains(QStringLiteral("sw"))) {
        p.strokeWidth = paramFrom(props.value(QStringLiteral("sw")), 0, comp, 0.0);
        any = true;
    }
    if (props.contains(QStringLiteral("a")))
        warnings->append(QStringLiteral("anchor point animation is not supported"));
    if (!any)
        return std::nullopt;
    return animator;
}

QList<QJsonObject> textLayers(const QJsonObject &doc)
{
    QList<QJsonObject> out;
    for (const QJsonValue &v : doc.value(QStringLiteral("layers")).toArray()) {
        const QJsonObject layer = v.toObject();
        if (layer.value(QStringLiteral("ty")).toInt(-1) == 5)
            out.append(layer);
    }
    return out;
}

Comp compOf(const QJsonObject &doc)
{
    Comp comp;
    comp.ip = doc.value(QStringLiteral("ip")).toDouble(0.0);
    comp.op = doc.value(QStringLiteral("op")).toDouble(30.0);
    comp.fr = doc.value(QStringLiteral("fr")).toDouble(30.0);
    return comp;
}

QString slugOf(const QString &name)
{
    QString slug = name.toLower();
    slug.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("-"));
    slug = slug.section(QLatin1Char('-'), 0, -1, QString::SectionSkipEmpty);
    while (slug.startsWith(QLatin1Char('-')))
        slug.remove(0, 1);
    while (slug.endsWith(QLatin1Char('-')))
        slug.chop(1);
    return slug.isEmpty() ? QStringLiteral("imported") : slug;
}

// Where the motion starts and ends decides the gallery: away→rest is an entrance, rest→away an
// exit, anything else a loop.
QString guessCategory(const QList<TextAnimator> &animators)
{
    using namespace drift::textanim;
    TextAnimationSet set;
    set.in.animators = animators;
    set.in.durationUs = 1'000'000;
    ResolvedSlots resolved;
    resolved.in = animators;
    QList<FragmentInfo> frags(12);
    for (int i = 0; i < 12; ++i) {
        frags[i].charIndex = frags[i].nonSpaceIndex = frags[i].wordIndex = i;
        frags[i].advance = 20;
    }
    Domains domains;
    domains.chars = domains.nonSpaceChars = domains.words = 12;
    domains.lines = 1;
    EvalContext ctx;
    ctx.windowDurationUs = 1'000'000;
    ctx.boxWidthPx = 400;
    ctx.boxHeightPx = 100;
    const auto away = [&](TimeUs t) {
        ctx.timelineUs = t;
        const Frame f = evaluateTextAnimation(set, resolved, frags, domains, ctx);
        double score = std::abs(1.0 - f.block.opacity) + std::abs(f.block.dx) / 100.0 + std::abs(f.block.dy) / 100.0
                       + std::abs(1.0 - f.block.scale);
        for (const FragmentProps &p : f.props)
            score += (std::abs(1.0 - p.opacity) + std::abs(p.dx) / 100.0 + std::abs(p.dy) / 100.0 + std::abs(1.0 - p.scaleX)
                      + std::abs(p.rotation) / 90.0 + p.blurPx / 20.0) / 12.0;
        return score;
    };
    const double start = away(0);
    const double end = away(1'000'000);
    if (start > 0.1 && end < 0.05)
        return QStringLiteral("in");
    if (end > 0.1 && start < 0.05)
        return QStringLiteral("out");
    return QStringLiteral("loop");
}

} // namespace

TextImportReport inspectLottieText(const QByteArray &json)
{
    TextImportReport report;
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (!doc.isObject()) {
        report.error = parseError.errorString();
        return report;
    }
    const Comp comp = compOf(doc.object());
    for (const QJsonObject &layer : textLayers(doc.object())) {
        TextLayerSummary summary;
        summary.name = layer.value(QStringLiteral("nm")).toString();
        for (const QJsonValue &v : layer.value(QStringLiteral("t")).toObject().value(QStringLiteral("a")).toArray()) {
            QStringList warnings;
            if (animatorFrom(v.toObject(), comp, &warnings))
                ++summary.animatorCount;
            summary.unsupported.append(warnings);
        }
        summary.unsupported.removeDuplicates();
        report.layers.append(summary);
    }
    report.ok = true;
    return report;
}

std::optional<TextAnimationPreset> importLottieTextPreset(const QByteArray &json, const ImportOptions &options,
                                                          QStringList *warnings, QString *error)
{
    QStringList localWarnings;
    if (!warnings)
        warnings = &localWarnings;
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (!doc.isObject()) {
        if (error)
            *error = parseError.errorString();
        return std::nullopt;
    }
    const QList<QJsonObject> layers = textLayers(doc.object());
    if (layers.isEmpty()) {
        if (error)
            *error = QStringLiteral("the document has no text layer");
        return std::nullopt;
    }
    const Comp comp = compOf(doc.object());

    QJsonObject layer;
    QList<TextAnimator> animators;
    const auto collect = [&](const QJsonObject &candidate) {
        QList<TextAnimator> out;
        for (const QJsonValue &v : candidate.value(QStringLiteral("t")).toObject().value(QStringLiteral("a")).toArray())
            if (std::optional<TextAnimator> a = animatorFrom(v.toObject(), comp, warnings))
                out.append(*a);
        return out;
    };
    if (options.layerIndex >= 0 && options.layerIndex < layers.size()) {
        layer = layers.at(options.layerIndex);
        animators = collect(layer);
    } else {
        for (const QJsonObject &candidate : layers) {
            animators = collect(candidate);
            if (!animators.isEmpty()) {
                layer = candidate;
                break;
            }
        }
    }
    if (animators.isEmpty()) {
        if (error)
            *error = QStringLiteral("no text animator Drift can use");
        return std::nullopt;
    }
    warnings->removeDuplicates();

    TextAnimationPreset preset;
    const QString name = layer.value(QStringLiteral("nm")).toString();
    preset.id = options.id.isEmpty() ? slugOf(name) : options.id;
    preset.label = options.label.isEmpty() ? (name.isEmpty() ? QStringLiteral("Imported") : name) : options.label;
    preset.category = QStringLiteral("imported");
    preset.sampleText = QStringLiteral("Imported");
    const QString slot = options.category.isEmpty() ? guessCategory(animators) : options.category;
    preset.slotKinds = {textAnimSlotKindFromString(slot)};
    preset.builtIn = false;
    preset.order = 900;
    for (const TextAnimator &a : animators)
        preset.animators.append(textAnimatorToJson(a));
    TextAnimParamSpec duration;
    duration.id = slot == QLatin1String("loop") ? QStringLiteral("period") : QStringLiteral("duration");
    duration.label = slot == QLatin1String("loop") ? QStringLiteral("Period") : QStringLiteral("Duration");
    duration.unit = QStringLiteral("s");
    duration.min = 0.05;
    duration.max = 10.0;
    duration.step = 0.01;
    duration.defaultValue = VectorSlotValue::fromScalar(comp.span() / std::max(1.0, comp.fr));
    preset.params = {duration};
    preset.flags = QJsonObject{{QStringLiteral("unitLocked"), true}, {QStringLiteral("orderLocked"), true},
                               {QStringLiteral("easeLocked"), true}, {QStringLiteral("staggerLocked"), true}};
    preset.report = *warnings;
    return preset;
}

} // namespace drift::lottie
