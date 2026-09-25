#include "engine/DepthSidecar.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMutex>
#include <QStandardPaths>
#include <QtEndian>

#include <zstd.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>
#include <list>

namespace drift {
namespace {

constexpr char kMagic[4] = {'D', 'R', 'D', 'P'};
constexpr quint32 kVersion = 1;
constexpr int kHeaderBytes = 40;
constexpr int kIndexEntryBytes = 28;
constexpr int kZstdLevel = 3;
// Decoded frames kept per sidecar. Playback walks forward one frame at a time and scrubbing
// revisits a few; at 392x700 this is about 4 MB.
constexpr size_t kFrameCacheSize = 8;
// A sidecar is a 16-bit map at inference resolution; anything past this is a corrupt header.
constexpr quint32 kMaxSide = 8192;

struct IndexEntry
{
    qint64 ptsUs = 0;
    quint64 offset = 0;
    quint32 bytes = 0;
    float lo = 0.0f;
    float hi = 0.0f;
};

struct Header
{
    quint32 width = 0;
    quint32 height = 0;
    quint32 frameCount = 0;
    float globalLo = 0.0f;
    float globalHi = 0.0f;
    quint64 indexOffset = 0;
};

QByteArray encodeHeader(const Header &h)
{
    QByteArray out(kHeaderBytes, '\0');
    uchar *p = reinterpret_cast<uchar *>(out.data());
    std::memcpy(p, kMagic, 4);
    qToLittleEndian<quint32>(kVersion, p + 4);
    qToLittleEndian<quint32>(h.width, p + 8);
    qToLittleEndian<quint32>(h.height, p + 12);
    qToLittleEndian<quint32>(h.frameCount, p + 16);
    qToLittleEndian<float>(h.globalLo, p + 20);
    qToLittleEndian<float>(h.globalHi, p + 24);
    qToLittleEndian<quint64>(h.indexOffset, p + 32);
    return out;
}

// The spread the reader normalises to is taken from the 1st and 99th percentiles rather than the
// extremes: a handful of pixels on a specular highlight or the sky would otherwise squeeze the
// whole subject into a sliver of the range.
void percentiles(const float *values, size_t count, float *p1, float *p99)
{
    const size_t stride = std::max<size_t>(1, count / 16384);
    std::vector<float> sample;
    sample.reserve(count / stride + 1);
    for (size_t i = 0; i < count; i += stride)
        sample.push_back(std::max(0.0f, values[i]));
    const auto at = [&](double q) {
        const size_t k = std::min(sample.size() - 1, size_t(q * double(sample.size() - 1)));
        std::nth_element(sample.begin(), sample.begin() + qsizetype(k), sample.end());
        return sample[k];
    };
    *p1 = at(0.01);
    *p99 = at(0.99);
}

} // namespace

// --- writer -------------------------------------------------------------------------------------

struct DepthSidecarWriter::Impl
{
    QFile file;
    QString path;
    QString tmpPath;
    QSize size;
    QString model;
    std::vector<IndexEntry> index;
    float globalLo = std::numeric_limits<float>::max();
    float globalHi = std::numeric_limits<float>::lowest();
    std::vector<quint16> quantised;
    QByteArray planes;
    QByteArray compressed;
    bool finished = false;
};

DepthSidecarWriter::DepthSidecarWriter()
    : d(std::make_unique<Impl>())
{
}

DepthSidecarWriter::~DepthSidecarWriter()
{
    if (!d->finished)
        abort();
}

bool DepthSidecarWriter::open(const QString &path, const QSize &size, const QString &model,
                              QString *errorOut)
{
    if (size.isEmpty() || size.width() > int(kMaxSide) || size.height() > int(kMaxSide)) {
        if (errorOut)
            *errorOut = QStringLiteral("Invalid depth map size");
        return false;
    }
    d->path = path;
    d->tmpPath = path + QStringLiteral(".part");
    d->size = size;
    d->model = model;
    d->file.setFileName(d->tmpPath);
    if (!d->file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorOut)
            *errorOut = QStringLiteral("Could not create %1").arg(d->tmpPath);
        return false;
    }
    // Placeholder; finish() rewrites it once the frame count and index offset are known.
    if (d->file.write(encodeHeader({})) != kHeaderBytes) {
        if (errorOut)
            *errorOut = QStringLiteral("Could not write %1").arg(d->tmpPath);
        return false;
    }
    return true;
}

