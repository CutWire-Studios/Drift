#include "engine/packages/assets/StickerPackImport.h"

#include "core/base/ZipArchive.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <algorithm>
#include <functional>

namespace {

constexpr qint64 kMaxManifestBytes = 256 * 1024;
constexpr qint64 kMaxImageBytes = 8LL * 1024 * 1024;
constexpr qint64 kMaxTotalBytes = 256LL * 1024 * 1024;
constexpr int kMaxImages = 5000;
constexpr int kMaxZipEntries = 20000;
constexpr int kMaxDirDepth = 6;
constexpr int kCustomOrder = 1000;

const QString kManifestName = QStringLiteral("manifest.json");

QString sanitizeId(const QString &name)
{
    QString id;
    for (const QChar c : name.toLower()) {
        if ((c >= QLatin1Char('a') && c <= QLatin1Char('z')) || (c >= QLatin1Char('0') && c <= QLatin1Char('9')))
            id.append(c);
        else if (!id.isEmpty() && !id.endsWith(QLatin1Char('-')))
            id.append(QLatin1Char('-'));
    }
    while (id.endsWith(QLatin1Char('-')))
        id.chop(1);
    return id.left(40);
}

bool isImage(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    return suffix == QLatin1String("png") || suffix == QLatin1String("webp");
}

// Relative, no "..", no drive or backslash tricks, nothing hidden or from a macOS resource fork.
QString safeRelativePath(const QString &raw)
{
    const QString path = QString(raw).replace(QLatin1Char('\\'), QLatin1Char('/'));
    if (path.isEmpty() || path.startsWith(QLatin1Char('/')) || path.contains(QLatin1Char(':')))
        return {};
    const QStringList parts = path.split(QLatin1Char('/'));
    QStringList kept;
    for (const QString &part : parts) {
        if (part.isEmpty() || part == QLatin1String("."))
            continue;
        if (part == QLatin1String("..") || part.startsWith(QLatin1Char('.')) || part == QLatin1String("__MACOSX"))
            return {};
        kept.append(part);
    }
    return kept.join(QLatin1Char('/'));
}

QString labelFor(const QString &path)
{
    QString label = QFileInfo(path).completeBaseName();
    label.replace(QLatin1Char('_'), QLatin1Char(' ')).replace(QLatin1Char('-'), QLatin1Char(' '));
    return label.simplified();
}

QString titleFor(const QString &folder)
{
    QString title = folder;
    title.replace(QLatin1Char('_'), QLatin1Char(' '));
    return title.simplified();
}

void sortFiles(QStringList *files)
{
    std::sort(files->begin(), files->end(), [](const QString &a, const QString &b) {
        return a.compare(b, Qt::CaseInsensitive) < 0;
    });
}

// Source-agnostic install: `paths` are every candidate file, `read` fetches one under a size cap.
using ReadFile = std::function<bool(const QString &path, qint64 cap, QByteArray *data)>;

StickerPackImportResult install(const QStringList &paths, const QString &fallbackName,
                                const ReadFile &read, const QString &destRoot)
{
    StickerPackImportResult result;

    QByteArray manifest;
    if (paths.contains(kManifestName) && !read(kManifestName, kMaxManifestBytes, &manifest))
        manifest.clear();

    QStringList images;
    for (const QString &path : paths) {
        if (isImage(path))
            images.append(path);
    }
    if (images.size() > kMaxImages) {
        result.error = QCoreApplication::translate("StickerPackImport", "The pack has too many images");
        return result;
    }

    const StickerPackLayout layout = planStickerPack(images, manifest, fallbackName);
    if (layout.categories.isEmpty()) {
        result.error = QCoreApplication::translate("StickerPackImport", "No .png or .webp images found");
        return result;
    }

    result.name = layout.name;
    result.packId = QStringLiteral("custom-") + sanitizeId(layout.name);
    if (result.packId == QLatin1String("custom-"))
        result.packId = QStringLiteral("custom-pack");

    if (!QDir().mkpath(destRoot)) {
        result.error = QCoreApplication::translate("StickerPackImport", "Could not create the sticker folder");
        return result;
    }
    QTemporaryDir staging(QDir(destRoot).filePath(QStringLiteral(".staging-XXXXXX")));
    if (!staging.isValid()) {
        result.error = QCoreApplication::translate("StickerPackImport", "Could not create the sticker folder");
        return result;
    }
    const QString stagingRoot = QDir::cleanPath(staging.path());

    QJsonArray categoriesJson;
    QJsonArray stickersJson;
    QSet<QString> usedIds;
    QSet<QString> copied;
    qint64 total = 0;
    for (const StickerPackLayout::Category &category : layout.categories) {
        const QString categoryId = result.packId + QLatin1Char('-') + category.id;
        categoriesJson.append(QJsonObject{{QStringLiteral("id"), categoryId},
                                          {QStringLiteral("label"), category.label}});
        for (const QString &file : category.files) {
            QByteArray data;
            if (!read(file, kMaxImageBytes, &data) || data.isEmpty())
                continue;
            total += data.size();
            if (total > kMaxTotalBytes) {
                result.error = QCoreApplication::translate("StickerPackImport", "The pack is too large");
                return result;
            }
            const QString target = QDir::cleanPath(stagingRoot + QLatin1Char('/') + file);
            if (!target.startsWith(stagingRoot + QLatin1Char('/')))
                continue;
            if (!copied.contains(file)) {
                QDir().mkpath(QFileInfo(target).absolutePath());
                QFile out(target);
                if (!out.open(QIODevice::WriteOnly) || out.write(data) != data.size()) {
                    result.error = QCoreApplication::translate("StickerPackImport", "Could not write the sticker files");
                    return result;
                }
                copied.insert(file);
            }

            const QString base = categoryId + QLatin1Char('-') + sanitizeId(QFileInfo(file).completeBaseName());
            QString id = base;
            for (int n = 2; usedIds.contains(id); ++n)
                id = base + QLatin1Char('-') + QString::number(n);
            usedIds.insert(id);
            stickersJson.append(QJsonObject{{QStringLiteral("id"), id},
                                            {QStringLiteral("file"), file},
                                            {QStringLiteral("label"), labelFor(file)},
                                            {QStringLiteral("category"), categoryId}});
        }
    }
    if (stickersJson.isEmpty()) {
        result.error = QCoreApplication::translate("StickerPackImport", "No .png or .webp images found");
        return result;
    }
    result.count = int(stickersJson.size());

    const QJsonObject pack{{QStringLiteral("id"), result.packId},
                           {QStringLiteral("name"), layout.name},
                           {QStringLiteral("custom"), true},
                           {QStringLiteral("order"), kCustomOrder},
                           {QStringLiteral("categories"), categoriesJson},
                           {QStringLiteral("stickers"), stickersJson}};
    QFile packFile(QDir(stagingRoot).filePath(QStringLiteral("pack.json")));
    if (!packFile.open(QIODevice::WriteOnly) || packFile.write(QJsonDocument(pack).toJson()) < 0) {
        result.error = QCoreApplication::translate("StickerPackImport", "Could not write the sticker files");
        return result;
    }
    packFile.close();

    const QString finalDir = QDir(destRoot).filePath(result.packId);
    QDir(finalDir).removeRecursively();
    staging.setAutoRemove(false);
    if (!QDir().rename(stagingRoot, finalDir)) {
        QDir(stagingRoot).removeRecursively();
        result.error = QCoreApplication::translate("StickerPackImport", "Could not install the sticker pack");
        return result;
    }
    return result;
}

// A zip usually wraps everything in one folder; that folder is the pack, not a category.
QString commonTopFolder(const QStringList &paths)
{
    QString top;
    for (const QString &path : paths) {
        const int slash = path.indexOf(QLatin1Char('/'));
        if (slash < 0)
            return {};
        const QString first = path.left(slash);
        if (top.isEmpty())
            top = first;
        else if (top != first)
            return {};
    }
    return top;
}

void collectFiles(const QDir &dir, const QString &prefix, int depth, QStringList *out)
{
    if (depth > kMaxDirDepth || out->size() > kMaxZipEntries)
        return;
    const QFileInfoList entries = dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo &info : entries) {
        if (info.isSymLink() || info.fileName().startsWith(QLatin1Char('.')))
            continue;
        const QString rel = prefix + info.fileName();
        if (info.isDir())
            collectFiles(QDir(info.absoluteFilePath()), rel + QLatin1Char('/'), depth + 1, out);
        else
            out->append(rel);
    }
}

} // namespace

