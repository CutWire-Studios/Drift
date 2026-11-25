#pragma once

#include <QByteArray>
#include <QString>
#include <QVariantMap>

class QCoreApplication;

// The headless modes beyond MCP. Parsed apart from HeadlessApp's own flags so the two sets can grow
// independently; runHeadless asks parseFarmCommand() first and only falls through to MCP when the
// mode is Mcp.
namespace drift::farm {

struct FarmCommand
{
    enum class Mode { Mcp, ListGpus, Render, Worker, FarmCoordinator, FarmWorker };

    Mode mode = Mode::Mcp;
    QString gpu;

    // Render
    QString project;
    QString output;
    QVariantMap settings;

    // FarmCoordinator / FarmWorker
    int port = 4800;
    QString token;
    QString url;
    QString name;
    int cacheGb = 0; // 0: the Render farm settings page's value
    // Coordinator: render this project on the farm, then exit.
    bool hasRender() const { return !project.isEmpty(); }
};

FarmCommand parseFarmCommand(int argc, char *argv[]);

// Flags of parseFarmCommand that take a value, so another parser can skip the value.
bool isFarmValueFlag(const QByteArray &arg);

// Runs a non-MCP mode to completion and returns the process exit code. The application exists but
// is not yet exec()ing; the GL thread needs this thread's event loop, so work runs on its own
// thread.
// `gpuLine` says which GPU the process rendered on, for a worker to report.
int runFarmCommand(const FarmCommand &command, QCoreApplication &app, const QString &gpuLine);

} // namespace drift::farm
