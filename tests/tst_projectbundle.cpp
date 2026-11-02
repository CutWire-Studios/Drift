#include "core/project/media/ImageSequence.h"
#include "engine/project/ProjectBundle.h"
#include "engine/project/ProjectDependencies.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

#include <zstd.h>

using namespace drift::bundle;

// The bundle is written from scratch here rather than from a fixture: the writer is the only thing
// that produces the format, so a fixture would only ever prove the reader agrees with itself.

class TestProjectBundle : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void roundTripsMixedStorage();
    void dedupesRepeatedPaths();
    void keepsDocumentResourceLayout();
    void compressesTextButNotMedia();
    void longRangeMatchesSpanTheManifest();
    void rejectsImplausibleManifestSize();
    void skipsAlreadyExtractedBlobs();
    void degradesMissingMediaToReference();
    void rejectsNewerMajorVersion();
    void rejectsCorruptBlob();
    void rejectsCorruptManifest();
    void rejectsBadMagic();
    void cancellingWriteKeepsPreviousFile();
    void collectsIntoTypedFolders();
    void collectReusesSameBytesAndSuffixesOthers();
    void collectMoveDeletesOriginals();
    void cancelledCollectMoveRestoresOriginals();
    void packagesImageSequenceFrames();
    void collectsImageSequenceIntoOneFolder();

private:
    QString writeSource(const QString &name, const QByteArray &content) const;
    // Five tiny PNGs named <dir>/render_0001.png ..; returns the sequence path for them.
    QString writeSequence(const QString &dir) const;
    // A bundle with one embedded .mp4 and one referenced .mp4, plus a face-track JSON.
    WriteRequest sampleRequest() const;
    void flipByte(const QString &path, qint64 offset) const;
    // Copy of `path` with format.version rewritten, header lengths and digest fixed up. Forging
    // the manifest is the only way to exercise the version gate from outside the writer.
    QString restamped(const QString &path, const QString &name, const QString &version) const;

    QTemporaryDir m_tmp;
};

void TestProjectBundle::init()
{
    QVERIFY(m_tmp.isValid());
}

QString TestProjectBundle::writeSource(const QString &name, const QByteArray &content) const
{
    const QString path = m_tmp.filePath(name);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return {};
    file.write(content);
    return path;
}

WriteRequest TestProjectBundle::sampleRequest() const
{
    WriteRequest request;
    request.document = QJsonObject{{QStringLiteral("version"), 3},
                                   {QStringLiteral("projectName"), QStringLiteral("Sample")}};
    request.projectId = QStringLiteral("proj-1");
    request.title = QStringLiteral("Sample");
    request.author = QStringLiteral("Tester");
    request.description = QStringLiteral("A project");
    request.createdAt = QDateTime(QDate(2026, 1, 2), QTime(3, 4, 5), QTimeZone::UTC);
    request.modifiedAt = request.createdAt;

    AddonRef addon;
    addon.id = QStringLiteral("cinematic");
    addon.version = QStringLiteral("1.2.0");
    addon.name = QStringLiteral("Cinematic Effects");
    addon.kinds = {QStringLiteral("effects")};
    request.addons = {addon};

    MediaEntry embedded;
    embedded.originalPath = writeSource(QStringLiteral("a.mp4"), QByteArray(4096, '\x01'));
    embedded.role = MediaRole::Source;
    embedded.embedded = true;

    MediaEntry referenced;
    referenced.originalPath = writeSource(QStringLiteral("b.mp4"), QByteArray(2048, '\x02'));
    referenced.role = MediaRole::Source;
    referenced.embedded = false;

    MediaEntry faceTrack;
    faceTrack.originalPath =
        writeSource(QStringLiteral("face.json"), QByteArray(8192, 'x'));
    faceTrack.role = MediaRole::FaceTrack;
    faceTrack.embedded = true;

    request.media = {embedded, referenced, faceTrack};
    return request;
}

void TestProjectBundle::flipByte(const QString &path, qint64 offset) const
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadWrite));
    QVERIFY(file.seek(offset < 0 ? file.size() + offset : offset));
    char byte = 0;
    QCOMPARE(file.read(&byte, 1), 1LL);
    byte = char(byte ^ 0xff);
    QVERIFY(file.seek(offset < 0 ? file.size() + offset : offset));
    QCOMPARE(file.write(&byte, 1), 1LL);
}

