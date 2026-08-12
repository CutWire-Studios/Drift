#include "MaskApplier.h"

#include "core/ShapePath.h"

#include <QPainter>
#include <QPainterPath>
#include <QtMath>

namespace {

// Square box of the given radius, centred — masks size their parametric shapes uniformly.
QRectF squareBounds(const QPointF &center, double radius)
{
    return QRectF(center.x() - radius, center.y() - radius, radius * 2.0, radius * 2.0);
}

QPainterPath maskPath(const drift::Mask &mask, int canvasWidth, int canvasHeight)
{
    const QPointF center(mask.x * canvasWidth, mask.y * canvasHeight);
    const double halfW = qMax(1.0, mask.w * canvasWidth * 0.5);
    const double halfH = qMax(1.0, mask.h * canvasHeight * 0.5);

    switch (mask.shape) {
    case drift::MaskShape::Rectangle: {
        QPainterPath path;
        path.addRect(QRectF(center.x() - halfW, center.y() - halfH, halfW * 2.0, halfH * 2.0));
        return path;
    }
    case drift::MaskShape::Ellipse: {
        QPainterPath path;
        path.addEllipse(center, halfW, halfH);
        return path;
    }
    case drift::MaskShape::Star:
        return drift::regularPolygonPath(squareBounds(center, qMin(halfW, halfH)), 5, mask.rotation);
    case drift::MaskShape::Heart:
        return drift::heartPath(squareBounds(center, qMin(halfW, halfH)));
    case drift::MaskShape::Bars: {
        const double barH = halfH;
        QPainterPath path;
        path.addRect(QRectF(0, 0, canvasWidth, barH));
        path.addRect(QRectF(0, canvasHeight - barH, canvasWidth, barH));
        return path;
    }
    case drift::MaskShape::Freeform: {
        QPainterPath path;
        if (mask.points.size() < 2)
            return path;

        const auto scale = [canvasWidth, canvasHeight](const QPointF &p) {
            return QPointF(p.x() * canvasWidth, p.y() * canvasHeight);
        };

        path.moveTo(scale(mask.points.first().pos));
        // Every edge, including the closing one back to the first vertex.
        for (int i = 0; i < mask.points.size(); ++i) {
            const drift::MaskPoint &from = mask.points.at(i);
            const drift::MaskPoint &to = mask.points.at((i + 1) % mask.points.size());
            const QPointF end = scale(to.pos);
            // Zero tangents on both ends mean a straight edge; anything else is a cubic, so a
            // later editor can expose handles without touching this.
            if (from.outTan.isNull() && to.inTan.isNull()) {
                path.lineTo(end);
            } else {
                path.cubicTo(scale(from.pos + from.outTan), scale(to.pos + to.inTan), end);
            }
        }
        path.closeSubpath();
        return path;
    }
    case drift::MaskShape::Media:
        // Raster, not parametric: the coverage map is decoded per frame in FrameCompositor and
        // rides on GpuLayer::matte. There is no path to rasterize.
        break;
    case drift::MaskShape::None:
        break;
    }
    return {};
}

// One separable box pass along x. Callers transpose to cover y, so this is the whole kernel.
// The window is a running sum with clamp-to-edge, which is what makes it O(1) per pixel
// instead of O(radius) — a 2D window per pixel is unaffordable at canvas sizes.
void boxBlurRows(const QImage &src, QImage &dst, int radius)
{
    const int width = src.width();
    const int height = src.height();
    const int window = radius * 2 + 1;

    for (int y = 0; y < height; ++y) {
        const uchar *in = src.constScanLine(y);
        uchar *out = dst.scanLine(y);

        // Seed the window at x = 0: radius+1 copies of the left edge plus the first radius pixels.
        int sum = int(in[0]) * (radius + 1);
        for (int x = 1; x <= radius; ++x)
            sum += in[qMin(x, width - 1)];

        for (int x = 0; x < width; ++x) {
            out[x] = uchar(sum / window);
            sum += in[qMin(x + radius + 1, width - 1)];
            sum -= in[qMax(x - radius, 0)];
        }
    }
}

QImage transposed(const QImage &src)
{
    QImage out(src.height(), src.width(), QImage::Format_Grayscale8);
    uchar *outBits = out.bits();
    const qsizetype outStride = out.bytesPerLine();
    for (int y = 0; y < src.height(); ++y) {
        const uchar *in = src.constScanLine(y);
        for (int x = 0; x < src.width(); ++x)
            outBits[x * outStride + y] = in[x];
    }
    return out;
}

// Blur rows, transpose, blur rows, transpose back — a separable box, identical to the 2D
// (2r+1)² window it replaces but O(1) per pixel instead of O(r²).
QImage boxBlurPass(const QImage &src, int radius)
{
    QImage rows(src.size(), QImage::Format_Grayscale8);
    boxBlurRows(src, rows, radius);
    const QImage flipped = transposed(rows);
    QImage cols(flipped.size(), QImage::Format_Grayscale8);
    boxBlurRows(flipped, cols, radius);
    return transposed(cols);
}

QImage blurAlpha(const QImage &alpha, int radius)
{
    if (radius <= 0 || alpha.isNull())
        return alpha;

    QImage out = alpha;
    if (out.format() != QImage::Format_Grayscale8)
        out = out.convertToFormat(QImage::Format_Grayscale8);

    // Repeated box passes approximate a gaussian; the count matches the old kernel so the
    // edge falloff is unchanged.
    const int passes = qBound(1, radius / 2, 8);
    for (int pass = 0; pass < passes; ++pass)
        out = boxBlurPass(out, radius);
    return out;
}

} // namespace

