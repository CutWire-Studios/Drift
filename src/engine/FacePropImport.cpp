#include "FacePropImport.h"

#include "core/ZipArchive.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QRegularExpression>

#include <cmath>
#include <functional>

namespace {

constexpr qint64 kMaxManifestBytes = 64 * 1024;
constexpr qint64 kMaxModelBytes = 64LL * 1024 * 1024;
constexpr qint64 kMaxThumbnailBytes = 8LL * 1024 * 1024;
constexpr int kMaxProps = 500;
constexpr int kMaxZipEntries = 20000;
constexpr int kMaxDirDepth = 4;

const QString kManifestName = QStringLiteral("prop.json");

bool validId(const QString &id)
{
    static const QRegularExpression re(QStringLiteral("^[a-z0-9][a-z0-9_-]{0,63}$"));
    return re.match(id).hasMatch();
}

QString sanitizeId(const QString &name)
{
    QString id;
    for (const QChar c : name.toLower()) {
        if ((c >= QLatin1Char('a') && c <= QLatin1Char('z')) || (c >= QLatin1Char('0') && c <= QLatin1Char('9'))
            || c == QLatin1Char('_') || c == QLatin1Char('-'))
            id.append(c);
        else if (!id.isEmpty() && !id.endsWith(QLatin1Char('-')))
            id.append(QLatin1Char('-'));
    }
    while (!id.isEmpty() && !id.front().isLetterOrNumber())
        id.remove(0, 1);
    return id.left(64);
}

// A file the manifest names must sit in the prop folder itself: no directories, no escape.
bool plainFileName(const QString &name)
{
    return !name.isEmpty() && name != QLatin1String(".") && name != QLatin1String("..")
           && !name.contains(QLatin1Char('/')) && !name.contains(QLatin1Char('\\'))
           && !name.contains(QLatin1Char(':'));
}

// Reads one file of a prop folder by name, refusing it before reading when it is over `cap`.
using PropFileReader =
    std::function<bool(const QString &name, qint64 cap, QByteArray *data, QString *error)>;

bool writeFile(const QString &path, const QByteArray &data)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}

bool installProp(const QString &destRoot, const QString &fallbackId, const QByteArray &manifestJson,
                 const PropFileReader &read, QString *installedId, QString *error)
{
    FacePropManifest manifest;
    if (!parseFacePropManifest(manifestJson, fallbackId, &manifest, error))
        return false;

    QByteArray model;
    if (!read(manifest.model, kMaxModelBytes, &model, error))
        return false;
    if (!model.startsWith("glTF")) {
        *error = QCoreApplication::translate("FacePropImport", "%1 is not a binary glTF model")
                     .arg(manifest.model);
        return false;
    }
    // A prop without its thumbnail still works; the picker shows a placeholder tile.
    QByteArray thumbnail;
    QString ignored;
    if (!manifest.thumbnail.isEmpty() && !read(manifest.thumbnail, kMaxThumbnailBytes, &thumbnail, &ignored))
        manifest.thumbnail.clear();

    QDir root(destRoot);
    const QString failedWrite =
        QCoreApplication::translate("FacePropImport", "Could not write to %1").arg(destRoot);
    if (!root.mkpath(QStringLiteral("."))) {
        *error = failedWrite;
        return false;
    }
    const QString stagingName = QStringLiteral(".staging-%1-%2")
                                    .arg(manifest.id)
                                    .arg(QRandomGenerator::global()->generate(), 8, 16, QLatin1Char('0'));
    QDir staging(root.filePath(stagingName));
    if (!root.mkdir(stagingName)
        || !writeFile(staging.filePath(kManifestName), manifestJson)
        || !writeFile(staging.filePath(manifest.model), model)
        || (!manifest.thumbnail.isEmpty() && !writeFile(staging.filePath(manifest.thumbnail), thumbnail))) {
        staging.removeRecursively();
        *error = failedWrite;
        return false;
    }

    // Replace, not merge: files the new version no longer ships must not linger.
    QDir existing(root.filePath(manifest.id));
    if ((existing.exists() && !existing.removeRecursively()) || !root.rename(stagingName, manifest.id)) {
        staging.removeRecursively();
        *error = failedWrite;
        return false;
    }
    *installedId = manifest.id;
    return true;
}

void installInto(FacePropImportResult *result, const QString &destRoot, const QString &source,
                 const QString &fallbackId, const QByteArray &manifestJson, const PropFileReader &read)
{
    QString id;
    QString error;
    if (installProp(destRoot, fallbackId, manifestJson, read, &id, &error))
        result->installedIds.append(id);
    else
        result->errors.append(QStringLiteral("%1: %2").arg(source, error));
}

void collectPropDirs(const QDir &dir, int depth, QStringList *out)
{
    if (out->size() > kMaxProps)
        return;
    if (QFileInfo(dir.filePath(kManifestName)).isFile()) {
        out->append(dir.absolutePath());
        return;
    }
    if (depth >= kMaxDirDepth)
        return;
    for (const QFileInfo &sub : dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks, QDir::Name))
        collectPropDirs(QDir(sub.absoluteFilePath()), depth + 1, out);
}

} // namespace

