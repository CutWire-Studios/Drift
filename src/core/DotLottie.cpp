#include "DotLottie.h"

#include "ZipArchive.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

namespace drift {

bool isDotLottiePath(const QString &path)
{
    return QFileInfo(path).suffix().compare(QLatin1String("lottie"), Qt::CaseInsensitive) == 0;
}

namespace {

bool isAnimationEntry(const QString &path)
{
    return path.endsWith(QLatin1String(".json"), Qt::CaseInsensitive)
           && (path.startsWith(QLatin1String("animations/")) || path.startsWith(QLatin1String("a/")));
}

bool isImageEntry(const QString &path)
{
    return path.startsWith(QLatin1String("images/")) || path.startsWith(QLatin1String("i/"));
}

} // namespace

QStringList unpackDotLottie(const QString &archivePath, const QString &destRoot, QString *error)
{
    auto fail = [&](const QString &message) {
        if (error)
            *error = message;
        return QStringList();
    };

    QFile file(archivePath);
    if (!file.open(QIODevice::ReadOnly))
        return fail(QStringLiteral("cannot open ") + archivePath);
    if (!zip::looksLikeZip(file.peek(4)))
        return fail(QStringLiteral("not a .lottie bundle (not a zip archive)"));

    QString zipError;
    const QList<zip::Entry> entries = zip::readEntries(file, &zipError);
    if (entries.isEmpty())
        return fail(zipError.isEmpty() ? QStringLiteral("empty .lottie bundle") : zipError);

    // Manifest order first, so a multi-animation bundle imports in the order its author listed.
    QStringList animationIds;
    for (const zip::Entry &entry : entries) {
        if (entry.path != QLatin1String("manifest.json"))
            continue;
        QByteArray manifest;
        if (!zip::extractEntry(file, entry, manifest))
            break;
        const QJsonArray animations =
            QJsonDocument::fromJson(manifest).object().value(QStringLiteral("animations")).toArray();
        for (const QJsonValue &value : animations) {
            const QString id = value.toObject().value(QStringLiteral("id")).toString();
            if (!id.isEmpty())
                animationIds.append(id);
        }
        break;
    }

    QList<zip::Entry> animations;
    QList<zip::Entry> images;
    for (const zip::Entry &entry : entries) {
        if (entry.isDir)
            continue;
        if (isAnimationEntry(entry.path))
            animations.append(entry);
        else if (isImageEntry(entry.path))
            images.append(entry);
    }
    if (animations.isEmpty())
        return fail(QStringLiteral("the bundle holds no animation"));
    // Manifest order, then anything the manifest forgot.
    std::stable_sort(animations.begin(), animations.end(), [&](const zip::Entry &a, const zip::Entry &b) {
        auto rank = [&](const zip::Entry &e) {
            const int i = animationIds.indexOf(QFileInfo(e.path).completeBaseName());
            return i < 0 ? animationIds.size() : i;
        };
        return rank(a) < rank(b);
    });

    // Content-addressed so re-importing the same bundle reuses the folder and never rewrites
    // files a project already references.
    file.seek(0);
    QCryptographicHash hasher(QCryptographicHash::Sha256);
    hasher.addData(&file);
    const QString folder = QDir(destRoot).filePath(QString::fromLatin1(hasher.result().toHex().left(24)));
    if (!QDir().mkpath(folder))
        return fail(QStringLiteral("cannot create ") + folder);

    const QString baseName = QFileInfo(archivePath).completeBaseName();
    QStringList out;
    QSet<QString> used;
    for (const zip::Entry &entry : animations) {
        QByteArray data;
        if (!zip::extractEntry(file, entry, data) || data.isEmpty())
            continue;
        const QString id = QFileInfo(entry.path).completeBaseName();
        QString name = animations.size() == 1 ? baseName : baseName + QLatin1Char('-') + id;
        if (used.contains(name))
            name += QLatin1Char('-') + id;
        used.insert(name);
        const QString target = QDir(folder).filePath(name + QStringLiteral(".json"));
        if (!QFile::exists(target)) {
            QFile outFile(target);
            if (!outFile.open(QIODevice::WriteOnly) || outFile.write(data) != data.size())
                return fail(QStringLiteral("cannot write ") + target);
        }
        out.append(target);
    }
    for (const zip::Entry &entry : images) {
        const QString target = QDir(folder).filePath(entry.path);
        if (QFile::exists(target))
            continue;
        QByteArray data;
        if (!zip::extractEntry(file, entry, data))
            continue;
        QDir().mkpath(QFileInfo(target).absolutePath());
        QFile outFile(target);
        if (outFile.open(QIODevice::WriteOnly))
            outFile.write(data);
    }
    if (out.isEmpty())
        return fail(QStringLiteral("the bundle's animations could not be extracted"));
    return out;
}

} // namespace drift
