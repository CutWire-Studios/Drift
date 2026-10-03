#include "VectorInspect.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include <algorithm>

#ifdef DRIFT_WITH_SKIA
#include "SkiaFonts.h"
#include "SkiaVectorObservers.h"
#include "SkiaVectorResources.h"

#include <QFileInfo>

#include "include/core/SkStream.h"
#include "modules/skottie/include/Skottie.h"
#include "modules/skottie/include/SlotManager.h"
#include "modules/svg/include/SkSVGDOM.h"
#include "modules/svg/include/SkSVGRenderContext.h"
#include "modules/svg/include/SkSVGSVG.h"
#endif

namespace drift::vec {

namespace {

QJsonArray toJsonArray(const QStringList &list)
{
    QJsonArray a;
    for (const QString &s : list)
        a.append(s);
    return a;
}

// Lottie expressions live in an "x" string on an animatable property ({"a":0,"k":...,"x":"..."}).
// Skottie has no evaluator, so every one of these renders as its static "k" value. The path
// names the property through the layer / shape "nm" names an author would recognise.
void collectExpressions(const QJsonValue &value, const QString &path, QStringList *out)
{
    if (value.isArray()) {
        const QJsonArray array = value.toArray();
        for (int i = 0; i < array.size(); ++i) {
            const QJsonValue item = array.at(i);
            QString name = QString::number(i);
            if (item.isObject()) {
                const QString nm = item.toObject().value(QStringLiteral("nm")).toString();
                if (!nm.isEmpty())
                    name = nm;
            }
            collectExpressions(item, path + QLatin1Char('/') + name, out);
        }
        return;
    }
    if (!value.isObject())
        return;
    const QJsonObject object = value.toObject();
    if (object.contains(QStringLiteral("k")) && object.value(QStringLiteral("x")).isString()
        && !object.value(QStringLiteral("x")).toString().trimmed().isEmpty()) {
        out->append(path);
    }
    for (auto it = object.constBegin(); it != object.constEnd(); ++it)
        collectExpressions(it.value(), path + QLatin1Char('/') + it.key(), out);
}

#ifdef DRIFT_WITH_SKIA

void inspectLottie(const QByteArray &data, InspectReport *report)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (!doc.isObject()) {
        report->error = parseError.error == QJsonParseError::NoError
                            ? QStringLiteral("not a Lottie document")
                            : QStringLiteral("invalid JSON: ") + parseError.errorString();
        return;
    }
    const QJsonObject root = doc.object();
    if (!root.contains(QStringLiteral("layers")) || !root.contains(QStringLiteral("op"))) {
        report->error = QStringLiteral("not a Lottie document (no layers/op)");
        return;
    }

    const drift::skia::InspectObservers observers;
    skottie::Animation::Builder builder(skottie::Animation::Builder::kPreferEmbeddedFonts);
    observers.attach(builder);
    builder.setFontManager(drift::skia::systemFontMgr())
        .setResourceProvider(drift::skia::makeVectorResourceProvider(QString()));
    const sk_sp<skottie::Animation> animation = builder.make(data.constData(), size_t(data.size()));
    if (!animation) {
        report->error = QStringLiteral("Skottie could not parse the animation");
        report->unsupported = observers.loggedLines();
        return;
    }

    report->ok = true;
    report->version = QString::fromUtf8(animation->version().c_str());
    report->title = root.value(QStringLiteral("nm")).toString();
    report->fps = animation->fps();
    report->durationUs = secondsToUs(animation->duration());
    report->width = qRound(animation->size().width());
    report->height = qRound(animation->size().height());
    for (const skottie::LayerInfo &layer : builder.getLayerInfo()) {
        report->layers.append({QString::fromUtf8(layer.fName.c_str()), layer.fSize.width(),
                               layer.fSize.height(), layer.fInPoint / animation->fps(),
                               layer.fOutPoint / animation->fps()});
    }
    if (const sk_sp<skottie::SlotManager> &manager = builder.getSlotManager()) {
        const skottie::SlotManager::SlotInfo info = manager->getSlotInfo();
        for (const SkString &id : info.fColorSlotIDs)
            report->slotInfos.append({QString::fromUtf8(id.c_str()), VectorSlotValue::Type::Color});
        for (const SkString &id : info.fScalarSlotIDs)
            report->slotInfos.append({QString::fromUtf8(id.c_str()), VectorSlotValue::Type::Scalar});
        for (const SkString &id : info.fVec2SlotIDs)
            report->slotInfos.append({QString::fromUtf8(id.c_str()), VectorSlotValue::Type::Vec2});
        for (const SkString &id : info.fTextSlotIDs)
            report->slotInfos.append({QString::fromUtf8(id.c_str()), VectorSlotValue::Type::Text});
        for (const SkString &id : info.fImageSlotIDs)
            report->slotInfos.append({QString::fromUtf8(id.c_str()), VectorSlotValue::Type::Image});
    }
    report->namedProperties = observers.namedProperties();
    // Skottie hands markers over as fractions of the duration.
    report->markers = observers.markers();
    for (VectorMarker &m : report->markers) {
        m.t0 *= animation->duration();
        m.t1 *= animation->duration();
    }
    report->unsupported = observers.loggedLines();

