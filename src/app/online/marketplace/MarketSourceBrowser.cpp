#include "app/online/marketplace/MarketSourceBrowser.h"

#include "app/online/marketplace/MarketTransport.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

using namespace drift::market;

MarketSourceBrowser::MarketSourceBrowser(QObject *parent)
    : QObject(parent)
{
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(kDebounceMs);
    connect(&m_debounce, &QTimer::timeout, this, &MarketSourceBrowser::reload);
}

void MarketSourceBrowser::setTransport(MarketTransport *transport)
{
    m_transport = transport;
}

MarketTransport *MarketSourceBrowser::transport() const
{
    return m_transport ? m_transport : MarketTransport::instance();
}

void MarketSourceBrowser::schedule()
{
    stopRequest();
    m_debounce.start();
}

#define DRIFT_BROWSER_STRING_SETTER(Name, member, signal)                                         \
    void MarketSourceBrowser::set##Name(const QString &value)                                     \
    {                                                                                             \
        if (member == value)                                                                      \
            return;                                                                               \
        member = value;                                                                           \
        emit signal();                                                                            \
        schedule();                                                                               \
    }

DRIFT_BROWSER_STRING_SETTER(Source, m_source, sourceChanged)
DRIFT_BROWSER_STRING_SETTER(Category, m_category, categoryChanged)
DRIFT_BROWSER_STRING_SETTER(Query, m_query, queryChanged)
DRIFT_BROWSER_STRING_SETTER(Sort, m_sort, sortChanged)
DRIFT_BROWSER_STRING_SETTER(Period, m_period, periodChanged)
DRIFT_BROWSER_STRING_SETTER(Provider, m_provider, providerChanged)

#undef DRIFT_BROWSER_STRING_SETTER

void MarketSourceBrowser::setFilters(const QVariantMap &value)
{
    if (m_filters == value)
        return;
    m_filters = value;
    emit filtersChanged();
    schedule();
}

void MarketSourceBrowser::setLimit(int value)
{
    value = qBound(1, value, 50);
    if (m_limit == value)
        return;
    m_limit = value;
    emit limitChanged();
    schedule();
}

void MarketSourceBrowser::setLoading(bool loading)
{
    if (m_loading == loading)
        return;
    m_loading = loading;
    emit loadingChanged();
}

void MarketSourceBrowser::setError(const QString &error, const QString &code)
{
    if (m_error == error && m_errorCode == code)
        return;
    m_error = error;
    m_errorCode = code;
    emit errorChanged();
}

void MarketSourceBrowser::stopRequest()
{
    if (m_call)
        m_call->abort();
    m_call.clear();
    setLoading(false);
}

void MarketSourceBrowser::cancel()
{
    m_debounce.stop();
    stopRequest();
}

void MarketSourceBrowser::reload()
{
    m_debounce.stop();
    stopRequest();
    m_nextCursor.clear();
    m_items.clear();
    m_quota.clear();
    m_warnings.clear();
    emit itemsChanged();
    setError({}, {});
    start(false);
}

void MarketSourceBrowser::loadMore()
{
    if (m_loading || m_nextCursor.isEmpty() || m_debounce.isActive())
        return;
    start(true);
}

void MarketSourceBrowser::start(bool append)
{
    if (m_source.isEmpty())
        return;
    if (!MarketTransport::configured()) {
        setError(MarketTransport::fallbackMessageForCode(QStringLiteral("invalid_client")),
                 QStringLiteral("invalid_client"));
        return;
    }

    Query query;
    const auto add = [&query](const QString &key, const QString &value) {
        if (!value.isEmpty())
            query.append({key, value});
    };
    add(QStringLiteral("category"), m_category);
    add(QStringLiteral("q"), m_query);
    add(QStringLiteral("sort"), m_sort);
    add(QStringLiteral("period"), m_period);
    add(QStringLiteral("provider"), m_provider);
    add(QStringLiteral("limit"), QString::number(m_limit));
    for (auto it = m_filters.cbegin(); it != m_filters.cend(); ++it)
        add(it.key(), it.value().toString());
    if (append)
        add(QStringLiteral("cursor"), m_nextCursor);

    const QString path = QStringLiteral("/marketplace/sources/")
        + QString::fromLatin1(QUrl::toPercentEncoding(m_source)) + QStringLiteral("/items");
    setLoading(true);
    Call *call = transport()->send("GET", path, query);
    m_call = call;
    connect(call, &Call::finished, this, [this, append](const Response &response) {
        m_call.clear();
        setLoading(false);
        if (!response.ok()) {
            const Problem problem = response.problem();
            setError(problem.message, problem.code);
            return;
        }
        const QJsonObject page = QJsonDocument::fromJson(response.body).object();
        QVariantList rows = append ? m_items : QVariantList();
        for (const QJsonValue &item : page.value(QStringLiteral("items")).toArray())
            rows.append(item.toObject().toVariantMap());
        m_items = rows;
        m_nextCursor = page.value(QStringLiteral("nextCursor")).toString();
        m_quota = page.value(QStringLiteral("quota")).toObject().toVariantMap();
        m_warnings.clear();
        for (const QJsonValue &warning : page.value(QStringLiteral("warnings")).toArray())
            m_warnings.append(warning.toString());
        setError({}, {});
        emit itemsChanged();
    });
}
