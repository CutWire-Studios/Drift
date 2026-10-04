#include "AddonPackage.h"

#include "AddonSigningKey.h"
#include "models/VersionCompare.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSysInfo>

#include <openssl/evp.h>
#include <zstd.h>

namespace drift::addon {
namespace {

constexpr char kMagic[8] = {'D', 'R', 'I', 'F', 'T', 'P', 'K', 'G'};
constexpr char kUserMagic[8] = {'D', 'R', 'I', 'F', 'T', 'F', 'X', '\0'};
constexpr quint32 kFormatVersion = 1;
constexpr int kHeaderSize = 16;  // magic + version + metadataLength
constexpr int kSizesSize = 16;   // payloadCompressed + payloadRaw
constexpr int kDigestSize = 32;  // SHA-256
constexpr int kSignatureSize = 64; // Ed25519
constexpr quint32 kMaxMetadataSize = 64u * 1024 * 1024;
constexpr qint64 kChunkSize = 1 << 20;
constexpr quint64 kMaxUserPayload = 256ull * 1024 * 1024; // Forge refuses to export more

quint32 readU32(const char *p)
{
    const auto *b = reinterpret_cast<const quint8 *>(p);
    return quint32(b[0]) | (quint32(b[1]) << 8) | (quint32(b[2]) << 16) | (quint32(b[3]) << 24);
}

void appendU32(QByteArray *out, quint32 v)
{
    for (int i = 0; i < 4; ++i)
        out->append(char((v >> (8 * i)) & 0xff));
}

void appendU64(QByteArray *out, quint64 v)
{
    for (int i = 0; i < 8; ++i)
        out->append(char((v >> (8 * i)) & 0xff));
}

quint64 readU64(const char *p)
{
    const auto *b = reinterpret_cast<const quint8 *>(p);
    quint64 v = 0;
    for (int i = 7; i >= 0; --i)
        v = (v << 8) | b[i];
    return v;
}

bool fail(QString *error, const QString &message)
{
    if (error)
        *error = message;
    return false;
}

// Reject anything that could escape the destination directory once joined onto it.
bool safeRelativePath(const QString &path)
{
    if (path.isEmpty() || path.size() > 512)
        return false;
    if (path.contains(QLatin1Char('\\')) || path.contains(QLatin1Char(':')))
        return false;
    if (QDir::isAbsolutePath(path) || path.startsWith(QLatin1Char('/')))
        return false;
    const QStringList parts = path.split(QLatin1Char('/'));
    for (const QString &part : parts) {
        if (part.isEmpty() || part == QLatin1String(".") || part == QLatin1String(".."))
            return false;
    }
    return true;
}

bool parseMetadata(const QByteArray &json, PackageInfo *info, QString *error, bool requireFiles = true)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (!doc.isObject())
        return fail(error, QStringLiteral("metadata is not valid JSON: %1").arg(parseError.errorString()));

    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("schema")).toInt() != 1)
        return fail(error, QStringLiteral("unsupported metadata schema"));

    info->raw = root;
    info->id = root.value(QStringLiteral("id")).toString();
    info->version = root.value(QStringLiteral("version")).toString();
    info->name = root.value(QStringLiteral("name")).toString();
    info->description = root.value(QStringLiteral("description")).toString();
    info->details = root.value(QStringLiteral("details")).toString();
    info->author = root.value(QStringLiteral("author")).toString();
    info->license = root.value(QStringLiteral("license")).toString();
    info->minAppVersion = root.value(QStringLiteral("minAppVersion")).toString();
    info->platform = root.value(QStringLiteral("platform")).toString().trimmed().toLower();
    info->installedSize = quint64(root.value(QStringLiteral("installedSize")).toDouble());

    if (info->id.isEmpty() || info->version.isEmpty())
        return fail(error, QStringLiteral("metadata is missing id or version"));

    const QJsonArray provides = root.value(QStringLiteral("provides")).toArray();
    for (const QJsonValue &value : provides) {
        const QJsonObject object = value.toObject();
        PackageProvide provide;
        provide.kind = object.value(QStringLiteral("kind")).toString();
        provide.root = object.value(QStringLiteral("root")).toString();
        provide.items = object.value(QStringLiteral("items")).toInt();
        if (provide.kind.isEmpty() || !safeRelativePath(provide.root))
            return fail(error, QStringLiteral("invalid provides entry"));
        info->provides.append(provide);
    }
    if (info->provides.isEmpty())
        return fail(error, QStringLiteral("metadata declares no provides entries"));

    // Entries must tile the uncompressed payload exactly, in order — the extractor writes
    // sequentially and never seeks.
    quint64 expectedOffset = 0;
    const QJsonArray files = root.value(QStringLiteral("files")).toArray();
    for (const QJsonValue &value : files) {
        const QJsonObject object = value.toObject();
        PackageFile file;
        file.path = object.value(QStringLiteral("path")).toString();
        file.offset = quint64(object.value(QStringLiteral("offset")).toDouble());
        file.size = quint64(object.value(QStringLiteral("size")).toDouble());
        file.sha256 = QByteArray::fromHex(object.value(QStringLiteral("sha256")).toString().toLatin1());
        if (!safeRelativePath(file.path))
            return fail(error, QStringLiteral("unsafe file path in metadata: %1").arg(file.path));
        if (file.sha256.size() != kDigestSize)
            return fail(error, QStringLiteral("missing file hash for %1").arg(file.path));
        if (file.offset != expectedOffset)
            return fail(error, QStringLiteral("non-contiguous file table at %1").arg(file.path));
        expectedOffset += file.size;
        info->files.append(file);
    }
    if (requireFiles && info->files.isEmpty())
        return fail(error, QStringLiteral("metadata lists no files"));

    return true;
}

