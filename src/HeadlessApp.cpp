#include "HeadlessApp.h"

#include "engine/AudioFileWriter.h"
#include "engine/EmojiCatalog.h"
#include "engine/FontCatalog.h"
#include "engine/ReverseProxyCache.h"
#include "mcp/McpServer.h"
#include "mcp/McpSession.h"
#include "mcp/McpStdioServer.h"
#include "models/AddonManager.h"
#include "models/AppController.h"
#include "models/AssetLibrary.h"
#include "models/EditorState.h"

// QApplication rather than QGuiApplication: the model layer is shared with the GUI build
// and this keeps the two paths on one application type. Nothing here opens a window.
#include <QApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QStringList>
#include <QSurfaceFormat>

#include <csignal>
#include <cstdio>
#include <cstdlib>

namespace drift {
namespace {

void onTerminate(int)
{
    QCoreApplication::quit();
}

// Headless drops the window, not the need for a GL context: the compositor still wants
// OpenGL 3.3 core on a QOffscreenSurface. The `offscreen` QPA plugin cannot hand one out
// on a host with no /dev/dri, which is the trap on a server — and GLX and WGL both clamp
// the request to whatever the driver can do rather than failing it, so a machine that
// tops out lower hands back a valid context and only the version reveals it. Report it at
// startup rather than letting the first export fail with nothing to go on.
QString describeOpenGl()
{
    QSurfaceFormat format;
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);

    QOffscreenSurface surface;
    surface.setFormat(format);
    surface.create();

    QOpenGLContext ctx;
    ctx.setFormat(format);
    if (!surface.isValid() || !ctx.create() || !ctx.makeCurrent(&surface)) {
        return QStringLiteral("unavailable — rendering and export will fail. Run under "
                              "`QT_QPA_PLATFORM=xcb xvfb-run -a`, or use an EGL platform "
                              "plugin.");
    }

    const QSurfaceFormat obtained = ctx.format();
    QString renderer;
    if (const auto *name = ctx.functions()->glGetString(GL_RENDERER))
        renderer = QString::fromUtf8(reinterpret_cast<const char *>(name));
    ctx.doneCurrent();

    QString line = QStringLiteral("%1.%2").arg(obtained.majorVersion()).arg(obtained.minorVersion());
    if (!renderer.isEmpty())
        line += QStringLiteral(" (%1)").arg(renderer);
    if (obtained.majorVersion() < 3
        || (obtained.majorVersion() == 3 && obtained.minorVersion() < 3)) {
        line += QStringLiteral(" — below the compositor's 3.3 floor; rendering and export "
                               "will fail");
    }
    return line;
}

// What is running and how to reach it. Goes to whichever stream is not an MCP transport.
void printBanner(std::FILE *out, const QString &glLine, bool stdioServing,
                 const drift::mcp::McpServer *http, const QString &projectPath)
{
    QString text = QStringLiteral("Drift %1 — headless\n").arg(QStringLiteral(DRIFT_VERSION));
    text += QStringLiteral("  platform  %1\n").arg(QGuiApplication::platformName());
    text += QStringLiteral("  opengl    %1\n").arg(glLine);
    text += QStringLiteral("  project   %1\n")
                .arg(projectPath.isEmpty() ? QStringLiteral("(empty timeline)") : projectPath);
    if (stdioServing)
        text += QStringLiteral("  mcp stdio serving on stdin/stdout\n");
    if (http) {
        text += QStringLiteral("  mcp http  %1\n").arg(http->url());
        text += QStringLiteral("  token     %1\n").arg(http->token());
        text += QStringLiteral("  session   %1\n").arg(drift::mcp::sessionFilePath());
    }
    if (!stdioServing && !http)
        text += QStringLiteral("  (no MCP transport requested)\n");

    const QByteArray utf8 = text.toUtf8();
    std::fwrite(utf8.constData(), 1, static_cast<size_t>(utf8.size()), out);
    std::fflush(out);
}

} // namespace

