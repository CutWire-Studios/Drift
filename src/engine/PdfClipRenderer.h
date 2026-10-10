#pragma once

#include "core/PdfSource.h"

#include <QImage>
#include <QSize>

// Renders a Pdf clip's viewport for one instant: the pages laid out by layoutPdfPages() and drawn
// through PDFium onto a transparent RGBA8888 image. Any thread. Identical requests return the
// same QImage so the GPU texture upload is reused. Null for an empty or oversized `devicePx`.
// Without the PDF viewer addon, or when the file will not open, the result is a black image
// carrying the reason instead.

namespace drift {

// `resolved` is PdfSource::resolvedAt() for the instant being drawn.
QImage renderPdfViewport(const PdfSource &resolved, QSize devicePx);

// Fills pageCount and pageSizes from the file. False, leaving `source` untouched, when the addon
// is missing or the file will not open.
bool probePdfSource(PdfSource &source);

// After the PDF viewer addon is installed, so placeholders stop being served.
void clearPdfRenderCaches();

} // namespace drift
