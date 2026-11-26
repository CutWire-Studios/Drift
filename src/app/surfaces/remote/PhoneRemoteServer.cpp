#include "app/surfaces/remote/PhoneRemoteServer.h"

#include "app/surfaces/remote/QrCode.h"

#include <QHostInfo>
#include <QJsonArray>
#include <QNetworkInterface>
#include <QPointer>
#include <QRandomGenerator>
#include <QSettings>
#include <QUuid>

#include <algorithm>
#include <cmath>

namespace drift {

namespace {

const QString kEnabledKey = QStringLiteral("surfaces/remote/enabled");
const QString kPhonesKey = QStringLiteral("surfaces/remote/phones");
constexpr int kTokenSeconds = 300;
constexpr int kAuthSeconds = 10;
constexpr int kMaxFailedPairings = 5;
const char kTokenAlphabet[] = "ABCDEFGHJKMNPQRSTUVWXYZ23456789";

QString randomToken()
{
    QString token;
    for (int i = 0; i < 8; ++i)
        token.append(QLatin1Char(kTokenAlphabet[QRandomGenerator::system()->bounded(int(sizeof(kTokenAlphabet) - 1))]));
    return token;
}

QString randomKey()
{
    QByteArray bytes(24, '\0');
    for (char &c : bytes)
        c = static_cast<char>(QRandomGenerator::system()->bounded(256));
    return QString::fromLatin1(bytes.toHex());
}

bool sameText(const QString &a, const QString &b)
{
    if (a.size() != b.size())
        return false;
    int diff = 0;
    for (int i = 0; i < a.size(); ++i)
        diff |= a.at(i).unicode() ^ b.at(i).unicode();
    return diff == 0;
}

} // namespace

PhoneRemoteServer::PhoneRemoteServer(ControlSurfaceManager *manager, QObject *parent)
    : QObject(parent), m_manager(manager)
{
    loadPhones();
    connect(&m_server, &QTcpServer::newConnection, this, &PhoneRemoteServer::onNewConnection);
    connect(m_manager, &ControlSurfaceManager::feedback, this,
            [this](const QString &device, const QString &control, double value) {
                if (!device.startsWith(QLatin1String("phone:")))
                    return;
                for (const auto &entry : m_connections) {
                    if (entry.second->phoneId == device.mid(6))
                        send(entry.first, {{"t", "fb"}, {"c", control}, {"v", value}});
                }
            });
    connect(m_manager, &ControlSurfaceManager::stripLabelsChanged, this, [this] {
        const QJsonArray names = QJsonArray::fromStringList(m_manager->stripLabels());
        for (const auto &entry : m_connections) {
            if (!entry.second->phoneId.isEmpty())
                send(entry.first, {{"t", "labels"}, {"names", names}});
        }
    });
    m_stateTimer.setInterval(100);
    connect(&m_stateTimer, &QTimer::timeout, this, &PhoneRemoteServer::sendState);

    if (QSettings().value(kEnabledKey, false).toBool()) {
        m_enabled = true;
        listen();
    }
}

void PhoneRemoteServer::loadPhones()
{
    const QJsonArray list = QJsonDocument::fromJson(QSettings().value(kPhonesKey).toByteArray()).array();
    for (const QJsonValue &v : list) {
        const QJsonObject o = v.toObject();
        m_phones.append({o.value("id").toString(), o.value("name").toString(), o.value("key").toString()});
    }
}

void PhoneRemoteServer::savePhones() const
{
    QJsonArray list;
    for (const Phone &p : m_phones)
        list.append(QJsonObject{{"id", p.id}, {"name", p.name}, {"key", p.key}});
    QSettings().setValue(kPhonesKey, QJsonDocument(list).toJson(QJsonDocument::Compact));
}

void PhoneRemoteServer::setEnabled(bool on)
{
    if (m_enabled == on)
        return;
    m_enabled = on;
    QSettings().setValue(kEnabledKey, on);
    if (on) {
        listen();
    } else {
        m_server.close();
        m_stateTimer.stop();
        m_token.clear();
        for (const auto &entry : m_connections)
            entry.first->disconnectFromHost();
    }
    emit changed();
}

void PhoneRemoteServer::listen()
{
    m_error.clear();
    if (m_server.isListening())
        return;
    if (!m_server.listen(QHostAddress::Any, remote::kDefaultPort) && !m_server.listen(QHostAddress::Any, 0))
        m_error = m_server.errorString();
    else
        m_stateTimer.start();
}

QString PhoneRemoteServer::lanAddress()
{
    QString fallback;
    for (const QNetworkInterface &nic : QNetworkInterface::allInterfaces()) {
        if (!(nic.flags() & QNetworkInterface::IsUp) || !(nic.flags() & QNetworkInterface::IsRunning)
            || (nic.flags() & QNetworkInterface::IsLoopBack))
            continue;
        for (const QNetworkAddressEntry &entry : nic.addressEntries()) {
            const QHostAddress a = entry.ip();
            if (a.protocol() != QAbstractSocket::IPv4Protocol)
                continue;
            const quint32 v = a.toIPv4Address();
            if ((v >> 24) == 10 || (v >> 20) == 0xAC1 || (v >> 16) == 0xC0A8)
                return a.toString();
            if (fallback.isEmpty())
                fallback = a.toString();
        }
    }
    return fallback;
}

QString PhoneRemoteServer::pairingLink(const QString &host, int port, const QString &token)
{
    return QStringLiteral("drift://pair/%1:%2/%3").arg(host).arg(port).arg(token);
}

bool PhoneRemoteServer::parsePairingCode(const QString &text, QString *host, int *port, QString *token)
{
    QString t = text.trimmed();
    const QString prefix = QStringLiteral("drift://pair/");
    if (t.startsWith(prefix, Qt::CaseInsensitive))
        t = t.mid(prefix.size());
    const int slash = t.indexOf(QLatin1Char('/'));
    const int colon = t.lastIndexOf(QLatin1Char(':'), slash < 0 ? -1 : slash);
    if (slash <= 0 || colon <= 0 || colon > slash)
        return false;
    bool ok = false;
    const int p = t.mid(colon + 1, slash - colon - 1).toInt(&ok);
    if (!ok || p < 1 || p > 65535 || slash + 1 >= t.size())
        return false;
    *host = t.left(colon);
    *port = p;
    *token = t.mid(slash + 1).toUpper();
    return true;
}

QString PhoneRemoteServer::pairingCode() const
{
    if (!pairing() || !m_server.isListening())
        return {};
    return QStringLiteral("%1:%2/%3").arg(lanAddress()).arg(m_server.serverPort()).arg(m_token);
}

QStringList PhoneRemoteServer::pairingQr() const
{
    QStringList rows;
    if (!pairing() || !m_server.isListening())
        return rows;
    const QrCode code = QrCode::encode(pairingLink(lanAddress(), m_server.serverPort(), m_token).toUtf8());
    for (int y = 0; y < code.size(); ++y) {
        QString row;
        for (int x = 0; x < code.size(); ++x)
            row.append(code.dark(x, y) ? QLatin1Char('1') : QLatin1Char('0'));
        rows.append(row);
    }
    return rows;
}

QVariantList PhoneRemoteServer::phones() const
{
    QVariantList out;
    for (const Phone &p : m_phones) {
        bool connected = false;
        for (const auto &entry : m_connections)
            connected |= entry.second->phoneId == p.id;
        out.append(QVariantMap{{"id", p.id}, {"name", p.name}, {"connected", connected}});
    }
    return out;
}

void PhoneRemoteServer::startPairing()
{
    setEnabled(true);
    listen();
    m_token = randomToken();
    m_tokenExpires = QDateTime::currentSecsSinceEpoch() + kTokenSeconds;
    m_failedPairings = 0;
    QTimer::singleShot(kTokenSeconds * 1000 + 100, this, [this] { emit changed(); });
    emit changed();
}

void PhoneRemoteServer::stopPairing()
{
    m_token.clear();
    emit changed();
}

void PhoneRemoteServer::forget(const QString &phoneId)
{
    for (const auto &entry : m_connections) {
        if (entry.second->phoneId == phoneId)
            entry.first->disconnectFromHost();
    }
    m_phones.removeIf([&](const Phone &p) { return p.id == phoneId; });
    savePhones();
    m_manager->forgetDevice(deviceId(phoneId));
    emit changed();
}

void PhoneRemoteServer::onNewConnection()
{
    while (QTcpSocket *socket = m_server.nextPendingConnection()) {
        socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
        auto connection = std::make_unique<Connection>();
        connection->socket = socket;
        m_connections.emplace(socket, std::move(connection));
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] { onReadyRead(socket); });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket] { onDisconnected(socket); });
        QPointer<QTcpSocket> guard(socket);
        QTimer::singleShot(kAuthSeconds * 1000, this, [this, guard] {
            if (!guard)
                return;
            const auto it = m_connections.find(guard.data());
            if (it != m_connections.end() && it->second->phoneId.isEmpty())
                guard->abort();
        });
    }
}

