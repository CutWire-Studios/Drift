#pragma once

#include "core/interchange/InterchangeReport.h"

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <memory>

class AppController;

namespace drift {
class Project;
}

// File > Import/Export: dry-run analysis of one picked file, import of the analysed result, and
// export with a preview of what is lost. Reached from QML as EditorState.interchange.
class InterchangeController : public QObject
{
    friend class AppController;

    Q_OBJECT

    // One row per format and direction: {id, name, extensions, canImport, canExport, stability, knownGaps}.
    // Names and gaps are translated; formatsChanged fires when the UI language changes.
    Q_PROPERTY(QVariantList formats READ formats NOTIFY formatsChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    // Empty until a file has been analysed. {ok, error, path, fileName, formatId, formatName, stability,
    // tracks, clips, sequences, assets, missingAssets, isTemplate, issues:[{severity, category, message,
    // itemName, at, count}]}.
    Q_PROPERTY(QVariantMap analysis READ analysis NOTIFY analysisChanged)

public:
    explicit InterchangeController(AppController *app, QObject *parent = nullptr);

    QVariantList formats() const;
    bool busy() const { return m_busy; }
    QVariantMap analysis() const { return m_analysis; }

    Q_INVOKABLE void analyze(const QUrl &url);
    Q_INVOKABLE void importAnalyzed();
    Q_INVOKABLE QVariantMap analyzeExport(const QString &formatId);
    Q_INVOKABLE void exportTo(const QString &formatId, const QUrl &url);
    Q_INVOKABLE void clear();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

signals:
    void formatsChanged();
    void busyChanged();
    void analysisChanged();
    void importFinished(bool ok);

private:
    struct Analysis;

    void setBusy(bool busy);
    void applyAnalysis(int generation, const std::shared_ptr<Analysis> &result);
    void startImport();
    void importTemplate();

    AppController *m_app = nullptr;
    bool m_busy = false;
    bool m_importAfterAnalysis = false;
    bool m_importing = false;
    int m_generation = 0;
    QVariantMap m_analysis;
    std::shared_ptr<drift::Project> m_project;
    drift::interchange::InterchangeReport m_report;
    QString m_path;
    QString m_formatId;
    QDateTime m_mtime;
    bool m_isTemplate = false;
};
