#pragma once

#include <QObject>
#include <QSet>
#include <QStringList>
#include <QVariantList>

#include <functional>
#include <memory>

// Game controllers as editing input. SDL3 (gamepad and sensor subsystems only) feeds this on
// desktop, the activity's InputDevice events on Android (android/src/org/cutwire/drift/
// GamepadInput.java); both push raw state in through the ingest calls and the service turns it into
// one shape for QML: dead-zoned, curved sticks (up is positive), triggers, held buttons, gyro rate.
//
// Buttons are named A B X Y LB RB LT RT Back Start Guide LS RS DpadUp DpadDown DpadLeft DpadRight.
// A press is announced as a keymap chord, "Pad+A" or with a held shoulder/trigger as a modifier,
// "Pad+LB+A", so the same action table as the keyboard binds them.
class QTimer;

namespace drift {

class GamepadBackend
{
public:
    virtual ~GamepadBackend() = default;
    // Called from a GUI-thread timer; drains the device events into the service.
    virtual void poll() = 0;
    virtual void rumble(double low, double high, int ms) = 0;
    virtual void setGyroEnabled(bool enabled) = 0;
};

class Gamepad : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available CONSTANT FINAL)
    Q_PROPERTY(bool connected READ connected NOTIFY devicesChanged FINAL)
    // {id, name, gyro, rumble} per attached controller.
    Q_PROPERTY(QVariantList devices READ devices NOTIFY devicesChanged FINAL)
    Q_PROPERTY(bool hasGyro READ hasGyro NOTIFY devicesChanged FINAL)
    Q_PROPERTY(bool hasRumble READ hasRumble NOTIFY devicesChanged FINAL)
    // The shortcut capture field is listening: presses must not also run the actions they are bound to.
    Q_PROPERTY(bool capturing READ capturing WRITE setCapturing NOTIFY capturingChanged FINAL)

    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY settingsChanged FINAL)
    Q_PROPERTY(double deadZone READ deadZone WRITE setDeadZone NOTIFY settingsChanged FINAL)
    Q_PROPERTY(double curve READ curve WRITE setCurve NOTIFY settingsChanged FINAL)
    // Degrees per second at full right-stick deflection.
    Q_PROPERTY(double lookSpeed READ lookSpeed WRITE setLookSpeed NOTIFY settingsChanged FINAL)
    Q_PROPERTY(bool gyroEnabled READ gyroEnabled WRITE setGyroEnabled NOTIFY settingsChanged FINAL)
    Q_PROPERTY(double gyroSensitivity READ gyroSensitivity WRITE setGyroSensitivity NOTIFY settingsChanged FINAL)
    Q_PROPERTY(bool hapticsEnabled READ hapticsEnabled WRITE setHapticsEnabled NOTIFY settingsChanged FINAL)

    Q_PROPERTY(double leftX READ leftX NOTIFY stateChanged FINAL)
    Q_PROPERTY(double leftY READ leftY NOTIFY stateChanged FINAL)
    Q_PROPERTY(double rightX READ rightX NOTIFY stateChanged FINAL)
    Q_PROPERTY(double rightY READ rightY NOTIFY stateChanged FINAL)
    Q_PROPERTY(double leftTrigger READ leftTrigger NOTIFY stateChanged FINAL)
    Q_PROPERTY(double rightTrigger READ rightTrigger NOTIFY stateChanged FINAL)
    // Angular rate in rad/s about the controller's own axes (x pitch, y yaw, z roll).
    Q_PROPERTY(double gyroX READ gyroX NOTIFY stateChanged FINAL)
    Q_PROPERTY(double gyroY READ gyroY NOTIFY stateChanged FINAL)
    Q_PROPERTY(double gyroZ READ gyroZ NOTIFY stateChanged FINAL)
    Q_PROPERTY(QStringList pressed READ pressed NOTIFY stateChanged FINAL)

public:
    explicit Gamepad(QObject *parent = nullptr);
    ~Gamepad() override;

    bool available() const;
    bool connected() const { return !m_devices.isEmpty(); }
    QVariantList devices() const { return m_devices; }
    bool hasGyro() const;
    bool hasRumble() const;
    bool capturing() const { return m_capturing; }
    void setCapturing(bool capturing);

    bool enabled() const { return m_enabled; }
    void setEnabled(bool enabled);
    double deadZone() const { return m_deadZone; }
    void setDeadZone(double value);
    double curve() const { return m_curve; }
    void setCurve(double value);
    double lookSpeed() const { return m_lookSpeed; }
    void setLookSpeed(double value);
    bool gyroEnabled() const { return m_gyroEnabled; }
    void setGyroEnabled(bool enabled);
    double gyroSensitivity() const { return m_gyroSensitivity; }
    void setGyroSensitivity(double value);
    bool hapticsEnabled() const { return m_hapticsEnabled; }
    void setHapticsEnabled(bool enabled);

    double leftX() const { return m_leftX; }
    double leftY() const { return m_leftY; }
    double rightX() const { return m_rightX; }
    double rightY() const { return m_rightY; }
    double leftTrigger() const { return m_leftTrigger; }
    double rightTrigger() const { return m_rightTrigger; }
    double gyroX() const { return m_gyro[0]; }
    double gyroY() const { return m_gyro[1]; }
    double gyroZ() const { return m_gyro[2]; }
    QStringList pressed() const { return m_pressed.values(); }

    Q_INVOKABLE bool buttonDown(const QString &name) const { return m_pressed.contains(name); }
    // Whether any of a keymap action's "Pad+" chords is held right now (fly mode's held actions).
    Q_INVOKABLE bool chordHeld(const QString &chord) const;
    Q_INVOKABLE void rumble(double low, double high, int ms);
    // A short pulse for an editing event ("cut", "snap"), when haptics are on.
    Q_INVOKABLE void haptic(const QString &kind);

    // SDL reports a controller whichever window has focus; a backend that can see input while the
    // app is in the background must drop it, so a pad in another game does not edit.
    bool acceptsInput() const;

    // --- Fed by a backend ------------------------------------------------------------------------

    // Raw sticks in -1..1 with y positive downwards, triggers 0..1.
    void ingestAxes(double lx, double ly, double rx, double ry, double lt, double rt);
    void ingestButton(const QString &name, bool down);
    void ingestGyro(double x, double y, double z);
    void ingestDevices(const QVariantList &devices);

signals:
    void capturingChanged();
    void devicesChanged();
    void settingsChanged();
    void stateChanged();
    void buttonPressed(const QString &chord);

private:
    void start();
    void applyPolling();
    void flushState();

    std::unique_ptr<GamepadBackend> m_backend;
    QTimer *m_poll = nullptr;
    QVariantList m_devices;
    QSet<QString> m_pressed;
    bool m_capturing = false;
    bool m_enabled = true;
    double m_deadZone = 0.15;
    double m_curve = 1.5;
    double m_lookSpeed = 140.0;
    bool m_gyroEnabled = false;
    double m_gyroSensitivity = 1.0;
    bool m_hapticsEnabled = true;
    double m_leftX = 0, m_leftY = 0, m_rightX = 0, m_rightY = 0;
    double m_leftTrigger = 0, m_rightTrigger = 0;
    double m_gyro[3] = {0, 0, 0};
    bool m_stateDirty = false;
    qint64 m_lastRumbleMs = 0;
};

std::unique_ptr<GamepadBackend> makeSdlGamepadBackend(Gamepad &service);

} // namespace drift
