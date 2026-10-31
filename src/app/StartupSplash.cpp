#include "app/StartupSplash.h"
#include "ui/theme/ThemeManager.h"

#include <QCoreApplication>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QPainter>
#include <QQuickWindow>
#include <QScreen>
#include <QSettings>
#include <QStyleHints>

namespace {

constexpr int kWidth = 520;
constexpr int kHeight = 300;
constexpr int kMargin = 32;
constexpr int kLogoSize = 56;

// Mirrors Theme.qml's darkMode closely enough for a few seconds of splash: a pinned built-in,
// then the light/dark toggle, then the OS. A user theme gets the built-in of its OS side.
bool prefersLight()
{
    QSettings settings;
    const QString selection = settings.value(QStringLiteral("Theme/selection")).toString();
    if (selection == QLatin1String("light") || selection == QLatin1String("dark"))
        return selection == QLatin1String("light");
    const QVariant darkMode = settings.value(QStringLiteral("ui/darkMode"));
    if (darkMode.isValid())
        return !darkMode.toBool();
    return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Light;
}

QString loadFontFamily(const char *resource, const QString &fallback)
{
    const int id = QFontDatabase::addApplicationFont(QString::fromLatin1(resource));
    const QStringList families = QFontDatabase::applicationFontFamilies(id);
    return families.isEmpty() ? fallback : families.constFirst();
}

} // namespace

StartupSplash::StartupSplash()
{
    const QVariantMap tokens = ThemeManager::builtinComponent(
        prefersLight() ? QStringLiteral("light") : QStringLiteral("dark"), QStringLiteral("startupSplash"));
    m_background = tokens.value(QStringLiteral("background")).value<QColor>();
    m_border = tokens.value(QStringLiteral("border")).value<QColor>();
    m_foreground = tokens.value(QStringLiteral("foreground")).value<QColor>();
    m_foregroundMuted = tokens.value(QStringLiteral("foregroundMuted")).value<QColor>();
    m_progressTrack = tokens.value(QStringLiteral("progressTrack")).value<QColor>();
    m_progressFill = tokens.value(QStringLiteral("progressFill")).value<QColor>();

    const QString body = loadFontFamily(":/qt/qml/Drift/resources/fonts/InterUI-Regular.ttf", font().family());
    const QString title = loadFontFamily(":/qt/qml/Drift/resources/fonts/InterUI-SemiBold.ttf", body);
    m_bodyFont = QFont(body);
    m_bodyFont.setPixelSize(12);
    m_titleFont = QFont(title);
    m_titleFont.setPixelSize(24);
    m_titleFont.setWeight(QFont::DemiBold);

    const qreal dpr = QGuiApplication::primaryScreen() ? QGuiApplication::primaryScreen()->devicePixelRatio() : 1.0;
    m_logo = QPixmap(QStringLiteral(":/app/drift.png"))
                 .scaled(QSize(kLogoSize, kLogoSize) * dpr, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    m_logo.setDevicePixelRatio(dpr);

    // QSplashScreen sizes itself to its pixmap; the drawing all happens in drawContents.
    QPixmap canvas(QSize(kWidth, kHeight) * dpr);
    canvas.setDevicePixelRatio(dpr);
    canvas.fill(m_background);
    setPixmap(canvas);
}

void StartupSplash::step(const QString &status, double progress)
{
    m_status = status;
    m_progress = qBound(0.0, progress, 1.0);
    // Synchronous: the event loop is not running yet, and pumping it here would deliver the
    // zero-timers that startup code queues for after the QML is loaded.
    repaint();
}

void StartupSplash::finishOn(QQuickWindow *window)
{
    if (!window) {
        close();
        deleteLater();
        return;
    }
    // frameSwapped comes from the render thread; queued, so the close runs on this one.
    connect(window, &QQuickWindow::frameSwapped, this, [this] {
        close();
        deleteLater();
    }, static_cast<Qt::ConnectionType>(Qt::QueuedConnection | Qt::SingleShotConnection));
}

void StartupSplash::drawContents(QPainter *painter)
{
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setRenderHint(QPainter::TextAntialiasing);
    const QRectF bounds(0, 0, kWidth, kHeight);

    painter->fillRect(bounds, m_background);
    painter->setPen(QPen(m_border, 1));
    painter->setBrush(Qt::NoBrush);
    painter->drawRect(bounds.adjusted(0.5, 0.5, -0.5, -0.5));

    painter->drawPixmap(QPointF(kMargin, kMargin), m_logo);

    const qreal textX = kMargin + kLogoSize + 16;
    painter->setFont(m_titleFont);
    painter->setPen(m_foreground);
    painter->drawText(QRectF(textX, kMargin, kWidth - textX - kMargin, 32), Qt::AlignLeft | Qt::AlignVCenter,
                      QStringLiteral("Drift"));
    painter->setFont(m_bodyFont);
    painter->setPen(m_foregroundMuted);
    painter->drawText(QRectF(textX, kMargin + 32, kWidth - textX - kMargin, 20), Qt::AlignLeft | Qt::AlignVCenter,
                      tr("Version %1").arg(QStringLiteral(DRIFT_VERSION)));

    const qreal barY = kHeight - kMargin - 3;
    const QRectF statusRect(kMargin, barY - 28, kWidth - 2 * kMargin, 20);
    painter->drawText(statusRect, Qt::AlignLeft | Qt::AlignVCenter,
                      QFontMetrics(m_bodyFont).elidedText(m_status, Qt::ElideRight, int(statusRect.width())));

    const QRectF track(kMargin, barY, kWidth - 2 * kMargin, 3);
    painter->setPen(Qt::NoPen);
    painter->setBrush(m_progressTrack);
    painter->drawRoundedRect(track, 1.5, 1.5);
    if (m_progress > 0.0) {
        painter->setBrush(m_progressFill);
        painter->drawRoundedRect(QRectF(track.topLeft(), QSizeF(track.width() * m_progress, track.height())), 1.5, 1.5);
    }
}

void StartupSplash::mousePressEvent(QMouseEvent *)
{
}