StickerPackLayout planStickerPack(const QStringList &imagePaths, const QByteArray &manifestJson,
                                  const QString &fallbackName)
{
    QStringList images;
    for (const QString &raw : imagePaths) {
        const QString path = safeRelativePath(raw);
        if (!path.isEmpty() && isImage(path) && !images.contains(path))
            images.append(path);
    }
    sortFiles(&images);

    StickerPackLayout layout;
    layout.name = fallbackName.trimmed().isEmpty() ? QCoreApplication::translate("StickerPackImport", "Custom stickers") : fallbackName.trimmed();

    const QJsonObject root = QJsonDocument::fromJson(manifestJson).object();
    const QJsonArray declared = root.value(QStringLiteral("categories")).toArray();
    if (const QString name = root.value(QStringLiteral("name")).toString().trimmed(); !name.isEmpty())
        layout.name = name;

    QSet<QString> usedIds;
    const auto addCategory = [&](const QString &rawId, const QString &label, const QStringList &files) {
        if (files.isEmpty())
            return;
        QString id = sanitizeId(rawId.isEmpty() ? label : rawId);
        if (id.isEmpty())
            id = QStringLiteral("stickers");
        const QString base = id;
        for (int n = 2; usedIds.contains(id); ++n)
            id = base + QLatin1Char('-') + QString::number(n);
        usedIds.insert(id);
        layout.categories.append({id, label.isEmpty() ? id : label, files});
    };

    if (!declared.isEmpty()) {
        for (const QJsonValue &value : declared) {
            const QJsonObject object = value.toObject();
            QStringList files;
            for (const QJsonValue &sticker : object.value(QStringLiteral("stickers")).toArray()) {
                const QString path = safeRelativePath(sticker.toString());
                if (images.contains(path) && !files.contains(path))
                    files.append(path);
            }
            const QString folder = safeRelativePath(object.value(QStringLiteral("folder")).toString());
            if (!folder.isEmpty()) {
                for (const QString &path : std::as_const(images)) {
                    if (path.startsWith(folder + QLatin1Char('/')) && !files.contains(path))
                        files.append(path);
                }
            }
            const QString label = object.value(QStringLiteral("name")).toString().trimmed();
            addCategory(object.value(QStringLiteral("id")).toString(), label, files);
        }
        if (!layout.categories.isEmpty())
            return layout;
    }

    QStringList rootFiles;
    QHash<QString, QStringList> byFolder;
    QStringList folderOrder;
    for (const QString &path : std::as_const(images)) {
        const int slash = path.indexOf(QLatin1Char('/'));
        if (slash < 0) {
            rootFiles.append(path);
            continue;
        }
        const QString folder = path.left(slash);
        if (!byFolder.contains(folder))
            folderOrder.append(folder);
        byFolder[folder].append(path);
    }
    addCategory(QString(), layout.name, rootFiles);
    for (const QString &folder : std::as_const(folderOrder))
        addCategory(folder, titleFor(folder), byFolder.value(folder));
    return layout;
}

