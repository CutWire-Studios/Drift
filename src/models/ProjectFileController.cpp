#include "ProjectFileController.h"

#include "AppController.h"

#include "AssetLibrary.h"
#include "core/EdlReader.h"
#include "core/KdenliveReader.h"
#include "core/MogrtReader.h"
#include "core/OtioReader.h"
#include "core/PrprojReader.h"
#include "core/ResolveReader.h"
#include "engine/AddonRegistry.h"
#include "engine/AndroidUri.h"
#include "engine/AudioFileWriter.h"
#include "engine/EffectCatalog.h"
#include "engine/Exporter.h"
#include "engine/ProjectDependencies.h"
#include "engine/FaceTrack.h"
#include "engine/MatteWriter.h"
#include "engine/MediaEditor.h"
#include "engine/TransitionCatalog.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>
#include <QtConcurrent>

#include <algorithm>
#include <functional>

// Shared with export and asset replace, which stay in AppController.cpp.
QString writeTargetPath(const QUrl &url, const QString &suffix = {});
QString readTargetPath(const QUrl &url);
bool writeTargetIsDisposable(const QUrl &url);
bool commitWriteTarget(const QString &staged, const QUrl &url,
                       const std::function<bool(qint64, qint64)> &progress, QString *error);
void discardWriteTarget(const QString &staged, const QUrl &url, bool deleteDestination = false);
QString projectLocation(const QUrl &url);
QString projectNameForUrl(const QUrl &url);
bool projectLocationExists(const QString &path);
#ifdef Q_OS_ANDROID
QString materializeContentUrl(const QUrl &url);
#endif

ProjectFileController::ProjectFileController(AppController *app, QObject *parent)
    : QObject(parent)
    , m_app(app)
{
    // Periodically snapshot unsaved work to a recovery file so a crash doesn't
    // lose progress. A confirmed close (Save or Don't Save) clears dirty first,
    // so aboutToQuit only writes this when quit was interrupted (SIGTERM, kill).
    // The file is also removed when the user saves, loads another project,
    // starts fresh, or discards recovery.
    m_autosaveTimer = new QTimer(this);
    m_autosaveTimer->setInterval(kAutosaveIntervalMs);
    connect(m_autosaveTimer, &QTimer::timeout, this, [this] {
        if (m_dirty)
            writeRecoveryFile();
    });
    m_autosaveTimer->start();
    connect(qApp, &QCoreApplication::aboutToQuit, this, [this] {
        // Remember the open project so opt-in reopen can load a clean .drift next launch.
        QSettings().setValue(QStringLiteral("lastSessionPath"), m_currentProjectPath);
        if (m_dirty)
            writeRecoveryFile(true);
        else
            m_recoveryWrite.waitForFinished();
    });
}

namespace {
// Every string in a project document, without deserializing it. Used to work out what the
// recovery snapshot still points at: collecting all strings rather than the path-shaped ones
// deliberately errs wide, because every entry here only spares a file from being swept.
void collectJsonStrings(const QJsonValue &value, QSet<QString> *out)
{
    if (value.isString()) {
        out->insert(value.toString());
    } else if (value.isArray()) {
        const QJsonArray array = value.toArray();
        for (const QJsonValue &item : array)
            collectJsonStrings(item, out);
    } else if (value.isObject()) {
        const QJsonObject object = value.toObject();
        for (auto it = object.constBegin(); it != object.constEnd(); ++it)
            collectJsonStrings(it.value(), out);
    }
}
} // namespace

// Every packaged project ever opened leaves its media unpacked under <AppData>/projects/<id>. Drop
// the ones no project in the recents list can still be pointing at, old stabilized renders nothing
// points at, and on Android the derived artifacts nothing points at either.
void ProjectFileController::sweepExtractionDirs()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty())
        return;

    QSet<QString> live;
    QSet<QString> liveFiles; // files outside any bundle that a known project still points at

    // A project's own id is not the only directory it depends on: a Save As copy gets a fresh id
    // but inherits the original's freeze frames, captures and media edits, which stay where they
    // were written. Liveness therefore follows the references as well, or the first sweep after
    // the original left the recents list would take the copy's media with it.
    const QString projectsRoot = QDir::cleanPath(QDir(base).filePath(QStringLiteral("projects")));
    const auto keepOwnerOf = [&](const QString &path) {
        if (path.isEmpty())
            return;
        const QString clean = QDir::cleanPath(path);
        if (!clean.startsWith(projectsRoot + QLatin1Char('/')))
            return;
        const QString rest = clean.mid(projectsRoot.size() + 1);
        const int slash = rest.indexOf(QLatin1Char('/'));
        live.insert(slash < 0 ? rest : rest.left(slash));
    };

    for (const QVariant &entry : recentProjects()) {
        const QString path = entry.toMap().value(QStringLiteral("path")).toString();
        QString error;
        const auto info = drift::bundle::readManifest(path, &error);
        if (!info)
            continue;
        live.insert(info->projectId);
        for (const drift::bundle::MediaEntry &media : info->media) {
            // Only referencing entries name a path on this machine; an embedded one records where
            // the file was on whatever machine packed it, and keepOwnerOf simply will not match.
            if (!media.embedded)
                keepOwnerOf(media.originalPath);
        }
        for (const drift::bundle::MediaEntry &media : info->media)
            liveFiles.insert(media.originalPath);
    }

    // The recovery snapshot is the only record of a session that was never saved, and it is a
    // project document like any other: its id names an extraction dir that appears in no manifest
    // — freeze frames are written straight into it — and its paths name derived artifacts nothing
    // else refers to.
    QJsonObject recovery;
    {
        QFile file(recoveryFilePath());
        if (file.open(QIODevice::ReadOnly))
            recovery = QJsonDocument::fromJson(file.readAll()).object();
    }
    collectJsonStrings(recovery, &liveFiles);
    for (const drift::MediaAsset &asset : m_app->m_project.assets())
        liveFiles.insert(asset.path);
#ifdef Q_OS_ANDROID
    const QString recoveryId = recovery.value(QStringLiteral("id")).toString();
    if (!recoveryId.isEmpty())
        live.insert(recoveryId);
    live.insert(m_app->m_project.id());
#endif

    QDir root(QDir(base).filePath(QStringLiteral("projects")));
    if (root.exists()) {
        const QFileInfoList dirs = root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QFileInfo &dir : dirs) {
            if (!live.contains(dir.fileName()))
                QDir(dir.absoluteFilePath()).removeRecursively();
        }
    }

    // Stabilized renders used to be written here, one per run, and a re-run or an undo stranded
    // the old one — gigabytes each at the old bitrate. New renders are project media, so the only
    // renders left are those of older projects, which the same embed-on-save reasoning as the
    // derived artifacts below makes safe to reclaim. The motion analyses beside them are a cache
    // keyed by source range; one unused for a week is dropped.
    if (const QString stabilizationDir = drift::stabilizationCacheDir(); !stabilizationDir.isEmpty()) {
        const QDateTime staleBefore = QDateTime::currentDateTime().addDays(-7);
        const QFileInfoList files = QDir(stabilizationDir).entryInfoList(QDir::Files);
        for (const QFileInfo &file : files) {
            const bool analysis = file.fileName().endsWith(QLatin1String(".trf"));
            if (analysis ? file.lastModified() < staleBefore
                         : !liveFiles.contains(file.absoluteFilePath()))
                QFile::remove(file.absoluteFilePath());
        }
    }
    // Left by the live vid.stab preview path and by the /tmp staging the filtergraph parser once
    // forced; nothing reads either any more.
    QDir(QDir(base).filePath(QStringLiteral("stabilization_temp"))).removeRecursively();
    for (const QFileInfo &file : QDir::temp().entryInfoList({QStringLiteral("drift-stab-*")},
                                                            QDir::Files | QDir::System))
        QFile::remove(file.absoluteFilePath());

#ifdef Q_OS_ANDROID
    // Mattes, face tracks and denoised audio are written under uuid names with no owner recorded
    // anywhere, so an undo or an abandoned project strands them for good. They are always
    // *embedded* when a project is saved (bundle::collectMedia), which is what makes reclaiming
    // them safe: the copy here is the last one for exactly two kinds of project — an unsaved
    // session, which the recovery snapshot above accounts for, and one saved but not yet reopened,
    // whose manifest still names this path. For anything else, opening the bundle re-extracts it.
    const QString derivedDirs[] = {drift::matteCacheDir(), drift::faceTrackCacheDir(),
                                   drift::denoiseCacheDir()};
    for (const QString &dirPath : derivedDirs) {
        if (dirPath.isEmpty())
            continue;
        const QFileInfoList files = QDir(dirPath).entryInfoList(QDir::Files);
        for (const QFileInfo &file : files) {
            if (!liveFiles.contains(file.absoluteFilePath()))
                QFile::remove(file.absoluteFilePath());
        }
    }

    // <AppData>/imports is deliberately NOT swept. It holds the app's only copy of every SAF
    // document ever imported and a saved project points straight at those bytes, so a wrong sweep
    // there would be data loss rather than a re-derivable cache miss.
