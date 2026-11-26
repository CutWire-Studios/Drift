#pragma once

#include "app/surfaces/ControlSurfaceManager.h"
#include "app/surfaces/osc/OscCodec.h"

#include <QHostAddress>
#include <QTimer>
#include <QUdpSocket>

namespace drift {

// OSC over UDP: the fixed /drift/... address space (docs/osc.md), plus any other address a
// controller sends, which becomes a control of the "osc" surface device so TouchOSC or Open Stage
// Control layouts can be bound with learn. Feedback goes back to the last sender.
class OscSurfaceService : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)
    Q_PROPERTY(int port READ port WRITE setPort NOTIFY changed)
    Q_PROPERTY(int feedbackPort READ feedbackPort WRITE setFeedbackPort NOTIFY changed)
    // One host or CIDR range per line. Empty accepts the local network only.
    Q_PROPERTY(QString allowedHosts READ allowedHosts WRITE setAllowedHosts NOTIFY changed)
    // Empty while listening, else why it is not.
    Q_PROPERTY(QString error READ error NOTIFY changed)

public:
    static constexpr const char *kDeviceId = "osc";

    explicit OscSurfaceService(ControlSurfaceManager *manager, QObject *parent = nullptr);

    bool enabled() const { return m_enabled; }
    void setEnabled(bool on);
    int port() const { return m_port; }
    void setPort(int port);
    int feedbackPort() const { return m_feedbackPort; }
    void setFeedbackPort(int port);
    QString allowedHosts() const { return m_allowedHosts; }
    void setAllowedHosts(const QString &hosts);
    QString error() const { return m_error; }

    // Loopback, private and link-local addresses when `rules` is empty; else any rule that matches.
    static bool hostAllowed(const QHostAddress &host, const QString &rules);
    // Handles one parsed message as if it had arrived from `sender`.
    void handle(const OscMessage &message, const QHostAddress &sender);

signals:
    void changed();

private:
    void applyListening();
    void readPending();
    void sendTo(const OscMessage &message);
    void handleDrift(const QStringList &path, const OscMessage &message);
    void handleControl(const OscMessage &message);
    void tickState();

    ControlSurfaceManager *m_manager;
    QUdpSocket m_socket;
    QTimer m_stateTimer;
    QHostAddress m_peer;
    bool m_enabled = false;
    int m_port = 8000;
    int m_feedbackPort = 9000;
    QString m_allowedHosts;
    QString m_error;
    double m_lastPlayhead = -1.0;
    int m_lastPlaying = -1;
};

} // namespace drift
