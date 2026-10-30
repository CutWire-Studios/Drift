#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace drift::market {
class Call;
} // namespace drift::market

// Fetches one listing, GET /marketplace/sources/{source}/items/{id}, for an open-item action
// or a deep link that only knows the ids.
class MarketItemLoader : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(QString itemId READ itemId WRITE setItemId NOTIFY itemIdChanged)
    Q_PROPERTY(QVariantMap item READ item NOTIFY itemChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)

public:
    explicit MarketItemLoader(QObject *parent = nullptr);

    QString source() const { return m_source; }
    void setSource(const QString &value);
    QString itemId() const { return m_itemId; }
    void setItemId(const QString &value);
    QVariantMap item() const { return m_item; }
    bool loading() const { return m_loading; }
    QString error() const { return m_error; }

    // Starts the request when both ids are set; clears the item otherwise.
    Q_INVOKABLE void load();

signals:
    void sourceChanged();
    void itemIdChanged();
    void itemChanged();
    void loadingChanged();
    void errorChanged();

private:
    void setLoading(bool loading);
    void setError(const QString &error);

    QString m_source;
    QString m_itemId;
    QVariantMap m_item;
    bool m_loading = false;
    QString m_error;
    QPointer<drift::market::Call> m_call;
};
