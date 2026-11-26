#include "app/surfaces/midi/MidiProtocols.h"

#include <QCoreApplication>

#include <algorithm>
#include <cmath>

namespace drift {

namespace {

constexpr int kStrips = 8;

QString indexed(const char *base, int i)
{
    return QLatin1String(base) + QString::number(i);
}

SurfaceControl control(const QString &id, const QString &label, SurfaceControl::Kind kind)
{
    return {id, label, kind};
}

QByteArray bytes(std::initializer_list<int> values)
{
    QByteArray out;
    for (int v : values)
        out.append(static_cast<char>(v));
    return out;
}

// What the strips of a fader controller drive when nothing is saved for it.
QList<SurfaceBindingDef> mixerDefaults()
{
    QList<SurfaceBindingDef> out;
    for (int i = 1; i <= kStrips; ++i) {
        const QString n = QString::number(i);
        out.append({indexed("fader", i), QStringLiteral("mixer.volume.") + n, {}, false, 1.0});
        out.append({indexed("mute", i), QStringLiteral("mixer.mute.") + n, {}, false, 1.0});
        out.append({indexed("solo", i), QStringLiteral("mixer.solo.") + n, {}, false, 1.0});
        out.append({indexed("vpot", i), QStringLiteral("mixer.pan.") + n, {}, false, 2.0});
    }
    out.append({QStringLiteral("play"), QStringLiteral("playPause"), {}, false, 1.0});
    out.append({QStringLiteral("stop"), QStringLiteral("stop"), {}, false, 1.0});
    out.append({QStringLiteral("rewind"), QStringLiteral("jumpBack"), {}, false, 1.0});
    out.append({QStringLiteral("ffwd"), QStringLiteral("jumpForward"), {}, false, 1.0});
    out.append({QStringLiteral("jog"), QStringLiteral("playhead.jog"), {}, false, 1.0});
    return out;
}

// A relative encoder byte: bit 6 is the direction, the low bits the number of ticks.
double relativeTicks(int value)
{
    const int magnitude = value & 0x3F;
    return (value & 0x40) ? -magnitude : magnitude;
}

} // namespace

QString MidiProtocol::protocolName(const QString &id)
{
    if (id == QLatin1String("mackie"))
        return QCoreApplication::translate("MidiProtocol", "Mackie Control");
    if (id == QLatin1String("hui"))
        return QCoreApplication::translate("MidiProtocol", "HUI");
    return QCoreApplication::translate("MidiProtocol", "Generic MIDI");
}

std::unique_ptr<MidiProtocol> makeMidiProtocol(const QString &id, ControlSurfaceManager *manager,
                                               const QString &deviceId)
{
    if (id == QLatin1String("mackie"))
        return std::make_unique<MackieProtocol>(manager, deviceId);
    if (id == QLatin1String("hui"))
        return std::make_unique<HuiProtocol>(manager, deviceId);
    return std::make_unique<GenericMidiProtocol>(manager, deviceId);
}

// --- Generic -------------------------------------------------------------------------------

QString GenericMidiProtocol::noteName(int note)
{
    static const char *names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    return QLatin1String(names[note % 12]) + QString::number(note / 12 - 1);
}

SurfaceDevice GenericMidiProtocol::describe(const QString &name) const
{
    SurfaceDevice d;
    d.id = m_deviceId;
    d.name = name;
    return d;
}

void GenericMidiProtocol::init()
{
    std::fill(std::begin(m_nrpnMsb), std::end(m_nrpnMsb), -1);
    std::fill(std::begin(m_nrpnLsb), std::end(m_nrpnLsb), -1);
    m_init = true;
}

void GenericMidiProtocol::absolute(const QString &id, const QString &label, double value)
{
    m_manager->ensureControl(m_deviceId, control(id, label, SurfaceControl::Absolute));
    m_manager->absolute(m_deviceId, id, value);
}

void GenericMidiProtocol::handle(const QByteArray &m)
{
    if (!m_init)
        init();
    if (m.size() < 2)
        return;
    const int status = static_cast<quint8>(m.at(0));
    const int type = status & 0xF0;
    const int ch = status & 0x0F;
    const int b1 = static_cast<quint8>(m.at(1));
    const int b2 = m.size() > 2 ? static_cast<quint8>(m.at(2)) : 0;

    if (type == 0x90 || type == 0x80) {
        const QString id = QStringLiteral("note.%1.%2").arg(ch + 1).arg(b1);
        m_manager->ensureControl(m_deviceId, control(id, QObject::tr("Note %1 (ch %2)").arg(noteName(b1)).arg(ch + 1),
                                                     SurfaceControl::Button));
        m_manager->button(m_deviceId, id, type == 0x90 && b2 > 0);
        return;
    }
    if (type == 0xE0) {
        absolute(QStringLiteral("pb.%1").arg(ch + 1), QObject::tr("Pitch bend (ch %1)").arg(ch + 1),
                 ((b2 << 7) | b1) / 16383.0);
        return;
    }
    if (type != 0xB0)
        return;

    const int cc = b1;
    if (cc == 99 || cc == 98) {
        (cc == 99 ? m_nrpnMsb : m_nrpnLsb)[ch] = b2;
        return;
    }
    if (cc == 101 || cc == 100) {
        m_nrpnMsb[ch] = m_nrpnLsb[ch] = -1;
        return;
    }
    if ((cc == 6 || cc == 38) && m_nrpnMsb[ch] >= 0 && m_nrpnLsb[ch] >= 0) {
        const int param = (m_nrpnMsb[ch] << 7) | m_nrpnLsb[ch];
        const QString id = QStringLiteral("nrpn.%1.%2").arg(ch + 1).arg(param);
        const QString label = QObject::tr("NRPN %1 (ch %2)").arg(param).arg(ch + 1);
        if (cc == 6) {
            m_dataMsb[ch] = b2;
            absolute(id, label, b2 / 127.0);
        } else {
            absolute(id, label, ((m_dataMsb[ch] << 7) | b2) / 16383.0);
        }
        return;
    }

    if (cc < 32) {
        m_msb[ch][cc] = static_cast<quint8>(b2);
        m_msbSeen[ch][cc] = true;
        if (m_pair14[ch][cc]) {
            absolute(QStringLiteral("cc14.%1.%2").arg(ch + 1).arg(cc),
                     QObject::tr("CC %1 fine (ch %2)").arg(cc).arg(ch + 1),
                     ((b2 << 7) | m_lsb[ch][cc]) / 16383.0);
            return;
        }
    } else if (cc < 64) {
        const int msbCc = cc - 32;
        if (m_msbSeen[ch][msbCc]) {
            m_pair14[ch][msbCc] = true;
            m_lsb[ch][msbCc] = static_cast<quint8>(b2);
            absolute(QStringLiteral("cc14.%1.%2").arg(ch + 1).arg(msbCc),
                     QObject::tr("CC %1 fine (ch %2)").arg(msbCc).arg(ch + 1),
                     ((m_msb[ch][msbCc] << 7) | b2) / 16383.0);
            return;
        }
    }
    absolute(QStringLiteral("cc.%1.%2").arg(ch + 1).arg(cc), QObject::tr("CC %1 (ch %2)").arg(cc).arg(ch + 1),
             b2 / 127.0);
}

// --- Mackie Control ------------------------------------------------------------------------

namespace {

struct MackieButton {
    const char *id;
    int note;
    const char *label;
};

const MackieButton kMackieButtons[] = {
    {"bankPrev", 0x2E, QT_TRANSLATE_NOOP("MidiProtocol", "Bank left")},
    {"bankNext", 0x2F, QT_TRANSLATE_NOOP("MidiProtocol", "Bank right")},
    {"rewind", 0x5B, QT_TRANSLATE_NOOP("MidiProtocol", "Rewind")},
    {"ffwd", 0x5C, QT_TRANSLATE_NOOP("MidiProtocol", "Fast forward")},
    {"stop", 0x5D, QT_TRANSLATE_NOOP("MidiProtocol", "Stop")},
    {"play", 0x5E, QT_TRANSLATE_NOOP("MidiProtocol", "Play")},
    {"record", 0x5F, QT_TRANSLATE_NOOP("MidiProtocol", "Record")},
    {"up", 0x60, QT_TRANSLATE_NOOP("MidiProtocol", "Cursor up")},
    {"down", 0x61, QT_TRANSLATE_NOOP("MidiProtocol", "Cursor down")},
    {"left", 0x62, QT_TRANSLATE_NOOP("MidiProtocol", "Cursor left")},
    {"right", 0x63, QT_TRANSLATE_NOOP("MidiProtocol", "Cursor right")},
    {"zoom", 0x64, QT_TRANSLATE_NOOP("MidiProtocol", "Zoom")},
    {"scrub", 0x65, QT_TRANSLATE_NOOP("MidiProtocol", "Scrub")},
};

// Per-strip button banks: base note and control id prefix.
struct MackieBank {
    const char *prefix;
    int base;
    const char *label;
};

const MackieBank kMackieBanks[] = {
    {"rec", 0x00, QT_TRANSLATE_NOOP("MidiProtocol", "Rec %1")},
    {"solo", 0x08, QT_TRANSLATE_NOOP("MidiProtocol", "Solo %1")},
    {"mute", 0x10, QT_TRANSLATE_NOOP("MidiProtocol", "Mute %1")},
    {"select", 0x18, QT_TRANSLATE_NOOP("MidiProtocol", "Select %1")},
    {"vsel", 0x20, QT_TRANSLATE_NOOP("MidiProtocol", "V-pot press %1")},
};

constexpr int kMackieTouchBase = 0x68;
constexpr int kMackieJogCc = 0x3C;
constexpr int kMackieVpotCc = 0x10;
constexpr int kMackieRingCc = 0x30;

} // namespace

SurfaceDevice MackieProtocol::describe(const QString &name) const
{
    SurfaceDevice d;
    d.id = m_deviceId;
    d.name = name;
    d.motorized = true;
    d.feedback = true;
    for (int i = 1; i <= kStrips; ++i) {
        d.controls.append(control(indexed("fader", i), QObject::tr("Fader %1").arg(i), SurfaceControl::Absolute));
        d.controls.append(control(indexed("vpot", i), QObject::tr("V-pot %1").arg(i), SurfaceControl::Relative));
        for (const MackieBank &b : kMackieBanks) {
            d.controls.append(control(indexed(b.prefix, i),
                                      QCoreApplication::translate("MidiProtocol", b.label).arg(i),
                                      SurfaceControl::Button));
        }
    }
    d.controls.append(control(QStringLiteral("faderM"), QObject::tr("Master fader"), SurfaceControl::Absolute));
    d.controls.append(control(QStringLiteral("jog"), QObject::tr("Jog wheel"), SurfaceControl::Relative));
    for (const MackieButton &b : kMackieButtons) {
        d.controls.append(control(QLatin1String(b.id), QCoreApplication::translate("MidiProtocol", b.label),
                                  SurfaceControl::Button));
    }
    d.defaults = mixerDefaults();
    d.defaults.append({QStringLiteral("bankPrev"), QStringLiteral("mixer.bankPrev"), {}, false, 1.0});
    d.defaults.append({QStringLiteral("bankNext"), QStringLiteral("mixer.bankNext"), {}, false, 1.0});
    d.defaults.append({QStringLiteral("left"), QStringLiteral("stepBack"), {}, false, 1.0});
    d.defaults.append({QStringLiteral("right"), QStringLiteral("stepForward"), {}, false, 1.0});
    return d;
}

QByteArray MackieProtocol::hostReply(const QByteArray &q)
{
    // F0 00 00 66 <id> 01 <serial 7> <challenge 4> F7  ->  F0 00 00 66 <id> 02 <serial 7> <response 4> F7
    if (q.size() != 18 || static_cast<quint8>(q.at(0)) != 0xF0 || static_cast<quint8>(q.at(5)) != 0x01)
        return {};
    const int c1 = static_cast<quint8>(q.at(13));
    const int c2 = static_cast<quint8>(q.at(14));
    const int c3 = static_cast<quint8>(q.at(15));
    const int c4 = static_cast<quint8>(q.at(16));
    QByteArray out = q;
    out[5] = 0x02;
    out[13] = static_cast<char>(0x7F & (c1 + (c2 ^ 0x0A) - c4));
    out[14] = static_cast<char>(0x7F & ((c3 >> 4) ^ (c1 + c4)));
    out[15] = static_cast<char>(0x7F & ((c4 - (c3 << 2)) ^ (c1 | c2)));
    out[16] = static_cast<char>(0x7F & (c2 - c3 + (0xF0 ^ (c4 << 4))));
    return out;
}

void MackieProtocol::handle(const QByteArray &m)
{
    if (m.isEmpty())
        return;
    const int status = static_cast<quint8>(m.at(0));
    if (status == 0xF0) {
        const QByteArray reply = hostReply(m);
        if (!reply.isEmpty() && send)
            send(reply);
        return;
    }
    if (m.size() < 3)
        return;
    const int type = status & 0xF0;
    const int ch = status & 0x0F;
    const int b1 = static_cast<quint8>(m.at(1));
    const int b2 = static_cast<quint8>(m.at(2));

    if (type == 0xE0) {
        const double v = ((b2 << 7) | b1) / 16383.0;
        if (ch < kStrips)
            m_manager->absolute(m_deviceId, indexed("fader", ch + 1), v);
        else if (ch == kStrips)
            m_manager->absolute(m_deviceId, QStringLiteral("faderM"), v);
        return;
    }
    if (type == 0xB0) {
        if (b1 == kMackieJogCc)
            m_manager->relative(m_deviceId, QStringLiteral("jog"), relativeTicks(b2));
        else if (b1 >= kMackieVpotCc && b1 < kMackieVpotCc + kStrips)
            m_manager->relative(m_deviceId, indexed("vpot", b1 - kMackieVpotCc + 1), relativeTicks(b2));
        return;
    }
    if (type != 0x90)
        return;

    const bool down = b2 > 0;
    if (b1 >= kMackieTouchBase && b1 < kMackieTouchBase + kStrips) {
        m_manager->setTouched(m_deviceId, indexed("fader", b1 - kMackieTouchBase + 1), down);
        return;
    }
    if (b1 == kMackieTouchBase + kStrips) {
        m_manager->setTouched(m_deviceId, QStringLiteral("faderM"), down);
        return;
    }
    for (const MackieBank &bank : kMackieBanks) {
        if (b1 >= bank.base && b1 < bank.base + kStrips) {
            m_manager->button(m_deviceId, indexed(bank.prefix, b1 - bank.base + 1), down);
            return;
        }
    }
    for (const MackieButton &b : kMackieButtons) {
        if (b.note == b1) {
            m_manager->button(m_deviceId, QLatin1String(b.id), down);
            return;
        }
    }
}

void MackieProtocol::feedback(const QString &id, double value)
{
    if (!send)
        return;
    if (id.startsWith(QLatin1String("fader"))) {
        const int ch = id == QLatin1String("faderM") ? kStrips : id.mid(5).toInt() - 1;
        if (ch < 0 || ch > kStrips)
            return;
        const int v = static_cast<int>(std::lround(std::clamp(value, 0.0, 1.0) * 16383.0));
        send(bytes({0xE0 | ch, v & 0x7F, (v >> 7) & 0x7F}));
        return;
    }
    if (id.startsWith(QLatin1String("vpot"))) {
        const int i = id.mid(4).toInt() - 1;
        if (i < 0 || i >= kStrips)
            return;
        // Boost/cut ring: eleven positions with the middle one lit when centred.
        const int position = 1 + static_cast<int>(std::lround(std::clamp(value, 0.0, 1.0) * 10.0));
        send(bytes({0xB0, kMackieRingCc + i, (1 << 4) | position}));
        return;
    }
    for (const MackieBank &bank : kMackieBanks) {
        if (id.startsWith(QLatin1String(bank.prefix)) && id.mid(static_cast<int>(strlen(bank.prefix))).toInt() > 0) {
            const int i = id.mid(static_cast<int>(strlen(bank.prefix))).toInt() - 1;
            if (i < kStrips)
                send(bytes({0x90, bank.base + i, value > 0.5 ? 0x7F : 0x00}));
            return;
        }
    }
    for (const MackieButton &b : kMackieButtons) {
        if (id == QLatin1String(b.id)) {
            send(bytes({0x90, b.note, value > 0.5 ? 0x7F : 0x00}));
            return;
        }
    }
}

QByteArray MackieProtocol::scribble(const QStringList &labels)
{
    QByteArray text;
    for (int i = 0; i < kStrips; ++i) {
        QByteArray cell = (i < labels.size() ? labels.at(i) : QString()).toLatin1().left(6);
        text.append(cell.leftJustified(7, ' '));
    }
    QByteArray out = bytes({0xF0, 0x00, 0x00, 0x66, 0x14, 0x12, 0x00});
    out.append(text);
    out.append(static_cast<char>(0xF7));
    return out;
}

void MackieProtocol::showLabels(const QStringList &labels)
{
    if (send)
        send(scribble(labels));
}

// --- HUI -----------------------------------------------------------------------------------

namespace {

constexpr int kHuiTransportZone = 0x0E;
constexpr int kHuiJogCc = 0x0D;

struct HuiSwitch {
    const char *prefix;
    int port;
};

const HuiSwitch kHuiStripSwitches[] = {{"select", 1}, {"mute", 2}, {"solo", 3}, {"rec", 7}};

struct HuiTransport {
    const char *id;
    int port;
};

const HuiTransport kHuiTransport[] = {{"rewind", 0}, {"ffwd", 1}, {"stop", 2}, {"play", 3}, {"record", 4}};

} // namespace

SurfaceDevice HuiProtocol::describe(const QString &name) const
{
    SurfaceDevice d;
    d.id = m_deviceId;
    d.name = name;
    d.motorized = true;
    d.feedback = true;
    for (int i = 1; i <= kStrips; ++i) {
        d.controls.append(control(indexed("fader", i), QObject::tr("Fader %1").arg(i), SurfaceControl::Absolute));
        d.controls.append(control(indexed("select", i), QObject::tr("Select %1").arg(i), SurfaceControl::Button));
        d.controls.append(control(indexed("mute", i), QObject::tr("Mute %1").arg(i), SurfaceControl::Button));
        d.controls.append(control(indexed("solo", i), QObject::tr("Solo %1").arg(i), SurfaceControl::Button));
        d.controls.append(control(indexed("rec", i), QObject::tr("Rec %1").arg(i), SurfaceControl::Button));
    }
    d.controls.append(control(QStringLiteral("jog"), QObject::tr("Jog wheel"), SurfaceControl::Relative));
    for (const HuiTransport &t : kHuiTransport)
        d.controls.append(control(QLatin1String(t.id), QLatin1String(t.id), SurfaceControl::Button));
    d.defaults = mixerDefaults();
    return d;
}

void HuiProtocol::handle(const QByteArray &m)
{
    if (m.size() < 3)
        return;
    const int status = static_cast<quint8>(m.at(0));
    const int type = status & 0xF0;
    const int b1 = static_cast<quint8>(m.at(1));
    const int b2 = static_cast<quint8>(m.at(2));

    if (type == 0x90 && b1 == 0x00) {
        if (send)
            send(bytes({0x90, 0x00, 0x7F}));
        return;
    }
    if (type != 0xB0)
        return;

    if (b1 < kStrips) {
        m_faderMsb[b1] = b2;
        return;
    }
    if (b1 >= 0x20 && b1 < 0x20 + kStrips) {
        const int strip = b1 - 0x20;
        m_manager->absolute(m_deviceId, indexed("fader", strip + 1), ((m_faderMsb[strip] << 7) | b2) / 16383.0);
        return;
    }
    if (b1 == kHuiJogCc) {
        m_manager->relative(m_deviceId, QStringLiteral("jog"), relativeTicks(b2));
        return;
    }
    if (b1 == 0x0F) {
        m_zone = b2;
        return;
    }
    if (b1 != 0x2F || m_zone < 0)
        return;

    const int port = b2 & 0x0F;
    const bool down = (b2 & 0x40) != 0;
    if (m_zone < kStrips) {
        if (port == 0) {
            m_manager->setTouched(m_deviceId, indexed("fader", m_zone + 1), down);
            return;
        }
        for (const HuiSwitch &s : kHuiStripSwitches) {
            if (s.port == port)
                m_manager->button(m_deviceId, indexed(s.prefix, m_zone + 1), down);
        }
    } else if (m_zone == kHuiTransportZone) {
        for (const HuiTransport &t : kHuiTransport) {
            if (t.port == port)
                m_manager->button(m_deviceId, QLatin1String(t.id), down);
        }
    }
}

void HuiProtocol::sendFader(int strip, double value)
{
    const int v = static_cast<int>(std::lround(std::clamp(value, 0.0, 1.0) * 16383.0));
    send(bytes({0xB0, strip, (v >> 7) & 0x7F}));
    send(bytes({0xB0, 0x20 + strip, v & 0x7F}));
}

void HuiProtocol::sendSwitch(int zone, int port, bool on)
{
    send(bytes({0xB0, 0x0C, zone}));
    send(bytes({0xB0, 0x2C, port | (on ? 0x40 : 0)}));
}

void HuiProtocol::feedback(const QString &id, double value)
{
    if (!send)
        return;
    if (id.startsWith(QLatin1String("fader"))) {
        const int strip = id.mid(5).toInt() - 1;
        if (strip >= 0 && strip < kStrips)
            sendFader(strip, value);
        return;
    }
    for (const HuiTransport &t : kHuiTransport) {
        if (id == QLatin1String(t.id)) {
            sendSwitch(kHuiTransportZone, t.port, value > 0.5);
            return;
        }
    }
    for (const HuiSwitch &s : kHuiStripSwitches) {
        if (id.startsWith(QLatin1String(s.prefix))) {
            const int strip = id.mid(static_cast<int>(strlen(s.prefix))).toInt() - 1;
            if (strip >= 0 && strip < kStrips)
                sendSwitch(strip, s.port, value > 0.5);
            return;
        }
    }
}

} // namespace drift
