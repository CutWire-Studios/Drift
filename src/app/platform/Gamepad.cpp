#include "app/platform/Gamepad.h"

#include <QElapsedTimer>
#include <QGuiApplication>
#include <QMetaObject>
#include <QSettings>
#include <QTimer>

#include <algorithm>
#include <cmath>

#ifdef Q_OS_ANDROID
#include <QJniEnvironment>
#include <QJniObject>
#include <QtCore/qcoreapplication_platform.h>
#endif

namespace drift {

namespace {

QString settingsKey(const char *name)
{
    return QLatin1String("gamepad/") + QLatin1String(name);
}

#ifdef Q_OS_ANDROID

constexpr const char *kInputClass = "org/cutwire/drift/GamepadInput";

class AndroidGamepad;
AndroidGamepad *g_android = nullptr;

class AndroidGamepad : public GamepadBackend
{
public:
    explicit AndroidGamepad(Gamepad &service)
        : m_service(service)
    {
        g_android = this;
        call("start");
    }
    ~AndroidGamepad() override
    {
        call("stop");
        g_android = nullptr;
    }

    void poll() override {}

    void rumble(double low, double high, int ms) override
    {
        QJniObject context = QNativeInterface::QAndroidApplication::context();
        if (!context.isValid())
            return;
        QJniObject::callStaticMethod<void>(kInputClass, "rumble", "(Landroid/content/Context;FFI)V",
                                           context.object(), jfloat(low), jfloat(high), jint(ms));
        QJniEnvironment().checkAndClearExceptions();
    }

    void setGyroEnabled(bool enabled) override
    {
        QJniObject context = QNativeInterface::QAndroidApplication::context();
        if (!context.isValid())
            return;
        QJniObject::callStaticMethod<void>(kInputClass, "setGyroEnabled", "(Landroid/content/Context;Z)V",
                                           context.object(), jboolean(enabled));
        QJniEnvironment().checkAndClearExceptions();
    }

    Gamepad &service() { return m_service; }

private:
    void call(const char *method)
    {
        QJniObject context = QNativeInterface::QAndroidApplication::context();
        if (!context.isValid())
            return;
        QJniObject::callStaticMethod<void>(kInputClass, method, "(Landroid/content/Context;)V", context.object());
        QJniEnvironment().checkAndClearExceptions();
    }