QString TestProjectBundle::restamped(const QString &path, const QString &name,
                                     const QString &version) const
{
    QFile source(path);
    if (!source.open(QIODevice::ReadOnly))
        return {};
    const QByteArray all = source.readAll();

    const auto readU32 = [&all](int at) {
        const auto *b = reinterpret_cast<const quint8 *>(all.constData() + at);
        return quint32(b[0]) | (quint32(b[1]) << 8) | (quint32(b[2]) << 16) | (quint32(b[3]) << 24);
    };
    const quint32 storedSize = readU32(12);
    const quint32 rawSize = readU32(16);

    QByteArray manifest(rawSize, Qt::Uninitialized);
    if (ZSTD_isError(ZSTD_decompress(manifest.data(), rawSize, all.constData() + 52, storedSize)))
        return {};

    QJsonObject root = QJsonDocument::fromJson(manifest).object();
    QJsonObject format = root.value(QStringLiteral("format")).toObject();
    format.insert(QStringLiteral("version"), version);
    root.insert(QStringLiteral("format"), format);
    const QByteArray patched = QJsonDocument(root).toJson(QJsonDocument::Compact);

    QByteArray packed(qsizetype(ZSTD_compressBound(size_t(patched.size()))), Qt::Uninitialized);
    const size_t written = ZSTD_compress(packed.data(), size_t(packed.size()), patched.constData(),
                                         size_t(patched.size()), 10);
    if (ZSTD_isError(written))
        return {};
    packed.resize(qsizetype(written));

    QByteArray header = all.left(52);
    const auto writeU32 = [&header](int at, quint32 v) {
        for (int i = 0; i < 4; ++i)
            header[at + i] = char((v >> (8 * i)) & 0xff);
    };
    writeU32(12, quint32(packed.size()));
    writeU32(16, quint32(patched.size()));
    header.replace(20, 32, QCryptographicHash::hash(patched, QCryptographicHash::Sha256));

    const QString out = m_tmp.filePath(name);
    QFile file(out);
    if (!file.open(QIODevice::WriteOnly))
        return {};
    file.write(header);
    file.write(packed);
    file.write(all.mid(52 + storedSize));
    return out;
}

void TestProjectBundle::roundTripsMixedStorage()
{
    const WriteRequest request = sampleRequest();
    const QString path = m_tmp.filePath(QStringLiteral("round.drift"));

    QString error;
    QVERIFY2(write(path, request, {}, &error), qPrintable(error));

    const auto info = readManifest(path, &error);
    QVERIFY2(info.has_value(), qPrintable(error));
    QCOMPARE(info->formatVersion, formatVersionString());
    QCOMPARE(info->projectId, QStringLiteral("proj-1"));
    QCOMPARE(info->title, QStringLiteral("Sample"));
    QCOMPARE(info->author, QStringLiteral("Tester"));
    QCOMPARE(info->description, QStringLiteral("A project"));
    QCOMPARE(info->createdAt, request.createdAt);
    QCOMPARE(info->document, request.document);
    QCOMPARE(info->addons.size(), 1);
    QCOMPARE(info->addons.first().id, QStringLiteral("cinematic"));
    QCOMPARE(info->addons.first().version, QStringLiteral("1.2.0"));
    QCOMPARE(info->addons.first().kinds, QStringList{QStringLiteral("effects")});
    QCOMPARE(info->media.size(), 3);
    QCOMPARE(info->embeddedBytes, 4096u + 8192u);

    const QString dest = m_tmp.filePath(QStringLiteral("out"));
    QHash<QString, QString> remap;
    QVERIFY2(extract(path, dest, {}, &remap, &error), qPrintable(error));

    // Only the embedded entries move; the referenced one still points at the original file.
    QCOMPARE(remap.size(), 2);
    QVERIFY(remap.contains(request.media.at(0).originalPath));
    QVERIFY(!remap.contains(request.media.at(1).originalPath));
    QVERIFY(remap.contains(request.media.at(2).originalPath));

    for (const MediaEntry &entry : request.media) {
        if (!entry.embedded)
            continue;
        QFile original(entry.originalPath);
        QFile extracted(remap.value(entry.originalPath));
        QVERIFY(original.open(QIODevice::ReadOnly));
        QVERIFY2(extracted.open(QIODevice::ReadOnly),
                 qPrintable(remap.value(entry.originalPath)));
        QCOMPARE(extracted.readAll(), original.readAll());
    }
}

