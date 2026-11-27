#include "app/editor/InterchangeController.h"

#include "app/bin/AssetLibrary.h"
#include "app/bin/MediaBinController.h"
#include "app/editor/AppController.h"
#include "app/editor/ProjectFileController.h"
#include "core/interchange/InterchangeFormats.h"
#include "core/interchange/TimelineExporter.h"
#include "core/interchange/premiere/MogrtReader.h"

#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QStandardPaths>
#include <QUuid>
#include <QtConcurrent>

#include <algorithm>

QString readTargetPath(const QUrl &url);

using drift::interchange::InterchangeFormat;
using drift::interchange::InterchangeIssue;
using drift::interchange::InterchangeReport;

namespace {

QString translated(const QString &source)
{
    return QCoreApplication::translate("Interchange", source.toUtf8().constData());
}

QString stabilityKey(InterchangeFormat::Stability stability)
{
    switch (stability) {
    case InterchangeFormat::Stability::Stable:
        return QStringLiteral("stable");
    case InterchangeFormat::Stability::Beta:
        return QStringLiteral("beta");
    case InterchangeFormat::Stability::Experimental:
        break;
    }
    return QStringLiteral("experimental");
}

const InterchangeFormat *findFormat(const QString &id, bool forImport)
{
    for (const InterchangeFormat &format : drift::interchange::interchangeFormats()) {
        if (format.id == id && (forImport ? format.canImport : format.canExport))
            return &format;
    }
    return nullptr;
}

QString severityKey(InterchangeIssue::Severity severity)
{
    switch (severity) {
    case InterchangeIssue::Severity::Error:
        return QStringLiteral("error");
    case InterchangeIssue::Severity::Dropped:
        return QStringLiteral("dropped");
    case InterchangeIssue::Severity::Degraded:
        return QStringLiteral("degraded");
    case InterchangeIssue::Severity::Info:
        break;
    }
    return QStringLiteral("info");
}

int severityRank(InterchangeIssue::Severity severity)
{
    switch (severity) {
    case InterchangeIssue::Severity::Error:
        return 0;
    case InterchangeIssue::Severity::Dropped:
        return 1;
    case InterchangeIssue::Severity::Degraded:
        return 2;
    case InterchangeIssue::Severity::Info:
        break;
    }
    return 3;
}

QVariantMap analysisMap(const InterchangeReport &report, bool ok, const QString &error, const QString &path,
                        const QString &formatId, bool forImport, bool isTemplate)
{
    QList<InterchangeIssue> sorted = report.issues;
    std::stable_sort(sorted.begin(), sorted.end(), [](const InterchangeIssue &a, const InterchangeIssue &b) {
        if (severityRank(a.severity) != severityRank(b.severity))
            return severityRank(a.severity) < severityRank(b.severity);
        return a.category < b.category;
    });
    QVariantList issues;
    for (const InterchangeIssue &issue : std::as_const(sorted)) {
        issues.append(QVariantMap{{QStringLiteral("severity"), severityKey(issue.severity)},
                                  {QStringLiteral("category"), issue.category},
                                  {QStringLiteral("message"), issue.message},
                                  {QStringLiteral("itemName"), issue.itemName},
                                  {QStringLiteral("at"), qint64(issue.at)},
                                  {QStringLiteral("count"), issue.count}});
    }

    const InterchangeFormat *format = findFormat(formatId, forImport);
    QVariantMap map;
    map.insert(QStringLiteral("ok"), ok);
    map.insert(QStringLiteral("error"), error);
    map.insert(QStringLiteral("path"), path);
    map.insert(QStringLiteral("fileName"), QFileInfo(path).fileName());
    map.insert(QStringLiteral("formatId"), formatId);
    map.insert(QStringLiteral("formatName"), format ? translated(format->displayName) : QString());
    map.insert(QStringLiteral("stability"),
               format ? stabilityKey(format->stability) : QStringLiteral("experimental"));
    map.insert(QStringLiteral("tracks"), report.tracks);
    map.insert(QStringLiteral("clips"), report.clips);
    map.insert(QStringLiteral("sequences"), report.sequences);
    map.insert(QStringLiteral("assets"), report.assets);
    map.insert(QStringLiteral("missingAssets"), report.missingAssets);
    map.insert(QStringLiteral("isTemplate"), isTemplate);
    map.insert(QStringLiteral("issues"), issues);
    return map;
}

} // namespace

