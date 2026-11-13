#pragma once

#include <QDateTime>
#include <QList>
#include <QString>

#include <functional>

namespace drift::crash {

// What the crash handler wrote to <crashes>/<id>/crash.json.
struct CrashInfo
{
    QString id;
    QString dir;
    QString code;          // "0xC0000005", or "0xB" for SIGSEGV
    QString module;        // file name only
    QString modulePath;    // full path; stays in the saved bundle, never uploaded
    QString moduleVersion;
    QString offset;
    QDateTime crashedAt;
    int uptimeSeconds = 0;

    bool valid() const { return !id.isEmpty() && !code.isEmpty(); }
};

CrashInfo readCrashInfo(const QString &crashDir);

constexpr int kMaxUploadAttempts = 5;
constexpr int kUploadGiveUpDays = 30;

// The on-disk state behind crash capture and the next launch's decisions, rooted at the app's
// local data folder:
//   sessions/<pid>.running     present while that process is alive and has not quit cleanly
//   crashes/<id>/crash.json    written by the crash handler, with upload.dmp and full.dmp beside it
//                              (Windows) or stack.txt (Linux, macOS)
//   crashes/<id>/shown         the user has been told about this crash
//   crashes/<id>/upload.json   {"attempts": n, "done": bool}
//   crashes/<id>/upload-log.txt the scrubbed log tail that goes with the upload
class CrashStore
{
public:
    explicit CrashStore(const QString &root);

    QString crashesDir() const { return m_root + QStringLiteral("/crashes"); }
    QString sessionsDir() const { return m_root + QStringLiteral("/sessions"); }

    void beginSession(qint64 pid) const;
    void endSession(qint64 pid) const;

    struct Startup
    {
        // Crashes the user has not been told about, newest first.
        QList<CrashInfo> crashes;
        // Sessions that ended without quitting and without leaving a crash record.
        int uncleanSessions = 0;
    };
    // Reads the markers of sessions `alive` says are gone, removes them, and lists the crashes
    // not yet shown. Does not mark them shown.
    Startup scan(const std::function<bool(qint64)> &alive) const;

    void markShown(const CrashInfo &crash) const;

    // Crashes that still want an upload, oldest first.
    QList<CrashInfo> pendingUploads(const QDateTime &now) const;
    int attempts(const CrashInfo &crash) const;
    void recordAttempt(const CrashInfo &crash) const;
    void markDone(const CrashInfo &crash) const;

    // Drops what is finished with and has been shown. A crash that was never shown is kept.
    void sweep(const QDateTime &now) const;

private:
    bool finished(const CrashInfo &crash, const QDateTime &now) const;

    QString m_root;
};

} // namespace drift::crash
