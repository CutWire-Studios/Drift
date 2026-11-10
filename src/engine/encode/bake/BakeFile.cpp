#include "engine/encode/bake/BakeFile.h"

#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUuid>
#include <QtEndian>

#include <zstd.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <set>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace drift {
namespace {

constexpr qint64 kHeaderSize = 4096;
constexpr char kMagic[8] = {'D', 'R', 'I', 'F', 'T', 'B', 'A', 'K'};
constexpr quint32 kVersion = 1;
constexpr qint64 kSlotBase = 64;
constexpr qint64 kSlotSize = 32;
constexpr quint32 kIndexSchema = 1;

quint32 crc32(const char *data, qsizetype n, quint32 seed = 0)
{
    static const std::array<quint32, 256> table = [] {
        std::array<quint32, 256> t{};
        for (quint32 i = 0; i < 256; ++i) {
            quint32 c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    quint32 c = ~seed;
    for (qsizetype i = 0; i < n; ++i)
        c = table[(c ^ static_cast<quint8>(data[i])) & 0xFF] ^ (c >> 8);
    return ~c;
}

struct Slot
{
    quint64 generation = 0;
    quint64 indexOffset = 0;
    quint64 indexSize = 0;
    quint32 indexCrc = 0;
    bool valid = false;
};

QByteArray encodeSlot(const Slot &s)
{
    QByteArray b(kSlotSize, '\0');
    uchar *p = reinterpret_cast<uchar *>(b.data());
    qToLittleEndian<quint64>(s.generation, p);
    qToLittleEndian<quint64>(s.indexOffset, p + 8);
    qToLittleEndian<quint64>(s.indexSize, p + 16);
    qToLittleEndian<quint32>(s.indexCrc, p + 24);
    qToLittleEndian<quint32>(crc32(b.constData(), 28), p + 28);
    return b;
}

Slot decodeSlot(const QByteArray &b)
{
    Slot s;
    if (b.size() != kSlotSize)
        return s;
    const uchar *p = reinterpret_cast<const uchar *>(b.constData());
    if (qFromLittleEndian<quint32>(p + 28) != crc32(b.constData(), 28))
        return s;
    s.generation = qFromLittleEndian<quint64>(p);
    s.indexOffset = qFromLittleEndian<quint64>(p + 8);
    s.indexSize = qFromLittleEndian<quint64>(p + 16);
    s.indexCrc = qFromLittleEndian<quint32>(p + 24);
    s.valid = s.generation > 0 && s.indexSize > 0;
    return s;
}

QByteArray headerBytes()
{
    QByteArray h(kHeaderSize, '\0');
    memcpy(h.data(), kMagic, 8);
    qToLittleEndian<quint32>(kVersion, reinterpret_cast<uchar *>(h.data()) + 8);
    return h;
}

QByteArray serializeIndex(const BakeIndex &index)
{
    QByteArray raw;
    QDataStream out(&raw, QIODevice::WriteOnly);
    out.setVersion(QDataStream::Qt_6_0);
    out << kIndexSchema << qint32(index.quality) << quint32(index.bakes.size());
    for (const Bake &b : index.bakes) {
        out << b.id << b.startFrame << b.endFrame << qint32(b.fps) << qint32(b.width)
            << qint32(b.height) << qint32(b.preset) << b.extradata << b.fingerprint
            << b.createdAtMs << quint32(b.gops.size());
        for (const BakeGop &g : b.gops)
            out << g.firstFrame << g.offset << g.size << g.packetSizes;
    }
    QByteArray comp(qsizetype(ZSTD_compressBound(size_t(raw.size()))), '\0');
    const size_t n = ZSTD_compress(comp.data(), size_t(comp.size()), raw.constData(),
                                   size_t(raw.size()), 3);
    if (ZSTD_isError(n))
        return {};
    comp.resize(qsizetype(n));
    return comp;
}

bool parseIndex(const QByteArray &comp, quint64 fileSize, BakeIndex &out)
{
    const unsigned long long rawSize = ZSTD_getFrameContentSize(comp.constData(), size_t(comp.size()));
    if (rawSize == ZSTD_CONTENTSIZE_ERROR || rawSize == ZSTD_CONTENTSIZE_UNKNOWN
        || rawSize > (1ull << 30))
        return false;
    QByteArray raw(qsizetype(rawSize), '\0');
    const size_t got = ZSTD_decompress(raw.data(), size_t(raw.size()), comp.constData(),
                                       size_t(comp.size()));
    if (ZSTD_isError(got) || got != size_t(raw.size()))
        return false;

    QDataStream in(raw);
    in.setVersion(QDataStream::Qt_6_0);
    quint32 schema = 0, count = 0;
    qint32 quality = 1;
    in >> schema >> quality >> count;
    if (in.status() != QDataStream::Ok || schema != kIndexSchema)
        return false;
    out.quality = quality;
    out.bakes.clear();
    for (quint32 i = 0; i < count; ++i) {
        Bake b;
        qint32 fps, w, h, preset;
        quint32 gopCount = 0;
        in >> b.id >> b.startFrame >> b.endFrame >> fps >> w >> h >> preset >> b.extradata
            >> b.fingerprint >> b.createdAtMs >> gopCount;
        if (in.status() != QDataStream::Ok || gopCount > quint32(raw.size()))
            return false;
        b.fps = fps;
        b.width = w;
        b.height = h;
        b.preset = preset;
        b.gops.resize(gopCount);
        for (BakeGop &g : b.gops) {
            in >> g.firstFrame >> g.offset >> g.size >> g.packetSizes;
            if (in.status() != QDataStream::Ok || g.offset < quint64(kHeaderSize)
                || g.offset + g.size > fileSize)
                return false;
        }
        out.bakes.push_back(std::move(b));
    }
    return true;
}

void collectBlocks(const BakeIndex &index, std::map<quint64, quint64> &blocks)
{
    for (const Bake &b : index.bakes)
        for (const BakeGop &g : b.gops)
            blocks[g.offset] = g.size;
}

bool gopOverlaps(const BakeGop &g, qint64 a, qint64 b)
{
    return g.firstFrame < b && g.firstFrame + g.packetSizes.size() > a;
}

bool syncFile(QFile &f)
{
    if (!f.flush())
        return false;
#ifdef Q_OS_WIN
    return FlushFileBuffers(reinterpret_cast<HANDLE>(_get_osfhandle(f.handle()))) != 0;
#else
    return ::fsync(f.handle()) == 0;
#endif
}

} // namespace

const Bake *BakeIndex::bakeAt(qint64 frame) const
{
    auto it = std::upper_bound(bakes.begin(), bakes.end(), frame,
                               [](qint64 f, const Bake &b) { return f < b.startFrame; });
    if (it == bakes.begin())
        return nullptr;
    --it;
    return frame < it->endFrame ? &*it : nullptr;
}

const Bake *BakeIndex::bakeById(const QString &id) const
{
    for (const Bake &b : bakes)
        if (b.id == id)
            return &b;
    return nullptr;
}

BakeLocation BakeIndex::locate(qint64 frame) const
{
    const Bake *b = bakeAt(frame);
    if (!b)
        return {};
    auto it = std::upper_bound(b->gops.begin(), b->gops.end(), frame,
                               [](qint64 f, const BakeGop &g) { return f < g.firstFrame; });
    if (it == b->gops.begin())
        return {};
    --it;
    const qint64 idx = frame - it->firstFrame;
    if (idx >= it->packetSizes.size())
        return {};
    return {b, &*it, int(idx)};
}

bool readGop(QFile &f, const BakeGop &g, QByteArray &out)
{
    if (!f.seek(qint64(g.offset)))
        return false;
    out = f.read(g.size);
    return out.size() == qsizetype(g.size);
}

std::unique_ptr<BakeFile> BakeFile::open(const QString &path, QString *errorOut)
{
    std::unique_ptr<BakeFile> f(new BakeFile);
    f->m_path = path;
    QDir().mkpath(QFileInfo(path).absolutePath());
    if (!f->load(errorOut))
        return nullptr;
    return f;
}

BakeFile::~BakeFile() = default;

bool BakeFile::load(QString *errorOut)
{
    m_file = std::make_unique<QFile>(m_path);
    if (!m_file->open(QIODevice::ReadWrite)) {
        if (errorOut)
            *errorOut = m_file->errorString();
        return false;
    }
    m_fileSize = quint64(m_file->size());
    m_free.clear();
    m_live.clear();
    m_staged.clear();
    m_pending.clear();
    m_history.clear();
    m_working = BakeIndex();
    m_working.epoch = m_epoch;
    m_activeSlot = -1;
    m_generation = 0;

    bool headerOk = false;
    if (m_fileSize >= quint64(kHeaderSize)) {
        const QByteArray h = m_file->read(12);
        headerOk = h.size() == 12 && memcmp(h.constData(), kMagic, 8) == 0
            && qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(h.constData()) + 8)
                == kVersion;
    }
    if (!headerOk) {
        m_file->resize(0);
        if (!writeAt(0, headerBytes()) || !sync()) {
            if (errorOut)
                *errorOut = m_file->errorString();
            return false;
        }
        m_fileSize = quint64(kHeaderSize);
        m_committed = std::make_shared<const BakeIndex>(m_working);
        return true;
    }

    Slot sbs[2];
    for (int i = 0; i < 2; ++i) {
        m_file->seek(kSlotBase + i * kSlotSize);
        sbs[i] = decodeSlot(m_file->read(kSlotSize));
        if (sbs[i].valid)
            m_generation = std::max(m_generation, sbs[i].generation);
    }
    const int order[2] = {sbs[1].generation > sbs[0].generation ? 1 : 0,
                          sbs[1].generation > sbs[0].generation ? 0 : 1};
    for (int i : order) {
        const Slot &s = sbs[i];
        if (!s.valid || s.indexOffset < quint64(kHeaderSize) || s.indexOffset + s.indexSize > m_fileSize
            || s.indexSize > (1ull << 30))
            continue;
        m_file->seek(qint64(s.indexOffset));
        const QByteArray comp = m_file->read(qint64(s.indexSize));
        if (quint64(comp.size()) != s.indexSize || crc32(comp.constData(), comp.size()) != s.indexCrc)
            continue;
        BakeIndex parsed;
        if (!parseIndex(comp, m_fileSize, parsed))
            continue;
        parsed.epoch = m_epoch;
        m_working = std::move(parsed);
        m_activeSlot = i;
        collectBlocks(m_working, m_live);
        m_live[s.indexOffset] = s.indexSize;
        break;
    }

    quint64 cursor = quint64(kHeaderSize);
    for (const auto &[off, size] : m_live) {
        if (off > cursor)
            m_free[cursor] = off - cursor;
        cursor = std::max(cursor, off + size);
    }
    if (m_fileSize > cursor)
        m_free[cursor] = m_fileSize - cursor;
    m_committed = std::make_shared<const BakeIndex>(m_working);
    truncateTail();
    return true;
}

std::shared_ptr<const BakeIndex> BakeFile::snapshot() const
{
    std::lock_guard lock(m_mutex);
    return m_committed;
}

Bake *BakeFile::findBake(const QString &id)
{
    for (Bake &b : m_working.bakes)
        if (b.id == id)
            return &b;
    return nullptr;
}

QString BakeFile::beginBake(qint64 startFrame, int fps, int w, int h, int preset,
                            QByteArray extradata, QByteArray fingerprint)
{
    std::lock_guard lock(m_mutex);
    Bake b;
    b.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    b.startFrame = startFrame;
    b.endFrame = startFrame;
    b.fps = fps;
    b.width = w;
    b.height = h;
    b.preset = preset;
    b.extradata = std::move(extradata);
    b.fingerprint = std::move(fingerprint);
    b.createdAtMs = QDateTime::currentMSecsSinceEpoch();
    m_working.bakes.push_back(b);
    return b.id;
}

bool BakeFile::appendGop(const QString &bakeId, qint64 firstFrame, const QByteArray &gopBytes,
                         const QVector<quint32> &packetSizes)
{
    std::lock_guard lock(m_mutex);
    Bake *b = findBake(bakeId);
    if (!b || packetSizes.isEmpty() || gopBytes.isEmpty() || firstFrame < b->endFrame)
        return false;
    quint64 total = 0;
    for (quint32 s : packetSizes)
        total += s;
    if (total != quint64(gopBytes.size()))
        return false;
    const quint64 offset = allocate(quint64(gopBytes.size()));
    if (!writeAt(offset, gopBytes))
        return false;
    BakeGop g;
    g.firstFrame = firstFrame;
    g.offset = offset;
    g.size = quint32(gopBytes.size());
    g.packetSizes = packetSizes;
    b->gops.push_back(std::move(g));
    b->endFrame = firstFrame + packetSizes.size();
    return true;
}

bool BakeFile::removeRange(qint64 startFrame, qint64 endFrame)
{
    std::lock_guard lock(m_mutex);
    if (endFrame <= startFrame)
        return true;
    std::vector<Bake> next;
    for (Bake &b : m_working.bakes) {
        if (b.endFrame <= startFrame || b.startFrame >= endFrame) {
            next.push_back(std::move(b));
            continue;
        }
        auto piece = [&](qint64 a, qint64 z, const QString &id) {
            if (z <= a)
                return false;
            Bake p = b;
            p.id = id;
            p.startFrame = a;
            p.endFrame = z;
            p.gops.clear();
            for (const BakeGop &g : b.gops)
                if (gopOverlaps(g, a, z))
                    p.gops.push_back(g);
            if (p.gops.empty())
                return false;
            next.push_back(std::move(p));
            return true;
        };
        const bool left = piece(b.startFrame, startFrame, b.id);
        piece(endFrame, b.endFrame, left ? QUuid::createUuid().toString(QUuid::WithoutBraces) : b.id);
    }
    m_working.bakes = std::move(next);
    return true;
}

bool BakeFile::deleteBake(const QString &id)
{
    std::lock_guard lock(m_mutex);
    for (auto it = m_working.bakes.begin(); it != m_working.bakes.end(); ++it) {
        if (it->id == id) {
            m_working.bakes.erase(it);
            return true;
        }
    }
    return false;
}

bool BakeFile::setFingerprint(const QString &id, QByteArray fingerprint)
{
    std::lock_guard lock(m_mutex);
    Bake *b = findBake(id);
    if (!b)
        return false;
    b->fingerprint = std::move(fingerprint);
    return true;
}

void BakeFile::setDefaultPreset(int preset)
{
    std::lock_guard lock(m_mutex);
    m_working.quality = preset;
}

int BakeFile::defaultPreset() const
{
    std::lock_guard lock(m_mutex);
    return m_working.quality;
}

quint64 BakeFile::fileBytes() const
{
    std::lock_guard lock(m_mutex);
    return m_fileSize;
}

quint64 BakeFile::liveBytes() const
{
    std::lock_guard lock(m_mutex);
    quint64 n = 0;
    for (const auto &[off, size] : m_live)
        n += size;
    return n;
}

void BakeFile::setCrashBeforeSuperblockForTest(bool on)
{
    std::lock_guard lock(m_mutex);
    m_crashBeforeSuperblock = on;
}

bool BakeFile::writeAt(quint64 offset, const QByteArray &data)
{
    return m_file->seek(qint64(offset)) && m_file->write(data) == data.size();
}

bool BakeFile::sync()
{
    return syncFile(*m_file);
}

void BakeFile::addFree(quint64 offset, quint64 size)
{
    auto next = m_free.lower_bound(offset);
    if (next != m_free.end() && offset + size == next->first) {
        size += next->second;
        next = m_free.erase(next);
    }
    if (next != m_free.begin()) {
        auto prev = std::prev(next);
        if (prev->first + prev->second == offset) {
            prev->second += size;
            return;
        }
    }
    m_free[offset] = size;
}

void BakeFile::reclaimPending()
{
    auto expired = [](const Pending &p) {
        return std::all_of(p.owners->begin(), p.owners->end(),
                           [](const std::weak_ptr<const BakeIndex> &w) { return w.expired(); });
    };
    std::vector<Pending> keep;
    for (Pending &p : m_pending) {
        if (expired(p))
            addFree(p.offset, p.size);
        else
            keep.push_back(std::move(p));
    }
    m_pending = std::move(keep);
}

void BakeFile::truncateTail()
{
    if (m_free.empty())
        return;
    auto last = std::prev(m_free.end());
    if (last->first + last->second != m_fileSize)
        return;
    m_fileSize = last->first;
    m_free.erase(last);
    m_file->resize(qint64(m_fileSize));
}

quint64 BakeFile::allocate(quint64 size)
{
    reclaimPending();
    quint64 offset = 0;
    bool found = false;
    for (auto it = m_free.begin(); it != m_free.end(); ++it) {
        if (it->second >= size) {
            offset = it->first;
            const quint64 rest = it->second - size;
            m_free.erase(it);
            if (rest)
                m_free[offset + size] = rest;
            found = true;
            break;
        }
    }
    if (!found) {
        offset = m_fileSize;
        m_fileSize += size;
    }
    m_staged[offset] = size;
    return offset;
}

bool BakeFile::commit()
{
    std::lock_guard lock(m_mutex);
    return commitLocked();
}

bool BakeFile::commitLocked()
{
    reclaimPending();

    auto published = std::make_shared<BakeIndex>(m_working);
    published->epoch = m_epoch;
    published->bakes.erase(std::remove_if(published->bakes.begin(), published->bakes.end(),
                                          [](const Bake &b) { return b.gops.empty(); }),
                           published->bakes.end());
    std::stable_sort(published->bakes.begin(), published->bakes.end(),
                     [](const Bake &a, const Bake &b) { return a.startFrame < b.startFrame; });

    const QByteArray comp = serializeIndex(*published);
    if (comp.isEmpty())
        return false;
    const quint64 indexOffset = allocate(quint64(comp.size()));
    if (!writeAt(indexOffset, comp) || !sync())
        return false;
    if (m_crashBeforeSuperblock)
        return false;

    Slot slot;
    slot.generation = m_generation + 1;
    slot.indexOffset = indexOffset;
    slot.indexSize = quint64(comp.size());
    slot.indexCrc = crc32(comp.constData(), comp.size());
    const int target = m_activeSlot == 0 ? 1 : 0;
    if (!m_file->seek(kSlotBase + target * kSlotSize) || m_file->write(encodeSlot(slot)) != kSlotSize
        || !sync())
        return false;
    m_generation = slot.generation;
    m_activeSlot = target;

    std::map<quint64, quint64> refs;
    collectBlocks(*published, refs);
    refs[indexOffset] = slot.indexSize;

    std::shared_ptr<const BakeIndex> old = std::move(m_committed);
    m_committed = published;
    m_history.erase(std::remove_if(m_history.begin(), m_history.end(),
                                   [](const std::weak_ptr<const BakeIndex> &w) { return w.expired(); }),
                    m_history.end());
    m_history.push_back(old);
    auto owners = std::make_shared<std::vector<std::weak_ptr<const BakeIndex>>>(m_history);
    old.reset();

    for (const auto &[off, size] : m_live)
        if (!refs.count(off))
            m_pending.push_back({off, size, owners});
    for (const auto &[off, size] : m_staged)
        if (!refs.count(off))
            addFree(off, size);
    m_live = std::move(refs);
    m_staged.clear();
    reclaimPending();
    truncateTail();
    return true;
}

bool BakeFile::compactTo(const QString &partPath)
{
    std::lock_guard lock(m_mutex);
    return compactToLocked(partPath);
}

bool BakeFile::compactToLocked(const QString &partPath)
{
    QFile part(partPath);
    if (!part.open(QIODevice::ReadWrite | QIODevice::Truncate))
        return false;
    auto fail = [&] {
        part.close();
        QFile::remove(partPath);
        return false;
    };
    if (part.write(headerBytes()) != kHeaderSize)
        return fail();

    BakeIndex index = *m_committed;
    index.epoch = m_epoch + 1;
    std::map<quint64, quint64> blocks;
    collectBlocks(index, blocks);
    std::map<quint64, quint64> moved;
    quint64 cursor = quint64(kHeaderSize);
    QByteArray buf;
    for (const auto &[off, size] : blocks) {
        BakeGop probe;
        probe.offset = off;
        probe.size = quint32(size);
        if (!readGop(*m_file, probe, buf) || part.write(buf) != buf.size())
            return fail();
        moved[off] = cursor;
        cursor += size;
    }
    for (Bake &b : index.bakes)
        for (BakeGop &g : b.gops)
            g.offset = moved.at(g.offset);

    const QByteArray comp = serializeIndex(index);
    if (comp.isEmpty() || part.write(comp) != comp.size())
        return fail();
    Slot slot;
    slot.generation = 1;
    slot.indexOffset = cursor;
    slot.indexSize = quint64(comp.size());
    slot.indexCrc = crc32(comp.constData(), comp.size());
    if (!part.seek(kSlotBase) || part.write(encodeSlot(slot)) != kSlotSize || !syncFile(part))
        return fail();
    part.close();
    return true;
}

bool BakeFile::compact()
{
    std::lock_guard lock(m_mutex);
    if (!commitLocked())
        return false;
    const QString part = m_path + QStringLiteral(".part");
    if (!compactToLocked(part))
        return false;

    m_file->close();
    std::error_code ec;
    std::filesystem::rename(std::filesystem::path(part.toStdU16String()), std::filesystem::path(m_path.toStdU16String()), ec);
    if (ec)
        QFile::remove(part);
    else
        ++m_epoch;
    return load(nullptr) && !ec;
}

} // namespace drift
