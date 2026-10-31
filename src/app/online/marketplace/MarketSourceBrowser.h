#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace drift::market {
class Call;
class MarketTransport;
} // namespace drift::market

// Pages through GET /marketplace/sources/{source}/items. Property changes schedule a debounced
// reload; the cursor is opaque and only ever handed back to the server. Usable without QML.
class MarketSourceBrowser : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(QString category READ category WRITE setCategory NOTIFY categoryChanged)
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged)
    Q_PROPERTY(QString sort READ sort WRITE setSort NOTIFY sortChanged)
    Q_PROPERTY(QString period READ period WRITE setPeriod NOTIFY periodChanged)
    Q_PROPERTY(QString provider READ provider WRITE setProvider NOTIFY providerChanged)
    Q_PROPERTY(QVariantMap filters READ filters WRITE setFilters NOTIFY filtersChanged)
    Q_PROPERTY(int limit READ limit WRITE setLimit NOTIFY limitChanged)
    Q_PROPERTY(QVariantList items READ items NOTIFY itemsChanged)
    Q_PROPERTY(bool hasMore READ hasMore NOTIFY itemsChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(QString errorCode READ errorCode NOTIFY errorChanged)
    Q_PROPERTY(QVariantMap quota READ quota NOTIFY itemsChanged)
    Q_PROPERTY(QStringList warnings READ warnings NOTIFY itemsChanged)

public:
    static constexpr int kDebounceMs = 250;

    explicit MarketSourceBrowser(QObject *parent = nullptr);

    void setTransport(drift::market::MarketTransport *transport);

    QString source() const { return m_source; }
    void setSource(const QString &value);
    QString category() const { return m_category; }
    void setCategory(const QString &value);
    QString query() const { return m_query; }
    void setQuery(const QString &value);
    QString sort() const { return m_sort; }
    void setSort(const QString &value);
    QString period() const { return m_period; }
    void setPeriod(const QString &value);
    QString provider() const { return m_provider; }
    void setProvider(const QString &value);
    QVariantMap filters() const { return m_filters; }
    void setFilters(const QVariantMap &value);
    int limit() const { return m_limit; }
    void setLimit(int value);

    QVariantList items() const { return m_items; }
    bool hasMore() const { return !m_nextCursor.isEmpty(); }
    QString nextCursor() const { return m_nextCursor; }
    bool loading() const { return m_loading; }
    QString error() const { return m_error; }
    QString errorCode() const { return m_errorCode; }
    QVariantMap quota() const { return m_quota; }
    QStringList warnings() const { return m_warnings; }

    Q_INVOKABLE void reload();
    Q_INVOKABLE void loadMore();
    // Replaces the items with the page that starts at a cursor an earlier listing returned.
    void loadFrom(const QString &cursor);
    Q_INVOKABLE void cancel();

signals:
    void sourceChanged();
    void categoryChanged();
    void queryChanged();
    void sortChanged();
    void periodChanged();
    void providerChanged();
    void filtersChanged();
    void limitChanged();
    void itemsChanged();
    void loadingChanged();
    void errorChanged();

private:
    void schedule();
    void start(bool append);
    void stopRequest();
    void setLoading(bool loading);
    void setError(const QString &error, const QString &code);

    drift::market::MarketTransport *transport() const;

    drift::market::MarketTransport *m_transport = nullptr;
    QTimer m_debounce;
    QString m_source;
    QString m_category;
    QString m_query;
    QString m_sort;
    QString m_period;
    QString m_provider;
    QVariantMap m_filters;
    int m_limit = 24;
    QVariantList m_items;
    QString m_nextCursor;
    QVariantMap m_quota;
    QStringList m_warnings;
    bool m_loading = false;
    QString m_error;
    QString m_errorCode;
    QPointer<drift::market::Call> m_call;
};
