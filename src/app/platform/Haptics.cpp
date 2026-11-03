#include "app/platform/Haptics.h"

#include <QSettings>

#ifdef Q_OS_ANDROID
#include <QJniEnvironment>
#include <QJniObject>
#include <QtCore/qcoreapplication_platform.h>
#endif

namespace {

// The floor between two effects, of any kind. A drag over a timeline with a snap target every few
// pixels generates move events far faster than an actuator can resolve them, and one tick each is
// not feedback, it is a rumble. 40 ms is under the threshold at which two ticks stop reading as
// two, and above the point where they smear into one continuous buzz.
constexpr qint64 kMinIntervalMs = 40;

// Snap targets are timeline seconds. Anything closer together than this is the same target arriving
// with float noise on it, not a new one.
constexpr qreal kSnapEpsilon = 0.0005;

QString settingsKey(const char *name)
{
    return QLatin1String("haptics/") + QLatin1String(name);
}

#ifdef Q_OS_ANDROID

constexpr const char *kHapticsClass = "org/cutwire/drift/Haptics";

bool deviceHasVibrator()
{
    QJniObject context = QNativeInterface::QAndroidApplication::context();
    if (!context.isValid())
        return false;
    const bool has = QJniObject::callStaticMethod<jboolean>(
        kHapticsClass, "hasVibrator", "(Landroid/content/Context;)Z", context.object());
    QJniEnvironment().checkAndClearExceptions();
    return has;
}

#endif

} // namespace

namespace drift {

Haptics::Haptics(QObject *parent)
    : QObject(parent)
{
    m_clock.start();
    m_enabled = QSettings().value(settingsKey("enabled"), true).toBool();
}

bool Haptics::isSupported() const
{
#ifdef Q_OS_ANDROID
    // Queried once, on the first read: the answer cannot change for the life of the process, and
    // the call reaches the Android context, so it wants the QML engine to be up rather than the
    // constructor to be running.
    static const bool supported = deviceHasVibrator();
    return supported;
#else
    return false;
#endif
}

void Haptics::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return;
    m_enabled = enabled;
    QSettings().setValue(settingsKey("enabled"), enabled);
    // Latches would otherwise hold whatever state they were in when it was switched off, and the
    // first drag after switching it back on would start mid-snap and stay silent.
    reset();
    emit enabledChanged();
    // The settings switch that just turned this on would otherwise be silent: fire() bailed while
    // m_enabled was still false. One tick now, so the opt-in is felt rather than trusted.
    if (enabled)
        fire(ToggleOn);
}

void Haptics::fire(Effect effect)
{
    if (!m_enabled)
        return;

    // Applied on every platform, not just the one that can feel it, so the limiter is exercised by
    // the desktop build and a call site that would machine-gun on device shows up in a debugger
    // here too. Ticks that can stack during a drag (select, snap, detent, press) are limited;
    // one-shots that name a discrete event always go through, so a button press 20 ms earlier
    // cannot swallow the confirm of the action it caused.
    const qint64 now = m_clock.elapsed();
    const bool oneShot = effect == PickUp || effect == Drop || effect == Boundary
                         || effect == Confirm || effect == ToggleOn || effect == ToggleOff
                         || effect == Success || effect == Error;
    if (!oneShot && m_lastFiredMs != 0 && now - m_lastFiredMs < kMinIntervalMs)
        return;
    m_lastFiredMs = now;

#ifdef Q_OS_ANDROID
    const jint intent = static_cast<jint>(effect);
    // performHapticFeedback walks the view hierarchy, which belongs to the Android UI thread; this
    // is called from Qt's GUI thread, which is not it. Posted, not blocked on: a tick that arrives
    // a frame late is still a tick, and a drag handler cannot afford to wait on another thread.
    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([intent] {
        QJniObject context = QNativeInterface::QAndroidApplication::context();
        if (!context.isValid())
            return;
        QJniObject::callStaticMethod<void>(kHapticsClass, "perform",
                                           "(Landroid/content/Context;I)V", context.object(),
                                           intent);
        // Feedback is advisory. A device that refuses it still edits video.
        QJniEnvironment().checkAndClearExceptions();
    });
#else
    Q_UNUSED(effect);
#endif
}

void Haptics::select()
{
    fire(Select);
}

void Haptics::pickUp()
{
    fire(PickUp);
}

void Haptics::drop()
{
    reset();
    fire(Drop);
}

void Haptics::reset()
{
    m_snapTarget = -1;
    m_lane = -1;
    m_trimState = TrimNone;
}

void Haptics::boundary()
{
    fire(Boundary);
}

void Haptics::detent()
{
    fire(Detent);
}

void Haptics::confirm()
{
    fire(Confirm);
}

void Haptics::press()
{
    fire(Press);
}

void Haptics::toggle(bool on)
{
    fire(on ? ToggleOn : ToggleOff);
}

void Haptics::success()
{
    fire(Success);
}

void Haptics::error()
{
    fire(Error);
}

void Haptics::snap(qreal targetSeconds)
{
    const bool engaged = targetSeconds >= 0;
    if (!engaged) {
        m_snapTarget = -1;
        return;
    }
    // Re-reporting the target the drag is already held by is the common case — it arrives on every
    // move event for as long as the finger stays inside the snap tolerance. Only a different target
    // is a new event.
    if (m_snapTarget >= 0 && qAbs(targetSeconds - m_snapTarget) < kSnapEpsilon)
        return;
    m_snapTarget = targetSeconds;
    fire(Snap);
}

void Haptics::lane(int trackIndex)
{
    if (trackIndex < 0) {
        m_lane = -1;
        return;
    }
    if (m_lane == trackIndex)
        return;
    // Silent on the first lane a drag reports: that is the track the clip was already on, and
    // picking it up has just spoken for itself.
    const bool hadLane = m_lane >= 0;
    m_lane = trackIndex;
    if (hadLane)
        fire(Detent);
}

void Haptics::trimStep(int outcome)
{
    // TrimNone is the edge declining to move for no particular reason — most often a snap holding
    // it in place across a run of move events. It carries no news, so it leaves the latch as it
    // found it; treating it as a release would let a parked snap re-tick on every other event.
    if (outcome == TrimNone)
        return;

    const int previous = m_trimState;
    m_trimState = outcome;
    if (outcome == previous)
        return;

    if (outcome == TrimSnapped)
        fire(Snap);
    else if (outcome == TrimBlocked)
        fire(Boundary);
    // TrimMoved is the edge running free: nothing to feel, but it clears the way for the next snap
    // or limit to be a transition again.
}

} // namespace drift

#include "moc_Haptics.cpp"
