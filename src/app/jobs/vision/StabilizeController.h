#pragma once

#include <QAtomicInt>
#include <QMap>
#include <QObject>
#include <QSet>
#include <QSharedPointer>
#include <QString>

#include <functional>

#include "core/base/Time.h"

class AppController;

// Clip stabilization, reached from QML as EditorState.stabilize.
class StabilizeController : public QObject
{
    Q_OBJECT

public:
    explicit StabilizeController(AppController *app, QObject *parent = nullptr);

    Q_INVOKABLE void run(int trackIndex, int clipIndex);
    Q_INVOKABLE void cancel(int trackIndex, int clipIndex);
    Q_INVOKABLE void remove(int trackIndex, int clipIndex);
    Q_INVOKABLE void setSmoothing(int trackIndex, int clipIndex, int value);
    Q_INVOKABLE void setTripod(int trackIndex, int clipIndex, bool enabled);
    Q_INVOKABLE void setMode(int trackIndex, int clipIndex, const QString &mode);

    // The two vid.stab passes' filter chains and the cached motion-analysis file for a source
    // range, shared with the clip stabilizer so a range analysed by one is not scanned again by
    // the other. The analysis path does not exist until a scan has been stored there.
    static QString detectFilter();
    static QString transformFilter(int smoothing, bool tripod);
    static QString analysisPath(const QString &sourcePath, drift::TimeUs rangeIn,
                                drift::TimeUs rangeOut);

    // Projects saved before stabilized videos were bin assets kept the render on the clip; this
    // makes each one a "(stabilized)" asset the clip reads instead. Runs on load, after bundle
    // paths are remapped.
    void migrateLegacyClips();
    double progressFor(const QString &clipId) const { return m_progress.value(clipId, 0.0); }
    // Clips being stabilized right now.
    int activeCount() const { return int(m_jobs.size()); }
    QString statusFor(const QString &clipId) const { return m_status.value(clipId); }

private:
    // Runs on the stabilize worker: maps a job's 0..1 onto [rangeFrom, rangeTo] of the clip's
    // progress, posts it to the UI thread, and returns false once `cancel` is set.
    std::function<bool(double)> progressReporter(const QString &clipId,
                                                 const QSharedPointer<QAtomicInt> &cancel,
                                                 double rangeFrom, double rangeTo);
    void setProgress(const QString &clipId, double progress, const QString &status, bool force);
    void clearProgress(const QString &clipId);

    AppController *m_app = nullptr;
    QMap<QString, QSharedPointer<QAtomicInt>> m_jobs;
    QMap<QString, double> m_progress;
    QMap<QString, QString> m_status;
    QMap<QString, qint64> m_lastProgressEmit;
    QSet<QString> m_cancelRequested;
};
