#include "engine/capture/RecordingLocation.h"

#include "engine/encode/proxies/ProxyNaming.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace drift {

namespace {

// The subfolder a project's recordings go in, so they sit beside the project without mixing into
// whatever else shares its folder.
constexpr const char *kProjectSubdir = "Recordings";

} // namespace

QString recordingLocationToString(RecordingLocation location)
{
    switch (location) {
    case RecordingLocation::Project:
        return QStringLiteral("project");
    case RecordingLocation::Folder:
        return QStringLiteral("folder");
    case RecordingLocation::Movies:
        break;
    }
    return QStringLiteral("movies");
}

std::optional<RecordingLocation> recordingLocationFromString(const QString &text)
{
    if (text == QLatin1String("movies"))
        return RecordingLocation::Movies;
    if (text == QLatin1String("project"))
        return RecordingLocation::Project;
    if (text == QLatin1String("folder"))
        return RecordingLocation::Folder;
    return std::nullopt;
}

QString moviesRecordingDir()
{
    QString root = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    if (root.isEmpty()) {
        // A headless or sandboxed session can have no movies location at all. Home is somewhere
        // the user can still find, which is the whole requirement.
        root = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    }
    if (root.isEmpty())
        return {};
    const QString dir =
        QDir::cleanPath(root + QLatin1Char('/') + QCoreApplication::applicationName());
    return QDir().mkpath(dir) ? dir : QString();
}

QString recordingFileName(const QString &kind, const QDateTime &when, const QString &extension)
{
    const QString label = kind.trimmed().isEmpty() ? QStringLiteral("Recording") : kind.trimmed();
    const QString stamp = when.toString(QStringLiteral("yyyy-MM-dd HH-mm-ss"));
    QString suffix = extension.trimmed();
    while (suffix.startsWith(QLatin1Char('.')))
        suffix.remove(0, 1);
    const QString leaf = stamp.isEmpty() ? label : label + QLatin1Char(' ') + stamp;
    return suffix.isEmpty() ? leaf : leaf + QLatin1Char('.') + suffix;
}

QString uniqueRecordingPath(const QString &dir, const QString &fileName)
{
    if (dir.isEmpty() || fileName.isEmpty())
        return {};

    const QDir folder(dir);
    const QFileInfo info(fileName);
    const QString stem = info.completeBaseName();
    const QString suffix = info.suffix();

    QString candidate = folder.filePath(fileName);
    for (int n = 2; QFileInfo::exists(candidate); ++n) {
        const QString leaf = suffix.isEmpty()
                                 ? QStringLiteral("%1 %2").arg(stem).arg(n)
                                 : QStringLiteral("%1 %2.%3").arg(stem).arg(n).arg(suffix);
        candidate = folder.filePath(leaf);
    }
    return candidate;
}

RecordingTarget chooseRecordingTarget(RecordingLocation mode, const QString &folder,
                                      const QString &projectPath, const QString &fileName)
{
    RecordingTarget target;
    if (fileName.isEmpty())
        return target;

    if (mode == RecordingLocation::Folder) {
        // Not created: a folder that has gone missing is usually an unmounted drive, and silently
        // recreating it on the mount point would hide the footage from the user's own backups.
        if (!folder.isEmpty() && QDir(folder).exists()) {
            const QString candidate = uniqueRecordingPath(folder, fileName);
            if (!candidate.isEmpty() && canWriteBeside(candidate)) {
                target.where = RecordingTarget::Where::Folder;
                target.path = candidate;
                return target;
            }
        }
        target.folderUnavailable = true;
    } else if (mode == RecordingLocation::Project) {
        const QString projectDir =
            projectPath.isEmpty() ? QString() : QFileInfo(projectPath).absolutePath();
        if (!projectDir.isEmpty() && QDir(projectDir).exists()) {
            const QString dir =
                QDir::cleanPath(projectDir + QLatin1Char('/') + QLatin1String(kProjectSubdir));
            if (QDir().mkpath(dir)) {
                const QString candidate = uniqueRecordingPath(dir, fileName);
                if (!candidate.isEmpty() && canWriteBeside(candidate)) {
                    target.where = RecordingTarget::Where::Project;
                    target.path = candidate;
                    return target;
                }
            }
        }
        target.projectUnavailable = true;
    }

    const QString movies = moviesRecordingDir();
    if (movies.isEmpty())
        return target; // path stays empty: the caller has to refuse to record
    target.where = RecordingTarget::Where::Movies;
    target.path = uniqueRecordingPath(movies, fileName);
    return target;
}

} // namespace drift