    const QJsonArray fonts = root.value(QStringLiteral("fonts")).toObject().value(QStringLiteral("list")).toArray();
    for (const QJsonValue &font : fonts) {
        const QString family = font.toObject().value(QStringLiteral("fFamily")).toString();
        if (!family.isEmpty() && !report->fonts.contains(family))
            report->fonts.append(family);
    }
    collectExpressions(root.value(QStringLiteral("layers")), QStringLiteral("layers"), &report->expressions);
    collectExpressions(root.value(QStringLiteral("assets")), QStringLiteral("assets"), &report->expressions);

    if (!report->expressions.isEmpty())
        report->hints.append(QStringLiteral("Expressions are not evaluated; they render as their static value. Bake them to keyframes before export."));
    if (!report->fonts.isEmpty() && !root.contains(QStringLiteral("chars")))
        report->hints.append(QStringLiteral("Text layers use system fonts; families that are not installed fall back. Export with glyphs embedded (chars) for a portable result."));
    if (!report->slotInfos.isEmpty())
        report->hints.append(QStringLiteral("Slots can be overridden per clip with set_lottie_slot."));
    if (!report->unsupported.isEmpty())
        report->hints.append(QStringLiteral("Some features were skipped by the renderer; see unsupported."));
}

void inspectSvg(const QByteArray &data, InspectReport *report)
{
    const SvgScan scan = scanSvg(data);
    SkMemoryStream stream(scan.rewritten.constData(), size_t(scan.rewritten.size()), false);
    const sk_sp<SkSVGDOM> dom = SkSVGDOM::Builder()
                                    .setFontManager(drift::skia::systemFontMgr())
                                    .setResourceProvider(drift::skia::makeVectorResourceProvider(QString()))
                                    .make(stream);
    if (!dom || !dom->getRoot()) {
        report->error = QStringLiteral("not a parsable SVG document");
        return;
    }
    report->ok = true;
    const SkSVGSVG *root = dom->getRoot();
    // Intrinsic size when the root gives absolute width/height; otherwise the viewBox stands in.
    const SkSize intrinsic = root->intrinsicSize(SkSVGLengthContext(SkSize::Make(0, 0)));
    if (intrinsic.width() > 0 && intrinsic.height() > 0) {
        report->width = qRound(intrinsic.width());
        report->height = qRound(intrinsic.height());
    } else if (root->getViewBox().has_value()) {
        report->width = qRound(root->getViewBox()->width());
        report->height = qRound(root->getViewBox()->height());
    }

    static const QRegularExpression smil(QStringLiteral("<(animate|animateTransform|animateMotion|animateColor|set)\\b"));
    static const QRegularExpression script(QStringLiteral("<script\\b"));
    static const QRegularExpression fontFamily(QStringLiteral("font-family\\s*[:=]\\s*[\"']?([^;\"'>]+)"));
    const QString text = QString::fromUtf8(data);
    QSet<QString> tags;
    for (auto it = smil.globalMatch(text); it.hasNext();)
        tags.insert(it.next().captured(1));
    for (const QString &tag : tags)
        report->unsupported.append(QStringLiteral("SMIL <%1> is not animated").arg(tag));
    if (script.match(text).hasMatch())
        report->unsupported.append(QStringLiteral("<script> is not executed"));
    for (auto it = fontFamily.globalMatch(text); it.hasNext();) {
        const QString family = it.next().captured(1).trimmed();
        if (!family.isEmpty() && !report->fonts.contains(family))
            report->fonts.append(family);
    }
    if (!tags.isEmpty())
        report->hints.append(QStringLiteral("SVG renders as a still; use a Lottie document for motion."));
    if (report->width <= 0 || report->height <= 0)
        report->hints.append(QStringLiteral("No intrinsic size or viewBox; the drawing stretches to the clip."));

    // Only ids the parsed DOM can find are reported: Skia drops <symbol>, <style> and the like
    // with their subtrees.
    for (const VectorSvgElement &element : scan.elements) {
        if (dom->findNodeById(element.id.toUtf8().constData()))
            report->svgElements.append(element);
    }
    if (!report->svgElements.isEmpty())
        report->hints.append(QStringLiteral("Elements with ids can be restyled per clip through the svg.<id>.<fill|stroke|strokeWidth|opacity|visible> slots; svg.fill / svg.stroke / svg.strokeWidth / svg.opacity restyle the whole drawing."));
}

#endif // DRIFT_WITH_SKIA

} // namespace

