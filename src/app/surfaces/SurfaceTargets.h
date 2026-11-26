#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include <cmath>
#include <functional>

namespace drift {

struct SurfaceTarget {
    enum Kind { Action, Absolute, Relative };

    QString id;
    QString label;
    QString group;
    Kind kind = Action;
};

// What a control surface can drive. The manager only knows ids; the editor implements the
// parameters behind them. An Absolute target takes a 0..1 value, a Relative one signed ticks, and
// an Action just runs.
class SurfaceTargets {
public:
    virtual ~SurfaceTargets() = default;

    virtual QList<SurfaceTarget> targets() const = 0;
    // Ids outside targets() resolve here too: effect parameters picked from the UI carry their
    // own id ("effect:<effectId>:<param>:<min>:<max>").
    virtual SurfaceTarget describe(const QString &id) const = 0;
    // False when the id is not handled here, and the manager forwards it as a keymap action.
    virtual bool trigger(const QString &id) = 0;
    virtual void setAbsolute(const QString &id, double normalized) = 0;
    virtual void nudge(const QString &id, double ticks) = 0;
    // The current value as 0..1 (1 or 0 for a lit or dark action), NaN when it has none.
    virtual double current(const QString &id) const = 0;
    virtual QStringList stripLabels() const { return {}; }
    // The target id for a UI property row (a clip property or an effect parameter), empty when it
    // cannot be driven.
    virtual QString idForProperty(const QString &key, double min, double max) const
    {
        Q_UNUSED(key) Q_UNUSED(min) Q_UNUSED(max)
        return {};
    }

    // The playhead in seconds, NaN without a project; for transports that report or set it.
    virtual double playheadSeconds() const { return std::nan(""); }
    virtual void seek(double seconds) { Q_UNUSED(seconds) }

    // Set by the manager. invalidated: what a pickup was compared against moved under it
    // (selection, mixer bank). labelsChanged: stripLabels() differs. requestAction: forward a
    // keymap action id to the shell.
    std::function<void()> invalidated;
    std::function<void()> labelsChanged;
    std::function<void(const QString &)> requestAction;
};

} // namespace drift