struct InterchangeController::Analysis
{
    bool ok = false;
    QString error;
    QString path;
    QString formatId;
    bool isTemplate = false;
    QDateTime mtime;
    std::shared_ptr<drift::Project> project;
    InterchangeReport report;
};

InterchangeController::InterchangeController(AppController *app, QObject *parent)
    : QObject(parent)
    , m_app(app)
{
    if (qApp)
        qApp->installEventFilter(this);
    connect(m_app->projectFile(), &ProjectFileController::projectLoadFinished, this,
            [this](bool ok, const QString &) {
                if (!m_importing)
                    return;
                m_importing = false;
                setBusy(false);
                emit importFinished(ok);
            });
}

bool InterchangeController::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::LanguageChange && watched == qApp)
        emit formatsChanged();
    return QObject::eventFilter(watched, event);
}

QVariantList InterchangeController::formats() const
{
    QVariantList list;
    for (const InterchangeFormat &format : drift::interchange::interchangeFormats()) {
        QStringList gaps;
        for (const QString &gap : format.knownGaps)
            gaps.append(translated(gap));
        list.append(QVariantMap{{QStringLiteral("id"), format.id},
                                {QStringLiteral("name"), translated(format.displayName)},
                                {QStringLiteral("extensions"), format.extensions},
                                {QStringLiteral("canImport"), format.canImport},
                                {QStringLiteral("canExport"), format.canExport},
                                {QStringLiteral("stability"), stabilityKey(format.stability)},
                                {QStringLiteral("knownGaps"), gaps}});
    }
    return list;
}

void InterchangeController::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit busyChanged();
}

void InterchangeController::clear()
{
    ++m_generation;
    m_importAfterAnalysis = false;
    m_project.reset();
    m_report = {};
    m_path.clear();
    m_formatId.clear();
    m_isTemplate = false;
    if (!m_importing)
        setBusy(false);
    if (!m_analysis.isEmpty()) {
        m_analysis.clear();
        emit analysisChanged();
    }
}

void InterchangeController::analyze(const QUrl &url)
{
    if (m_importing)
        return;
    clear();
    const int generation = m_generation;
    setBusy(true);
    (void)QtConcurrent::run([this, url, generation]() {
        auto result = std::make_shared<Analysis>();
        result->path = readTargetPath(url);
        if (result->path.isEmpty()) {
            result->error = tr("That file location isn’t valid");
        } else {
            result->mtime = QFileInfo(result->path).lastModified();
            result->formatId = drift::interchange::detectImportFormat(result->path);
            QString error;
            if (result->formatId.isEmpty()) {
                result->error = tr("Drift doesn’t recognise this file as an importable format.");
            } else if (result->formatId == QLatin1String("mogrt")) {
                result->isTemplate = true;
                result->report.formatId = result->formatId;
                drift::mogrt::MogrtTemplate tmpl;
                result->ok = drift::mogrt::analyzeTemplate(result->path, &tmpl, &result->report, &error);
                if (!result->ok)
                    result->error = error.isEmpty() ? tr("Failed to read the Motion Graphics Template") : error;
            } else {
                const std::optional<drift::Project> project =
                    drift::interchange::readImport(result->formatId, result->path, &result->report, &error);
                result->ok = project.has_value();
                if (project)
                    result->project = std::make_shared<drift::Project>(*project);
                else
                    result->error = error.isEmpty() ? tr("Failed to read this file") : error;
            }
        }
        QMetaObject::invokeMethod(
            this, [this, generation, result]() { applyAnalysis(generation, result); }, Qt::QueuedConnection);
    });
}