#endif
}
QString ProjectFileController::projectName() const
{
    return m_app->m_project.name();
}
void ProjectFileController::setProjectName(const QString &name)
{
    if (m_app->m_project.name() == name)
        return;

    m_app->m_project.setName(name);
    setDirty(true);
    emit projectNameChanged();
    emit projectMetadataChanged();
}

QVariantMap ProjectFileController::projectMetadata() const
{
    return QVariantMap{
        {QStringLiteral("title"), m_app->m_project.name()},
        {QStringLiteral("author"), m_app->m_project.author()},
        {QStringLiteral("description"), m_app->m_project.description()},
        {QStringLiteral("createdAt"), m_app->m_project.createdAt().toLocalTime()},
        {QStringLiteral("modifiedAt"), m_app->m_project.modifiedAt().toLocalTime()},
    };
}

void ProjectFileController::setProjectMetadata(const QString &title, const QString &author,
                                       const QString &description)
{
    const bool nameChanged = m_app->m_project.name() != title;
    if (!nameChanged && m_app->m_project.author() == author && m_app->m_project.description() == description)
        return;

    if (nameChanged)
        m_app->m_project.setName(title);
    m_app->m_project.setAuthor(author);
    m_app->m_project.setDescription(description);

    // The next project starts from whoever the user is now, so they only type it once.
    QSettings().setValue(QStringLiteral("authorName"), author);

    setDirty(true);
    if (nameChanged)
        emit projectNameChanged();
    emit projectMetadataChanged();
}
void ProjectFileController::reportMissingCatalogEntries()
{
    QSet<QString> missingEffects;
    QSet<QString> missingTransitions;

    for (const drift::Track &track : m_app->m_project.tracks()) {
        for (const drift::Clip &clip : track.clips) {
            for (const drift::Effect &effect : clip.effects) {
                if (!effect.catalogId.isEmpty() && !effectDefForId(effect.catalogId))
                    missingEffects.insert(effect.catalogId);
            }
        }
        for (const drift::Transition &transition : track.transitions) {
            if (!transition.kindId.isEmpty() && !transitionDefForId(transition.kindId))
                missingTransitions.insert(transition.kindId);
        }
    }

    const int total = missingEffects.size() + missingTransitions.size();
    if (total == 0)
        return;

    QStringList names = QStringList(missingEffects.begin(), missingEffects.end())
                        + QStringList(missingTransitions.begin(), missingTransitions.end());
    names.sort();
    const QString sample = names.mid(0, 3).join(QStringLiteral(", "));

    m_app->setLastMessage(total == 1
                       ? tr("This project uses \"%1\", which isn’t installed — it "
                            "won’t show. Open Extras to install it.").arg(sample)
                       : tr("This project uses %1 effects or transitions that aren’t "
                            "installed (%2%3) — they won’t show. Open Extras to install them.")
                             .arg(total)
                             .arg(sample, names.size() > 3 ? tr(", …") : QString()));
}
int ProjectFileController::projectWidth() const
{
    return m_app->m_project.width();
}

int ProjectFileController::projectHeight() const
{
    return m_app->m_project.height();
}

int ProjectFileController::projectFps() const
{
    return m_app->m_project.fps();
}

void ProjectFileController::setProjectResolution(int width, int height)
{
    setProjectSetup(width, height, m_app->m_project.fps());
}

void ProjectFileController::setProjectFps(int fps)
{
    setProjectSetup(m_app->m_project.width(), m_app->m_project.height(), fps);
}

void ProjectFileController::setProjectSetup(int width, int height, int fps)
{
    width = qBound(16, width, 7680);
    height = qBound(16, height, 4320);
    fps = qBound(1, fps, 240);
    if (m_app->m_project.width() == width && m_app->m_project.height() == height && m_app->m_project.fps() == fps)
        return;

    // Answering the first-run layout chooser is not an edit — it is the project taking its initial
    // shape. Pushing an undo command here is what used to leave a brand-new project dirty with one
    // entry on a freshly cleared stack. ProjectSetupDialog runs after the first clip was added, so
    // the stack is non-empty by then and it still gets its undo step.
    const bool pristine =
        m_app->m_undoStack.count() == 0 && !m_dirty && m_currentProjectPath.isEmpty();

    const drift::Project before = m_app->m_project;
    if (m_app->m_project.width() != width || m_app->m_project.height() != height)
        drift::rebaseClipLayout(m_app->m_project, m_app->m_project.width(), m_app->m_project.height(), width, height, 0.0, 0.0);
    m_app->m_project.setResolution(width, height);
    const bool fpsChanged = m_app->m_project.fps() != fps;
    m_app->m_project.setFps(fps);
    if (!pristine)
        m_app->pushProjectEdit(before, fpsChanged && width == before.width() && height == before.height()
                                    ? tr("Frame rate")
                                    : tr("Project setup"));
    m_app->finishEdit(tr("Project setup updated"));
    if (fpsChanged && m_app->m_playback.isPlaying())
        m_app->m_playback.syncDisplayCadence();
}

QVariantMap ProjectFileController::background() const
{
    const drift::Background &bg = m_app->m_project.background();
    QVariantMap map;
    map.insert(QStringLiteral("kind"), drift::backgroundKindToString(bg.kind));
    map.insert(QStringLiteral("color"), bg.color.name(QColor::HexArgb));
    map.insert(QStringLiteral("blurStrength"), bg.blurStrength);
    return map;
}

void ProjectFileController::setBackground(const QVariantMap &background)
{
    drift::Background bg = m_app->m_project.background();
    if (background.contains(QStringLiteral("kind"))) {
        bg.kind = drift::backgroundKindFromString(background.value(QStringLiteral("kind")).toString());
    }
    if (background.contains(QStringLiteral("color"))) {
        const QColor color(background.value(QStringLiteral("color")).toString());
        if (color.isValid())
            bg.color = color;
    }
    if (background.contains(QStringLiteral("blurStrength")))
        bg.blurStrength = qBound(0.0, background.value(QStringLiteral("blurStrength")).toDouble(), 200.0);

    const drift::Background &current = m_app->m_project.background();
    if (current.kind == bg.kind && current.color == bg.color
        && qFuzzyCompare(current.blurStrength + 1.0, bg.blurStrength + 1.0))
        return;

    const drift::Project before = m_app->m_project;
    m_app->m_project.setBackground(bg);
    m_app->pushProjectEdit(before, tr("Change background"));
    m_app->finishEdit(tr("Background updated"));
    emit backgroundChanged();
    m_app->emitPreviewFrame();
}
drift::bundle::WriteRequest ProjectFileController::buildWriteRequest(bool embedSource) const
{
    drift::bundle::WriteRequest request;
    request.document = QJsonDocument::fromJson(m_app->serializeProjectJson()).object();
    request.projectId = m_app->m_project.id();
    request.title = m_app->m_project.name();
    request.author = m_app->m_project.author();
    request.description = m_app->m_project.description();
    request.createdAt = m_app->m_project.createdAt();
    request.modifiedAt = m_app->m_project.modifiedAt();
    request.addons = drift::bundle::collectAddons(m_app->m_project);
    request.media = drift::bundle::collectMedia(m_app->m_project, embedSource);

    // Without this a plain Save of a project that arrived as a package would drop its media back
    // to references into the extraction dir, which the startup sweep is free to delete.
    if (!embedSource) {
        for (drift::bundle::MediaEntry &entry : request.media) {
            if (m_embeddedSources.contains(entry.originalPath)
                || m_embeddedSources.contains(entry.resourceOf))
                entry.embedded = true;
        }
    }
    return request;
}

void ProjectFileController::rememberEmbeddedSources(const QList<drift::bundle::MediaEntry> &media)
{
    m_embeddedSources.clear();
    for (const drift::bundle::MediaEntry &entry : media) {
        if (entry.embedded)
            m_embeddedSources.insert(entry.originalPath);
    }
}

void ProjectFileController::saveProject(const QUrl &url)
{
    writeProjectBundle(url, std::nullopt);
}

