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
#include "modules/svg/include/SkSVGRenderContext.h"
#include "modules/svg/include/SkSVGSVG.h"
#endif

namespace drift::vec {

#ifdef DRIFT_WITH_SKIA

namespace {

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
};

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

    static QString cacheKey(const VectorSource &source)
    {
        QString hash = source.hash;
        if (hash.isEmpty())
            hash = vectorSourceHash(vectorSourceBytes(source));
        QString key = hash;
        if (!source.slotValues.isEmpty()) {
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
            SkMemoryStream stream(data.constData(), size_t(data.size()), false);
            doc->svg = SkSVGDOM::Builder()
                           .setFontManager(drift::skia::systemFontMgr())
                           .setResourceProvider(drift::skia::makeVectorResourceProvider(baseDir))
                           .make(stream);
            if (!doc->svg || !doc->svg->getRoot())
                return nullptr;
            const SkSVGSVG *root = doc->svg->getRoot();
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
    DocumentPainter(std::shared_ptr<Document> doc, double timeSec, const QSize &size, VectorFit fit)
        : m_doc(std::move(doc)), m_timeSec(timeSec), m_size(size), m_fit(fit)
    {
    }

    QSize size() const override { return m_size; }

    // Animations redraw every frame; a still is drawn once per size and served from the GPU cache.
    quint64 cacheKey() const override
    {
        if (m_doc->animation)
            return 0;
        return qHashMulti(m_doc->key, m_size.width(), m_size.height(), int(m_fit)) | 1;
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
        m_doc->svg->setContainerSize(SkSize::Make(float(dst.width()), float(dst.height())));
        m_doc->svg->render(&canvas);
        canvas.restore();
    }

private:
    std::shared_ptr<Document> m_doc;
    double m_timeSec;
    QSize m_size;
    VectorFit m_fit;
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
    return std::make_shared<DocumentPainter>(std::move(doc), usToSeconds(folded), request.size,
                                             request.source.fit);
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
