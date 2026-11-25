#include "app/surfaces/remote/PhoneRemoteClient.h"

#include "app/surfaces/remote/PhoneRemoteServer.h"

#include <QDateTime>
#include <QJsonArray>
#include <QSettings>
#include <QSysInfo>

#ifdef Q_OS_ANDROID
#include <QJniEnvironment>
#include <QJniObject>
#include <QMetaObject>
#include <QPointer>
#include <QtCore/qcoreapplication_platform.h>
#endif

namespace drift {

namespace {

const QString kHostKey = QStringLiteral("remote/host");
const QString kPortKey = QStringLiteral("remote/port");
const QString kKeyKey = QStringLiteral("remote/key");
constexpr int kRetryMs = 3000;
constexpr qint64 kGyroIntervalMs = 33;
// Yaw and pitch rates of a steady wrist turn are around 1 rad/s; this makes that a brisk pan.
constexpr double kGyroTicksPerRadian = 90.0;

#ifdef Q_OS_ANDROID
constexpr const char *kSensorsClass = "org/cutwire/drift/RemoteSensors";
QPointer<PhoneRemoteClient> g_client;

// Called on the sensor thread.
void JNICALL nativeGyro(JNIEnv *, jclass, jfloat x, jfloat y, jfloat z)
{
    if (!g_client)
        return;
    QMetaObject::invokeMethod(g_client.data(), [x, y, z] {
        if (g_client)
            g_client->gyroSample(x, y, z);
    }, Qt::QueuedConnection);
}
#endif

} // namespace

PhoneRemoteClient::PhoneRemoteClient(QObject *parent)
    : QObject(parent)
{
    QSettings settings;
    m_host = settings.value(kHostKey).toString();
    m_port = settings.value(kPortKey).toInt();
    m_key = settings.value(kKeyKey).toString();

    connect(&m_socket, &QTcpSocket::connected, this, &PhoneRemoteClient::onConnected);
    connect(&m_socket, &QTcpSocket::readyRead, this, &PhoneRemoteClient::onReadyRead);
    connect(&m_socket, &QTcpSocket::disconnected, this, &PhoneRemoteClient::onClosed);
    connect(&m_socket, &QTcpSocket::errorOccurred, this, [this] {
        if (m_socket.state() == QAbstractSocket::UnconnectedState) {
            m_error = m_socket.errorString();
            onClosed();
        }
    });
    m_retry.setInterval(kRetryMs);
    m_retry.setSingleShot(true);
    connect(&m_retry, &QTimer::timeout, this, [this] {
        if (m_wantConnected && !m_host.isEmpty())
            connectNow(m_host, m_port, QString());
    });
#ifdef Q_OS_ANDROID
    g_client = this;
#endif
}

QString PhoneRemoteClient::state() const
{
    if (m_welcomed)
        return QStringLiteral("connected");
    return m_socket.state() == QAbstractSocket::UnconnectedState ? QStringLiteral("disconnected")
                                                                  : QStringLiteral("connecting");
}

bool PhoneRemoteClient::gyroAvailable() const
{
#ifdef Q_OS_ANDROID
    return true;
#else
    return false;
#endif
}

void PhoneRemoteClient::setGyro(bool on)
{
    if (m_gyro == on || (on && !gyroAvailable()))
        return;
    m_gyro = on;
#ifdef Q_OS_ANDROID
    QJniObject context = QNativeInterface::QAndroidApplication::context();
    if (on) {
        const JNINativeMethod methods[] = {{"nativeGyro", "(FFF)V", reinterpret_cast<void *>(nativeGyro)}};
        QJniEnvironment().registerNativeMethods(kSensorsClass, methods, 1);
        QJniObject::callStaticMethod<jboolean>(kSensorsClass, "start", "(Landroid/content/Context;)Z",
                                               context.object());
    } else {
        QJniObject::callStaticMethod<void>(kSensorsClass, "stop", "()V");
    }
#endif
    emit gyroChanged();
}

void PhoneRemoteClient::gyroSample(double x, double y, double z)
{
    Q_UNUSED(z)
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (!m_gyro || !m_welcomed || m_lastGyroMs == 0) {
        m_lastGyroMs = now;
        return;
    }
    const double dt = (now - m_lastGyroMs) / 1000.0;
    if (now - m_lastGyroMs < kGyroIntervalMs)
        return;
    m_lastGyroMs = now;
    // Held upright in landscape: rotation about the phone's long axis steers left and right, about
    // the short one up and down.
    relative(QStringLiteral("gyro.x"), -y * dt * kGyroTicksPerRadian);
    relative(QStringLiteral("gyro.y"), -x * dt * kGyroTicksPerRadian);
}

bool PhoneRemoteClient::pair(const QString &code)
{
    QString host;
    int port = 0;
    QString token;
    if (!PhoneRemoteServer::parsePairingCode(code, &host, &port, &token))
        return false;
    m_wantConnected = true;
    connectNow(host, port, token);
    return true;
}

void PhoneRemoteClient::reconnect()
{
    if (m_host.isEmpty() || m_key.isEmpty())
        return;
    m_wantConnected = true;
    connectNow(m_host, m_port, QString());
}

void PhoneRemoteClient::connectNow(const QString &host, int port, const QString &token)
{
    m_socket.abort();
    m_welcomed = false;
    m_reader = remote::FrameReader();
    m_host = host;
    m_port = port;
    m_token = token;
    m_error.clear();
    m_socket.connectToHost(host, static_cast<quint16>(port));
    emit stateChanged();
}

void PhoneRemoteClient::disconnectFromDesktop()
{
    m_wantConnected = false;
    m_retry.stop();
    m_socket.abort();
    onClosed();
}

void PhoneRemoteClient::forget()
{
    disconnectFromDesktop();
    m_key.clear();
    QSettings settings;
    settings.remove(kHostKey);
    settings.remove(kPortKey);
    settings.remove(kKeyKey);
    emit stateChanged();
}

void PhoneRemoteClient::onConnected()
{
    QJsonObject hello{{"t", "hello"}, {"name", QSysInfo::machineHostName()}};
    if (!m_token.isEmpty())
        hello.insert("token", m_token);
    else
        hello.insert("key", m_key);
    send(hello);
}

void PhoneRemoteClient::onClosed()
{
    const bool was = m_welcomed;
    m_welcomed = false;
    if (m_wantConnected && !m_key.isEmpty())
        m_retry.start();
    if (was || m_socket.state() == QAbstractSocket::UnconnectedState)
        emit stateChanged();
}

void PhoneRemoteClient::send(const QJsonObject &message)
{
    if (m_socket.state() == QAbstractSocket::ConnectedState)
        m_socket.write(remote::encodeFrame(message));
}

void PhoneRemoteClient::onReadyRead()
{
    m_reader.append(m_socket.readAll());
    QJsonObject m;
    while (m_reader.take(&m)) {
        const QString type = m.value("t").toString();
        if (type == QLatin1String("paired")) {
            m_key = m.value("key").toString();
            m_token.clear();
            QSettings settings;
            settings.setValue(kHostKey, m_host);
            settings.setValue(kPortKey, m_port);
            settings.setValue(kKeyKey, m_key);
        } else if (type == QLatin1String("welcome")) {
            m_welcomed = true;
            m_hostName = m.value("name").toString();
            emit stateChanged();
        } else if (type == QLatin1String("denied")) {
            m_error = tr("The desktop did not accept this code. Pair again from Settings > Control surfaces.");
            m_wantConnected = false;
            emit stateChanged();
        } else if (type == QLatin1String("fb")) {
            const QString control = m.value("c").toString();
            const double value = m.value("v").toDouble();
            m_values.insert(control, value);
            emit valuesChanged();
            emit feedback(control, value);
        } else if (type == QLatin1String("state")) {
            m_playing = m.value("playing").toBool();
            m_playhead = m.value("playhead").toDouble();
            emit transportChanged();
        } else if (type == QLatin1String("labels")) {
            m_labels.clear();
            for (const QJsonValue &v : m.value("names").toArray())
                m_labels.append(v.toString());
            emit labelsChanged();
        }
    }
    if (m_reader.broken())
        m_socket.abort();
}

void PhoneRemoteClient::button(const QString &control, bool down)
{
    send({{"t", "btn"}, {"c", control}, {"d", down ? 1 : 0}});
}

void PhoneRemoteClient::absolute(const QString &control, double value)
{
    send({{"t", "abs"}, {"c", control}, {"v", value}});
}

void PhoneRemoteClient::relative(const QString &control, double value)
{
    send({{"t", "rel"}, {"c", control}, {"v", value}});
}

} // namespace drift
