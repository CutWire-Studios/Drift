#include "app/farm/TemplateBatch.h"

#include "app/farm/RenderQueue.h"
#include "core/project/folder/ProjectLint.h"
#include "engine/projdir/ProjectTemplate.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>

namespace drift::farm {

using namespace drift::projdir;

TemplateBatchResult queueTemplateBatch(RenderQueue &queue, const TemplateBatch &batch)
{
    TemplateBatchResult result;
    if (!isTemplateFolder(batch.folder)) {
        result.error = QStringLiteral("%1 has no template.json").arg(batch.folder);
        return result;
    }
    const FolderContents folder = readFolderContents(batch.folder);
    TemplateSpec spec;
    QList<Problem> problems;
    if (!readTemplate(folder.templateJson, &spec, &problems)) {
        result.error = formatProblems(problems);
        return result;
    }

    QByteArray dataBytes;
    if (!batch.dataFile.isEmpty()) {
        QFile file(batch.dataFile);
        if (!file.open(QIODevice::ReadOnly)) {
            result.error = QStringLiteral("cannot read %1").arg(batch.dataFile);
            return result;
        }
        dataBytes = file.readAll();
    }
    QString dataError;
    const QJsonArray rows = templateRows(folder, spec, dataBytes, batch.dataFile, &dataError);
    if (!dataError.isEmpty()) {
        result.error = QStringLiteral("data: %1").arg(dataError);
        return result;
    }

    QList<int> selected; // 0 = no data, one render of the defaults
    if (rows.isEmpty()) {
        selected.append(0);
    } else if (batch.all) {
        for (int i = 1; i <= rows.size(); ++i)
            selected.append(i);
    } else if (batch.row >= 1 && batch.row <= rows.size()) {
        selected.append(batch.row);
    } else {
        result.error = QStringLiteral("the data has %1 rows: pick a row (1 to %1) or every row").arg(rows.size());
        return result;
    }

    QString pattern = batch.output.isEmpty() ? spec.outputPattern : batch.output;
    if (pattern.isEmpty())
        pattern = QStringLiteral("render-{row}.mp4");
    if (QDir::isRelativePath(pattern))
        pattern = QDir::current().filePath(pattern);
    if (selected.size() > 1 && !pattern.contains(QLatin1String("{row}"))) {
        result.error = QStringLiteral("the output pattern needs {row} to tell %1 renders apart").arg(selected.size());
        return result;
    }

    QTemporaryDir scratch;
    for (const int row : std::as_const(selected)) {
        const QJsonObject rowData = row > 0 ? rows.at(row - 1).toObject() : QJsonObject();
        const Expansion expanded = expandTemplate(folder, spec, rowData, batch.overrides);
        if (!expanded.ok) {
            result.failures.append(QStringLiteral("row %1: %2").arg(row).arg(formatProblems(expanded.problems).trimmed()));
            continue;
        }
        const QString snapshot = scratch.filePath(QStringLiteral("row-%1.json").arg(row));
        QFile file(snapshot);
        if (!file.open(QIODevice::WriteOnly)
            || file.write(QJsonDocument(expanded.doc).toJson(QJsonDocument::Compact)) < 0) {
            result.failures.append(QStringLiteral("row %1: cannot write the project snapshot").arg(row));
            continue;
        }
        file.close();
        const QString out = expandOutputPattern(pattern, row, expanded.params);
        const QString id = queue.addProjectFile(snapshot, out, batch.settings, batch.target, batch.hold,
                                                QJsonObject{{QStringLiteral("template"), folder.rootDir},
                                                            {QStringLiteral("row"), rowData}});
        if (id.isEmpty())
            result.failures.append(QStringLiteral("row %1: could not be added to the queue").arg(row));
        else
            result.jobs.append({row, id, out});
    }
    if (result.jobs.isEmpty() && result.error.isEmpty())
        result.error = result.failures.join(QLatin1Char('\n'));
    return result;
}

} // namespace drift::farm
