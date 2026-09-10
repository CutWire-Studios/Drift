#include "FrameSheet.h"

#include <QFont>
#include <QFontMetrics>
#include <QPainter>

#include <algorithm>
#include <cmath>

namespace drift::framesheet {

quint64 dHash(const QImage &image)
{
    if (image.isNull())
        return 0;
    const QImage g = image.convertToFormat(QImage::Format_Grayscale8)
                         .scaled(9, 8, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    quint64 hash = 0;
    for (int y = 0; y < 8; ++y) {
        const uchar *row = g.constScanLine(y);
        for (int x = 0; x < 8; ++x) {
            if (row[x] < row[x + 1])
                hash |= quint64(1) << (y * 8 + x);
        }
    }
    return hash;
}

int hammingDistance(quint64 a, quint64 b)
{
    return int(qPopulationCount(a ^ b));
}

Selection selectChanges(const QList<quint64> &hashes, int minChange, int window, int maxKeep)
{
    Selection sel;
    if (hashes.isEmpty())
        return sel;

    QList<int> kept{0};
    QList<int> change{0}; // distance to the previous kept frame
    for (int i = 1; i < hashes.size(); ++i) {
        const int from = window > 0 ? std::max(0, int(kept.size()) - window) : 0;
        bool distinct = true;
        for (int j = from; j < kept.size(); ++j) {
            if (hammingDistance(hashes.at(i), hashes.at(kept.at(j))) <= minChange) {
                distinct = false;
                break;
            }
        }
        if (!distinct)
            continue;
        change.append(hammingDistance(hashes.at(i), hashes.at(kept.last())));
        kept.append(i);
    }

    if (maxKeep > 0 && kept.size() > maxKeep) {
        QList<int> order;
        for (int j = 1; j < kept.size(); ++j)
            order.append(j);
        std::sort(order.begin(), order.end(), [&change](int a, int b) {
            if (change.at(a) != change.at(b))
                return change.at(a) > change.at(b);
            return a < b;
        });
        order.resize(maxKeep - 1);
        QList<int> trimmed{0};
        for (int j : std::as_const(order))
            trimmed.append(kept.at(j));
        std::sort(trimmed.begin(), trimmed.end());
        kept = trimmed;
    }

    sel.kept = kept;
    sel.skipped = int(hashes.size()) - int(kept.size());
    return sel;
}

QList<int> selectUniform(int count, int n)
{
    QList<int> out;
    if (count <= 0 || n <= 0)
        return out;
    if (n >= count) {
        for (int i = 0; i < count; ++i)
            out.append(i);
        return out;
    }
    for (int i = 0; i < n; ++i)
        out.append(int(qint64(i) * count / n));
    return out;
}

Layout layoutFor(int n, double aspect, int cols, int tileWidth, int maxLongEdge, int maxTokens)
{
    Layout layout;
    if (n <= 0)
        return layout;
    if (aspect <= 0.0)
        aspect = 16.0 / 9.0;
    if (cols <= 0)
        cols = n <= 6 ? 3 : 4;
    cols = std::max(1, std::min(cols, n));
    const int rows = (n + cols - 1) / cols;

    int w = tileWidth > 0 ? tileWidth : maxLongEdge / cols;
    w = std::min(w, maxLongEdge / cols);
    int h = 0;
    int tokens = 0;
    for (;; --w) {
        h = std::max(1, int(std::lround(w / aspect)));
        const int sheetW = w * cols;
        const int sheetH = h * rows;
        tokens = ((sheetW + 27) / 28) * ((sheetH + 27) / 28);
        if ((std::max(sheetW, sheetH) <= maxLongEdge && tokens <= maxTokens) || w <= 1)
            break;
    }

    layout.cols = cols;
    layout.rows = rows;
    layout.tile = QSize(w, h);
    layout.sheet = QSize(w * cols, h * rows);
    layout.tokens = tokens;
    return layout;
}

QImage compose(const Layout &layout, const QList<Tile> &tiles, bool labels)
{
    if (layout.sheet.isEmpty())
        return {};
    QImage sheet(layout.sheet, QImage::Format_RGB888);
    sheet.fill(Qt::black);

    QPainter p(&sheet);
    QFont font;
    font.setPixelSize(std::max(18, layout.tile.height() / 12));
    p.setFont(font);
    const QFontMetrics metrics(font);
    constexpr int kPad = 4;

    const int cells = layout.cols * layout.rows;
    for (int i = 0; i < tiles.size() && i < cells; ++i) {
        const Tile &tile = tiles.at(i);
        const QPoint cell((i % layout.cols) * layout.tile.width(),
                          (i / layout.cols) * layout.tile.height());
        if (!tile.image.isNull()) {
            const QImage scaled = tile.image.scaled(layout.tile, Qt::KeepAspectRatio,
                                                    Qt::SmoothTransformation);
            p.drawImage(cell.x() + (layout.tile.width() - scaled.width()) / 2,
                        cell.y() + (layout.tile.height() - scaled.height()) / 2, scaled);
        }
        if (!labels || tile.label.isEmpty())
            continue;
        const QRect text = metrics.boundingRect(tile.label);
        const QRect box(cell.x(), cell.y(), text.width() + 2 * kPad, metrics.height() + 2 * kPad);
        p.fillRect(box, Qt::black);
        p.setPen(Qt::white);
        p.drawText(box.adjusted(kPad, kPad, -kPad, -kPad), Qt::AlignLeft | Qt::AlignVCenter,
                   tile.label);
    }
    return sheet;
}

} // namespace drift::framesheet
