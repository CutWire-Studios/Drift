#pragma once

#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace drift::market {
class Call;
class MarketTransport;
} // namespace drift::market

// The server-driven marketplace layout: which pages and sections exist and which sources they
// read. Loaded from the disk cache, else the bundled default, then refreshed from
// GET /marketplace with If-None-Match. Everything the UI shows comes out of prune(), so an
// unknown section type or a missing source never reaches a renderer.
class MarketplaceManifest : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList pages READ pages NOTIFY changed)
    Q_PROPERTY(QVariantMap features READ features NOTIFY changed)
    Q_PROPERTY(QVariantMap sources READ sources NOTIFY changed)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(bool offline READ offline NOTIFY errorChanged)
    Q_PROPERTY(int revision READ revision NOTIFY changed)

public:
    static constexpr int kSupportedSchema = 1;

    explicit MarketplaceManifest(QObject *parent = nullptr,
                                 drift::market::MarketTransport *transport = nullptr,
                                 const QString &cacheDir = {});

    QVariantList pages() const { return m_pages; }
    QVariantMap features() const { return m_features; }
    QVariantMap sources() const { return m_sources; }
    bool loading() const { return m_loading; }
    QString error() const { return m_error; }
    bool offline() const { return m_offline; }
    int revision() const { return m_revision; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE QVariantMap source(const QString &id) const;
    Q_INVOKABLE QVariant feature(const QString &key, const QVariant &fallback = {}) const;
    Q_INVOKABLE QVariantMap page(const QString &id) const;
    Q_INVOKABLE void requestOpen(const QString &page, const QString &tab = {},
                                 const QString &source = {}, const QString &itemId = {});

    // Drops what this client cannot render. An empty map means the document is unusable
    // (schema newer than this client, or not a manifest at all).
    static QVariantMap prune(const QJsonObject &manifest, const QString &language = {});
    static QString uiLanguage();

signals:
    void changed();
    void loadingChanged();
    void errorChanged();
    void openRequested(const QString &page, const QString &tab, const QString &source,
                       const QString &itemId);

private:
    QString cachePath() const;
    QString etagPath() const;
    bool applyRaw(const QJsonObject &raw);
    void loadLocal();
    void writeCache(const QByteArray &body, const QByteArray &etag) const;
    void setLoading(bool loading);
    void setStatus(const QString &error, bool offline);

    drift::market::MarketTransport *m_transport = nullptr;
    QString m_cacheDir;
    QJsonObject m_raw;
    QByteArray m_etag;
    QVariantList m_pages;
    QVariantMap m_features;
    QVariantMap m_sources;
    bool m_loading = false;
    QString m_error;
    bool m_offline = false;
    int m_revision = 0;
    QPointer<drift::market::Call> m_call;
};
