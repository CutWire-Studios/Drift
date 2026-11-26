#pragma once

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QVariantMap>

#include <functional>

namespace drift {
class Project;
}

// What one worker executes: render `settings` of the project at `projectPath` to `outputPath`.
// The same shape runs in the editor's own process, in a local `--headless --worker` child and on a
// farm worker, so queue, farm and CLI all go through runTask(). A chunk of a farm job is a task
// whose settings carry a startUs/endUs slice and `muteAudio`.
namespace drift::farm {

struct RenderTask
{
    QString id;
    // A project JSON document or a .drift bundle.
    QString projectPath;
    // The export dialog's map: Exporter::settingsFromMap() keys, plus startUs/endUs.
    QVariantMap settings;
    QString outputPath;
    // originalPath -> where this machine has the file (a farm worker's cache).
    QHash<QString, QString> pathRemap;
    // Chunks carry no audio: it is rendered once and joined in afterwards.
    bool muteAudio = false;

    QJsonObject toJson() const;
    static RenderTask fromJson(const QJsonObject &object);
};

struct TaskResult
{
    bool ok = false;
    QString error;
    QString outputPath; // where the file landed: the task's, plus the container's extension if it had none
    qint64 frames = 0; // video frames in the finished file, counted by demuxing it
};

using ProgressFn = std::function<bool(double)>;

TaskResult runTask(const RenderTask &task, const ProgressFn &progress);

// Loads a project JSON or .drift bundle (embedded media is unpacked under the cache) into `out`.
bool loadProjectDocument(const QString &path, drift::Project *out, QString *error);

// `path` with the extension the settings' container implies, if it has none.
QString withContainerSuffix(const QVariantMap &settings, const QString &path);

// Video packets in the file's first video stream, or -1.
qint64 countVideoFrames(const QString &path);

// Frames Exporter::run will encode for the range, by its own rule.
qint64 expectedFrames(const drift::Project &project, const QVariantMap &settings);

} // namespace drift::farm
