#include "app/online/marketplace/MarketInstall.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace drift::market {

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("MarketInstall", text);
}

} // namespace

const InstallFile *InstallJob::mainFile() const
{
    for (const InstallFile &file : files) {
        if (file.role == QLatin1String("main"))
            return &file;
    }
    return files.isEmpty() ? nullptr : &files.first();
}

InstallJob parseInstallJob(const Response &response)
{
    InstallJob job;
    const QJsonObject obj = QJsonDocument::fromJson(response.body).object();
    if (!response.ok()) {
        const Problem problem = response.problem();
        job.errorCode = problem.code;
        job.errorMessage = problem.message;
        job.retryable = problem.retryable;
        job.minAppVersion = obj.value(QStringLiteral("minAppVersion")).toString();
        if (problem.code == QLatin1String("app_too_old")) {
            job.retryable = false;
            if (!job.minAppVersion.isEmpty())
                job.errorMessage = tr("This needs Drift %1 or newer. Update Drift to install it.")
                                       .arg(job.minAppVersion);
        } else if (problem.code.endsWith(QLatin1String("quota_exceeded"))) {
            job.retryable = false;
        }
        return job;
    }

    job.id = obj.value(QStringLiteral("id")).toString();
    job.source = obj.value(QStringLiteral("source")).toString();
    job.status = obj.value(QStringLiteral("status")).toString();
    job.progress = obj.value(QStringLiteral("progress")).toDouble();
    job.minAppVersion = obj.value(QStringLiteral("minAppVersion")).toString();
    job.license = obj.value(QStringLiteral("license")).toObject().toVariantMap();
    for (const QJsonValue &value : obj.value(QStringLiteral("files")).toArray()) {
        const QJsonObject f = value.toObject();
        InstallFile file;
        file.role = f.value(QStringLiteral("role")).toString();
        file.name = f.value(QStringLiteral("name")).toString();
        file.url = QUrl(f.value(QStringLiteral("url")).toString());
        file.size = qint64(f.value(QStringLiteral("size")).toDouble());
        file.sha256 = f.value(QStringLiteral("sha256")).toString().trimmed().toLower();
        file.mime = f.value(QStringLiteral("mime")).toString();
        job.files.append(file);
    }
    if (job.status == QLatin1String("failed")) {
        const QJsonObject err = obj.value(QStringLiteral("error")).toObject();
        job.errorCode = err.value(QStringLiteral("code")).toString(QStringLiteral("download_failed"));
        const QString detail = err.value(QStringLiteral("detail")).toString().trimmed();
        job.errorMessage = detail.isEmpty() ? MarketTransport::fallbackMessageForCode(job.errorCode) : detail;
        job.retryable = MarketTransport::isRetryable(job.errorCode, QString());
    } else if (job.status == QLatin1String("ready") && job.files.isEmpty()) {
        job.errorCode = QStringLiteral("download_failed");
        job.errorMessage = MarketTransport::fallbackMessageForCode(job.errorCode);
    }
    return job;
}

Call *requestInstall(MarketTransport *transport, const QString &source, const QString &itemId,
                     const QString &variantId)
{
    QJsonObject body{{QStringLiteral("source"), source}, {QStringLiteral("itemId"), itemId}};
    if (!variantId.isEmpty())
        body.insert(QStringLiteral("variantId"), variantId);
    return transport->send("POST", QStringLiteral("/marketplace/installs"), {},
                           QJsonDocument(body).toJson(QJsonDocument::Compact));
}

Call *pollInstall(MarketTransport *transport, const QString &jobId)
{
    return transport->send("GET", QStringLiteral("/marketplace/installs/") + jobId, {});
}

QString safeFileName(const QString &name)
{
    QString out = QFileInfo(name).fileName();
    out.replace(QLatin1Char('/'), QLatin1Char('_'));
    out.replace(QLatin1Char('\\'), QLatin1Char('_'));
    if (out == QLatin1String(".") || out == QLatin1String("..") || out.isEmpty())
        out = QStringLiteral("download");
    return out;
}

FileFetch::FileFetch(MarketTransport *transport, const InstallFile &file, const QString &destPath,
                     QObject *parent)
    : QObject(parent)
    , m_transport(transport)
    , m_file(file)
    , m_destPath(destPath)
{
}

void FileFetch::start()
{
    QDir().mkpath(QFileInfo(m_destPath).absolutePath());
    m_out = new QSaveFile(m_destPath, this);
    if (!m_out->open(QIODevice::WriteOnly)) {
        finish(QStringLiteral("download_failed"), tr("Could not save that file."));
        return;
    }
    m_hash = new QCryptographicHash(QCryptographicHash::Sha256);
    m_reply = m_transport->fetchSigned(m_file.url);
    connect(m_reply, &QNetworkReply::readyRead, this, [this] {
        const QByteArray chunk = m_reply->readAll();
        m_out->write(chunk);
        m_hash->addData(chunk);
    });
    connect(m_reply, &QNetworkReply::downloadProgress, this,
            [this](qint64 received, qint64 total) { emit progress(received, total); });
    connect(m_reply, &QNetworkReply::finished, this, [this] {
        QNetworkReply *reply = m_reply;
        reply->deleteLater();
        if (m_aborted)
            return;
        const QByteArray tail = reply->readAll();
        m_out->write(tail);
        m_hash->addData(tail);
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError || (status != 0 && status >= 400)) {
            finish(QStringLiteral("download_failed"),
                   MarketTransport::fallbackMessageForCode(QStringLiteral("download_failed")));
            return;
        }
        const QString actual = QString::fromLatin1(m_hash->result().toHex());
        if (!m_file.sha256.isEmpty() && actual != m_file.sha256) {
            finish(QStringLiteral("checksum_mismatch"),
                   tr("The downloaded file did not match what the marketplace sent."));
            return;
        }
        if (m_file.size > 0 && m_out->size() != m_file.size) {
            finish(QStringLiteral("checksum_mismatch"),
                   tr("The downloaded file did not match what the marketplace sent."));
            return;
        }
        finish({}, {});
    });
}

void FileFetch::abort()
{
    if (m_done)
        return;
    m_aborted = true;
    if (m_reply)
        m_reply->abort();
    m_done = true;
    if (m_out)
        m_out->cancelWriting();
    delete m_hash;
    m_hash = nullptr;
    deleteLater();
}

void FileFetch::finish(const QString &errorCode, const QString &message)
{
    if (m_done)
        return;
    m_done = true;
    QString code = errorCode;
    QString text = message;
    if (code.isEmpty() && !m_out->commit()) {
        code = QStringLiteral("download_failed");
        text = tr("Could not save that file.");
    } else if (!code.isEmpty()) {
        m_out->cancelWriting();
    }
    delete m_hash;
    m_hash = nullptr;
    emit finished(code, text);
    deleteLater();
}

} // namespace drift::market
