#pragma once

#include "core/project/folder/ProjectLint.h"
#include "engine/projdir/ProjectFolderLint.h"

#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QSet>

namespace drift::projdir {

// A small Language Server for project folders: diagnostics from the lint and hover text for keys.
// Completion and validation-as-you-type come from the JSON schemas through a regular JSON language
// server; this adds what a schema cannot say. Transport-free: feed it decoded messages, send what it
// returns.
class ProjectLsp
{
public:
    explicit ProjectLsp(LintContext context = {}) : m_context(std::move(context)) {}

    QList<QJsonObject> handle(const QJsonObject &message);
    bool exiting() const { return m_exit; }
    int exitCode() const { return m_shutdown ? 0 : 1; }

    // `Content-Length` framing, shared with the stdio loop.
    static QByteArray frame(const QJsonObject &message);

    // Key at `line`/`character` of `text` and where it sits, for tests and hover.
    struct KeyAt
    {
        QString key;
        QStringList path; // keys of the objects it is nested in, outermost first
    };
    static bool keyAt(const QByteArray &text, int line, int character, KeyAt *out);
    static QString hoverText(const QString &relPath, const KeyAt &at);

private:
    QList<QJsonObject> publish();
    QString relPath(const QString &uri) const;
    FolderContents currentContents() const;

    LintContext m_context;
    QString m_root;
    QMap<QString, QByteArray> m_open; // relative path -> unsaved text
    QSet<QString> m_published;
    bool m_shutdown = false;
    bool m_exit = false;
};

// Reads framed messages on stdin and answers on stdout until `exit`.
int runLspStdio(const LintContext &context);

} // namespace drift::projdir
