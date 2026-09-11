#pragma once

#include <QImage>
#include <QList>
#include <QSize>
#include <QString>

// Contact sheets for agents: pick the frames worth showing and tile them into one image
// that fits a single vision call. Pure functions — decoding and compositing happen in the
// caller.
namespace drift::framesheet {

// 64-bit difference hash: grayscale, 9x8, bit set where a pixel is darker than its right
// neighbour. Null image -> 0.
quint64 dHash(const QImage &image);
int hammingDistance(quint64 a, quint64 b);

struct Selection
{
    QList<int> kept; // ascending indices into the hash list; always starts with 0
    int skipped = 0;
};

// Index 0 is always kept; every later index is kept when its distance to each of the last
// `window` kept hashes exceeds `minChange`. Over `maxKeep` survivors, index 0 plus the
// maxKeep-1 with the largest distance to their previous kept frame remain.
Selection selectChanges(const QList<quint64> &hashes, int minChange, int window, int maxKeep);

// `n` indices spread evenly over 0..count-1, starting at 0.
QList<int> selectUniform(int count, int n);

struct Layout
{
    int cols = 0;
    int rows = 0;
    QSize tile;
    QSize sheet;
    int tokens = 0; // ceil(w/28) * ceil(h/28), the vision cost of the sheet
};

// cols 0 -> 3 for n <= 6, else 4. The tile shrinks until the sheet's long edge fits
// `maxLongEdge` and its token cost fits `maxTokens`.
Layout layoutFor(int n, double aspect, int cols, int tileWidth, int maxLongEdge = 1456,
                 int maxTokens = 1568);

struct Tile
{
    QImage image;
    QString label;
};

// Black sheet; each tile scaled to fit its cell and centred, label burned top-left in white
// on solid black when `labels` is set.
QImage compose(const Layout &layout, const QList<Tile> &tiles, bool labels);

} // namespace drift::framesheet
