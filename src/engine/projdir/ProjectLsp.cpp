#include "engine/projdir/ProjectLsp.h"

#include "core/project/folder/ProjectSchema.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUrl>

#include <cstdio>

#ifdef Q_OS_WIN
#include <fcntl.h>
#include <io.h>
#endif

namespace drift::projdir {

namespace {

QJsonObject response(const QJsonValue &id, const QJsonValue &result)
{
    return {{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("id"), id},
            {QStringLiteral("result"), result}};
}

int lineLength(const QByteArray &text, int line)
{
    int current = 0;
    int start = 0;
    for (int i = 0; i < text.size(); ++i) {
        if (text.at(i) == '\n') {
            if (current == line)
                return i - start;
            ++current;
            start = i + 1;
        }
    }
    return current == line ? int(text.size()) - start : 0;
}

} // namespace

QByteArray ProjectLsp::frame(const QJsonObject &message)
{
    const QByteArray body = QJsonDocument(message).toJson(QJsonDocument::Compact);
    return "Content-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body;
}

QString ProjectLsp::relPath(const QString &uri) const
{
    const QString path = QUrl(uri).toLocalFile();
    if (m_root.isEmpty() || !path.startsWith(m_root + QLatin1Char('/')))
        return {};
    return path.mid(m_root.size() + 1);
}

FolderContents ProjectLsp::currentContents() const
{
    FolderContents contents = readFolderContents(m_root);
    for (auto it = m_open.constBegin(); it != m_open.constEnd(); ++it) {
        if (it.key() == QLatin1String("template.json"))
            contents.templateJson = it.value();
        else if (it.key().startsWith(QLatin1String("scripts/")) && it.key().endsWith(QLatin1String(".lua")))
            contents.scripts.insert(it.key(), it.value());
        else if (it.key().startsWith(QLatin1String("data/")))
            contents.data.insert(it.key(), it.value());
        else if (!kindForPath(it.key()).isEmpty())
            contents.project.insert(it.key(), it.value());
    }
    return contents;
}

QList<QJsonObject> ProjectLsp::publish()
{
    const FolderContents contents = currentContents();
    FileMap files = contents.project;
    files.insert(QStringLiteral("template.json"), contents.templateJson);
    for (auto it = contents.scripts.constBegin(); it != contents.scripts.constEnd(); ++it)
        files.insert(it.key(), it.value());
    const QList<Problem> problems = lintContents(contents, m_context);
    QMap<QString, QJsonArray> byFile;
    for (const Problem &p : problems) {
        const int line = qMax(0, p.line - 1);
        const int start = qMax(0, p.column - 1);
        const int severity = p.severity == QLatin1String("error") ? 1 : p.severity == QLatin1String("warning") ? 2 : 3;
        byFile[p.file].append(QJsonObject{
            {QStringLiteral("range"),
             QJsonObject{{QStringLiteral("start"), QJsonObject{{QStringLiteral("line"), line}, {QStringLiteral("character"), start}}},
                         {QStringLiteral("end"),
                          QJsonObject{{QStringLiteral("line"), line},
                                      {QStringLiteral("character"), qMax(start + 1, lineLength(files.value(p.file), line))}}}}},
            {QStringLiteral("severity"), severity},
            {QStringLiteral("source"), QStringLiteral("drift")},
            {QStringLiteral("message"), p.message},
        });
    }
    QList<QJsonObject> out;
    const auto send = [&](const QString &file, const QJsonArray &diagnostics) {
        out.append({{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                    {QStringLiteral("method"), QStringLiteral("textDocument/publishDiagnostics")},
                    {QStringLiteral("params"),
                     QJsonObject{{QStringLiteral("uri"), QUrl::fromLocalFile(QDir(m_root).filePath(file)).toString()},
                                 {QStringLiteral("diagnostics"), diagnostics}}}});
    };
    QSet<QString> now;
    for (auto it = byFile.constBegin(); it != byFile.constEnd(); ++it) {
        send(it.key(), it.value());
        now.insert(it.key());
    }
    for (const QString &gone : std::as_const(m_published)) {
        if (!now.contains(gone))
            send(gone, QJsonArray());
    }
    m_published = now;
    return out;
}

bool ProjectLsp::keyAt(const QByteArray &text, int line, int character, KeyAt *out)
{
    int offset = 0;
    for (int l = 0; l < line; ++l) {
        const int next = text.indexOf('\n', offset);
        if (next < 0)
            return false;
        offset = next + 1;
    }
    offset += character;

    struct Frame
    {
        bool object = false;
        bool expectKey = false;
        QString key;
    };
    QList<Frame> stack;
    for (int i = 0; i < text.size(); ++i) {
        const char c = text.at(i);
        if (c == '{') {
            stack.append({true, true, {}});
        } else if (c == '[') {
            stack.append({false, false, {}});
        } else if (c == '}' || c == ']') {
            if (!stack.isEmpty())
                stack.removeLast();
        } else if (c == ',') {
            if (!stack.isEmpty() && stack.last().object)
                stack.last().expectKey = true;
        } else if (c == '"') {
            int end = i + 1;
            while (end < text.size() && text.at(end) != '"')
                end += text.at(end) == '\\' ? 2 : 1;
            const QString s = QString::fromUtf8(text.mid(i + 1, end - i - 1));
            if (!stack.isEmpty() && stack.last().object && stack.last().expectKey) {
                stack.last().key = s;
                stack.last().expectKey = false;
                if (offset >= i && offset <= end) {
                    out->key = s;
                    out->path.clear();
                    for (int f = 0; f + 1 < stack.size(); ++f) {
                        if (stack.at(f).object)
                            out->path.append(stack.at(f).key);
                    }
                    return true;
                }
            } else if (offset >= i && offset <= end) {
                return false;
            }
            i = end;
        }
    }
    return false;
}

QString ProjectLsp::hoverText(const QString &relPath, const KeyAt &at)
{
    QString kindName = kindForPath(relPath);
    if (kindName == QLatin1String("track") && at.path == QStringList{QStringLiteral("clips")})
        kindName = QStringLiteral("clip");
    else if (!at.path.isEmpty())
        return {};
    const KindDesc *kind = kindDescription(kindName);
    const FieldDesc *field = kind ? kind->field(at.key) : nullptr;
    if (!field)
        return {};
    QString text = QStringLiteral("**%1** (%2)\n\n%3").arg(field->key, field->type, field->doc);
    if (!field->enumValues.isEmpty())
        text += QStringLiteral("\n\nOne of: %1").arg(field->enumValues.join(QStringLiteral(", ")));
    return text;
}

QList<QJsonObject> ProjectLsp::handle(const QJsonObject &message)
{
    const QString method = message.value(QStringLiteral("method")).toString();
    const QJsonValue id = message.value(QStringLiteral("id"));
    const QJsonObject params = message.value(QStringLiteral("params")).toObject();

    if (method == QLatin1String("initialize")) {
        const QString root = QUrl(params.value(QStringLiteral("rootUri")).toString()).toLocalFile();
        m_root = QDir::cleanPath(root);
        QJsonObject caps{
            {QStringLiteral("textDocumentSync"), 1},
            {QStringLiteral("hoverProvider"), true},
        };
        return {response(id, QJsonObject{{QStringLiteral("capabilities"), caps},
                                         {QStringLiteral("serverInfo"), QJsonObject{{QStringLiteral("name"), QStringLiteral("drift")}}}})};
    }
    if (method == QLatin1String("initialized"))
        return publish();
    if (method == QLatin1String("shutdown")) {
        m_shutdown = true;
        return {response(id, QJsonValue::Null)};
    }
    if (method == QLatin1String("exit")) {
        m_exit = true;
        return {};
    }
    if (method == QLatin1String("textDocument/didOpen") || method == QLatin1String("textDocument/didChange")) {
        const QJsonObject doc = params.value(QStringLiteral("textDocument")).toObject();
        const QString rel = relPath(doc.value(QStringLiteral("uri")).toString());
        if (rel.isEmpty())
            return {};
        QString text = doc.value(QStringLiteral("text")).toString();
        const QJsonArray changes = params.value(QStringLiteral("contentChanges")).toArray();
        if (!changes.isEmpty())
            text = changes.last().toObject().value(QStringLiteral("text")).toString();
        m_open.insert(rel, text.toUtf8());
        return publish();
    }
    if (method == QLatin1String("textDocument/didClose")) {
        const QString rel = relPath(params.value(QStringLiteral("textDocument")).toObject().value(QStringLiteral("uri")).toString());
        m_open.remove(rel);
        return publish();
    }
    if (method == QLatin1String("textDocument/didSave"))
        return publish();
    if (method == QLatin1String("textDocument/hover")) {
        const QString rel = relPath(params.value(QStringLiteral("textDocument")).toObject().value(QStringLiteral("uri")).toString());
        const QJsonObject pos = params.value(QStringLiteral("position")).toObject();
        QByteArray text = m_open.value(rel);
        if (text.isNull()) {
            QFile file(QDir(m_root).filePath(rel));
            if (file.open(QIODevice::ReadOnly))
                text = file.readAll();
        }
        KeyAt at;
        if (!rel.isEmpty() && keyAt(text, pos.value(QStringLiteral("line")).toInt(),
                                    pos.value(QStringLiteral("character")).toInt(), &at)) {
            const QString md = hoverText(rel, at);
            if (!md.isEmpty())
                return {response(id, QJsonObject{{QStringLiteral("contents"),
                                                  QJsonObject{{QStringLiteral("kind"), QStringLiteral("markdown")},
                                                              {QStringLiteral("value"), md}}}})};
        }
        return {response(id, QJsonValue::Null)};
    }
    if (id.isUndefined())
        return {};
    return {response(id, QJsonValue::Null)};
}

int runLspStdio(const LintContext &context)
{
#ifdef Q_OS_WIN
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    ProjectLsp server(context);
    const auto readExactly = [](int n) {
        QByteArray out;
        while (out.size() < n) {
            char buffer[4096];
            const size_t want = qMin<size_t>(sizeof buffer, size_t(n - out.size()));
            const size_t got = std::fread(buffer, 1, want, stdin);
            if (got == 0)
                break;
            out.append(buffer, int(got));
        }
        return out;
    };
    while (!server.exiting()) {
        int length = -1;
        QByteArray header;
        int c;
        while ((c = std::fgetc(stdin)) != EOF) {
            header.append(char(c));
            if (header.endsWith("\r\n\r\n"))
                break;
        }
        if (c == EOF)
            return server.exitCode();
        for (const QByteArray &line : header.split('\n')) {
            if (line.toLower().startsWith("content-length:"))
                length = line.mid(15).trimmed().toInt();
        }
        if (length < 0)
            continue;
        const QByteArray body = readExactly(length);
        const QJsonObject message = QJsonDocument::fromJson(body).object();
        for (const QJsonObject &reply : server.handle(message)) {
            const QByteArray bytes = ProjectLsp::frame(reply);
            std::fwrite(bytes.constData(), 1, size_t(bytes.size()), stdout);
            std::fflush(stdout);
        }
    }
    return server.exitCode();
}

} // namespace drift::projdir
