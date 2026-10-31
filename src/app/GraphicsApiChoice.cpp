#include "app/GraphicsApiChoice.h"

#include <QCoreApplication>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QSettings>

namespace drift::graphics {

namespace {

const QString kOpenGl = QStringLiteral("opengl");
const QString kChoiceKey = QStringLiteral("ui/graphicsApi");
const QString kPendingKey = QStringLiteral("ui/graphicsApiPending");
const QString kRevertedKey = QStringLiteral("ui/graphicsApiRevertedFrom");

QString g_revertedFrom;

QSGRendererInterface::GraphicsApi toQt(const QString &id)
{
    if (id == QLatin1String("d3d11"))
        return QSGRendererInterface::Direct3D11;
    if (id == QLatin1String("d3d12"))
        return QSGRendererInterface::Direct3D12;
    if (id == QLatin1String("vulkan"))
        return QSGRendererInterface::Vulkan;
    if (id == QLatin1String("metal"))
        return QSGRendererInterface::Metal;
    return QSGRendererInterface::OpenGL;
}

QString argumentApi(int argc, char *argv[])
{
    const QByteArray prefix = "--graphics-api=";
    for (int i = 1; i < argc; ++i) {
        const QByteArray arg(argv[i]);
        if (arg.startsWith(prefix))
            return QString::fromLatin1(arg.mid(prefix.size())).toLower();
        if (arg == "--graphics-api" && i + 1 < argc)
            return QString::fromLatin1(argv[i + 1]).toLower();
    }
    return {};
}

} // namespace

QStringList availableApis()
{
    QStringList apis{kOpenGl};
#if defined(Q_OS_WIN)
    apis << QStringLiteral("d3d11") << QStringLiteral("d3d12");
#elif defined(Q_OS_MACOS)
    apis << QStringLiteral("metal");
#endif
#if QT_CONFIG(vulkan) && !defined(Q_OS_ANDROID) && !defined(Q_OS_MACOS) && !defined(Q_OS_WASM)
    apis << QStringLiteral("vulkan");
#endif
    return apis;
}

QString storedApi()
{
    const QString id = QSettings().value(kChoiceKey, kOpenGl).toString();
    return availableApis().contains(id) ? id : kOpenGl;
}

void storeApi(const QString &id)
{
    QSettings settings;
    settings.setValue(kChoiceKey, availableApis().contains(id) ? id : kOpenGl);
    settings.remove(kPendingKey);
}

bool applyAtStartup(int argc, char *argv[])
{
    QSettings settings;
    g_revertedFrom = settings.value(kRevertedKey).toString();
    settings.remove(kRevertedKey);

    QString id = argumentApi(argc, argv);
    const bool fromSettings = id.isEmpty();
    if (fromSettings) {
        id = storedApi();
        if (id != kOpenGl && settings.value(kPendingKey).toBool()) {
            qWarning("The previous launch on graphics API '%s' never drew a frame; using OpenGL",
                     qPrintable(id));
            g_revertedFrom = id;
            id = kOpenGl;
            settings.setValue(kChoiceKey, kOpenGl);
        }
        settings.remove(kPendingKey);
    } else if (!availableApis().contains(id)) {
        qWarning("--graphics-api: '%s' is not available here (%s); using OpenGL", qPrintable(id),
                 qPrintable(availableApis().join(QStringLiteral(", "))));
        id = kOpenGl;
    }

    QQuickWindow::setGraphicsApi(toQt(id));
    // The global share context is what lets the preview wrap the compositor's texture. Without
    // it GlRuntime runs an unshared context and the compositor publishes readbacks instead.
    if (id == kOpenGl)
        QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);

    const bool guarded = fromSettings && id != kOpenGl;
    if (guarded) {
        // Synced now: a crash during startup must still find the mark on the next launch.
        settings.setValue(kPendingKey, true);
        settings.sync();
    }
    return guarded;
}

void markStarted()
{
    QSettings().remove(kPendingKey);
}

void revertAfterFailure()
{
    QSettings settings;
    settings.setValue(kRevertedKey, settings.value(kChoiceKey).toString());
    settings.setValue(kChoiceKey, kOpenGl);
    settings.remove(kPendingKey);
    settings.sync();
}

QString revertedFrom()
{
    return g_revertedFrom;
}

} // namespace drift::graphics
