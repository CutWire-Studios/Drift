#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

#include <map>
#include <memory>
#include <mutex>
#include <vector>

class QFile;

namespace drift {

// One group of pictures: a keyframe plus the P-frame packets that follow it, stored back to back.
// It covers project frames [firstFrame, firstFrame + packetSizes.size()). The GOP is the unit of
// allocation, so freeing a bake frees whole GOPs.
struct BakeGop
{
    qint64 firstFrame = 0;
    quint64 offset = 0;
    quint32 size = 0;
    QVector<quint32> packetSizes;
};

// A contiguous baked range at project fps. After a split, its GOPs may reach outside
// [startFrame, endFrame): those frames are still needed to decode the ones inside.
struct Bake
{
    QString id;
    qint64 startFrame = 0;
    qint64 endFrame = 0; // half-open
    int fps = 0;
    int width = 0;
    int height = 0;
    int preset = 1; // 0 compact, 1 balanced, 2 high
    QByteArray extradata;
    QByteArray fingerprint;
    qint64 createdAtMs = 0;
    std::vector<BakeGop> gops; // sorted by firstFrame, not overlapping
};

struct BakeLocation
{
    const Bake *bake = nullptr;
    const BakeGop *gop = nullptr;
    int indexInGop = 0;
    explicit operator bool() const { return bake && gop; }
};

// Immutable committed state. Readers keep the shared_ptr for as long as they use its offsets; the
// file never reuses space such a snapshot references.
class BakeIndex
{
public:
    int quality = 1; // preset for new bakes
    // Bumped when compaction moves every GOP. A reader whose file handle was opened under an older
    // epoch must reopen the file before using offsets from this snapshot.
    quint64 epoch = 0;
    std::vector<Bake> bakes; // sorted by startFrame

    const Bake *bakeAt(qint64 frame) const;
    const Bake *bakeById(const QString &id) const;
    BakeLocation locate(qint64 frame) const;
};

// Reads one GOP's bytes through the caller's own handle, so concurrent readers never share a seek
// position. `f` must be open for reading on the bake file.
bool readGop(QFile &f, const BakeGop &g, QByteArray &out);

// The per-project bake container (.driftbake): a heap of GOP blobs, a zstd-compressed index, and
// two alternating superblock slots in a 4 KiB header. Commits write the index into free space,
// fsync, then flip the slot with the lower generation, so a crash at any point leaves the previous
// index intact. Free space is derived on open (file minus everything the index references), so
// orphaned writes are reclaimed automatically.
//
// One writer; every method below is serialised by an internal mutex. Readers use snapshot() plus
// readGop() on their own QFile.
class BakeFile
{
public:
    // Opens, or creates when missing, empty or from another format version (this is cache data, so
    // an unreadable file is discarded rather than reported).
    static std::unique_ptr<BakeFile> open(const QString &path, QString *errorOut = nullptr);
    ~BakeFile();

    const QString &path() const { return m_path; }

    // Last committed state.
    std::shared_ptr<const BakeIndex> snapshot() const;

    // Starts an uncommitted bake, empty until appendGop. Returns its id, or an empty string on failure.
    QString beginBake(qint64 startFrame, int fps, int w, int h, int preset, QByteArray extradata,
                      QByteArray fingerprint);
    // `gopBytes` is the concatenation of the packets in `packetSizes`. firstFrame must not precede
    // the bake's current end. Extends the bake to cover the GOP. Not visible to snapshot() until commit().
    bool appendGop(const QString &bakeId, qint64 firstFrame, const QByteArray &gopBytes,
                   const QVector<quint32> &packetSizes);
    // Publishes the working state. Bakes without GOPs are not published.
    bool commit();

    // Trims or splits every bake overlapping [startFrame, endFrame). A split gives the right part a
    // new id. GOPs no remaining range needs are released at the next commit().
    bool removeRange(qint64 startFrame, qint64 endFrame);
    bool deleteBake(const QString &id);
    bool setFingerprint(const QString &id, QByteArray fingerprint);
    void setDefaultPreset(int preset);
    int defaultPreset() const;

    quint64 fileBytes() const;
    // Bytes referenced by the committed index (GOPs and the index blob).
    quint64 liveBytes() const;

    // Commits, then rewrites only the live GOPs into <path>.part, renames it over the file and
    // reopens it. snapshot() afterwards carries a new epoch: readers reopen their QFile and take a
    // fresh snapshot. On Windows the rename fails while readers hold handles; the original stays
    // intact and false is returned.
    bool compact();
    // Writes a compacted copy of the committed state to `partPath` without touching this file.
    // To finish by hand: close every handle, rename `partPath` over path(), construct a new BakeFile.
    bool compactTo(const QString &partPath);

    // Makes the next commit() write its index but stop before the superblock, as a crash would.
    void setCrashBeforeSuperblockForTest(bool on);

    BakeFile(const BakeFile &) = delete;
    BakeFile &operator=(const BakeFile &) = delete;

private:
    BakeFile() = default;

    struct Pending
    {
        quint64 offset;
        quint64 size;
        std::shared_ptr<std::vector<std::weak_ptr<const BakeIndex>>> owners;
    };

    bool load(QString *errorOut);
    bool commitLocked();
    bool compactToLocked(const QString &partPath);
    quint64 allocate(quint64 size);
    void addFree(quint64 offset, quint64 size);
    void reclaimPending();
    void truncateTail();
    bool writeAt(quint64 offset, const QByteArray &data);
    bool sync();
    Bake *findBake(const QString &id);

    QString m_path;
    std::unique_ptr<QFile> m_file;
    mutable std::mutex m_mutex;
    quint64 m_fileSize = 0;
    int m_activeSlot = -1;
    quint64 m_generation = 0;
    quint64 m_epoch = 0;
    bool m_crashBeforeSuperblock = false;
    BakeIndex m_working;
    std::shared_ptr<const BakeIndex> m_committed;
    std::vector<std::weak_ptr<const BakeIndex>> m_history;
    std::map<quint64, quint64> m_free;
    std::map<quint64, quint64> m_live;
    std::map<quint64, quint64> m_staged;
    std::vector<Pending> m_pending;
};

} // namespace drift
