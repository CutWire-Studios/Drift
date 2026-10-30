#pragma once

#include <QJsonObject>
#include <QString>

class DownloadCenter;
class MarketInstallRouter;
class MarketplaceManifest;
class StockInstaller;

struct McpMarketContext
{
    MarketplaceManifest *manifest = nullptr;
    MarketInstallRouter *router = nullptr;
    DownloadCenter *downloads = nullptr;
    StockInstaller *stock = nullptr;

    bool valid() const { return manifest && router && downloads && stock; }
};

namespace drift::mcp {

// The `market` toolbox: every marketplace source for agents. Listing goes through one
// MarketSourceBrowser per source, installs through the MarketInstallRouter, and the job list is
// the DownloadCenter. All of it is asynchronous over the network; these block on a nested event
// loop the way capture does, so one call returns the answer.
QJsonObject marketStatus(const McpMarketContext &ctx, const QJsonObject &args);
QJsonObject marketSearch(const McpMarketContext &ctx, const QJsonObject &args);
QJsonObject marketItem(const McpMarketContext &ctx, const QJsonObject &args);
QJsonObject marketResolve(const McpMarketContext &ctx, const QJsonObject &args);
// A raw MCP result carrying an image, or a plain payload when no image was asked for.
QJsonObject marketPreview(const McpMarketContext &ctx, const QJsonObject &args);
// Starts the install and, with wait, blocks until it ends. On success the reply carries
// `result` ({asset} | {prop} | {effect} | {theme} | {look} | {template}); the caller places it.
QJsonObject marketInstall(const McpMarketContext &ctx, const QJsonObject &args);
QJsonObject marketDownloads(const McpMarketContext &ctx, const QJsonObject &args);
QJsonObject marketCancelDownload(const McpMarketContext &ctx, const QString &id);
QJsonObject marketRetryDownload(const McpMarketContext &ctx, const QString &id);
QJsonObject marketOpen(const McpMarketContext &ctx, const QJsonObject &args);

} // namespace drift::mcp
