#include "engine/projdir/ProjectTemplate.h"

#include "core/project/folder/ProjectData.h"
#include "core/project/folder/ProjectLint.h"
#include "engine/projdir/ProjectScripts.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>

#include <cmath>

namespace drift::projdir {

namespace {

QString valueText(const QJsonValue &v)
{
    if (v.isDouble()) {
        const double d = v.toDouble();
        return d == std::floor(d) ? QString::number(qint64(d)) : QString::number(d);
    }
    return v.toString();
}

QString safeFileText(QString text)
{
    static const QRegularExpression bad(QStringLiteral("[\\\\/:*?\"<>|\\x00-\\x1f]"));
    text.replace(bad, QStringLiteral("_"));
    return text.trimmed();
}

} // namespace

FolderContents readFolderContents(const QString &dir)
{
    FolderContents contents;
    contents.rootDir = QDir::cleanPath(QFileInfo(dir).absoluteFilePath());
    contents.project = readProjectFolder(contents.rootDir);
    contents.scripts = readScripts(contents.rootDir);
    contents.data = readData(contents.rootDir);
    QFile file(QDir(contents.rootDir).filePath(QStringLiteral("template.json")));
    if (file.open(QIODevice::ReadOnly))
        contents.templateJson = file.readAll();
    return contents;
}

bool isTemplateFolder(const QString &dir)
{
    return QFileInfo(QDir(dir).filePath(QStringLiteral("template.json"))).isFile();
}

bool readTemplate(const QByteArray &templateJson, TemplateSpec *spec, QList<Problem> *problems)
{
    if (templateJson.isNull()) {
        problems->append({QStringLiteral("template.json"), 1, 1, QStringLiteral("template.json is missing")});
        return false;
    }
    QJsonValue value;
    Problem problem;
    if (!parseJsonFile(QStringLiteral("template.json"), templateJson, &value, &problem) || !value.isObject()) {
        problems->append(problem.message.isEmpty() ? Problem{QStringLiteral("template.json"), 1, 1, QStringLiteral("Expected a JSON object")}
                                                    : problem);
        return false;
    }
    const QJsonObject object = value.toObject();
    static const QStringList types{QStringLiteral("text"), QStringLiteral("media"), QStringLiteral("color"),
                                   QStringLiteral("number"), QStringLiteral("choice")};
    bool ok = true;
    QSet<QString> seen;
    for (const QJsonValue &v : object.value(QStringLiteral("params")).toArray()) {
        const QJsonObject p = v.toObject();
        TemplateParam param;
        param.name = p.value(QStringLiteral("name")).toString();
        param.type = p.value(QStringLiteral("type")).toString(QStringLiteral("text"));
        param.label = p.value(QStringLiteral("label")).toString(param.name);
        param.defaultValue = p.value(QStringLiteral("default"));
        for (const QJsonValue &o : p.value(QStringLiteral("options")).toArray())
            param.options.append(o.toString());
        const auto bad = [&](const QString &message) {
            Problem q{QStringLiteral("template.json"), 1, 1, message};
            locateKey(templateJson, QStringLiteral("name"), param.name, &q.line, &q.column);
            problems->append(q);
            ok = false;
        };
        if (param.name.isEmpty())
            bad(QStringLiteral("a parameter has no name"));
        else if (!types.contains(param.type))
            bad(QStringLiteral("parameter \"%1\": type must be one of %2").arg(param.name, types.join(QStringLiteral(", "))));
        else if (seen.contains(param.name))
            bad(QStringLiteral("parameter \"%1\" is declared twice").arg(param.name));
        else if (param.type == QLatin1String("choice") && param.options.isEmpty())
            bad(QStringLiteral("parameter \"%1\" is a choice and needs options").arg(param.name));
        seen.insert(param.name);
        spec->params.append(param);
    }
    spec->dataFile = object.value(QStringLiteral("data")).toString();
    spec->outputPattern = object.value(QStringLiteral("output")).toString();
    return ok;
}

QJsonArray templateRows(const FolderContents &folder, const TemplateSpec &spec, const QByteArray &dataOverride,
                        const QString &dataName, QString *error)
{
    if (!dataOverride.isEmpty())
        return parseDataRows(dataName, dataOverride, error);
    if (spec.dataFile.isEmpty())
        return {};
    const QString key = QDir::cleanPath(spec.dataFile);
    if (!folder.data.contains(key)) {
        if (error)
            *error = QStringLiteral("data file %1 not found").arg(spec.dataFile);
        return {};
    }
    return parseDataRows(spec.dataFile, folder.data.value(key), error);
}

QJsonObject resolveParams(const FolderContents &folder, const TemplateSpec &spec, const QJsonObject &row,
                         const QJsonObject &overrides, QList<Problem> *problems)
{
    QJsonObject params;
    for (const TemplateParam &p : spec.params) {
        QJsonValue v = p.defaultValue;
        if (row.contains(p.name))
            v = row.value(p.name);
        if (overrides.contains(p.name))
            v = overrides.value(p.name);
        const auto bad = [&](const QString &message) {
            problems->append({QStringLiteral("template.json"), 1, 1, message});
        };
        if (p.type == QLatin1String("number")) {
            bool numeric = v.isDouble();
            double d = v.toDouble();
            if (!numeric) {
                d = v.toString().toDouble(&numeric);
            }
            if (!numeric) {
                bad(QStringLiteral("parameter \"%1\" should be a number, got \"%2\"").arg(p.name, valueText(v)));
                d = 0;
            }
            v = d;
        } else if (p.type == QLatin1String("color")) {
            const QColor color(valueText(v));
            if (!color.isValid() && !valueText(v).isEmpty())
                bad(QStringLiteral("parameter \"%1\" should be a colour, got \"%2\"").arg(p.name, valueText(v)));
            v = color.isValid() ? color.name(QColor::HexRgb) : valueText(v);
        } else if (p.type == QLatin1String("choice")) {
            const QString text = valueText(v);
            if (!p.options.contains(text))
                bad(QStringLiteral("parameter \"%1\" is \"%2\"; choose one of %3").arg(p.name, text, p.options.join(QStringLiteral(", "))));
            v = text;
        } else if (p.type == QLatin1String("media")) {
            QString path = valueText(v);
            if (!path.isEmpty() && QDir::isRelativePath(path) && !path.contains(QLatin1String("://")))
                path = QDir::cleanPath(folder.rootDir + QLatin1Char('/') + path);
            v = path;
        } else {
            v = valueText(v);
        }
        params.insert(p.name, v);
    }
    return params;
}

Expansion expandTemplate(const FolderContents &folder, const TemplateSpec &spec, const QJsonObject &row,
                         const QJsonObject &overrides)
{
    Expansion out;
    const QJsonObject params = resolveParams(folder, spec, row, overrides, &out.problems);
    for (const Problem &p : out.problems) {
        if (p.severity == QLatin1String("error"))
            return out;
    }

    const JoinResult joined = joinProject(folder.project, folder.rootDir);
    out.problems += joined.problems;
    if (!joined.ok)
        return out;

    ScriptInput input;
    input.scripts = folder.scripts;
    input.data = folder.data;
    input.params = params;
    input.rootDir = folder.rootDir;
    const ScriptOutcome scripted = runScripts(joined.doc, input);
    out.problems += scripted.problems;
    for (const Problem &p : scripted.problems) {
        if (p.severity == QLatin1String("error"))
            return out;
    }
    out.doc = scripted.doc;
    out.params = params;
    out.ok = true;
    return out;
}

QString expandOutputPattern(const QString &pattern, int row, const QJsonObject &params)
{
    QString out = pattern;
    out.replace(QStringLiteral("{row}"), QString::number(row));
    for (auto it = params.constBegin(); it != params.constEnd(); ++it)
        out.replace(QLatin1Char('{') + it.key() + QLatin1Char('}'), safeFileText(valueText(it.value())));
    return out;
}

} // namespace drift::projdir