bool checkUserLayout(const PackageInfo &info, QString *error)
{
    if (info.provides.size() != 1)
        return fail(error, QStringLiteral("a user package must provide exactly one item"));
    const PackageProvide &provide = info.provides.first();
    // Audio effects are safe to sideload: a package only names one of the built-in processors and
    // sets its parameters, so it carries no code.
    if (provide.kind != QLatin1String("effects") && provide.kind != QLatin1String("transitions")
        && provide.kind != QLatin1String("audio-effects")) {
        return fail(error, QStringLiteral("a user package can only provide an effect, a transition or an audio effect"));
    }

    const QString rootPrefix = provide.root + QLatin1Char('/');
    QString folder;
    for (const PackageFile &file : info.files) {
        const QString rest = file.path.startsWith(rootPrefix) ? file.path.mid(rootPrefix.size()) : QString();
        const QString first = rest.section(QLatin1Char('/'), 0, 0);
        if (first.isEmpty() || first == rest)
            return fail(error, QStringLiteral("unexpected file outside the package folder: %1").arg(file.path));
        if (folder.isEmpty())
            folder = first;
        else if (first != folder)
            return fail(error, QStringLiteral("a user package must contain a single folder"));
    }
    return true;
}

// Read the fixed header and the metadata block, leaving `file` positioned at payloadCompressed.
bool readHeader(QFile &file, PackageInfo *info, QCryptographicHash *digest, QString *error,
                Container container)
{
    const bool user = container == Container::User;
    QByteArray header = file.read(kHeaderSize);
    if (header.size() != kHeaderSize)
        return fail(error, user ? QStringLiteral("file is too short to be a .driftfx")
                                : QStringLiteral("file is too short to be a .driftpkg"));
    if (memcmp(header.constData(), user ? kUserMagic : kMagic, sizeof(kMagic)) != 0)
        return fail(error, user ? QStringLiteral("not a .driftfx (bad magic)")
                                : QStringLiteral("not a .driftpkg (bad magic)"));

    const quint32 version = readU32(header.constData() + 8);
    if (version != kFormatVersion) {
        return fail(error, QStringLiteral("package format version %1 is newer than this build "
                                          "understands — update Drift")
                               .arg(version));
    }

    const quint32 metadataLength = readU32(header.constData() + 12);
    if (metadataLength == 0 || metadataLength > kMaxMetadataSize)
        return fail(error, QStringLiteral("implausible metadata length"));

    const QByteArray metadata = file.read(metadataLength);
    if (quint32(metadata.size()) != metadataLength)
        return fail(error, QStringLiteral("truncated metadata"));

    if (digest) {
        digest->addData(header);
        digest->addData(metadata);
    }
    if (!parseMetadata(metadata, info, error))
        return false;
    return !user || checkUserLayout(*info, error);
}

