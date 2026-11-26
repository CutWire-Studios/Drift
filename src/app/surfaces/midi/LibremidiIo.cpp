#include "app/surfaces/midi/MidiIo.h"

#include <QMetaObject>

#include <libremidi/libremidi.hpp>

#include <map>
#include <memory>

namespace drift {

namespace {

QString portName(const libremidi::port_information &p)
{
    return QString::fromStdString(p.display_name.empty() ? p.port_name : p.display_name);
}

// Software loopbacks that are never a controller.
bool ignored(const QString &name)
{
    return name.contains(QLatin1String("Midi Through"), Qt::CaseInsensitive)
           || name.startsWith(QLatin1String("System:")) || name.contains(QLatin1String("libremidi"));
}

class LibremidiIo final : public MidiIo {
public:
    explicit LibremidiIo(QObject *parent)
        : MidiIo(parent), m_observer(libremidi::observer_configuration{.track_hardware = true,
                                                                       .track_virtual = true,
                                                                       .notify_in_constructor = false})
    {
    }

    QStringList inputPorts() override
    {
        m_inputs.clear();
        m_outputs.clear();
        for (const libremidi::input_port &p : m_observer.get_input_ports()) {
            QString name = portName(p);
            if (ignored(name))
                continue;
            for (int n = 2; m_inputs.count(name) > 0; ++n)
                name = portName(p) + QStringLiteral(" (%1)").arg(n);
            m_inputs.emplace(name, p);
        }
        for (const libremidi::output_port &p : m_observer.get_output_ports())
            m_outputs.emplace(portName(p), p);
        QStringList names;
        for (const auto &entry : m_inputs)
            names.append(entry.first);
        return names;
    }

    bool open(const QString &name) override
    {
        const auto in = m_inputs.find(name);
        if (in == m_inputs.end())
            return false;
        Port port;
        libremidi::input_configuration config;
        config.on_message = [this, name](const libremidi::message &m) {
            const QByteArray bytes(reinterpret_cast<const char *>(m.bytes.data()), static_cast<qsizetype>(m.bytes.size()));
            QMetaObject::invokeMethod(this, [this, name, bytes] { emit received(name, bytes); },
                                      Qt::QueuedConnection);
        };
        config.ignore_sysex = false;
        port.in = std::make_unique<libremidi::midi_in>(std::move(config), libremidi::midi_in_configuration_for(m_observer));
        if (port.in->open_port(in->second).is_set())
            return false;
        // The device's output is the port of the same name; surfaces with feedback need it.
        const auto out = m_outputs.find(name);
        if (out != m_outputs.end()) {
            port.out = std::make_unique<libremidi::midi_out>(libremidi::output_configuration{},
                                                             libremidi::midi_out_configuration_for(m_observer));
            if (port.out->open_port(out->second).is_set())
                port.out.reset();
        }
        m_open[name] = std::move(port);
        return true;
    }

    void close(const QString &name) override { m_open.erase(name); }

    void send(const QString &name, const QByteArray &bytes) override
    {
        const auto it = m_open.find(name);
        if (it == m_open.end() || !it->second.out)
            return;
        it->second.out->send_message(reinterpret_cast<const unsigned char *>(bytes.constData()),
                                     static_cast<size_t>(bytes.size()));
    }

private:
    struct Port {
        std::unique_ptr<libremidi::midi_in> in;
        std::unique_ptr<libremidi::midi_out> out;
    };

    libremidi::observer m_observer;
    std::map<QString, libremidi::input_port> m_inputs;
    std::map<QString, libremidi::output_port> m_outputs;
    std::map<QString, Port> m_open;
};

} // namespace

MidiIo *createPlatformMidiIo(QObject *parent)
{
    return new LibremidiIo(parent);
}

} // namespace drift
