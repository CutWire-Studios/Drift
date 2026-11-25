#include "app/surfaces/midi/MidiSurfaceService.h"

#include <QSettings>

namespace drift {

namespace {

QString settingsKey(const QString &portName)
{
    QString key = portName;
    key.replace(QLatin1Char('/'), QLatin1Char('|'));
    return QStringLiteral("surfaces/midi/protocol/") + key;
}

// Controllers that speak Mackie Control announce it in their name.
QString guessProtocol(const QString &portName)
{
    static const QStringList mackie = {QStringLiteral("mackie"), QStringLiteral("mcu"), QStringLiteral("x-touch"),
                                       QStringLiteral("icon platform")};
    for (const QString &hint : mackie) {
        if (portName.contains(hint, Qt::CaseInsensitive))
            return QStringLiteral("mackie");
    }
    return QStringLiteral("generic");
}

} // namespace

MidiSurfaceService::MidiSurfaceService(ControlSurfaceManager *manager, MidiIo *io, QObject *parent)
    : QObject(parent), m_manager(manager), m_io(io)
{
    m_enabled = QSettings().value(QStringLiteral("surfaces/midi/enabled"), true).toBool();
    connect(m_io, &MidiIo::received, this, [this](const QString &name, const QByteArray &bytes) {
        const auto it = m_sessions.find(name);
        if (it == m_sessions.end())
            return;
        for (const QByteArray &message : splitMidiMessages(bytes))
            it->second.protocol->handle(message);
    });
    connect(m_manager, &ControlSurfaceManager::feedback, this, &MidiSurfaceService::onFeedback);
    connect(m_manager, &ControlSurfaceManager::stripLabelsChanged, this, [this] {
        for (auto &entry : m_sessions)
            entry.second.protocol->showLabels(m_manager->stripLabels());
    });
    m_timer.setInterval(1500);
    connect(&m_timer, &QTimer::timeout, this, &MidiSurfaceService::poll);
    if (m_enabled) {
        m_timer.start();
        QTimer::singleShot(0, this, &MidiSurfaceService::poll);
    }
}

void MidiSurfaceService::setEnabled(bool on)
{
    if (m_enabled == on)
        return;
    m_enabled = on;
    QSettings().setValue(QStringLiteral("surfaces/midi/enabled"), on);
    if (on) {
        m_timer.start();
        poll();
    } else {
        m_timer.stop();
        poll();
    }
    emit changed();
}

QString MidiSurfaceService::protocolFor(const QString &portName) const
{
    return QSettings().value(settingsKey(portName), guessProtocol(portName)).toString();
}

void MidiSurfaceService::setProtocol(const QString &portName, const QString &protocol)
{
    QSettings().setValue(settingsKey(portName), protocol);
    if (m_sessions.count(portName) > 0) {
        closeSession(portName);
        poll();
    }
    emit changed();
}

QVariantList MidiSurfaceService::protocols() const
{
    QVariantList out;
    for (const QString &id : {QStringLiteral("generic"), QStringLiteral("mackie"), QStringLiteral("hui")})
        out.append(QVariantMap{{QStringLiteral("id"), id}, {QStringLiteral("label"), MidiProtocol::protocolName(id)}});
    return out;
}

QVariantList MidiSurfaceService::ports() const
{
    QVariantList out;
    for (const QString &name : m_available) {
        const QString protocol = protocolFor(name);
        out.append(QVariantMap{{QStringLiteral("name"), name},
                               {QStringLiteral("protocol"), protocol},
                               {QStringLiteral("protocolLabel"), MidiProtocol::protocolName(protocol)},
                               {QStringLiteral("open"), m_sessions.count(name) > 0}});
    }
    return out;
}

void MidiSurfaceService::poll()
{
    const QStringList names = m_enabled ? m_io->inputPorts() : QStringList();
    bool changedPorts = names != m_available;
    m_available = names;

    QStringList open;
    for (const auto &entry : m_sessions)
        open.append(entry.first);
    for (const QString &name : open) {
        if (!names.contains(name)) {
            closeSession(name);
            changedPorts = true;
        }
    }
    for (const QString &name : names) {
        if (m_sessions.count(name) == 0) {
            openSession(name);
            changedPorts = true;
        }
    }
    if (changedPorts)
        emit changed();
}

void MidiSurfaceService::openSession(const QString &name)
{
    if (!m_io->open(name))
        return;
    Session session;
    session.protocolId = protocolFor(name);
    session.protocol = makeMidiProtocol(session.protocolId, m_manager, deviceId(name));
    session.protocol->send = [this, name](const QByteArray &bytes) { m_io->send(name, bytes); };
    MidiProtocol *protocol = session.protocol.get();
    // Registered first: registering pushes the current values out through onFeedback.
    m_sessions.emplace(name, std::move(session));
    m_manager->registerDevice(protocol->describe(name));
    protocol->start();
    protocol->showLabels(m_manager->stripLabels());
}

void MidiSurfaceService::closeSession(const QString &name)
{
    if (m_sessions.erase(name) == 0)
        return;
    m_io->close(name);
    m_manager->unregisterDevice(deviceId(name));
}

void MidiSurfaceService::onFeedback(const QString &device, const QString &controlId, double value)
{
    if (!device.startsWith(QLatin1String("midi:")))
        return;
    const auto it = m_sessions.find(device.mid(5));
    if (it != m_sessions.end())
        it->second.protocol->feedback(controlId, value);
}

} // namespace drift
