#include "app/mcp/tools/McpProjectFolder.h"

#include "app/editor/AppController.h"
#include "app/editor/ProjectFileController.h"
#include "app/editor/ProjectFolderController.h"
#include "app/mcp/protocol/McpJson.h"
#include "engine/projdir/ProjectFolderLint.h"
#include "engine/projdir/ProjectGuide.h"
#include "engine/projdir/ProjectTemplate.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QtGlobal>
#if QT_CONFIG(process)
#include <QProcess>
#endif
#include <QStandardPaths>
#include <QTimer>

namespace drift::mcp::projectfolder {

using namespace drift::projdir;

QJsonObject guide()
{
    const QString text = projectFolderGuide();
    if (text.isEmpty())
        return err("io_error", QStringLiteral("the embedded project folder guide is missing"));
    return ok({{QStringLiteral("guide"), text}});
}

QJsonObject lint(AppController *controller, const QJsonObject &args)
{
    QString folder = args.value(QStringLiteral("folder")).toString();
    if (folder.isEmpty() && controller)
        folder = controller->projectFile()->folder()->folderPath();
    if (folder.isEmpty())
        return err("bad_args", QStringLiteral("pass folder, or open a project folder first"));
    if (!isProjectFolder(folder))
        return err("not_found", QStringLiteral("%1 is not a project folder (no project.json)").arg(folder));
    QList<Problem> problems = lintFolder(folder, engineLintContext());
    if (!args.value(QStringLiteral("info")).toBool(false))
        problems.removeIf([](const Problem &p) { return p.severity == QLatin1String("info"); });
    int errors = 0;
    int warnings = 0;
    for (const Problem &p : problems) {
        errors += p.severity == QLatin1String("error");
        warnings += p.severity == QLatin1String("warning");
    }
    return ok({{QStringLiteral("folder"), folder},
               {QStringLiteral("problems"), problemsToJson(problems)},
               {QStringLiteral("errors"), errors},
               {QStringLiteral("warnings"), warnings},
               {QStringLiteral("text"), formatProblems(problems)}});
}

QJsonObject renderTemplate(const QJsonObject &args)
{
    const QString folder = args.value(QStringLiteral("folder")).toString();
    if (!isTemplateFolder(folder))
        return err("not_found", QStringLiteral("%1 has no template.json").arg(folder));
    const FolderContents contents = readFolderContents(folder);
    TemplateSpec spec;
    QList<Problem> problems;
    if (!readTemplate(contents.templateJson, &spec, &problems))
        return err("bad_template", formatProblems(problems));

    const QString data = args.value(QStringLiteral("data")).toString();
    QByteArray dataBytes;
    if (!data.isEmpty()) {
        QFile file(data);
        if (!file.open(QIODevice::ReadOnly))
            return err("bad_args", QStringLiteral("cannot read %1").arg(data));
        dataBytes = file.readAll();
    }
    QString dataError;
    const QJsonArray rows = templateRows(contents, spec, dataBytes, data, &dataError);
    if (!dataError.isEmpty())
        return err("bad_args", QStringLiteral("data: %1").arg(dataError));

    const bool all = args.value(QStringLiteral("all")).toBool(false);
    const int row = args.value(QStringLiteral("row")).toInt(0);
    QList<int> selected;
    if (rows.isEmpty())
        selected.append(0);
    else if (all)
        for (int i = 1; i <= rows.size(); ++i)
            selected.append(i);
    else if (row >= 1 && row <= rows.size())
        selected.append(row);
    else
        return err("bad_args", QStringLiteral("the data has %1 rows: pass row (1..%1) or all:true").arg(rows.size()));

    QString pattern = args.value(QStringLiteral("output")).toString();
    if (pattern.isEmpty())
        pattern = spec.outputPattern.isEmpty() ? QStringLiteral("render-{row}.mp4") : spec.outputPattern;
    if (QDir::isRelativePath(pattern))
        pattern = QDir(QFileInfo(contents.rootDir).absolutePath()).filePath(pattern);
    if (selected.size() > 1 && !pattern.contains(QLatin1String("{row}")))
        return err("bad_args", QStringLiteral("output needs {row} to tell %1 renders apart").arg(selected.size()));

    QJsonArray outputs;
    for (const int r : std::as_const(selected)) {
        QList<Problem> ignored;
        const QJsonObject params = resolveParams(contents, spec, r > 0 ? rows.at(r - 1).toObject() : QJsonObject(), {}, &ignored);
        outputs.append(expandOutputPattern(pattern, r, params));
    }

#if QT_CONFIG(process)
    QStringList cmd{QStringLiteral("--headless"), QStringLiteral("--isolated-session"), QStringLiteral("--render"),
                    contents.rootDir};
    if (!data.isEmpty())
        cmd << QStringLiteral("--data") << data;
    if (all)
        cmd << QStringLiteral("--all");
    else if (row > 0)
        cmd << QStringLiteral("--row") << QString::number(row);
    cmd << QStringLiteral("-o") << pattern;

    const QString logPath = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                                .filePath(QStringLiteral("drift-template-render-%1.log").arg(QCoreApplication::applicationPid()));
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.setStandardOutputFile(logPath);
    process.setProgram(QCoreApplication::applicationFilePath());
    process.setArguments(cmd);
    const bool wait = args.value(QStringLiteral("wait")).toBool(false);
    QJsonObject result{{QStringLiteral("outputs"), outputs}, {QStringLiteral("log"), logPath}};
    if (!wait) {
        qint64 pid = 0;
        if (!process.startDetached(&pid))
            return err("start_failed", QStringLiteral("could not start %1").arg(process.program()));
        result.insert(QStringLiteral("pid"), pid);
        result.insert(QStringLiteral("started"), true);
        return ok(result);
    }
    QEventLoop loop;
    QObject::connect(&process, &QProcess::finished, &loop, &QEventLoop::quit);
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeout.start(int(qBound(1.0, args.value(QStringLiteral("timeout")).toDouble(1800.0), 7200.0) * 1000.0));
    process.start();
    if (!process.waitForStarted())
        return err("start_failed", QStringLiteral("could not start %1").arg(process.program()));
    loop.exec();
    if (process.state() != QProcess::NotRunning) {
        process.kill();
        process.waitForFinished();
        result.insert(QStringLiteral("timeout"), true);
    }
    result.insert(QStringLiteral("exit_code"), process.exitCode());
    QFile log(logPath);
    if (log.open(QIODevice::ReadOnly))
        result.insert(QStringLiteral("output"), QString::fromLocal8Bit(log.readAll().right(4000)));
    if (process.exitCode() != 0)
        return err("render_failed", result.value(QStringLiteral("output")).toString());
    return ok(result);
#else
    return err("unsupported", QStringLiteral("rendering a template needs a desktop build"));
#endif
}

} // namespace drift::mcp::projectfolder
