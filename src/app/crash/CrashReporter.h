#pragma once

#include "app/crash/CrashMeta.h"
#include "app/crash/CrashStore.h"

#include <QList>
#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

// The app-facing side of crash reporting: tells the startup dialog what happened last time,
// builds the saved bundle, and uploads the report in the background. Capture itself is
// platform/CrashHandler_win.cpp; the decisions and file formats are in the crash/ helpers.
class CrashReporter : public QObject
{
    Q_OBJECT
    // Capture and the startup dialog exist on Windows only for now.
    Q_PROPERTY(bool supported READ supported CONSTANT)
    // "none", "crash" or "unclean" (no crash record, the previous run just did not quit).
    Q_PROPERTY(QString startupKind READ startupKind CONSTANT)
    Q_PROPERTY(QString reasonText READ reasonText CONSTANT)
    Q_PROPERTY(QString reasonLinkUrl READ reasonLinkUrl CONSTANT)
    Q_PROPERTY(QString reasonLinkLabel READ reasonLinkLabel CONSTANT)
    Q_PROPERTY(bool reportWillBeSent READ reportWillBeSent CONSTANT)
    Q_PROPERTY(bool automaticReports READ automaticReports WRITE setAutomaticReports NOTIFY automaticReportsChanged)
    Q_PROPERTY(bool safeMode READ safeMode CONSTANT)
    // Outcome of the last save, ready to show.
    Q_PROPERTY(QString saveMessage READ saveMessage NOTIFY saveMessageChanged)

public:
    explicit CrashReporter(QObject *parent = nullptr);

    // Windows: marks this session running, finds out how the last one ended, and prepares what a
    // crash upload needs. Call once, after the translators are installed.
    void begin();
    // Uploads whatever is pending, or discards it when the setting is off. Never blocks.
    void startUploads();

    bool supported() const;
    QString startupKind() const { return m_kind; }
    QString reasonText() const { return m_reasonText; }
    QString reasonLinkUrl() const { return m_reasonLinkUrl; }
    QString reasonLinkLabel() const { return m_reasonLinkLabel; }
    bool reportWillBeSent() const { return m_kind == QLatin1String("crash") && automaticReports(); }
    bool automaticReports() const;
    void setAutomaticReports(bool enabled);
    bool safeMode() const;
    QString saveMessage() const { return m_saveMessage; }

    // Asks for a folder and writes the bundle there: the crash's when there was one, otherwise
    // the same diagnostics the Help menu offers.
    Q_INVOKABLE void saveCrashFile();
    Q_INVOKABLE void saveDiagnostics();
    Q_INVOKABLE void restartInSafeMode();
    // The dialog's Continue.
    Q_INVOKABLE void dismiss();

signals:
    void automaticReportsChanged();
    void saveMessageChanged();
    void dismissed();

private:
    void saveBundle(const drift::crash::CrashInfo *crash, const QString &prefix);
    drift::crash::SystemInfo gatherSystemInfo() const;
    void uploadNext();
    void finishUpload(QNetworkReply *reply);

    drift::crash::CrashStore m_store;
    QString m_kind = QStringLiteral("none");
    QString m_reasonText;
    QString m_reasonLinkUrl;
    QString m_reasonLinkLabel;
    QString m_reasonKey;
    QList<drift::crash::CrashInfo> m_unshown;
    QString m_saveMessage;

    QNetworkAccessManager *m_network = nullptr;
    QList<drift::crash::CrashInfo> m_queue;
    drift::crash::SystemInfo m_system;
};
