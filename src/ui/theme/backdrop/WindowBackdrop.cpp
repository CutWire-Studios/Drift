#include "ui/theme/backdrop/WindowBackdrop.h"

#include "ui/theme/ThemeManager.h"

#include <QCoreApplication>
#include <QOperatingSystemVersion>
#include <QPointer>
#include <QQuickWindow>
#include <QSettings>
#include <QtQml/qqml.h>

#include <QtNumeric>

class QWindow;

namespace drift::backdrop {
void applyNative(QWindow *window, const QString &backdrop, bool darkVariant);
}

namespace {

WindowBackdrop *g_instance = nullptr;
bool g_translucencyActive = false;

bool platformAllowsTranslucentWindows()
{
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS) || defined(Q_OS_WASM)
    return false;
#else
    return true;
#endif
}

QStringList platformSupportedBackdrops()
{
#if defined(Q_OS_WIN)
    const QOperatingSystemVersion current = QOperatingSystemVersion::current();
    if (current >= QOperatingSystemVersion::Windows11_22H2)
        return {QStringLiteral("none"), QStringLiteral("mica"), QStringLiteral("acrylic"),
                QStringLiteral("blur")};
    if (current >= QOperatingSystemVersion::Windows10)
        return {QStringLiteral("none"), QStringLiteral("blur"), QStringLiteral("acrylic")};
    return {QStringLiteral("none")};
#elif defined(Q_OS_MACOS)
    return {QStringLiteral("none"), QStringLiteral("vibrancy"), QStringLiteral("blur")};
#elif defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
    return {QStringLiteral("none"), QStringLiteral("blur")};
#else
    return {QStringLiteral("none")};
#endif
}

// The pre-QApplication read has no organization name set yet. Passing the same
// names main() sets later opens the same file QSettings() will use afterwards.
QSettings themeSettings()
{
    if (QCoreApplication::organizationName().isEmpty()) {
        return QSettings(QSettings::UserScope, QStringLiteral("CutWire Drift"),
                         QStringLiteral("CutWire Drift"));
    }
    return QSettings();
}

bool readTranslucentSetting()
{
    QSettings settings = themeSettings();
    settings.beginGroup(QStringLiteral("Theme"));
    return settings.value(QStringLiteral("translucentWindows"), false).toBool();
}

} // namespace

#if !defined(Q_OS_WIN) && !defined(Q_OS_MACOS)
namespace drift::backdrop {
void applyNative(QWindow *, const QString &, bool) {}
}
#endif

// The attached windows. A nested type keeps QPointer out of the header.
class WindowBackdrop::WindowList
{
public:
    QList<QPointer<QQuickWindow>> windows;
};

WindowBackdrop *WindowBackdrop::shared()
{
    static WindowBackdrop instance;
    return &instance;
}

void WindowBackdrop::registerSingleton()
{
    static bool registered = false;
    if (registered)
        return;
    registered = true;
    qmlRegisterSingletonInstance("Drift", 1, 0, "WindowBackdrop", shared());
}

bool WindowBackdrop::startupWantsAlphaBuffer()
{
    if (!platformAllowsTranslucentWindows())
        return false;
    return readTranslucentSetting();
}

void WindowBackdrop::noteProcessTranslucency(bool active)
{
    if (g_translucencyActive == active)
        return;
    g_translucencyActive = active;
    if (g_instance)
        g_instance->refreshBackdrop();
}

bool WindowBackdrop::translucencyActive() const
{
    return g_translucencyActive;
}

WindowBackdrop::WindowBackdrop(QObject *parent)
    : QObject(parent)
    , m_supported(platformSupportedBackdrops())
    , m_windows(new WindowList)
{
    g_instance = this;
    m_translucentWindows = readTranslucentSetting();
    m_startupTranslucent = m_translucentWindows;
    connect(ThemeManager::shared(), &ThemeManager::themeChanged, this, &WindowBackdrop::refreshBackdrop);
    refreshBackdrop();
}

