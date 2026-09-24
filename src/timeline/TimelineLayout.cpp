#include "TimelineLayout.h"

#include <QtMath>

#include <algorithm>
#include <cmath>

namespace timelinelayout {

QRectF clipRect(double startSeconds, double durationSeconds, double pxPerSecond, double rowHeight,
                double ringWidth, double minWidth, double offsetX, double offsetY)
{
    const double x = std::max(ringWidth, startSeconds * pxPerSecond + ringWidth + offsetX);
    const double w = std::max(minWidth, durationSeconds * pxPerSecond - 2.0 * ringWidth);
    const double h = std::max(0.0, rowHeight - 2.0 * ringWidth);
    return QRectF(x, ringWidth + offsetY, w, h);
}

Window contentWindow(double viewX, double viewW, double chunk, double pad)
{
    Window w;
    w.left = std::floor((viewX - pad) / chunk) * chunk;
    w.right = std::ceil((viewX + std::max(0.0, viewW) + pad) / chunk) * chunk;
    return w;
}

TileGrid tileGrid(double bodyX, double bodyWidth, double inPoint, double outPoint,
                  double sourceDuration, const Window &window, double tileWidth)
{
    TileGrid g;
    g.tileWidth = tileWidth;
    g.sourceMapped = sourceDuration > 0.0 && outPoint > inPoint && bodyWidth > 0.0;
    g.pxPerSourceSec = g.sourceMapped ? bodyWidth / (outPoint - inPoint) : 0.0;
    g.stripOriginX = g.sourceMapped ? -inPoint * g.pxPerSourceSec : 0.0;
    if (bodyWidth <= 0.0)
        return g;

    const double stripWidth = g.sourceMapped
        ? std::max(sourceDuration * g.pxPerSourceSec, bodyWidth - g.stripOriginX)
        : bodyWidth;
    const int totalTiles = std::max(1, int(std::ceil(stripWidth / tileWidth)));
    const int firstInBody = std::max(0, int(std::floor(-g.stripOriginX / tileWidth)));
    const int lastInBody =
        std::min(totalTiles - 1, int(std::floor((bodyWidth - g.stripOriginX) / tileWidth)));

    const double localLeft = window.left - bodyX - g.stripOriginX;
    const double localRight = window.right - bodyX - g.stripOriginX;
    g.firstTile = std::max(firstInBody, int(std::floor(localLeft / tileWidth)));
    g.lastTile = std::min(lastInBody, int(std::floor(localRight / tileWidth)));
    return g;
}

int frameForTile(const TileGrid &grid, int tileIndex, double sourceDuration, int frameCount)
{
    frameCount = std::max(1, frameCount);
    if (grid.sourceMapped && grid.pxPerSourceSec > 0.0 && sourceDuration > 0.0) {
        const double srcSec = (tileIndex + 0.5) * grid.tileWidth / grid.pxPerSourceSec;
        const int f = int(std::floor(srcSec / sourceDuration * frameCount));
        return std::clamp(f, 0, frameCount - 1);
    }
    return ((tileIndex % frameCount) + frameCount) % frameCount;
}

int tileLevel(const TileGrid &grid)
{
    if (grid.pxPerSourceSec <= 0.0)
        return 0;
    const double secondsPerTile = grid.tileWidth / grid.pxPerSourceSec;
    return std::clamp(int(std::floor(std::log2(secondsPerTile))), -3, 14);
}

double tileSourceSeconds(const TileGrid &grid, int tileIndex)
{
    if (!grid.sourceMapped || grid.pxPerSourceSec <= 0.0)
        return -1.0;
    return (tileIndex + 0.5) * grid.tileWidth / grid.pxPerSourceSec;
}

Span waveformSpan(double clipX, double clipWidth, const Window &window, double maxWidth)
{
    Span s;
    s.left = std::max(0.0, window.left - clipX);
    const double right = std::min(clipWidth, window.right - clipX);
    s.width = std::clamp(std::floor(right - s.left), 0.0, maxWidth);
    return s;
}

Zone hitZone(const QRectF &clip, const QPointF &point, double edgeMargin, double hotspotExtra)
{
    const QRectF hover = clip.adjusted(-hotspotExtra, -6.0, hotspotExtra, 6.0);
    if (!hover.contains(point))
        return Zone::None;
    const QRectF body = clip.adjusted(edgeMargin, 0.0, -edgeMargin, 0.0);
    if (body.width() > 0.0 && body.contains(point))
        return Zone::Body;
    return Zone::Hover;
}

} // namespace timelinelayout
