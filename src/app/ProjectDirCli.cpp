#include "app/ProjectDirCli.h"

#include "app/editor/EditorState.h"
#include "app/editor/ProjectFileController.h"
#include "app/mcp/tools/McpDispatcher.h"
#include "engine/packages/effects/EffectCatalog.h"
#include "engine/projdir/ProjectFolderLint.h"
#include "engine/projdir/ProjectLsp.h"
#include "engine/projdir/ProjectTemplate.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <cstdio>

namespace drift::projdir {

namespace {

void print(std::FILE *out, const QString &text)
{
    const QByteArray bytes = text.toUtf8();
    std::fwrite(bytes.constData(), 1, size_t(bytes.size()), out);
    std::fflush(out);
}

int runLint(int argc, char *argv[])
{
    QString folder;
    bool json = false;
    bool all = false;
    for (int i = 2; i < argc; ++i) {
        const QByteArray arg(argv[i]);
        if (arg == "--json")
            json = true;
        else if (arg == "--all")
            all = true;
        else if (!arg.startsWith('-'))
            folder = QString::fromLocal8Bit(arg);
    }
    if (folder.isEmpty()) {
        print(stderr, QStringLiteral("usage: drift lint <project folder> [--json] [--all]\n"));
        return 2;
    }
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("CutWire Drift"));
    QCoreApplication::setOrganizationName(QStringLiteral("CutWire Drift"));
    if (!isProjectFolder(folder)) {
        print(stderr, QStringLiteral("%1: not a project folder (no project.json)\n").arg(folder));
        return 2;
    }
    reloadEffectCatalog();
    QList<Problem> problems = lintFolder(folder, engineLintContext());
    // info lines (a parameter left at its default) are noise unless asked for.
    if (!all)
        problems.removeIf([](const Problem &p) { return p.severity == QLatin1String("info"); });
    if (json)
        print(stdout, QString::fromUtf8(QJsonDocument(problemsToJson(problems)).toJson()));
    else
        print(stdout, formatProblems(problems));
    return hasErrors(problems) ? 1 : 0;
}

} // namespace

bool handleProjectDirCli(int argc, char *argv[], int *exitCode)
{
    if (argc >= 2 && qstrcmp(argv[1], "lint") == 0) {
        *exitCode = runLint(argc, argv);
        return true;
    }
    for (int i = 1; i < argc; ++i) {
        if (qstrcmp(argv[i], "--lsp") == 0) {
            QCoreApplication app(argc, argv);
            QCoreApplication::setApplicationName(QStringLiteral("CutWire Drift"));
            QCoreApplication::setOrganizationName(QStringLiteral("CutWire Drift"));
            reloadEffectCatalog();
            *exitCode = runLspStdio(engineLintContext());
            return true;
        }
    }
    return false;
}

bool parseTemplateRender(int argc, char *argv[], RenderRequest *request)
{
    for (int i = 1; i < argc; ++i) {
        const QByteArray arg(argv[i]);
        if (arg == "--render" && i + 1 < argc)
            request->folder = QString::fromLocal8Bit(argv[++i]);
        else if (arg == "--data" && i + 1 < argc)
            request->dataFile = QString::fromLocal8Bit(argv[++i]);
        else if ((arg == "-o" || arg == "--output") && i + 1 < argc)
            request->output = QString::fromLocal8Bit(argv[++i]);
        else if (arg == "--row" && i + 1 < argc)
            request->row = QByteArray(argv[++i]).toInt();
        else if (arg == "--all")
            request->all = true;
    }
    return !request->folder.isEmpty() && isTemplateFolder(request->folder);
}