bool parseFacePropManifest(const QByteArray &json, const QString &fallbackId,
                           FacePropManifest *out, QString *error)
{
    const auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (!doc.isObject())
        return fail(QCoreApplication::translate("FacePropImport", "prop.json is not valid JSON"));
    const QJsonObject root = doc.object();

    if (root.value(QStringLiteral("type")).toString() != QLatin1String("face-prop"))
        return fail(QCoreApplication::translate("FacePropImport", "prop.json is not a face prop"));
    if (root.value(QStringLiteral("schema")).toInt(1) > 1)
        return fail(QCoreApplication::translate("FacePropImport",
                                                "prop.json needs a newer version of Drift"));

    FacePropManifest m;
    m.id = root.contains(QStringLiteral("id")) ? root.value(QStringLiteral("id")).toString()
                                               : sanitizeId(fallbackId);
    if (!validId(m.id))
        return fail(QCoreApplication::translate("FacePropImport", "invalid prop id “%1”").arg(m.id));
    m.name = root.value(QStringLiteral("name")).toString().trimmed();
    if (m.name.isEmpty())
        m.name = m.id;

    m.model = root.value(QStringLiteral("model")).toString();
    if (!plainFileName(m.model) || !m.model.endsWith(QLatin1String(".glb"), Qt::CaseInsensitive))
        return fail(QCoreApplication::translate("FacePropImport",
                                                "prop.json must name a .glb model in the prop folder"));
    m.thumbnail = root.value(QStringLiteral("thumbnail")).toString();
    if (!m.thumbnail.isEmpty() && !plainFileName(m.thumbnail))
        return fail(QCoreApplication::translate("FacePropImport",
                                                "prop.json thumbnail must be a file in the prop folder"));
    if (m.thumbnail == m.model || m.thumbnail == kManifestName)
        return fail(QCoreApplication::translate("FacePropImport", "prop.json names the same file twice"));

    m.description = root.value(QStringLiteral("description")).toString();
    m.license = root.value(QStringLiteral("license")).toString();
    for (const QJsonValue &tag : root.value(QStringLiteral("tags")).toArray()) {
        if (tag.isString())
            m.tags.append(tag.toString());
    }

    static const QStringList numericKeys{
        QStringLiteral("scale"),         QStringLiteral("offsetX"),
        QStringLiteral("offsetY"),       QStringLiteral("offsetZ"),
        QStringLiteral("rotX"),          QStringLiteral("rotY"),
        QStringLiteral("rotZ"),          QStringLiteral("occlusionSize"),
        QStringLiteral("occlusionOffset"), QStringLiteral("occlusionDepth"),
    };
    const QJsonObject params = root.value(QStringLiteral("params")).toObject();
    for (const QString &key : numericKeys) {
        if (!params.contains(key))
            continue;
        const QJsonValue v = params.value(key);
        if (!v.isDouble() || !std::isfinite(v.toDouble()))
            return fail(QCoreApplication::translate("FacePropImport", "param “%1” must be a number").arg(key));
        m.params.insert(key, v.toDouble());
    }
    if (params.contains(QStringLiteral("occlusion"))) {
        const QJsonValue v = params.value(QStringLiteral("occlusion"));
        if (!v.isBool())
            return fail(QCoreApplication::translate("FacePropImport", "param “occlusion” must be true or false"));
        m.params.insert(QStringLiteral("occlusion"), v.toBool());
    }

    *out = m;
    return true;
}

