#pragma once

#include <QRectF>
#include <QList>

// Geometry the scene-graph timeline shares with the QML clip overlay, kept free of Qt Quick so
// it can be tested on its own. Every formula here mirrors one in TimelineClipItem.qml or
// ClipFilmstrip.qml; where they disagree, the QML is what the user has been looking at.
namespace timelinelayout {

// The clip's rect in its track row: inset by the selection ring on every side, and floored to a
// minimum width so a short clip stays visible and grabbable at any zoom.
QRectF clipRect(double startSeconds, double durationSeconds, double pxPerSecond, double rowHeight,
                double ringWidth, double minWidth, double offsetX = 0.0, double offsetY = 0.0);

// The band of x, in the same space as clip rects, that should have content built for it. Snapped
// outward to `chunk` so scrolling inside a chunk changes nothing, and padded so a clip entering
// the viewport already has its content.
struct Window
{
    double left = 0.0;
    double right = 0.0;
    bool contains(double l, double r) const { return r >= left && l <= right; }
    bool operator==(const Window &) const = default;
};
Window contentWindow(double viewX, double viewW, double chunk = 512.0, double pad = 256.0);

// Filmstrip tiles over a clip body, on a grid anchored to the source so trimming the head slides
// the frames with the source rather than with the clip's left edge.
struct TileGrid
{
    bool sourceMapped = false;
    double pxPerSourceSec = 0.0;
    double stripOriginX = 0.0; // relative to the body's left edge
    int firstTile = 0;         // inclusive
    int lastTile = -1;         // inclusive
    double tileWidth = 120.0;
};
TileGrid tileGrid(double bodyX, double bodyWidth, double inPoint, double outPoint,
                  double sourceDuration, const Window &window, double tileWidth = 120.0);
// Which coarse strip frame stands in for a tile.
int frameForTile(const TileGrid &grid, int tileIndex, double sourceDuration, int frameCount);
// The on-demand tile level for this zoom: the largest power-of-two seconds that fit in a tile.
int tileLevel(const TileGrid &grid);
// Source seconds at the middle of a tile, or a negative value when the grid is unmapped.
double tileSourceSeconds(const TileGrid &grid, int tileIndex);

// The slice of a clip's width that the waveform is drawn over: the part inside the window,
// capped at `maxWidth` px so a very wide viewport cannot ask for an unbounded query.
struct Span
{
    double left = 0.0; // relative to the clip's left edge
    double width = 0.0;
};
Span waveformSpan(double clipX, double clipWidth, const Window &window, double maxWidth = 4096.0);

enum class Zone { None, Body, Hover };

// Where a point lands on a clip. Body is the part a press moves or selects: the clip minus its
// edge strips, which belong to the trim handles on the overlay. Hover is the wider area around
// the clip in which the overlay has to exist so those handles can be reached.
Zone hitZone(const QRectF &clip, const QPointF &point, double edgeMargin, double hotspotExtra);

} // namespace timelinelayout