void PhoneRemoteServer::onReadyRead(QTcpSocket *socket)
{
    const auto it = m_connections.find(socket);
    if (it == m_connections.end())
        return;
    Connection &c = *it->second;
    c.reader.append(socket->readAll());
    QJsonObject message;
    while (m_connections.count(socket) > 0 && c.reader.take(&message))
        handle(c, message);
    if (m_connections.count(socket) > 0 && c.reader.broken())
        socket->abort();
}

void PhoneRemoteServer::onDisconnected(QTcpSocket *socket)
{
    const auto it = m_connections.find(socket);
    if (it == m_connections.end())
        return;
    const QString phoneId = it->second->phoneId;
    m_connections.erase(it);
    socket->deleteLater();
    if (!phoneId.isEmpty()) {
        bool stillConnected = false;
        for (const auto &entry : m_connections)
            stillConnected |= entry.second->phoneId == phoneId;
        if (!stillConnected)
            m_manager->unregisterDevice(deviceId(phoneId));
    }
    emit changed();
}

void PhoneRemoteServer::send(QTcpSocket *socket, const QJsonObject &message)
{
    socket->write(remote::encodeFrame(message));
}

void PhoneRemoteServer::hello(Connection &c, const QJsonObject &message)
{
    const QString name = message.value("name").toString().left(40);
    const QString token = message.value("token").toString().toUpper();
    const QString key = message.value("key").toString();
    const Phone *phone = nullptr;

    if (!token.isEmpty()) {
        if (pairing() && sameText(token, m_token)) {
            m_phones.append({QUuid::createUuid().toString(QUuid::WithoutBraces), name.isEmpty() ? tr("Phone") : name,
                             randomKey()});
            savePhones();
            m_token.clear();
            phone = &m_phones.last();
            send(c.socket, {{"t", "paired"}, {"id", phone->id}, {"key", phone->key}});
        } else if (++m_failedPairings >= kMaxFailedPairings) {
            m_token.clear();
        }
    } else if (!key.isEmpty()) {
        for (const Phone &p : m_phones) {
            if (sameText(p.key, key))
                phone = &p;
        }
    }

    if (!phone) {
        send(c.socket, {{"t", "denied"}});
        c.socket->disconnectFromHost();
        emit changed();
        return;
    }

    for (const auto &entry : m_connections) {
        if (entry.second.get() != &c && entry.second->phoneId == phone->id)
            entry.first->abort();
    }
    c.phoneId = phone->id;
    SurfaceDevice device;
    device.id = deviceId(phone->id);
    device.name = tr("Phone: %1").arg(phone->name);
    device.controls = remote::controls();
    device.motorized = true;
    device.feedback = true;
    device.defaults = remote::defaults();
    m_manager->registerDevice(device);
    send(c.socket, {{"t", "welcome"}, {"name", QHostInfo::localHostName()}});
    send(c.socket, {{"t", "labels"}, {"names", QJsonArray::fromStringList(m_manager->stripLabels())}});
    m_lastPlaying = -1;
    emit changed();
}

