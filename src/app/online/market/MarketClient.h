#pragma once

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
#include <memory>

class AssetLibrary;
class QNetworkAccessManager;
class QNetworkReply;
class QNetworkRequest;
class QTimer;

// Talks to market.cutwire.org. Lives in the app layer, not driftengine, so tools/ and tests/
// that do not need the marketplace keep working with no extra network stack.
//
// Catalog, search, and download jobs are the only store the UI knows. Providers are data from
// GET /catalog, not adapters in this client. Downloaded files land under AppData/marketplace
// and are imported through AssetLibrary as local copies we own.
class MarketClient : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool configured READ configured CONSTANT)
    Q_PROPERTY(bool catalogLoading READ catalogLoading NOTIFY catalogLoadingChanged)
    Q_PROPERTY(QString catalogError READ catalogError NOTIFY catalogErrorChanged)
    Q_PROPERTY(bool catalogErrorRetryable READ catalogErrorRetryable NOTIFY catalogErrorChanged)
    Q_PROPERTY(QVariantList types READ types NOTIFY catalogChanged)
    Q_PROPERTY(QString activeTypeId READ activeTypeId WRITE setActiveTypeId NOTIFY activeTypeIdChanged)
    Q_PROPERTY(QString activeProviderId READ activeProviderId WRITE setActiveProviderId
                   NOTIFY activeProviderIdChanged)
    Q_PROPERTY(QVariantList providers READ providers NOTIFY providersChanged)
    Q_PROPERTY(QVariantList filters READ filters NOTIFY providersChanged)
    Q_PROPERTY(bool canSearch READ canSearch NOTIFY providersChanged)
    Q_PROPERTY(bool canResolve READ canResolve NOTIFY providersChanged)
    // "All sources": activeProviderId "*" searches every source of the active type that has
    // no download limit, in parallel. Metered sources are only searched on their own, so
    // browsing never spends someone's quota on results they did not ask for.
    Q_PROPERTY(bool canSearchAll READ canSearchAll NOTIFY providersChanged)
    // Where stock downloads are saved. Empty until the user first picks a folder; persisted.
    Q_PROPERTY(QUrl lastDownloadDir READ lastDownloadDir WRITE setLastDownloadDir
                   NOTIFY lastDownloadDirChanged)
    Q_PROPERTY(QVariantMap quota READ quota NOTIFY quotaChanged)
    Q_PROPERTY(QVariantList items READ items NOTIFY itemsChanged)
    Q_PROPERTY(bool searching READ searching NOTIFY searchingChanged)
    Q_PROPERTY(QString searchError READ searchError NOTIFY searchErrorChanged)
    Q_PROPERTY(bool searchErrorRetryable READ searchErrorRetryable NOTIFY searchErrorChanged)
    // The frozen `code` behind searchError, or empty when the failure did not carry one. The
    // message is what to show; this is for deciding what to *offer* alongside it, which differs
    // per code — not_found can be opened in a browser. `reason` is deliberately not exposed: it is additive and must degrade to the bare
    // code, which isRetryable() already does in here.
    Q_PROPERTY(QString searchErrorCode READ searchErrorCode NOTIFY searchErrorChanged)
    Q_PROPERTY(bool hasMore READ hasMore NOTIFY itemsChanged)
    Q_PROPERTY(int downloadsRevision READ downloadsRevision NOTIFY downloadsRevisionChanged)
    // Every job this session has seen, oldest first, finished ones included. The download
    // manager renders this; the asset cards keep using downloadInfo() for their own item.
    Q_PROPERTY(QVariantList downloads READ downloads NOTIFY downloadsRevisionChanged)
    Q_PROPERTY(int activeDownloadCount READ activeDownloadCount NOTIFY downloadsRevisionChanged)
    Q_PROPERTY(int maxConcurrentDownloads READ maxConcurrentDownloads CONSTANT)