namespace drift {

QImage maskAlphaMap(const Mask &mask, int canvasWidth, int canvasHeight)
{
    if (mask.shape == MaskShape::None || mask.shape == MaskShape::Media || canvasWidth <= 0
        || canvasHeight <= 0)
        return {};

    QImage alpha(canvasWidth, canvasHeight, QImage::Format_Grayscale8);
    alpha.fill(mask.invert ? 255 : 0);

    QPainter mp(&alpha);
    mp.setRenderHint(QPainter::Antialiasing);
    mp.setBrush(mask.invert ? Qt::black : Qt::white);
    mp.setPen(Qt::NoPen);

    QPainterPath path = maskPath(mask, canvasWidth, canvasHeight);
    if (!path.isEmpty()) {
        const QPointF center(mask.x * canvasWidth, mask.y * canvasHeight);
        // Bars is a full-width band by construction, so rotating it only clips the frame.
        // Every other shape, freeform included, rotates about its centre.
        if (!qFuzzyIsNull(mask.rotation) && mask.shape != MaskShape::Bars) {
            QTransform transform;
            transform.translate(center.x(), center.y());
            transform.rotate(mask.rotation);
            transform.translate(-center.x(), -center.y());
            path = transform.map(path);
        }
        mp.drawPath(path);
    }
    mp.end();

    if (mask.feather > 0.0)
        alpha = blurAlpha(alpha, qMax(1, static_cast<int>(mask.feather)));

    return alpha;
}

QImage maskAlphaMap(const QList<Mask> &masks, int canvasWidth, int canvasHeight)
{
    if (canvasWidth <= 0 || canvasHeight <= 0)
        return {};

    QImage accum;
    for (const Mask &mask : masks) {
        // A matte has no path to rasterize — FrameCompositor decodes its frame and the GPU
        // compositor folds it in, because only they know the time.
        if (!mask.contributes() || mask.shape == MaskShape::Media)
            continue;

        const QImage coverage = maskAlphaMap(mask, canvasWidth, canvasHeight);
        if (coverage.isNull())
            continue;

        // The first contributing entry has nothing to combine with, so it seeds the accumulator
        // whatever its op says. Starting from black instead would make a lone Subtract or
        // Intersect entry blank the clip.
        if (accum.isNull()) {
            accum = coverage;
            continue;
        }

        uchar *dst = accum.bits();
        const uchar *src = coverage.constBits();
        const qsizetype dstStride = accum.bytesPerLine();
        const qsizetype srcStride = coverage.bytesPerLine();

        for (int y = 0; y < canvasHeight; ++y) {
            uchar *dstRow = dst + y * dstStride;
            const uchar *srcRow = src + y * srcStride;
            for (int x = 0; x < canvasWidth; ++x) {
                const int a = dstRow[x];
                const int b = srcRow[x];
                switch (mask.op) {
                case MaskOp::Add:
                    dstRow[x] = uchar(qMax(a, b));
                    break;
                case MaskOp::Subtract:
                    dstRow[x] = uchar(a * (255 - b) / 255);
                    break;
                case MaskOp::Intersect:
                    dstRow[x] = uchar(a * b / 255);
                    break;
                }
            }
        }
    }
    return accum;
}

namespace {

QImage multiplyAlpha(const QImage &frame, const QImage &alpha, int canvasWidth, int canvasHeight)
{
    QImage rgba = frame.convertToFormat(QImage::Format_RGBA8888);
    if (rgba.size() != alpha.size())
        rgba = rgba.scaled(canvasWidth, canvasHeight, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

    QImage out(rgba.size(), QImage::Format_RGBA8888);
    for (int y = 0; y < rgba.height(); ++y) {
        const QRgb *src = reinterpret_cast<const QRgb *>(rgba.constScanLine(y));
        QRgb *dst = reinterpret_cast<QRgb *>(out.scanLine(y));
        const uchar *alphaLine = alpha.constScanLine(y);
        for (int x = 0; x < rgba.width(); ++x) {
            const int a = qAlpha(src[x]) * alphaLine[x] / 255;
            dst[x] = qRgba(qRed(src[x]), qGreen(src[x]), qBlue(src[x]), a);
        }
    }
    return out;
}

} // namespace

QImage applyMask(const QImage &frame, const Mask &mask, int canvasWidth, int canvasHeight)
{
    if (mask.shape == MaskShape::None || mask.shape == MaskShape::Media || frame.isNull())
        return frame;

    const QImage alpha = maskAlphaMap(mask, canvasWidth, canvasHeight);
    if (alpha.isNull())
        return frame;

    return multiplyAlpha(frame, alpha, canvasWidth, canvasHeight);
}

QImage applyMask(const QImage &frame, const QList<Mask> &masks, int canvasWidth, int canvasHeight)
{
    if (frame.isNull() || masksAreInert(masks))
        return frame;

    const QImage alpha = maskAlphaMap(masks, canvasWidth, canvasHeight);
    if (alpha.isNull())
        return frame;

    return multiplyAlpha(frame, alpha, canvasWidth, canvasHeight);
}

} // namespace drift