bool DepthSidecarWriter::writeFrame(TimeUs ptsUs, const float *disparity, QString *errorOut)
{
    const int w = d->size.width();
    const int h = d->size.height();
    const size_t count = size_t(w) * size_t(h);

    float lo = std::numeric_limits<float>::max();
    float hi = 0.0f;
    for (size_t i = 0; i < count; ++i) {
        const float v = std::max(0.0f, disparity[i]);
        lo = std::min(lo, v);
        hi = std::max(hi, v);
    }
    float p1 = 0.0f;
    float p99 = 0.0f;
    percentiles(disparity, count, &p1, &p99);
    d->globalLo = std::min(d->globalLo, p1);
    d->globalHi = std::max(d->globalHi, p99);

    // Quantised over this frame's own range so every frame gets the full 16 bits.
    const float span = hi - lo;
    const float scale = span > 0.0f ? 65535.0f / span : 0.0f;
    d->quantised.resize(count);
    for (size_t i = 0; i < count; ++i) {
        const float v = std::max(0.0f, disparity[i]);
        d->quantised[i] = quint16(std::lround(std::clamp((v - lo) * scale, 0.0f, 65535.0f)));
    }

    // Depth is smooth, so neighbouring differences are small and their high bytes mostly zero;
    // splitting the planes hands zstd long runs of them.
    d->planes.resize(qsizetype(count) * 2);
    auto *high = reinterpret_cast<uchar *>(d->planes.data());
    uchar *low = high + count;
    for (int y = 0; y < h; ++y) {
        const quint16 *row = d->quantised.data() + size_t(y) * w;
        quint16 prev = 0;
        for (int x = 0; x < w; ++x) {
            const quint16 delta = quint16(row[x] - prev);
            prev = row[x];
            const size_t i = size_t(y) * w + x;
            high[i] = uchar(delta >> 8);
            low[i] = uchar(delta & 0xff);
        }
    }

    d->compressed.resize(qsizetype(ZSTD_compressBound(size_t(d->planes.size()))));
    const size_t bytes = ZSTD_compress(d->compressed.data(), size_t(d->compressed.size()),
                                       d->planes.constData(), size_t(d->planes.size()), kZstdLevel);
    if (ZSTD_isError(bytes)) {
        if (errorOut)
            *errorOut = QStringLiteral("Depth compression failed: %1")
                            .arg(QString::fromUtf8(ZSTD_getErrorName(bytes)));
        return false;
    }

    IndexEntry entry;
    entry.ptsUs = ptsUs;
    entry.offset = quint64(d->file.pos());
    entry.bytes = quint32(bytes);
    entry.lo = lo;
    entry.hi = hi;
    if (d->file.write(d->compressed.constData(), qint64(bytes)) != qint64(bytes)) {
        if (errorOut)
            *errorOut = QStringLiteral("Could not write %1").arg(d->tmpPath);
        return false;
    }
    d->index.push_back(entry);
    return true;
}

bool DepthSidecarWriter::finish(QString *errorOut)
{
    const auto fail = [&](const QString &message) {
        if (errorOut)
            *errorOut = message;
        abort();
        return false;
    };
    if (d->index.empty())
        return fail(QStringLiteral("No depth frames were written"));

    Header header;
    header.width = quint32(d->size.width());
    header.height = quint32(d->size.height());
    header.frameCount = quint32(d->index.size());
    header.globalLo = d->globalLo;
    header.globalHi = std::max(d->globalHi, d->globalLo);
    header.indexOffset = quint64(d->file.pos());

    const QByteArray model = d->model.toUtf8();
    QByteArray footer(qsizetype(d->index.size()) * kIndexEntryBytes + 4 + model.size(), '\0');
    uchar *p = reinterpret_cast<uchar *>(footer.data());
    for (const IndexEntry &e : d->index) {
        qToLittleEndian<qint64>(e.ptsUs, p);
        qToLittleEndian<quint64>(e.offset, p + 8);
        qToLittleEndian<quint32>(e.bytes, p + 16);
        qToLittleEndian<float>(e.lo, p + 20);
        qToLittleEndian<float>(e.hi, p + 24);
        p += kIndexEntryBytes;
    }
    qToLittleEndian<quint32>(quint32(model.size()), p);
    std::memcpy(p + 4, model.constData(), size_t(model.size()));

    if (d->file.write(footer) != footer.size() || !d->file.seek(0)
        || d->file.write(encodeHeader(header)) != kHeaderBytes || !d->file.flush()) {
        return fail(QStringLiteral("Could not write %1").arg(d->tmpPath));
    }
    d->file.close();

    QFile::remove(d->path);
    if (!QFile::rename(d->tmpPath, d->path))
        return fail(QStringLiteral("Could not move the depth map into place at %1").arg(d->path));
    d->finished = true;
    return true;
}

