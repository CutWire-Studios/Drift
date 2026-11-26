#include "app/farm/FarmWorker.h"

#include "app/farm/RenderTask.h"
#include "core/base/VersionCompare.h"
#include "engine/addons/AddonRegistry.h"
#include "engine/encode/export/Exporter.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSet>
#include <QThread>
#include <QDateTime>

#include <algorithm>
#include <cstdio>

namespace drift::farm {

namespace {

struct HttpResult
{
    int status = 0;
    QByteArray body;
    QString error;
    bool ok() const { return status >= 200 && status < 300; }
};

// One request at a time, waiting on a local event loop; lives on the thread that made it.
class BlockingHttp
{
public:
    BlockingHttp(const QUrl &base, const QString &token) : m_base(base), m_token(token.toUtf8()) {}

    HttpResult post(const QString &path, const QJsonObject &body)
    {
        return send(QByteArrayLiteral("POST"), path, QJsonDocument(body).toJson(QJsonDocument::Compact), {}, {});
    }
    HttpResult download(const QString &path, const QString &file)
    {
        return send(QByteArrayLiteral("GET"), path, {}, file, {});
    }
    HttpResult upload(const QString &path, const QString &file)
    {
        return send(QByteArrayLiteral("PUT"), path, {}, {}, file);
    }

private:
    HttpResult send(const QByteArray &method, const QString &path, const QByteArray &body,
                    const QString &downloadTo, const QString &uploadFrom)
    {
        QUrl url = m_base;
        const QUrl target(path);
        url.setPath(target.path());
        url.setQuery(target.query());
        QNetworkRequest request(url);
        request.setRawHeader("Authorization", "Bearer " + m_token);
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        request.setTransferTimeout(60000);

        QFile upload(uploadFrom);
        QFile out(downloadTo);
        QNetworkReply *reply = nullptr;
        if (!uploadFrom.isEmpty()) {
            if (!upload.open(QIODevice::ReadOnly))
                return {0, {}, QStringLiteral("Could not read %1").arg(uploadFrom)};
            request.setHeader(QNetworkRequest::ContentLengthHeader, upload.size());
            reply = m_nam.put(request, &upload);
        } else if (method == "GET") {
            reply = m_nam.get(request);
        } else {
            reply = m_nam.post(request, body);
        }
        if (!downloadTo.isEmpty() && !out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            reply->abort();
            reply->deleteLater();
            return {0, {}, QStringLiteral("Could not write %1").arg(downloadTo)};
        }
        QEventLoop loop;
        QObject::connect(reply, &QNetworkReply::readyRead, &loop, [&] {
            if (out.isOpen())
                out.write(reply->readAll());
        });
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();
        if (out.isOpen()) {
            out.write(reply->readAll());
            out.close();
        }
        HttpResult result;
        result.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (downloadTo.isEmpty())
            result.body = reply->readAll();
        if (reply->error() != QNetworkReply::NoError && result.status == 0)
            result.error = reply->errorString();
        else if (result.status >= 400)
            result.error = QJsonDocument::fromJson(result.body).object().value(QStringLiteral("error")).toString(
                QStringLiteral("HTTP %1").arg(result.status));
        reply->deleteLater();
        return result;
    }

    QNetworkAccessManager m_nam;
    QUrl m_base;
    QByteArray m_token;
};

void logLine(const QString &line)
{
    std::fprintf(stderr, "[worker] %s\n", qPrintable(line));
    std::fflush(stderr);
}

// Media by content hash, evicting the least recently used when over the limit.
class MediaCache
{
public:
    MediaCache(const QString &dir, qint64 limit) : m_dir(dir), m_limit(limit) { QDir().mkpath(dir); }

