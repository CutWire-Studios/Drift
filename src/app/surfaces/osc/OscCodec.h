#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QVariant>

namespace drift {

struct OscMessage {
    QString address;
    // int, double (float32/64), QString, QByteArray (blob), bool (T/F), invalid (N and I).
    QVariantList args;
};

// OSC 1.0 packets: messages and bundles (nested ones too). Anything malformed ends the packet
// with what was read so far.
QList<OscMessage> parseOscPacket(const QByteArray &packet);

// Arguments go out as int32 for int, float32 for double, string for QString, T/F for bool.
QByteArray encodeOscMessage(const OscMessage &message);

} // namespace drift
