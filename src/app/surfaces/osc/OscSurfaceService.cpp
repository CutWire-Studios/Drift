#include "app/surfaces/osc/OscSurfaceService.h"

#include <QNetworkDatagram>
#include <QRegularExpression>
#include <QSettings>

#include <algorithm>
#include <cmath>

namespace drift {

namespace {

const QString kEnabledKey = QStringLiteral("surfaces/osc/enabled");
const QString kPortKey = QStringLiteral("surfaces/osc/port");
const QString kFeedbackPortKey = QStringLiteral("surfaces/osc/feedbackPort");
const QString kHostsKey = QStringLiteral("surfaces/osc/allowedHosts");

bool isLocalNetwork(const QHostAddress &host)
{
    if (host.isLoopback())
        return true;
    bool v4 = false;
    const quint32 a = host.toIPv4Address(&v4);
    if (v4) {
        return (a >> 24) == 10 || (a >> 20) == 0xAC1 || (a >> 16) == 0xC0A8 || (a >> 16) == 0xA9FE;
    }
    return host.isInSubnet(QHostAddress::parseSubnet(QStringLiteral("fc00::/7")))
           || host.isInSubnet(QHostAddress::parseSubnet(QStringLiteral("fe80::/10")));
}

double firstNumber(const OscMessage &m, double fallback)
{
    if (m.args.isEmpty())
        return fallback;
    const QVariant &v = m.args.first();
    if (v.typeId() == QMetaType::Bool)
        return v.toBool() ? 1.0 : 0.0;
    bool ok = false;
    const double d = v.toDouble(&ok);
    return ok ? d : fallback;
}

} // namespace

OscSurfaceService::OscSurfaceService(ControlSurfaceManager *manager, QObject *parent)
    : QObject(parent), m_manager(manager)
{
    QSettings settings;
    m_port = std::clamp(settings.value(kPortKey, 8000).toInt(), 1, 65535);
    m_feedbackPort = std::clamp(settings.value(kFeedbackPortKey, 9000).toInt(), 1, 65535);
    m_allowedHosts = settings.value(kHostsKey).toString();
    m_enabled = settings.value(kEnabledKey, false).toBool();

    connect(&m_socket, &QUdpSocket::readyRead, this, &OscSurfaceService::readPending);
    connect(m_manager, &ControlSurfaceManager::feedback, this,
            [this](const QString &device, const QString &control, double value) {
                if (device == QLatin1String(kDeviceId))
                    sendTo({control, {value}});
            });
    m_stateTimer.setInterval(100);
    connect(&m_stateTimer, &QTimer::timeout, this, &OscSurfaceService::tickState);
    applyListening();
}

void OscSurfaceService::setEnabled(bool on)
{
    if (m_enabled == on)
        return;
    m_enabled = on;
    QSettings().setValue(kEnabledKey, on);
    applyListening();
}

void OscSurfaceService::setPort(int port)
{
    port = std::clamp(port, 1, 65535);
    if (m_port == port)
        return;
    m_port = port;
    QSettings().setValue(kPortKey, port);
    applyListening();
}

void OscSurfaceService::setFeedbackPort(int port)
{
    port = std::clamp(port, 1, 65535);
    if (m_feedbackPort == port)
        return;
    m_feedbackPort = port;
    QSettings().setValue(kFeedbackPortKey, port);
    emit changed();
}

void OscSurfaceService::setAllowedHosts(const QString &hosts)
{
    if (m_allowedHosts == hosts)
        return;
    m_allowedHosts = hosts;
    QSettings().setValue(kHostsKey, hosts);
    emit changed();
}

void OscSurfaceService::applyListening()
{
    m_socket.close();
    m_stateTimer.stop();
    m_manager->unregisterDevice(QLatin1String(kDeviceId));
    m_error.clear();
    if (m_enabled) {
        if (m_socket.bind(QHostAddress::Any, static_cast<quint16>(m_port))) {
            SurfaceDevice d;
            d.id = QLatin1String(kDeviceId);
            d.name = tr("OSC (port %1)").arg(m_port);
            d.motorized = true;
            d.feedback = true;
            m_manager->registerDevice(d);
            m_stateTimer.start();
        } else {
            m_error = m_socket.errorString();
        }
    }
    emit changed();
}

bool OscSurfaceService::hostAllowed(const QHostAddress &host, const QString &rules)
{
    QHostAddress h = host;
    bool v4 = false;
    const quint32 a = host.toIPv4Address(&v4);
    if (v4)
        h = QHostAddress(a);
    const QStringList lines = rules.split(QRegularExpression(QStringLiteral("[\\s,;]+")), Qt::SkipEmptyParts);
    if (lines.isEmpty())
        return isLocalNetwork(h);
    for (const QString &rule : lines) {
        if (rule.contains(QLatin1Char('/'))) {
            if (h.isInSubnet(QHostAddress::parseSubnet(rule)))
                return true;
        } else if (h == QHostAddress(rule)) {
            return true;
        }
    }
    return false;
}

void OscSurfaceService::readPending()
{
    while (m_socket.hasPendingDatagrams()) {
        const QNetworkDatagram datagram = m_socket.receiveDatagram();
        if (!hostAllowed(datagram.senderAddress(), m_allowedHosts))
            continue;
        m_peer = datagram.senderAddress();
        for (const OscMessage &message : parseOscPacket(datagram.data()))
            handle(message, m_peer);
    }
}

void OscSurfaceService::sendTo(const OscMessage &message)
{
    if (m_peer.isNull() || m_socket.state() != QAbstractSocket::BoundState)
        return;
    m_socket.writeDatagram(encodeOscMessage(message), m_peer, static_cast<quint16>(m_feedbackPort));
}

void OscSurfaceService::handle(const OscMessage &message, const QHostAddress &sender)
{
    Q_UNUSED(sender)
    const QStringList path = message.address.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (!path.isEmpty() && path.first() == QLatin1String("drift"))
        handleDrift(path.mid(1), message);
    else
        handleControl(message);
}

void OscSurfaceService::handleDrift(const QStringList &path, const OscMessage &message)
{
    const double value = firstNumber(message, 1.0);
    if (path.size() == 2 && path.at(0) == QLatin1String("transport")) {
        if (value <= 0.0)
            return;
        if (path.at(1) == QLatin1String("play"))
            m_manager->runAction(QStringLiteral("playPause"));
        else if (path.at(1) == QLatin1String("stop"))
            m_manager->runAction(QStringLiteral("stop"));
        return;
    }
    if (path.size() == 2 && path.at(0) == QLatin1String("action")) {
        if (value > 0.0)
            m_manager->runAction(path.at(1));
        return;
    }
    if (path.size() == 2 && path.at(0) == QLatin1String("param")) {
        const SurfaceTarget target = m_manager->targetProvider()->describe(path.at(1));
        if (target.kind == SurfaceTarget::Absolute)
            m_manager->setTargetAbsolute(target.id, value);
        else if (target.kind == SurfaceTarget::Relative)
            m_manager->nudgeTarget(target.id, value);
        else if (value > 0.0)
            m_manager->runAction(target.id);
        return;
    }
    if (path.size() == 1 && path.at(0) == QLatin1String("playhead")) {
        if (!message.args.isEmpty())
            m_manager->targetProvider()->seek(value);
        return;
    }
    if (path.size() == 1 && path.at(0) == QLatin1String("ping"))
        sendTo({QStringLiteral("/drift/pong"), {}});
}

void OscSurfaceService::handleControl(const OscMessage &message)
{
    const QString id = message.address;
    const bool hasNumber = !message.args.isEmpty() && message.args.first().typeId() != QMetaType::QString;
    const bool isFloat = hasNumber && (message.args.first().typeId() == QMetaType::Double
                                       || message.args.first().typeId() == QMetaType::Float);
    const QString device = QLatin1String(kDeviceId);
    if (isFloat) {
        m_manager->ensureControl(device, {id, id, SurfaceControl::Absolute});
        m_manager->absolute(device, id, firstNumber(message, 0.0));
    } else {
        m_manager->ensureControl(device, {id, id, SurfaceControl::Button});
        m_manager->button(device, id, firstNumber(message, 1.0) > 0.0);
    }
}

void OscSurfaceService::tickState()
{
    if (m_peer.isNull())
        return;
    const double playhead = m_manager->targetProvider()->playheadSeconds();
    if (!std::isnan(playhead) && std::abs(playhead - m_lastPlayhead) > 1e-6) {
        m_lastPlayhead = playhead;
        sendTo({QStringLiteral("/drift/playhead"), {playhead}});
    }
    const double playing = m_manager->targetProvider()->current(QStringLiteral("playPause"));
    if (!std::isnan(playing) && static_cast<int>(playing) != m_lastPlaying) {
        m_lastPlaying = static_cast<int>(playing);
        sendTo({QStringLiteral("/drift/transport/playing"), {m_lastPlaying}});
    }
}

} // namespace drift