int runTemplateRender(EditorState &state, const RenderRequest &request)
{
    const FolderContents folder = readFolderContents(request.folder);
    TemplateSpec spec;
    QList<Problem> problems;
    if (!readTemplate(folder.templateJson, &spec, &problems)) {
        print(stderr, formatProblems(problems));
        return 2;
    }

    QByteArray dataBytes;
    if (!request.dataFile.isEmpty()) {
        QFile file(request.dataFile);
        if (!file.open(QIODevice::ReadOnly)) {
            print(stderr, QStringLiteral("cannot read %1\n").arg(request.dataFile));
            return 2;
        }
        dataBytes = file.readAll();
    }
    QString error;
    const QJsonArray rows = templateRows(folder, spec, dataBytes, request.dataFile, &error);
    if (!error.isEmpty()) {
        print(stderr, QStringLiteral("data: %1\n").arg(error));
        return 2;
    }

    QList<int> selected; // 1-based row numbers; 0 = no data, render the defaults once
    if (rows.isEmpty()) {
        selected.append(0);
    } else if (request.all) {
        for (int i = 1; i <= rows.size(); ++i)
            selected.append(i);
    } else if (request.row >= 1 && request.row <= rows.size()) {
        selected.append(request.row);
    } else {
        print(stderr, QStringLiteral("the data has %1 rows: pass --row N (1 to %1) or --all\n").arg(rows.size()));
        return 2;
    }

    QString pattern = request.output.isEmpty() ? spec.outputPattern : request.output;
    if (pattern.isEmpty())
        pattern = QStringLiteral("render-{row}.mp4");
    if (QDir::isRelativePath(pattern))
        pattern = QDir::current().filePath(pattern);
    // Several rows into one file name would overwrite each other.
    if (selected.size() > 1 && !pattern.contains(QLatin1String("{row}"))) {
        print(stderr, QStringLiteral("the output pattern needs {row} to tell %1 renders apart\n").arg(selected.size()));
        return 2;
    }

    QTemporaryDir scratch;
    int failures = 0;
    drift::mcp::McpDispatcher dispatcher(&state);
    for (const int row : std::as_const(selected)) {
        const Expansion expanded = expandTemplate(folder, spec, row > 0 ? rows.at(row - 1).toObject() : QJsonObject());
        if (!expanded.ok) {
            print(stderr, formatProblems(expanded.problems));
            ++failures;
            continue;
        }
        const QString out = expandOutputPattern(pattern, row, expanded.params);
        const QString projectPath = scratch.filePath(QStringLiteral("row-%1.json").arg(row));
        QFile file(projectPath);
        if (!file.open(QIODevice::WriteOnly)
            || file.write(QJsonDocument(expanded.doc).toJson(QJsonDocument::Compact)) < 0) {
            print(stderr, QStringLiteral("cannot write %1\n").arg(projectPath));
            ++failures;
            continue;
        }
        file.close();

        bool loaded = false;
        bool loadOk = false;
        QEventLoop loadLoop;
        QObject::connect(state.projectFile(), &ProjectFileController::projectLoadFinished, &loadLoop,
                         [&](bool ok, const QString &) {
                             loaded = true;
                             loadOk = ok;
                             loadLoop.quit();
                         });
        state.projectFile()->loadProjectJson(QUrl::fromLocalFile(projectPath));
        if (!loaded)
            loadLoop.exec();
        if (!loadOk) {
            print(stderr, QStringLiteral("row %1: the expanded project did not load\n").arg(row));
            ++failures;
            continue;
        }

        QDir().mkpath(QFileInfo(out).absolutePath());
        bool finished = false;
        bool exportOk = false;
        QEventLoop exportLoop;
        QObject::connect(&state, &AppController::exportFinished, &exportLoop, [&](bool ok) {
            finished = true;
            exportOk = ok;
            exportLoop.quit();
        });
        const QJsonObject started = dispatcher.applyOne(QStringLiteral("export_video"), {{QStringLiteral("path"), out}});
        if (!started.value(QStringLiteral("ok")).toBool()) {
            print(stderr, QStringLiteral("row %1: %2 %3\n").arg(row).arg(started.value(QStringLiteral("error")).toString(),
                                                                    started.value(QStringLiteral("detail")).toString()));
            ++failures;
            continue;
        }
        if (!finished && state.exportInProgress())
            exportLoop.exec();
        if (finished && !exportOk) {
            print(stderr, QStringLiteral("row %1: export failed\n").arg(row));
            ++failures;
            continue;
        }
        print(stdout, QStringLiteral("%1\t%2\n").arg(row).arg(out));
    }
    return failures ? 1 : 0;
}

} // namespace drift::projdir
