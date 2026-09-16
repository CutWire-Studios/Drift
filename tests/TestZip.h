#pragma once

#include <QByteArray>
#include <QFile>
#include <QList>
#include <QPair>
#include <QString>

#include <zlib.h>

// Writes a stored (uncompressed) zip — the minimum a .lottie bundle needs — for tests that
// exercise the readers without shipping binary fixtures.
inline bool writeStoredZip(const QString &path, const QList<QPair<QString, QByteArray>> &entries)
{
    QByteArray out;
    QByteArray central;
    auto put16 = [](QByteArray &b, quint16 v) { b.append(char(v & 0xff)); b.append(char(v >> 8)); };
    auto put32 = [&](QByteArray &b, quint32 v) { put16(b, quint16(v & 0xffff)); put16(b, quint16(v >> 16)); };
    for (const auto &entry : entries) {
        const QByteArray name = entry.first.toUtf8();
        const QByteArray &data = entry.second;
        const quint32 crc = quint32(crc32(0, reinterpret_cast<const Bytef *>(data.constData()), uInt(data.size())));
        const quint32 offset = quint32(out.size());
        put32(out, 0x04034b50); put16(out, 20); put16(out, 0); put16(out, 0); put16(out, 0); put16(out, 0);
        put32(out, crc); put32(out, quint32(data.size())); put32(out, quint32(data.size()));
        put16(out, quint16(name.size())); put16(out, 0);
        out.append(name); out.append(data);
        put32(central, 0x02014b50); put16(central, 20); put16(central, 20); put16(central, 0); put16(central, 0);
        put16(central, 0); put16(central, 0);
        put32(central, crc); put32(central, quint32(data.size())); put32(central, quint32(data.size()));
        put16(central, quint16(name.size())); put16(central, 0); put16(central, 0); put16(central, 0); put16(central, 0);
        put32(central, 0); put32(central, offset);
        central.append(name);
    }
    const quint32 cdOffset = quint32(out.size());
    out.append(central);
    put32(out, 0x06054b50); put16(out, 0); put16(out, 0);
    put16(out, quint16(entries.size())); put16(out, quint16(entries.size()));
    put32(out, quint32(central.size())); put32(out, cdOffset); put16(out, 0);
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(out) == out.size();
}
