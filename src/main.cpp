#include "engine/AudioFileWriter.h"
#include "engine/EmojiCatalog.h"
#include "engine/FontCatalog.h"
#include "engine/ReverseProxyCache.h"
#ifndef Q_OS_ANDROID
#include "mcp/McpStdio.h"
#endif
#include "models/AddonManager.h"
#include "models/AppController.h"
#include "models/AssetLibrary.h"
#include "models/EditorState.h"
#include "models/FileDialogs.h"
#include "models/Haptics.h"
#include "models/LayoutStore.h"
#include "models/UpdateChecker.h"
#include "engine/VaapiZeroCopy.h"
#include "ClipPreviewImageProvider.h"
#include "DriftImageProvider.h"
#include "MulticamImageProvider.h"
#include "SegmentImageProvider.h"
#include "TextStylePreviewImageProvider.h"
#include "preview/PreviewItem.h"

// QApplication (not QGuiApplication) is required so QFileDialog can use the
// native platform file picker, which routes through xdg-desktop-portal.
#include <QApplication>
#include <QCoreApplication>
#include <QEvent>
#include <QFileOpenEvent>
#include <QIcon>
#include <QImageReader>
#include <QLoggingCategory>
#include <QMessageBox>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QQmlApplicationEngine>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QSurfaceFormat>
#include <QtQml/qqml.h>
#include <QFile>

#ifdef Q_OS_ANDROID
#include "core/Project.h"
#include "engine/FrameCompositor.h"

#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#endif

extern "C" {
#include <libavutil/log.h>
}

namespace {

bool verboseLoggingRequested(int argc, char *argv[])
{
    if (qEnvironmentVariableIntValue("DRIFT_VERBOSE") != 0)
        return true;
    for (int i = 1; i < argc; ++i) {
        if (qstrcmp(argv[i], "--verbose") == 0)
            return true;
    }
    return false;
}

// Qt only writes messages to stderr when it thinks a console is attached — a
// controlling TTY on Unix, a console window on Windows. Everywhere else it hands
// them to the platform's own sink instead: journald on Linux, OutputDebugString on
// Windows. So `drift > log.txt 2>&1`, which is exactly what someone does to collect
// a log for a bug report, produced an empty file and every qWarning went to the
// journal unnoticed. Send them to stderr always.
//
// Not on Android: there the platform sink is logcat, which *is* the way to read an
// Android app's log, and a GUI app's stderr goes to /dev/null. Forcing it there
// would throw the logs away rather than redirect them.
//
// QT_LOGGING_TO_CONSOLE would also work but is deprecated in Qt 6 and prints a
// warning about itself on every launch. Anything the user set already wins.
void forceStderrLogging()
{
#ifndef Q_OS_ANDROID
    if (qEnvironmentVariableIsEmpty("QT_FORCE_STDERR_LOGGING")
        && qEnvironmentVariableIsEmpty("QT_ASSUME_STDERR_HAS_CONSOLE")
        && qEnvironmentVariableIsEmpty("QT_LOGGING_TO_CONSOLE")) {
        qputenv("QT_FORCE_STDERR_LOGGING", "1");
    }
#endif
}

// FFmpeg logs at INFO and Qt prints every qDebug/qInfo, which buries the failures worth acting on
// under per-frame filtergraph chatter. qWarning is this codebase's failure channel, so it stays on
// either way. QT_LOGGING_RULES is applied after these (EnvironmentRules outrank ApiRules), so it
// still overrides them.
void applyLogLevel(bool verbose)
{
    QLoggingCategory::setFilterRules(verbose
                                         ? QStringLiteral("*.debug=true\n"
                                                          "*.info=true\n"
                                                          "qt.*.debug=false")
                                         : QStringLiteral("*.debug=false\n"
                                                          "*.info=false"));
    av_log_set_level(verbose ? AV_LOG_VERBOSE : AV_LOG_ERROR);
}

class FileOpenFilter : public QObject
{
public:
    explicit FileOpenFilter(AppController *controller, QObject *parent = nullptr)
        : QObject(parent)
        , m_controller(controller)
    {
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::FileOpen) {
            const auto *open = static_cast<QFileOpenEvent *>(event);
            m_controller->queueExternalProject(open->url());
            return true;
        }
        return QObject::eventFilter(watched, event);
    }

private:
    AppController *m_controller = nullptr;
};

