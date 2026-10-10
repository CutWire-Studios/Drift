#pragma once

#include "Keyframe.h"
#include "Time.h"

#include <QJsonObject>
#include <QMap>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QStringList>
#include <QVector>

namespace drift {

// A PDF placed on a Graphic track (ClipType::Pdf). The file lives in Clip::path; the clip frame is
// fixed and the pages are seen through a viewport the size of that frame, panned and zoomed by the
// keyframeable scrollX / scrollY / zoom. Rendering lives in engine; this struct and
// layoutPdfPages() are the pure part both the renderer and the inspector share.

enum class PdfLayout { Column, Row, Grid };

QString pdfLayoutToString(PdfLayout layout);
PdfLayout pdfLayoutFromString(const QString &layout);

struct PdfSource
{
    QString path; // kept equal to Clip::path (remapProjectPaths re-points both)

    // Probed when the file was attached so layout never needs PDFium. 0 = not probed, or PDFium
    // is not installed; pageSizes are in points, one per page.
    int pageCount = 0;
    QVector<QSizeF> pageSizes;

    int firstPage = 1; // 1-based
    int lastPage = 0;  // 0 = through the last page

    PdfLayout layout = PdfLayout::Column;
    int gridColumns = 2;
    double gap = 12.0; // PDF points of the reference page; scales with it

    // Keyframeable statics. Keys in keyframes are exactly these member names.
    double scrollX = 0.0;
    double scrollY = 0.0;
    double zoom = 1.0;
    QMap<QString, KeyframeTrack<double>> keyframes; // clip-relative times

    bool isEmpty() const { return path.isEmpty(); }
    bool isAnimated() const;
    PdfSource resolvedAt(TimeUs clipTimeUs) const;

    QJsonObject toJson() const;
    static PdfSource fromJson(const QJsonObject &o);
};

// Fixed order: scrollX, scrollY, zoom.
const QStringList &pdfKeyframeProperties();
bool pdfScalar(const PdfSource &source, const QString &key, double *out);
// Clamps into the value's valid range. False for an unknown key.
bool setPdfScalar(PdfSource &source, const QString &key, double value);
QString pdfKeyframeLabel(const QString &key);

struct PdfPagePlacement
{
    int pageIndex = 0; // 0-based
    QRectF rect;       // viewport (target) pixels
};

// The pages of `src` (already resolved at a time) that show in a viewport of `viewport` pixels.
// zoom 1 fits the cross axis: page width for Column, page height for Row, the whole grid's width
// for Grid. Scroll counts pages (Column/Row) or rows/columns (Grid): scrollY = k puts the top
// edge of page firstPage + floor(k) at the viewport top and the fraction moves through that page
// plus one gap. Empty when the document is not probed.
QVector<PdfPagePlacement> layoutPdfPages(const PdfSource &src, QSizeF viewport);

} // namespace drift
