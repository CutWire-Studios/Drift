#include "PdfiumRuntime.h"

#include "GpuPackageParse.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QList>
#include <QMutex>

#include <type_traits>
#include <utility>

#ifdef _WIN32
#    include <windows.h>
#else
#    include <dlfcn.h>
#endif

namespace drift::pdfium {
namespace {

#ifdef _WIN32
#    define DRIFT_FPDF_CALL __stdcall
#else
#    define DRIFT_FPDF_CALL
#endif

using FPDF_DOCUMENT = void *;
using FPDF_PAGE = void *;
using FPDF_BITMAP = void *;

struct FPDF_FILEACCESS
{
    unsigned long m_FileLen;
    int (*m_GetBlock)(void *param, unsigned long position, unsigned char *buf, unsigned long size);
    void *m_Param;
};

struct FS_MATRIX
{
    float a, b, c, d, e, f;
};

struct FS_RECTF
{
    float left, top, right, bottom;
};

struct FS_SIZEF
{
    float width, height;
};

constexpr int kBitmapBgra = 4;
constexpr int kReverseByteOrder = 0x10;
constexpr unsigned long kWhite = 0xFFFFFFFFul;
constexpr int kMaxCachedDocuments = 8;

struct Api
{
    void(DRIFT_FPDF_CALL *InitLibrary)();
    FPDF_DOCUMENT(DRIFT_FPDF_CALL *LoadCustomDocument)(FPDF_FILEACCESS *, const char *);
    void(DRIFT_FPDF_CALL *CloseDocument)(FPDF_DOCUMENT);
    int(DRIFT_FPDF_CALL *GetPageCount)(FPDF_DOCUMENT);
    int(DRIFT_FPDF_CALL *GetPageSizeByIndexF)(FPDF_DOCUMENT, int, FS_SIZEF *);
    FPDF_PAGE(DRIFT_FPDF_CALL *LoadPage)(FPDF_DOCUMENT, int);
    void(DRIFT_FPDF_CALL *ClosePage)(FPDF_PAGE);
    FPDF_BITMAP(DRIFT_FPDF_CALL *BitmapCreateEx)(int, int, int, void *, int);
    int(DRIFT_FPDF_CALL *BitmapFillRect)(FPDF_BITMAP, int, int, int, int, unsigned long);
    void(DRIFT_FPDF_CALL *BitmapDestroy)(FPDF_BITMAP);
    void(DRIFT_FPDF_CALL *RenderPageBitmapWithMatrix)(FPDF_BITMAP, FPDF_PAGE, const FS_MATRIX *,
                                                      const FS_RECTF *, int);
    unsigned long(DRIFT_FPDF_CALL *GetLastError)();
};

QMutex g_loadMutex;
enum class State { Unloaded, Loaded, Failed };
State g_state = State::Unloaded;
QString g_error;
Api g_api{};

// Guards every call into PDFium. Recursive because Document's destructor can run while a caller
// already holds it.
QRecursiveMutex g_pdfiumMutex;

QStringList libraryNames()
{
#if defined(_WIN32)
    return {QStringLiteral("pdfium.dll")};
#elif defined(__APPLE__)
    return {QStringLiteral("libpdfium.dylib")};
#else
    return {QStringLiteral("libpdfium.so")};
#endif
}

QString findLibrary(const QString &dir)
{
    for (const QString &name : libraryNames()) {
        const QString path = QDir(dir).filePath(name);
        if (QFileInfo::exists(path))
            return path;
    }
    return {};
}

QStringList candidates()
{
    QStringList found;
    const QStringList roots = GpuPackageParse::defaultSearchPaths(
        QStringLiteral("DRIFT_PDFIUM_DIR"), QStringLiteral("pdfium"),
        QString::fromLatin1(kPdfiumKind));
    for (const QString &root : roots) {
        QString lib = findLibrary(QDir(root).filePath(QStringLiteral("lib")));
        if (lib.isEmpty())
            lib = findLibrary(root);
        if (!lib.isEmpty())
            found.append(lib);
    }
    return found;
}

int DRIFT_FPDF_CALL readBlock(void *param, unsigned long position, unsigned char *buf,
                              unsigned long size)
{
    auto *file = static_cast<QFile *>(param);
    if (!file->seek(static_cast<qint64>(position)))
        return 0;
    return file->read(reinterpret_cast<char *>(buf), static_cast<qint64>(size))
                   == static_cast<qint64>(size)
               ? 1
               : 0;
}

struct CacheEntry
{
    QString path;
    QDateTime modified;
    qint64 size;
    std::shared_ptr<Document> document;
};

QMutex g_cacheMutex;
QList<CacheEntry> g_cache;

} // namespace

class Document
{
public:
    QFile file;
    FPDF_FILEACCESS access{};
    FPDF_DOCUMENT handle = nullptr;

