#include "PdfClipRenderer.h"

#include "PdfiumRuntime.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QFont>
#include <QList>
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>
#include <QTextOption>

#include <algorithm>
#include <tuple>

namespace drift {

namespace {

constexpr int kMaxSide = 16384;
constexpr int kMaxResultEntries = 6;
constexpr int kMaxPlaceholderEntries = 8;

struct ResultKey
{
    QString path;
    qint64 mtimeMs = 0;
    qint64 fileSize = 0;
    int firstPage = 1;
    int lastPage = 0;
    int layout = 0;
    int gridColumns = 0;
    double gap = 0;
    double scrollX = 0;
    double scrollY = 0;
    double zoom = 0;
    int width = 0;
    int height = 0;

    bool operator==(const ResultKey &o) const
    {
        return std::tie(path, mtimeMs, fileSize, firstPage, lastPage, layout, gridColumns, gap,
                        scrollX, scrollY, zoom, width, height)
               == std::tie(o.path, o.mtimeMs, o.fileSize, o.firstPage, o.lastPage, o.layout,
                           o.gridColumns, o.gap, o.scrollX, o.scrollY, o.zoom, o.width, o.height);
    }
};

struct ResultEntry
{
    ResultKey key;
    QImage image;
};

struct PlaceholderEntry
{
    QString message;
    QSize size;
    QImage image;
};

QMutex g_cacheMutex;
QList<ResultEntry> g_results; // least recently used first
QList<PlaceholderEntry> g_placeholders;

QImage placeholderImage(const QString &message, QSize size)
{
    {
        QMutexLocker lock(&g_cacheMutex);
        for (const PlaceholderEntry &entry : std::as_const(g_placeholders)) {
            if (entry.size == size && entry.message == message)
                return entry.image;
        }
    }

    QImage image(size, QImage::Format_RGBA8888);
    image.fill(Qt::black);
    {
        QPainter painter(&image);
        painter.setRenderHint(QPainter::TextAntialiasing);
        QFont font = painter.font();
        font.setPixelSize(std::max(10, std::min(size.width(), size.height()) / 18));
        painter.setFont(font);
        painter.setPen(Qt::white);
        const int margin = size.width() / 16;
        QTextOption option(Qt::AlignCenter);
        option.setWrapMode(QTextOption::WordWrap);
        painter.drawText(QRectF(image.rect()).adjusted(margin, margin, -margin, -margin), message,
                         option);
    }

    QMutexLocker lock(&g_cacheMutex);
    if (g_placeholders.size() >= kMaxPlaceholderEntries)
        g_placeholders.removeFirst();
    g_placeholders.append({message, size, image});
    return image;
}

} // namespace

QImage renderPdfViewport(const PdfSource &resolved, QSize devicePx)
{
    if (devicePx.isEmpty() || devicePx.width() > kMaxSide || devicePx.height() > kMaxSide)
        return {};

    if (!pdfium::available()) {
        return placeholderImage(
            QCoreApplication::translate("PdfClipRenderer",
                                        "Install the PDF viewer addon to show this PDF"),
            devicePx);
    }
    QString error;
    if (!pdfium::ensureLoaded(&error))
        return placeholderImage(error, devicePx);

    const QFileInfo info(resolved.path);
    ResultKey key;
    key.path = resolved.path;
    key.mtimeMs = info.lastModified().toMSecsSinceEpoch();
    key.fileSize = info.size();
    key.firstPage = resolved.firstPage;
    key.lastPage = resolved.lastPage;
    key.layout = int(resolved.layout);
    key.gridColumns = resolved.gridColumns;
    key.gap = resolved.gap;
    key.scrollX = resolved.scrollX;
    key.scrollY = resolved.scrollY;
    key.zoom = resolved.zoom;
    key.width = devicePx.width();
    key.height = devicePx.height();

    {
        QMutexLocker lock(&g_cacheMutex);
        for (int i = 0; i < g_results.size(); ++i) {
            if (g_results.at(i).key == key) {
                g_results.move(i, g_results.size() - 1);
                return g_results.last().image;
            }
        }
    }

    const auto document = pdfium::openDocument(resolved.path);
    if (!document) {
        return placeholderImage(
            QCoreApplication::translate("PdfClipRenderer", "Can't open this PDF"), devicePx);
    }

    PdfSource source = resolved;
    if (source.pageCount <= 0 || source.pageSizes.size() != source.pageCount) {
        source.pageCount = pdfium::pageCount(*document);
        source.pageSizes.clear();
        for (int i = 0; i < source.pageCount; ++i)
            source.pageSizes.append(pdfium::pageSize(*document, i));
    }

    QImage image(devicePx, QImage::Format_RGBA8888);
    image.fill(Qt::transparent);
    const QRectF bounds(image.rect());
    for (const PdfPagePlacement &placement : layoutPdfPages(source, QSizeF(devicePx))) {
        const QSizeF pageSize = source.pageSizes.value(placement.pageIndex);
        if (pageSize.isEmpty())
            continue;
        const QRectF &r = placement.rect;
        // FPDF_RenderPageBitmapWithMatrix applies the matrix to the page already placed at its
        // natural size in y-down device space with its top-left at the origin.
        const pdfium::Matrix matrix{float(r.width() / pageSize.width()), 0.f, 0.f,
                                    float(r.height() / pageSize.height()), float(r.left()),
                                    float(r.top())};
        pdfium::renderPage(*document, placement.pageIndex, matrix, r.intersected(bounds), image);
    }

    QMutexLocker lock(&g_cacheMutex);
    if (g_results.size() >= kMaxResultEntries)
        g_results.removeFirst();
    g_results.append({key, image});
    return image;
}

bool probePdfSource(PdfSource &source)
{
    if (!pdfium::available() || !pdfium::ensureLoaded())
        return false;
    const auto document = pdfium::openDocument(source.path);
    if (!document)
        return false;
    const int count = pdfium::pageCount(*document);
    if (count <= 0)
        return false;
    source.pageCount = count;
    source.pageSizes.clear();
    for (int i = 0; i < count; ++i)
        source.pageSizes.append(pdfium::pageSize(*document, i));
    return true;
}

void clearPdfRenderCaches()
{
    QMutexLocker lock(&g_cacheMutex);
    g_results.clear();
    g_placeholders.clear();
}

} // namespace drift
