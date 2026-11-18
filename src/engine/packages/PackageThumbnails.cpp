#include "engine/packages/PackageThumbnails.h"

#include "engine/gpu/effects/GpuEffectExecutor.h"

#include <QColor>
#include <QImage>
#include <QLinearGradient>
#include <QPainter>

namespace {

// The time the offline tools have always rendered effects at.
constexpr drift::TimeUs kThumbnailTimeUs = 500000;

// SMPTE-style colour bars over a grey ramp and a hue sweep, laid out for any aspect ratio.
QImage makeTestCard(QSize size)
{
    QImage image(size, QImage::Format_RGBA8888);
    image.fill(Qt::black);
    QPainter p(&image);
    const qreal w = size.width();
    const qreal h = size.height();

    const QColor bars[] = {{191, 191, 191}, {191, 191, 0}, {0, 191, 191}, {0, 191, 0},
                           {191, 0, 191},   {191, 0, 0},   {0, 0, 191}};
    const QColor reverse[] = {{0, 0, 191}, {19, 19, 19}, {191, 0, 191}, {19, 19, 19},
                              {0, 191, 191}, {19, 19, 19}, {191, 191, 191}};
    const qreal barW = w / 7.0;
    for (int i = 0; i < 7; ++i) {
        p.fillRect(QRectF(i * barW, 0, barW + 1, h * 0.62), bars[i]);
        p.fillRect(QRectF(i * barW, h * 0.62, barW + 1, h * 0.08), reverse[i]);
    }

    QLinearGradient ramp(0, 0, w, 0);
    ramp.setColorAt(0.0, Qt::black);
    ramp.setColorAt(1.0, Qt::white);
    p.fillRect(QRectF(0, h * 0.70, w, h * 0.15), ramp);

    QLinearGradient hue(0, 0, w, 0);
    for (int i = 0; i <= 6; ++i)
        hue.setColorAt(i / 6.0, QColor::fromHsvF((i % 6) / 6.0, 1.0, 1.0));
    p.fillRect(QRectF(0, h * 0.85, w, h * 0.15 + 1), hue);
    p.end();
    return image;
}

// Two bases that stay distinguishable at any progress: warm vs cool, and different structure.
QImage makeBaseA(int size)
{
    QImage image(size, size, QImage::Format_RGBA8888);
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing, true);

    QLinearGradient bg(0, 0, size, size);
    bg.setColorAt(0.0, QColor(232, 108, 52));
    bg.setColorAt(1.0, QColor(122, 30, 62));
    p.fillRect(image.rect(), bg);

    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 214, 132));
    p.drawEllipse(QPoint(int(size * 0.34), int(size * 0.34)), int(size * 0.16), int(size * 0.16));

    p.setBrush(QColor(60, 16, 40, 190));
    for (int i = 0; i < 4; ++i) {
        const int y = int(size * (0.60 + i * 0.10));
        p.drawRect(0, y, size, int(size * 0.045));
    }
    p.end();
    return image;
}

QImage makeBaseB(int size)
{
    QImage image(size, size, QImage::Format_RGBA8888);
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing, true);

    QLinearGradient bg(0, size, size, 0);
    bg.setColorAt(0.0, QColor(24, 44, 96));
    bg.setColorAt(1.0, QColor(58, 186, 176));
    p.fillRect(image.rect(), bg);

    // A checker reads clearly through warps, wipes and pixelation.
    p.setBrush(QColor(232, 244, 255, 60));
    p.setPen(Qt::NoPen);
    const int cells = 8;
    const double step = double(size) / cells;
    for (int y = 0; y < cells; ++y) {
        for (int x = 0; x < cells; ++x) {
            if ((x + y) % 2 == 0)
                p.drawRect(QRectF(x * step, y * step, step, step));
        }
    }

    p.setBrush(QColor(255, 255, 255, 220));
    p.drawEllipse(QPoint(int(size * 0.66), int(size * 0.62)), int(size * 0.14), int(size * 0.14));
    p.end();
    return image;
}

// Scaled to cover the target and cropped about its centre. The built-in bases are square, so
// this is a plain resize unless a non-square cell is requested.
QImage coverCrop(const QImage &image, QSize size)
{
    const QImage scaled = image.scaled(size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    return scaled.copy((scaled.width() - size.width()) / 2, (scaled.height() - size.height()) / 2,
                       size.width(), size.height());
}

QImage defaultBase(QImage (*paint)(int), QSize size)
{
    return coverCrop(paint(qMax(size.width(), size.height()) * 2), size);
}

QImage renderStrip(const QString &cacheKey, const drift::GpuEffectDefinition &gpu,
                   const QMap<QString, QVariant> &params, QSize cellSize, int frames,
                   const QList<QImage> &sources)
{
    QImage strip(cellSize.width() * frames, cellSize.height(), QImage::Format_RGBA8888);
    strip.fill(Qt::transparent);
    QPainter painter(&strip);
    for (int i = 0; i < frames; ++i) {
        const double progress = double(i) / double(frames - 1);
        bool ok = false;
        const QImage cell = GpuEffectExecutor::instance().apply(cacheKey, gpu, sources, params,
                                                                kThumbnailTimeUs, progress, &ok);
        if (!ok || cell.isNull())
            return {};
        painter.drawImage(QPoint(i * cellSize.width(), 0), cell);
    }
    painter.end();
    return strip;
}

} // namespace

QImage defaultEffectBase(int side)
{
    return makeTestCard(QSize(side, side));
}

QImage defaultSampleImage(int which, QSize size)
{
    if (which != 1 && which != 2)
        return makeTestCard(size);
    return defaultBase(which == 1 ? makeBaseA : makeBaseB, size);
}

QImage renderEffectThumbnail(const QString &cacheKey, const drift::GpuEffectDefinition &gpu,
                             const QMap<QString, QVariant> &params, QSize size, const QImage &source)
{
    const QImage base = source.isNull() ? makeTestCard(size) : source;
    bool ok = false;
    const QImage result = GpuEffectExecutor::instance().apply(cacheKey, gpu, {base}, params,
                                                              kThumbnailTimeUs, 0.0, &ok);
    if (!ok)
        return {};
    return result.convertToFormat(QImage::Format_RGBA8888)
        .scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

QImage renderTransitionStrip(const QString &cacheKey, const drift::GpuEffectDefinition &gpu,
                             const QMap<QString, QVariant> &params, QSize cellSize, int frames,
                             const QImage &from, const QImage &to)
{
    if (frames < 2 || cellSize.isEmpty())
        return {};
    const QImage fromImage = from.isNull() ? defaultBase(makeBaseA, cellSize) : from;
    const QImage toImage = to.isNull() ? defaultBase(makeBaseB, cellSize) : to;
    return renderStrip(cacheKey, gpu, params, cellSize, frames, {fromImage, toImage});
}

QImage renderAnimationStrip(const QString &cacheKey, const drift::GpuEffectDefinition &gpu,
                            QMap<QString, QVariant> params, QSize cellSize, int frames,
                            bool outDirection, const QImage &source)
{
    if (frames < 2 || cellSize.isEmpty())
        return {};
    params.insert(QStringLiteral("u_direction"), outDirection ? 1.0 : 0.0);
    const QImage base = source.isNull() ? defaultBase(makeBaseA, cellSize) : source;
    return renderStrip(cacheKey, gpu, params, cellSize, frames, {base});
}