void InterchangeController::applyAnalysis(int generation, const std::shared_ptr<Analysis> &result)
{
    if (generation != m_generation)
        return;
    m_project = result->project;
    m_report = result->report;
    m_path = result->path;
    m_formatId = result->formatId;
    m_mtime = result->mtime;
    m_isTemplate = result->isTemplate;
    m_analysis = analysisMap(result->report, result->ok, result->error, result->path, result->formatId, true,
                             result->isTemplate);
    emit analysisChanged();

    const bool wantedImport = m_importAfterAnalysis;
    m_importAfterAnalysis = false;
    if (wantedImport && result->ok) {
        startImport();
        return;
    }
    setBusy(false);
    if (wantedImport)
        emit importFinished(false);
}

void InterchangeController::importAnalyzed()
{
    if (m_busy || m_path.isEmpty() || !m_analysis.value(QStringLiteral("ok")).toBool()) {
        emit importFinished(false);
        return;
    }
    if (QFileInfo(m_path).lastModified() != m_mtime) {
        const QString path = m_path;
        analyze(QUrl::fromLocalFile(path));
        m_importAfterAnalysis = true;
        return;
    }
    startImport();
}

void InterchangeController::startImport()
{
    if (m_isTemplate) {
        importTemplate();
        return;
    }
    if (!m_project) {
        emit importFinished(false);
        return;
    }
    setBusy(true);
    m_importing = true;
    if (!m_app->projectFile()->loadInterchangeProject(*m_project, m_report)) {
        m_importing = false;
        setBusy(false);
        emit importFinished(false);
    }
}

void InterchangeController::importTemplate()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString destDir =
        QDir(base).filePath(QStringLiteral("templates/%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    QDir().mkpath(destDir);

    QString readError;
    const std::optional<drift::mogrt::MogrtTemplate> tmpl = drift::mogrt::readTemplate(m_path, destDir, &readError);
    if (!tmpl) {
        m_app->setLastMessage(readError.isEmpty() ? tr("Failed to unpack Motion Graphics Template") : readError,
                              QStringLiteral("error"));
        setBusy(false);
        emit importFinished(false);
        return;
    }

    const drift::Project before = m_app->m_project;

    drift::TimeUs insertTime = 0;
    bool hasClips = false;
    for (const drift::Track &t : m_app->m_project.tracks()) {
        if (!t.clips.isEmpty()) {
            hasClips = true;
            break;
        }
    }
    if (hasClips)
        insertTime = m_app->m_playheadUs;

    QString applyError;
    if (!drift::mogrt::applyTemplateToProject(*tmpl, m_app->m_project, insertTime, &applyError)) {
        m_app->setLastMessage(applyError.isEmpty() ? tr("Failed to apply template to project") : applyError,
                              QStringLiteral("error"));
        setBusy(false);
        emit importFinished(false);
        return;
    }

    m_app->pushProjectEdit(before, tr("Import template: %1").arg(tmpl->title));

    if (m_app->m_assetLibrary)
        m_app->m_assetLibrary->setProject(&m_app->m_project);
    m_app->m_bin->folderModel()->setProject(&m_app->m_project);

    m_app->projectFile()->setDirty(true);
    m_app->finishEdit(tr("Template imported: %1").arg(tmpl->title));
    m_app->setLastMessage(tr("Template imported: %1").arg(tmpl->title), QStringLiteral("success"));
    setBusy(false);
    emit importFinished(true);
}

QVariantMap InterchangeController::analyzeExport(const QString &formatId)
{
    const InterchangeReport report = drift::interchange::analyzeExport(m_app->m_project, formatId);
    return analysisMap(report, true, QString(), QString(), formatId, false, false);
}

void InterchangeController::exportTo(const QString &formatId, const QUrl &url)
{
    const QString path = url.toLocalFile();
    if (path.isEmpty()) {
        m_app->setLastMessage(tr("That export location isn’t valid"), QStringLiteral("error"));
        return;
    }
    QString error;
    const QString format = formatId == QLatin1String("tsv") ? QStringLiteral("txt") : formatId;
    if (!drift::interchange::exportProjectInterchange(m_app->m_project, path, format, &error)) {
        m_app->setLastMessage(error.isEmpty() ? tr("Export failed") : error, QStringLiteral("error"));
        return;
    }
    m_app->setLastMessage(tr("Exported %1").arg(QFileInfo(path).fileName()), QStringLiteral("success"));
}