#ifdef Q_OS_ANDROID
// On-device render check. With <AppDataLocation>/selftest.json in place, composite one frame and
// write selftest.png beside it instead of starting the UI. This is tools/renderframe moved onto
// the device: it exercises FFmpeg decode, the GLES offscreen context, the shader translation and
// package discovery with no QML, no preview item and no clock in the way.
bool runSelfTest()
{
    QString dir;
    QString projectPath;
    const QStringList candidates =
        QStandardPaths::standardLocations(QStandardPaths::AppDataLocation);
    for (const QString &candidate : candidates) {
        const QString path = QDir(candidate).filePath(QStringLiteral("selftest.json"));
        if (QFile::exists(path)) {
            dir = candidate;
            projectPath = path;
            break;
        }
    }

    if (projectPath.isEmpty()) {
        qWarning("selftest: no selftest.json in any of: %s",
                 qPrintable(candidates.join(QLatin1String(", "))));
        return false;
    }

    qWarning("selftest: loading %s", qPrintable(projectPath));

    QFile file(projectPath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning("selftest: cannot open %s", qPrintable(projectPath));
        return true;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) {
        qWarning("selftest: not a JSON object");
        return true;
    }

    QString error;
    drift::Project project = drift::Project::fromJson(doc.object(), &error);
    if (!error.isEmpty()) {
        qWarning("selftest: project load failed: %s", qPrintable(error));
        return true;
    }

    const drift::TimeUs timeUs = qEnvironmentVariableIntValue("DRIFT_SELFTEST_TIME_US");

    FrameCompositor compositor;
    compositor.setProject(&project);
    const QImage frame = compositor.compositeAt(timeUs);
    if (frame.isNull()) {
        qWarning("selftest: compositor returned an empty frame at %lld us",
                 static_cast<long long>(timeUs));
        return true;
    }

    const QString outPath = QDir(dir).filePath(QStringLiteral("selftest.png"));
    if (!frame.save(outPath))
        qWarning("selftest: failed to write %s", qPrintable(outPath));
    else
        qWarning("selftest: wrote %s (%dx%d)", qPrintable(outPath), frame.width(), frame.height());

    return true;
}
#endif // Q_OS_ANDROID

#ifndef Q_OS_ANDROID
// Creates a throwaway context in `format`. On success reports what the driver
// actually handed back, which is not always what was asked for.
bool probeOpenGl(const QSurfaceFormat &format, QSurfaceFormat *obtained = nullptr,
                 QString *renderer = nullptr)
{
    QOffscreenSurface surface;
    surface.setFormat(format);
    surface.create();
    if (!surface.isValid())
        return false;

    QOpenGLContext ctx;
    ctx.setFormat(format);
    if (!ctx.create())
        return false;
    if (obtained)
        *obtained = ctx.format();
    if (renderer && ctx.makeCurrent(&surface)) {
        if (QOpenGLFunctions *fn = ctx.functions()) {
            if (const char *name = reinterpret_cast<const char *>(fn->glGetString(GL_RENDERER)))
                *renderer = QString::fromUtf8(name);
        }
        ctx.doneCurrent();
    }
    return true;
}

