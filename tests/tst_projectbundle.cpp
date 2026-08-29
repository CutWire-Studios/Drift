#include "engine/ProjectBundle.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
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
    void compressesTextButNotMedia();
    void skipsAlreadyExtractedBlobs();
    void degradesMissingMediaToReference();
    void rejectsNewerMajorVersion();
    void rejectsCorruptBlob();
    void rejectsCorruptManifest();
    void rejectsBadMagic();
    void cancellingWriteKeepsPreviousFile();

private:
    QString writeSource(const QString &name, const QByteArray &content) const;
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

QTEST_MAIN(TestProjectBundle)
#include "tst_projectbundle.moc"