    Gamepad &m_service;
};

// All of these run on the Android UI thread.
void JNICALL nativeAxes(JNIEnv *, jclass, jfloat lx, jfloat ly, jfloat rx, jfloat ry, jfloat lt, jfloat rt)
{
    QMetaObject::invokeMethod(qApp, [=] {
        if (g_android)
            g_android->service().ingestAxes(lx, ly, rx, ry, lt, rt);
    }, Qt::QueuedConnection);
}

void JNICALL nativeButton(JNIEnv *env, jclass, jstring name, jboolean down)
{
    const QString button = QJniObject(name).toString();
    Q_UNUSED(env);
    QMetaObject::invokeMethod(qApp, [button, down] {
        if (g_android)
            g_android->service().ingestButton(button, down);
    }, Qt::QueuedConnection);
}

void JNICALL nativeGyro(JNIEnv *, jclass, jfloat x, jfloat y, jfloat z)
{
    QMetaObject::invokeMethod(qApp, [=] {
        if (g_android)
            g_android->service().ingestGyro(x, y, z);
    }, Qt::QueuedConnection);
}

// "name\tgyro\trumble" per controller.
void JNICALL nativeDevices(JNIEnv *, jclass, jobjectArray rows)
{
    QVariantList devices;
    QJniEnvironment env;
    const jsize count = env->GetArrayLength(rows);
    for (jsize i = 0; i < count; ++i) {
        const QStringList fields = QJniObject(env->GetObjectArrayElement(rows, i)).toString().split(QLatin1Char('\t'));
        if (fields.size() < 3)
            continue;
        devices.append(QVariantMap{{QStringLiteral("id"), i}, {QStringLiteral("name"), fields.at(0)},
                                   {QStringLiteral("gyro"), fields.at(1) == QLatin1String("1")},
                                   {QStringLiteral("rumble"), fields.at(2) == QLatin1String("1")}});
    }
    QMetaObject::invokeMethod(qApp, [devices] {
        if (g_android)
            g_android->service().ingestDevices(devices);
    }, Qt::QueuedConnection);
}

std::unique_ptr<GamepadBackend> makeBackend(Gamepad &service)
{
    static const bool registered = [] {
        const JNINativeMethod methods[] = {
            {"nativeAxes", "(FFFFFF)V", reinterpret_cast<void *>(nativeAxes)},
            {"nativeButton", "(Ljava/lang/String;Z)V", reinterpret_cast<void *>(nativeButton)},
            {"nativeGyro", "(FFF)V", reinterpret_cast<void *>(nativeGyro)},
            {"nativeDevices", "([Ljava/lang/String;)V", reinterpret_cast<void *>(nativeDevices)},
        };
        return QJniEnvironment().registerNativeMethods(kInputClass, methods, 4);
    }();
    if (!registered)
        return nullptr;
    return std::make_unique<AndroidGamepad>(service);
}

#elif defined(DRIFT_HAVE_SDL)

std::unique_ptr<GamepadBackend> makeBackend(Gamepad &service)
{
    return makeSdlGamepadBackend(service);
}

#else

std::unique_ptr<GamepadBackend> makeBackend(Gamepad &)
{
    return nullptr;
}

#endif

} // namespace

Gamepad::Gamepad(QObject *parent)
    : QObject(parent)
{
    QSettings settings;
    m_enabled = settings.value(settingsKey("enabled"), m_enabled).toBool();
    m_deadZone = std::clamp(settings.value(settingsKey("deadZone"), m_deadZone).toDouble(), 0.0, 0.6);
    m_curve = std::clamp(settings.value(settingsKey("curve"), m_curve).toDouble(), 1.0, 3.0);
    m_lookSpeed = std::clamp(settings.value(settingsKey("lookSpeed"), m_lookSpeed).toDouble(), 30.0, 360.0);
    m_gyroEnabled = settings.value(settingsKey("gyro"), m_gyroEnabled).toBool();
    m_gyroSensitivity = std::clamp(settings.value(settingsKey("gyroSensitivity"), m_gyroSensitivity).toDouble(), 0.1, 5.0);
    m_hapticsEnabled = settings.value(settingsKey("haptics"), m_hapticsEnabled).toBool();

    m_poll = new QTimer(this);
    connect(m_poll, &QTimer::timeout, this, [this] {
        if (m_backend)
            m_backend->poll();
        flushState();
    });
    // SDL's joystick enumeration and HIDAPI threads are not worth paying for before the editor is up.
    QTimer::singleShot(2000, this, &Gamepad::start);
}

Gamepad::~Gamepad() = default;

bool Gamepad::available() const
{
#if defined(Q_OS_ANDROID) || defined(DRIFT_HAVE_SDL)
    return true;
#else
    return false;
#endif
}

void Gamepad::start()
{
    if (!m_enabled || m_backend)
        return;
    m_backend = makeBackend(*this);
    if (!m_backend)
        return;
    m_backend->setGyroEnabled(m_gyroEnabled);
    applyPolling();
}

void Gamepad::applyPolling()
{
    // Hot-plug is noticed by the backend's own events, so a slow poll is enough while idle.
    m_poll->start(connected() ? 8 : 500);
}

bool Gamepad::acceptsInput() const
{
    return QGuiApplication::applicationState() == Qt::ApplicationActive;
}

bool Gamepad::hasGyro() const
{
    return std::any_of(m_devices.cbegin(), m_devices.cend(), [](const QVariant &d) { return d.toMap().value(QStringLiteral("gyro")).toBool(); });
}

bool Gamepad::hasRumble() const
{
    return std::any_of(m_devices.cbegin(), m_devices.cend(), [](const QVariant &d) { return d.toMap().value(QStringLiteral("rumble")).toBool(); });
}

void Gamepad::setCapturing(bool capturing)
{
    if (m_capturing == capturing)
        return;
    m_capturing = capturing;
    emit capturingChanged();
}

void Gamepad::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return;
    m_enabled = enabled;
    QSettings().setValue(settingsKey("enabled"), enabled);
    if (enabled) {
        start();
    } else {
        m_poll->stop();
        m_backend.reset();
        m_pressed.clear();
        m_leftX = m_leftY = m_rightX = m_rightY = m_leftTrigger = m_rightTrigger = 0;
        m_gyro[0] = m_gyro[1] = m_gyro[2] = 0;
        ingestDevices({});
        emit stateChanged();
    }
    emit settingsChanged();
}

void Gamepad::setDeadZone(double value)
{
    value = std::clamp(value, 0.0, 0.6);
    if (qFuzzyCompare(m_deadZone, value))
        return;
    m_deadZone = value;
    QSettings().setValue(settingsKey("deadZone"), value);
    emit settingsChanged();
}

void Gamepad::setCurve(double value)
{
    value = std::clamp(value, 1.0, 3.0);
    if (qFuzzyCompare(m_curve, value))
        return;
    m_curve = value;
    QSettings().setValue(settingsKey("curve"), value);
    emit settingsChanged();
}

void Gamepad::setLookSpeed(double value)
{
    value = std::clamp(value, 30.0, 360.0);
    if (qFuzzyCompare(m_lookSpeed, value))
        return;
    m_lookSpeed = value;
    QSettings().setValue(settingsKey("lookSpeed"), value);
    emit settingsChanged();
}

