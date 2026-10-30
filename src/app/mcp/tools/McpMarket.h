#pragma once

#include <QJsonObject>
#include <QString>

class StockInstaller;

namespace drift::mcp {

// The `market` toolbox: stock media from the marketplace for agents. Listing goes through a
// MarketSourceBrowser, installs through the StockInstaller, and the job list is the
// DownloadCenter. Both are asynchronous over the network; these block on a nested event loop
// the way capture does, so one call returns the answer.
QJsonObject marketStatus(StockInstaller *client);
QJsonObject marketSearch(StockInstaller *client, const QJsonObject &args);
QJsonObject marketResolve(StockInstaller *client, const QString &url);
QJsonObject marketItem(StockInstaller *client, const QString &id);
QJsonObject marketDownload(StockInstaller *client, const QJsonObject &args);
QJsonObject marketDownloads(StockInstaller *client, bool clear);
QJsonObject marketCancelDownload(StockInstaller *client, const QString &id);

} // namespace drift::mcp
