#include "app/farm/FarmCli.h"

#include "app/farm/FarmCoordinator.h"
#include "app/farm/FarmWorker.h"
#include "app/farm/RenderQueue.h"
#include "app/farm/RenderTask.h"
#include "app/farm/TemplateBatch.h"
#include "engine/encode/export/Exporter.h"
#include "core/base/CacheRoot.h"
#include "engine/projdir/ProjectTemplate.h"

#include <QDir>
#include <QHostInfo>
#include <QSettings>

#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>

#include <atomic>
#include <cstdio>
#include <iostream>
#include <mutex>
#include <string>
#include <memory>
#include <thread>

namespace drift::farm {

FarmCommand parseFarmCommand(int argc, char *argv[])
{
    FarmCommand command;
    const auto value = [&](int &i) { return i + 1 < argc ? QString::fromLocal8Bit(argv[++i]) : QString(); };
    for (int i = 1; i < argc; ++i) {
        const QByteArray arg(argv[i]);
        if (arg == "--gpu") {
            command.gpu = value(i);
        } else if (arg == "--worker") {
            command.mode = FarmCommand::Mode::Worker;
        } else if (arg == "--farm-coordinator") {
            command.mode = FarmCommand::Mode::FarmCoordinator;
        } else if (arg == "--farm-worker") {
            command.mode = FarmCommand::Mode::FarmWorker;
            command.url = value(i);
        } else if (arg == "--port") {
            command.port = value(i).toInt();
        } else if (arg == "--token") {
            command.token = value(i);
        } else if (arg == "--name") {
            command.name = value(i);
        } else if (arg == "--cache-gb") {
            command.cacheGb = value(i).toInt();
        } else if (arg == "--list-gpus") {
            command.mode = FarmCommand::Mode::ListGpus;
        } else if (arg == "--render") {
            if (command.mode == FarmCommand::Mode::Mcp)
                command.mode = FarmCommand::Mode::Render;
            command.project = value(i);
        } else if (arg == "-o" || arg == "--output") {
            command.output = value(i);
        } else if (arg == "--data") {
            command.data = value(i);
        } else if (arg == "--row") {
            command.row = value(i).toInt();
        } else if (arg == "--all") {
            command.all = true;
        } else if (arg == "--preset") {
            command.settings.insert(QStringLiteral("preset"), value(i));
        } else if (arg == "--codec") {
            command.settings.insert(QStringLiteral("videoCodecId"), value(i));
        } else if (arg == "--crf") {
            command.settings.insert(QStringLiteral("crf"), value(i).toInt());
        } else if (arg == "--height") {
            command.settings.insert(QStringLiteral("targetHeight"), value(i).toInt());
        } else if (arg == "--fps") {
            command.settings.insert(QStringLiteral("fpsNum"), value(i).toInt());
            command.settings.insert(QStringLiteral("fpsDen"), 1);
        } else if (arg == "--start") {
            command.settings.insert(QStringLiteral("startUs"), value(i).toDouble() * 1e6);
        } else if (arg == "--end") {
            command.settings.insert(QStringLiteral("endUs"), value(i).toDouble() * 1e6);
        }
    }
    return command;
}

bool isFarmValueFlag(const QByteArray &arg)
{
    static const QList<QByteArray> flags{"--gpu", "--render", "--data", "--row", "-o", "--output", "--preset", "--codec", "--crf",
                                         "--height", "--fps", "--start", "--end", "--farm-worker", "--port", "--token", "--name",
                                         "--cache-gb"};
    return flags.contains(arg);
}

namespace {

// Turns the CLI's "preset" into the scale keys the export settings carry.
bool resolvePreset(const QVariantMap &given, QVariantMap *out)
{
    *out = given;
    const QString preset = out->take(QStringLiteral("preset")).toString();
    if (preset.isEmpty())
        return true;
    const ExportScalePreset *scale = Exporter::scalePresetById(preset);
    if (!scale) {
        std::fprintf(stderr, "Unknown preset '%s'\n", qPrintable(preset));
        return false;
    }
    out->insert(QStringLiteral("targetHeight"), scale->targetHeight);
    out->insert(QStringLiteral("videoBitrateKbps"), scale->videoBitrateKbps);
    return true;
}

int runRender(const FarmCommand &command, QCoreApplication &app)
{
    if (command.output.isEmpty()) {
        std::fprintf(stderr, "--render needs -o <file>\n");
        return 2;
    }
    RenderTask task;
    task.projectPath = command.project;
    task.outputPath = command.output;
    if (!resolvePreset(command.settings, &task.settings))
        return 2;

    TaskResult result;
    int lastPercent = -1;
    QThread *thread = QThread::create([&] {
        result = runTask(task, [&lastPercent](double fraction) {
            const int percent = static_cast<int>(fraction * 100.0);
            if (percent != lastPercent) {
                lastPercent = percent;
                std::fprintf(stderr, "\rrendering %3d%%", percent);
                std::fflush(stderr);
            }
            return true;
        });
        QMetaObject::invokeMethod(&app, &QCoreApplication::quit, Qt::QueuedConnection);
    });
    thread->start();
    app.exec();
    thread->wait();
    delete thread;

    std::fprintf(stderr, "\n");
    if (!result.ok) {
        std::fprintf(stderr, "render failed: %s\n", qPrintable(result.error));
        return 1;
    }
    std::fprintf(stdout, "%s\n", qPrintable(QFileInfo(result.outputPath).absoluteFilePath()));
    return 0;
}

// The child end of WorkerProcess: JSON lines on stdin and stdout, one task at a time. stdout carries
// nothing else, so logging stays on stderr.
int runWorkerMode(QCoreApplication &app)
{
    static std::mutex outputMutex;
    static std::atomic<bool> cancelRequested{false};
    static std::atomic<bool> busy{false};
    const auto emitLine = [](const QJsonObject &message) {
        const QByteArray line = QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n';
        std::lock_guard<std::mutex> lock(outputMutex);
        std::fwrite(line.constData(), 1, static_cast<size_t>(line.size()), stdout);
        std::fflush(stdout);
    };

    const auto handle = [&app, emitLine](const QByteArray &line) {
        const QJsonObject message = QJsonDocument::fromJson(line).object();
        const QString cmd = message.value(QStringLiteral("cmd")).toString();
        if (cmd == QLatin1String("quit")) {
            app.quit();
        } else if (cmd == QLatin1String("cancel")) {
            cancelRequested = true;
        } else if (cmd == QLatin1String("run") && !busy.exchange(true)) {
            cancelRequested = false;
            const RenderTask task = RenderTask::fromJson(message.value(QStringLiteral("task")).toObject());
            QThread *thread = QThread::create([task, emitLine] {
                int lastPercent = -1;
                const TaskResult result = runTask(task, [&](double fraction) {
                    const int percent = static_cast<int>(fraction * 100.0);
                    if (percent != lastPercent) {
                        lastPercent = percent;
                        emitLine(QJsonObject{{QStringLiteral("event"), QStringLiteral("progress")},
                                             {QStringLiteral("fraction"), fraction}});
                    }
                    return !cancelRequested.load();
                });
                busy = false;
                emitLine(QJsonObject{{QStringLiteral("event"), QStringLiteral("done")},
                                     {QStringLiteral("ok"), result.ok},
                                     {QStringLiteral("error"), result.error},
                                     {QStringLiteral("frames"), static_cast<double>(result.frames)},
                                     {QStringLiteral("outputPath"), result.outputPath}});
            });
            QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
            thread->start();
        }
    };

    std::thread reader([&app, handle] {
        std::string line;
        while (std::getline(std::cin, line)) {
            const QByteArray bytes = QByteArray::fromStdString(line);
            QMetaObject::invokeMethod(&app, [handle, bytes] { handle(bytes); }, Qt::QueuedConnection);
        }
        QMetaObject::invokeMethod(&app, &QCoreApplication::quit, Qt::QueuedConnection);
    });
    reader.detach();

    emitLine(QJsonObject{{QStringLiteral("event"), QStringLiteral("ready")}});
    app.exec();
    // The reader is parked in a blocking read nothing can wake; leave without unwinding into it.
    std::fflush(stdout);
    std::_Exit(0);
}

int runCoordinator(const FarmCommand &command, QCoreApplication &app, const QString &gpuLine)
{
    Q_UNUSED(gpuLine);
    RenderQueue queue;
    FarmCoordinator coordinator(&queue);
    if (!command.token.isEmpty())
        coordinator.setToken(command.token);
    if (!coordinator.start(command.port)) {
        std::fprintf(stderr, "Could not listen on port %d: %s\n", command.port, qPrintable(coordinator.lastError()));
        return 1;
    }
    std::fprintf(stdout, "farm coordinator  %s\ntoken             %s\n", qPrintable(coordinator.address()),
                 qPrintable(coordinator.token()));
    std::fflush(stdout);

    int exitCode = 0;
    if (command.hasRender() && drift::projdir::isTemplateFolder(command.project)) {
        QVariantMap settings;
        if (!resolvePreset(command.settings, &settings))
            return 2;
        TemplateBatch batch;
        batch.folder = command.project;
        batch.dataFile = command.data;
        batch.output = command.output;
        batch.row = command.row;
        batch.all = command.all;
        batch.settings = settings;
        batch.target = JobTarget::Farm;
        const TemplateBatchResult result = queueTemplateBatch(queue, batch);
        for (const QString &failure : result.failures)
            std::fprintf(stderr, "%s\n", qPrintable(failure));
        if (!result.error.isEmpty()) {
            std::fprintf(stderr, "%s\n", qPrintable(result.error));
            return 2;
        }
        auto pending = std::make_shared<QSet<QString>>();
        for (const TemplateBatchJob &job : result.jobs) {
            pending->insert(job.id);
            std::fprintf(stdout, "%d\t%s\n", job.row, qPrintable(job.output));
        }
        std::fflush(stdout);
        exitCode = result.failures.isEmpty() ? 0 : 1;
        QObject::connect(&queue, &RenderQueue::jobFinished, &app, [&, pending](const QString &finished, bool ok) {
            if (!pending->remove(finished))
                return;
            if (!ok) {
                std::fprintf(stderr, "render failed: %s\n", qPrintable(queue.find(finished)->error));
                exitCode = 1;
            }
            if (pending->isEmpty())
                QCoreApplication::quit();
        });
        for (const TemplateBatchJob &job : result.jobs)
            queue.start(job.id);
    } else if (command.hasRender()) {
        if (command.output.isEmpty()) {
            std::fprintf(stderr, "--render needs -o <file>\n");
            return 2;
        }
        QVariantMap settings;
        if (!resolvePreset(command.settings, &settings))
            return 2;
        const QString id = queue.addProjectFile(command.project, command.output, settings, JobTarget::Farm, false);
        if (id.isEmpty())
            return 1;
        queue.start(id);
        QObject::connect(&queue, &RenderQueue::jobFinished, &app, [&](const QString &finished, bool ok) {
            if (finished != id)
                return;
            if (!ok)
                std::fprintf(stderr, "render failed: %s\n", qPrintable(queue.find(id)->error));
            exitCode = ok ? 0 : 1;
            QCoreApplication::quit();
        });
    }
    app.exec();
    return exitCode;
}

int runFarmWorker(const FarmCommand &command, QCoreApplication &app, const QString &gpuLine)
{
    FarmWorker::Config config;
    config.coordinator = QUrl(command.url.contains(QLatin1String("://")) ? command.url
                                                                          : QStringLiteral("http://") + command.url);
    config.token = command.token;
    config.name = command.name.isEmpty() ? QHostInfo::localHostName() : command.name;
    config.gpu = gpuLine;
    config.cacheDir = QDir(drift::cacheRoot()).filePath(QStringLiteral("farm-media"));
    config.cacheLimitBytes = static_cast<qint64>(command.cacheGb > 0 ? command.cacheGb : QSettings().value(QStringLiteral("farm/cacheGb"), 20).toInt()) << 30;

    std::atomic<bool> stop{false};
    FarmWorker worker(config);
    QThread *thread = QThread::create([&] {
        worker.run(stop);
        QMetaObject::invokeMethod(&app, &QCoreApplication::quit, Qt::QueuedConnection);
    });
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, [&] { stop = true; });
    thread->start();
    app.exec();
    stop = true;
    thread->wait();
    delete thread;
    return 0;
}

} // namespace

int runFarmCommand(const FarmCommand &command, QCoreApplication &app, const QString &gpuLine)
{
    switch (command.mode) {
    case FarmCommand::Mode::Render:
        return runRender(command, app);
    case FarmCommand::Mode::Worker:
        return runWorkerMode(app);
    case FarmCommand::Mode::FarmCoordinator:
        return runCoordinator(command, app, gpuLine);
    case FarmCommand::Mode::FarmWorker:
        return runFarmWorker(command, app, gpuLine);
    default:
        return 2;
    }
}

} // namespace drift::farm
