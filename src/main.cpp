#include "engine/AudioFileWriter.h"
#include "engine/EmojiCatalog.h"
#include "engine/FontCatalog.h"
#include "engine/ReverseProxyCache.h"
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
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QSurfaceFormat>
#include <QtQml/qqml.h>

int main(int argc, char *argv[])
{
#ifdef Q_OS_MACOS
    // macOS has no compatibility profile: a context is either legacy 2.1 or core 3.2+. Qt's
    // default format asks for the former while GlRuntime asks for 3.3 core, and NSOpenGLContext
    // refuses to share between the two. The share is then dropped with only a warning, and the
    // compositor's texture is not valid in the scene graph's context, so the preview stays black.
    // Matching the default to what GlRuntime asks for keeps the two shareable. X11 and WGL share
    // across differing formats, so this is not needed there.
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