namespace {

bool paintableSvgTag(QStringView tag)
{
    static const QStringList tags{QStringLiteral("path"), QStringLiteral("rect"), QStringLiteral("circle"),
                                  QStringLiteral("ellipse"), QStringLiteral("line"), QStringLiteral("polyline"),
                                  QStringLiteral("polygon"), QStringLiteral("text"), QStringLiteral("g"),
                                  QStringLiteral("use")};
    return tags.contains(tag.toString());
}

// A presentation attribute as written, from the attribute or the style="" declaration list.
QString svgPresentation(const QXmlStreamAttributes &attrs, const QString &name)
{
    if (attrs.hasAttribute(name))
        return attrs.value(name).toString().trimmed();
    const QString style = attrs.value(QStringLiteral("style")).toString();
    for (const QString &decl : style.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
        const int colon = decl.indexOf(QLatin1Char(':'));
        if (colon > 0 && decl.left(colon).trimmed() == name)
            return decl.mid(colon + 1).trimmed();
    }
    return {};
}

// One rule of an SVG's <style> sheet, narrowed to a compound selector without combinators or
// pseudo-classes (tag, .a, #b, tag.a.b). Anything else is dropped rather than misapplied.
struct CssRule
{
    QString tag; // empty matches any
    QString id;
    QStringList classes;
    int specificity = 0;
    QString declarations;
};

bool parseCssSelector(const QString &text, CssRule *rule)
{
    static const QRegularExpression simple(QStringLiteral("^([A-Za-z][\\w-]*|\\*)?((?:[.#][\\w-]+)*)$"));
    static const QRegularExpression part(QStringLiteral("([.#])([\\w-]+)"));
    const QRegularExpressionMatch match = simple.match(text);
    if (text.isEmpty() || !match.hasMatch())
        return false;
    if (match.captured(1) != QLatin1String("*"))
        rule->tag = match.captured(1);
    for (auto it = part.globalMatch(match.captured(2)); it.hasNext();) {
        const QRegularExpressionMatch p = it.next();
        if (p.captured(1) == QLatin1String("#")) {
            if (!rule->id.isEmpty())
                return false;
            rule->id = p.captured(2);
        } else {
            rule->classes.append(p.captured(2));
        }
    }
    rule->specificity = (rule->id.isEmpty() ? 0 : 10000) + int(rule->classes.size()) * 100 + (rule->tag.isEmpty() ? 0 : 1);
    return true;
}

// Least specific first, source order among equals, so later declarations win as in CSS.
QList<CssRule> parseCss(QString css)
{
    static const QRegularExpression comments(QStringLiteral("/\\*.*?\\*/"), QRegularExpression::DotMatchesEverythingOption);
    css.remove(comments);
    QList<CssRule> rules;
    qsizetype pos = 0;
    while (true) {
        const qsizetype open = css.indexOf(QLatin1Char('{'), pos);
        if (open < 0)
            break;
        const QString prelude = css.mid(pos, open - pos).trimmed();
        qsizetype close = open + 1;
        for (int depth = 1; close < css.size() && depth > 0; ++close) {
            if (css.at(close) == QLatin1Char('{'))
                ++depth;
            else if (css.at(close) == QLatin1Char('}'))
                --depth;
        }
        QString declarations = css.mid(open + 1, close - open - 2);
        pos = close;
        // @media, @font-face, @keyframes: nothing in them maps onto a static attribute.
        if (prelude.startsWith(QLatin1Char('@')))
            continue;
        declarations.remove(QStringLiteral("!important"));
        QStringList cleaned;
        for (const QString &declaration : declarations.split(QLatin1Char(';'))) {
            if (!declaration.trimmed().isEmpty())
                cleaned.append(declaration.trimmed());
        }
        for (const QString &selector : prelude.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
            CssRule rule;
            if (!parseCssSelector(selector.trimmed(), &rule))
                continue;
            rule.declarations = cleaned.join(QLatin1Char(';'));
            rules.append(rule);
        }
    }
    std::stable_sort(rules.begin(), rules.end(),
                     [](const CssRule &a, const CssRule &b) { return a.specificity < b.specificity; });
    return rules;
}

QString localName(QStringView qualified)
{
    return qualified.mid(qualified.indexOf(QLatin1Char(':')) + 1).toString();
}

// Skia's SVG parser stops at any <!ENTITY> declaration (its guard against entity expansion),
// which is how Illustrator's "SVG 1.1" export opens, and it ignores <style> sheets, which is how
// Illustrator writes fills and strokes by default; such files failed to import or drew all black.
// They are rewritten with the entities expanded (QXmlStreamReader caps the expansion), the DTD
// dropped and the CSS rules folded into each element's style="". Other documents pass as is.
QByteArray skiaReadySvg(const QByteArray &data)
{
    static const QRegularExpression needsRewrite(QStringLiteral("<!DOCTYPE[^>\\[]*\\[|<style\\b"));
    const QString text = QString::fromUtf8(data);
    if (!needsRewrite.match(text).hasMatch())
        return data;

    QString css;
    {
        QXmlStreamReader reader(text);
        reader.setNamespaceProcessing(false);
        int styleDepth = 0;
        while (!reader.atEnd()) {
            const QXmlStreamReader::TokenType token = reader.readNext();
            if (token == QXmlStreamReader::StartElement && (styleDepth > 0 || localName(reader.qualifiedName()) == QLatin1String("style")))
                ++styleDepth;
            else if (token == QXmlStreamReader::EndElement && styleDepth > 0)
                --styleDepth;
            else if (token == QXmlStreamReader::Characters && styleDepth > 0)
                css += reader.text();
        }
        if (reader.hasError())
            return data;
    }
    const QList<CssRule> rules = parseCss(css);

    QByteArray out;
    QXmlStreamWriter writer(&out);
    writer.writeStartDocument();
    QXmlStreamReader reader(text);
    reader.setNamespaceProcessing(false);
    int styleDepth = 0;
    while (!reader.atEnd()) {
        const QXmlStreamReader::TokenType token = reader.readNext();
        if (token == QXmlStreamReader::StartElement) {
            const QString tag = localName(reader.qualifiedName());
            if (styleDepth > 0 || tag == QLatin1String("style")) {
                ++styleDepth;
                continue;
            }
            const QXmlStreamAttributes attrs = reader.attributes();
            const QString id = attrs.value(QStringLiteral("id")).toString();
            const QStringList classes = attrs.value(QStringLiteral("class")).toString().split(QLatin1Char(' '), Qt::SkipEmptyParts);
            QString style;
            for (const CssRule &rule : rules) {
                if ((!rule.tag.isEmpty() && rule.tag != tag) || (!rule.id.isEmpty() && rule.id != id))
                    continue;
                if (!std::all_of(rule.classes.begin(), rule.classes.end(),
                                 [&](const QString &c) { return classes.contains(c); }))
                    continue;
                style += rule.declarations + QLatin1Char(';');
            }
            // Written last, after the presentation attributes, so it overrides them as CSS does;
            // the element's own style="" goes after the sheet's for the same reason.
            style += attrs.value(QStringLiteral("style"));
            writer.writeStartElement(reader.qualifiedName().toString());
            for (const QXmlStreamAttribute &attr : attrs) {
                if (attr.qualifiedName() != QLatin1String("style"))
                    writer.writeAttribute(attr.qualifiedName().toString(), attr.value().toString());
            }
            if (!style.isEmpty())
                writer.writeAttribute(QStringLiteral("style"), style);
        } else if (token == QXmlStreamReader::EndElement) {
            if (styleDepth > 0)
                --styleDepth;
            else
                writer.writeEndElement();
        } else if (token == QXmlStreamReader::Characters && styleDepth == 0) {
            writer.writeCharacters(reader.text().toString());
        }
    }
    if (reader.hasError())
        return data;
    writer.writeEndDocument();
    return out;
}

} // namespace