void TestProjectBundle::dedupesRepeatedPaths()
{
    WriteRequest request = sampleRequest();
    // A clip carries a copy of its asset's path, which is exactly this shape.
    MediaEntry duplicate = request.media.at(0);
    request.media.append(duplicate);

    const QString path = m_tmp.filePath(QStringLiteral("dedup.drift"));
    QString error;
    QVERIFY2(write(path, request, {}, &error), qPrintable(error));

    const auto info = readManifest(path, &error);
    QVERIFY2(info.has_value(), qPrintable(error));
    QCOMPARE(info->media.size(), 4);
    QCOMPARE(info->media.at(0).blob, info->media.at(3).blob);
    QCOMPARE(info->media.at(0).fileName, info->media.at(3).fileName);
    // Two entries, one copy of the bytes: 4096 + 8192, not 4096 twice.
    QCOMPARE(info->embeddedBytes, 4096u + 8192u + 4096u);

    QDir dir(m_tmp.filePath(QStringLiteral("dedup-out")));
    QHash<QString, QString> remap;
    QVERIFY2(extract(path, dir.path(), {}, &remap, &error), qPrintable(error));
    QCOMPARE(dir.entryList(QDir::Files).size(), 2);
}

void TestProjectBundle::keepsDocumentResourceLayout()
{
    // Two animations from different folders that both name images/img_0.png, one image also used
    // as standalone media, and a referenced document whose resource must stay behind with it.
    QVERIFY(QDir(m_tmp.path()).mkpath(QStringLiteral("a/images")));
    QVERIFY(QDir(m_tmp.path()).mkpath(QStringLiteral("b/images")));
    QVERIFY(QDir(m_tmp.path()).mkpath(QStringLiteral("c/images")));
    const QString docA = writeSource(QStringLiteral("a/anim.json"), "{\"a\":1}");
    const QString imgA = writeSource(QStringLiteral("a/images/img_0.png"), QByteArray(300, 'A'));
    const QString docB = writeSource(QStringLiteral("b/anim.json"), "{\"b\":2}");
    const QString imgB = writeSource(QStringLiteral("b/images/img_0.png"), QByteArray(200, 'B'));
    const QString docC = writeSource(QStringLiteral("c/anim.json"), "{\"c\":3}");
    const QString imgC = writeSource(QStringLiteral("c/images/img_0.png"), QByteArray(100, 'C'));

    const auto entry = [](const QString &path, const QString &resourceOf, bool embedded) {
        MediaEntry e;
        e.originalPath = path;
        e.resourceOf = resourceOf;
        e.embedded = embedded;
        return e;
    };
    WriteRequest request = sampleRequest();
    request.media = {entry(imgA, QString(), true), entry(docA, QString(), true),
                     entry(imgA, docA, true),      entry(docB, QString(), true),
                     entry(imgB, docB, true),      entry(docC, QString(), false),
                     entry(imgC, docC, true)};

    const QString path = m_tmp.filePath(QStringLiteral("layout.drift"));
    QString error;
    QVERIFY2(write(path, request, {}, &error), qPrintable(error));
    const auto info = readManifest(path, &error);
    QVERIFY2(info.has_value(), qPrintable(error));
    QVERIFY(!info->media.at(6).embedded);
    QCOMPARE(info->media.at(6).resourceOf, docC);

    const QString dest = m_tmp.filePath(QStringLiteral("layout-out"));
    QHash<QString, QString> remap;
    QVERIFY2(extract(path, dest, {}, &remap, &error), qPrintable(error));

    // Documents and standalone media are remapped; resources are found beside their document.
    QCOMPARE(remap.size(), 3);
    const auto readAt = [](const QString &file) {
        QFile f(file);
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    };
    const QString outA = remap.value(docA);
    const QString outB = remap.value(docB);
    QVERIFY(QFileInfo(outA).absolutePath() != QFileInfo(outB).absolutePath());
    QCOMPARE(readAt(outA), readAt(docA));
    QCOMPARE(readAt(outB), readAt(docB));
    QCOMPARE(readAt(QFileInfo(outA).absolutePath() + QStringLiteral("/images/img_0.png")), readAt(imgA));
    QCOMPARE(readAt(QFileInfo(outB).absolutePath() + QStringLiteral("/images/img_0.png")), readAt(imgB));
    QCOMPARE(readAt(remap.value(imgA)), readAt(imgA));
}