int runHeadless(int argc, char *argv[])
{
    quint16 httpPort = 0;
    QString httpToken;
    bool stdioRequested = false;
    QStringList positional;
    for (int i = 1; i < argc; ++i) {
        const QByteArray arg(argv[i]);
        if (arg == "--mcp-stdio") {
            stdioRequested = true;
        } else if (arg == "--mcp-port" && i + 1 < argc) {
            httpPort = QByteArray(argv[++i]).toUShort();
        } else if (arg == "--mcp-token" && i + 1 < argc) {
            httpToken = QString::fromLocal8Bit(argv[++i]);
        } else if (!arg.startsWith('-')) {
            positional.append(QString::fromLocal8Bit(arg));
        }
    }
    // stdio is the default transport. --mcp-port on its own means HTTP only, so a daemon
    // started with stdin on /dev/null does not quit the instant it reads EOF.
    const bool serveStdio = stdioRequested || httpPort == 0;
    if (httpToken.isEmpty())
        httpToken = qEnvironmentVariable("DRIFT_MCP_TOKEN");

    // Deliberately not QQuickWindow::setGraphicsApi / AA_ShareOpenGLContexts, which the
    // GUI path sets here: with the attribute on and no QQuickWindow to build a share
    // context from, GlRuntime::initGlObjects() refuses to create one of its own and
    // nothing renders at all.
    QCoreApplication::setApplicationName("CutWire Drift");
    QCoreApplication::setOrganizationName("CutWire Drift");

    QApplication app(argc, argv);
    AppController::installUiTranslators();

    // Text and caption clips resolve their faces through these, so they are needed even
    // with nothing on screen.
    reloadFontCatalog();
    reloadEmojiCatalog();
    drift::sweepDenoisePreviews();
    drift::ReverseProxyCache::instance().load();
    drift::ReverseProxyCache::instance().sweep(drift::ReverseProxyCache::kDefaultMaxBytes);

    const QString glLine = describeOpenGl();

    static AssetLibrary assetLibrary;
    static EditorState editorState(&assetLibrary);
    static AddonManager addonManager;
    editorState.setAddonManager(&addonManager);

    // consumeStartupProject() is the hook the QML window normally calls once it is up;
    // here there is no window to wait for. Only positional arguments are considered, so
    // the value of --mcp-port is not mistaken for a project path.
    positional.prepend(app.arguments().value(0));
    editorState.queueExternalProject(AppController::startupProjectUrlFromArguments(positional));
    editorState.consumeStartupProject();

    drift::mcp::McpServer *server = editorState.mcpServer();
    if (httpPort > 0) {
        if (!httpToken.isEmpty())
            server->setToken(httpToken);
        server->setPort(httpPort);
        if (!server->start()) {
            fprintf(stderr, "Could not start the MCP HTTP server: %s\n",
                    qPrintable(server->error()));
            return 1;
        }
    }

    drift::mcp::McpStdioServer stdioServer(server);
    if (serveStdio) {
        // Closing stdin is how a client says it is done. With HTTP also serving, the
        // process is a daemon that outlives any one client, so it keeps running.
        if (httpPort == 0) {
            QObject::connect(&stdioServer, &drift::mcp::McpStdioServer::finished, &app,
                             &QCoreApplication::quit);
        }
        stdioServer.start();
    }

    // Whatever is not carrying MCP gets the banner. When stdio is a transport, stdout may
    // hold nothing but MCP messages — a banner there is precisely what breaks clients.
    printBanner(serveStdio ? stderr : stdout, glLine, serveStdio, httpPort > 0 ? server : nullptr,
                editorState.currentProjectPath());

    // Not async-signal-safe in the strictest reading, but quit() only posts an event and
    // this is the usual shape for a Qt daemon.
    std::signal(SIGINT, onTerminate);
    std::signal(SIGTERM, onTerminate);

    const int code = app.exec();

    // aboutToQuit has run by now, so the recovery file and settings are already on disk.
    // What is left is the stdin reader, parked in a blocking read that nothing can wake
    // portably — on Linux closing the descriptor does not interrupt a read already in
    // flight — and unwinding into exit() from here deadlocks the main thread against the
    // FILE lock that reader holds, which is a Ctrl-C that never returns the shell. Leave
    // directly instead.
    if (stdioServer.isReading())
        std::_Exit(code);
    return code;
}

} // namespace drift
