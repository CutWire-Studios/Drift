#include "app/editor/ProjectFolderController.h"

#include "app/editor/AppController.h"
#include "app/editor/ProjectFileController.h"
#include "core/project/ProjectDiff.h"
#include "core/project/folder/ProjectLint.h"
#include "core/project/folder/ProjectSchema.h"
#include "engine/project/ProjectBundle.h"
#include "engine/projdir/ProjectFolderLint.h"
#include "engine/projdir/ProjectGuide.h"
#include "engine/projdir/ProjectScripts.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QJsonDocument>
#include <QtGlobal>
#if QT_CONFIG(process)
#include <QProcess>
#endif
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>

using namespace drift::projdir;

namespace {

constexpr int kWriteDebounceMs = 300;
constexpr int kRescanDebounceMs = 150;
constexpr int kPollMs = 3000;
constexpr int kMaxWatchedFiles = 3000;

QString positionText(const Problem &p)
{
    QString text = p.file;
    if (p.line > 0)
        text += QStringLiteral(":%1:%2").arg(p.line).arg(p.column);
    return text + QStringLiteral(": ") + p.message;
}

// Everything a person or an agent might edit by hand; the poll compares these, so it also sees
// scripts and data files.
bool watchedName(const QString &rel)
{
    if (rel.startsWith(QLatin1String(".drift/")))
        return false;
    return rel.endsWith(QLatin1String(".json")) || rel.endsWith(QLatin1String(".lua"))
        || rel.endsWith(QLatin1String(".csv"));
}

void writeProjectFolderRaw(const QString &root, const FileMap &files)
{
    for (auto it = files.constBegin(); it != files.constEnd(); ++it) {
        const QString path = QDir(root).filePath(it.key());
        QFile current(path);
        if (current.open(QIODevice::ReadOnly) && current.readAll() == it.value())
            continue;
        current.close();
        QDir().mkpath(QFileInfo(path).absolutePath());
        QSaveFile out(path);
        if (out.open(QIODevice::WriteOnly) && out.write(it.value()) == it.value().size())
            out.commit();
    }
}

// Files the editor keeps current inside every project folder: the schemas, the Lua definitions, the agent
// guide. The guide and .luarc.json are the user's once they change them.
void writeSupportFiles(const QString &root)
{
    FileMap generated = schemaFiles();
    generated.insert(QStringLiteral(".drift/lua/drift.lua"), luaDefinitions());
    writeProjectFolderRaw(root, generated);

    const QString guide = projectFolderGuide();
    const QString agents = QDir(root).filePath(QStringLiteral("AGENTS.md"));
    const QByteArray marker = "<!-- drift:project-folder-guide -->\n";
    if (!guide.isEmpty()) {
        QFile existing(agents);
        const bool ours = !existing.exists() || (existing.open(QIODevice::ReadOnly) && existing.readAll().startsWith(marker));
        existing.close();
        const QByteArray text = marker + guide.toUtf8();
        if (ours) {
            QFile current(agents);
            if (!current.open(QIODevice::ReadOnly) || current.readAll() != text) {
                current.close();
                writeProjectFolderRaw(root, {{QStringLiteral("AGENTS.md"), text}});
            }
        }
    }
    const QString luarc = QDir(root).filePath(QStringLiteral(".luarc.json"));
    if (!QFileInfo::exists(luarc))
        writeProjectFolderRaw(root, {{QStringLiteral(".luarc.json"), luarcJson()}});
}

} // namespace

ProjectFolderController::Composed ProjectFolderController::compose(const QString &root)
{
    return compose(readFolderContents(root), root);
}

