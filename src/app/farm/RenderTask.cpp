#include "app/farm/RenderTask.h"

#include "app/editor/ProjectFileController.h"
#include "core/base/CacheRoot.h"
#include "core/project/Project.h"
#include "engine/encode/export/Exporter.h"
#include "engine/project/ProjectBundle.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>

extern "C" {
#include <libavformat/avformat.h>
}

#include <cmath>
#include <climits>

namespace drift::farm {

QJsonObject RenderTask::toJson() const
{
    QJsonObject remap;
    for (auto it = pathRemap.begin(); it != pathRemap.end(); ++it)
        remap.insert(it.key(), it.value());
    return QJsonObject{{QStringLiteral("id"), id},
                       {QStringLiteral("projectPath"), projectPath},
                       {QStringLiteral("settings"), QJsonObject::fromVariantMap(settings)},
                       {QStringLiteral("outputPath"), outputPath},
                       {QStringLiteral("pathRemap"), remap},
                       {QStringLiteral("muteAudio"), muteAudio}};
}

RenderTask RenderTask::fromJson(const QJsonObject &object)
{
    RenderTask task;
    task.id = object.value(QStringLiteral("id")).toString();
    task.projectPath = object.value(QStringLiteral("projectPath")).toString();
    task.settings = object.value(QStringLiteral("settings")).toObject().toVariantMap();
    task.outputPath = object.value(QStringLiteral("outputPath")).toString();
    const QJsonObject remap = object.value(QStringLiteral("pathRemap")).toObject();
    for (auto it = remap.begin(); it != remap.end(); ++it)
        task.pathRemap.insert(it.key(), it.value().toString());
    task.muteAudio = object.value(QStringLiteral("muteAudio")).toBool();
    return task;
}

bool loadProjectDocument(const QString &path, drift::Project *out, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("Could not read %1").arg(path);
        return false;
    }
    const QByteArray head = file.peek(8);
    QJsonObject root;
    QHash<QString, QString> remap;
    if (head.startsWith("DRIFTPRJ")) {
        file.close();
        const auto info = drift::bundle::readManifest(path, error);
        if (!info)
            return false;
        root = info->document;
        const QString key = QString::fromLatin1(
            QCryptographicHash::hash(QFileInfo(path).canonicalFilePath().toUtf8(), QCryptographicHash::Sha1)
                .toHex().left(16));
        const QString dest = QDir(drift::cacheRoot()).filePath(QStringLiteral("farm/bundles/") + key);
        QDir().mkpath(dest);
        if (!drift::bundle::extract(path, dest, {}, &remap, error))
            return false;
    } else {
        QJsonParseError parse;
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parse);
        if (!doc.isObject()) {
            *error = QStringLiteral("%1 is not a Drift project").arg(QFileInfo(path).fileName());
            return false;
        }
        root = doc.object();
    }
    QString parseError;
    *out = drift::Project::fromJson(root, &parseError);
    if (!parseError.isEmpty()) {
        *error = parseError;
        return false;
    }
    if (!remap.isEmpty())
        ProjectFileController::remapPaths(*out, remap);
    return true;
}

QString withContainerSuffix(const QVariantMap &settingsMap, const QString &path)
{
    if (!QFileInfo(path).suffix().isEmpty())
        return path;
    const ExportSettings settings = Exporter::settingsFromMap(settingsMap);
    const QString container = settings.gifExport
        ? QStringLiteral("gif")
        : settings.audioOnly ? Exporter::preferredAudioOnlyContainer(settings.audioCodecId)
                             : Exporter::preferredContainer(settings.videoCodecId, settings.audioCodecId);
    return path + QLatin1Char('.') + Exporter::defaultSuffix(container, settings.audioOnly);
}

qint64 countVideoFrames(const QString &path)
{
    AVFormatContext *fmt = nullptr;
    const QByteArray utf8 = path.toUtf8();
    if (avformat_open_input(&fmt, utf8.constData(), nullptr, nullptr) < 0)
        return -1;
    qint64 frames = -1;
    if (avformat_find_stream_info(fmt, nullptr) >= 0) {
        const int index = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        if (index >= 0) {
            frames = 0;
            AVPacket *pkt = av_packet_alloc();
            while (av_read_frame(fmt, pkt) >= 0) {
                if (pkt->stream_index == index)
                    ++frames;
                av_packet_unref(pkt);
            }
            av_packet_free(&pkt);
        }
    }
    avformat_close_input(&fmt);
    return frames;
}

qint64 expectedFrames(const drift::Project &project, const QVariantMap &settingsMap)
{
    const ExportSettings settings = Exporter::settingsFromMap(settingsMap);
    drift::TimeUs start = 0;
    drift::TimeUs end = 0;
    QString error;
    resolveExportRange(project, settings, &start, &end, &error);
    if (end <= start)
        return 0;
    int num = qMax(1, project.fps());
    int den = 1;
    if (settings.fpsNum > 0 && settings.fpsDen > 0) {
        num = settings.fpsNum;
        den = settings.fpsDen;
    }
    av_reduce(&num, &den, num, den, INT_MAX);
    const double fps = double(num) / double(den);
    return qMax<qint64>(1, std::llround(double(end - start) * fps / 1e6));
}

TaskResult runTask(const RenderTask &task, const ProgressFn &progress)
{
    TaskResult result;
    drift::Project loaded;
    if (!loadProjectDocument(task.projectPath, &loaded, &result.error))
        return result;
    if (!task.pathRemap.isEmpty())
        ProjectFileController::remapPaths(loaded, task.pathRemap);
    if (task.muteAudio) {
        loaded.forEachTrackList([](QList<drift::Track> &tracks) {
            for (drift::Track &track : tracks)
                track.muted = true;
        });
    }

    const drift::Project project = loaded.exportView({});
    ExportSettings settings = Exporter::settingsFromMap(task.settings);
    // Bakes belong to the editor's project cache and name files on its machine.
    settings.useBakes = false;
    settings.bakes = nullptr;

    const QString output = withContainerSuffix(task.settings, task.outputPath);
    result.outputPath = output;
    QDir().mkpath(QFileInfo(output).absolutePath());
    if (!Exporter::run(project, settings, output, &result.error, progress)) {
        if (result.error.isEmpty())
            result.error = QStringLiteral("Export failed");
        return result;
    }
    result.frames = settings.audioOnly || settings.gifExport ? 0 : countVideoFrames(output);
    result.ok = true;
    return result;
}

} // namespace drift::farm