void Gamepad::setGyroEnabled(bool enabled)
{
    if (m_gyroEnabled == enabled)
        return;
    m_gyroEnabled = enabled;
    QSettings().setValue(settingsKey("gyro"), enabled);
    if (m_backend)
        m_backend->setGyroEnabled(enabled);
    if (!enabled)
        ingestGyro(0, 0, 0);
    emit settingsChanged();
}

void Gamepad::setGyroSensitivity(double value)
{
    value = std::clamp(value, 0.1, 5.0);
    if (qFuzzyCompare(m_gyroSensitivity, value))
        return;
    m_gyroSensitivity = value;
    QSettings().setValue(settingsKey("gyroSensitivity"), value);
    emit settingsChanged();
}

void Gamepad::setHapticsEnabled(bool enabled)
{
    if (m_hapticsEnabled == enabled)
        return;
    m_hapticsEnabled = enabled;
    QSettings().setValue(settingsKey("haptics"), enabled);
    emit settingsChanged();
}

bool Gamepad::chordHeld(const QString &chord) const
{
    if (!chord.startsWith(QLatin1String("Pad+")))
        return false;
    const QStringList parts = chord.mid(4).split(QLatin1Char('+'));
    return std::all_of(parts.cbegin(), parts.cend(), [this](const QString &name) { return m_pressed.contains(name); });
}

void Gamepad::rumble(double low, double high, int ms)
{
    if (m_backend && connected())
        m_backend->rumble(std::clamp(low, 0.0, 1.0), std::clamp(high, 0.0, 1.0), ms);
}

void Gamepad::haptic(const QString &kind)
{
    if (!m_hapticsEnabled || !connected())
        return;
    // Editing events can arrive far faster than a motor resolves them.
    static QElapsedTimer clock = [] { QElapsedTimer t; t.start(); return t; }();
    const qint64 now = clock.elapsed();
    if (now - m_lastRumbleMs < 60)
        return;
    m_lastRumbleMs = now;
    if (kind == QLatin1String("cut"))
        rumble(0.5, 0.0, 40);
    else
        rumble(0.0, 0.4, 25);
}

void Gamepad::ingestAxes(double lx, double ly, double rx, double ry, double lt, double rt)
{
    const auto shape = [this](double x, double y, double *outX, double *outY) {
        const double mag = std::hypot(x, y);
        if (mag <= m_deadZone) {
            *outX = *outY = 0;
            return;
        }
        const double scaled = std::min(1.0, (mag - m_deadZone) / (1.0 - m_deadZone));
        const double gain = std::pow(scaled, m_curve) / mag;
        *outX = x * gain;
        *outY = -y * gain;
    };
    shape(lx, ly, &m_leftX, &m_leftY);
    shape(rx, ry, &m_rightX, &m_rightY);
    m_leftTrigger = std::clamp(lt, 0.0, 1.0);
    m_rightTrigger = std::clamp(rt, 0.0, 1.0);
    // Triggers double as buttons, with a gap between press and release so a hovering finger does not chatter.
    const auto trigger = [this](const QString &name, double value) {
        const bool down = m_pressed.contains(name);
        if (!down && value > 0.5)
            ingestButton(name, true);
        else if (down && value < 0.3)
            ingestButton(name, false);
    };
    trigger(QStringLiteral("LT"), m_leftTrigger);
    trigger(QStringLiteral("RT"), m_rightTrigger);
    m_stateDirty = true;
}

void Gamepad::ingestButton(const QString &name, bool down)
{
    if (m_pressed.contains(name) == down)
        return;
    QString chord;
    if (down) {
        chord = QStringLiteral("Pad+");
        // Held shoulders and triggers are the modifiers of the chord.
        for (const char *mod : {"LB", "RB", "LT", "RT"}) {
            if (m_pressed.contains(QLatin1String(mod)) && name != QLatin1String(mod))
                chord += QLatin1String(mod) + QLatin1Char('+');
        }
        chord += name;
        m_pressed.insert(name);
    } else {
        m_pressed.remove(name);
    }
    m_stateDirty = true;
    if (down)
        emit buttonPressed(chord);
}

void Gamepad::ingestGyro(double x, double y, double z)
{
    m_gyro[0] = x;
    m_gyro[1] = y;
    m_gyro[2] = z;
    m_stateDirty = true;
}

void Gamepad::ingestDevices(const QVariantList &devices)
{
    if (devices == m_devices)
        return;
    const bool wasConnected = connected();
    m_devices = devices;
    if (m_devices.isEmpty() && !m_pressed.isEmpty()) {
        m_pressed.clear();
        m_leftX = m_leftY = m_rightX = m_rightY = m_leftTrigger = m_rightTrigger = 0;
        m_stateDirty = true;
    }
    if (m_poll && m_poll->isActive() && wasConnected != connected())
        applyPolling();
    emit devicesChanged();
}

void Gamepad::flushState()
{
    if (!m_stateDirty)
        return;
    m_stateDirty = false;
    emit stateChanged();
}

} // namespace drift

#include "moc_Gamepad.cpp"