// Writes the decompressed byte stream out across the file table, hashing each file as it closes.
class PayloadWriter
{
public:
    PayloadWriter(const QString &stagingDir, const QList<PackageFile> &files)
        : m_stagingDir(stagingDir), m_files(files)
    {
    }

    bool write(const char *data, qsizetype size, QString *error)
    {
        while (size > 0) {
            if (m_index >= m_files.size())
                return fail(error, QStringLiteral("payload is larger than the file table"));

            const PackageFile &entry = m_files.at(m_index);
            if (!m_current.isOpen() && !openCurrent(entry, error))
                return false;

            const qsizetype take = qMin<qint64>(size, qint64(entry.size - m_written));
            if (take > 0) {
                if (m_current.write(data, take) != take)
                    return fail(error, QStringLiteral("write failed: %1").arg(m_current.errorString()));
                m_hash.addData(QByteArrayView(data, take));
                m_written += quint64(take);
                data += take;
                size -= take;
            }

            if (m_written == entry.size && !closeCurrent(entry, error))
                return false;
        }
        return true;
    }

    bool finish(QString *error)
    {
        // A zero-byte trailing entry never gets a write() call, so open and close it here.
        while (m_index < m_files.size()) {
            const PackageFile &entry = m_files.at(m_index);
            if (!m_current.isOpen() && !openCurrent(entry, error))
                return false;
            if (m_written != entry.size)
                return fail(error, QStringLiteral("payload ended mid-file at %1").arg(entry.path));
            if (!closeCurrent(entry, error))
                return false;
        }
        return true;
    }

private:
    bool openCurrent(const PackageFile &entry, QString *error)
    {
        const QString path = QDir(m_stagingDir).filePath(entry.path);
        const QString parent = QFileInfo(path).absolutePath();
        if (!QDir().mkpath(parent))
            return fail(error, QStringLiteral("cannot create %1").arg(parent));

        m_current.setFileName(path);
        if (!m_current.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return fail(error, QStringLiteral("cannot write %1: %2").arg(path, m_current.errorString()));
        m_hash.reset();
        m_written = 0;
        return true;
    }

    bool closeCurrent(const PackageFile &entry, QString *error)
    {
        m_current.close();
        if (m_hash.result() != entry.sha256)
            return fail(error, QStringLiteral("checksum mismatch for %1").arg(entry.path));
        ++m_index;
        return true;
    }

    QString m_stagingDir;
    const QList<PackageFile> &m_files;
    int m_index = 0;
    QFile m_current;
    QCryptographicHash m_hash{QCryptographicHash::Sha256};
    quint64 m_written = 0;
};

bool verifySignature(const QByteArray &digest, const QByteArray &signature)
{
    if (digest.size() != kDigestSize || signature.size() != kSignatureSize)
        return false;

    EVP_PKEY *key = EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, nullptr,
                                                kSigningPublicKey.data(), kSigningPublicKey.size());
    if (!key)
        return false;

    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    bool ok = false;
    if (ctx && EVP_DigestVerifyInit(ctx, nullptr, nullptr, nullptr, key) == 1) {
        ok = EVP_DigestVerify(ctx, reinterpret_cast<const unsigned char *>(signature.constData()),
                              size_t(signature.size()),
                              reinterpret_cast<const unsigned char *>(digest.constData()),
                              size_t(digest.size()))
             == 1;
    }
    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(key);
    return ok;
}

struct DCtxDeleter
{
    void operator()(ZSTD_DCtx *ctx) const { ZSTD_freeDCtx(ctx); }
};

} // namespace

QString currentPlatform()
{
#if defined(_WIN32)
    const QString os = QStringLiteral("win");
#elif defined(__APPLE__)
    const QString os = QStringLiteral("osx");
#elif defined(__ANDROID__)
    // Android is Linux-flavoured but its native libraries are linked against the NDK's libc and
    // will not load on a desktop Linux build of the same architecture, so it needs a tag of its
    // own rather than falling through to "linux-arm64".
    const QString os = QStringLiteral("android");
#else
    const QString os = QStringLiteral("linux");
#endif

    // Spelled the way upstream release archives and NuGet runtime identifiers do, so a recipe
    // names one string rather than translating between two conventions.
    const QString arch = QSysInfo::buildCpuArchitecture();
    if (arch == QLatin1String("x86_64") || arch == QLatin1String("amd64"))
        return os + QStringLiteral("-x64");
    if (arch == QLatin1String("arm64") || arch == QLatin1String("aarch64"))
        return os + QStringLiteral("-arm64");
    return os + QLatin1Char('-') + arch;
}