void DepthSidecarWriter::abort()
{
    if (d->file.isOpen())
        d->file.close();
    if (!d->tmpPath.isEmpty())
        QFile::remove(d->tmpPath);
    d->finished = true;
}

// --- reader -------------------------------------------------------------------------------------

struct DepthSidecar::Impl
{
    Header header;
    std::vector<IndexEntry> index;
    quint64 keyBase = 0;

    mutable QMutex mutex; // guards file and cache
    mutable QFile file;
    mutable std::list<std::pair<int, std::shared_ptr<const DepthFrame>>> cache; // most recent first

    std::shared_ptr<const DepthFrame> decode(int i) const;
};

std::shared_ptr<const DepthFrame> DepthSidecar::Impl::decode(int i) const
{
    const IndexEntry &e = index[size_t(i)];
    const int w = int(header.width);
    const int h = int(header.height);
    const size_t count = size_t(w) * size_t(h);

    if (!file.seek(qint64(e.offset)))
        return nullptr;
    const QByteArray compressed = file.read(qint64(e.bytes));
    if (compressed.size() != qsizetype(e.bytes))
        return nullptr;

    std::vector<uchar> planes(count * 2);
    const size_t got = ZSTD_decompress(planes.data(), planes.size(), compressed.constData(),
                                       size_t(compressed.size()));
    if (ZSTD_isError(got) || got != planes.size())
        return nullptr;

    // Undo the delta coding and this frame's quantisation, then map onto the clip-wide range.
    const float frameScale = (e.hi - e.lo) / 65535.0f;
    const float globalSpan = header.globalHi - header.globalLo;
    const float toNorm = globalSpan > 0.0f ? 65535.0f / globalSpan : 0.0f;

    auto frame = std::make_shared<DepthFrame>();
    frame->size = QSize(w, h);
    frame->key = keyBase | quint64(i);
    frame->values.resize(count);
    const uchar *high = planes.data();
    const uchar *low = high + count;
    for (int y = 0; y < h; ++y) {
        quint16 value = 0;
        for (int x = 0; x < w; ++x) {
            const size_t k = size_t(y) * w + x;
            value = quint16(value + quint16((high[k] << 8) | low[k]));
            const float disparity = e.lo + float(value) * frameScale;
            const float n = (disparity - header.globalLo) * toNorm;
            frame->values[k] = quint16(std::lround(std::clamp(n, 0.0f, 65535.0f)));
        }
    }
    return frame;
}

DepthSidecar::DepthSidecar()
    : d(std::make_unique<Impl>())
{
}

DepthSidecar::~DepthSidecar() = default;

std::shared_ptr<const DepthSidecar> DepthSidecar::open(const QString &path, QString *errorOut)
{
    const auto fail = [&](const QString &message) -> std::shared_ptr<const DepthSidecar> {
        if (errorOut)
            *errorOut = message;
        return nullptr;
    };

    std::shared_ptr<DepthSidecar> sidecar(new DepthSidecar());
    Impl &d = *sidecar->d;
    d.file.setFileName(path);
    if (!d.file.open(QIODevice::ReadOnly))
        return fail(QStringLiteral("Could not open %1").arg(path));

    const QByteArray raw = d.file.read(kHeaderBytes);
    if (raw.size() != kHeaderBytes || std::memcmp(raw.constData(), kMagic, 4) != 0)
        return fail(QStringLiteral("%1 is not a depth map").arg(path));
    const uchar *p = reinterpret_cast<const uchar *>(raw.constData());
    if (qFromLittleEndian<quint32>(p + 4) != kVersion)
        return fail(QStringLiteral("%1 was written by a newer version of Drift").arg(path));
    d.header.width = qFromLittleEndian<quint32>(p + 8);
    d.header.height = qFromLittleEndian<quint32>(p + 12);
    d.header.frameCount = qFromLittleEndian<quint32>(p + 16);
    d.header.globalLo = qFromLittleEndian<float>(p + 20);
    d.header.globalHi = qFromLittleEndian<float>(p + 24);
    d.header.indexOffset = qFromLittleEndian<quint64>(p + 32);

    // A zero frame count is what an unfinished file's placeholder header says.
    if (d.header.width == 0 || d.header.height == 0 || d.header.width > kMaxSide
        || d.header.height > kMaxSide || d.header.frameCount == 0) {
        return fail(QStringLiteral("%1 is incomplete or corrupt").arg(path));
    }

    const qint64 indexBytes = qint64(d.header.frameCount) * kIndexEntryBytes;
    if (!d.file.seek(qint64(d.header.indexOffset)))
        return fail(QStringLiteral("%1 is incomplete or corrupt").arg(path));
    const QByteArray index = d.file.read(indexBytes);
    if (index.size() != indexBytes)
        return fail(QStringLiteral("%1 is incomplete or corrupt").arg(path));

    d.index.resize(d.header.frameCount);
    const uchar *q = reinterpret_cast<const uchar *>(index.constData());
    for (IndexEntry &e : d.index) {
        e.ptsUs = qFromLittleEndian<qint64>(q);
        e.offset = qFromLittleEndian<quint64>(q + 8);
        e.bytes = qFromLittleEndian<quint32>(q + 16);
        e.lo = qFromLittleEndian<float>(q + 20);
        e.hi = qFromLittleEndian<float>(q + 24);
        if (e.offset + e.bytes > d.header.indexOffset)
            return fail(QStringLiteral("%1 is incomplete or corrupt").arg(path));
        q += kIndexEntryBytes;
    }

    static std::atomic<quint64> nextInstance{1};
    d.keyBase = nextInstance.fetch_add(1) << 32;
    return sidecar;
}