void ProjectFileController::saveProjectAs(const QUrl &url)
{
    ProjectIdentity copy;
    // A new id, because the extraction directory and the per-project derived-media directory are
    // both named by it: leaving the two documents sharing one would have an edit to the duplicate
    // write freeze frames and media edits into the original's folder, and a packaged copy unpack
    // over the original's media. sweepExtractionDirs follows references rather than ids, so the
    // derived files the duplicate inherits at the old id survive the original leaving recents.
    copy.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    // The header shows the project title, not the file name. Carrying the original's title into a
    // copy made specifically to be a different version is how you end up editing the wrong one.
    copy.name = projectNameForUrl(url);
    if (copy.name.isEmpty())
        copy.name = m_app->m_project.name();
    writeProjectBundle(url, copy);
}

void ProjectFileController::adoptProjectIdentity(const std::optional<ProjectIdentity> &adopt)
{
    if (!adopt)
        return;
    m_app->m_project.setId(adopt->id);
    if (m_app->m_project.name() != adopt->name) {
        m_app->m_project.setName(adopt->name);
        emit projectNameChanged();
    }
}

void ProjectFileController::writeProjectBundle(const QUrl &url, const std::optional<ProjectIdentity> &adopt)
{
    const QString path = writeTargetPath(url);
    if (path.isEmpty()) {
        m_app->setLastMessage(tr("That save location isn’t valid"), QStringLiteral("error"));
        return;
    }
    if (m_packaging) {
        m_app->setLastMessage(tr("Already saving"), QStringLiteral("warning"));
        return;
    }

    m_app->m_project.setModifiedAt(QDateTime::currentDateTimeUtc());

    // Built up front on both paths: the worker the Android branch may hand this to must not be
    // reading the project while the timeline is free to change under it.
    drift::bundle::WriteRequest request = buildWriteRequest(/*embedSource=*/false);
    // Save As writes the copy's identity into the file but leaves the open document alone until
    // the write lands, so a failed one cannot strand the session under a name and an id that
    // belong to a file that does not exist — with the original still one Ctrl+S away.
    if (adopt) {
        request.projectId = adopt->id;
        request.title = adopt->name;
    }

#ifdef Q_OS_ANDROID
    // A project that arrived as a package keeps its media inside it (see buildWriteRequest), so a
    // plain Save of one streams every embedded source through the bundle writer and then a second
    // time into the SAF document — gigabytes, and off the GUI thread with progress and a way out.
    // That is packageProject's exact shape, so Save borrows it wholesale, packaging flag included.
    //
    // Only for that case: a project whose media is referenced rather than embedded writes a JSON
    // manifest and nothing else, and putting *every* Save behind a modal progress dialog would be
    // a bad trade for the common one. Desktop always writes straight to the picked path.
    const bool streamsMedia = std::any_of(request.media.cbegin(), request.media.cend(),
                                          [](const drift::bundle::MediaEntry &entry) {
                                              return entry.embedded
                                                  && entry.role == drift::bundle::MediaRole::Source;
                                          });
    if (streamsMedia) {
        m_packageCancel = 0;
        m_packaging = true;
        m_packageProgress = 0.0;
        emit packagingChanged();
        emit packageProgressChanged();

        // `adopt` is captured by value on both hops: it is a reference parameter, and the identity
        // has to outlive this call to reach the completion that applies it.
        (void)QtConcurrent::run([this, path, url, request, adopt]() {
            Exporter::BackgroundHold hold(QStringLiteral("Saving project"));
            QString error;
            const auto progress = [this](qint64 done, qint64 total) {
                if (m_packageCancel.loadRelaxed())
                    return false;
                const double fraction = total > 0 ? double(done) / double(total) : 0.0;
                QMetaObject::invokeMethod(
                    this,
                    [this, fraction]() {
                        m_packageProgress = fraction;
                        emit packageProgressChanged();
                    },
                    Qt::QueuedConnection);
                return true;
            };
            const bool written = drift::bundle::write(path, request, progress, &error);
            bool ok = written;
            if (ok) {
                // Reports progress but never returns false. commitWriteTarget opens the
                // destination WriteOnly|Truncate, so the user's existing project is gone the
                // moment the copy starts; honouring Cancel here would leave that document
                // truncated and then delete the staged bundle that was about to replace it.
                // Cancel therefore only reaches the bundle writer above, which is still working
                // against the staging file and can be abandoned safely.
                const auto reportOnly = [&progress](qint64 done, qint64 total) {
                    (void)progress(done, total);
                    return true;
                };
                ok = commitWriteTarget(path, url, reportOnly, &error);
            }
            // Never deletes the destination document: on the Save-in-place path this URL is
            // the user's existing project, and a failed write is no reason to take it away.
            if (!ok)
                discardWriteTarget(path, url);
            QMetaObject::invokeMethod(
                this,
                [this, ok, written, error, url, request, adopt]() {
                    m_packaging = false;
                    emit packagingChanged();
                    if (!ok) {
                        // Only a failed commit says anything about the document: a bundle
                        // writer failure is about the staging file. A commit most likely lost
                        // its write grant across a restart, so drop the association and let
                        // the next Save ask for a location. Not on Save As: the document that
                        // failed is the copy, and the grant on it came from the picker moments
                        // ago — the remembered path still names the original, which is fine.
                        if (written && !adopt)
                            setCurrentProjectPath(QString());
                        m_app->setLastMessage(error, QStringLiteral("error"));
                        emit projectSaved(false);
                        return;
                    }
                    rememberEmbeddedSources(request.media);
                    adoptProjectIdentity(adopt);
                    m_packageProgress = 1.0;
                    emit packageProgressChanged();
                    const QString location = projectLocation(url);
                    setCurrentProjectPath(location);
                    addRecentProject(location);
                    setDirty(false);
                    deleteRecoveryFile();
                    emit projectMetadataChanged();
                    m_app->setLastMessage(adopt ? tr("Saved a copy") : tr("Project saved"),
                                   QStringLiteral("success"));
                    emit projectSaved(true);
                },
                Qt::QueuedConnection);
        });
        return;
    }
#endif

    QString error;
    if (!drift::bundle::write(path, request, {}, &error)) {
        discardWriteTarget(path, url);
        m_app->setLastMessage(error, QStringLiteral("error"));
        emit projectSaved(false);
        return;
    }
    if (!commitWriteTarget(path, url, {}, &error)) {
        discardWriteTarget(path, url);
        // Saving over the remembered document failed — most likely its write grant did not
        // survive the restart — so drop the association and let the next Save ask for a location.
        // A failed Save As says nothing about the original, so it keeps its path (see above).
        if (!adopt)
            setCurrentProjectPath(QString());
        m_app->setLastMessage(error, QStringLiteral("error"));
        emit projectSaved(false);
        return;
    }
    rememberEmbeddedSources(request.media);
    adoptProjectIdentity(adopt);

    const QString location = projectLocation(url);
    setCurrentProjectPath(location);
    addRecentProject(location);
    setDirty(false);
    deleteRecoveryFile();
    emit projectMetadataChanged();
    m_app->setLastMessage(adopt ? tr("Saved a copy") : tr("Project saved"), QStringLiteral("success"));
    emit projectSaved(true);
}

void ProjectFileController::saveProjectJson(const QUrl &url)
{
    // Not toLocalFile: Qt's Android content file engine opens the encoded URI directly, so the
    // plain QFile below works on a SAF document with no staging copy in between.
    const QString path = AndroidUri::filePath(url);
    if (path.isEmpty()) {
        m_app->setLastMessage(tr("That save location isn’t valid"), QStringLiteral("error"));
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        m_app->setLastMessage(tr("Couldn’t write %1: %2").arg(QFileInfo(path).fileName(),
                                                       file.errorString()),
                       QStringLiteral("error"));
        return;
    }
    const QByteArray json = m_app->serializeProjectJson();
    if (file.write(json) != json.size() || !file.flush()) {
        m_app->setLastMessage(tr("Couldn’t write %1: %2").arg(QFileInfo(path).fileName(),
                                                       file.errorString()),
                       QStringLiteral("error"));
        return;
    }
    m_app->setLastMessage(tr("Project JSON saved"), QStringLiteral("success"));
}

namespace {

// saveProjectJson writes a JSON object; a .drift bundle starts with the "DRIFTPRJ" magic.
// Peeking lets loadProject accept a dropped / CLI / MCP JSON path without relying on the suffix.
bool fileStartsWithJsonObject(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    QByteArray head = file.read(64);
    if (head.startsWith("\xEF\xBB\xBF"))
        head.remove(0, 3);
    for (int i = 0; i < head.size(); ++i) {
        const char c = head.at(i);
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
            continue;
        return c == '{';
    }
    return false;
}

} // namespace