void TestProjectBundle::compressesTextButNotMedia()
{
    const WriteRequest request = sampleRequest();
    const QString path = m_tmp.filePath(QStringLiteral("codec.drift"));
    QString error;
    QVERIFY2(write(path, request, {}, &error), qPrintable(error));

    // 8 KiB of one repeated character must shrink; the .mp4 of repeated bytes is stored as-is, so
    // the file cannot be smaller than it.
    const qint64 size = QFileInfo(path).size();
    QVERIFY(size > 4096);
    QVERIFY(size < 4096 + 8192);
}

// Repeats a few MiB apart are beyond the default level-10 window; only long-distance matching
// with a window sized to the manifest folds them away.
void TestProjectBundle::longRangeMatchesSpanTheManifest()
{
    QByteArray chunk;
    quint32 seed = 12345;
    for (int i = 0; i < 3 * 1024 * 1024; ++i) {
        seed = seed * 1664525u + 1013904223u;
        chunk.append("0123456789abcdef"[(seed >> 24) & 15]);
    }
    QJsonArray copies;
    for (int i = 0; i < 4; ++i)
        copies.append(QString::fromLatin1(chunk));

    WriteRequest request = sampleRequest();
    request.document.insert(QStringLiteral("copies"), copies);
    const QString path = m_tmp.filePath(QStringLiteral("long-range.drift"));
    QString error;
    QVERIFY2(write(path, request, {}, &error), qPrintable(error));
    QVERIFY(QFileInfo(path).size() < 3 * 1024 * 1024);

    const auto info = readManifest(path, &error);
    QVERIFY2(info.has_value(), qPrintable(error));
    const QJsonArray back = info->document.value(QStringLiteral("copies")).toArray();
    QCOMPARE(back.size(), 4);
    QCOMPARE(back.at(3).toString(), QString::fromLatin1(chunk));
}

void TestProjectBundle::rejectsImplausibleManifestSize()
{
    const QString path = m_tmp.filePath(QStringLiteral("big-claim.drift"));
    QString error;
    QVERIFY2(write(path, sampleRequest(), {}, &error), qPrintable(error));
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadWrite));
    // manifestRaw sits at offset 16; claim 4 GiB - 1, far past the decompressed ceiling.
    QVERIFY(file.seek(16));
    QCOMPARE(file.write(QByteArray(4, '\xff')), 4);
    file.close();
    QVERIFY(!readManifest(path, &error).has_value());
    QVERIFY(error.contains(QStringLiteral("implausible")));
}

void TestProjectBundle::skipsAlreadyExtractedBlobs()
{
    const WriteRequest request = sampleRequest();
    const QString path = m_tmp.filePath(QStringLiteral("reopen.drift"));
    QString error;
    QVERIFY2(write(path, request, {}, &error), qPrintable(error));

    const QString dest = m_tmp.filePath(QStringLiteral("reopen-out"));
    QHash<QString, QString> remap;
    QVERIFY2(extract(path, dest, {}, &remap, &error), qPrintable(error));

    const QString target = remap.value(request.media.at(0).originalPath);
    const QDateTime before = QFileInfo(target).lastModified();

    QHash<QString, QString> second;
    QVERIFY2(extract(path, dest, {}, &second, &error), qPrintable(error));
    QCOMPARE(second, remap);
    QCOMPARE(QFileInfo(target).lastModified(), before);
}