public:
    explicit MarketClient(QObject *parent = nullptr);
    ~MarketClient() override;

    void setAssetLibrary(AssetLibrary *library);

    bool configured() const;
    bool catalogLoading() const { return m_catalogLoading; }
    QString catalogError() const { return m_catalogError; }
    bool catalogErrorRetryable() const { return m_catalogErrorRetryable; }
    QVariantList types() const { return m_types; }
    QString activeTypeId() const { return m_activeTypeId; }
    void setActiveTypeId(const QString &id);
    QString activeProviderId() const { return m_activeProviderId; }
    void setActiveProviderId(const QString &id);
    QVariantList providers() const;
    QVariantList filters() const;
    bool canSearch() const;
    bool canResolve() const;
    bool canSearchAll() const { return allProviderIds().size() >= 2; }
    QUrl lastDownloadDir() const { return m_lastDownloadDir; }
    void setLastDownloadDir(const QUrl &dir);
    QVariantMap quota() const { return m_quota; }
    QVariantList items() const { return m_items; }
    bool searching() const { return m_searching; }
    QString searchError() const { return m_searchError; }
    bool searchErrorRetryable() const { return m_searchErrorRetryable; }
    QString searchErrorCode() const { return m_searchErrorCode; }
    bool hasMore() const;
    int downloadsRevision() const { return m_downloadsRevision; }
    QVariantList downloads() const;
    int activeDownloadCount() const;
    int maxConcurrentDownloads() const;

    // The one-time opt-in gate. Nothing in the store — catalog, search, resolve or download —
    // is offered until this has been given, so a user cannot reach a rate-limited third-party
    // download without having read what it is.

    Q_INVOKABLE void refreshCatalog();
    Q_INVOKABLE void search(const QString &query, const QVariantMap &filterValues = {});
    Q_INVOKABLE void resolveUrl(const QString &url);
    Q_INVOKABLE void loadMore();
    // destinationDir is where the finished file is written before it is imported into the
    // bin. Empty falls back to the app data area, which is what happens for anything that
    // starts a download without going through the folder prompt.
    Q_INVOKABLE void download(const QString &itemId, const QString &variantId = QString(),
                              const QUrl &destinationDir = QUrl(),
                              const QString &title = QString(),
                              const QString &mediaKind = QString());
    // Forgets finished, failed and cancelled jobs. Running ones are left alone.
    Q_INVOKABLE void clearFinishedDownloads();
    Q_INVOKABLE void retryDownload(const QString &itemId);
    Q_INVOKABLE void cancelDownload(const QString &itemId);
    // Drops an in-flight search or resolve. Distinct from cancelDownload: this is the
    // request that fills the grid, not one of the per-item jobs.
    Q_INVOKABLE void cancelSearch();
    Q_INVOKABLE QVariantMap downloadInfo(const QString &itemId) const;
    Q_INVOKABLE QVariantMap itemById(const QString &itemId) const;

    // A signed GET on the Drift API, for other stores that share this identity (Drift Assets).
    QNetworkReply *signedGet(const QString &path) { return get(apiUrl(path)); }

signals:
    void catalogLoadingChanged();
    void catalogErrorChanged();
    void catalogChanged();
    void activeTypeIdChanged();
    void lastDownloadDirChanged();
    void activeProviderIdChanged();
    void providersChanged();
    void quotaChanged();
    void itemsChanged();
    void searchingChanged();
    void searchErrorChanged();
    void downloadsRevisionChanged();
    void downloadProgress(const QString &itemId, double fraction, const QString &phase);
    void downloadFailed(const QString &itemId, const QString &code, const QString &message);
    void downloadImported(const QString &itemId, const QString &name);
    // Raised once per job when it is first accepted, so the manager window can show itself
    // without polling activeDownloadCount.
    void downloadStarted(const QString &itemId);