SvgScan scanSvg(const QByteArray &data)
{
    SvgScan scan;
    const QByteArray ready = skiaReadySvg(data);
    const QString text = QString::fromUtf8(ready);

    QSet<QString> taken;
    {
        QXmlStreamReader probe(text);
        while (!probe.atEnd()) {
            if (probe.readNext() == QXmlStreamReader::StartElement && probe.attributes().hasAttribute(QStringLiteral("id")))
                taken.insert(probe.attributes().value(QStringLiteral("id")).toString());
        }
    }

    // Insertion points: the closing bracket of each id-less paintable start tag, found from the
    // reader's offset after the tag so attribute values holding '>' cannot mislead.
    QList<QPair<int, QString>> insertions;
    int minted = 0;
    int defsDepth = 0;
    QXmlStreamReader reader(text);
    while (!reader.atEnd()) {
        const QXmlStreamReader::TokenType token = reader.readNext();
        if (token == QXmlStreamReader::EndElement) {
            if (reader.name() == QLatin1String("defs"))
                --defsDepth;
            continue;
        }
        if (token != QXmlStreamReader::StartElement)
            continue;
        const QString tag = reader.name().toString();
        if (tag == QLatin1String("defs"))
            ++defsDepth;
        const QXmlStreamAttributes attrs = reader.attributes();
        const QString id = attrs.value(QStringLiteral("id")).toString();
        if (!id.isEmpty()) {
            VectorSvgElement element;
            element.id = id;
            element.tag = tag;
            element.classes = attrs.value(QStringLiteral("class")).toString().split(QLatin1Char(' '), Qt::SkipEmptyParts);
            element.inDefs = defsDepth > 0;
            element.fill = svgPresentation(attrs, QStringLiteral("fill"));
            element.stroke = svgPresentation(attrs, QStringLiteral("stroke"));
            element.strokeWidth = svgPresentation(attrs, QStringLiteral("stroke-width"));
            element.opacity = svgPresentation(attrs, QStringLiteral("opacity"));
            scan.elements.append(element);
            continue;
        }
        if (!paintableSvgTag(tag))
            continue;
        QString fresh;
        do {
            fresh = QStringLiteral("drift-%1").arg(++minted);
        } while (taken.contains(fresh));
        int close = int(reader.characterOffset()) - 1;
        while (close > 0 && text.at(close) != QLatin1Char('>'))
            --close;
        if (close > 0 && text.at(close - 1) == QLatin1Char('/'))
            --close;
        insertions.append({close, fresh});
        scan.injectedIds.append(fresh);
    }
    if (reader.hasError()) {
        scan.rewritten = ready;
        scan.injectedIds.clear();
        return scan;
    }
    QString rewritten = text;
    for (int i = insertions.size() - 1; i >= 0; --i)
        rewritten.insert(insertions.at(i).first, QStringLiteral(" id=\"%1\"").arg(insertions.at(i).second));
    scan.rewritten = rewritten.toUtf8();
    return scan;
}

