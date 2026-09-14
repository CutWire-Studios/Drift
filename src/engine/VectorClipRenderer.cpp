#include "VectorClipRenderer.h"

#include "VectorInspect.h"

#ifdef DRIFT_WITH_SKIA
#include "SkiaFonts.h"
#include "SkiaRuntime.h"
#include "SkiaVectorResources.h"

#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QMutex>
#include <QMutexLocker>
#include <QRectF>

#include <list>

#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkMatrix.h"
#include "include/core/SkRect.h"
#include "include/core/SkStream.h"
#include "modules/skottie/include/Skottie.h"
#include "modules/skottie/include/SlotManager.h"
#include "modules/svg/include/SkSVGDOM.h"
#include "modules/svg/include/SkSVGNode.h"
#include "modules/svg/include/SkSVGRenderContext.h"
#include "modules/svg/include/SkSVGSVG.h"
#include "modules/svg/include/SkSVGTypes.h"

#include <optional>
#include <vector>
#endif

namespace drift::vec {

#ifdef DRIFT_WITH_SKIA

namespace {

// One SVG node the overrides can touch, with the presentation attributes it was parsed with so
// a later paint with different overrides can put them back.
struct SvgNode
{
    QString id;
    SkSVGNode *node = nullptr;
    SkSVGProperty<SkSVGPaint, true> fill;
    SkSVGProperty<SkSVGPaint, true> stroke;
    SkSVGProperty<SkSVGLength, true> strokeWidth;
    SkSVGProperty<SkSVGNumberType, false> opacity;
    SkSVGProperty<SkSVGDisplay, false> display;
};

struct Document
{
    VectorKind kind = VectorKind::Lottie;
    sk_sp<skottie::Animation> animation;
    sk_sp<skottie::SlotManager> slotManager;
    sk_sp<SkSVGDOM> svg;
    QSizeF size;             // the document's own size; empty when an SVG declares none
    double durationSec = 0;  // 0 for a still
    quint64 key = 0;         // cache key, folded into painter keys
    QMutex mutex;            // seek + render are one critical section
    // SVG: the root and every element with an id (the document's own and the ones scanSvg
    // minted), and which override set is currently written into the tree.
    std::vector<SvgNode> svgNodes;
    QHash<QString, size_t> svgNodeIndex;
    QByteArray svgAppliedKey;
};

// The svg.* slots of a source in applied form, plus a canonical key for cache lookups.
struct SvgOverrides
{
    struct Element
    {
        std::optional<QColor> fill;
        std::optional<QColor> stroke;
        std::optional<double> strokeWidth;
        std::optional<double> opacity;
        std::optional<bool> visible;
    };
    Element document;
    QMap<QString, Element> elements;
    QByteArray key;

    bool isEmpty() const { return key.isEmpty(); }