private:
    struct Job;

    static QString apiBase();
    QUrl apiUrl(const QString &path) const;
    QNetworkReply *get(const QUrl &url);
    QNetworkReply *post(const QUrl &url, const QByteArray &body);
    void sign(QNetworkRequest *request, const QByteArray &method, const QByteArray &body) const;

    void setCatalogLoading(bool loading);
    void setCatalogError(const QString &error, bool retryable = true);
    void setSearching(bool searching);
    void setSearchError(const QString &error, bool retryable = true, const QString &code = {});
    void bumpDownloads();
    // Resolve is a job now, not a synchronous reply: extraction can outlive any sane HTTP
    // timeout, so the POST only creates it and this polls until it is ready or failed.
    void pollResolve();
    void finishResolveFailure(const QJsonObject &problem);
    // Starts as many waiting jobs as the concurrency cap allows. Called whenever a job is
    // added or leaves the running set.
    void pumpDownloadQueue();
    void beginJob(const std::shared_ptr<Job> &job);
    static bool jobIsRunning(const Job &job);
    static bool jobIsFinished(const Job &job);
    QVariantMap jobToMap(const Job &job) const;

    QVariantMap activeType() const;
    QVariantMap activeProvider() const;
    bool hasCapability(const QString &name) const;
    bool searchingAll() const { return m_activeProviderId == QLatin1String("*"); }
    QStringList allProviderIds() const;
    void startFanoutSearch(bool append);
    void applyCatalog(const QJsonArray &types);
    void applySearchPage(const QJsonObject &page, bool append);
    void applyResolvedItem(const QJsonObject &item);
    void startSearch(bool append);
    void abortInFlightSearch();
    void pollJobs();
    void finishJobFile(Job *job, const QJsonObject &file, const QString &attribution);
    void failJob(const QString &itemId, const QString &code, const QString &message);
    void importReadyFile(const QString &itemId, const QString &path, const QString &displayName);


    // Wording of last resort. The service curates the sentence per reason and sanitizes it
    // before it goes out, so these only cover a reply that carried no detail at all — a
    // proxy error page, or a connection that never reached the service.
    static QString fallbackMessageForCode(const QString &code);
    // Whether offering the user a retry is honest. A geoblocked item or an unsupported link
    // will fail identically forever; a timeout or a blocked source may not.
    static bool isRetryable(const QString &code, const QString &reason);
    // Returns the sentence to show. Pass a QNetworkReply error to distinguish a service
    // reply from never having reached the service.
    static QString parseProblem(const QByteArray &body, int httpStatus, QString *codeOut,
                                QString *reasonOut = nullptr, bool *retryableOut = nullptr,
                                int networkError = 0);

    AssetLibrary *m_library = nullptr;
    QNetworkAccessManager *m_network = nullptr;
    QTimer *m_pollTimer = nullptr;

    bool m_catalogLoading = false;
    QString m_catalogError;
    bool m_catalogErrorRetryable = true;
    QVariantList m_types;
    QString m_activeTypeId;
    QString m_activeProviderId;
    QVariantMap m_quota;

    QVariantList m_items;
    QHash<QString, int> m_itemIndex;
    bool m_searching = false;
    QString m_searchError;
    bool m_searchErrorRetryable = true;
    QString m_searchErrorCode;
    QString m_query;
    QVariantMap m_filterValues;
    QString m_nextCursor;
    QPointer<QNetworkReply> m_searchReply;
    // "All sources" state: one reply and one cursor per source, and a generation so a
    // superseded fan-out's late replies are dropped rather than merged.
    QList<QPointer<QNetworkReply>> m_fanoutReplies;
    QHash<QString, QString> m_fanoutCursors;
    int m_fanoutGeneration = 0;
    QUrl m_lastDownloadDir;
    QString m_resolveJobId;
    QTimer *m_resolvePollTimer = nullptr;

    QHash<QString, std::shared_ptr<Job>> m_jobs;
    // QHash has no order and the manager lists jobs oldest first, so the order lives here.
    QStringList m_jobOrder;
    int m_downloadsRevision = 0;

};
