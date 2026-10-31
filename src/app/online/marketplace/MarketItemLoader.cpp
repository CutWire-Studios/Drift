#include "app/online/marketplace/MarketItemLoader.h"

#include "app/online/marketplace/MarketTransport.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

using namespace drift::market;

MarketItemLoader::MarketItemLoader(QObject *parent)
    : QObject(parent)
{
}

void MarketItemLoader::setSource(const QString &value)
{
    if (m_source == value)
        return;
    m_source = value;
    emit sourceChanged();
}

void MarketItemLoader::setItemId(const QString &value)
{
    if (m_itemId == value)
        return;
    m_itemId = value;
    emit itemIdChanged();
}

void MarketItemLoader::setLoading(bool loading)
{
    if (m_loading == loading)
        return;
    m_loading = loading;
    emit loadingChanged();
}

void MarketItemLoader::setError(const QString &error)
{
    if (m_error == error)
        return;
    m_error = error;
    emit errorChanged();
}

void MarketItemLoader::load()
{
    if (m_call)
        m_call->abort();
    m_call.clear();
    if (!m_item.isEmpty()) {
        m_item.clear();
        emit itemChanged();
    }
    setError({});
    if (m_source.isEmpty() || m_itemId.isEmpty()) {
        setLoading(false);
        return;
    }
    if (!MarketTransport::configured()) {
        setLoading(false);
        setError(MarketTransport::fallbackMessageForCode(QStringLiteral("invalid_client")));
        return;
    }
    const auto encoded = [](const QString &part) {
        return QString::fromLatin1(QUrl::toPercentEncoding(part));
    };
    setLoading(true);
    Call *call = MarketTransport::instance()->send(
        "GET",
        QStringLiteral("/marketplace/sources/") + encoded(m_source) + QStringLiteral("/items/")
            + encoded(m_itemId),
        Query());
    m_call = call;
    const QString source = m_source;
    const QString id = m_itemId;
    connect(call, &Call::finished, this, [this, source, id](const Response &response) {
        m_call.clear();
        setLoading(false);
        if (!response.ok()) {
            setError(response.problem().message);
            return;
        }
        QVariantMap item = QJsonDocument::fromJson(response.body).object().toVariantMap();
        if (item.isEmpty()) {
            setError(tr("That item was not found."));
            return;
        }
        item.insert(QStringLiteral("source"), source);
        if (item.value(QStringLiteral("id")).toString().isEmpty())
            item.insert(QStringLiteral("id"), id);
        m_item = item;
        emit itemChanged();
    });
}