bool ProjectFileController::beginProjectLoad()
{
    if (m_projectLoadPending)
        return false;
    m_projectLoadPending = true;
    emit projectLoadPendingChanged();
    return true;
}

void ProjectFileController::finishProjectLoad(bool ok, const QString &message)
{
    // Only the call that actually acquired the flag (beginProjectLoad() returned true)
    // reaches here — a rejected request returns before ever calling this — so it is
    // always safe to release: nothing else can be mid-load while we are.
    m_projectLoadPending = false;
    emit projectLoadPendingChanged();
    emit projectLoadFinished(ok, message);
}

void ProjectFileController::loadProjectJson(const QUrl &url)
{
    if (!beginProjectLoad()) {
        m_app->setLastMessage(tr("Still opening a project — try again in a moment."),
                       QStringLiteral("warning"));
        return;
    }
    loadProjectJsonInternal(url);
}

void ProjectFileController::loadProjectJsonInternal(const QUrl &url)
{
    const QString path = AndroidUri::filePath(url);
    if (path.isEmpty()) {
        const QString message = tr("That project location isn’t valid");
        m_app->setLastMessage(message, QStringLiteral("error"));
        finishProjectLoad(false, message);
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        const QString message = tr("Couldn’t read %1: %2").arg(QFileInfo(path).fileName(),
                                                               file.errorString());
        m_app->setLastMessage(message, QStringLiteral("error"));
        finishProjectLoad(false, message);
        return;
    }

    const QByteArray data = file.readAll();
    file.close();

    // Drop an in-flight bundle extract so it cannot land on top of this document.
    ++m_loadGeneration;

    QString error;
    if (!m_app->applyProjectJson(data, &error)) {
        m_app->setLastMessage(error, QStringLiteral("error"));
        finishProjectLoad(false, error);
        return;
    }

    // Referenced media only — a previous packaged project's extraction paths must not hitch a
    // ride into the next Save.
    m_embeddedSources.clear();
    // Untitled: Save must not write a .drift bundle over this .json, and recents stay .drift.
    setCurrentProjectPath(QString());
    setDirty(true);
    deleteRecoveryFile();
    m_app->setProjectLayoutChosen(true);
    const QString message = tr("Project JSON loaded");
    m_app->setLastMessage(message, QStringLiteral("success"));
    finishProjectLoad(true, message);
}

void ProjectFileController::loadPremiereProject(const QUrl &url)
{
    const QString path = readTargetPath(url);
    if (path.isEmpty()) {
        m_app->setLastMessage(tr("That project location isn’t valid"), QStringLiteral("error"));
        return;
    }

    QString readError;
    const std::optional<drift::Project> proj = drift::prproj::readProject(path, &readError);
    if (!proj) {
        m_app->setLastMessage(readError.isEmpty() ? tr("Failed to open Premiere Pro project") : readError,
                       QStringLiteral("error"));
        return;
    }

    // Drop an in-flight bundle extract so it cannot land on top of this document.
    ++m_loadGeneration;

    const QByteArray data = QJsonDocument(proj->toJson()).toJson(QJsonDocument::Compact);

    QString error;
    if (!m_app->applyProjectJson(data, &error)) {
        m_app->setLastMessage(error, QStringLiteral("error"));
        return;
    }

    m_embeddedSources.clear();
    // Untitled: Save must not write a .drift bundle over this .prproj, and recents stay .drift.
    setCurrentProjectPath(QString());
    setDirty(true);
    deleteRecoveryFile();
    m_app->setProjectLayoutChosen(true);
    m_app->setLastMessage(tr("Premiere Pro project imported: %1").arg(proj->name()), QStringLiteral("success"));
}

