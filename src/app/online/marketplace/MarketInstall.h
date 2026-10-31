#pragma once

#include "app/online/marketplace/MarketTransport.h"

#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantMap>

class QCryptographicHash;
class QSaveFile;

namespace drift::market {

struct InstallFile
{
    QString role;
    QString name;
    QUrl url;
    qint64 size = 0;
    QString sha256;
    QString mime;
};

// The InstallJobDto behind POST /marketplace/installs and GET /installs/{id}, with a failed
// HTTP exchange folded in: errorCode and errorMessage are set whenever the job cannot go on,
// whichever way it failed.
struct InstallJob
{
    QString id;
    QString source;
    QString status;
    double progress = 0;
    QList<InstallFile> files;
    QVariantMap license;
    QString minAppVersion;
    QString errorCode;
    QString errorMessage;
    bool retryable = true;

    bool failed() const { return !errorCode.isEmpty() || status == QLatin1String("failed"); }
    bool ready() const { return !failed() && status == QLatin1String("ready"); }
    const InstallFile *mainFile() const;
};

InstallJob parseInstallJob(const Response &response);
Call *requestInstall(MarketTransport *transport, const QString &source, const QString &itemId,
                     const QString &variantId);
Call *pollInstall(MarketTransport *transport, const QString &jobId);

// A file name that is safe to join onto a directory.
QString safeFileName(const QString &name);

// Fetches one install file into destPath and checks its sha256 (and size when known). A file
// on the API host goes out signed. The partial file is only committed when the hash matches.
class FileFetch : public QObject
{
    Q_OBJECT
public:
    FileFetch(MarketTransport *transport, const InstallFile &file, const QString &destPath,
              QObject *parent = nullptr);

    void start();
    void abort();

signals:
    void progress(qint64 received, qint64 total);
    // errorCode is empty on success.
    void finished(const QString &errorCode, const QString &message);

private:
    void finish(const QString &errorCode, const QString &message);

    MarketTransport *m_transport = nullptr;
    InstallFile m_file;
    QString m_destPath;
    QPointer<QNetworkReply> m_reply;
    QSaveFile *m_out = nullptr;
    QCryptographicHash *m_hash = nullptr;
    bool m_done = false;
    bool m_aborted = false;
};

} // namespace drift::market
