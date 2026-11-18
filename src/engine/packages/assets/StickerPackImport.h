#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

// Custom sticker packs the user imports from a .zip or a folder of .png/.webp images. An import
// is normalised into a StickerCatalog package (pack.json + the images) under the user data dir,
// so the catalog, drag and drop, and saved projects treat it like any installed sticker pack.
//
// Optional manifest.json at the pack root (or inside the single top-level folder of a zip):
//   {"name": "My Pack",
//    "categories": [{"id": "faces", "name": "Faces",
//                    "stickers": ["faces/smile.webp", ...]}   // or "folder": "faces"
//                  ]}
// Without one, each top-level subfolder is a category named after the folder, and images at the
// root form one category named after the pack.

struct StickerPackLayout
{
    struct Category
    {
        QString id;
        QString label;
        QStringList files; // pack-relative, forward slashes
    };

    QString name;
    QList<Category> categories;
};

// `imagePaths` are the pack-relative paths found in the source; unsafe paths (absolute, "..",
// hidden) and non-images are ignored.
StickerPackLayout planStickerPack(const QStringList &imagePaths, const QByteArray &manifestJson,
                                  const QString &fallbackName);

struct StickerPackImportResult
{
    QString packId;
    QString name;
    int count = 0;
    QString error; // empty on success
};

QString userStickerPacksDir();

// Replaces an earlier import of the same name.
StickerPackImportResult importStickerPackFromZip(const QString &zipPath, const QString &destRoot);
StickerPackImportResult importStickerPackFromDirectory(const QString &dirPath, const QString &destRoot);

// Only touches packs this importer wrote (pack.json with "custom": true).
bool removeUserStickerPack(const QString &packId, QString *error, const QString &destRoot);