    static SvgOverrides fromSource(const VectorSource &source)
    {
        SvgOverrides out;
        QJsonObject json;
        for (auto it = source.slotValues.cbegin(); it != source.slotValues.cend(); ++it) {
            SvgOverrideKey parsed;
            if (!parseSvgOverrideKey(it.key(), &parsed) || it->type != svgOverrideType(parsed.prop))
                continue;
            Element &e = parsed.elementId.isEmpty() ? out.document : out.elements[parsed.elementId];
            if (parsed.prop == QLatin1String("fill"))
                e.fill = it->color;
            else if (parsed.prop == QLatin1String("stroke"))
                e.stroke = it->color;
            else if (parsed.prop == QLatin1String("strokeWidth"))
                e.strokeWidth = qMax(0.0, it->scalar);
            else if (parsed.prop == QLatin1String("opacity"))
                e.opacity = qBound(0.0, it->scalar, 1.0);
            else if (parsed.prop == QLatin1String("visible"))
                e.visible = it->scalar >= 0.5;
            json.insert(it.key(), it->toJson());
        }
        if (!json.isEmpty())
            out.key = QJsonDocument(json).toJson(QJsonDocument::Compact);
        return out;
    }
};

SkSVGProperty<SkSVGPaint, true> svgPaintFor(const QColor &c)
{
    return SkSVGProperty<SkSVGPaint, true>(
        SkSVGPaint(SkSVGColor(SkColorSetARGB(c.alpha(), c.red(), c.green(), c.blue()))));
}

// Writes one element's overrides onto its node. The whole-document colours only replace a paint
// the node already has (a fill="none" outline stays hollow, a stroke is never added), so a
// recoloured icon keeps its structure; an element's own overrides are unconditional.
void applySvgElement(const SvgNode &n, const SvgOverrides::Element &e, bool wholeDocument)
{
    const auto hasPaint = [](const SkSVGProperty<SkSVGPaint, true> &p) {
        return p.isValue() && p->type() != SkSVGPaint::Type::kNone;
    };
    if (e.fill && (!wholeDocument || hasPaint(n.fill)))
        n.node->setFill(svgPaintFor(*e.fill));
    if (e.stroke && (!wholeDocument || hasPaint(n.stroke)))
        n.node->setStroke(svgPaintFor(*e.stroke));
    if (e.strokeWidth && (!wholeDocument || n.strokeWidth.isValue()))
        n.node->setStrokeWidth(SkSVGProperty<SkSVGLength, true>(SkSVGLength(float(*e.strokeWidth))));
    if (e.opacity && !wholeDocument)
        n.node->setOpacity(SkSVGProperty<SkSVGNumberType, false>(float(*e.opacity)));
    if (e.visible && !wholeDocument)
        n.node->setDisplay(SkSVGProperty<SkSVGDisplay, false>(*e.visible ? SkSVGDisplay::kInline : SkSVGDisplay::kNone));
}

// Puts the parsed attributes back on every node, then writes the given overrides. Runs under
// the document mutex: the tree is shared by every clip drawing this document.
void applySvgOverrides(Document &doc, const SvgOverrides &overrides)
{
    if (doc.svgAppliedKey == overrides.key)
        return;
    for (const SvgNode &n : doc.svgNodes) {
        n.node->setFill(n.fill);
        n.node->setStroke(n.stroke);
        n.node->setStrokeWidth(n.strokeWidth);
        n.node->setOpacity(n.opacity);
        n.node->setDisplay(n.display);
    }
    if (!overrides.isEmpty()) {
        const SvgOverrides::Element &d = overrides.document;
        // An unset root fill is the default black every unstyled shape inherits; the root is
        // also where a document-wide opacity and stroke width land.
        SvgNode &root = doc.svgNodes.front();
        if (d.fill && !root.fill.isValue())
            root.node->setFill(svgPaintFor(*d.fill));
        if (d.strokeWidth && !root.strokeWidth.isValue())
            root.node->setStrokeWidth(SkSVGProperty<SkSVGLength, true>(SkSVGLength(float(*d.strokeWidth))));
        if (d.opacity)
            root.node->setOpacity(SkSVGProperty<SkSVGNumberType, false>(float(*d.opacity)));
        for (const SvgNode &n : doc.svgNodes)
            applySvgElement(n, d, true);
        for (auto it = overrides.elements.cbegin(); it != overrides.elements.cend(); ++it) {
            const auto index = doc.svgNodeIndex.constFind(it.key());
            if (index != doc.svgNodeIndex.constEnd())
                applySvgElement(doc.svgNodes[*index], it.value(), false);
        }
    }
    doc.svgAppliedKey = overrides.key;
}

// Parsed documents by hash + slot overrides. Small: each holds a whole scene graph, and a
// timeline rarely has more than a handful of distinct animations in play.
class DocumentCache
{
public:
    std::shared_ptr<Document> get(const VectorSource &source)
    {
        const QString key = cacheKey(source);
        QMutexLocker lock(&m_mutex);
        auto it = m_index.find(key);
        if (it != m_index.end()) {
            m_lru.splice(m_lru.begin(), m_lru, it.value());
            return m_lru.front().second;
        }
        lock.unlock();

        std::shared_ptr<Document> doc = build(source);
        if (doc)
            doc->key = qHash(key) | 1;

        lock.relock();
        // Another thread may have built the same document meanwhile; theirs wins so both
        // painters share one mutex.
        it = m_index.find(key);
        if (it != m_index.end()) {
            m_lru.splice(m_lru.begin(), m_lru, it.value());
            return m_lru.front().second;
        }
        m_lru.emplace_front(key, doc);
        m_index.insert(key, m_lru.begin());
        while (m_lru.size() > kMaxEntries) {
            m_index.remove(m_lru.back().first);
            m_lru.pop_back();
        }
        return doc;
    }

    void clear()
    {
        QMutexLocker lock(&m_mutex);
        m_lru.clear();
        m_index.clear();
    }

private:
    static constexpr size_t kMaxEntries = 8;

    // Lottie slots are baked into the parsed animation, so they are part of the key; SVG
    // overrides are written onto the shared tree at paint time and are not.
    static QString cacheKey(const VectorSource &source)
    {
        QString hash = source.hash;
        if (hash.isEmpty())
            hash = vectorSourceHash(vectorSourceBytes(source));
        QString key = hash;
        if (source.kind != VectorKind::Svg && !source.slotValues.isEmpty()) {
            QJsonObject slotJson;
            for (auto it = source.slotValues.cbegin(); it != source.slotValues.cend(); ++it)
                slotJson.insert(it.key(), it.value().toJson());
            key += QLatin1Char('|') + QString::fromUtf8(QJsonDocument(slotJson).toJson(QJsonDocument::Compact));
        }
        return key;
    }

