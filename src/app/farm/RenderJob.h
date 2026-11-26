#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace drift::farm {

enum class JobState { Held, Queued, Running, Done, Failed, Cancelled };
enum class JobTarget { Local, Farm };

QString jobStateName(JobState state);
JobState jobStateFromName(const QString &name);

// One slice of a farm job: a frame range rendered by one worker, joined with the others after.
struct JobChunk
{
    enum class State { Pending, Running, Done };

    int index = 0;
    qint64 startFrame = 0;
    qint64 frames = 0;
    qint64 startUs = 0;
    qint64 endUs = 0;
    State state = State::Pending;
    int attempts = 0;
    double progress = 0.0;
    QString worker;
    QString file; // the finished slice, on the coordinator
};

// What the render queue stores per job. The project is a snapshot file the queue owns, so a job
// renders what the timeline was when it was queued, and survives edits and restarts.
struct RenderJob
{
    QString id;
    QString name;
    QString projectPath;
    QVariantMap settings;
    QString output;
    JobTarget target = JobTarget::Local;
    JobState state = JobState::Held;
    double progress = 0.0;
    QString error;
    QStringList log;
    QDateTime created;
    QDateTime started;
    QDateTime finished;
    QString worker;
    // Where a batch job hangs its template and data row ({"template": path, "row": {...}}); a
    // job with an empty source is a plain project render.
    QJsonObject source;

    // Farm runtime only; rebuilt from scratch when a job is queued again.
    QList<JobChunk> chunks;
    bool chunked = false;
    bool cancelRequested = false;

    QJsonObject toJson() const;
    static RenderJob fromJson(const QJsonObject &object);
    void appendLog(const QString &line);
};

} // namespace drift::farm