// Qt Quick asks for QSurfaceFormat::defaultFormat() and calls qFatal() when it
// cannot have it, so on a driver below OpenGL 3.3 Drift aborts before there is a
// window to put an error in — the app simply vanishes. Say why first.
void warnIfNoOpenGl()
{
    // Probe exactly what Qt Quick will ask for — and check what came *back*, not
    // just that creation succeeded. Both WGL and GLX quietly clamp the request to
    // whatever the driver can do rather than failing it, so a machine that tops out
    // at 3.0 hands back a valid 3.0 context and only the version reveals it. That
    // clamp is the whole of issue #139: Qt Quick is content, and the compositor's
    // 3.3 floor is what breaks.
    auto atLeast33 = [](const QSurfaceFormat &f) {
        return f.majorVersion() > 3 || (f.majorVersion() == 3 && f.minorVersion() >= 3);
    };

    QSurfaceFormat obtained;
    QString renderer;
    bool haveGl = probeOpenGl(QSurfaceFormat::defaultFormat(), &obtained, &renderer);
    if (haveGl && atLeast33(obtained))
        return;

    // The 3.3 request did not give us 3.3. The two platforms disagree about how it
    // fails — WGL clamps and hands back a valid older context, GLX refuses outright
    // — so ask again for the bare minimum. That is what reveals the driver's real
    // ceiling, and it is the difference between "your driver is too old" and "you
    // have no driver", which are very different things to tell someone.
    if (!haveGl)
        haveGl = probeOpenGl(QSurfaceFormat(), &obtained, &renderer);

    const QString driver =
        renderer.isEmpty() ? QCoreApplication::translate("main", "unknown") : renderer;

    QString title;
    QString body;
    if (!haveGl) {
        title = QCoreApplication::translate("main", "No OpenGL driver");
        body = QCoreApplication::translate(
            "main",
            "Drift could not create an OpenGL context, so it cannot draw its interface "
            "or render the preview.\n\nInstall or update your graphics driver.");
    } else if (atLeast33(obtained)) {
        // New enough, so it is the 3.3 *core profile* that could not be had — a
        // driver or session quirk, not old hardware. Do not tell someone their GPU
        // is too old when it is not.
        title = QCoreApplication::translate("main", "OpenGL context unavailable");
        body = QCoreApplication::translate(
                   "main",
                   "Drift could not create an OpenGL 3.3 core profile context, though "
                   "this driver reports OpenGL %1.%2 (%3).\n\nThe video preview cannot "
                   "render. Updating your graphics driver may help.")
                   .arg(obtained.majorVersion())
                   .arg(obtained.minorVersion())
                   .arg(driver);
    } else {
        title = QCoreApplication::translate("main", "Graphics driver is too old");
        // Deliberately does not promise what happens next: below 3.3 the preview
        // cannot render on any platform, and on some Drift cannot start at all.
        body = QCoreApplication::translate(
                   "main",
                   "Drift needs OpenGL 3.3, but this graphics driver only provides "
                   "OpenGL %1.%2 (%3).\n\nThe video preview cannot render, and Drift may "
                   "not start at all. Update your graphics driver, or run Drift on a "
                   "machine with a newer GPU.")
                   .arg(obtained.majorVersion())
                   .arg(obtained.minorVersion())
                   .arg(driver);
    }

    // Also to the log: the dialog cannot be read from a terminal or a bug report.
    qCritical("%s: %s", qUtf8Printable(title), qUtf8Printable(body));
    QMessageBox::critical(nullptr, title, body);
}
#endif

} // namespace

int main(int argc, char *argv[])
{
    forceStderrLogging();
    applyLogLevel(verboseLoggingRequested(argc, argv));

#ifndef Q_OS_ANDROID
    for (int i = 1; i < argc; ++i) {
        if (qstrcmp(argv[i], "--mcp-stdio") == 0) {
            QCoreApplication app(argc, argv);
            QCoreApplication::setApplicationName("CutWire Drift");
            QCoreApplication::setOrganizationName("CutWire Drift");
            return drift::mcp::runStdioAttach();
        }
    }
#endif

#ifdef Q_OS_ANDROID
    // Android is GLES-only; the desktop 3.3 core profile the engine asks for cannot be created
    // here at all. Both contexts that matter — the Qt Quick scene graph's and the compositor's
    // offscreen one in GlRuntime — must agree on the version before they can share textures.
    QSurfaceFormat androidFormat;
    androidFormat.setRenderableType(QSurfaceFormat::OpenGLES);
    androidFormat.setVersion(3, 0);
    androidFormat.setDepthBufferSize(0);
    androidFormat.setStencilBufferSize(0);
    QSurfaceFormat::setDefaultFormat(androidFormat);
#else
    // On NVIDIA/Wayland, leaving the API unspecified makes EGL interpret 3.3
    // as an invalid GLES version and fail with EGL_BAD_MATCH. Start from the
    // default format to retain the platform-selected window-buffer attributes.
    QSurfaceFormat format = QSurfaceFormat::defaultFormat();
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    QSurfaceFormat::setDefaultFormat(format);
#endif

    // Keep Qt Quick on OpenGL and create its global share context before the
    // application, enabling zero-copy texture handoff from the compositor.
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);