ProjectFolderController::Composed ProjectFolderController::compose(const FolderContents &contents, const QString &root)
{
    Composed out;
    out.contents = contents;
    out.aux = out.contents.scripts;
    for (auto it = out.contents.data.constBegin(); it != out.contents.data.constEnd(); ++it)
        out.aux.insert(it.key(), it.value());
    if (!out.contents.templateJson.isNull())
        out.aux.insert(QStringLiteral("template.json"), out.contents.templateJson);

    const JoinResult joined = joinProject(out.contents.project, root);
    out.problems = joined.problems;
    if (!joined.ok)
        return out;

    ScriptInput input;
    input.scripts = out.contents.scripts;
    input.data = out.contents.data;
    input.rootDir = root;
    if (!out.contents.templateJson.isNull()) {
        TemplateSpec spec;
        QList<Problem> ignored;
        if (readTemplate(out.contents.templateJson, &spec, &ignored)) {
            for (const TemplateParam &p : spec.params)
                input.params.insert(p.name, p.defaultValue);
        }
    }
    const ScriptOutcome scripted = runScripts(joined.doc, input);
    out.problems += scripted.problems;
    out.doc = scripted.doc;
    out.scriptsChangedDoc = scripted.doc != joined.doc;
    out.ok = true;

    FileMap lintFiles = out.contents.project;
    if (!out.contents.templateJson.isNull())
        lintFiles.insert(QStringLiteral("template.json"), out.contents.templateJson);
    LintContext context = engineLintContext();
    out.problems += lintProject(lintFiles, root, context);
    return out;
}

ProjectFolderController::ProjectFolderController(ProjectFileController *file, QObject *parent)
    : QObject(parent)
    , m_file(file)
{
    m_writeTimer = new QTimer(this);
    m_writeTimer->setSingleShot(true);
    m_writeTimer->setInterval(kWriteDebounceMs);
    connect(m_writeTimer, &QTimer::timeout, this, &ProjectFolderController::startWrite);

    m_rescanTimer = new QTimer(this);
    m_rescanTimer->setSingleShot(true);
    m_rescanTimer->setInterval(kRescanDebounceMs);
    connect(m_rescanTimer, &QTimer::timeout, this, &ProjectFolderController::rescan);

    // inotify misses files replaced by rename and does nothing on some network shares.
    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(kPollMs);
    connect(m_pollTimer, &QTimer::timeout, this, [this] {
        if (m_writing || !active())
            return;
        QMap<QString, QPair<qint64, qint64>> now;
        const QDir base(m_path);
        QDirIterator it(m_path, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QFileInfo info(it.next());
            const QString rel = base.relativeFilePath(info.filePath());
            if (watchedName(rel))
                now.insert(rel, {info.size(), info.lastModified().toMSecsSinceEpoch()});
        }
        if (now != m_signature)
            scheduleRescan();
    });

    m_watcher = new QFileSystemWatcher(this);
    connect(m_watcher, &QFileSystemWatcher::fileChanged, this, [this] { scheduleRescan(); });
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { scheduleRescan(); });

    connect(m_file, &ProjectFileController::documentEdited, this, &ProjectFolderController::scheduleWrite);
    connect(qApp, &QCoreApplication::aboutToQuit, this, [this] {
        if (active())
            flushNow();
    });
}

QString ProjectFolderController::errorText() const
{
    for (const Problem &p : m_problems) {
        if (p.severity == QLatin1String("error"))
            return positionText(p);
    }
    return {};
}

QVariantList ProjectFolderController::problems() const
{
    QVariantList out;
    for (const Problem &p : m_problems) {
        out.append(QVariantMap{{QStringLiteral("file"), p.file},
                               {QStringLiteral("line"), p.line},
                               {QStringLiteral("column"), p.column},
                               {QStringLiteral("message"), p.message},
                               {QStringLiteral("severity"), p.severity},
                               {QStringLiteral("text"), positionText(p)}});
    }
    return out;
}

void ProjectFolderController::setProblems(const QList<Problem> &problems)
{
    m_problems = problems;
    emit problemsChanged();
}

void ProjectFolderController::dismissError()
{
    setProblems({});
}

void ProjectFolderController::activate(const QString &path, const Composed &composed)
{
    m_path = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    m_known = composed.contents.project;
    m_knownAux = composed.aux;
    m_isTemplate = !composed.contents.templateJson.isNull();
    m_problems = composed.problems;
    writeSupportFiles(m_path);
    // Cache, schema and the conflicts of this folder; none of it belongs in version control.
    const QString gitignore = QDir(m_path).filePath(QStringLiteral(".gitignore"));
    if (!QFileInfo::exists(gitignore)) {
        QFile out(gitignore);
        if (out.open(QIODevice::WriteOnly))
            out.write(".drift/\n");
    }
    m_pollTimer->start();
    rewatch();
    emit activeChanged();
    emit problemsChanged();
}