    ~Document()
    {
        QMutexLocker lock(&g_pdfiumMutex);
        if (handle)
            g_api.CloseDocument(handle);
    }
};

bool ensureLoaded(QString *error)
{
    QMutexLocker lock(&g_loadMutex);
    if (g_state != State::Unloaded) {
        if (error)
            *error = g_error;
        return g_state == State::Loaded;
    }

    const auto fail = [&](const QString &message, bool sticky) {
        if (sticky) {
            g_state = State::Failed;
            g_error = message;
        }
        if (error)
            *error = message;
        return false;
    };

    const QStringList found = candidates();
    if (found.isEmpty()) {
        return fail(QStringLiteral("PDFium is not installed. Install it from the Addon Manager, or "
                                   "point DRIFT_PDFIUM_DIR at an extracted pdfium release."),
                    false);
    }
    const QString &libPath = found.first();

#ifdef _WIN32
    HMODULE handle = LoadLibraryW(libPath.toStdWString().c_str());
    if (!handle)
        return fail(QStringLiteral("Cannot load %1 (error %2)").arg(libPath).arg(GetLastError()),
                    true);
    const auto symbol = [&](const char *name) { return reinterpret_cast<void *>(GetProcAddress(handle, name)); };
#else
    void *handle = dlopen(libPath.toLocal8Bit().constData(), RTLD_NOW | RTLD_LOCAL);
    if (!handle)
        return fail(QStringLiteral("Cannot load %1 (%2)")
                        .arg(libPath, QString::fromLocal8Bit(dlerror())),
                    true);
    const auto symbol = [&](const char *name) { return dlsym(handle, name); };
#endif

    Api api{};
    const char *missing = nullptr;
    const auto bind = [&](auto &slot, const char *name) {
        slot = reinterpret_cast<std::remove_reference_t<decltype(slot)>>(symbol(name));
        if (!slot && !missing)
            missing = name;
    };
    bind(api.InitLibrary, "FPDF_InitLibrary");
    bind(api.LoadCustomDocument, "FPDF_LoadCustomDocument");
    bind(api.CloseDocument, "FPDF_CloseDocument");
    bind(api.GetPageCount, "FPDF_GetPageCount");
    bind(api.GetPageSizeByIndexF, "FPDF_GetPageSizeByIndexF");
    bind(api.LoadPage, "FPDF_LoadPage");
    bind(api.ClosePage, "FPDF_ClosePage");
    bind(api.BitmapCreateEx, "FPDFBitmap_CreateEx");
    bind(api.BitmapFillRect, "FPDFBitmap_FillRect");
    bind(api.BitmapDestroy, "FPDFBitmap_Destroy");
    bind(api.RenderPageBitmapWithMatrix, "FPDF_RenderPageBitmapWithMatrix");
    bind(api.GetLastError, "FPDF_GetLastError");
    if (missing) {
        return fail(QStringLiteral("%1 is not a usable PDFium library (missing %2)")
                        .arg(libPath, QString::fromLatin1(missing)),
                    true);
    }

    {
        QMutexLocker pdfium(&g_pdfiumMutex);
        api.InitLibrary();
    }
    g_api = api;
    g_state = State::Loaded;
    qInfo("[pdfium] loaded %s", qUtf8Printable(libPath));
    return true;
}

bool available()
{
    QMutexLocker lock(&g_loadMutex);
    if (g_state == State::Loaded)
        return true;
    return !candidates().isEmpty();
}

bool loaded()
{
    QMutexLocker lock(&g_loadMutex);
    return g_state == State::Loaded;
}

std::shared_ptr<Document> openDocument(const QString &path, QString *error)
{
    if (!ensureLoaded(error))
        return nullptr;

    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    if (canonical.isEmpty()) {
        if (error)
            *error = QStringLiteral("Cannot find %1").arg(path);
        return nullptr;
    }
    const QDateTime modified = info.lastModified();
    const qint64 size = info.size();

    QMutexLocker cacheLock(&g_cacheMutex);
    for (int i = 0; i < g_cache.size(); ++i) {
        const CacheEntry &entry = g_cache.at(i);
        if (entry.path == canonical && entry.modified == modified && entry.size == size) {
            g_cache.move(i, 0);
            return g_cache.first().document;
        }
    }

    auto document = std::make_shared<Document>();
    document->file.setFileName(canonical);
    if (!document->file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("Cannot open %1: %2").arg(path, document->file.errorString());
        return nullptr;
    }
    document->access.m_FileLen = static_cast<unsigned long>(document->file.size());
    document->access.m_GetBlock = readBlock;
    document->access.m_Param = &document->file;

    {
        QMutexLocker lock(&g_pdfiumMutex);
        document->handle = g_api.LoadCustomDocument(&document->access, nullptr);
        if (!document->handle) {
            if (error) {
                *error = QStringLiteral("PDFium could not open %1 (error %2)")
                             .arg(path)
                             .arg(g_api.GetLastError());
            }
            return nullptr;
        }
    }

    g_cache.prepend({canonical, modified, size, document});
    while (g_cache.size() > kMaxCachedDocuments)
        g_cache.removeLast();
    return document;
}

int pageCount(const Document &document)
{
    QMutexLocker lock(&g_pdfiumMutex);
    return g_api.GetPageCount(document.handle);
}

QSizeF pageSize(const Document &document, int pageIndex)
{
    QMutexLocker lock(&g_pdfiumMutex);
    FS_SIZEF size{};
    if (!g_api.GetPageSizeByIndexF(document.handle, pageIndex, &size))
        return {};
    return {size.width, size.height};
}

bool renderPage(const Document &document, int pageIndex, const Matrix &pageToTarget,
                const QRectF &clip, QImage &target)
{
    if (target.format() != QImage::Format_RGBA8888 || target.isNull())
        return false;

    const QRect bounds = clip.toAlignedRect().intersected(target.rect());
    if (bounds.isEmpty())
        return false;

    QMutexLocker lock(&g_pdfiumMutex);
    FPDF_PAGE page = g_api.LoadPage(document.handle, pageIndex);
    if (!page)
        return false;

    FPDF_BITMAP bitmap = g_api.BitmapCreateEx(target.width(), target.height(), kBitmapBgra,
                                              target.bits(), static_cast<int>(target.bytesPerLine()));
    if (!bitmap) {
        g_api.ClosePage(page);
        return false;
    }

    g_api.BitmapFillRect(bitmap, bounds.x(), bounds.y(), bounds.width(), bounds.height(), kWhite);
    const FS_MATRIX matrix{pageToTarget.a, pageToTarget.b, pageToTarget.c,
                           pageToTarget.d, pageToTarget.e, pageToTarget.f};
    const FS_RECTF rect{static_cast<float>(bounds.left()), static_cast<float>(bounds.top()),
                        static_cast<float>(bounds.right() + 1), static_cast<float>(bounds.bottom() + 1)};
    g_api.RenderPageBitmapWithMatrix(bitmap, page, &matrix, &rect, kReverseByteOrder);

    g_api.BitmapDestroy(bitmap);
    g_api.ClosePage(page);
    return true;
}

} // namespace drift::pdfium