QSize DepthSidecar::size() const
{
    return QSize(int(d->header.width), int(d->header.height));
}

int DepthSidecar::frameCount() const
{
    return int(d->index.size());
}

TimeUs DepthSidecar::ptsAt(int index) const
{
    return d->index[size_t(index)].ptsUs;
}

std::shared_ptr<const DepthFrame> DepthSidecar::frameAt(TimeUs sourceUs) const
{
    const auto it = std::lower_bound(d->index.cbegin(), d->index.cend(), sourceUs,
                                     [](const IndexEntry &e, TimeUs t) { return e.ptsUs < t; });
    int i = int(it - d->index.cbegin());
    if (i == int(d->index.size()))
        i = int(d->index.size()) - 1;
    else if (i > 0 && sourceUs - d->index[size_t(i - 1)].ptsUs < it->ptsUs - sourceUs)
        --i;

    QMutexLocker lock(&d->mutex);
    for (auto c = d->cache.begin(); c != d->cache.end(); ++c) {
        if (c->first == i) {
            d->cache.splice(d->cache.begin(), d->cache, c);
            return d->cache.front().second;
        }
    }
    std::shared_ptr<const DepthFrame> frame = d->decode(i);
    if (!frame)
        return nullptr;
    d->cache.emplace_front(i, frame);
    if (d->cache.size() > kFrameCacheSize)
        d->cache.pop_back();
    return frame;
}

double DepthSidecar::sample(TimeUs sourceUs, double nx, double ny) const
{
    const std::shared_ptr<const DepthFrame> frame = frameAt(sourceUs);
    if (!frame)
        return 0.0;
    const int w = frame->size.width();
    const int h = frame->size.height();
    const double fx = std::clamp(nx * w - 0.5, 0.0, double(w - 1));
    const double fy = std::clamp(ny * h - 0.5, 0.0, double(h - 1));
    const int x0 = int(fx);
    const int y0 = int(fy);
    const int x1 = std::min(x0 + 1, w - 1);
    const int y1 = std::min(y0 + 1, h - 1);
    const double tx = fx - x0;
    const double ty = fy - y0;
    const auto v = [&](int x, int y) { return double(frame->values[size_t(y) * w + x]); };
    const double top = v(x0, y0) * (1.0 - tx) + v(x1, y0) * tx;
    const double bottom = v(x0, y1) * (1.0 - tx) + v(x1, y1) * tx;
    return (top * (1.0 - ty) + bottom * ty) / 65535.0;
}

namespace {

struct CachedSidecar
{
    QDateTime modified;
    qint64 size = 0;
    std::shared_ptr<const DepthSidecar> sidecar;
};

QMutex g_cacheMutex;
QHash<QString, CachedSidecar> g_cache;

} // namespace

std::shared_ptr<const DepthSidecar> loadDepthSidecarCached(const QString &path)
{
    if (path.isEmpty())
        return nullptr;
    const QFileInfo info(path);
    if (!info.exists())
        return nullptr;

    QMutexLocker lock(&g_cacheMutex);
    const auto it = g_cache.constFind(path);
    if (it != g_cache.cend() && it->modified == info.lastModified() && it->size == info.size())
        return it->sidecar;

    QString error;
    std::shared_ptr<const DepthSidecar> sidecar = DepthSidecar::open(path, &error);
    if (!sidecar)
        qWarning("[depth] %s", qUtf8Printable(error));
    g_cache.insert(path, {info.lastModified(), info.size(), sidecar});
    return sidecar;
}

QString depthCacheDir()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (base.isEmpty())
        return {};
    const QString dir = QDir(base).filePath(QStringLiteral("depth"));
    QDir().mkpath(dir);
    return dir;
}

} // namespace drift
