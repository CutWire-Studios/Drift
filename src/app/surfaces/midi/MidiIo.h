#pragma once

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QStringList>

namespace drift {

// Splits a buffer that holds several complete MIDI messages (Android delivers them batched).
inline QList<QByteArray> splitMidiMessages(const QByteArray &data)
{
    QList<QByteArray> out;
    int i = 0;
    while (i < data.size()) {
        const quint8 status = static_cast<quint8>(data.at(i));
        if (status < 0x80) {
            ++i;
            continue;
        }
        int length = 1;
        if (status == 0xF0) {
            const int end = data.indexOf(static_cast<char>(0xF7), i);
            length = end < 0 ? static_cast<int>(data.size()) - i : end - i + 1;
        } else if (status >= 0xF8) {
            length = 1;
        } else if (status == 0xF1 || status == 0xF3 || (status & 0xF0) == 0xC0 || (status & 0xF0) == 0xD0) {
            length = 2;
        } else if (status == 0xF6 || status == 0xF7) {
            length = 1;
        } else {
            length = 3;
        }
        out.append(data.mid(i, length));
        i += length;
    }
    return out;
}

// The platform's MIDI ports. Implementations deliver `received` on the thread that owns them.
class MidiIo : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;

    // Display names of the input ports a controller could be on, each unique.
    virtual QStringList inputPorts() = 0;
    // Opens the input and, when there is one, the output of the same device.
    virtual bool open(const QString &name) = 0;
    virtual void close(const QString &name) = 0;
    virtual void send(const QString &name, const QByteArray &bytes) = 0;

signals:
    void received(const QString &name, const QByteArray &bytes);
};

// libremidi on desktop, android.media.midi on the phone; null where neither exists (wasm).
MidiIo *createPlatformMidiIo(QObject *parent);

} // namespace drift