QString userStickerPacksDir()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
        .filePath(QStringLiteral("stickers"));
}

StickerPackImportResult importStickerPackFromZip(const QString &zipPath, const QString &destRoot)
{
    StickerPackImportResult result;
    const QString archiveName = QFileInfo(zipPath).fileName();

    QFile file(zipPath);
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = QCoreApplication::translate("StickerPackImport", "Could not open %1").arg(archiveName);
        return result;
    }
    if (!drift::zip::looksLikeZip(file.peek(4))) {
        result.error = QCoreApplication::translate("StickerPackImport", "%1 is not a .zip archive").arg(archiveName);
        return result;
    }
    QString zipError;
    const QList<drift::zip::Entry> entries = drift::zip::readEntries(file, &zipError);
    if (entries.size() > kMaxZipEntries) {
        result.error = QCoreApplication::translate("StickerPackImport", "%1 has too many files").arg(archiveName);
        return result;
    }

    QHash<QString, drift::zip::Entry> byPath;
    for (const drift::zip::Entry &entry : entries) {
        if (entry.isDir)
            continue;
        const QString path = safeRelativePath(entry.path);
        if (!path.isEmpty())
            byPath.insert(path, entry);
    }
    if (byPath.isEmpty()) {
        result.error = zipError.isEmpty() ? QCoreApplication::translate("StickerPackImport", "No .png or .webp images found") : zipError;
        return result;
    }

    const QString top = commonTopFolder(byPath.keys());
    const QString prefix = top.isEmpty() ? QString() : top + QLatin1Char('/');
    QStringList paths;
    QHash<QString, drift::zip::Entry> byRelative;
    for (auto it = byPath.cbegin(); it != byPath.cend(); ++it) {
        const QString rel = it.key().mid(prefix.size());
        paths.append(rel);
        byRelative.insert(rel, it.value());
    }

    const ReadFile read = [&](const QString &path, qint64 cap, QByteArray *data) {
        const auto it = byRelative.constFind(path);
        if (it == byRelative.constEnd())
            return false;
        // The header sizes are only the archive's claim, so the inflated size is checked too.
        return it->uncompressedSize <= cap && it->compressedSize <= cap
               && drift::zip::extractEntry(file, *it, *data) && data->size() <= cap;
    };
    return install(paths, top.isEmpty() ? QFileInfo(zipPath).completeBaseName() : titleFor(top), read,
                   destRoot);
}

