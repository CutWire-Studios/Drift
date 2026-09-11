#pragma once

#include <QJsonObject>
#include <QString>

class MarketClient;

namespace drift::mcp {

// The `market` toolbox: stock media from market.cutwire.org for agents. Every op except
// marketStatus refuses to run until the user has accepted the marketplace terms in the app —
// downloads consume a per-machine quota, so an agent must never be the one to opt in.
// The client is asynchronous over the network; these block on a nested event loop the way
// capture does, so one call returns the answer.
QJsonObject marketStatus(MarketClient *client);
QJsonObject marketSearch(MarketClient *client, const QJsonObject &args);
QJsonObject marketResolve(MarketClient *client, const QString &url);
QJsonObject marketItem(MarketClient *client, const QString &id);
QJsonObject marketDownload(MarketClient *client, const QJsonObject &args);
QJsonObject marketDownloads(MarketClient *client, bool clear);
QJsonObject marketCancelDownload(MarketClient *client, const QString &id);

} // namespace drift::mcp
