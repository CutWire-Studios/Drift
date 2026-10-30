#pragma once

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QUrl>
#include <QVariantMap>

#include <functional>
#include <memory>

class DownloadCenter;
class QTimer;

namespace drift::market {
struct InstallFile;
struct InstallJob;
class Call;
class FileFetch;
class MarketTransport;
} // namespace drift::market

// Installs stock media (stock.<type> sources) through POST /marketplace/installs: the server
// prepares the file as a job, this polls it, downloads the result, checks its sha256 and
// imports it into the media bin. Browsing is not here; MarketSourceBrowser does that. Jobs are
// reported to the DownloadCenter as kind "stock".
class StockInstaller : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool configured READ configured CONSTANT)
    // Where stock downloads are saved. Empty until the user first picks a folder; persisted.
    Q_PROPERTY(QUrl lastDownloadDir READ lastDownloadDir WRITE setLastDownloadDir
                   NOTIFY lastDownloadDirChanged)
    Q_PROPERTY(int maxConcurrentDownloads READ maxConcurrentDownloads CONSTANT)
    Q_PROPERTY(bool resolving READ resolving NOTIFY resolveChanged)
    Q_PROPERTY(QVariantMap resolvedItem READ resolvedItem NOTIFY resolveChanged)
    Q_PROPERTY(QString resolveError READ resolveError NOTIFY resolveChanged)
    Q_PROPERTY(QString resolveErrorCode READ resolveErrorCode NOTIFY resolveChanged)
    Q_PROPERTY(bool resolveErrorRetryable READ resolveErrorRetryable NOTIFY resolveChanged)

public:
    using Importer = std::function<QStringList(const QStringList &)>;

    explicit StockInstaller(QObject *parent = nullptr,
                            drift::market::MarketTransport *transport = nullptr,
                            DownloadCenter *center = nullptr);
    ~StockInstaller() override;

    // Header-only so a build that never imports (tests) does not link the library.
    template<class Library>
    void setAssetLibrary(Library *library)
    {
        m_importer = [library](const QStringList &paths) { return library->importLocalPaths(paths); };
    }
    void setImporter(Importer importer) { m_importer = std::move(importer); }

    bool configured() const;
    QUrl lastDownloadDir() const { return m_lastDownloadDir; }
    void setLastDownloadDir(const QUrl &dir);
    int maxConcurrentDownloads() const;
    bool resolving() const { return m_resolving; }
    QVariantMap resolvedItem() const { return m_resolvedItem; }
    QString resolveError() const { return m_resolveError; }
    QString resolveErrorCode() const { return m_resolveErrorCode; }
    bool resolveErrorRetryable() const { return m_resolveErrorRetryable; }

    static QString jobId(const QString &source, const QString &itemId);

    // destinationDir is where the finished file is written before it is imported into the bin.
    // Empty or not a local folder falls back to the app data area.
    Q_INVOKABLE void download(const QString &source, const QString &itemId,
                              const QString &variantId = QString(),
                              const QUrl &destinationDir = QUrl(),
                              const QString &title = QString(),
                              const QString &mediaKind = QString());
    // By job id or bare item id, the latter for QML that only knows the item it is showing.
    Q_INVOKABLE QVariantMap downloadInfo(const QString &id) const;
    Q_INVOKABLE void cancelDownload(const QString &id);
    Q_INVOKABLE void retryDownload(const QString &id);

    // Paste a link: POST /marketplace/resolve, then poll the job until it names an item.
    Q_INVOKABLE void resolveUrl(const QString &url);
    Q_INVOKABLE void cancelResolve();

signals:
    void lastDownloadDirChanged();
    void resolveChanged();
    void downloadFailed(const QString &id, const QString &code, const QString &message);
    void downloadImported(const QString &id, const QString &name);

private:
    struct Job;

    void pump();
    void begin(const std::shared_ptr<Job> &job);
    void onInstallResponse(const std::shared_ptr<Job> &job, const drift::market::InstallJob &install);
    void pollJobs();
    void fetchFile(const std::shared_ptr<Job> &job, const drift::market::InstallFile &file);
    void importFile(const std::shared_ptr<Job> &job);
    void fail(const std::shared_ptr<Job> &job, const QString &code, const QString &message,
              bool retryable);
    void publish(const Job &job);
    void pollResolve();
    void setResolveState(bool resolving, const QVariantMap &item, const QString &error = {},
                         const QString &code = {}, bool retryable = true);

    drift::market::MarketTransport *m_transport = nullptr;
    DownloadCenter *m_center = nullptr;
    Importer m_importer;
    QTimer *m_pollTimer = nullptr;
    QTimer *m_resolveTimer = nullptr;
    QUrl m_lastDownloadDir;
    QHash<QString, std::shared_ptr<Job>> m_jobs;
    QStringList m_order;

    bool m_resolving = false;
    QVariantMap m_resolvedItem;
    QString m_resolveError;
    QString m_resolveErrorCode;
    bool m_resolveErrorRetryable = true;
    QString m_resolveJobId;
    QPointer<drift::market::Call> m_resolveCall;
};