StickerPackImportResult importStickerPackFromDirectory(const QString &dirPath, const QString &destRoot)
{
    StickerPackImportResult result;
    const QDir base(dirPath);
    if (!base.exists()) {
        result.error = QCoreApplication::translate("StickerPackImport", "Could not open %1").arg(dirPath);
        return result;
    }

    QStringList paths;
    collectFiles(base, QString(), 0, &paths);

    const ReadFile read = [&](const QString &path, qint64 cap, QByteArray *data) {
        const QFileInfo info(base.filePath(path));
        if (!info.isFile() || info.isSymLink() || info.size() > cap)
            return false;
        QFile file(info.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly))
            return false;
        *data = file.readAll();
        return true;
    };
    return install(paths, titleFor(base.dirName()), read, destRoot);
}

bool removeUserStickerPack(const QString &packId, QString *error, const QString &destRoot)
{
    const QString root = QFileInfo(destRoot).canonicalFilePath();
    const QString target = QFileInfo(QDir(destRoot).filePath(packId)).canonicalFilePath();
    bool custom = false;
    if (!root.isEmpty() && !target.isEmpty() && QFileInfo(target).absolutePath() == root) {
        QFile file(QDir(target).filePath(QStringLiteral("pack.json")));
        custom = file.open(QIODevice::ReadOnly)
                 && QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("custom")).toBool();
    }
    if (!custom) {
        if (error)
            *error = QCoreApplication::translate("StickerPackImport", "“%1” is not an imported sticker pack").arg(packId);
        return false;
    }
    if (!QDir(target).removeRecursively()) {
        if (error)
            *error = QCoreApplication::translate("StickerPackImport", "Could not delete “%1”").arg(packId);
        return false;
    }
    return true;
}