void ProjectFileController::importMogrt(const QUrl &url)
{
    const QString path = readTargetPath(url);
    if (path.isEmpty()) {
        m_app->setLastMessage(tr("That template location isn’t valid"), QStringLiteral("error"));
        return;
    }

    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString destDir =
        QDir(base).filePath(QStringLiteral("templates/%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    QDir().mkpath(destDir);

    QString readError;
    const std::optional<drift::mogrt::MogrtTemplate> tmpl = drift::mogrt::readTemplate(path, destDir, &readError);
    if (!tmpl) {
        m_app->setLastMessage(readError.isEmpty() ? tr("Failed to unpack Motion Graphics Template") : readError,
                       QStringLiteral("error"));
        return;
    }

    const drift::Project before = m_app->m_project;

    // Insert at current playhead if project already has clips; otherwise at start (0).
    drift::TimeUs insertTime = 0;
    bool hasClips = false;
    for (const drift::Track &t : m_app->m_project.tracks()) {
        if (!t.clips.isEmpty()) {
            hasClips = true;
            break;
        }
    }
    if (hasClips) {
        insertTime = m_app->m_playheadUs;
    }

    QString applyError;
    if (!drift::mogrt::applyTemplateToProject(*tmpl, m_app->m_project, insertTime, &applyError)) {
        m_app->setLastMessage(applyError.isEmpty() ? tr("Failed to apply template to project") : applyError,
                       QStringLiteral("error"));
        return;
    }

    m_app->pushProjectEdit(before, tr("Import template: %1").arg(tmpl->title));

    if (m_app->m_assetLibrary)
        m_app->m_assetLibrary->setProject(&m_app->m_project);
    m_app->m_binFolderModel.setProject(&m_app->m_project);

    setDirty(true);
    m_app->finishEdit(tr("Template imported: %1").arg(tmpl->title));
    m_app->setLastMessage(tr("Template imported: %1").arg(tmpl->title), QStringLiteral("success"));
}

void ProjectFileController::loadKdenliveProject(const QUrl &url)
{
    const QString path = readTargetPath(url);
    if (path.isEmpty()) {
        m_app->setLastMessage(tr("That project location isn’t valid"), QStringLiteral("error"));
        return;
    }

    QString readError;
    const std::optional<drift::Project> proj = drift::kdenlive::readProject(path, &readError);
    if (!proj) {
        m_app->setLastMessage(readError.isEmpty() ? tr("Failed to open Kdenlive / MLT project") : readError,
                       QStringLiteral("error"));
        return;
    }

    // Drop an in-flight bundle extract so it cannot land on top of this document.
    ++m_loadGeneration;

    const QByteArray data = QJsonDocument(proj->toJson()).toJson(QJsonDocument::Compact);

    QString error;
    if (!m_app->applyProjectJson(data, &error)) {
        m_app->setLastMessage(error, QStringLiteral("error"));
        return;
    }

    m_embeddedSources.clear();
    // Untitled: Save must not write a .drift bundle over this project, and recents stay .drift.
    setCurrentProjectPath(QString());
    setDirty(true);
    deleteRecoveryFile();
    m_app->setProjectLayoutChosen(true);
    m_app->setLastMessage(tr("Kdenlive project imported: %1").arg(proj->name()), QStringLiteral("success"));
}

void ProjectFileController::loadResolveProject(const QUrl &url)
{
    const QString path = readTargetPath(url);
    if (path.isEmpty()) {
        m_app->setLastMessage(tr("That project location isn’t valid"), QStringLiteral("error"));
        return;
    }

    QString readError;
    const std::optional<drift::Project> proj = drift::resolve::readProject(path, &readError);
    if (!proj) {
        m_app->setLastMessage(readError.isEmpty() ? tr("Failed to open DaVinci Resolve project / timeline") : readError,
                       QStringLiteral("error"));
        return;
    }

    // Drop an in-flight bundle extract so it cannot land on top of this document.
    ++m_loadGeneration;

    const QByteArray data = QJsonDocument(proj->toJson()).toJson(QJsonDocument::Compact);

    QString error;
    if (!m_app->applyProjectJson(data, &error)) {
        m_app->setLastMessage(error, QStringLiteral("error"));
        return;
    }

    m_embeddedSources.clear();
    setCurrentProjectPath(QString());
    setDirty(true);
    deleteRecoveryFile();
    m_app->setProjectLayoutChosen(true);
    m_app->setLastMessage(tr("DaVinci Resolve project imported: %1").arg(proj->name()), QStringLiteral("success"));
}

void ProjectFileController::loadEdlTimeline(const QUrl &url)
{
    const QString path = readTargetPath(url);
    if (path.isEmpty()) {
        m_app->setLastMessage(tr("That project location isn’t valid"), QStringLiteral("error"));
        return;
    }

    QString readError;
    const std::optional<drift::Project> proj = drift::edl::readProject(path, &readError);
    if (!proj) {
        m_app->setLastMessage(readError.isEmpty() ? tr("Failed to open Edit Decision List (.edl)") : readError,
                       QStringLiteral("error"));
        return;
    }

    ++m_loadGeneration;

    const QByteArray data = QJsonDocument(proj->toJson()).toJson(QJsonDocument::Compact);

    QString error;
    if (!m_app->applyProjectJson(data, &error)) {
        m_app->setLastMessage(error, QStringLiteral("error"));
        return;
    }

    m_embeddedSources.clear();
    setCurrentProjectPath(QString());
    setDirty(true);
    deleteRecoveryFile();
    m_app->setProjectLayoutChosen(true);
    m_app->setLastMessage(tr("EDL imported: %1").arg(proj->name()), QStringLiteral("success"));
}

void ProjectFileController::loadOtioTimeline(const QUrl &url)
{
    const QString path = readTargetPath(url);
    if (path.isEmpty()) {
        m_app->setLastMessage(tr("That project location isn’t valid"), QStringLiteral("error"));
        return;
    }

    QString readError;
    const std::optional<drift::Project> proj = drift::otio::readProject(path, &readError);
    if (!proj) {
        m_app->setLastMessage(readError.isEmpty() ? tr("Failed to open OpenTimelineIO (.otio) sequence") : readError,
                       QStringLiteral("error"));
        return;
    }

    ++m_loadGeneration;

    const QByteArray data = QJsonDocument(proj->toJson()).toJson(QJsonDocument::Compact);

    QString error;
    if (!m_app->applyProjectJson(data, &error)) {
        m_app->setLastMessage(error, QStringLiteral("error"));
        return;
    }

    m_embeddedSources.clear();
    setCurrentProjectPath(QString());
    setDirty(true);
    deleteRecoveryFile();
    m_app->setProjectLayoutChosen(true);
    m_app->setLastMessage(tr("OpenTimelineIO imported: %1").arg(proj->name()), QStringLiteral("success"));
}

void ProjectFileController::packageProject(const QUrl &url)
{
    // The bundle writer needs a real file to seek in, so on Android this stages into app storage
    // and the finished bundle is streamed into the picked document below.
    const QString path = writeTargetPath(url, QStringLiteral("drift"));
    if (path.isEmpty()) {
        m_app->setLastMessage(tr("That save location isn’t valid"), QStringLiteral("error"));
        return;
    }
    if (m_packaging)
        return;

    m_app->m_project.setModifiedAt(QDateTime::currentDateTimeUtc());
    m_packageCancel = 0;
    m_packaging = true;
    m_packageProgress = 0.0;
    emit packagingChanged();
    emit packageProgressChanged();

    // The whole request is built here, on the GUI thread: the worker copies gigabytes and must not
    // be reading the project while the timeline is free to change under it.
    const drift::bundle::WriteRequest request = buildWriteRequest(/*embedSource=*/true);

    // Before the job, not inside it: commitWriteTarget truncates the destination the moment it
    // opens, so afterwards every document looks disposable.
    const bool disposable = writeTargetIsDisposable(url);

    (void)QtConcurrent::run([this, path, url, request, disposable]() {
        Exporter::BackgroundHold hold(QStringLiteral("Saving project"));
        QString error;
        const auto progress = [this](qint64 done, qint64 total) {
            if (m_packageCancel.loadRelaxed())
                return false;
            const double fraction = total > 0 ? double(done) / double(total) : 0.0;
            QMetaObject::invokeMethod(
                this,
                [this, fraction]() {
                    m_packageProgress = fraction;
                    emit packageProgressChanged();
                },
                Qt::QueuedConnection);
            return true;
        };
        bool ok = drift::bundle::write(path, request, progress, &error);
        if (ok) {
            // See saveProject: commitWriteTarget has already truncated the destination by the time
            // it reports anything, so Cancel reaches the bundle writer above and no further.
            const auto reportOnly = [&progress](qint64 done, qint64 total) {
                (void)progress(done, total);
                return true;
            };
            ok = commitWriteTarget(path, url, reportOnly, &error);
        }
        // A shareable copy always goes to a document the picker just created, so a failed write
        // has nothing worth keeping behind it.
        if (!ok)
            discardWriteTarget(path, url, disposable);
        QMetaObject::invokeMethod(
            this,
            [this, ok, error, url, request]() {
                m_packaging = false;
                emit packagingChanged();
                if (!ok) {
                    m_app->setLastMessage(error, QStringLiteral("error"));
                    emit packageFinished(false, error);
                    return;
                }
                rememberEmbeddedSources(request.media);
                m_packageProgress = 1.0;
                emit packageProgressChanged();
                const QString location = projectLocation(url);
                setCurrentProjectPath(location);
                addRecentProject(location);
                setDirty(false);
                deleteRecoveryFile();
                emit projectMetadataChanged();
                m_app->setLastMessage(tr("Shareable copy ready"), QStringLiteral("success"));
                emit packageFinished(true, QStringLiteral("Shareable copy ready"));
            },
            Qt::QueuedConnection);
    });
}

void ProjectFileController::cancelPackage()
{
    m_packageCancel = 1;
}

void ProjectFileController::collectMediaToFolder(const QUrl &folder, bool move)
{
    const QString dest = folder.toLocalFile();
    if (dest.isEmpty() || !QFileInfo(dest).isDir()) {
        m_app->setLastMessage(tr("That folder isn’t valid"), QStringLiteral("error"));
        return;
    }
    if (m_collectingMedia)
        return;

    // Built here, on the GUI thread, for the same reason packageProject builds its request here.
    const QList<drift::bundle::MediaEntry> media = drift::bundle::collectMedia(m_app->m_project, false);
    QHash<QString, QString> subfolders;
    for (const drift::bundle::MediaEntry &entry : media) {
        if (!entry.resourceOf.isEmpty())
            continue;
        QString folderName = QStringLiteral("Other");
        if (entry.role != drift::bundle::MediaRole::Source)
            folderName = QStringLiteral("Derived");
        else if (AssetLibrary::isVideoPath(entry.originalPath))
            folderName = QStringLiteral("Video");
        else if (AssetLibrary::isAudioPath(entry.originalPath))
            folderName = QStringLiteral("Audio");
        else if (AssetLibrary::isImagePath(entry.originalPath))
            folderName = QStringLiteral("Images");
        subfolders.insert(entry.originalPath, folderName);
    }

    m_collectMediaCancel = 0;
    m_collectingMedia = true;
    m_collectMediaProgress = 0.0;
    emit collectingMediaChanged();
    emit collectMediaProgressChanged();

    const int generation = m_loadGeneration;
    (void)QtConcurrent::run([this, media, subfolders, dest, move, generation]() {
        const auto progress = [this](qint64 done, qint64 total) {
            if (m_collectMediaCancel.loadRelaxed())
                return false;
            const double fraction = total > 0 ? double(done) / double(total) : 0.0;
            QMetaObject::invokeMethod(
                this,
                [this, fraction]() {
                    m_collectMediaProgress = fraction;
                    emit collectMediaProgressChanged();
                },
                Qt::QueuedConnection);
            return true;
        };
        QHash<QString, QString> remap;
        int undeleted = 0;
        QString error;
        const bool ok = drift::bundle::collectToFolder(media, subfolders, dest, move, progress,
                                                       &remap, &undeleted, &error);
        QMetaObject::invokeMethod(
            this,
            [this, ok, remap, undeleted, error, move, generation]() {
                m_collectingMedia = false;
                emit collectingMediaChanged();
                if (!ok) {
                    m_app->setLastMessage(error, QStringLiteral("error"));
                    return;
                }
                // The dialog is modal, so only a project opened from outside the UI (MCP, a
                // second instance handing over a file) can get here; its paths are not these.
                if (generation != m_loadGeneration)
                    return;
                if (remap.isEmpty()) {
                    m_app->setLastMessage(tr("All media is already in that folder"), QStringLiteral("info"));
                    return;
                }

                const drift::Project before = m_app->m_project;
                remapProjectPaths(remap);
                if (move)
                    m_app->m_undoStack.clear();
                else
                    m_app->pushProjectEdit(before, tr("Collect media"));
                if (m_app->m_assetLibrary)
                    m_app->m_assetLibrary->setProject(&m_app->m_project);
                m_app->m_binFolderModel.setProject(&m_app->m_project);
                m_app->restoreFilmstripsAfterLoad();
                m_app->notifyTracksChanged();
                setDirty(true);

                if (undeleted > 0)
                    m_app->setLastMessage(tr("Media collected, but %n original(s) couldn’t be deleted", "",
                                      undeleted),
                                   QStringLiteral("warning"));
                else
                    m_app->setLastMessage(move ? tr("Media moved and relinked")
                                        : tr("Media copied and relinked"),
                                   QStringLiteral("success"));
            },
            Qt::QueuedConnection);
    });
}

void ProjectFileController::cancelCollectMedia()
{
    m_collectMediaCancel = 1;
}

void ProjectFileController::loadProject(const QUrl &url)
{
    if (!beginProjectLoad()) {
        m_app->setLastMessage(tr("Still opening a project — try again in a moment."),
                       QStringLiteral("warning"));
        return;
    }

    // The bundle reader seeks through its input and hands media paths to FFmpeg, so a SAF document
    // is staged to a real file first. The JSON branch below needs no such thing and takes the URL.
    const QString path = readTargetPath(url);
    if (path.isEmpty()) {
        const QString message = tr("That project location isn’t valid");
        m_app->setLastMessage(message, QStringLiteral("error"));
        finishProjectLoad(false, message);
        return;
    }

    // Disabled: external project imports (Premiere Pro, DaVinci Resolve/FCPXML, Kdenlive/Shotcut,
    // .mogrt, EDL, OTIO) landed in the last two weeks but need more fixing before they ship. Leave
    // the branches commented out; uncomment to re-enable once the readers are stable.
    // if (path.endsWith(QLatin1String(".prproj"), Qt::CaseInsensitive)
    //     || path.endsWith(QLatin1String(".xml"), Qt::CaseInsensitive)
    //     || drift::prproj::isPremiereProject(path)) {
    //     loadPremiereProject(url);
    //     return;
    // }
    //
    // if (path.endsWith(QLatin1String(".drp"), Qt::CaseInsensitive)
    //     || path.endsWith(QLatin1String(".fcpxml"), Qt::CaseInsensitive)
    //     || drift::resolve::isResolveProject(path)) {
    //     loadResolveProject(url);
    //     return;
    // }
    //
    // if (path.endsWith(QLatin1String(".mogrt"), Qt::CaseInsensitive)
    //     || drift::mogrt::isMogrtFile(path)) {
    //     importMogrt(url);
    //     return;
    // }
    //
    // if (path.endsWith(QLatin1String(".kdenlive"), Qt::CaseInsensitive)
    //     || path.endsWith(QLatin1String(".mlt"), Qt::CaseInsensitive)
    //     || drift::kdenlive::isKdenliveProject(path)) {
    //     loadKdenliveProject(url);
    //     return;
    // }
    //
    // if (path.endsWith(QLatin1String(".edl"), Qt::CaseInsensitive)
    //     || drift::edl::isEdlTimeline(path)) {
    //     loadEdlTimeline(url);
    //     return;
    // }
    //
    // if (path.endsWith(QLatin1String(".otio"), Qt::CaseInsensitive)
    //     || drift::otio::isOtioTimeline(path)) {
    //     loadOtioTimeline(url);
    //     return;
    // }

    if (fileStartsWithJsonObject(path)) {
        // Not loadProjectJson(): this call already owns the pending flag via the
        // beginProjectLoad() above, and loadProjectJson()'s own gate would see it
        // already held and reject its own request.
        loadProjectJsonInternal(url);
        return;
    }

    QString error;
    const std::optional<drift::bundle::BundleInfo> info =
        drift::bundle::readManifest(path, &error);
    if (!info) {
        m_app->setLastMessage(error, QStringLiteral("error"));
        finishProjectLoad(false, error);
        return;
    }

    // Named by the project's own id, which survives the round-trip, so reopening the same bundle
    // lands on the files it already unpacked.
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString destDir =
        QDir(base).filePath(QStringLiteral("projects/%1/media").arg(info->projectId));

    const int generation = ++m_loadGeneration;
    const drift::bundle::BundleInfo bundle = *info;

    auto finishLoad = [this, url, bundle, generation](const QHash<QString, QString> &remap,
                                                      const QString &extractError, bool extractOk) {
        if (generation != m_loadGeneration)
            return;
        if (!extractOk) {
            m_app->setLastMessage(extractError, QStringLiteral("error"));
            finishProjectLoad(false, extractError);
            return;
        }

        m_pendingPathRemap = remap;
        QString applyError;
        if (!m_app->applyProjectJson(QJsonDocument(bundle.document).toJson(QJsonDocument::Compact),
                              &applyError)) {
            m_pendingPathRemap.clear();
            m_app->setLastMessage(applyError, QStringLiteral("error"));
            finishProjectLoad(false, applyError);
            return;
        }

        m_embeddedSources.clear();
        for (const drift::bundle::MediaEntry &entry : bundle.media) {
            if (entry.embedded && entry.role == drift::bundle::MediaRole::Source
                && entry.resourceOf.isEmpty())
                m_embeddedSources.insert(remap.value(entry.originalPath, entry.originalPath));
        }

        // Not the staged path: on Android that lives in the cache the startup sweep clears, and
        // Save-in-place has to write back to the document the user actually opened. projectLocation
        // upgrades the picker's one-shot grant to a persistable read+write one on the way.
        const QString location = projectLocation(url);
        setCurrentProjectPath(location);
        addRecentProject(location);
        deleteRecoveryFile();
        m_app->setProjectLayoutChosen(true);
        const QString message = tr("Project loaded");
        m_app->setLastMessage(message, QStringLiteral("success"));
        reportMissingAddons(bundle.addons);
        finishProjectLoad(true, message);
    };

    if (bundle.embeddedBytes <= 0) {
        finishLoad({}, {}, true);
        return;
    }

    m_app->setLastMessage(tr("Unpacking project media…"));
    (void)QtConcurrent::run([this, path, destDir, generation, finishLoad]() {
        QString error;
        QHash<QString, QString> remap;
        const bool ok = drift::bundle::extract(path, destDir, {}, &remap, &error);
        QMetaObject::invokeMethod(
            this,
            [finishLoad, remap, error, ok, generation, this]() {
                if (generation != m_loadGeneration)
                    return;
                finishLoad(remap, error, ok);
            },
            Qt::QueuedConnection);
    });
}

void ProjectFileController::reportMissingAddons(const QList<drift::bundle::AddonRef> &addons)
{
    QVariantList missing;
    for (const drift::bundle::AddonRef &addon : addons) {
        if (drift::addon::installedAddon(addon.id))
            continue;
        missing.append(QVariantMap{
            {QStringLiteral("id"), addon.id},
            {QStringLiteral("name"), addon.name.isEmpty() ? addon.id : addon.name},
            {QStringLiteral("version"), addon.version},
            {QStringLiteral("kinds"), addon.kinds},
        });
    }
    if (!missing.isEmpty())
        emit missingAddons(missing);
}

void ProjectFileController::remapProjectPaths(const QHash<QString, QString> &remap)
{
    if (remap.isEmpty())
        return;

    // Tiles are keyed on the old paths, and a source that was missing is blacklisted until
    // this runs — without it a relinked clip's filmstrip never comes back.
    m_app->m_filmstripTiles.clear();

    const auto repoint = [&remap](QString &path) {
        const auto it = remap.constFind(path);
        if (it == remap.constEnd())
            return false;
        path = it.value();
        return true;
    };

    for (drift::MediaAsset &asset : m_app->m_project.assets()) {
        if (repoint(asset.path)) {
            asset.thumbnailPath.clear();
            asset.filmstripPath.clear();
        }
    }

    // A relinked copy has a new path and mtime, which the fingerprint reads as a different file.
    // The size still has to agree: the transcript is only carried over for the same bytes.
    QHash<QString, drift::TranscriptPtr> transcripts = m_app->m_project.transcripts();
    for (auto it = transcripts.begin(); it != transcripts.end(); ++it) {
        if (!it.value())
            continue;
        QString path = it.value()->source.path;
        if (!repoint(path))
            continue;
        const drift::SourceFingerprint moved = drift::SourceFingerprint::of(path);
        if (moved.size != it.value()->source.size)
            continue;
        auto updated = std::make_shared<drift::Transcript>(*it.value());
        updated->source = moved;
        it.value() = std::move(updated);
    }
    m_app->m_project.setTranscripts(transcripts);

    m_app->m_project.forEachTrackList([&](QList<drift::Track> &tracks) {
        for (drift::Track &track : tracks) {
            for (drift::Clip &clip : track.clips) {
                // Masks live on adjustment clips, which this flat walk already covers.
                repoint(clip.mask.mediaPath);
                repoint(clip.mask.mediaFgrPath);
                repoint(clip.faceTrackPath);
                repoint(clip.depthPath);
                repoint(clip.legacyStabilizePath);
                for (drift::VectorSlotValue &slot : clip.vector.slotValues) {
                    if (slot.type == drift::VectorSlotValue::Type::Image)
                        repoint(slot.image);
                }
                for (drift::TextShadingLayer &layer : clip.textStyle.layers)
                    repoint(layer.paint.texture.path);
                for (drift::TextShadingLayer &layer : clip.shapeStyle.layers)
                    repoint(layer.paint.texture.path);
                for (drift::Effect &effect : clip.effects) {
                    const EffectPresetEntry *def = effectDefForId(effect.catalogId);
                    if (!def)
                        continue;
                    for (const drift::EffectParamSpec &spec : def->meta.parameters) {
                        if (!spec.isFilePath())
                            continue;
                        auto it = effect.parameters.find(spec.key);
                        if (it == effect.parameters.end())
                            continue;
                        QString path = it.value().toString();
                        if (repoint(path))
                            it.value() = path;
                    }
                }
                if (repoint(clip.path)) {
                    // Cache renders keyed on the old path; AssetLibrary and
                    // restoreFilmstripsAfterLoad regenerate them for the new one.
                    clip.thumbnailPath.clear();
                    clip.filmstripPath.clear();
                    // The renderer reads the vector document via clip.vector, not clip.path.
                    if (clip.type == drift::ClipType::Vector && !clip.vector.isInline())
                        clip.vector.path = clip.path;
                    if (clip.type == drift::ClipType::Model3d)
                        clip.model3d.path = clip.path;
                }
            }
        }
    });
}

void ProjectFileController::rehydrateMissingSources()
{
#ifdef Q_OS_ANDROID
    // <AppData>/imports is where every SAF import lands, and nothing sweeps it — so a missing file
    // means the whole app-storage tree went (uninstall, "clear storage") or the project came from
    // another device. The document the media was picked from is the only way back.
    QList<QUrl> pending;
    QStringList missingPaths;
    for (const drift::MediaAsset &asset : m_app->m_project.assets()) {
        if (asset.sourceUri.isEmpty() || QFileInfo::exists(asset.path))
            continue;
        pending.append(QUrl(asset.sourceUri));
        missingPaths.append(asset.path);
    }
    if (pending.isEmpty())
        return;

    const int generation = ++m_loadGeneration;
    auto *watcher = new QFutureWatcher<QHash<QString, QString>>(this);
    connect(watcher, &QFutureWatcher<QHash<QString, QString>>::finished, this,
            [this, watcher, generation]() {
                watcher->deleteLater();
                // Another project opened while the copies ran; this one's paths are gone.
                if (generation != m_loadGeneration)
                    return;

                const QHash<QString, QString> remap = watcher->result();
                if (remap.isEmpty())
                    return;

                // Rewrites the assets, the clips' duplicated paths, matte and face-track paths and
                // every file-path effect param, and clears the caches keyed on the old ones.
                remapProjectPaths(remap);

                // remapProjectPaths edits the document directly, so the bin and the timeline have
                // to be told; a load has already cleared undo, and a restored file is not an edit.
                if (m_app->m_assetLibrary)
                    m_app->m_assetLibrary->setProject(&m_app->m_project);
                m_app->m_binFolderModel.setProject(&m_app->m_project);
                m_app->restoreFilmstripsAfterLoad();
                m_app->notifyTracksChanged();
            });

    watcher->setFuture(QtConcurrent::run([pending, missingPaths]() {
        QHash<QString, QString> remap;
        for (int i = 0; i < pending.size(); ++i) {
            // Empty when the grant expired or the user revoked it, or the document is simply gone.
            // The asset then stays missing, which is exactly where it was a moment ago.
            const QString restored = materializeContentUrl(pending.at(i));
            if (!restored.isEmpty())
                remap.insert(missingPaths.at(i), restored);
        }
        return remap;
    }));
#endif
}

void ProjectFileController::newProject(bool silent)
{
    m_app->setPlaying(false);
    m_app->resetSessionState();
    // Whole-document replacement rather than resetToDefaultTimeline(), which only clears the
    // tracks — the asset pool, name, canvas size, bookmarks, work area and background all used to
    // survive into the "new" project.
    m_app->m_project = drift::Project{};
    m_app->m_project.setAuthor(QSettings().value(QStringLiteral("authorName")).toString());
    m_embeddedSources.clear();
    if (m_app->m_assetLibrary)
        m_app->m_assetLibrary->setProject(&m_app->m_project);
    m_app->m_binFolderModel.setProject(&m_app->m_project);
    m_app->setCurrentBinFolderId(QString());
    m_app->m_playback.setProject(&m_app->m_project);
    m_app->m_undoStack.clear();
    m_app->clearSelection();
    m_app->setPlayheadUs(0);
    setCurrentProjectPath(QString());
    // Per-project editor prefs: these travel in the project file, so they belong to the document
    // that was just discarded. Same defaults applyProjectJson falls back to.
    m_app->m_snapEnabled = true;
    m_app->m_rippleEnabled = false;
    m_app->m_allowClipOverlap = false;
    m_app->setLoopWorkAreaEnabled(false);
    m_app->setMediaGridMode(true);
    m_app->m_preferences->setAudioMixerVisible(false);
    setDirty(false);
    deleteRecoveryFile();
    // Always notify — even when already false — so the layout chooser reopens
    // after "Decide later" + New Project.
    m_app->m_projectLayoutChosen = false;
    emit m_app->projectLayoutChosenChanged();
    emit m_app->snapEnabledChanged();
    emit m_app->rippleEnabledChanged();
    emit m_app->allowClipOverlapChanged();
    m_app->notifyTracksChanged();
    emit m_app->sequenceTabsChanged();
    emit m_app->bookmarksChanged();
    emit m_app->workAreaChanged();
    emit projectNameChanged();
    emit projectMetadataChanged();
    emit backgroundChanged();
    if (!silent)
        m_app->setLastMessage(tr("New project"));
}

void ProjectFileController::openRecentProject(const QString &path)
{
    if (path.isEmpty())
        return;
    // Recents hold a SAF document as its encoded URI, which fromLocalFile would mangle.
    loadProject(m_app->fileUrl(path));
}

QVariantList ProjectFileController::recentProjects() const
{
    QSettings settings;
    const QStringList paths = settings.value(QStringLiteral("recentProjects")).toStringList();
    QVariantList out;
    for (const QString &path : paths) {
        if (path.startsWith(QLatin1String("content://"), Qt::CaseInsensitive)) {
            // QFileInfo knows nothing about a document id, so every Android recent used to render
            // as "(missing)". The provider is the only thing that can answer either question.
            out.append(QVariantMap{
                {QStringLiteral("path"), path},
                {QStringLiteral("name"), AndroidUri::displayName(QUrl(path))},
                {QStringLiteral("exists"), projectLocationExists(path)},
            });
            continue;
        }
        const QFileInfo info(path);
        out.append(QVariantMap{
            {QStringLiteral("path"), path},
            {QStringLiteral("name"), info.fileName()},
            {QStringLiteral("exists"), info.exists()},
        });
    }
    return out;
}

void ProjectFileController::addRecentProject(const QString &path)
{
    if (path.isEmpty())
        return;
    QSettings settings;
    QStringList paths = settings.value(QStringLiteral("recentProjects")).toStringList();
    paths.removeAll(path);
    paths.prepend(path);
    while (paths.size() > kMaxRecentProjects)
        paths.removeLast();
    settings.setValue(QStringLiteral("recentProjects"), paths);
    emit recentProjectsChanged();
}

void ProjectFileController::clearRecentProjects()
{
    QSettings settings;
    settings.remove(QStringLiteral("recentProjects"));
    emit recentProjectsChanged();
}

void ProjectFileController::removeRecentProject(const QString &path)
{
    if (path.isEmpty())
        return;
    QSettings settings;
    QStringList paths = settings.value(QStringLiteral("recentProjects")).toStringList();
    if (paths.removeAll(path) == 0)
        return;
    settings.setValue(QStringLiteral("recentProjects"), paths);
    emit recentProjectsChanged();
}

void ProjectFileController::setDirty(bool dirty)
{
    if (m_dirty == dirty)
        return;
    m_dirty = dirty;
    emit dirtyChanged();
}

void ProjectFileController::setCurrentProjectPath(const QString &path)
{
    if (m_currentProjectPath == path)
        return;
    m_currentProjectPath = path;
    // Cleared by newProject(); set after load/save/package so next launch can reopen.
    QSettings().setValue(QStringLiteral("lastSessionPath"), path);
    emit currentProjectPathChanged();
}

QString ProjectFileController::recoveryFilePath()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    // Plain JSON, not a bundle: this is an internal crash snapshot written every few seconds and
    // never opened through the file dialog, so it must not repack the project's media.
    return dir + QStringLiteral("/recovery/autosave.json");
}

void ProjectFileController::flushRecoverySnapshot()
{
    // Same two things aboutToQuit does, because on Android it never runs: without the settings
    // write, "reopen last project at startup" also silently never has a path to reopen.
    QSettings().setValue(QStringLiteral("lastSessionPath"), m_currentProjectPath);
    if (m_dirty)
        writeRecoveryFile();
}
namespace {

// Serializes and writes a recovery file to a temp sibling of `path`. Returns the temp path, or an
// empty string on failure. Runs on any thread: everything it reads is its own copy.
QString writeRecoveryTemp(const drift::Project &project, QJsonObject session, const QJsonObject &meta,
                          const QString &path)
{
    QJsonObject root = project.toJson();
    for (auto it = session.constBegin(); it != session.constEnd(); ++it)
        root.insert(it.key(), it.value());
    root.insert(QStringLiteral("__recovery"), meta);

    // Write to a temp sibling and rename so a crash mid-write can't corrupt the recovery file
    // itself. Compact: nothing reads this but restoreAutosave, and it takes either form.
    const QString tmpPath = path + QStringLiteral(".tmp");
    QFile file(tmpPath);
    if (!file.open(QIODevice::WriteOnly))
        return {};
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    file.close();
    return tmpPath;
}

void promoteRecoveryTemp(const QString &tmpPath, const QString &path)
{
    // Never remove the current file for a temp that is not there to replace it.
    if (tmpPath.isEmpty() || !QFile::exists(tmpPath))
        return;
    if (QFile::exists(path))
        QFile::remove(path);
    QFile::rename(tmpPath, path);
}

} // namespace

void ProjectFileController::writeRecoveryFile(bool synchronous)
{
    const QString path = recoveryFilePath();
    QDir().mkpath(QFileInfo(path).absolutePath());

    QJsonObject meta;
    meta.insert(QStringLiteral("originalPath"), m_currentProjectPath);
    meta.insert(QStringLiteral("projectName"), m_app->m_project.name());
    meta.insert(QStringLiteral("savedAt"), QDateTime::currentDateTime().toString(Qt::ISODate));
    const QJsonObject session = m_app->sessionJson();

    if (synchronous) {
        // Waits on the worker only: its continuation needs this thread's event loop.
        m_recoveryWrite.waitForFinished();
        ++m_recoveryGeneration;
        promoteRecoveryTemp(writeRecoveryTemp(m_app->m_project, session, meta, path), path);
        return;
    }

    // Serializing a large project took tens of milliseconds on the GUI thread every 15 seconds,
    // playing or not. The snapshot playback already keeps is reused when it is current, so the
    // only GUI-thread cost left is the session fields above.
    if (m_recoveryWrite.isRunning())
        return;
    std::shared_ptr<const drift::Project> snapshot = m_app->m_playback.projectSnapshot(&m_app->m_project);
    if (!snapshot)
        snapshot = std::make_shared<const drift::Project>(m_app->m_project.detachedCopy());
    const quint64 generation = m_recoveryGeneration;
    m_recoveryWrite = QtConcurrent::run([snapshot, session, meta, path] {
        return writeRecoveryTemp(*snapshot, session, meta, path);
    });
    m_recoveryWrite.then(this, [this, generation, path](const QString &tmpPath) {
        // The file was deleted (a save, a new project) while this was being written.
        if (generation != m_recoveryGeneration) {
            if (!tmpPath.isEmpty())
                QFile::remove(tmpPath);
            return;
        }
        promoteRecoveryTemp(tmpPath, path);
    });
}

void ProjectFileController::deleteRecoveryFile()
{
    ++m_recoveryGeneration;
    const QString path = recoveryFilePath();
    if (QFile::exists(path))
        QFile::remove(path);
    if (m_recoveryAvailable) {
        m_recoveryAvailable = false;
        m_recoveryInfo.clear();
        emit recoveryChanged();
    }
}

void ProjectFileController::detectRecoveryFile()
{
    const QString path = recoveryFilePath();
    QFile file(path);
    if (!file.exists() || !file.open(QIODevice::ReadOnly))
        return;

    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    const QJsonObject meta = root.value(QStringLiteral("__recovery")).toObject();
    m_recoveryInfo = QVariantMap{
        {QStringLiteral("originalPath"), meta.value(QStringLiteral("originalPath")).toString()},
        {QStringLiteral("projectName"), meta.value(QStringLiteral("projectName")).toString()},
        {QStringLiteral("savedAt"), meta.value(QStringLiteral("savedAt")).toString()},
    };
    m_recoveryAvailable = true;
    emit recoveryChanged();
}

void ProjectFileController::restoreAutosave()
{
    const QString path = recoveryFilePath();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        m_app->setLastMessage(tr("No recovery file found"), QStringLiteral("warning"));
        return;
    }

    const QByteArray data = file.readAll();
    file.close();

    const QString originalPath = m_recoveryInfo.value(QStringLiteral("originalPath")).toString();
    QString error;
    if (!m_app->applyProjectJson(data, &error)) {
        m_app->setLastMessage(error, QStringLiteral("error"));
        return;
    }

    // Restore the association with the original file (if any) and mark unsaved so
    // the user is nudged to re-save; keep the recovery file until the next save.
    setCurrentProjectPath(originalPath);
    setDirty(true);
    m_recoveryAvailable = false;
    m_recoveryInfo.clear();
    emit recoveryChanged();
    m_app->setProjectLayoutChosen(true);
    m_app->setLastMessage(tr("Recovered unsaved work"), QStringLiteral("success"));
}