    static void applySlots(skottie::SlotManager &manager, const VectorSource &source)
    {
        for (auto it = source.slotValues.cbegin(); it != source.slotValues.cend(); ++it) {
            const SkString id(it.key().toUtf8().constData());
            const VectorSlotValue &v = it.value();
            switch (v.type) {
            case VectorSlotValue::Type::Color:
                manager.setColorSlot(id, SkColorSetARGB(v.color.alpha(), v.color.red(), v.color.green(), v.color.blue()));
                break;
            case VectorSlotValue::Type::Scalar:
                manager.setScalarSlot(id, float(v.scalar));
                break;
            case VectorSlotValue::Type::Vec2:
                manager.setVec2Slot(id, SkV2{float(v.vec2.x()), float(v.vec2.y())});
                break;
            case VectorSlotValue::Type::Text:
                if (std::optional<skottie::TextPropertyValue> text = manager.getTextSlot(id)) {
                    text->fText = SkString(v.text.toUtf8().constData());
                    manager.setTextSlot(id, *text);
                }
                break;
            case VectorSlotValue::Type::Image:
                if (sk_sp<skresources::ImageAsset> asset = drift::skia::imageAssetFromFile(v.image))
                    manager.setImageSlot(id, asset);
                break;
            }
        }
    }

    static std::shared_ptr<Document> build(const VectorSource &source)
    {
        const QByteArray data = vectorSourceBytes(source);
        if (data.isEmpty())
            return nullptr;
        const QString baseDir = source.isInline() ? QString() : QFileInfo(source.path).absolutePath();
        auto doc = std::make_shared<Document>();
        doc->kind = source.kind;

        if (source.kind == VectorKind::Svg) {
            const SvgScan scan = scanSvg(data);
            SkMemoryStream stream(scan.rewritten.constData(), size_t(scan.rewritten.size()), false);
            doc->svg = SkSVGDOM::Builder()
                           .setFontManager(drift::skia::systemFontMgr())
                           .setResourceProvider(drift::skia::makeVectorResourceProvider(baseDir))
                           .make(stream);
            if (!doc->svg || !doc->svg->getRoot())
                return nullptr;
            SkSVGSVG *root = doc->svg->getRoot();
            const auto remember = [&](const QString &id, SkSVGNode *node) {
                SvgNode n;
                n.id = id;
                n.node = node;
                n.fill = node->getFill();
                n.stroke = node->getStroke();
                n.strokeWidth = node->getStrokeWidth();
                n.opacity = node->getOpacity();
                n.display = node->getDisplay();
                doc->svgNodeIndex.insert(id, doc->svgNodes.size());
                doc->svgNodes.push_back(n);
            };
            remember(QString(), root);
            QStringList ids = scan.injectedIds;
            for (const VectorSvgElement &element : scan.elements)
                ids.append(element.id);
            for (const QString &id : ids) {
                if (sk_sp<SkSVGNode> *node = doc->svg->findNodeById(id.toUtf8().constData()); node && *node
                    && node->get() != root)
                    remember(id, node->get());
            }
            const SkSize intrinsic = root->intrinsicSize(SkSVGLengthContext(SkSize::Make(0, 0)));
            if (intrinsic.width() > 0 && intrinsic.height() > 0)
                doc->size = QSizeF(intrinsic.width(), intrinsic.height());
            else if (root->getViewBox().has_value())
                doc->size = QSizeF(root->getViewBox()->width(), root->getViewBox()->height());
            return doc;
        }

        skottie::Animation::Builder builder(skottie::Animation::Builder::kPreferEmbeddedFonts);
        builder.setFontManager(drift::skia::systemFontMgr())
            .setResourceProvider(drift::skia::makeVectorResourceProvider(baseDir));
        doc->animation = builder.make(data.constData(), size_t(data.size()));
        if (!doc->animation)
            return nullptr;
        doc->slotManager = builder.getSlotManager();
        if (doc->slotManager)
            applySlots(*doc->slotManager, source);
        doc->size = QSizeF(doc->animation->size().width(), doc->animation->size().height());
        doc->durationSec = doc->animation->duration();
        return doc;
    }

