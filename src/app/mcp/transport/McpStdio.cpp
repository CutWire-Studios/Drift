#include "app/mcp/transport/McpStdio.h"
#include "app/mcp/protocol/McpSession.h"
#include "app/mcp/transport/McpBridge.h"

#include <QByteArray>
#include <QJsonDocument>

#include <cstdio>

#ifdef Q_OS_WIN
#include <fcntl.h>
#include <io.h>
#endif

namespace drift::mcp {
namespace {

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

int runStdioAttach(qint64 pinnedPid)
{
#ifdef Q_OS_WIN
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif

    StdioBridge bridge(pinnedPid);

    // Say why up front for whoever ran this in a terminal, but keep serving: a client
    // that spawned us before the editor was up, or before Agent access was switched on,
    // gets a working bridge as soon as the user gets there. stderr only — the spec lets
    // nothing but MCP messages onto stdout.
    {
        SessionInfo probe = bridge.attached();
        QString error;
        if (!resolveAttachedSession(&probe, &error))
            fprintf(stderr, "%s\n", qPrintable(error));
    }

    for (;;) {
        const QByteArray message = readStdioMessage(stdin);
        if (message.isEmpty())
            return 0; // stdin closed

        // The session registry is re-read per request rather than once at startup: the
        // token rotates every time Agent access is toggled, and the old bridge died with
        // HTTP 401 for the rest of its life the first time that happened.
        const QByteArray reply = bridge.handle(message);
        if (!reply.isEmpty())
            writeStdioMessage(stdout, reply);
    }
}

} // namespace drift::mcp
