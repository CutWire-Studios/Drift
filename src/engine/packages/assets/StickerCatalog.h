#pragma once

#include <QList>
#include <QString>
#include <QStringList>

// File-based sticker packages: stickers/<pack-id>/{pack.json, *.png}, discovered the same way as
// font, effect and transition packages. Stickers used to be extracted from an emoji font at build
// time and embedded in the QRC; they are now an addon, so an empty catalog is a normal state and
// the UI shows an install prompt instead.

struct StickerEntry
{
    QString id;    // "<pack-id>/<sticker-id>", unique across installed packs
    QString label;
    QString category;
    QString path; // absolute filesystem path
};

struct StickerCategory
{
    QString id;
    QString label;
    QString packId;
    bool custom = false; // from a pack the user imported
};

struct StickerPack
{
    QString id;
    QString name;
    QString license;
    QString packageDir;
    int order = 0;
    bool custom = false; // imported by the user (StickerPackImport.h); removable
    QList<StickerCategory> categories;
    QList<StickerEntry> stickers;
};

const QList<StickerPack> &stickerPacks();

// Merged across packs in display order, de-duplicated by category id.
QList<StickerCategory> stickerCategories();
QList<StickerEntry> stickers();

void reloadStickerCatalog(const QStringList &packageRoots = {});

// Projects saved before stickers became an addon store QRC paths like
// ":/qt/qml/Drift/resources/stickers/grinning.png", which no longer resolve. Map one onto the
// same-named file in an installed pack; returns empty when nothing matches.
QString resolveLegacyStickerPath(const QString &qrcPath);