void ProjectFileController::discardAutosave()
{
    // Fresh timeline and clear the autosave snapshot from the previous session.
    newProject();
    m_app->setLastMessage(tr("Started new session"));
}

bool ProjectFileController::restoreLastSessionIfEnabled()
{
    if (!m_app->m_preferences->reopenLastProject())
        return false;

    // Unsaved (or crashed) session takes priority over the last clean .drift path.
    if (m_recoveryAvailable) {
        restoreAutosave();
        return true;
    }

    const QString path = QSettings().value(QStringLiteral("lastSessionPath")).toString();
    if (path.isEmpty() || !projectLocationExists(path))
        return false;

    // fileUrl, not fromLocalFile: lastSessionPath mirrors currentProjectPath, which on Android is
    // the encoded content:// URI projectLocation() stored — fromLocalFile turns that into
    // "file:///content:/…" and the load fails with "That project location isn't valid".
    loadProject(m_app->fileUrl(path));
    return true;
}

QUrl ProjectFileController::startupProjectUrlFromArguments(const QStringList &args)
{
    for (int i = 1; i < args.size(); ++i) {
        const QString &arg = args.at(i);
        if (arg.isEmpty() || arg.startsWith(QLatin1Char('-')))
            continue;
        const QUrl url = QUrl::fromUserInput(arg, QDir::currentPath(), QUrl::AssumeLocalFile);
        if (url.isValid())
            return url;
    }
    return {};
}

void ProjectFileController::queueExternalProject(const QUrl &url)
{
    if (!url.isValid() || url.isEmpty())
        return;
    if (url == m_lastExternalProject)
        return;
    m_lastExternalProject = url;
    if (m_uiReady)
        emit m_app->externalProjectOpenRequested(url);
    else
        m_pendingStartupProject = url;
}

bool ProjectFileController::consumeStartupProject()
{
    m_uiReady = true;
    if (!m_pendingStartupProject.isValid() || m_pendingStartupProject.isEmpty())
        return false;
    const QUrl url = m_pendingStartupProject;
    m_pendingStartupProject.clear();
    loadProject(url);
    return true;
}

void ProjectFileController::discardUnsavedChanges()
{
    // Don't Save before quit: clear dirty so aboutToQuit does not write a
    // recovery file the user just chose to throw away. Timeline is left alone —
    // the window is closing (or the caller is about to replace the project).
    setDirty(false);
    deleteRecoveryFile();
}