void TestProjectBundle::degradesMissingMediaToReference()
{
    WriteRequest request = sampleRequest();
    MediaEntry gone;
    gone.originalPath = m_tmp.filePath(QStringLiteral("never-existed.mp4"));
    gone.role = MediaRole::Source;
    gone.embedded = true;
    request.media.append(gone);

    // Losing one file must not cost the user the edit itself.
    const QString path = m_tmp.filePath(QStringLiteral("missing.drift"));
    QString error;
    QVERIFY2(write(path, request, {}, &error), qPrintable(error));

    const auto info = readManifest(path, &error);
    QVERIFY2(info.has_value(), qPrintable(error));
    QCOMPARE(info->media.size(), 4);
    QVERIFY(!info->media.at(3).embedded);
    QCOMPARE(info->media.at(3).originalPath, gone.originalPath);
}

void TestProjectBundle::rejectsNewerMajorVersion()
{
    const WriteRequest request = sampleRequest();
    const QString path = m_tmp.filePath(QStringLiteral("base.drift"));
    QString error;
    QVERIFY2(write(path, request, {}, &error), qPrintable(error));

    // A bumped minor still opens; a bumped major does not.
    const QString minor = restamped(path, QStringLiteral("minor.drift"),
                                    QStringLiteral("%1.99.0").arg(kFormatMajor));
    QVERIFY(!minor.isEmpty());
    QVERIFY2(readManifest(minor, &error).has_value(), qPrintable(error));

    const QString major = restamped(path, QStringLiteral("major.drift"),
                                    QStringLiteral("%1.0.0").arg(kFormatMajor + 1));
    QVERIFY(!major.isEmpty());
    QVERIFY(!readManifest(major, &error).has_value());
    QVERIFY(error.contains(QStringLiteral("newer version")));
}

void TestProjectBundle::rejectsCorruptBlob()
{
    const WriteRequest request = sampleRequest();
    const QString path = m_tmp.filePath(QStringLiteral("badblob.drift"));
    QString error;
    QVERIFY2(write(path, request, {}, &error), qPrintable(error));

    // Past the manifest and inside the first stored blob; the trailer is the last 64 bytes.
    flipByte(path, QFileInfo(path).size() - 64 - 2048);

    const QString dest = m_tmp.filePath(QStringLiteral("badblob-out"));
    QHash<QString, QString> remap;
    QVERIFY(!extract(path, dest, {}, &remap, &error));
    QVERIFY(!error.isEmpty());
}

void TestProjectBundle::rejectsCorruptManifest()
{
    const WriteRequest request = sampleRequest();
    const QString path = m_tmp.filePath(QStringLiteral("badmanifest.drift"));
    QString error;
    QVERIFY2(write(path, request, {}, &error), qPrintable(error));

    flipByte(path, 60); // inside the compressed manifest, past the 52-byte header

    QVERIFY(!readManifest(path, &error).has_value());
    QVERIFY(!error.isEmpty());
}

void TestProjectBundle::rejectsBadMagic()
{
    const WriteRequest request = sampleRequest();
    const QString path = m_tmp.filePath(QStringLiteral("magic.drift"));
    QString error;
    QVERIFY2(write(path, request, {}, &error), qPrintable(error));

    flipByte(path, 0);

    QVERIFY(!readManifest(path, &error).has_value());
    QVERIFY(error.contains(QStringLiteral("magic")));
}

void TestProjectBundle::cancellingWriteKeepsPreviousFile()
{
    const WriteRequest request = sampleRequest();
    const QString path = m_tmp.filePath(QStringLiteral("cancel.drift"));

    QString error;
    QVERIFY2(write(path, request, {}, &error), qPrintable(error));
    const QByteArray before = [&] {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }();
    QVERIFY(!before.isEmpty());

    // QSaveFile stages the rewrite, so an abort must leave the good bundle in place.
    QVERIFY(!write(path, request, [](qint64, qint64) { return false; }, &error));
    QCOMPARE(error, QStringLiteral("Cancelled"));

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), before);
}

