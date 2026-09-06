#pragma once

#include <QObject>

class QThread;

namespace drift::mcp {
class McpServer;

// Serves MCP on this process's own stdin/stdout, dispatching straight into an in-process
// McpServer — no TCP, no bearer token, no session file. This is what `--headless` hands
// to a client's mcpServers config; McpStdio.cpp's runStdioAttach() is the other end of
// the pipe when the editor is already running with a GUI.
class McpStdioServer : public QObject
{
    Q_OBJECT

public:
    explicit McpStdioServer(McpServer *server, QObject *parent = nullptr);
    ~McpStdioServer() override;

    void start();

    // True while the reader thread is still parked on stdin. Shutdown has to know:
    // nothing can wake a blocking read portably, so the process cannot unwind past it.
    bool isReading() const;

signals:
    // stdin reached EOF — the client that spawned this process is gone.
    void finished();

private:
    void readLoop();

    McpServer *m_server = nullptr;
    QThread *m_thread = nullptr;
};

} // namespace drift::mcp
