#include "app/farm/RenderJob.h"

#include <QJsonArray>

namespace drift::farm {

QString jobStateName(JobState state)
{
    switch (state) {
    case JobState::Held: return QStringLiteral("held");
    case JobState::Queued: return QStringLiteral("queued");
    case JobState::Running: return QStringLiteral("running");
    case JobState::Done: return QStringLiteral("done");
    case JobState::Failed: return QStringLiteral("failed");
    case JobState::Cancelled: return QStringLiteral("cancelled");
    }
    return QStringLiteral("held");
}

JobState jobStateFromName(const QString &name)
{
    for (const JobState state : {JobState::Held, JobState::Queued, JobState::Running, JobState::Done,
                                 JobState::Failed, JobState::Cancelled}) {
        if (jobStateName(state) == name)
            return state;
    }
    return JobState::Held;
}

QJsonObject RenderJob::toJson() const
{
    return QJsonObject{{QStringLiteral("id"), id},
                       {QStringLiteral("name"), name},
                       {QStringLiteral("projectPath"), projectPath},
                       {QStringLiteral("settings"), QJsonObject::fromVariantMap(settings)},
                       {QStringLiteral("output"), output},
                       {QStringLiteral("target"), target == JobTarget::Farm ? QStringLiteral("farm")
                                                                            : QStringLiteral("local")},
                       {QStringLiteral("state"), jobStateName(state)},
                       {QStringLiteral("progress"), progress},
                       {QStringLiteral("error"), error},
                       {QStringLiteral("log"), QJsonArray::fromStringList(log)},
                       {QStringLiteral("created"), created.toString(Qt::ISODate)},
                       {QStringLiteral("started"), started.toString(Qt::ISODate)},
                       {QStringLiteral("finished"), finished.toString(Qt::ISODate)},
                       {QStringLiteral("source"), source}};
}

RenderJob RenderJob::fromJson(const QJsonObject &object)
{
    RenderJob job;
    job.id = object.value(QStringLiteral("id")).toString();
    job.name = object.value(QStringLiteral("name")).toString();
    job.projectPath = object.value(QStringLiteral("projectPath")).toString();
    job.settings = object.value(QStringLiteral("settings")).toObject().toVariantMap();
    job.output = object.value(QStringLiteral("output")).toString();
    job.target = object.value(QStringLiteral("target")).toString() == QLatin1String("farm") ? JobTarget::Farm
                                                                                              : JobTarget::Local;
    job.state = jobStateFromName(object.value(QStringLiteral("state")).toString());
    job.progress = object.value(QStringLiteral("progress")).toDouble();
    job.error = object.value(QStringLiteral("error")).toString();
    for (const QJsonValue &line : object.value(QStringLiteral("log")).toArray())
        job.log.append(line.toString());
    job.created = QDateTime::fromString(object.value(QStringLiteral("created")).toString(), Qt::ISODate);
    job.started = QDateTime::fromString(object.value(QStringLiteral("started")).toString(), Qt::ISODate);
    job.finished = QDateTime::fromString(object.value(QStringLiteral("finished")).toString(), Qt::ISODate);
    job.source = object.value(QStringLiteral("source")).toObject();
    return job;
}

void RenderJob::appendLog(const QString &line)
{
    log.append(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss ")) + line);
    while (log.size() > 200)
        log.removeFirst();
}

} // namespace drift::farm
