#pragma once

#include "app/surfaces/SurfaceTargets.h"

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QTimer>
#include <QUrl>
#include <QVariantList>

#include <cmath>

namespace drift {

struct SurfaceControl {
    enum Kind { Button, Absolute, Relative };

    QString id;
    QString label;
    Kind kind = Button;
};

struct SurfaceBindingDef {
    QString control;
    QString target;
    QString label;
    bool invert = false;
    double sensitivity = 1.0;
    // For an Absolute control that is really an endless encoder sending a counter: "twos" (1 up,
    // 127 down), "signmag" (bit 6 is the sign) or "offset" (64 is still). Empty reads it as a position.
    QString mode;
};

struct SurfaceDevice {
    QString id;
    QString name;
    QList<SurfaceControl> controls;
    // A motorized device follows the app (feedback moves its faders), so absolute controls apply
    // directly instead of waiting to catch up with the value.
    bool motorized = false;
    bool feedback = false;
    // Applied only the first time the device is seen with no saved bindings.
    QList<SurfaceBindingDef> defaults;
};

// Every physical or virtual control surface (MIDI, OSC, the phone remote) registers its controls
// here and reports their movements; bindings turn those into editor actions and parameters.
class ControlSurfaceManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList devices READ devices NOTIFY devicesChanged)
    // While on, parameters in the UI can be clicked to arm them for binding.
    Q_PROPERTY(bool learnMode READ learnMode WRITE setLearnMode NOTIFY learnChanged)
    // The armed target, waiting for a control to move. Empty when nothing is armed.
    Q_PROPERTY(QString learnTarget READ learnTarget NOTIFY learnChanged)
    Q_PROPERTY(QString learnLabel READ learnLabel NOTIFY learnChanged)

public:
    explicit ControlSurfaceManager(SurfaceTargets *targets, QObject *parent = nullptr);

    QVariantList devices() const;
    bool learnMode() const { return m_learnMode; }
    void setLearnMode(bool on);
    QString learnTarget() const { return m_learnTarget; }
    QString learnLabel() const { return m_learnLabel; }

    void registerDevice(const SurfaceDevice &device);
    void unregisterDevice(const QString &deviceId);
    void ensureControl(const QString &deviceId, const SurfaceControl &control);

    void button(const QString &deviceId, const QString &controlId, bool down);
    void absolute(const QString &deviceId, const QString &controlId, double normalized);
    void relative(const QString &deviceId, const QString &controlId, double ticks);
    // A touched motorized fader must not be fought by feedback.
    void setTouched(const QString &deviceId, const QString &controlId, bool touched);

    SurfaceTargets *targetProvider() const { return m_targets; }
    QStringList stripLabels() const { return m_targets->stripLabels(); }

    Q_INVOKABLE QVariantList bindings(const QString &deviceId) const;
    Q_INVOKABLE QVariantList targets() const;
    Q_INVOKABLE void armLearn(const QString &targetId, const QString &label = QString());
    // Arms a UI property row: a clip property key ("opacity") or an effect parameter ("fx.2.amount").
    Q_INVOKABLE void armLearnProperty(const QString &key, const QString &label, double min, double max);
    Q_INVOKABLE void cancelLearn();
    Q_INVOKABLE void bind(const QString &deviceId, const QString &controlId, const QString &targetId,
                          const QString &label = QString());
    Q_INVOKABLE void removeBinding(const QString &deviceId, const QString &controlId);
    Q_INVOKABLE void setBindingOption(const QString &deviceId, const QString &controlId,
                                      const QString &key, const QVariant &value);
    Q_INVOKABLE void clearBindings(const QString &deviceId);
    Q_INVOKABLE void forgetDevice(const QString &deviceId);
    Q_INVOKABLE QString exportJson(const QString &deviceId = QString()) const;
    // Returns the number of bindings read, -1 when the text is not a bindings file. A non-empty
    // deviceId loads the file's first device onto that device instead.
    Q_INVOKABLE int importJson(const QString &json, const QString &deviceId = QString());
    Q_INVOKABLE bool exportToFile(const QUrl &file, const QString &deviceId = QString()) const;
    Q_INVOKABLE int importFromFile(const QUrl &file, const QString &deviceId = QString());
    Q_INVOKABLE void resetPickup();

signals:
    void devicesChanged();
    void learnChanged();
    void learned(const QString &deviceId, const QString &controlId, const QString &targetId);
    // Not a parameter of its own: a keymap action id, for the shell to dispatch.
    void actionRequested(const QString &actionId);
    void inputSeen(const QString &deviceId, const QString &controlId, double value);
    // Device-side value of a control, 0..1, for feedback-capable devices.
    void feedback(const QString &deviceId, const QString &controlId, double value);
    // Strip names (mixer bank) changed; Mackie scribble strips follow it.
    void stripLabelsChanged();

private:
    struct Binding : SurfaceBindingDef {
        bool pickedUp = false;
        double last = std::nan("");
    };
    struct DeviceState {
        SurfaceDevice info;
        bool connected = false;
        QList<Binding> bindings;
    };

    static constexpr double kPickupWindow = 0.02;

    void input(const QString &deviceId, const QString &controlId, SurfaceControl::Kind kind, double v);
    void apply(bool motorized, Binding &b, SurfaceControl::Kind kind, double v);
    void applyRelative(Binding &b, double ticks);
    void trigger(const QString &target);
    Binding *find(const QString &deviceId, const QString &controlId);
    void bindInternal(const QString &deviceId, const QString &controlId, const QString &targetId,
                      const QString &label);
    void changed();
    void load();
    void save() const;
    QJsonObject deviceJson(const QString &id, const DeviceState &state) const;
    void pushFeedback();
    void updateFeedbackTimer();

    SurfaceTargets *m_targets;
    QHash<QString, DeviceState> m_devices;
    QStringList m_order;
    QSet<QString> m_seen;
    QHash<QString, double> m_sent;
    QSet<QString> m_touched;
    QTimer m_feedbackTimer;
    bool m_learnMode = false;
    QString m_learnTarget;
    QString m_learnLabel;
    bool m_loading = false;
};

} // namespace drift