FacePropImportResult importFacePropsFromZip(const QString &zipPath, const QString &destRoot)
{
    FacePropImportResult result;
    const QString archiveName = QFileInfo(zipPath).fileName();

    QFile file(zipPath);
    if (!file.open(QIODevice::ReadOnly)) {
        result.errors.append(QCoreApplication::translate("FacePropImport", "Could not open %1").arg(archiveName));
        return result;
    }
    if (!drift::zip::looksLikeZip(file.peek(4))) {
        result.errors.append(QCoreApplication::translate(
            "FacePropImport", "%1 is not a .zip archive. Other archive formats are not supported.")
                                 .arg(archiveName));
        return result;
    }
    QString zipError;
    const QList<drift::zip::Entry> entries = drift::zip::readEntries(file, &zipError);
    if (entries.size() > kMaxZipEntries) {
        result.errors.append(QCoreApplication::translate("FacePropImport", "%1 has too many files")
                                 .arg(archiveName));
        return result;
    }

    QHash<QString, drift::zip::Entry> byPath;
    QStringList manifests;
    for (const drift::zip::Entry &entry : entries) {
        if (entry.isDir)
            continue;
        const QString path = QString(entry.path).replace(QLatin1Char('\\'), QLatin1Char('/'));
        byPath.insert(path, entry);
        if (path == kManifestName || path.endsWith(QLatin1Char('/') + kManifestName))
            manifests.append(path);
    }
    if (manifests.isEmpty()) {
        result.errors.append(zipError.isEmpty()
                                 ? QCoreApplication::translate("FacePropImport", "No face props (prop.json) found in %1")
                                       .arg(archiveName)
                                 : zipError);
        return result;
    }
    if (manifests.size() > kMaxProps) {
        result.errors.append(QCoreApplication::translate("FacePropImport", "%1 has too many props")
                                 .arg(archiveName));
        return result;
    }

    const auto extract = [&](const QString &path, qint64 cap, QByteArray *data, QString *error) {
        const auto it = byPath.constFind(path);
        if (it == byPath.constEnd()) {
            *error = QCoreApplication::translate("FacePropImport", "%1 is missing")
                         .arg(path.section(QLatin1Char('/'), -1));
            return false;
        }
        // Checked before extracting so a hostile archive cannot make us inflate gigabytes; the
        // size after is checked too, since the header's size is only the archive's claim.
        if (it->uncompressedSize > cap || it->compressedSize > cap || !drift::zip::extractEntry(file, *it, *data)
            || data->size() > cap) {
            *error = QCoreApplication::translate("FacePropImport", "%1 is too large or damaged")
                         .arg(path.section(QLatin1Char('/'), -1));
            return false;
        }
        return true;
    };

    for (const QString &manifestPath : manifests) {
        const QString prefix = manifestPath.left(manifestPath.size() - kManifestName.size());
        const QString folder = prefix.isEmpty() ? QFileInfo(zipPath).completeBaseName()
                                                : prefix.section(QLatin1Char('/'), -2, -2);
        const QString source = prefix.isEmpty() ? archiveName : prefix.chopped(1);
        QByteArray manifestJson;
        QString error;
        if (!extract(manifestPath, kMaxManifestBytes, &manifestJson, &error)) {
            result.errors.append(QStringLiteral("%1: %2").arg(source, error));
            continue;
        }
        installInto(&result, destRoot, source, folder, manifestJson,
                    [&](const QString &name, qint64 cap, QByteArray *data, QString *err) {
                        return extract(prefix + name, cap, data, err);
                    });
    }
    return result;
}

FacePropImportResult importFacePropsFromDirectory(const QString &dirPath, const QString &destRoot)
{
    FacePropImportResult result;
    const QDir base(dirPath);
    if (!base.exists()) {
        result.errors.append(QCoreApplication::translate("FacePropImport", "Could not open %1").arg(dirPath));
        return result;
    }

    QStringList propDirs;
    collectPropDirs(base, 0, &propDirs);
    if (propDirs.isEmpty()) {
        result.errors.append(QCoreApplication::translate("FacePropImport", "No face props (prop.json) found in %1")
                                 .arg(base.dirName()));
        return result;
    }
    if (propDirs.size() > kMaxProps) {
        result.errors.append(QCoreApplication::translate("FacePropImport", "%1 has too many props")
                                 .arg(base.dirName()));
        return result;
    }

    for (const QString &propDir : propDirs) {
        const QDir dir(propDir);
        const auto read = [&](const QString &name, qint64 cap, QByteArray *data, QString *error) {
            const QFileInfo info(dir.filePath(name));
            if (!info.isFile() || info.isSymLink()) {
                *error = QCoreApplication::translate("FacePropImport", "%1 is missing").arg(name);
                return false;
            }
            QFile file(info.absoluteFilePath());
            if (info.size() > cap || !file.open(QIODevice::ReadOnly)) {
                *error = QCoreApplication::translate("FacePropImport", "%1 is too large or unreadable").arg(name);
                return false;
            }
            *data = file.readAll();
            return true;
        };
        QByteArray manifestJson;
        QString error;
        if (!read(kManifestName, kMaxManifestBytes, &manifestJson, &error)) {
            result.errors.append(QStringLiteral("%1: %2").arg(dir.dirName(), error));
            continue;
        }
        installInto(&result, destRoot, dir.dirName(), dir.dirName(), manifestJson, read);
    }
    return result;
}

bool removeUserFaceProp(const QString &id, QString *error, const QString &destRoot)
{
    // Canonical on both sides, so neither ".." in the id nor a symlink can reach outside the root.
    const QString root = QFileInfo(destRoot).canonicalFilePath();
    const QString target = QFileInfo(QDir(destRoot).filePath(id)).canonicalFilePath();
    if (root.isEmpty() || target.isEmpty() || QFileInfo(target).absolutePath() != root
        || !QFileInfo(target).isDir()) {
        if (error)
            *error = QCoreApplication::translate("FacePropImport", "“%1” is not an imported face prop").arg(id);
        return false;
    }
    if (!QDir(target).removeRecursively()) {
        if (error)
            *error = QCoreApplication::translate("FacePropImport", "Could not delete “%1”").arg(id);
        return false;
    }
    return true;
}
