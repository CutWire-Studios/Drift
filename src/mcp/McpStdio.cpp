#include "mcp/McpStdio.h"
#include "mcp/McpSession.h"

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QTcpSocket>

#include <cstdio>

#ifdef Q_OS_WIN
#include <fcntl.h>
#include <io.h>
#endif

namespace drift::mcp {
namespace {

// The id to echo on a reply. Undefined means there is nobody to reply to — a
// notification, or a batch whose failures cannot be attributed to one request.
QJsonValue requestId(const QByteArray &message)
{
    const QJsonDocument doc = QJsonDocument::fromJson(message);
    if (!doc.isObject())
        return QJsonValue::Undefined;
    return doc.object().value(QStringLiteral("id"));
}

void writeRpcError(const QJsonValue &id, int code, const QString &message)
{
    // JSON-RPC forbids responding to a notification, and a client cannot match an
    // id-less error to anything it is waiting on. stderr already carried the reason.
    if (id.isUndefined())
        return;

    const QJsonObject body{
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("id"), id},
        {QStringLiteral("error"),
         QJsonObject{{QStringLiteral("code"), code}, {QStringLiteral("message"), message}}},
    };
    writeStdioMessage(stdout, QJsonDocument(body).toJson(QJsonDocument::Compact));
}

int httpStatus(const QByteArray &response)
{
    const int nl = response.indexOf('\n');
    const QByteArray line = nl < 0 ? response : response.left(nl);
    const auto parts = line.split(' ');
    return parts.size() >= 2 ? parts.at(1).toInt() : 0;
}

QByteArray postJson(quint16 port, const QString &token, const QByteArray &body, QString *error,
                    int *statusOut)
{
    QTcpSocket socket;
    socket.connectToHost(QStringLiteral("127.0.0.1"), port);
    if (!socket.waitForConnected(2000)) {
        if (error)
            *error = QStringLiteral(
                "Could not connect to Drift. Is the editor open with Agent access enabled?");
        return {};
    }

    QByteArray req;
    req += "POST /mcp HTTP/1.1\r\n";
    req += "Host: 127.0.0.1\r\n";
    req += "Content-Type: application/json\r\n";
    req += "Authorization: Bearer ";
    req += token.toUtf8();
    req += "\r\nContent-Length: ";
    req += QByteArray::number(body.size());
    req += "\r\nConnection: close\r\n\r\n";
    req += body;
    socket.write(req);
    socket.waitForBytesWritten(2000);

    QByteArray response;
    while (socket.waitForReadyRead(15000) || socket.bytesAvailable() > 0)
        response += socket.readAll();

    const int sep = response.indexOf("\r\n\r\n");
    if (sep < 0) {
        if (error)
            *error = QStringLiteral("Empty response from Drift MCP.");
        return {};
    }
    if (statusOut)
        *statusOut = httpStatus(response);
    return response.mid(sep + 4);
}

// The body of a Content-Length framed message, once its header block has been read.
QByteArray readBody(std::FILE *in, int length)
{
    QByteArray body;
    body.resize(length);
    int got = 0;
    while (got < length) {
        const int n = static_cast<int>(
            std::fread(body.data() + got, 1, static_cast<size_t>(length - got), in));
        if (n <= 0)
            break;
        got += n;
    }
    body.truncate(got);
    return body;
}

} // namespace

QByteArray readStdioMessage(std::FILE *in)
{
    int contentLength = -1;
    QByteArray line;
    for (;;) {
        const int ch = std::fgetc(in);
        if (ch != EOF && ch != '\n') {
            line.append(static_cast<char>(ch));
            continue;
        }
        if (ch == EOF && line.isEmpty())
            return {};

        // A BOM can only lead the stream, but stripping it from every line costs nothing
        // and saves tracking which line is the first. Windows clients do emit them.
        if (line.startsWith("\xEF\xBB\xBF"))
            line.remove(0, 3);
        if (line.endsWith('\r'))
            line.chop(1);

        // Newline-delimited JSON: the line is the whole message. Checked before anything
        // else so a leading blank line or BOM cannot push a valid request into the
        // header-block branch below, where it would wait for a blank line that a
        // newline-delimited client never sends.
        if (line.startsWith('{') || line.startsWith('['))
            return line;

        // A blank line closes a Content-Length header block; anywhere else it is filler.
        if (line.isEmpty() && contentLength > 0)
            return readBody(in, contentLength);

        const int at = line.toLower().indexOf("content-length:");
        if (at >= 0)
            contentLength = line.mid(at + 15).trimmed().toInt();
        // Any other header — Content-Type, say — is ignored.

        if (ch == EOF)
            return {};
        line.clear();
    }
}

void writeStdioMessage(std::FILE *out, const QByteArray &json)
{
    // Recompacting is what enforces the one-line invariant: a newline inside a string
    // comes back escaped, and any indentation collapses.
    const QByteArray line = QJsonDocument::fromJson(json).toJson(QJsonDocument::Compact).trimmed();
    std::fwrite(line.constData(), 1, static_cast<size_t>(line.size()), out);
    std::fputc('\n', out);
    std::fflush(out);
}

int runStdioAttach()
{
#ifdef Q_OS_WIN
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif

    // Say why up front for whoever ran this in a terminal, but keep serving: a client
    // that spawned us before the editor was up, or before Agent access was switched on,
    // gets a working bridge as soon as the user gets there. stderr only — the spec lets
    // nothing but MCP messages onto stdout.
    {
        QString error;
        if (!readSessionFile(nullptr, nullptr, &error))
            fprintf(stderr, "%s\n", qPrintable(error));
    }

    for (;;) {
        const QByteArray message = readStdioMessage(stdin);
        if (message.isEmpty())
            return 0; // stdin closed

        const QJsonValue id = requestId(message);

        // Re-read per request rather than once at startup: the token rotates every time
        // Agent access is toggled, and the old bridge died with HTTP 401 for the rest of
        // its life the first time that happened.
        quint16 port = 0;
        QString token;
        QString error;
        if (!readSessionFile(&port, &token, &error)) {
            fprintf(stderr, "%s\n", qPrintable(error));
            writeRpcError(id, -32000, error);
            continue;
        }

        QString postError;
        int status = 0;
        const QByteArray reply = postJson(port, token, message, &postError, &status);
        if (!postError.isEmpty() && reply.isEmpty()) {
            fprintf(stderr, "%s\n", qPrintable(postError));
            writeRpcError(id, -32000, postError);
            continue;
        }
        if (status > 0 && (status < 200 || status >= 300)) {
            const QString msg = QStringLiteral("Drift MCP HTTP %1").arg(status);
            fprintf(stderr, "%s\n", qPrintable(msg));
            writeRpcError(id, -32000, msg);
            continue;
        }
        // JSON-RPC notifications (no id) must not get a response. The HTTP server
        // answers those with 202 and an empty body — forwarding that would break
        // stdio clients after `notifications/initialized`.
        if (status == 202 || reply.trimmed().isEmpty())
            continue;
        writeStdioMessage(stdout, reply);
    }
}

} // namespace drift::mcp