void ProjectFolderController::close()
{
    if (!active())
        return;
    m_writeTimer->stop();
    m_rescanTimer->stop();
    m_pollTimer->stop();
    if (!m_watcher->files().isEmpty())
        m_watcher->removePaths(m_watcher->files());
    if (!m_watcher->directories().isEmpty())
        m_watcher->removePaths(m_watcher->directories());
    m_writeFuture.waitForFinished();
    m_path.clear();
    m_known.clear();
    m_knownAux.clear();
    m_isTemplate = false;
    m_problems.clear();
    m_writeAgain = m_rescanAfterWrite = false;
    emit activeChanged();
    emit problemsChanged();
}

QString ProjectFolderController::conflictDir() const
{
    return QDir(m_path).filePath(QStringLiteral(".drift/conflicts/")
                                 + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz")));
}

void ProjectFolderController::scheduleWrite()
{
    if (active())
        m_writeTimer->start();
}

void ProjectFolderController::startWrite()
{
    if (!active())
        return;
    if (m_writing) {
        m_writeAgain = true;
        return;
    }
    const ProjectFileController::FolderCapture capture = m_file->captureFolder();
    m_writing = true;
    const QString path = m_path;
    const FileMap known = m_known;
    const QString conflicts = conflictDir();
    m_writeFuture = QtConcurrent::run([this, capture, path, known, conflicts] {
        QJsonObject doc = capture.project->toJson(true);
        if (!capture.guides.isEmpty())
            doc.insert(QStringLiteral("guides"), capture.guides);
        const FileMap files = splitProject(doc, path);
        const WriteReport report = writeProjectFolder(path, files, &known, conflicts);
        QMetaObject::invokeMethod(
            this, [this, capture, files, report, path] {
                m_writing = false;
                if (path != m_path)
                    return;
                landWrite(capture.project, capture.revision, files, report);
            },
            Qt::QueuedConnection);
    });
}

void ProjectFolderController::landWrite(const std::shared_ptr<const drift::Project> &project,
                                        quint64 revision, const FileMap &files, const WriteReport &report)
{
    if (!report.error.isEmpty()) {
        m_file->notify(report.error, QStringLiteral("error"));
        return;
    }
    // What we wrote is what the folder holds now, except where somebody else got there first.
    for (auto it = files.constBegin(); it != files.constEnd(); ++it) {
        if (!report.conflicts.contains(it.key()))
            m_known.insert(it.key(), it.value());
    }
    for (const QString &rel : report.deleted)
        m_known.remove(rel);
    if (!report.conflicts.isEmpty()) {
        m_file->notify(tr("%1 was changed on disk while you edited it; the version on disk was kept and "
                          "yours is in .drift/conflicts")
                           .arg(describe(report.conflicts)),
                       QStringLiteral("warning"));
        m_rescanAfterWrite = true;
    }
    if (m_file->documentRevision() == revision)
        m_file->markFolderClean(project);
    rewatch();
    if (m_writeAgain) {
        m_writeAgain = false;
        startWrite();
        return;
    }
    if (m_rescanAfterWrite) {
        m_rescanAfterWrite = false;
        scheduleRescan();
    }
}

void ProjectFolderController::flushNow()
{
    if (!active())
        return;
    m_writeTimer->stop();
    // A write in flight lands first, and may start the one it deferred.
    while (m_writing) {
        m_writeFuture.waitForFinished();
        QCoreApplication::sendPostedEvents(this, QEvent::MetaCall);
    }
    const ProjectFileController::FolderCapture capture = m_file->captureFolder();
    QJsonObject doc = capture.project->toJson(true);
    if (!capture.guides.isEmpty())
        doc.insert(QStringLiteral("guides"), capture.guides);
    const FileMap files = splitProject(doc, m_path);
    const WriteReport report = writeProjectFolder(m_path, files, &m_known, conflictDir());
    m_writing = false;
    m_writeAgain = false;
    landWrite(capture.project, capture.revision, files, report);
}

void ProjectFolderController::scheduleRescan()
{
    if (m_writing) {
        m_rescanAfterWrite = true;
        return;
    }
    if (active())
        m_rescanTimer->start();
}

QString ProjectFolderController::describe(const QStringList &files)
{
    QStringList names = files.mid(0, 2);
    QString text = names.join(QStringLiteral(", "));
    if (files.size() > 2)
        text += tr(" and %n more", nullptr, files.size() - 2);
    return text;
}

void ProjectFolderController::reload()
{
    // Forget what we knew so the next rescan treats every file as possibly changed.
    scheduleRescan();
}

void ProjectFolderController::rescan()
{
    if (!active())
        return;
    if (m_writing) {
        m_rescanAfterWrite = true;
        return;
    }
    const FolderContents contents = readFolderContents(m_path);
    rewatch();
    if (contents.project.isEmpty()) {
        setProblems({{QStringLiteral("project.json"), 0, 0,
                      tr("The project folder has no project.json (moved or deleted?)")}});
        return;
    }
    // Our own writes land here too, so the cheap comparison comes before any parsing.
    FileMap auxNow = contents.scripts;
    for (auto it = contents.data.constBegin(); it != contents.data.constEnd(); ++it)
        auxNow.insert(it.key(), it.value());
    if (!contents.templateJson.isNull())
        auxNow.insert(QStringLiteral("template.json"), contents.templateJson);
    if (contents.project == m_known && auxNow == m_knownAux) {
        // A file fixed back to what the editor holds: the banner has to clear.
        if (!errorText().isEmpty())
            setProblems(compose(contents, m_path).problems);
        return;
    }
    Composed composed = compose(contents, m_path);

    QStringList changed;
    const auto collect = [&changed](const FileMap &now, const FileMap &was) {
        for (auto it = now.constBegin(); it != now.constEnd(); ++it) {
            if (!was.contains(it.key()) || was.value(it.key()) != it.value())
                changed.append(it.key());
        }
        for (auto it = was.constBegin(); it != was.constEnd(); ++it) {
            if (!now.contains(it.key()))
                changed.append(it.key());
        }
    };
    collect(composed.contents.project, m_known);
    collect(composed.aux, m_knownAux);
    changed.sort();

    if (!composed.ok) {
        setProblems(composed.problems);
        return;
    }
    QString error;
    std::optional<AppController::ParsedProject> parsed = AppController::parseProjectJson(composed.doc, &error);
    if (!parsed) {
        composed.problems.prepend({QStringLiteral("project.json"), 1, 1, error});
        setProblems(composed.problems);
        return;
    }
    setProblems(composed.problems);
    m_known = composed.contents.project;
    m_knownAux = composed.aux;
    m_isTemplate = !composed.contents.templateJson.isNull();

    const QJsonObject current = m_file->currentDocument();
    const QJsonObject incoming = parsed->project.toJson(true);
    if (current == incoming)
        return;
    m_file->applyFolderProject(std::move(parsed->project), parsed->root,
                               tr("External edit: %1").arg(describe(changed)));
    emit filesChangedOnDisk(changed);
}

void ProjectFolderController::detachClip(const QString &clipId)
{
    m_file->detachGenerated(clipId, {});
}

void ProjectFolderController::detachScript(const QString &script)
{
    m_file->detachGenerated({}, script);
}

QVariantMap ProjectFolderController::templateInfo(const QUrl &dataFile) const
{
    QVariantMap out;
    if (!active())
        return out;
    const FolderContents contents = readFolderContents(m_path);
    TemplateSpec spec;
    QList<Problem> problems;
    if (!readTemplate(contents.templateJson, &spec, &problems))
        return out;
    QVariantList params;
    for (const TemplateParam &p : spec.params) {
        params.append(QVariantMap{{QStringLiteral("name"), p.name},
                                  {QStringLiteral("type"), p.type},
                                  {QStringLiteral("label"), p.label},
                                  {QStringLiteral("default"), p.defaultValue.toVariant()},
                                  {QStringLiteral("options"), p.options}});
    }
    QByteArray override;
    if (dataFile.isLocalFile()) {
        QFile file(dataFile.toLocalFile());
        if (file.open(QIODevice::ReadOnly))
            override = file.readAll();
    }
    QString error;
    const QJsonArray rows = templateRows(contents, spec, override, dataFile.toLocalFile(), &error);
    out.insert(QStringLiteral("params"), params);
    out.insert(QStringLiteral("rowCount"), int(rows.size()));
    out.insert(QStringLiteral("dataError"), error);
    out.insert(QStringLiteral("output"), spec.outputPattern);
    out.insert(QStringLiteral("dataFile"), spec.dataFile);
    return out;
}

void ProjectFolderController::renderTemplate(const QUrl &dataFile, const QString &outputPattern, int row, bool all)
{
#if QT_CONFIG(process)
    if (!active() || m_render)
        return;
    QStringList args{QStringLiteral("--headless"), QStringLiteral("--isolated-session"), QStringLiteral("--render"), m_path};
    if (dataFile.isLocalFile())
        args << QStringLiteral("--data") << dataFile.toLocalFile();
    if (all)
        args << QStringLiteral("--all");
    else if (row > 0)
        args << QStringLiteral("--row") << QString::number(row);
    if (!outputPattern.isEmpty())
        args << QStringLiteral("-o") << outputPattern;
    m_renderLog.clear();
    m_render = new QProcess(this);
    m_render->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_render, &QProcess::readyRead, this, [this] {
        m_renderLog += QString::fromLocal8Bit(m_render->readAll());
        emit renderingChanged();
    });
    connect(m_render, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        const bool ok = status == QProcess::NormalExit && code == 0;
        m_render->deleteLater();
        m_render = nullptr;
        emit renderingChanged();
        m_file->notify(ok ? tr("Template render finished") : tr("Template render failed"),
                       ok ? QStringLiteral("success") : QStringLiteral("error"));
    });
    connect(m_render, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        if (m_render && m_render->state() == QProcess::NotRunning) {
            m_renderLog += tr("Could not start the renderer");
            m_render->deleteLater();
            m_render = nullptr;
            emit renderingChanged();
        }
    });
    m_render->start(QCoreApplication::applicationFilePath(), args);
    emit renderingChanged();
