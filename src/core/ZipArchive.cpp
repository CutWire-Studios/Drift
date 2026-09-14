#include "ZipArchive.h"

#include <QObject>

#include <zlib.h>

namespace drift::zip {
namespace {

constexpr quint32 kZipLocalHeaderMagic = 0x04034b50;
constexpr quint32 kZipCentralDirMagic = 0x02014b50;
constexpr quint32 kZipEOCDMagic = 0x06054b50;

quint16 readU16(const char *p)
{
    const auto *b = reinterpret_cast<const uchar *>(p);
    return static_cast<quint16>(b[0] | (b[1] << 8));
}

quint32 readU32(const char *p)
{
    const auto *b = reinterpret_cast<const uchar *>(p);
    return static_cast<quint32>(b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24));
}

bool safeRelativePath(const QString &path)
{
    if (path.isEmpty() || path.startsWith(QLatin1Char('/')) || path.startsWith(QLatin1Char('\\')))
        return false;
    if (path.contains(QStringLiteral("..")) || path.contains(QLatin1Char(':')))
        return false;
    return true;
}

qint64 findEOCD(QFile &file)
{
    const qint64 fileSize = file.size();
    if (fileSize < 22)
        return -1;

    const qint64 scanLen = qMin(fileSize, static_cast<qint64>(65535 + 22));
    if (!file.seek(fileSize - scanLen))
        return -1;

    const QByteArray buf = file.read(scanLen);
    for (int i = buf.size() - 22; i >= 0; --i) {
        if (static_cast<uchar>(buf[i]) == 0x50
            && static_cast<uchar>(buf[i + 1]) == 0x4b
            && static_cast<uchar>(buf[i + 2]) == 0x05
            && static_cast<uchar>(buf[i + 3]) == 0x06) {
            return fileSize - scanLen + i;
        }
    }
    return -1;
}

QByteArray decompressRawDeflate(const QByteArray &compressed, quint32 uncompressedSize)
{
    z_stream strm;
    std::memset(&strm, 0, sizeof(strm));
    // -MAX_WBITS tells zlib to decode raw deflate without zlib/gzip headers
    if (inflateInit2(&strm, -MAX_WBITS) != Z_OK)
        return {};

    strm.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(compressed.constData()));
    strm.avail_in = static_cast<uInt>(compressed.size());

    QByteArray out;
    const int initialCapacity = uncompressedSize > 0 ? static_cast<int>(uncompressedSize)
                                                     : static_cast<int>(compressed.size() * 3);
    out.resize(initialCapacity);
    strm.next_out = reinterpret_cast<Bytef *>(out.data());
    strm.avail_out = static_cast<uInt>(out.size());

    int ret = inflate(&strm, Z_FINISH);
    if (ret == Z_STREAM_END) {
        out.resize(out.size() - strm.avail_out);
        inflateEnd(&strm);
        return out;
    }

    // Dynamic buffer loop if size estimation didn't match
    inflateEnd(&strm);
    std::memset(&strm, 0, sizeof(strm));
    if (inflateInit2(&strm, -MAX_WBITS) != Z_OK)
        return {};

    strm.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(compressed.constData()));
    strm.avail_in = static_cast<uInt>(compressed.size());

    out.clear();
    char buffer[32768];
    ret = Z_OK;
    while (ret == Z_OK || ret == Z_BUF_ERROR) {
        strm.next_out = reinterpret_cast<Bytef *>(buffer);
        strm.avail_out = sizeof(buffer);
        ret = inflate(&strm, Z_NO_FLUSH);
        const int have = sizeof(buffer) - strm.avail_out;
        if (have > 0)
            out.append(buffer, have);
        if (ret == Z_STREAM_END)
            break;
    }
    inflateEnd(&strm);
    return (ret == Z_STREAM_END || !out.isEmpty()) ? out : QByteArray{};
}

} // namespace

bool looksLikeZip(const QByteArray &head)
{
    return head.size() >= 4 && readU32(head.constData()) == kZipLocalHeaderMagic;
}

QList<Entry> readEntries(QFile &file, QString *error)
{
    const qint64 eocdPos = findEOCD(file);
    if (eocdPos < 0) {
        if (error)
            *error = QObject::tr("Not a valid ZIP archive");
        return {};
    }

    if (!file.seek(eocdPos))
        return {};

    const QByteArray eocdData = file.read(22);
    if (eocdData.size() < 22)
        return {};

    const quint16 totalEntries = readU16(eocdData.constData() + 10);
    const quint32 cdOffset = readU32(eocdData.constData() + 16);

    if (!file.seek(cdOffset))
        return {};

    QList<Entry> entries;
    entries.reserve(totalEntries);

    for (int i = 0; i < totalEntries; ++i) {
        const QByteArray cdHeader = file.read(46);
        if (cdHeader.size() < 46)
            break;

        if (readU32(cdHeader.constData()) != kZipCentralDirMagic)
            break;

        Entry entry;
        entry.method = readU16(cdHeader.constData() + 10);
        entry.compressedSize = readU32(cdHeader.constData() + 20);
        entry.uncompressedSize = readU32(cdHeader.constData() + 24);
        const quint16 fileNameLen = readU16(cdHeader.constData() + 28);
        const quint16 extraLen = readU16(cdHeader.constData() + 30);
        const quint16 commentLen = readU16(cdHeader.constData() + 32);
        entry.localOffset = readU32(cdHeader.constData() + 42);

        const QByteArray nameData = file.read(fileNameLen);
        entry.path = QString::fromUtf8(nameData);
        entry.isDir = entry.path.endsWith(QLatin1Char('/')) || entry.path.endsWith(QLatin1Char('\\'));

        // Skip extra and comment bytes in Central Directory
        if (extraLen + commentLen > 0)
            file.seek(file.pos() + extraLen + commentLen);

        if (safeRelativePath(entry.path))
            entries.append(entry);
    }

    return entries;
}

bool extractEntry(QFile &file, const Entry &entry, QByteArray &outData)
{
    if (entry.isDir)
        return true;

    if (!file.seek(entry.localOffset))
        return false;

    const QByteArray localHeader = file.read(30);
    if (localHeader.size() < 30 || readU32(localHeader.constData()) != kZipLocalHeaderMagic)
        return false;

    const quint16 localNameLen = readU16(localHeader.constData() + 26);
    const quint16 localExtraLen = readU16(localHeader.constData() + 28);

    const qint64 dataOffset = entry.localOffset + 30 + localNameLen + localExtraLen;
    if (!file.seek(dataOffset))
        return false;

    const QByteArray rawData = file.read(entry.compressedSize);
    if (rawData.size() != static_cast<int>(entry.compressedSize))
        return false;

    if (entry.method == 0) {
        // Stored
        outData = rawData;
        return true;
    } else if (entry.method == 8) {
        // Deflated
        outData = decompressRawDeflate(rawData, entry.uncompressedSize);
        return !outData.isEmpty() || entry.uncompressedSize == 0;
    }

    return false;
}

} // namespace drift::zip
