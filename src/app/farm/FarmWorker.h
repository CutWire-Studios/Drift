#pragma once

#include <QString>
#include <QUrl>

#include <atomic>

namespace drift::farm {

// The worker end of the farm: registers with a coordinator, polls for tasks, fetches the project
// and any media it has not cached (by content hash), renders, and uploads the result. Blocking;
// run it on a thread of its own, since rendering needs the main thread's event loop.
class FarmWorker
{
public:
    struct Config
    {
        QUrl coordinator;
        QString token;
        QString name;
        QString gpu;
        QString cacheDir;
        qint64 cacheLimitBytes = 20ll << 30;
    };

    explicit FarmWorker(Config config) : m_config(std::move(config)) {}

    void run(const std::atomic<bool> &stop);

private:
    Config m_config;
};

} // namespace drift::farm