    QMutex m_mutex;
    std::list<std::pair<QString, std::shared_ptr<Document>>> m_lru;
    QHash<QString, std::list<std::pair<QString, std::shared_ptr<Document>>>::iterator> m_index;
};

DocumentCache &documentCache()
{
    static DocumentCache cache;
    return cache;
}

QRectF fitRect(const QSizeF &doc, const QSize &layer, VectorFit fit)
{
    const QRectF full(0, 0, layer.width(), layer.height());
    if (fit == VectorFit::Stretch || doc.isEmpty())
        return full;
    const double sx = full.width() / doc.width();
    const double sy = full.height() / doc.height();
    const double s = fit == VectorFit::Cover ? qMax(sx, sy) : qMin(sx, sy);
    QRectF rect(0, 0, doc.width() * s, doc.height() * s);
    rect.moveCenter(full.center());
    return rect;
}

class DocumentPainter final : public skia::VectorPainter
{
public:
    DocumentPainter(std::shared_ptr<Document> doc, double timeSec, const QSize &size, VectorFit fit,
                    SvgOverrides overrides, bool keyframed)
        : m_doc(std::move(doc)), m_timeSec(timeSec), m_size(size), m_fit(fit), m_overrides(std::move(overrides)),
          m_keyframed(keyframed)
    {
    }

    QSize size() const override { return m_size; }

    // Animations and keyframed overrides redraw every frame; a still is drawn once per size and
    // override set and served from the GPU cache.
    quint64 cacheKey() const override
    {
        if (m_doc->animation || m_keyframed)
            return 0;
        return qHashMulti(m_doc->key, m_size.width(), m_size.height(), int(m_fit), qHash(m_overrides.key)) | 1;
    }

    void paint(SkCanvas &canvas) const override
    {
        const QRectF dst = fitRect(m_doc->size, m_size, m_fit);
        QMutexLocker lock(&m_doc->mutex);
        canvas.save();
        if (m_doc->animation) {
            // render(dst) would letterbox uniformly; fitRect already chose the aspect, and
            // Stretch needs the non-uniform map.
            const SkRect rect = SkRect::MakeXYWH(float(dst.x()), float(dst.y()), float(dst.width()), float(dst.height()));
            canvas.concat(SkMatrix::RectToRect(SkRect::MakeSize(m_doc->animation->size()), rect));
            m_doc->animation->seekFrameTime(m_timeSec);
            m_doc->animation->render(&canvas);
            canvas.restore();
            return;
        }
        canvas.translate(float(dst.x()), float(dst.y()));
        applySvgOverrides(*m_doc, m_overrides);
        m_doc->svg->setContainerSize(SkSize::Make(float(dst.width()), float(dst.height())));
        m_doc->svg->render(&canvas);
        canvas.restore();
    }

private:
    std::shared_ptr<Document> m_doc;
    double m_timeSec;
    QSize m_size;
    VectorFit m_fit;
    SvgOverrides m_overrides;
    bool m_keyframed;
};

} // namespace

std::shared_ptr<const skia::VectorPainter> makePainter(const RenderRequest &request)
{
    if (request.size.isEmpty() || request.source.isEmpty())
        return nullptr;
    std::shared_ptr<Document> doc = documentCache().get(request.source);
    if (!doc)
        return nullptr;
    TimeUs folded = 0;
    if (!foldVectorTime(request.animUs + request.source.startOffsetUs, secondsToUs(doc->durationSec),
                        request.source.loop, &folded))
        return nullptr;
    SvgOverrides overrides;
    if (request.source.kind == VectorKind::Svg)
        overrides = SvgOverrides::fromSource(request.source);
    return std::make_shared<DocumentPainter>(std::move(doc), usToSeconds(folded), request.size,
                                             request.source.fit, std::move(overrides), request.keyframed);
}

QImage renderToImage(const RenderRequest &request)
{
    const std::shared_ptr<const skia::VectorPainter> painter = makePainter(request);
    return painter ? skia::SkiaRuntime::rasterize(*painter) : QImage();
}

QImage renderThumbnail(const VectorSource &source, const QSize &size, double atFraction)
{
    RenderRequest request;
    request.source = source;
    request.source.loop = VectorLoop::Hold;
    request.source.startOffsetUs = 0;
    request.size = size;
    if (std::shared_ptr<Document> doc = documentCache().get(source))
        request.animUs = static_cast<TimeUs>(secondsToUs(doc->durationSec) * qBound(0.0, atFraction, 1.0));
    return renderToImage(request);
}

void clearVectorDocumentCache()
{
    documentCache().clear();
}

#else // !DRIFT_WITH_SKIA

std::shared_ptr<const skia::VectorPainter> makePainter(const RenderRequest &)
{
    return nullptr;
}

QImage renderToImage(const RenderRequest &)
{
    return {};
}

QImage renderThumbnail(const VectorSource &, const QSize &, double)
{
    return {};
}

void clearVectorDocumentCache() {}

#endif

} // namespace drift::vec