void PhoneRemoteServer::handle(Connection &c, const QJsonObject &message)
{
    const QString type = message.value("t").toString();
    if (type == QLatin1String("hello")) {
        if (c.phoneId.isEmpty())
            hello(c, message);
        return;
    }
    if (c.phoneId.isEmpty())
        return;
    const QString device = deviceId(c.phoneId);
    const QString control = message.value("c").toString();
    if (type == QLatin1String("btn"))
        m_manager->button(device, control, message.value("d").toInt() != 0);
    else if (type == QLatin1String("abs"))
        m_manager->absolute(device, control, message.value("v").toDouble());
    else if (type == QLatin1String("rel"))
        m_manager->relative(device, control, message.value("v").toDouble());
}

void PhoneRemoteServer::sendState()
{
    const double playhead = m_manager->targetProvider()->playheadSeconds();
    const double playing = m_manager->targetProvider()->current(QStringLiteral("playPause"));
    const int playingInt = std::isnan(playing) ? -1 : static_cast<int>(playing);
    if (std::abs(playhead - m_lastPlayhead) < 1e-6 && playingInt == m_lastPlaying)
        return;
    m_lastPlayhead = playhead;
    m_lastPlaying = playingInt;
    for (const auto &entry : m_connections) {
        if (!entry.second->phoneId.isEmpty())
            send(entry.first, {{"t", "state"}, {"playing", playingInt == 1}, {"playhead", playhead}});
    }
}

} // namespace drift