std::optional<PackageInfo> readManifest(const QString &packagePath, QString *error, Container container)
{
    QFile file(packagePath);
    if (!file.open(QIODevice::ReadOnly)) {
        fail(error, QStringLiteral("cannot open %1: %2").arg(packagePath, file.errorString()));
        return std::nullopt;
    }

    PackageInfo info;
    if (!readHeader(file, &info, nullptr, error, container))
        return std::nullopt;
    return info;
}

bool checkCompatible(const PackageInfo &info, QString *error)
{
    // A native-code package built for another platform installs perfectly and then fails to load,
    // which is a much harder thing to explain than a refusal.
    if (!info.platform.isEmpty() && info.platform != currentPlatform()) {
        return fail(error, QStringLiteral("%1 is built for %2, but this is %3")
                               .arg(info.id, info.platform, currentPlatform()));
    }
    if (!info.minAppVersion.isEmpty() && drift::compareVersions(QStringLiteral(DRIFT_VERSION), info.minAppVersion) < 0) {
        return fail(error, QStringLiteral("%1 requires Drift %2 or newer, but this is %3")
                               .arg(info.id, info.minAppVersion, QStringLiteral(DRIFT_VERSION)));
    }
    return true;
}

std::optional<PackageInfo> parseFolderManifest(const QByteArray &json, QString *error)
{
    PackageInfo info;
    if (!parseMetadata(json, &info, error, false))
        return std::nullopt;
    // The id names the folder it is installed into, so it must stay one path component.
    if (!safeRelativePath(info.id) || info.id.contains(QLatin1Char('/'))) {
        fail(error, QStringLiteral("invalid addon id: %1").arg(info.id));
        return std::nullopt;
    }
    return info;
}

bool hasNativeCode(const PackageInfo &info)
{
    for (const PackageProvide &provide : info.provides) {
        if (provide.kind == QLatin1String("onnxruntime") || provide.kind == QLatin1String("onnxruntime-ep"))
            return true;
    }
    return false;
}

SignatureCheck checkSignature(const QString &packagePath, QString *error)
{
    QFile file(packagePath);
    if (!file.open(QIODevice::ReadOnly)) {
        fail(error, QStringLiteral("cannot open %1: %2").arg(packagePath, file.errorString()));
        return SignatureCheck::Corrupt;
    }

    QCryptographicHash digest(QCryptographicHash::Sha256);
    PackageInfo info;
    if (!readHeader(file, &info, &digest, error, Container::Signed))
        return SignatureCheck::Corrupt;

    const QByteArray sizes = file.read(kSizesSize);
    if (sizes.size() != kSizesSize) {
        fail(error, QStringLiteral("truncated payload header"));
        return SignatureCheck::Corrupt;
    }
    digest.addData(sizes);

    const quint64 payloadCompressed = readU64(sizes.constData());
    if (file.pos() + qint64(payloadCompressed) + kDigestSize + kSignatureSize != file.size()) {
        fail(error, QStringLiteral("package is truncated or has trailing garbage"));
        return SignatureCheck::Corrupt;
    }

    QByteArray buffer(kChunkSize, Qt::Uninitialized);
    quint64 remaining = payloadCompressed;
    while (remaining > 0) {
        const qint64 got = file.read(buffer.data(), qMin<qint64>(buffer.size(), qint64(remaining)));
        if (got <= 0) {
            fail(error, QStringLiteral("truncated payload"));
            return SignatureCheck::Corrupt;
        }
        digest.addData(QByteArrayView(buffer.constData(), got));
        remaining -= quint64(got);
    }

    const QByteArray actualDigest = file.read(kDigestSize);
    if (actualDigest != digest.result()) {
        fail(error, QStringLiteral("package contents do not match its digest"));
        return SignatureCheck::Corrupt;
    }
    return verifySignature(actualDigest, file.read(kSignatureSize)) ? SignatureCheck::Official
                                                                      : SignatureCheck::Unverified;
}

