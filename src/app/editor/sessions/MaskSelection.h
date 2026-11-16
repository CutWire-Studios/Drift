#pragma once

#include <QImage>
#include <QPoint>
#include <QPointF>
#include <QSize>
#include <QVector>

namespace drift::masking {

// Fills the closed polygon (pixel coordinates) into a Grayscale8 matte, antialiased: white paints,
// black erases.
void fillPolygon(QImage &image, const QVector<QPointF> &pixelPolygon, bool erase, double opacity = 1.0);

// Blends a Grayscale8 coverage image into the matte (scaled to the matte's size when it differs):
// paint moves the matte towards white by the coverage, erase towards black.
void applyCover(QImage &image, const QImage &cover, bool erase);

// The closed loops a freehand stroke draws: every place the path crosses itself yields the
// enclosed polygon, and a path that ends within `closeTolerance` of its start is closed there.
// Loops enclosing less than `minArea` are dropped, so a wobble or a back-and-forth does not fill.
QVector<QVector<QPointF>> findStrokeLoops(const QVector<QPointF> &pixelPoints, double closeTolerance,
                                          double minArea);

// Per-pixel edge costs of an image for the livewire (Intelligent Scissors) path search: Laplacian
// zero crossings, gradient magnitude and gradient direction, weighted as in Mortensen & Barrett.
class EdgeMap
{
public:
    EdgeMap() = default;
    explicit EdgeMap(const QImage &image);

    bool isNull() const { return m_size.isEmpty(); }
    QSize size() const { return m_size; }

    // The lowest-cost pixel within `radius` of `p`: where a click lands on the nearest edge.
    QPoint snap(QPoint p, int radius) const;
    // The cheapest 8-connected path from `from` to `to`, both included. The search stays in the
    // two points' bounding box plus a margin; falls back to a straight line when it finds nothing.
    QVector<QPoint> path(QPoint from, QPoint to) const;

private:
    QPoint clamped(QPoint p) const;
    float linkCost(int p, int q, bool diagonal) const;

    QSize m_size;
    QVector<float> m_zero;
    QVector<float> m_grad;
    QVector<float> m_dirX;
    QVector<float> m_dirY;
};

// Selects the pixels of `image` whose CIELAB colour lies within `tolerance` (0..100) of the colour at
// `seed`, as a Grayscale8 coverage (255 inside) with a softened edge. `contiguous` keeps only the
// region connected to the seed.
QImage magicWandCover(const QImage &image, QPoint seed, double tolerance, bool contiguous);

} // namespace drift::masking
