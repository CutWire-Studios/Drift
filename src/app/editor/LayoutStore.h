#pragma once

#include <QObject>
#include <QRect>
#include <QString>

// Remembers where the editor window sat and how the user had the panels sized, so a
// session opens the way the last one was left instead of at the built-in defaults.
//
// App-wide (QSettings), not per-project: window placement belongs to the display the
// user is at, not to the .drift file — opening the same project on a laptop should not
// drag a layout sized for a desktop monitor along with it.
class LayoutStore : public QObject
{
    Q_OBJECT

public:
    explicit LayoutStore(QObject *parent = nullptr);

    // Geometry of the last ordinary-sized (neither maximized nor fullscreen) session,
    // already checked against the displays attached right now. A null rect means there
    // is nothing usable to restore — nothing stored yet, or the monitor it was saved on
    // is gone — and the caller should leave the window at its defaults.
    Q_INVOKABLE QRect savedWindowGeometry(int minimumWidth, int minimumHeight) const;
    Q_INVOKABLE bool savedWindowMaximized() const;
    Q_INVOKABLE void saveWindowState(const QRect &windowedGeometry, bool maximized);

    // Panel proportions, keyed by workspace and panel ("landscape/assets"). Stored as a
    // fraction of the split the panel sits in rather than a pixel width, so a layout
    // dragged out on a 4K display still makes sense on a laptop screen.
    Q_INVOKABLE double panelFraction(const QString &key, double fallback) const;
    Q_INVOKABLE void setPanelFraction(const QString &key, double fraction);

    // Plain per-panel values that are not proportions of a split: a rail width in icon
    // units, the tab a panel was left on.
    Q_INVOKABLE double layoutNumber(const QString &key, double fallback) const;
    Q_INVOKABLE void setLayoutNumber(const QString &key, double value);
    Q_INVOKABLE QString layoutString(const QString &key, const QString &fallback) const;
    Q_INVOKABLE void setLayoutString(const QString &key, const QString &value);
};