bool install(const QString &packagePath, const QString &destDir, const ProgressFn &progress,
             PackageInfo *installed, QString *error, Container container, bool requireSignature)
{
    const bool user = container == Container::User;
    QFile file(packagePath);
    if (!file.open(QIODevice::ReadOnly))
        return fail(error, QStringLiteral("cannot open %1: %2").arg(packagePath, file.errorString()));

    QCryptographicHash digest(QCryptographicHash::Sha256);
    PackageInfo info;
    if (!readHeader(file, &info, &digest, error, container))
        return false;

    // Refused here rather than in the manager, because this is also the path a side-loaded file
    // takes.
    if (!checkCompatible(info, error))
        return false;

    const QByteArray sizes = file.read(kSizesSize);
    if (sizes.size() != kSizesSize)
        return fail(error, QStringLiteral("truncated payload header"));
    digest.addData(sizes);

    const quint64 payloadCompressed = readU64(sizes.constData());
    const quint64 payloadRaw = readU64(sizes.constData() + 8);

    quint64 tableTotal = 0;
    for (const PackageFile &entry : info.files)
        tableTotal += entry.size;
    if (tableTotal != payloadRaw)
        return fail(error, QStringLiteral("file table does not account for the whole payload"));
    if (user && payloadRaw > kMaxUserPayload)
        return fail(error, QStringLiteral("package is too large"));

    const qint64 trailerStart = file.pos() + qint64(payloadCompressed);
    if (trailerStart + kDigestSize + (user ? 0 : kSignatureSize) != file.size())
        return fail(error, QStringLiteral("package is truncated or has trailing garbage"));

    const QString stagingDir = destDir + QStringLiteral(".partial");
    QDir staging(stagingDir);
    if (staging.exists())
        staging.removeRecursively();
    if (!QDir().mkpath(stagingDir))
        return fail(error, QStringLiteral("cannot create %1").arg(stagingDir));

    // Anything that leaves this function without promoting must not leave staging behind.
    struct StagingGuard
    {
        QString path;
        bool promoted = false;
        ~StagingGuard()
        {
            if (!promoted)
                QDir(path).removeRecursively();
        }
    } guard{stagingDir};

    std::unique_ptr<ZSTD_DCtx, DCtxDeleter> dctx(ZSTD_createDCtx());
    if (!dctx)
        return fail(error, QStringLiteral("cannot create a zstd context"));

    PayloadWriter writer(stagingDir, info.files);
    QByteArray inBuffer(kChunkSize, Qt::Uninitialized);
    QByteArray outBuffer(qint64(ZSTD_DStreamOutSize()), Qt::Uninitialized);
    quint64 remaining = payloadCompressed;
    quint64 produced = 0;

    while (remaining > 0) {
        const qint64 want = qMin<qint64>(inBuffer.size(), qint64(remaining));
        const qint64 got = file.read(inBuffer.data(), want);
        if (got <= 0)
            return fail(error, QStringLiteral("truncated payload"));
        digest.addData(QByteArrayView(inBuffer.constData(), got));
        remaining -= quint64(got);

        ZSTD_inBuffer in{inBuffer.constData(), size_t(got), 0};
        while (in.pos < in.size) {
            ZSTD_outBuffer out{outBuffer.data(), size_t(outBuffer.size()), 0};
            const size_t ret = ZSTD_decompressStream(dctx.get(), &out, &in);
            if (ZSTD_isError(ret))
                return fail(error, QStringLiteral("zstd error: %1").arg(QString::fromUtf8(ZSTD_getErrorName(ret))));
            if (!writer.write(outBuffer.constData(), qsizetype(out.pos), error))
                return false;
            produced += out.pos;
        }

        if (progress && !progress(qint64(produced), qint64(payloadRaw)))
            return fail(error, QStringLiteral("cancelled"));
    }

    if (produced != payloadRaw)
        return fail(error, QStringLiteral("payload decompressed to the wrong size"));
    if (!writer.finish(error))
        return false;

    const QByteArray expectedDigest = digest.result();
    const QByteArray actualDigest = file.read(kDigestSize);
    if (actualDigest != expectedDigest)
        return fail(error, QStringLiteral("package contents do not match its digest"));
    if (!user && requireSignature && !verifySignature(actualDigest, file.read(kSignatureSize)))
        return fail(error, QStringLiteral("package signature is not valid for this build of Drift"));

    QDir existing(destDir);
    if (existing.exists() && !existing.removeRecursively())
        return fail(error, QStringLiteral("cannot replace %1").arg(destDir));
    if (!QDir().mkpath(QFileInfo(destDir).absolutePath()))
        return fail(error, QStringLiteral("cannot create the parent of %1").arg(destDir));
    if (!QDir().rename(stagingDir, destDir))
        return fail(error, QStringLiteral("cannot move %1 into place").arg(stagingDir));

    guard.promoted = true;
    if (installed)
        *installed = info;
    return true;
}

