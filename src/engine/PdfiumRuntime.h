#pragma once

#include <QImage>
#include <QRectF>
#include <QSizeF>
#include <QString>

#include <memory>

// Loading PDFium, which Drift does not link. Same arrangement as OrtRuntime: the "pdfium" addon
// installs the library and the app dlopens it, declaring the handful of C entry points it uses
// locally instead of vendoring headers.
//
// PDFium is not thread-safe, so every call into it — opening, closing, page access, rendering — is
// serialized on one process-wide mutex taken inside this API. Callers on any thread need no locking
// of their own.
namespace drift::pdfium {

inline constexpr char kPdfiumKind[] = "pdfium";

// Idempotent. Not finding an installed library is reported but not remembered, so installing the
// addon later works without a restart; a library that is present but fails to load or lacks an
// entry point is sticky.
bool ensureLoaded(QString *error = nullptr);

// Whether a library is installed or already loaded, without loading it.
bool available();

// True once a library has been loaded into the process; it stays loaded, so an addon change after
// this point only takes effect on the next launch.
bool loaded();

class Document;

// Documents are cached by canonical path, modification time and size, so asking again for the same
// file is cheap. Null when PDFium is not loadable or the file cannot be opened.
std::shared_ptr<Document> openDocument(const QString &path, QString *error = nullptr);

int pageCount(const Document &document);

// In points, for a 0-based page index. Empty on failure.
QSizeF pageSize(const Document &document, int pageIndex);

struct Matrix
{
    float a, b, c, d, e, f;
};

// Renders onto `target`, which must be QImage::Format_RGBA8888, in place. The clip is in target
// pixels and is first filled with opaque white, so the result is a page on paper. Annotations are
// not drawn.
bool renderPage(const Document &document, int pageIndex, const Matrix &pageToTarget,
                const QRectF &clip, QImage &target);

} // namespace drift::pdfium