#else
    Q_UNUSED(dataFile) Q_UNUSED(outputPattern) Q_UNUSED(row) Q_UNUSED(all)
    m_file->notify(tr("Rendering a template needs a desktop build"), QStringLiteral("error"));
#endif
}

void ProjectFolderController::rewatch()
{
    if (!active())
        return;
    if (!m_watcher->files().isEmpty())
        m_watcher->removePaths(m_watcher->files());
    if (!m_watcher->directories().isEmpty())
        m_watcher->removePaths(m_watcher->directories());
    QStringList dirs{m_path};
    QStringList files;
    m_signature.clear();
    const QDir base(m_path);
    QDirIterator it(m_path, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QFileInfo info(it.next());
        const QString rel = base.relativeFilePath(info.filePath());
        if (rel == QLatin1String(".drift") || rel.startsWith(QLatin1String(".drift/")))
            continue;
        if (info.isDir()) {
            dirs.append(info.filePath());
        } else if (watchedName(rel)) {
            files.append(info.filePath());
            m_signature.insert(rel, {info.size(), info.lastModified().toMSecsSinceEpoch()});
        }
    }
    m_watcher->addPaths(dirs);
    if (files.size() <= kMaxWatchedFiles)
        m_watcher->addPaths(files);
}

void ProjectFolderController::openFolder(const QUrl &folder)
{
    m_file->loadProject(folder);
}

