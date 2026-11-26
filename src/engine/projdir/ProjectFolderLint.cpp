#include "engine/projdir/ProjectFolderLint.h"

#include "engine/packages/effects/EffectCatalog.h"
#include "engine/projdir/ProjectScripts.h"
#include "engine/projdir/ProjectTemplate.h"

#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QSet>

namespace drift::projdir {

LintContext engineLintContext()
{
    LintContext context;
    context.effectParams = [](const QString &id) -> std::optional<QStringList> {
        const EffectPresetEntry *def = effectDefForId(id);
        if (!def)
            return std::nullopt;
        QStringList keys;
        for (const drift::EffectParamSpec &spec : def->meta.parameters)
            keys.append(spec.key);
        return keys;
    };
    return context;
}

QList<Problem> lintFolder(const QString &dir, const LintContext &context)
{
    return lintContents(readFolderContents(dir), context);
}

QList<Problem> lintContents(const FolderContents &contents, const LintContext &context)
{
    FileMap files = contents.project;
    if (!contents.templateJson.isNull())
        files.insert(QStringLiteral("template.json"), contents.templateJson);
    QList<Problem> problems = lintProject(files, contents.rootDir, context);

    if (!contents.templateJson.isNull()) {
        TemplateSpec spec;
        readTemplate(contents.templateJson, &spec, &problems);
        if (!spec.dataFile.isEmpty() && !contents.data.contains(QDir::cleanPath(spec.dataFile)))
            problems.append({QStringLiteral("template.json"), 1, 1,
                             QStringLiteral("data file %1 not found").arg(spec.dataFile)});
    }

    const JoinResult joined = joinProject(contents.project, contents.rootDir);
    if (joined.ok && !contents.scripts.isEmpty()) {
        ScriptInput input;
        input.scripts = contents.scripts;
        input.data = contents.data;
        input.rootDir = contents.rootDir;
        if (!contents.templateJson.isNull()) {
            TemplateSpec spec;
            QList<Problem> ignored;
            if (readTemplate(contents.templateJson, &spec, &ignored)) {
                for (const TemplateParam &p : spec.params)
                    input.params.insert(p.name, p.defaultValue);
            }
        }
        problems += runScripts(joined.doc, input).problems;
    }

    // Generated items whose script is gone stay, unowned and unlocked; say so once per script.
    QSet<QString> reported;
    for (const QString &file : files.keys()) {
        if (!file.startsWith(QLatin1String("timeline/")) && !file.startsWith(QLatin1String("assets/"))
            && !file.startsWith(QLatin1String("sequences/")))
            continue;
        const QByteArray &bytes = files.value(file);
        int at = 0;
        const QByteArray needle = "\"generatedBy\"";
        while ((at = bytes.indexOf(needle, at)) >= 0) {
            at += needle.size();
            const int quote = bytes.indexOf('"', bytes.indexOf(':', at));
            const int end = bytes.indexOf('"', quote + 1);
            if (quote < 0 || end < 0)
                break;
            const QString script = QString::fromUtf8(bytes.mid(quote + 1, end - quote - 1));
            if (!contents.scripts.contains(script) && !reported.contains(file + script)) {
                reported.insert(file + script);
                Problem p{file, 1, 1, QStringLiteral("generatedBy names %1, which does not exist; the items stay but are no longer replaced").arg(script),
                          QStringLiteral("warning")};
                locateKey(bytes, QStringLiteral("generatedBy"), script, &p.line, &p.column);
                problems.append(p);
            }
        }
    }
    std::stable_sort(problems.begin(), problems.end(), [](const Problem &a, const Problem &b) {
        return a.file == b.file ? a.line < b.line : a.file < b.file;
    });
    return problems;
}

QJsonArray problemsToJson(const QList<Problem> &problems)
{
    QJsonArray out;
    for (const Problem &p : problems) {
        out.append(QJsonObject{{QStringLiteral("file"), p.file},
                               {QStringLiteral("line"), p.line},
                               {QStringLiteral("column"), p.column},
                               {QStringLiteral("severity"), p.severity},
                               {QStringLiteral("message"), p.message}});
    }
    return out;
}

bool hasErrors(const QList<Problem> &problems)
{
    for (const Problem &p : problems) {
        if (p.severity == QLatin1String("error"))
            return true;
    }
    return false;
}

} // namespace drift::projdir
