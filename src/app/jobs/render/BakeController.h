#pragma once

#include "core/base/Time.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QPair>
#include <QSet>
#include <QString>
#include <QTimer>
#include <QVariantList>

#include <functional>
#include <list>
#include <memory>

namespace drift {
struct Bake;
class BakeFile;
class BakeIndex;
class BakeReaderPool;
class Project;
} // namespace drift

class AppController;

// Range bakes for the open project, reached from QML as EditorState.bakes. Owns the project's
// BakeFile (cacheRoot()/bakes/<projectId>.driftbake), runs bake jobs one at a time on the render
// lane, publishes the committed index to preview, and tells fresh bakes from stale ones by
// comparing each bake's stored fingerprint with the project as it is now.
class BakeController : public QObject
{
    Q_OBJECT
    // [{id, start, end (seconds), startFrame, endFrame, state: "fresh"|"stale"|"baking",
    //   progress (0..1, baking only), bytes, quality (0..2), qualityLabel, width, height}], by start.
    // The running job shows as a "baking" row with id "pending" until its first commit.
    Q_PROPERTY(QVariantList bakes READ bakes NOTIFY bakesChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    // Preset for new bakes: 0 compact, 1 balanced, 2 high. Stored in the bake file when there is one,
    // and always as the global default.
    Q_PROPERTY(int bakeQuality READ bakeQuality WRITE setBakeQuality NOTIFY bakeQualityChanged)
    Q_PROPERTY(double totalBytes READ totalBytes NOTIFY bakesChanged)
    Q_PROPERTY(int staleCount READ staleCount NOTIFY bakesChanged)

public:
    explicit BakeController(AppController *app, QObject *parent = nullptr);

    QVariantList bakes() const;
    bool busy() const { return m_busy; }
    double progress() const { return m_progress; }
    int bakeQuality() const;
    void setBakeQuality(int preset);
    double totalBytes() const;
    int staleCount() const { return m_staleIds.size(); }

    // Bakes the work area at the current quality. Queued behind a running bake.
    Q_INVOKABLE void bakeWorkArea();
    // Same range as bake `id`, at the current quality.
    Q_INVOKABLE void rebake(const QString &id);
    Q_INVOKABLE void rebakeAllStale();
    // Stops the running bake (frames baked so far are kept) and drops the queue.
    Q_INVOKABLE void cancelBake();
    Q_INVOKABLE void deleteBake(const QString &id);
    Q_INVOKABLE void deleteAllBakes();
    Q_INVOKABLE qint64 estimateBytesPerMinute(int preset) const;
    // Stale bakes overlapping [startSeconds, endSeconds), in the same shape as `bakes`.
    Q_INVOKABLE QVariantList staleBakesIn(double startSeconds, double endSeconds);
    Q_INVOKABLE void setWorkAreaToBake(const QString &id);

    // The reader pool an export should use: the preview's pool with every bake (includeStale), or
    // a private one without the stale bakes. Null when there are no bakes. Bakes overlapping any
    // of `skip` (main-timeline [start, end) spans) are left out too: a bake is the picture as the
    // preview shows it, review tracks included, which an export must not reuse.
    std::shared_ptr<drift::BakeReaderPool> exportPool(bool includeStale,
                                                      const QList<QPair<drift::TimeUs, drift::TimeUs>> &skip = {});

signals:
    void bakesChanged();
    void busyChanged();
    void progressChanged();
    void bakeQualityChanged();

private:
    struct Request
    {
        qint64 startFrame = 0;
        qint64 endFrame = 0;
    };
    struct JobState;

    void syncProject();
    void releaseFile();
    bool ensureFile();
    void publish();
    void recomputeStale();
    void scheduleStaleCheck();
    void enqueue(Request request);
    void startNext();
    void onJobDone(const std::shared_ptr<JobState> &state, bool cancelled, const QString &error);
    void onProgress(double fraction);
    void whenIdle(std::function<void()> op);
    void maybeCompact();
    Request requestForUs(qint64 startUs, qint64 endUs) const;
    QVariantMap bakeMap(const drift::Bake &bake, const QString &state) const;

    AppController *m_app = nullptr;
    std::shared_ptr<drift::BakeFile> m_file;
    QString m_boundId;
    std::shared_ptr<const drift::BakeIndex> m_index;
    QSet<QString> m_staleIds;
    QTimer m_staleTimer;

    QList<Request> m_queue;
    std::list<std::function<void()>> m_afterJob;
    bool m_busy = false;
    bool m_detached = false;
    QString m_jobId;
    double m_progress = 0.0;
    qint64 m_activeStart = 0;
    qint64 m_activeEnd = 0;
    QSet<QString> m_activePreIds;
};
