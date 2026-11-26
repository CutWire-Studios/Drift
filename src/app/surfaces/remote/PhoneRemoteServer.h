#pragma once

#include "app/surfaces/ControlSurfaceManager.h"
#include "app/surfaces/remote/RemoteProtocol.h"

#include <QDateTime>
#include <QHash>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QVariantList>

#include <map>
#include <memory>

namespace drift {

// The desktop end of the phone remote: pairs a phone with a one-time token shown as a QR code,
// then registers it as a control surface device and relays its controls and the editor's feedback.
class PhoneRemoteServer : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)
    Q_PROPERTY(bool pairing READ pairing NOTIFY changed)
    // host:port/TOKEN, the same text the QR code carries, for typing in by hand.
    Q_PROPERTY(QString pairingCode READ pairingCode NOTIFY changed)
    // Rows of "0" and "1" for the QR code of the pairing link.
    Q_PROPERTY(QStringList pairingQr READ pairingQr NOTIFY changed)
    // [{id, name, connected}]
    Q_PROPERTY(QVariantList phones READ phones NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)

public:
    explicit PhoneRemoteServer(ControlSurfaceManager *manager, QObject *parent = nullptr);

    bool enabled() const { return m_enabled; }
    void setEnabled(bool on);
    bool pairing() const { return !m_token.isEmpty() && QDateTime::currentSecsSinceEpoch() < m_tokenExpires; }
    QString pairingCode() const;
    QStringList pairingQr() const;
    QVariantList phones() const;
    QString error() const { return m_error; }
    quint16 port() const { return m_server.serverPort(); }

    Q_INVOKABLE void startPairing();
    Q_INVOKABLE void stopPairing();
    Q_INVOKABLE void forget(const QString &phoneId);

    static QString deviceId(const QString &phoneId) { return QStringLiteral("phone:") + phoneId; }
    static QString lanAddress();
    // The pairing link a QR code carries, and the typed form; both parse back to host, port, token.
    static QString pairingLink(const QString &host, int port, const QString &token);
    static bool parsePairingCode(const QString &text, QString *host, int *port, QString *token);

signals:
    void changed();

private:
    struct Connection {
        QTcpSocket *socket = nullptr;
        remote::FrameReader reader;
        QString phoneId;
        int denied = 0;
    };
    struct Phone {
        QString id;
        QString name;
        QString key;
    };

    void listen();
    void onNewConnection();
    void onReadyRead(QTcpSocket *socket);
    void onDisconnected(QTcpSocket *socket);
    void handle(Connection &c, const QJsonObject &message);
    void hello(Connection &c, const QJsonObject &message);
    void send(QTcpSocket *socket, const QJsonObject &message);
    void sendState();
    void loadPhones();
    void savePhones() const;

    ControlSurfaceManager *m_manager;
    // Declared before the server so it outlives the sockets the server deletes, whose disconnected
    // signals still look their entry up.
    std::map<QTcpSocket *, std::unique_ptr<Connection>> m_connections;
    QTcpServer m_server;
    QList<Phone> m_phones;
    QTimer m_stateTimer;
    QString m_token;
    qint64 m_tokenExpires = 0;
    int m_failedPairings = 0;
    QString m_error;
    bool m_enabled = false;
    double m_lastPlayhead = -1.0;
    int m_lastPlaying = -1;
};

} // namespace drift
