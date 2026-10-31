#include "app/online/downloads/DownloadBridges.h"

#include "app/online/addons/AddonManager.h"
#include "app/online/downloads/DownloadCenter.h"
#include "app/online/updates/UpdateChecker.h"

#include <QCoreApplication>

namespace {

QString addonTitle(AddonManager *addons, const QString &id)
{
    for (const QVariant &row : addons->catalog()) {
        const QVariantMap addon = row.toMap();
        if (addon.value(QStringLiteral("id")).toString() == id)
            return addon.value(QStringLiteral("name")).toString();
    }
    return id;
}

} // namespace

void bridgeUpdates(DownloadCenter *center, UpdateChecker *updates)
{
    const auto sync = [center, updates] {
        const QVariantMap map = updates->downloadJob();
        if (map.isEmpty())
            return;
        const QString status = map.value(QStringLiteral("status")).toString();
        DownloadCenter::Job job;
        job.id = QStringLiteral("update");
        job.kind = QStringLiteral("update");
        job.mediaKind = QStringLiteral("update");
        job.title = map.value(QStringLiteral("title")).toString();
        job.status = status == QLatin1String("preparing") ? QStringLiteral("processing") : status;
        job.progress = map.value(QStringLiteral("progress")).toDouble();
        job.bytesReceived = map.value(QStringLiteral("bytesReceived")).toLongLong();
        job.bytesTotal = map.value(QStringLiteral("bytesTotal")).toLongLong();
        job.error = map.value(QStringLiteral("errorMessage")).toString();
        job.retryable = status == QLatin1String("failed");
        job.cancellable = !DownloadCenter::isFinished(job.status);
        job.phase = job.status == QLatin1String("done") ? map.value(QStringLiteral("doneDetail")).toString()
                    : job.status == QLatin1String("failed") ? job.error
                                                            : map.value(QStringLiteral("phase")).toString();
        center->upsert(job);
    };
    QObject::connect(updates, &UpdateChecker::downloadJobChanged, center, sync);
    center->registerHandler(
        QStringLiteral("update"), [updates](const QString &) { updates->cancelDownload(); },
        [updates](const QString &) { updates->retryDownload(); });
}

void bridgeAddons(DownloadCenter *center, AddonManager *addons)
{
    const auto idFor = [](const QString &addonId) { return QStringLiteral("addon:") + addonId; };
    const auto base = [=](const QString &addonId) {
        DownloadCenter::Job job = center->job(idFor(addonId));
        job.id = idFor(addonId);
        job.kind = QStringLiteral("addon");
        job.mediaKind = QStringLiteral("addon");
        job.title = addonTitle(addons, addonId);
        job.cancellable = true;
        job.retryable = true;
        return job;
    };

    QObject::connect(addons, &AddonManager::progressChanged, center,
                     [=](const QString &id, double fraction, const QString &phase) {
        DownloadCenter::Job job = base(id);
        const bool installing = phase == QLatin1String("Installing");
        job.status = installing ? QStringLiteral("installing") : QStringLiteral("downloading");
        job.phase = installing ? QCoreApplication::translate("DownloadCenter", "Installing…")
                               : QCoreApplication::translate("DownloadCenter", "Downloading…");
        job.progress = fraction;
        job.error.clear();
        center->upsert(job);
    });
    QObject::connect(addons, &AddonManager::transferSucceeded, center, [=](const QString &id) {
        DownloadCenter::Job job = base(id);
        job.status = QStringLiteral("done");
        job.phase = QCoreApplication::translate("DownloadCenter", "Installed");
        job.progress = 1;
        job.result = {{QStringLiteral("addonId"), id}};
        center->upsert(job);
    });
    QObject::connect(addons, &AddonManager::transferFailed, center,
                     [=](const QString &id, const QString &reason) {
        DownloadCenter::Job job = base(id);
        const bool cancelled = reason == QLatin1String("Cancelled");
        job.status = cancelled ? QStringLiteral("cancelled") : QStringLiteral("failed");
        job.phase = cancelled ? QCoreApplication::translate("DownloadCenter", "Cancelled") : reason;
        job.error = cancelled ? QString() : reason;
        center->upsert(job);
    });

    center->registerHandler(
        QStringLiteral("addon"),
        [addons](const QString &id) { addons->cancel(id.mid(int(qstrlen("addon:")))); },
        [addons](const QString &id) { addons->install(id.mid(int(qstrlen("addon:")))); });
}