void ProjectFolderController::saveAsFolder(const QUrl &parent, const QString &name)
{
    const QString slug = slugify(name, 60);
    const QString path = QDir(parent.toLocalFile())
                             .filePath((slug.isEmpty() ? QStringLiteral("project") : slug)
                                       + QLatin1String(kFolderSuffix));
    if (QFileInfo::exists(path) && !QDir(path).isEmpty()) {
        m_file->notify(tr("%1 already exists and isn’t empty").arg(QFileInfo(path).fileName()),
                       QStringLiteral("error"));
        return;
    }
    const ProjectFileController::FolderCapture capture = m_file->captureFolder();
    QJsonObject doc = capture.project->toJson(true);
    if (!capture.guides.isEmpty())
        doc.insert(QStringLiteral("guides"), capture.guides);
    const FileMap files = splitProject(doc, path);
    const WriteReport report = writeProjectFolder(path, files);
    if (!report.error.isEmpty()) {
        m_file->notify(report.error, QStringLiteral("error"));
        return;
    }
    m_file->adoptFolder(path, files, capture.project);
    m_file->notify(tr("Saved as project folder %1").arg(QFileInfo(path).fileName()),
                   QStringLiteral("success"));
}

void ProjectFolderController::exportBundle(const QUrl &file)
{
    m_file->exportBundleCopy(file);
}

#include "moc_ProjectFolderController.cpp"