WindowBackdrop::~WindowBackdrop()
{
    delete m_windows;
    m_windows = nullptr;
    if (g_instance == this)
        g_instance = nullptr;
}

void WindowBackdrop::setTranslucentWindows(bool enabled)
{
    if (m_translucentWindows == enabled)
        return;
    m_translucentWindows = enabled;
    {
        QSettings settings = themeSettings();
        settings.beginGroup(QStringLiteral("Theme"));
        settings.setValue(QStringLiteral("translucentWindows"), enabled);
        settings.endGroup();
    }
    emit translucentWindowsChanged();

    const bool required = enabled != m_startupTranslucent;
    if (m_restartRequired == required)
        return;
    m_restartRequired = required;
    emit restartRequiredChanged();
}

QColor WindowBackdrop::windowBackground(const QColor &token, qreal backgroundOpacity) const
{
    if (!token.isValid())
        return token;
    if (!qIsFinite(backgroundOpacity))
        backgroundOpacity = 1.0;
    backgroundOpacity = qBound(0.0, backgroundOpacity, 1.0);

    qreal alpha = token.alphaF();
    if (!translucencyActive())
        alpha = 1.0;
    else
        alpha *= backgroundOpacity;
    alpha = qBound(0.0, alpha, 1.0);
    if (qAbs(alpha - token.alphaF()) < 0.0001)
        return token;

    QColor out = token;
    out.setAlphaF(alpha);
    return out;
}

void WindowBackdrop::attach(QQuickWindow *window)
{
    if (!window)
        return;
    for (const QPointer<QQuickWindow> &existing : m_windows->windows) {
        if (existing == window)
            return;
    }
    m_windows->windows.append(window);

    QPointer<QQuickWindow> watched = window;
    connect(window, &QObject::destroyed, this, [this, watched] {
        if (!m_windows)
            return;
        m_windows->windows.removeAll(watched);
    });
    connect(window, &QWindow::visibilityChanged, this, [this, watched](QWindow::Visibility visibility) {
        if (watched && visibility != QWindow::Hidden)
            applyTo(watched);
    });
    if (window->isVisible() || window->visibility() != QWindow::Hidden)
        applyTo(window);
}

QString WindowBackdrop::resolveBackdrop() const
{
    if (!translucencyActive())
        return QStringLiteral("none");

    QString requested = QStringLiteral("none");
    if (QQmlPropertyMap *window = ThemeManager::shared()->window()) {
        const QVariant value = window->value(QStringLiteral("backdrop"));
        if (value.isValid())
            requested = value.toString().trimmed();
    }
    if (requested.isEmpty() || !m_supported.contains(requested))
        return QStringLiteral("none");
    return requested;
}

bool WindowBackdrop::themeVariantIsDark() const
{
    const QString variant = ThemeManager::shared()->token(QStringLiteral("variant")).toString();
    return variant != QLatin1String("light");
}

void WindowBackdrop::refreshBackdrop()
{
    const QString next = resolveBackdrop();
    if (m_backdrop != next) {
        m_backdrop = next;
        emit backdropChanged();
    }
    reapplyAll();
}

void WindowBackdrop::reapplyAll()
{
    if (!m_windows)
        return;
    QList<QPointer<QQuickWindow>> alive;
    alive.reserve(m_windows->windows.size());
    for (const QPointer<QQuickWindow> &window : m_windows->windows) {
        if (!window)
            continue;
        alive.append(window);
        applyTo(window);
    }
    m_windows->windows = alive;
}

void WindowBackdrop::applyTo(QQuickWindow *window)
{
    if (!window || !translucencyActive())
        return;
    // winId() forces a platform window. A hidden tool window has not been
    // placed yet; visibilityChanged applies the backdrop when it is shown.
    if (!window->handle() && window->visibility() == QWindow::Hidden)
        return;
    drift::backdrop::applyNative(window, m_backdrop, themeVariantIsDark());
}
