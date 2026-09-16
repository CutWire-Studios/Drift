#pragma once

#include <QByteArray>
#include <QFile>
#include <QList>
#include <QString>

// Minimal read-only ZIP support (stored and deflated entries, no zip64): enough for the
// archives Drift opens — .mogrt templates and .lottie animation bundles.

namespace drift::zip {

struct Entry
{
    QString path;
    quint16 method = 0;
    quint32 compressedSize = 0;
    quint32 uncompressedSize = 0;
    quint32 localOffset = 0;
    bool isDir = false;
};

// True when the bytes start with a local file header — the cheap "is this a zip" probe.
bool looksLikeZip(const QByteArray &head);

// The central directory, minus entries whose path escapes the archive root.
QList<Entry> readEntries(QFile &file, QString *error);

bool extractEntry(QFile &file, const Entry &entry, QByteArray &outData);

} // namespace drift::zip
