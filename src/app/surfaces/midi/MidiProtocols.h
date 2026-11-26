#pragma once

#include "app/surfaces/ControlSurfaceManager.h"

#include <QByteArray>

#include <functional>
#include <memory>

namespace drift {

// How the bytes of one MIDI port map onto a surface device's controls, and back for feedback.
class MidiProtocol {
public:
    MidiProtocol(ControlSurfaceManager *manager, QString deviceId)
        : m_manager(manager), m_deviceId(std::move(deviceId))
    {
    }
    virtual ~MidiProtocol() = default;

    virtual SurfaceDevice describe(const QString &name) const = 0;
    virtual void handle(const QByteArray &message) = 0;
    // The device's side of a control changed: move a fader, light an LED.
    virtual void feedback(const QString &controlId, double value) { Q_UNUSED(controlId) Q_UNUSED(value) }
    virtual void showLabels(const QStringList &labels) { Q_UNUSED(labels) }
    // Called once after the device is registered, for handshakes and initial state.
    virtual void start() {}

    std::function<void(const QByteArray &)> send;

    static QString protocolName(const QString &id);

protected:
    ControlSurfaceManager *m_manager;
    QString m_deviceId;
};

// Notes, CC (7 and 14 bit), pitch bend and NRPN become controls as they are first seen, so
// "learn" works with any controller.
class GenericMidiProtocol final : public MidiProtocol {
public:
    using MidiProtocol::MidiProtocol;

    SurfaceDevice describe(const QString &name) const override;
    void handle(const QByteArray &message) override;

    static QString noteName(int note);

private:
    void absolute(const QString &id, const QString &label, double value);

    quint8 m_msb[16][32] = {};
    bool m_msbSeen[16][32] = {};
    quint8 m_lsb[16][32] = {};
    bool m_pair14[16][32] = {};
    int m_nrpnMsb[16];
    int m_nrpnLsb[16];
    int m_dataMsb[16] = {};
    bool m_init = false;
    void init();
};

// Mackie Control Universal: eight motorized faders with mute/solo/select/rec, v-pots, a jog
// wheel, transport, bank buttons and a scribble strip for track names.
class MackieProtocol final : public MidiProtocol {
public:
    using MidiProtocol::MidiProtocol;

    SurfaceDevice describe(const QString &name) const override;
    void handle(const QByteArray &message) override;
    void feedback(const QString &controlId, double value) override;
    void showLabels(const QStringList &labels) override;

    static QByteArray hostReply(const QByteArray &query);
    static QByteArray scribble(const QStringList &labels);
};

// HUI, as used by Pro Tools era controllers: faders are CC pairs, buttons are zone/port pairs and
// the host has to answer the device's ping or it drops the connection.
class HuiProtocol final : public MidiProtocol {
public:
    using MidiProtocol::MidiProtocol;

    SurfaceDevice describe(const QString &name) const override;
    void handle(const QByteArray &message) override;
    void feedback(const QString &controlId, double value) override;

private:
    void sendFader(int strip, double value);
    void sendSwitch(int zone, int port, bool on);

    int m_zone = -1;
    int m_faderMsb[8] = {};
};

std::unique_ptr<MidiProtocol> makeMidiProtocol(const QString &id, ControlSurfaceManager *manager,
                                               const QString &deviceId);

} // namespace drift
