#pragma once

#include "core/project/Project.h"
#include "core/project/folder/ProjectFolder.h"
#include "engine/projdir/ProjectTemplate.h"

#include <QFuture>
#include <QObject>
#include <QSet>
#include <QUrl>
#include <QVariantList>

#include <memory>

class QFileSystemWatcher;
class QProcess;
class QTimer;
class ProjectFileController;

// A project that lives as a folder of JSON files (docs/project-folder.md). The folder is canonical:
// GUI edits are written out after a short debounce, and files changed on disk are read back and applied
// to the open project as one undo step. Reached from QML as EditorState.projectFile.folder.
class ProjectFolderController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    Q_PROPERTY(QString folderPath READ folderPath NOTIFY activeChanged)
    // "file:line:col: message" of the first problem, empty when the folder reads cleanly.
    Q_PROPERTY(QString errorText READ errorText NOTIFY problemsChanged)
    // [{file, line, column, message, severity}]
    Q_PROPERTY(QVariantList problems READ problems NOTIFY problemsChanged)
    // The folder has a template.json: File > Render with data applies.
    Q_PROPERTY(bool isTemplate READ isTemplate NOTIFY activeChanged)
    Q_PROPERTY(bool rendering READ rendering NOTIFY renderingChanged)
    Q_PROPERTY(QString renderLog READ renderLog NOTIFY renderingChanged)

public:
    explicit ProjectFolderController(ProjectFileController *file, QObject *parent = nullptr);

    bool active() const { return !m_path.isEmpty(); }
    QString folderPath() const { return m_path; }
    QString errorText() const;
    QVariantList problems() const;
    const QList<drift::projdir::Problem> &problemList() const { return m_problems; }

    bool isTemplate() const { return m_isTemplate; }
    bool rendering() const { return m_render != nullptr; }
    QString renderLog() const { return m_renderLog; }

    // A folder read from disk and composed into one project document: the files joined, the scripts run,
    // the lint applied. Pure of editor state, so opening and re-reading share it.
    struct Composed
    {
        drift::projdir::FolderContents contents;
        drift::projdir::FileMap aux; // scripts, data and template.json: what changes re-run the scripts
        QJsonObject doc;
        QList<drift::projdir::Problem> problems;
        bool ok = false;
        bool scriptsChangedDoc = false;
    };
    static Composed compose(const QString &root);
    static Composed compose(const drift::projdir::FolderContents &contents, const QString &root);

    // Becomes the folder behind the open project.
    void activate(const QString &path, const Composed &composed);
    void close();
    void setProblems(const QList<drift::projdir::Problem> &problems);

    // Writes the open project out now and waits for it. Ctrl+S and quit.
    Q_INVOKABLE void flushNow();
    // Re-reads the folder from disk, as if a file had just changed.
    Q_INVOKABLE void reload();
    void scheduleWrite();
    // Detach turns what a script made into plain, editable data: one clip, or everything from `script`.
    Q_INVOKABLE void detachClip(const QString &clipId);
    Q_INVOKABLE void detachScript(const QString &script);
    // [{name, type, label, default, options}], the row count of `dataFile` (or the template's own data)
    // and the template's output pattern.
    Q_INVOKABLE QVariantMap templateInfo(const QUrl &dataFile = QUrl()) const;
    // Renders the template in a headless child process, one output per row (row 0 + all = every row).
    Q_INVOKABLE void renderTemplate(const QUrl &dataFile, const QString &outputPattern, int row, bool all);
    Q_INVOKABLE void dismissError();
    Q_INVOKABLE void openFolder(const QUrl &folder);
    // <parent>/<name>.driftproj from the open project. Becomes the open document.
    Q_INVOKABLE void saveAsFolder(const QUrl &parent, const QString &name);
    // A .drift bundle of the open folder, written beside it without leaving folder mode.
    Q_INVOKABLE void exportBundle(const QUrl &file);

signals:
    void activeChanged();
    void problemsChanged();
    // Relative paths of files that changed on disk and were read back.
    void filesChangedOnDisk(const QStringList &files);
    void renderingChanged();

private:
    void startWrite();
    void landWrite(const std::shared_ptr<const drift::Project> &project, quint64 revision,
                   const drift::projdir::FileMap &files, const drift::projdir::WriteReport &report);
    void scheduleRescan();
    void rescan();
    void rewatch();
    QString conflictDir() const;
    static QString describe(const QStringList &files);

    ProjectFileController *m_file = nullptr;
    QString m_path;
    // What the folder held the last time we wrote it or read it.
    drift::projdir::FileMap m_known;
    drift::projdir::FileMap m_knownAux;
    bool m_isTemplate = false;
    QProcess *m_render = nullptr;
    QString m_renderLog;
    QList<drift::projdir::Problem> m_problems;

    QTimer *m_writeTimer = nullptr;
    QTimer *m_rescanTimer = nullptr;
    QTimer *m_pollTimer = nullptr;
    QFileSystemWatcher *m_watcher = nullptr;
    QMap<QString, QPair<qint64, qint64>> m_signature;
    bool m_writing = false;
    bool m_writeAgain = false;
    bool m_rescanAfterWrite = false;
    QFuture<void> m_writeFuture;
};