#ifdef Q_OS_WIN
    // DirectWrite mis-maps glyphs in the qrc-embedded Inter used by Theme.fontFamily
    // (neighbouring letters, stray diacritics). FreeType renders the same file correctly.
    // An explicit QT_QPA_PLATFORM from the environment still wins.
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "windows:fontengine=freetype");

    // Before allowing desktop OpenGL, Qt looks the GPU up in a blacklist keyed on
    // the vendor and device id it gets from Direct3D 9. When that probe fails the
    // ids come back 0x0000, which matches the list's "Standard VGA" entry, and Qt
    // silently loads opengl32sw.dll instead — Mesa llvmpipe, OpenGL 3.0 at best.
    // Qt Quick runs on 3.0, so the window looks fine while the compositor's 3.3
    // floor fails and the preview stays black on cards that would have run it.
    // The list also still bans a set of pre-2015 Intel parts outright. Skip the id
    // lookup and let context creation decide: Qt's own testDesktopGL() still runs,
    // so a machine that genuinely cannot do desktop GL still falls back.
    // Anything the user set — including a custom buglist — still wins.
    if (qEnvironmentVariableIsEmpty("QT_NO_OPENGL_BUGLIST")
        && qEnvironmentVariableIsEmpty("QT_OPENGL")
        && qEnvironmentVariableIsEmpty("QT_OPENGL_BUGLIST")) {
        qputenv("QT_NO_OPENGL_BUGLIST", "1");
    }
#endif

    // Names must be set before reading QSettings for ui/scale, and QT_SCALE_FACTOR
    // must be in the environment before QApplication is constructed.
    QCoreApplication::setApplicationName("CutWire Drift");
    QCoreApplication::setOrganizationName("CutWire Drift");
    AppController::applyStoredUiScale();
    // Qt's xcb plugin defaults to GLX, so eglGetCurrentDisplay() is null and
    // zero-copy sticky-disables. Only force EGL when the user opted in — default
    // X11 behaviour stays byte-identical. An explicit QT_XCB_GL_INTEGRATION still wins.
    drift::applyVaapiZeroCopyXcbEgl();

    QApplication app(argc, argv);
    if (!QImageReader::supportedImageFormats().contains("svg")) {
        qWarning("SVG icons will not display: Qt's SVG image plugin is missing or built "
                 "for a different Qt version than this binary. Install a matching qt6-svg "
                 "(same version as qt6-base).");
    }
    // Associates the window with the installed .desktop entry so shells (notably
    // Wayland) can find its icon and app metadata.
    QGuiApplication::setDesktopFileName(QStringLiteral("org.cutwire.Drift"));
    // Title bar / taskbar icon when no desktop entry is available (Windows, and
    // Linux runs from the build tree). The .exe still needs the Windows .rc icon
    // for Explorer and pinned-taskbar identity.
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/app/drift.png")));

    // qsTr/tr resolve when the QML engine loads, so translators must be installed first.
    // Protocol strings under src/mcp/ are excluded from the catalog; they stay English.
    AppController::installUiTranslators();

#ifndef Q_OS_ANDROID
    // The whole UI is a Qt Quick scene graph on OpenGL, so with no OpenGL at all
    // there is no window to put an error in — the app would just appear not to
    // start. That is reachable on Windows, where the packages no longer carry Qt's
    // software rasterizer as a fallback. Deliberately narrow: this asks for no
    // particular version, so it fires only when a context cannot be created at
    // all. A driver that is merely too old still starts, and the preview explains
    // itself in the panel instead.
    warnIfNoOpenGl();
