#include "app/editor/LayoutStore.h"

#include <QGuiApplication>
#include <QScreen>
#include <QSettings>

#include <algorithm>
#include <cmath>

namespace {

constexpr auto kGeometryKey = "ui/window/geometry";
constexpr auto kMaximizedKey = "ui/window/maximized";

QString settingsKey(const QString &panelKey)
{
    return QStringLiteral("ui/layout/") + panelKey;
}

// A panel that remembers itself as a sliver or as the whole window is a layout the
// user cannot get out of without dragging every handle back. Anything outside this
// band is treated as corrupt and falls back to the default proportions.
constexpr double kMinFraction = 0.05;
constexpr double kMaxFraction = 0.95;

// Enough of the window has to land on some screen for the user to grab its title bar
// and drag it back. Requiring the whole rect to fit is too strict — windows legitimately
// straddle a screen edge, and one pixel of overhang would throw the layout away.
bool isReachable(const QRect &geometry)
{
    for (const QScreen *screen : QGuiApplication::screens()) {
        const QRect overlap = screen->availableGeometry().intersected(geometry);
        if (overlap.width() >= 120 && overlap.height() >= 40)
            return true;
    }
    return false;
}

} // namespace

LayoutStore::LayoutStore(QObject *parent)
    : QObject(parent)
{
}

QRect LayoutStore::savedWindowGeometry(int minimumWidth, int minimumHeight) const
{
    const QRect stored = QSettings().value(QLatin1String(kGeometryKey)).toRect();
    if (stored.width() <= 0 || stored.height() <= 0)
        return {};

    QRect geometry = stored;
    geometry.setWidth(std::max(geometry.width(), minimumWidth));
    geometry.setHeight(std::max(geometry.height(), minimumHeight));

    // The display set can change between sessions, so a window saved on a big monitor
    // has to be cut down to whatever it is reopening on.
    const QScreen *screen = QGuiApplication::screenAt(geometry.center());
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    if (screen) {
        const QRect available = screen->availableGeometry();
        geometry.setWidth(std::min(geometry.width(), available.width()));
        geometry.setHeight(std::min(geometry.height(), available.height()));
    }

    if (!isReachable(geometry))
        return {};
    return geometry;
}

bool LayoutStore::savedWindowMaximized() const
{
    return QSettings().value(QLatin1String(kMaximizedKey), false).toBool();
}

void LayoutStore::saveWindowState(const QRect &windowedGeometry, bool maximized)
{
    QSettings settings;
    // A session spent entirely maximized never samples a windowed rect; keeping the
    // previous one means "restore down" still lands where the user last had it.
    if (windowedGeometry.width() > 0 && windowedGeometry.height() > 0) {
        QRect geometry = windowedGeometry;
        // Wayland does not let a client know or choose where its window sits, so the
        // position it reports is a placeholder. Storing it would overwrite a real one
        // from an X11 session on the same machine with (0, 0) — the size and the
        // maximized state are the parts Wayland can actually restore.
        if (QGuiApplication::platformName().contains(QLatin1String("wayland"))) {
            const QRect stored = settings.value(QLatin1String(kGeometryKey)).toRect();
            geometry.moveTopLeft(stored.isNull() ? QPoint(0, 0) : stored.topLeft());
        }
        settings.setValue(QLatin1String(kGeometryKey), geometry);
    }
    settings.setValue(QLatin1String(kMaximizedKey), maximized);
}

double LayoutStore::panelFraction(const QString &key, double fallback) const
{
    bool ok = false;
    const double fraction = QSettings().value(settingsKey(key)).toDouble(&ok);
    if (!ok || !std::isfinite(fraction) || fraction < kMinFraction || fraction > kMaxFraction)
        return fallback;
    return fraction;
}

void LayoutStore::setPanelFraction(const QString &key, double fraction)
{
    if (!std::isfinite(fraction) || fraction < kMinFraction || fraction > kMaxFraction)
        return;
    QSettings().setValue(settingsKey(key), fraction);
}

double LayoutStore::layoutNumber(const QString &key, double fallback) const
{
    bool ok = false;
    const double value = QSettings().value(settingsKey(key)).toDouble(&ok);
    return ok && std::isfinite(value) ? value : fallback;
}

void LayoutStore::setLayoutNumber(const QString &key, double value)
{
    if (std::isfinite(value))
        QSettings().setValue(settingsKey(key), value);
}

QString LayoutStore::layoutString(const QString &key, const QString &fallback) const
{
    return QSettings().value(settingsKey(key), fallback).toString();
}

void LayoutStore::setLayoutString(const QString &key, const QString &value)
{
    QSettings().setValue(settingsKey(key), value);
}

#include "moc_LayoutStore.cpp"