QJsonObject InspectReport::toJson() const
{
    QJsonObject o{
        {QStringLiteral("ok"), ok},
        {QStringLiteral("kind"), vectorKindToString(kind)},
    };
    if (!ok) {
        o.insert(QStringLiteral("error"), error);
        o.insert(QStringLiteral("unsupported"), toJsonArray(unsupported));
        return o;
    }
    QJsonArray layerArray;
    for (const VectorLayerInfo &l : layers) {
        layerArray.append(QJsonObject{{QStringLiteral("name"), l.name},
                                      {QStringLiteral("width"), l.width},
                                      {QStringLiteral("height"), l.height},
                                      {QStringLiteral("inSec"), l.inSec},
                                      {QStringLiteral("outSec"), l.outSec}});
    }
    QJsonArray slotArray;
    for (const VectorSlotInfo &s : slotInfos) {
        slotArray.append(QJsonObject{{QStringLiteral("id"), s.id},
                                     {QStringLiteral("type"), vectorSlotTypeToString(s.type)}});
    }
    QJsonArray propArray;
    for (const VectorNamedProperty &p : namedProperties)
        propArray.append(QJsonObject{{QStringLiteral("node"), p.node}, {QStringLiteral("type"), p.type}});
    QJsonArray markerArray;
    for (const VectorMarker &m : markers) {
        markerArray.append(QJsonObject{{QStringLiteral("name"), m.name},
                                       {QStringLiteral("t0"), m.t0},
                                       {QStringLiteral("t1"), m.t1}});
    }
    o.insert(QStringLiteral("version"), version);
    o.insert(QStringLiteral("title"), title);
    o.insert(QStringLiteral("fps"), fps);
    o.insert(QStringLiteral("durationSec"), usToSeconds(durationUs));
    o.insert(QStringLiteral("width"), width);
    o.insert(QStringLiteral("height"), height);
    o.insert(QStringLiteral("layers"), layerArray);
    o.insert(QStringLiteral("slots"), slotArray);
    o.insert(QStringLiteral("namedProperties"), propArray);
    o.insert(QStringLiteral("fonts"), toJsonArray(fonts));
    o.insert(QStringLiteral("markers"), markerArray);
    o.insert(QStringLiteral("expressions"), toJsonArray(expressions));
    o.insert(QStringLiteral("unsupported"), toJsonArray(unsupported));
    o.insert(QStringLiteral("hints"), toJsonArray(hints));
    if (kind == VectorKind::Svg) {
        QJsonArray elementArray;
        for (const VectorSvgElement &e : svgElements) {
            elementArray.append(QJsonObject{{QStringLiteral("id"), e.id},
                                            {QStringLiteral("tag"), e.tag},
                                            {QStringLiteral("classes"), toJsonArray(e.classes)},
                                            {QStringLiteral("inDefs"), e.inDefs},
                                            {QStringLiteral("fill"), e.fill},
                                            {QStringLiteral("stroke"), e.stroke},
                                            {QStringLiteral("strokeWidth"), e.strokeWidth},
                                            {QStringLiteral("opacity"), e.opacity}});
        }
        o.insert(QStringLiteral("elements"), elementArray);
    }
    return o;
}

