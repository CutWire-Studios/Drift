#include "app/crash/CrashBundle.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <zstd.h>

#include <cstdio>
#include <cstring>

namespace drift::crash {

namespace {

constexpr qint64 kChunk = 1 << 20;
constexpr int kLevel = 9;

// POSIX ustar, which every tar (bsdtar in Windows 10+, macOS, GNU) reads.
QByteArray tarHeader(const QByteArray &name, quint64 size)
{
    QByteArray header(512, '\0');
    char *h = header.data();
    std::memcpy(h, name.constData(), size_t(qMin<qsizetype>(name.size(), 99)));
    std::memcpy(h + 100, "0000644", 7);
    std::memcpy(h + 108, "0000000", 7);
    std::memcpy(h + 116, "0000000", 7);
    std::snprintf(h + 124, 12, "%011llo", static_cast<unsigned long long>(size));
    std::snprintf(h + 136, 12, "%011llo",
                  static_cast<unsigned long long>(QDateTime::currentSecsSinceEpoch()));
    std::memset(h + 148, ' ', 8);
    h[156] = '0';
    std::memcpy(h + 257, "ustar", 6);
    std::memcpy(h + 263, "00", 2);
    unsigned sum = 0;
    for (int i = 0; i < 512; ++i)
        sum += static_cast<unsigned char>(h[i]);
    std::snprintf(h + 148, 8, "%06o", sum);
    return header;
}

class ZstdFile
{
public:
    explicit ZstdFile(QFile &out) : m_out(out), m_ctx(ZSTD_createCCtx())
    {
        ZSTD_CCtx_setParameter(m_ctx, ZSTD_c_compressionLevel, kLevel);
        ZSTD_CCtx_setParameter(m_ctx, ZSTD_c_checksumFlag, 1);
        m_buffer.resize(qsizetype(ZSTD_CStreamOutSize()));
    }
    ~ZstdFile() { ZSTD_freeCCtx(m_ctx); }

    bool write(const QByteArray &data) { return feed(data, ZSTD_e_continue); }
    bool finish() { return feed({}, ZSTD_e_end); }

private:
    bool feed(const QByteArray &data, ZSTD_EndDirective mode)
    {
        ZSTD_inBuffer in{data.constData(), size_t(data.size()), 0};
        for (;;) {
            ZSTD_outBuffer out{m_buffer.data(), size_t(m_buffer.size()), 0};
            const size_t left = ZSTD_compressStream2(m_ctx, &out, &in, mode);
            if (ZSTD_isError(left))
                return false;
            if (out.pos && m_out.write(m_buffer.constData(), qint64(out.pos)) != qint64(out.pos))
                return false;
            if (mode == ZSTD_e_end ? left == 0 : in.pos == in.size)
                return true;
        }
    }

    QFile &m_out;
    ZSTD_CCtx *m_ctx;
    QByteArray m_buffer;
};

} // namespace

bool writeTarZst(const QString &path, const QList<BundleItem> &items, QString *error)
{
    QFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error)
            *error = out.errorString();
        return false;
    }
    const auto fail = [&](const QString &message) {
        if (error)
            *error = message;
        out.close();
        QFile::remove(path);
        return false;
    };

    ZstdFile zst(out);
    for (const BundleItem &item : items) {
        QFile source(item.filePath);
        if (!item.filePath.isEmpty() && !source.open(QIODevice::ReadOnly))
            return fail(QStringLiteral("cannot read %1").arg(item.filePath));
        // The live log keeps growing while this runs, so it is cut at the size seen on open.
        const quint64 size = item.filePath.isEmpty() ? quint64(item.data.size()) : quint64(source.size());
        if (!zst.write(tarHeader(item.name.toUtf8(), size)))
            return fail(out.errorString());

        if (item.filePath.isEmpty()) {
            if (!zst.write(item.data))
                return fail(out.errorString());
        } else {
            quint64 remaining = size;
            while (remaining > 0) {
                const QByteArray chunk = source.read(qint64(qMin<quint64>(remaining, kChunk)));
                if (chunk.isEmpty() || !zst.write(chunk))
                    return fail(QStringLiteral("cannot read %1").arg(item.filePath));
                remaining -= quint64(chunk.size());
            }
        }
        if (const int pad = int((512 - size % 512) % 512); pad && !zst.write(QByteArray(pad, '\0')))
            return fail(out.errorString());
    }
    if (!zst.write(QByteArray(1024, '\0')) || !zst.finish())
        return fail(out.errorString());
    return true;
}

QString newestAppleReport(const QString &reportsDir, const QDateTime &since)
{
    QString newest;
    QDateTime newestTime;
    const QFileInfoList files = QDir(reportsDir).entryInfoList(QDir::Files);
    for (const QFileInfo &file : files) {
        const QString name = file.fileName();
        const bool matches = name.startsWith(QLatin1String("drift"), Qt::CaseInsensitive)
                             && (name.endsWith(QLatin1String(".ips"), Qt::CaseInsensitive)
                                 || name.endsWith(QLatin1String(".crash"), Qt::CaseInsensitive));
        if (matches && file.lastModified() >= since && (newest.isEmpty() || file.lastModified() > newestTime)) {
            newest = file.absoluteFilePath();
            newestTime = file.lastModified();
        }
    }
    return newest;
}

QList<BundleItem> bundleItems(const QString &crashDir, const QString &logDir, const QString &systemInfo,
                              const QString &appleReport)
{
    QList<BundleItem> items;
    const auto addFile = [&items](const QString &name, const QString &path) {
        if (QFileInfo::exists(path))
            items.append({name, path, {}});
    };
    if (!crashDir.isEmpty()) {
        addFile(QStringLiteral("crash.json"), crashDir + QStringLiteral("/crash.json"));
        addFile(QStringLiteral("full.dmp"), crashDir + QStringLiteral("/full.dmp"));
        addFile(QStringLiteral("stack.txt"), crashDir + QStringLiteral("/stack.txt"));
    }
    if (!appleReport.isEmpty())
        addFile(QFileInfo(appleReport).fileName(), appleReport);
    addFile(QStringLiteral("drift.log"), logDir + QStringLiteral("/drift.log"));
    addFile(QStringLiteral("drift.prev.log"), logDir + QStringLiteral("/drift.prev.log"));
    items.append({QStringLiteral("system-info.txt"), {}, systemInfo.toUtf8()});
    return items;
}

} // namespace drift::crash
