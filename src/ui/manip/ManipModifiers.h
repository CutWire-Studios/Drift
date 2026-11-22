#pragma once

#include <QObject>
#include <Qt>

namespace drift::manip {

// What the held keys, or the touch context bar's sticky chips, ask a drag to do. Alt carries three
// intents because each tool reads the one that means something to it: from-centre on boxes,
// break-handle on paths, exclude on masks and brushes.
struct ManipModifiers
{
    Q_GADGET
    Q_PROPERTY(bool constrain MEMBER constrain)
    Q_PROPERTY(bool fromCenter MEMBER fromCenter)
    Q_PROPERTY(bool breakTangent MEMBER breakTangent)
    Q_PROPERTY(bool noSnap MEMBER noSnap)
    Q_PROPERTY(bool exclude MEMBER exclude)

public:
    bool constrain = false;
    bool fromCenter = false;
    bool breakTangent = false;
    bool noSnap = false;
    bool exclude = false;

    // Shift = constrain, Alt = fromCenter/breakTangent/exclude, Ctrl (Cmd on macOS, which Qt
    // reports as ControlModifier) = noSnap.
    static ManipModifiers fromQt(Qt::KeyboardModifiers m)
    {
        ManipModifiers r;
        r.constrain = m.testFlag(Qt::ShiftModifier);
        r.fromCenter = r.breakTangent = r.exclude = m.testFlag(Qt::AltModifier);
        r.noSnap = m.testFlag(Qt::ControlModifier);
        return r;
    }
    Q_INVOKABLE static drift::manip::ManipModifiers fromQtFlags(int m)
    {
        return fromQt(Qt::KeyboardModifiers::fromInt(m));
    }

    // A sticky chip adds to what the keyboard says; it never cancels it.
    ManipModifiers merged(const ManipModifiers &chips) const
    {
        ManipModifiers r;
        r.constrain = constrain || chips.constrain;
        r.fromCenter = fromCenter || chips.fromCenter;
        r.breakTangent = breakTangent || chips.breakTangent;
        r.noSnap = noSnap || chips.noSnap;
        r.exclude = exclude || chips.exclude;
        return r;
    }

    bool operator==(const ManipModifiers &o) const = default;
};

} // namespace drift::manip