#endif

    // Registering the bundled fonts needs a QGuiApplication, and must happen before the compositor
    // thread starts touching QFontDatabase.
    reloadFontCatalog();
    reloadEmojiCatalog();

    // Noise-removal A/B snippets are scratch. Anything still here is from a previous session that
    // did not get to clean up after itself.
    drift::sweepDenoisePreviews();

#ifdef Q_OS_ANDROID
    // Every content:// write is staged through <cache>/staged and unlinked once the copy into the
    // document finishes, so a file still here is debris from an encode that was killed.
    {
        QDir staged(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                    + QStringLiteral("/staged"));
        const QFileInfoList leftovers = staged.entryInfoList(QDir::Files);
        for (const QFileInfo &file : leftovers)
            QFile::remove(file.absoluteFilePath());
    }

    qWarning("app data locations: %s",
             qPrintable(QStandardPaths::standardLocations(QStandardPaths::AppDataLocation)
                            .join(QLatin1String(", "))));
    if (runSelfTest())
        return 0;
#endif

    // Reversed proxies are a pure cache: dropping one only costs the clip its smooth playback, so
    // they are pruned to a budget rather than kept forever the way mattes are.
    drift::ReverseProxyCache::instance().load();
    drift::ReverseProxyCache::instance().sweep(drift::ReverseProxyCache::kDefaultMaxBytes);

    qmlRegisterType<PreviewItem>("Drift", 1, 0, "PreviewItem");

    static AssetLibrary assetLibrary;
    static EditorState editorState(&assetLibrary);
    static FileDialogs fileDialogs;
    static AddonManager addonManager;
    static UpdateChecker updateChecker;
    static LayoutStore layoutStore;
    static drift::Haptics haptics;
    editorState.setAddonManager(&addonManager);
    qmlRegisterSingletonInstance("Drift", 1, 0, "AssetLibrary", &assetLibrary);
    qmlRegisterSingletonInstance("Drift", 1, 0, "BinFolderModel", editorState.binFolderModel());
    qmlRegisterSingletonInstance("Drift", 1, 0, "EditorState", &editorState);
    qmlRegisterSingletonInstance("Drift", 1, 0, "AppController", &editorState);
    qmlRegisterSingletonInstance("Drift", 1, 0, "FileDialogs", &fileDialogs);
    qmlRegisterSingletonInstance("Drift", 1, 0, "Addons", &addonManager);
    qmlRegisterSingletonInstance("Drift", 1, 0, "Updates", &updateChecker);
    qmlRegisterSingletonInstance("Drift", 1, 0, "LayoutMemory", &layoutStore);
    qmlRegisterSingletonInstance("Drift", 1, 0, "Haptics", &haptics);

    app.installEventFilter(new FileOpenFilter(&editorState, &app));
    editorState.queueExternalProject(
        AppController::startupProjectUrlFromArguments(app.arguments()));

    QQmlApplicationEngine engine;
    QObject::connect(&editorState, &AppController::uiLanguageChanged,
                     &engine, &QQmlEngine::retranslate);
    engine.addImageProvider(QStringLiteral("drift"), new DriftImageProvider());
    engine.addImageProvider(QStringLiteral("segment"), new SegmentImageProvider());
    engine.addImageProvider(QStringLiteral("clippreview"), new ClipPreviewImageProvider());
    engine.addImageProvider(QStringLiteral("multicam"), new MulticamImageProvider());
    engine.addImageProvider(QStringLiteral("textstyle"), new TextStylePreviewImageProvider());
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QGuiApplication::exit(-1); }, Qt::QueuedConnection);
    // Main.qml is the desktop layout. AndroidMain.qml is the touch entry point; the desktop tree
    // stays compiled so the touch port can reuse leaf components.
#ifdef Q_OS_ANDROID
    engine.loadFromModule("Drift", "AndroidMain");
#else
    engine.loadFromModule("Drift", "Main");
#endif

    return app.exec();
}
