#pragma once

#include "app/surfaces/remote/RemoteProtocol.h"

#include <QObject>
#include <QTcpSocket>
#include <QTimer>
#include <QVariantMap>

namespace drift {

// The phone's end of the remote: connects to a desktop Drift, sends control movements and keeps
// the values the desktop reports back (fader positions, trackball positions, transport state).
class PhoneRemoteClient : public QObject {
    Q_OBJECT
    // "disconnected", "connecting" or "connected".
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(bool paired READ paired NOTIFY stateChanged)
    Q_PROPERTY(QString hostName READ hostName NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY transportChanged)
    Q_PROPERTY(double playhead READ playhead NOTIFY transportChanged)
    Q_PROPERTY(QVariantMap values READ values NOTIFY valuesChanged)
    Q_PROPERTY(QStringList labels READ labels NOTIFY labelsChanged)
    Q_PROPERTY(bool gyroAvailable READ gyroAvailable CONSTANT)
    Q_PROPERTY(bool gyro READ gyro WRITE setGyro NOTIFY gyroChanged)

public:
    explicit PhoneRemoteClient(QObject *parent = nullptr);

    QString state() const;
    bool paired() const { return !m_key.isEmpty(); }
    QString hostName() const { return m_hostName; }
    QString error() const { return m_error; }
    bool playing() const { return m_playing; }
    double playhead() const { return m_playhead; }
    QVariantMap values() const { return m_values; }
    QStringList labels() const { return m_labels; }
    bool gyroAvailable() const;
    bool gyro() const { return m_gyro; }
    void setGyro(bool on);

    // A pairing code or link from the desktop's "Pair phone" screen. Returns false when it does not parse.
    Q_INVOKABLE bool pair(const QString &code);
    Q_INVOKABLE void reconnect();
    Q_INVOKABLE void disconnectFromDesktop();
    Q_INVOKABLE void forget();

    Q_INVOKABLE void button(const QString &control, bool down);
    Q_INVOKABLE void absolute(const QString &control, double value);
    Q_INVOKABLE void relative(const QString &control, double value);

    // Android only: angular velocity in rad/s, from the sensor thread's queued call.
    void gyroSample(double x, double y, double z);

signals:
    void stateChanged();
    void transportChanged();
    void valuesChanged();
    void labelsChanged();
    void gyroChanged();
    void feedback(const QString &control, double value);

private:
    void connectNow(const QString &host, int port, const QString &token);
    void onConnected();
    void onReadyRead();
    void onClosed();
    void send(const QJsonObject &message);

    QTcpSocket m_socket;
    remote::FrameReader m_reader;
    QTimer m_retry;
    QString m_host;
    int m_port = 0;
    QString m_token;
    QString m_key;
    QString m_hostName;
    QString m_error;
    QVariantMap m_values;
    QStringList m_labels;
    bool m_playing = false;
    double m_playhead = 0.0;
    bool m_welcomed = false;
    bool m_wantConnected = false;
    bool m_gyro = false;
    qint64 m_lastGyroMs = 0;
};

} // namespace drift
