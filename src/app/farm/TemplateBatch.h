#pragma once

#include "app/farm/RenderJob.h"

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace drift::farm {

class RenderQueue;

struct TemplateBatch
{
    QString folder;
    QString dataFile; // empty: the template's own data
    QString output;   // pattern with {row} and {param}; empty: the template's
    int row = 0;      // 1-based, used when !all and the data has rows
    bool all = false;
    QJsonObject overrides;
    QVariantMap settings;
    JobTarget target = JobTarget::Local;
    bool hold = false;
};

struct TemplateBatchJob
{
    int row = 0;
    QString id;
    QString output;
};

struct TemplateBatchResult
{
    QString error; // nothing was queued
    QList<TemplateBatchJob> jobs;
    QStringList failures; // rows that did not expand or could not be queued
};

// One queue job per selected data row: the row is expanded into a project snapshot and queued
// unchunked, with {template, row} as the job's source.
TemplateBatchResult queueTemplateBatch(RenderQueue &queue, const TemplateBatch &batch);

} // namespace drift::farm
