#include "SkiaShapePainter.h"

#include "ShapeRaster.h"
#include "SkiaPath.h"
#include "core/ShapePath.h"

#include "include/core/SkCanvas.h"

namespace drift::skia {

namespace {

class ShapePainter final : public VectorPainter
{
public:
    ShapePainter(const ShapeStyle &style, int width, int height, double renderScale)
        : m_layout(shapeLayout(style, width, height, renderScale)),
          m_size(qMax(1, width), qMax(1, height)),
          m_key(shapeRasterKey(style, width, height, renderScale))
    {
    }

    QSize size() const override { return m_size; }
    quint64 cacheKey() const override { return m_key; }

    void paint(SkCanvas &canvas) const override
    {
        const SkPath path = toSkPath(shapePath(m_layout.style, m_layout.bounds));
        SkPaint fill;
        fill.setAntiAlias(true);
        if (applyBrush(fill, shapeBrush(m_layout.style, m_layout.bounds)))
            canvas.drawPath(path, fill);
        SkPaint stroke;
        stroke.setAntiAlias(true);
        if (applyPen(stroke, shapePen(m_layout.style, m_layout.strokeWidth)))
            canvas.drawPath(path, stroke);
    }

private:
    ShapeLayout m_layout;
    QSize m_size;
    quint64 m_key;
};

} // namespace

std::shared_ptr<const VectorPainter> makeShapePainter(const ShapeStyle &style, int width,
                                                      int height, double renderScale)
{
    return std::make_shared<ShapePainter>(style, width, height, renderScale);
}

} // namespace drift::skia