VectorKind detectVectorKind(const QByteArray &data)
{
    const QByteArray head = data.left(512).trimmed();
    if (head.startsWith('<'))
        return VectorKind::Svg;
    // A UTF-8 BOM in front of an SVG is common enough to check for.
    if (head.startsWith("\xEF\xBB\xBF<"))
        return VectorKind::Svg;
    return VectorKind::Lottie;
}

InspectReport inspectVector(const QByteArray &data, VectorKind kind)
{
    InspectReport report;
    report.kind = kind;
    if (data.trimmed().isEmpty()) {
        report.error = QStringLiteral("empty document");
        return report;
    }
#ifdef DRIFT_WITH_SKIA
    if (kind == VectorKind::Svg)
        inspectSvg(data, &report);
    else
        inspectLottie(data, &report);
#else
    report.error = QStringLiteral("this build has no vector renderer (DRIFT_WITH_SKIA is off)");
#endif
    return report;
}

QByteArray vectorSourceBytes(const VectorSource &source)
{
    if (source.isInline())
        return source.source.toUtf8();
    if (source.path.isEmpty())
        return {};
    QFile file(source.path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

bool probeVectorSource(VectorSource &source, QString *error)
{
    const QByteArray data = vectorSourceBytes(source);
    if (data.isEmpty()) {
        if (error)
            *error = source.isInline() ? QStringLiteral("empty document")
                                       : QStringLiteral("cannot read ") + source.path;
        return false;
    }
    const InspectReport report = inspectVector(data, source.kind);
    if (!report.ok) {
        if (error)
            *error = report.error;
        return false;
    }
    source.hash = vectorSourceHash(data);
    source.width = report.width;
    source.height = report.height;
    source.fps = report.fps;
    source.durationUs = report.durationUs;
    if (source.title.isEmpty())
        source.title = report.title;
    return true;
}

} // namespace drift::vec
