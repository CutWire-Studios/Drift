#include "PdfSource.h"

#include "Effect.h"

#include <QCoreApplication>
#include <QJsonArray>

#include <algorithm>
#include <cmath>

namespace drift {

QString pdfLayoutToString(PdfLayout layout)
{
    switch (layout) {
    case PdfLayout::Column:
        break;
    case PdfLayout::Row:
        return QStringLiteral("row");
    case PdfLayout::Grid:
        return QStringLiteral("grid");
    }
    return QStringLiteral("column");
}

PdfLayout pdfLayoutFromString(const QString &layout)
{
    if (layout == QStringLiteral("row"))
        return PdfLayout::Row;
    if (layout == QStringLiteral("grid"))
        return PdfLayout::Grid;
    return PdfLayout::Column;
}

bool PdfSource::isAnimated() const
{
    for (auto it = keyframes.constBegin(); it != keyframes.constEnd(); ++it) {
        if (!it->isEmpty() && it->enabled())
            return true;
    }
    return false;
}

PdfSource PdfSource::resolvedAt(TimeUs clipTimeUs) const
{
    PdfSource out = *this;
    for (auto it = keyframes.constBegin(); it != keyframes.constEnd(); ++it) {
        if (!it->isEmpty() && it->enabled())
            setPdfScalar(out, it.key(), it->evaluateAt(clipTimeUs));
    }
    out.keyframes.clear();
    return out;
}

QJsonObject PdfSource::toJson() const
{
    QJsonArray sizesJson;
    for (const QSizeF &s : pageSizes)
        sizesJson.append(QJsonArray{s.width(), s.height()});
    QJsonObject o{
        {QStringLiteral("path"), path},
        {QStringLiteral("pageCount"), pageCount},
        {QStringLiteral("pageSizes"), sizesJson},
        {QStringLiteral("firstPage"), firstPage},
        {QStringLiteral("lastPage"), lastPage},
        {QStringLiteral("layout"), pdfLayoutToString(layout)},
        {QStringLiteral("gridColumns"), gridColumns},
        {QStringLiteral("gap"), gap},
    };
    for (const QString &key : pdfKeyframeProperties()) {
        double v = 0.0;
        pdfScalar(*this, key, &v);
        o.insert(key, v);
    }
    QJsonObject keyframesJson;
    for (auto it = keyframes.cbegin(); it != keyframes.cend(); ++it) {
        if (!it->isEmpty())
            keyframesJson.insert(it.key(), keyframesToJson(it.value()));
    }
    if (!keyframesJson.isEmpty())
        o.insert(QStringLiteral("keyframes"), keyframesJson);
    return o;
}

PdfSource PdfSource::fromJson(const QJsonObject &o)
{
    PdfSource p;
    if (o.isEmpty())
        return p;
    p.path = o.value(QStringLiteral("path")).toString();
    p.pageCount = std::max(0, o.value(QStringLiteral("pageCount")).toInt());
    const QJsonArray sizesJson = o.value(QStringLiteral("pageSizes")).toArray();
    for (const QJsonValue &v : sizesJson) {
        const QJsonArray a = v.toArray();
        p.pageSizes.append(a.size() == 2 ? QSizeF(a.at(0).toDouble(), a.at(1).toDouble())
                                         : QSizeF());
    }
    p.firstPage = std::max(1, o.value(QStringLiteral("firstPage")).toInt(1));
    p.lastPage = std::max(0, o.value(QStringLiteral("lastPage")).toInt(0));
    p.layout = pdfLayoutFromString(o.value(QStringLiteral("layout")).toString());
    p.gridColumns = std::max(1, o.value(QStringLiteral("gridColumns")).toInt(2));
    p.gap = std::max(0.0, o.value(QStringLiteral("gap")).toDouble(12.0));
    for (const QString &key : pdfKeyframeProperties()) {
        if (o.contains(key))
            setPdfScalar(p, key, o.value(key).toDouble());
    }
    const QJsonObject keyframesJson = o.value(QStringLiteral("keyframes")).toObject();
    for (auto it = keyframesJson.constBegin(); it != keyframesJson.constEnd(); ++it)
        p.keyframes.insert(it.key(), keyframesFromJson(it.value().toObject()));
    return p;
}

const QStringList &pdfKeyframeProperties()
{
    static const QStringList keys{
        QStringLiteral("scrollX"),
        QStringLiteral("scrollY"),
        QStringLiteral("zoom"),
    };
    return keys;
}

bool pdfScalar(const PdfSource &source, const QString &key, double *out)
{
    double v = 0.0;
    if (key == QStringLiteral("scrollX"))
        v = source.scrollX;
    else if (key == QStringLiteral("scrollY"))
        v = source.scrollY;
    else if (key == QStringLiteral("zoom"))
        v = source.zoom;
    else
        return false;
    if (out)
        *out = v;
    return true;
}

bool setPdfScalar(PdfSource &source, const QString &key, double value)
{
    if (key == QStringLiteral("scrollX"))
        source.scrollX = std::clamp(value, -1.0, 10000.0);
    else if (key == QStringLiteral("scrollY"))
        source.scrollY = std::clamp(value, -1.0, 10000.0);
    else if (key == QStringLiteral("zoom"))
        source.zoom = std::clamp(value, 0.05, 64.0);
    else
        return false;
    return true;
}

QString pdfKeyframeLabel(const QString &key)
{
    if (key == QStringLiteral("scrollX"))
        return QCoreApplication::translate("PdfSource", "Scroll X");
    if (key == QStringLiteral("scrollY"))
        return QCoreApplication::translate("PdfSource", "Scroll Y");
    if (key == QStringLiteral("zoom"))
        return QCoreApplication::translate("PdfSource", "Zoom");
    return key;
}

namespace {

// Distance from the start of unit 0 to the point `scroll` units in. Unit i spans [start_i,
// start_i + size_i); past either end the nearest unit's size carries on linearly.
double scrollOffset(const QVector<double> &unitSize, double scroll)
{
    if (unitSize.isEmpty())
        return 0.0;
    if (scroll < 0.0)
        return scroll * unitSize.first();
    const int last = int(unitSize.size()) - 1;
    const int n = int(std::floor(scroll));
    double start = 0.0;
    for (int i = 0; i < std::min(n, last); ++i)
        start += unitSize.at(i);
    if (n > last)
        return start + unitSize.at(last) + (scroll - last - 1) * unitSize.at(last);
    return start + (scroll - n) * unitSize.at(n);
}

QSizeF validSize(const QSizeF &s, const QSizeF &fallback)
{
    return s.width() > 0.0 && s.height() > 0.0 ? s : fallback;
}

} // namespace

QVector<PdfPagePlacement> layoutPdfPages(const PdfSource &src, QSizeF viewport)
{
    QVector<PdfPagePlacement> out;
    if (src.pageCount <= 0 || src.pageSizes.isEmpty() || viewport.width() <= 0.0
        || viewport.height() <= 0.0)
        return out;

    const int first = std::max(1, src.firstPage) - 1;
    const int last = std::min(src.lastPage > 0 ? src.lastPage : src.pageCount, src.pageCount) - 1;
    if (first > last)
        return out;

    const QSizeF ref = src.pageSizes.first();
    if (ref.width() <= 0.0 || ref.height() <= 0.0)
        return out;
    const auto sizeOf = [&](int index) {
        return index < src.pageSizes.size() ? validSize(src.pageSizes.at(index), ref) : ref;
    };
    const int count = last - first + 1;
    const double zoom = std::clamp(src.zoom, 0.05, 64.0);
    const double gapPts = std::max(0.0, src.gap);
    const QRectF view(QPointF(0, 0), viewport);

    const auto place = [&](int index, const QRectF &rect) {
        if (rect.intersects(view))
            out.append({index, rect});
    };

    if (src.layout == PdfLayout::Column) {
        const double w = viewport.width() * zoom;
        const double k = w / ref.width();
        const double g = gapPts * k;
        QVector<double> units(count);
        QVector<double> heights(count);
        for (int i = 0; i < count; ++i) {
            const QSizeF s = sizeOf(first + i);
            heights[i] = s.height() * w / s.width();
            units[i] = heights[i] + g;
        }
        const double offY = scrollOffset(units, src.scrollY);
        const double x = -src.scrollX * (w + g);
        double top = 0.0;
        for (int i = 0; i < count; ++i) {
            place(first + i, QRectF(x, top - offY, w, heights[i]));
            top += units[i];
        }
    } else if (src.layout == PdfLayout::Row) {
        const double h = viewport.height() * zoom;
        const double k = h / ref.height();
        const double g = gapPts * k;
        QVector<double> units(count);
        QVector<double> widths(count);
        for (int i = 0; i < count; ++i) {
            const QSizeF s = sizeOf(first + i);
            widths[i] = s.width() * h / s.height();
            units[i] = widths[i] + g;
        }
        const double offX = scrollOffset(units, src.scrollX);
        const double y = -src.scrollY * (h + g);
        double left = 0.0;
        for (int i = 0; i < count; ++i) {
            place(first + i, QRectF(left - offX, y, widths[i], h));
            left += units[i];
        }
    } else {
        const int cols = std::max(1, src.gridColumns);
        const double k = viewport.width() * zoom / (cols * ref.width() + (cols - 1) * gapPts);
        const double w = ref.width() * k;
        const double g = gapPts * k;
        const int rows = (count + cols - 1) / cols;
        QVector<double> heights(count);
        QVector<double> rowUnits(rows, 0.0);
        for (int i = 0; i < count; ++i) {
            const QSizeF s = sizeOf(first + i);
            heights[i] = s.height() * w / s.width();
            rowUnits[i / cols] = std::max(rowUnits[i / cols], heights[i]);
        }
        QVector<double> rowTops(rows);
        double top = 0.0;
        for (int r = 0; r < rows; ++r) {
            rowTops[r] = top;
            rowUnits[r] += g;
            top += rowUnits[r];
        }
        const double offY = scrollOffset(rowUnits, src.scrollY);
        const double offX = src.scrollX * (w + g);
        for (int i = 0; i < count; ++i)
            place(first + i, QRectF((i % cols) * (w + g) - offX, rowTops[i / cols] - offY, w,
                                   heights[i]));
    }
    return out;
}

} // namespace drift