namespace {

MediaEntry collectEntry(const QString &path, const QString &resourceOf = QString(),
                        MediaRole role = MediaRole::Source)
{
    MediaEntry e;
    e.originalPath = path;
    e.resourceOf = resourceOf;
    e.role = role;
    return e;
}

QByteArray readAll(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

} // namespace

void TestProjectBundle::collectsIntoTypedFolders()
{
    QVERIFY(QDir(m_tmp.path()).mkpath(QStringLiteral("src/anim/images")));
    const QString clip = writeSource(QStringLiteral("src/clip.mp4"), QByteArray(500, 'v'));
    const QString doc = writeSource(QStringLiteral("src/anim/anim.json"), "{}");
    const QString img = writeSource(QStringLiteral("src/anim/images/img_0.png"), "png");
    const QString matte = writeSource(QStringLiteral("src/matte.mkv"), "matte");
    const QList<MediaEntry> media = {collectEntry(clip), collectEntry(doc), collectEntry(img, doc),
                                     collectEntry(matte, QString(), MediaRole::Matte),
                                     collectEntry(m_tmp.filePath(QStringLiteral("gone.mp4")))};
    const QHash<QString, QString> subfolders = {{clip, QStringLiteral("Video")},
                                                {matte, QStringLiteral("Derived")}};

    const QString dest = m_tmp.filePath(QStringLiteral("collect-typed"));
    QVERIFY(QDir().mkpath(dest));
    QHash<QString, QString> remap;
    QString error;
    QVERIFY2(collectToFolder(media, subfolders, dest, false, {}, &remap, nullptr, &error),
             qPrintable(error));

    // The missing file is skipped; the resource travels with its document but is not remapped.
    QCOMPARE(remap.size(), 3);
    QCOMPARE(remap.value(clip), QDir(dest).filePath(QStringLiteral("Video/clip.mp4")));
    QCOMPARE(remap.value(matte), QDir(dest).filePath(QStringLiteral("Derived/matte.mkv")));
    QCOMPARE(remap.value(doc), QDir(dest).filePath(QStringLiteral("Other/anim/anim.json")));
    QCOMPARE(readAll(QDir(dest).filePath(QStringLiteral("Other/anim/images/img_0.png"))),
             QByteArray("png"));
    QVERIFY(QFileInfo::exists(clip));
}

void TestProjectBundle::collectReusesSameBytesAndSuffixesOthers()
{
    QVERIFY(QDir(m_tmp.path()).mkpath(QStringLiteral("x")));
    QVERIFY(QDir(m_tmp.path()).mkpath(QStringLiteral("y")));
    const QString first = writeSource(QStringLiteral("x/take.mp4"), "first");
    const QString second = writeSource(QStringLiteral("y/take.mp4"), "other");
    const QHash<QString, QString> subfolders = {{first, QStringLiteral("Video")},
                                                {second, QStringLiteral("Video")}};
    const QString dest = m_tmp.filePath(QStringLiteral("collect-reuse"));
    QVERIFY(QDir().mkpath(dest));

    QHash<QString, QString> remap;
    QString error;
    QVERIFY2(collectToFolder({collectEntry(first), collectEntry(second)}, subfolders, dest, false,
                             {}, &remap, nullptr, &error),
             qPrintable(error));
    const QString firstOut = QDir(dest).filePath(QStringLiteral("Video/take.mp4"));
    const QString secondOut = QDir(dest).filePath(QStringLiteral("Video/take (2).mp4"));
    QCOMPARE(remap.value(first), firstOut);
    QCOMPARE(remap.value(second), secondOut);
    QCOMPARE(readAll(secondOut), QByteArray("other"));

    // Collecting again finds both already there instead of making a third and fourth copy.
    QHash<QString, QString> again;
    QVERIFY2(collectToFolder({collectEntry(first), collectEntry(second)}, subfolders, dest, false,
                             {}, &again, nullptr, &error),
             qPrintable(error));
    QCOMPARE(again, remap);
    QCOMPARE(QDir(QDir(dest).filePath(QStringLiteral("Video"))).entryList(QDir::Files).size(), 2);
}

void TestProjectBundle::collectMoveDeletesOriginals()
{
    QVERIFY(QDir(m_tmp.path()).mkpath(QStringLiteral("src/anim/images")));
    const QString clip = writeSource(QStringLiteral("src/clip.mp4"), "video");
    const QString doc = writeSource(QStringLiteral("src/anim/anim.json"), "{}");
    const QString img = writeSource(QStringLiteral("src/anim/images/img_0.png"), "png");
    // The image is also a bin asset in its own right, so the move needs it twice.
    const QList<MediaEntry> media = {collectEntry(doc), collectEntry(img, doc), collectEntry(img),
                                     collectEntry(clip)};
    const QHash<QString, QString> subfolders = {{clip, QStringLiteral("Video")},
                                                {img, QStringLiteral("Images")}};
    const QString dest = m_tmp.filePath(QStringLiteral("collect-move"));
    QVERIFY(QDir().mkpath(dest));

    QHash<QString, QString> remap;
    int undeleted = -1;
    QString error;
    QVERIFY2(collectToFolder(media, subfolders, dest, true, {}, &remap, &undeleted, &error),
             qPrintable(error));
    QCOMPARE(undeleted, 0);
    QVERIFY(!QFileInfo::exists(clip));
    QVERIFY(!QFileInfo::exists(doc));
    QVERIFY(!QFileInfo::exists(img));
    QCOMPARE(readAll(remap.value(clip)), QByteArray("video"));
    QCOMPARE(readAll(remap.value(img)), QByteArray("png"));
    QCOMPARE(readAll(QDir(dest).filePath(QStringLiteral("Other/anim/images/img_0.png"))),
             QByteArray("png"));
}

void TestProjectBundle::cancelledCollectMoveRestoresOriginals()
{
    const QString a = writeSource(QStringLiteral("a.mp4"), QByteArray(100, 'a'));
    const QString b = writeSource(QStringLiteral("b.mp4"), QByteArray(100, 'b'));
    const QString dest = m_tmp.filePath(QStringLiteral("collect-cancel"));
    QVERIFY(QDir().mkpath(dest));

    int calls = 0;
    const auto cancelSecond = [&calls](qint64, qint64) { return ++calls < 2; };
    QHash<QString, QString> remap;
    QString error;
    QVERIFY(!collectToFolder({collectEntry(a), collectEntry(b)}, {}, dest, true, cancelSecond,
                             &remap, nullptr, &error));
    QVERIFY(remap.isEmpty());
    QCOMPARE(readAll(a), QByteArray(100, 'a'));
    QCOMPARE(readAll(b), QByteArray(100, 'b'));
    QVERIFY(!QFileInfo::exists(QDir(dest).filePath(QStringLiteral("Other/a.mp4"))));
    QVERIFY(!QFileInfo::exists(QDir(dest).filePath(QStringLiteral("Other/b.mp4"))));
}

QString TestProjectBundle::writeSequence(const QString &dir) const
{
    QDir().mkpath(m_tmp.filePath(dir));
    for (int i = 1; i <= 5; ++i) {
        QImage image(4, 4, QImage::Format_RGB32);
        image.fill(QColor(i * 40, 0, 0));
        image.save(m_tmp.filePath(dir + QStringLiteral("/render_") + QStringLiteral("%1").arg(i, 4, 10, QLatin1Char('0'))
                                  + QStringLiteral(".png")));
    }
    drift::ImageSequence sequence;
    sequence.pattern = m_tmp.filePath(dir) + QStringLiteral("/render_%04d.png");
    sequence.start = 1;
    sequence.fpsNum = 24;
    return drift::imageSequencePath(sequence);
}

void TestProjectBundle::packagesImageSequenceFrames()
{
    const QString sequencePath = writeSequence(QStringLiteral("seq-pack"));
    const auto sequence = drift::parseImageSequencePath(sequencePath);
    QVERIFY(sequence.has_value());

    WriteRequest request = sampleRequest();
    request.media.clear();
    MediaEntry entry;
    entry.originalPath = sequencePath;
    entry.embedded = true;
    request.media.append(entry);
    for (const QString &frame : drift::imageSequenceFrames(*sequence)) {
        MediaEntry resource;
        resource.originalPath = frame;
        resource.resourceOf = sequencePath;
        resource.embedded = true;
        request.media.append(resource);
    }
    QCOMPARE(request.media.size(), 6);

    const QString path = m_tmp.filePath(QStringLiteral("seq.drift"));
    QString error;
    QVERIFY2(write(path, request, {}, &error), qPrintable(error));

    const QString dest = m_tmp.filePath(QStringLiteral("seq-out"));
    QHash<QString, QString> remap;
    QVERIFY2(extract(path, dest, {}, &remap, &error), qPrintable(error));
    QCOMPARE(remap.size(), 1);
    const auto unpacked = drift::parseImageSequencePath(remap.value(sequencePath));
    QVERIFY(unpacked.has_value());
    QCOMPARE(unpacked->start, 1);
    QCOMPARE(unpacked->fpsNum, 24);
    QCOMPARE(QFileInfo(unpacked->pattern).fileName(), QStringLiteral("render_%04d.png"));
    const QStringList frames = drift::imageSequenceFrames(*unpacked);
    QCOMPARE(frames.size(), 5);
    const QStringList originals = drift::imageSequenceFrames(*sequence);
    for (int i = 0; i < frames.size(); ++i)
        QCOMPARE(readAll(frames.at(i)), readAll(originals.at(i)));
}

void TestProjectBundle::collectsImageSequenceIntoOneFolder()
{
    const QString sequencePath = writeSequence(QStringLiteral("seq-src"));
    const auto sequence = drift::parseImageSequencePath(sequencePath);
    QVERIFY(sequence.has_value());
    const QStringList originals = drift::imageSequenceFrames(*sequence);
    QCOMPARE(originals.size(), 5);
    QList<QByteArray> bytes;
    for (const QString &frame : originals)
        bytes.append(readAll(frame));

    QList<MediaEntry> media = {collectEntry(sequencePath)};
    for (const QString &frame : originals)
        media.append(collectEntry(frame, sequencePath));
    const QHash<QString, QString> subfolders = {{sequencePath, QStringLiteral("Video")}};

    const auto check = [&](const QString &dest, const QString &folderName) {
        QHash<QString, QString> remap;
        int undeleted = -1;
        QString error;
        QVERIFY2(collectToFolder(media, subfolders, dest, dest.contains(QLatin1String("move")),
                                 {}, &remap, &undeleted, &error),
                 qPrintable(error));
        QCOMPARE(remap.size(), 1);
        const auto moved = drift::parseImageSequencePath(remap.value(sequencePath));
        QVERIFY(moved.has_value());
        QCOMPARE(moved->start, 1);
        QCOMPARE(moved->fpsNum, 24);
        QCOMPARE(moved->pattern,
                 QDir(dest).filePath(QStringLiteral("Video/") + folderName + QStringLiteral("/render_%04d.png")));
        const QStringList frames = drift::imageSequenceFrames(*moved);
        QCOMPARE(frames.size(), 5);
        for (int i = 0; i < frames.size(); ++i)
            QCOMPARE(readAll(frames.at(i)), bytes.at(i));
    };

    const QString copyDest = m_tmp.filePath(QStringLiteral("seq-collect-copy"));
    QVERIFY(QDir().mkpath(copyDest));
    check(copyDest, QStringLiteral("render"));
    for (const QString &frame : originals)
        QVERIFY(QFileInfo::exists(frame));

    // The folder name is taken by identical frames, so collecting again reuses it...
    check(copyDest, QStringLiteral("render"));

    // ...and a different sequence with the same pattern gets a folder of its own, not renamed frames.
    QImage other(4, 4, QImage::Format_RGB32);
    other.fill(Qt::green);
    QVERIFY(other.save(originals.at(2)));
    bytes[2] = readAll(originals.at(2));
    check(copyDest, QStringLiteral("render (2)"));

    const QString moveDest = m_tmp.filePath(QStringLiteral("seq-collect-move"));
    QVERIFY(QDir().mkpath(moveDest));
    check(moveDest, QStringLiteral("render"));
    for (const QString &frame : originals)
        QVERIFY(!QFileInfo::exists(frame));
}

QTEST_MAIN(TestProjectBundle)
#include "tst_projectbundle.moc"