bool writeUserPackage(const QString &packageDir, const QString &kind, const QJsonObject &meta,
                      const QString &outPath, QString *error)
{
    const QDir dir(packageDir);
    const QString folder = dir.dirName();

    QStringList relPaths;
    QDirIterator it(packageDir, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext())
        relPaths.append(dir.relativeFilePath(it.next()));
    relPaths.sort();
    if (relPaths.isEmpty())
        return fail(error, QStringLiteral("%1 has no files").arg(packageDir));

    QByteArray payload;
    QJsonArray files;
    for (const QString &rel : std::as_const(relPaths)) {
        QFile file(dir.filePath(rel));
        if (!file.open(QIODevice::ReadOnly))
            return fail(error, QStringLiteral("cannot read %1: %2").arg(file.fileName(), file.errorString()));
        const QByteArray bytes = file.readAll();
        files.append(QJsonObject{
            {QStringLiteral("path"), kind + QLatin1Char('/') + folder + QLatin1Char('/') + rel},
            {QStringLiteral("offset"), double(payload.size())},
            {QStringLiteral("size"), double(bytes.size())},
            {QStringLiteral("sha256"),
             QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex())},
        });
        payload.append(bytes);
    }
    if (quint64(payload.size()) > kMaxUserPayload)
        return fail(error, QStringLiteral("package is too large"));

    QJsonObject root = meta;
    root.insert(QStringLiteral("schema"), 1);
    root.insert(QStringLiteral("platform"), QString());
    root.insert(QStringLiteral("installedSize"), double(payload.size()));
    root.insert(QStringLiteral("files"), files);
    root.insert(QStringLiteral("provides"),
                QJsonArray{QJsonObject{{QStringLiteral("kind"), kind},
                                       {QStringLiteral("root"), kind},
                                       {QStringLiteral("items"), 1}}});
    if (!root.contains(QStringLiteral("minAppVersion")))
        root.insert(QStringLiteral("minAppVersion"), QStringLiteral(DRIFT_VERSION));
    const QByteArray metadata = QJsonDocument(root).toJson(QJsonDocument::Compact);

    QByteArray compressed(qsizetype(ZSTD_compressBound(size_t(payload.size()))), Qt::Uninitialized);
    const size_t packed = ZSTD_compress(compressed.data(), size_t(compressed.size()), payload.constData(),
                                        size_t(payload.size()), 19);
    if (ZSTD_isError(packed))
        return fail(error, QStringLiteral("zstd error: %1").arg(QString::fromUtf8(ZSTD_getErrorName(packed))));
    compressed.resize(qsizetype(packed));

    QByteArray out(kUserMagic, sizeof(kUserMagic));
    appendU32(&out, kFormatVersion);
    appendU32(&out, quint32(metadata.size()));
    out.append(metadata);
    appendU64(&out, quint64(compressed.size()));
    appendU64(&out, quint64(payload.size()));
    out.append(compressed);
    out.append(QCryptographicHash::hash(out, QCryptographicHash::Sha256));

    if (!QDir().mkpath(QFileInfo(outPath).absolutePath()))
        return fail(error, QStringLiteral("cannot create the parent of %1").arg(outPath));
    const QString partial = outPath + QStringLiteral(".partial");
    QFile file(partial);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return fail(error, QStringLiteral("cannot write %1: %2").arg(partial, file.errorString()));
    const bool written = file.write(out) == out.size();
    file.close();
    if (!written) {
        QFile::remove(partial);
        return fail(error, QStringLiteral("write failed: %1").arg(file.errorString()));
    }
    QFile::remove(outPath);
    if (!QFile::rename(partial, outPath)) {
        QFile::remove(partial);
        return fail(error, QStringLiteral("cannot move %1 into place").arg(partial));
    }
    return true;
}

} // namespace drift::addon