    QString pathFor(const QString &hash, const QString &name) const
    {
        return QDir(m_dir).filePath(hash + QLatin1Char('.') + QFileInfo(name).suffix());
    }
    bool has(const QString &hash, const QString &name) const { return QFileInfo::exists(pathFor(hash, name)); }
    void touch(const QString &path) const
    {
        QFile file(path);
        if (file.open(QIODevice::ReadWrite))
            file.setFileTime(QDateTime::currentDateTime(), QFileDevice::FileModificationTime);
    }
    void evict(const QSet<QString> &keep) const
    {
        QFileInfoList files = QDir(m_dir).entryInfoList(QDir::Files, QDir::Time | QDir::Reversed);
        qint64 total = 0;
        for (const QFileInfo &file : files)
            total += file.size();
        for (const QFileInfo &file : files) {
            if (total <= m_limit)
                break;
            if (keep.contains(file.absoluteFilePath()) || file.suffix() == QLatin1String("part"))
                continue;
            total -= file.size();
            QFile::remove(file.absoluteFilePath());
        }
    }

private:
    QString m_dir;
    qint64 m_limit;
};

} // namespace

void FarmWorker::run(const std::atomic<bool> &stop)
{
    BlockingHttp http(m_config.coordinator, m_config.token);
    MediaCache cache(m_config.cacheDir, m_config.cacheLimitBytes);
    const QString workRoot = QDir(m_config.cacheDir).filePath(QStringLiteral("work"));

    QJsonArray encoders;
    for (const QVariant &codec : Exporter::videoCodecs()) {
        const QVariantMap map = codec.toMap();
        if (map.value(QStringLiteral("available")).toBool())
            encoders.append(map.value(QStringLiteral("id")).toString());
    }
    const QJsonObject registration{{QStringLiteral("name"), m_config.name},
                                   {QStringLiteral("gpu"), m_config.gpu},
                                   {QStringLiteral("encoders"), encoders}};

    QString workerId;
    const auto sleepMs = [&stop](int ms) {
        for (int slept = 0; slept < ms && !stop.load(); slept += 100)
            QThread::msleep(100);
    };

    while (!stop.load()) {
        if (workerId.isEmpty()) {
            const HttpResult reply = http.post(QStringLiteral("/v1/workers/register"), registration);
            if (!reply.ok()) {
                logLine(QStringLiteral("cannot reach the coordinator: %1").arg(reply.error));
                sleepMs(3000);
                continue;
            }
            workerId = QJsonDocument::fromJson(reply.body).object().value(QStringLiteral("workerId")).toString();
            logLine(QStringLiteral("registered as %1").arg(workerId));
        }
        const HttpResult polled = http.post(QStringLiteral("/v1/workers/%1/poll").arg(workerId), {});
        if (polled.status == 404) {
            workerId.clear();
            continue;
        }
        if (!polled.ok()) {
            logLine(QStringLiteral("poll failed: %1").arg(polled.error));
            sleepMs(3000);
            continue;
        }
        const QJsonObject task = QJsonDocument::fromJson(polled.body).object().value(QStringLiteral("task")).toObject();
        if (task.isEmpty()) {
            sleepMs(1000);
            continue;
        }

        const QString id = task.value(QStringLiteral("id")).toString();
        const QString taskDir = QDir(workRoot).filePath(id);
        QDir().mkpath(taskDir);
        logLine(QStringLiteral("task %1").arg(id));

        std::atomic<double> fraction{0.0};
        std::atomic<bool> cancel{false};
        std::atomic<bool> beating{true};
        QThread *beat = QThread::create([&] {
            BlockingHttp beatHttp(m_config.coordinator, m_config.token);
            while (beating.load()) {
                const HttpResult r = beatHttp.post(QStringLiteral("/v1/workers/%1/beat").arg(workerId),
                                                   QJsonObject{{QStringLiteral("task"), id},
                                                               {QStringLiteral("fraction"), fraction.load()}});
                if (r.ok() && QJsonDocument::fromJson(r.body).object().value(QStringLiteral("cancel")).toBool())
                    cancel = true;
                for (int i = 0; i < 20 && beating.load(); ++i)
                    QThread::msleep(100);
            }
        });
        beat->start();

        const auto done = [&](bool ok, const QString &error, qint64 frames) {
            http.post(QStringLiteral("/v1/tasks/%1/done").arg(id),
                      QJsonObject{{QStringLiteral("worker"), workerId},
                                  {QStringLiteral("ok"), ok},
                                  {QStringLiteral("error"), error},
                                  {QStringLiteral("frames"), static_cast<double>(frames)}});
            if (!ok)
                logLine(QStringLiteral("task %1 failed: %2").arg(id, error));
        };
        const auto finish = [&](bool ok, const QString &error, qint64 frames) {
            beating = false;
            beat->wait();
            delete beat;
            done(ok, error, frames);
            QDir(taskDir).removeRecursively();
        };

        // Add-ons first: a render missing one would silently differ from the editor's.
        QString missing;
        for (const QJsonValue &value : task.value(QStringLiteral("addons")).toArray()) {
            const QJsonObject addon = value.toObject();
            const drift::addon::InstalledAddon *installed = drift::addon::installedAddon(addon.value(QStringLiteral("id")).toString());
            if (!installed)
                missing += (missing.isEmpty() ? QString() : QStringLiteral(", "))
                    + addon.value(QStringLiteral("name")).toString(addon.value(QStringLiteral("id")).toString());
        }
        if (!missing.isEmpty()) {
            finish(false, QStringLiteral("Missing add-on: %1 (install it on this worker)").arg(missing), 0);
            continue;
        }
        const QString codec = task.value(QStringLiteral("settings")).toObject().value(QStringLiteral("videoCodecId")).toString(QStringLiteral("h264"));
        bool encoderOk = task.value(QStringLiteral("settings")).toObject().value(QStringLiteral("audioOnly")).toBool();
        for (const QJsonValue &value : encoders)
            encoderOk = encoderOk || value.toString() == codec;
        if (!encoderOk) {
            finish(false, QStringLiteral("No encoder for %1 on this worker").arg(codec), 0);
            continue;
        }

        const QString projectFile = QDir(taskDir).filePath(QStringLiteral("project.json"));
        if (const HttpResult r = http.download(QStringLiteral("/v1/tasks/%1/project").arg(id), projectFile); !r.ok()) {
            finish(false, QStringLiteral("Could not fetch the project: %1").arg(r.error), 0);
            continue;
        }

        QHash<QString, QString> remap;
        QSet<QString> keep;
        QString fetchError;
        const QJsonArray media = task.value(QStringLiteral("media")).toArray();
        for (const QJsonValue &value : media) {
            const QJsonObject entry = value.toObject();
            const QString hash = entry.value(QStringLiteral("hash")).toString();
            const QString name = entry.value(QStringLiteral("name")).toString();
            const QString cached = cache.pathFor(hash, name);
            keep.insert(QFileInfo(cached).absoluteFilePath());
            if (!cache.has(hash, name)) {
                const QString part = cached + QStringLiteral(".part");
                const HttpResult r = http.download(QStringLiteral("/v1/media/%1").arg(hash), part);
                if (!r.ok() || !QFile::rename(part, cached)) {
                    QFile::remove(part);
                    fetchError = QStringLiteral("Could not fetch %1: %2").arg(name, r.error);
                    break;
                }
                cache.evict(keep);
            }
            cache.touch(cached);
            remap.insert(entry.value(QStringLiteral("path")).toString(), cached);
        }
        if (!fetchError.isEmpty()) {
            finish(false, fetchError, 0);
            continue;
        }

        RenderTask render;
        render.id = id;
        render.projectPath = projectFile;
        render.settings = task.value(QStringLiteral("settings")).toObject().toVariantMap();
        render.outputPath = QDir(taskDir).filePath(QStringLiteral("result.") + task.value(QStringLiteral("ext")).toString());
        render.pathRemap = remap;
        render.muteAudio = task.value(QStringLiteral("muteAudio")).toBool();
        const TaskResult result = runTask(render, [&](double f) {
            fraction = f;
            return !cancel.load() && !stop.load();
        });
        if (!result.ok) {
            finish(false, result.error, 0);
            continue;
        }
        const HttpResult uploaded = http.upload(QStringLiteral("/v1/tasks/%1/result?worker=%2").arg(id, workerId), result.outputPath);
        if (!uploaded.ok()) {
            finish(false, QStringLiteral("Could not upload the result: %1").arg(uploaded.error), 0);
            continue;
        }
        finish(true, {}, result.frames);
    }
}

} // namespace drift::farm
