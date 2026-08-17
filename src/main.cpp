#include "engine/AudioFileWriter.h"
#include "engine/EmojiCatalog.h"
#include "engine/FontCatalog.h"
#include "engine/ReverseProxyCache.h"
#include "mcp/McpStdio.h"
#include "models/AddonManager.h"
#include "models/AppController.h"
#include "models/AssetLibrary.h"
#include "models/EditorState.h"
#include "models/FileDialogs.h"
#include "models/UpdateChecker.h"
#include "ClipPreviewImageProvider.h"
#include "DriftImageProvider.h"
#include "SegmentImageProvider.h"
#include "TextStylePreviewImageProvider.h"
#include "preview/PreviewItem.h"

// QApplication (not QGuiApplication) is required so QFileDialog can use the
// native platform file picker, which routes through xdg-desktop-portal.
#include <QApplication>
#include <QCoreApplication>
#include <QIcon>
#include <QLibraryInfo>
#include <QLocale>
#include <QLoggingCategory>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QSurfaceFormat>
#include <QTranslator>
#include <QtQml/qqml.h>

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

} // namespace

int main(int argc, char *argv[])
{
    applyLogLevel(verboseLoggingRequested(argc, argv));

    for (int i = 1; i < argc; ++i) {
        if (qstrcmp(argv[i], "--mcp-stdio") == 0) {
            QCoreApplication app(argc, argv);
            QCoreApplication::setApplicationName("CutWire Drift");
            QCoreApplication::setOrganizationName("CutWire Drift");
            return drift::mcp::runStdioAttach();
        }
    }

#ifdef Q_OS_MACOS
    // NSOpenGLContext will not share between the legacy 2.1 context Qt defaults to and the 3.3
    // core one GlRuntime creates, and drops the share with only a warning, leaving the preview
    // black. Match the default so both are core 3.3. X11 and WGL share across differing formats.
    QSurfaceFormat macFormat = QSurfaceFormat::defaultFormat();
    macFormat.setVersion(3, 3);
    macFormat.setProfile(QSurfaceFormat::CoreProfile);
    QSurfaceFormat::setDefaultFormat(macFormat);
#endif

    // The compositor renders into an FBO on its own GL context and hands the
    // texture to the scene graph without a readback. That requires both contexts
    // to share objects, and the scene graph to actually be on OpenGL. Both must
    // be set before the QApplication is constructed.
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);

    QApplication app(argc, argv);
    QApplication::setApplicationName("CutWire Drift");
    QApplication::setOrganizationName("CutWire Drift");
    // Associates the window with the installed .desktop entry so shells (notably
    // Wayland) can find its icon and app metadata.
    QGuiApplication::setDesktopFileName(QStringLiteral("org.cutwire.Drift"));
    // Title bar / taskbar icon when no desktop entry is available (Windows, and
    // Linux runs from the build tree). The .exe still needs the Windows .rc icon
    // for Explorer and pinned-taskbar identity.
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/app/drift.png")));

    // qsTr/tr resolve when the QML engine loads, so translators must be installed first.
    // Protocol strings under src/mcp/ are excluded from the catalog; they stay English.
    QTranslator appTranslator;
    if (appTranslator.load(QLocale(), QStringLiteral("drift"), QStringLiteral("_"),
                           QStringLiteral(":/i18n"))) {
        app.installTranslator(&appTranslator);
    }
    QTranslator qtTranslator;
    if (qtTranslator.load(QLocale(), QStringLiteral("qtbase"), QStringLiteral("_"),
                          QLibraryInfo::path(QLibraryInfo::TranslationsPath))) {
        app.installTranslator(&qtTranslator);
    }

    // Registering the bundled fonts needs a QGuiApplication, and must happen before the compositor
    // thread starts touching QFontDatabase.
    reloadFontCatalog();
    reloadEmojiCatalog();

    // Noise-removal A/B snippets are scratch. Anything still here is from a previous session that
    // did not get to clean up after itself.
    drift::sweepDenoisePreviews();

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
    qmlRegisterSingletonInstance("Drift", 1, 0, "AssetLibrary", &assetLibrary);
    qmlRegisterSingletonInstance("Drift", 1, 0, "EditorState", &editorState);
    qmlRegisterSingletonInstance("Drift", 1, 0, "AppController", &editorState);
    qmlRegisterSingletonInstance("Drift", 1, 0, "FileDialogs", &fileDialogs);
    qmlRegisterSingletonInstance("Drift", 1, 0, "Addons", &addonManager);
    qmlRegisterSingletonInstance("Drift", 1, 0, "Updates", &updateChecker);

    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("drift"), new DriftImageProvider());
    engine.addImageProvider(QStringLiteral("segment"), new SegmentImageProvider());
    engine.addImageProvider(QStringLiteral("clippreview"), new ClipPreviewImageProvider());
    engine.addImageProvider(QStringLiteral("textstyle"), new TextStylePreviewImageProvider());
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QGuiApplication::exit(-1); }, Qt::QueuedConnection);
    engine.loadFromModule("Drift", "Main");

    return app.exec();
}
