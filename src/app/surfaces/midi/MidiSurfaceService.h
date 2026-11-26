#pragma once

#include "app/surfaces/ControlSurfaceManager.h"
#include "app/surfaces/midi/MidiIo.h"
#include "app/surfaces/midi/MidiProtocols.h"

#include <QHash>
#include <QTimer>
#include <QVariantList>

#include <map>
#include <memory>

namespace drift {

// Opens every MIDI controller that is plugged in, registers it as a control surface and follows
// hot-plugging by rescanning the port list.
class MidiSurfaceService : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)
    // [{name, protocol, protocolLabel, open}]
    Q_PROPERTY(QVariantList ports READ ports NOTIFY changed)
    Q_PROPERTY(QVariantList protocols READ protocols CONSTANT)

public:
    MidiSurfaceService(ControlSurfaceManager *manager, MidiIo *io, QObject *parent = nullptr);

    bool enabled() const { return m_enabled; }
    void setEnabled(bool on);
    QVariantList ports() const;
    QVariantList protocols() const;

    Q_INVOKABLE void setProtocol(const QString &portName, const QString &protocol);
    QString protocolFor(const QString &portName) const;
    void poll();

signals:
    void changed();

private:
    struct Session {
        std::unique_ptr<MidiProtocol> protocol;
        QString protocolId;
    };

    static QString deviceId(const QString &portName) { return QStringLiteral("midi:") + portName; }
    void openSession(const QString &name);
    void closeSession(const QString &name);
    void onFeedback(const QString &deviceId, const QString &controlId, double value);

    ControlSurfaceManager *m_manager;
    MidiIo *m_io;
    QTimer m_timer;
    std::map<QString, Session> m_sessions;
    QStringList m_available;
    bool m_enabled = true;
};

} // namespace drift
