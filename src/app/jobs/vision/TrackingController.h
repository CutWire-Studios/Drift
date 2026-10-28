#pragma once

#include "core/base/Time.h"
#include "core/project/Project.h"

#include <QAtomicInt>
#include <QObject>
#include <QPointF>
#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

#include <functional>

class AppController;
class TrackerListModel;

// Point and planar motion tracking on a media clip, and turning a track into mask or transform
// keys. Reached from QML as EditorState.tracking. The trackers themselves live on
// Clip::motionTrackers, so the project stays the single source of truth: a worker thread only
// ever sees a snapshot, and posts batches of samples back to be merged by clip and tracker id.
//
// Every call addresses the tracked MEDIA clip by (trackIndex, clipIndex) and the tracker by id.
class TrackingController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString busyTrackerId READ busyTrackerId NOTIFY busyChanged)
    Q_PROPERTY(TrackerListModel *trackerList READ trackerList CONSTANT)

public:
    explicit TrackingController(AppController *app, QObject *parent = nullptr);

    bool busy() const { return m_busy; }
    double progress() const { return m_progress; }
    QString busyTrackerId() const { return m_busyTrackerId; }
    TrackerListModel *trackerList() const { return m_trackerList; }

    // [{id, name, kind: "point"|"planar", solve, minConfidence, pointCount, referenceSeconds,
    //   targets: [{clipId, mode, name}], sampleRange: [startSec, endSec]}]. Seconds are timeline.
    // sampleRange is empty when nothing is tracked yet.
    Q_INVOKABLE QVariantList trackers(int trackIndex, int clipIndex) const;
    // What the overlay draws at a timeline time: {points: [{x, y, confidence, userKey, patternW,
    // patternH, searchW, searchH, has}], region: [{x, y}], trail: [[{x, y}], ...]}. Positions are
    // normalized to the clip frame. region is the planar outline mapped through the homography
    // (the reference outline when untracked there); empty for point trackers. trail is the
    // tracked path over +-15 frames, one list per point.
    Q_INVOKABLE QVariantMap trackerFrame(int trackIndex, int clipIndex, const QString &id,
                                         double timelineSeconds) const;
    // Per bucket over the clip's timeline span: -1 = no data, else the lowest confidence in it.
    // Planar trackers store no confidence, so a tracked bucket reads 1.
    Q_INVOKABLE QVariantList confidenceStrip(int trackIndex, int clipIndex, const QString &id,
                                             int buckets) const;

    // Normalized position at the playhead. Reference = the playhead's source time. Returns the id,
    // or an empty string when the clip cannot be tracked. Selects nothing.
    Q_INVOKABLE QString addPointTracker(int trackIndex, int clipIndex, double x, double y);
    // region: >= 4 normalized {x, y}.
    Q_INVOKABLE QString addPlanarTracker(int trackIndex, int clipIndex, const QVariantList &region);
    // Point trackers hold up to 4 points; more points allow rotation and scale solves.
    Q_INVOKABLE void addTrackerPoint(int trackIndex, int clipIndex, const QString &id, double x, double y);
    // A user correction at the playhead: writes a key sample there. Later tracking restarts from
    // it and never overwrites it.
    Q_INVOKABLE void setTrackerPoint(int trackIndex, int clipIndex, const QString &id, int pointIndex,
                                     double x, double y);
    // Live-drag variant; the QML calls EditorState.commitPreviewDrag() on release.
    Q_INVOKABLE void previewSetTrackerPoint(int trackIndex, int clipIndex, const QString &id,
                                            int pointIndex, double x, double y);
    // Planar: redefines the outline at the playhead. That frame becomes the new reference and the
    // existing homographies are dropped.
    Q_INVOKABLE void setTrackerRegion(int trackIndex, int clipIndex, const QString &id,
                                      const QVariantList &region);
    // Planar: one corner dragged at the playhead. Off the reference frame it only moves the outline
    // there, through a homography that keeps the other frames' homographies; at the reference, or
    // with none yet, it is setTrackerRegion.
    Q_INVOKABLE void correctPlanarCorners(int trackIndex, int clipIndex, const QString &id,
                                          const QVariantList &region);
    // key: name | solve | minConfidence | patternSize | searchSize. The sizes are a normalized
    // width; the height follows so the box is square in pixels.
    Q_INVOKABLE void setTrackerProperty(int trackIndex, int clipIndex, const QString &id,
                                        const QString &key, const QVariant &value);
    // removeKeys also strips the keys the tracker wrote onto its targets, as removeTarget does.
    Q_INVOKABLE void removeTracker(int trackIndex, int clipIndex, const QString &id, bool removeKeys);
    // Drops the non-key samples after (forward) or before the playhead, in timeline order.
    Q_INVOKABLE void clearSamples(int trackIndex, int clipIndex, const QString &id, bool forward);

    // Tracks from the playhead to the clip end (forward) or start, or one frame. Stops when every
    // point is lost or below the tracker's minConfidence. Samples land in the project as they
    // arrive; the whole run is one undo step, taken when it finishes or is cancelled.
    Q_INVOKABLE void track(int trackIndex, int clipIndex, const QString &id, bool forward,
                           bool singleFrame);
    Q_INVOKABLE void cancel();

    // [{clipId, name, kind: "mask"|"clip"}]: masks pinned to this clip, then the other picture
    // clips overlapping its span.
    Q_INVOKABLE QVariantList targetCandidates(int trackIndex, int clipIndex) const;
    // mode: "mask" | "followClip". Adding is idempotent (an existing target just changes mode).
    // Applies straight away.
    Q_INVOKABLE void setTarget(int trackIndex, int clipIndex, const QString &id,
                               const QString &targetClipId, const QString &mode);
    Q_INVOKABLE void removeTarget(int trackIndex, int clipIndex, const QString &id,
                                  const QString &targetClipId);
    // Follow targets: true puts the clip's centre on the tracked point, false keeps its offset from
    // it at the reference frame. Re-applies that target.
    Q_INVOKABLE void setTargetCenter(int trackIndex, int clipIndex, const QString &id,
                                     const QString &targetClipId, bool center);
    // Regenerates the keys on every target. One undo step.
    Q_INVOKABLE void apply(int trackIndex, int clipIndex, const QString &id);

    // The same calls addressed by the tracked clip's id, so callers need not keep (track, clip) in
    // step with the timeline. They do nothing (or return empty) when the id names no clip.
    Q_INVOKABLE QVariantList trackers(const QString &clipId) const;
    Q_INVOKABLE QString addPointTracker(const QString &clipId, double x, double y);
    Q_INVOKABLE QString addPlanarTracker(const QString &clipId, const QVariantList &region);
    Q_INVOKABLE void addTrackerPoint(const QString &clipId, const QString &id, double x, double y);
    Q_INVOKABLE void setTrackerProperty(const QString &clipId, const QString &id, const QString &key,
                                        const QVariant &value);
    Q_INVOKABLE void removeTracker(const QString &clipId, const QString &id, bool removeKeys);
    Q_INVOKABLE void clearSamples(const QString &clipId, const QString &id, bool forward);
    Q_INVOKABLE void track(const QString &clipId, const QString &id, bool forward, bool singleFrame);
    Q_INVOKABLE void setTarget(const QString &clipId, const QString &id, const QString &targetClipId,
                               const QString &mode);
    Q_INVOKABLE void removeTarget(const QString &clipId, const QString &id, const QString &targetClipId);
    Q_INVOKABLE void setTargetCenter(const QString &clipId, const QString &id, const QString &targetClipId,
                                     bool center);
    Q_INVOKABLE void apply(const QString &clipId, const QString &id);

    // Points every tracker that drives mask adjustment `fromId` at `toId` instead and regenerates
    // the keys there. Edits the project in place: the caller owns the undo step.
    void retargetMask(const QString &fromId, const QString &toId);

    // Id of the tracker (on any clip) that drives clip `targetClipId`, else empty.
    Q_INVOKABLE QString trackerForTarget(const QString &targetClipId) const;
    // The tracker following this mask adjustment, creating a point tracker on its host (the clip the
    // mask is pinned to) when none does. x/y are the point's place in the host frame; a negative x
    // puts it on the mask's own centre at the playhead. One undo step tr("Track mask"). Returns the
    // tracker id, or empty when the mask has no video host.
    Q_INVOKABLE QString ensureMaskTracker(const QString &maskClipId, double x = -1.0, double y = -1.0);
    // The mask was dragged at the playhead: writes the tracker's key there so the track passes where
    // the mask now is, and regenerates the mask's keys. Single-point trackers only.
    Q_INVOKABLE void correctFromMask(const QString &maskClipId);
    // {clipId, track, clip, name} of the topmost video clip below the follower's track that covers
    // the playhead; empty when there is none.
    Q_INVOKABLE QVariantMap videoUnder(const QString &followerClipId) const;
    // A point tracker on `hostClipId` at (x, y) that makes `followerClipId` follow it. One undo step
    // tr("Follow object"). Returns the tracker id, or empty.
    Q_INVOKABLE QString followWithNewTracker(const QString &hostClipId, const QString &followerClipId, double x,
                                             double y);
    // Draws a mask on the tracked clip from this tracker's geometry at its reference frame (the
    // outline for planar, a rectangle around the point for point trackers), makes it a "mask" target
    // and applies. One undo step tr("Add mask"). Returns the mask clip id, or empty.
    Q_INVOKABLE QString addMaskForTracker(const QString &clipId, const QString &trackerId);
    // {track, clip, name} of the clip with this id; empty when there is none.
    Q_INVOKABLE QVariantMap clipRefById(const QString &clipId) const;
    // [{trackerId, trackerName, hostClipId, hostName, mode}] for every tracker that drives this clip.
    Q_INVOKABLE QVariantList trackersDriving(const QString &clipId) const;

    // {trackerId, trackState: "none"|"partial"|"full", solve} of the tracker driving this mask
    // adjustment (wherever it lives); trackerId is empty when none does. "full" = samples span the
    // host clip, "none" = nothing tracked beyond the reference.
    QVariantMap maskTrackInfo(const QString &maskClipId) const;

    struct FramePoint
    {
        double x = 0;
        double y = 0;
        double confidence = 0;
        bool userKey = false;
        double patternW = 0;
        double patternH = 0;
        double searchW = 0;
        double searchH = 0;
        bool has = false;
        bool ghost = false;
    };
    // trackerFrame as plain data, for the items that draw it without crossing a QVariantMap.
    struct FrameView
    {
        QVector<FramePoint> points;
        QVector<QPointF> region;
        QVector<QVector<QPointF>> trails;
    };
    FrameView frameView(int trackIndex, int clipIndex, const QString &id, double timelineSeconds) const;
    FrameView frameView(const QString &clipId, const QString &id, double timelineSeconds) const;
    QVector<double> confidenceBuckets(int trackIndex, int clipIndex, const QString &id, int buckets) const;
    QVector<double> confidenceBuckets(const QString &clipId, const QString &id, int buckets) const;
    bool locateClip(const QString &clipId, int *trackIndex, int *clipIndex) const;

    struct FrameResult;
    struct Job;

signals:
    void busyChanged();
    void progressChanged();
    // Any tracker data changed; the UI re-queries.
    void trackersChanged();
    // Live while tracking, at most every 100 ms.
    void samplesUpdated(const QString &trackerId);

private:
    drift::MotionTracker buildPointTracker(drift::Clip &host, double x, double y) const;
    // Adds (or re-modes) a target and applies it; the caller owns the undo step. False when the target is unusable.
    bool addTarget(drift::Clip &host, drift::MotionTracker &tracker, const QString &targetClipId,
                   const QString &mode, QString *error);
    // The video clip a mask adjustment is pinned to (else the one under it on its lane's track at the playhead).
    bool maskHostIds(const QString &maskClipId, QString *hostId) const;
    drift::Clip *hostAt(int trackIndex, int clipIndex) const;
    const drift::Clip *hostView(int trackIndex, int clipIndex) const;
    // Runs `edit` on the tracker as one undo step. Refused while a track job runs.
    bool editTracker(int trackIndex, int clipIndex, const QString &id, const QString &undoText,
                     const std::function<bool(drift::Clip &, drift::MotionTracker &)> &edit);
    void mergeBatch(const QString &clipId, const QString &trackerId, const QList<FrameResult> &frames,
                    drift::TimeUs lastTimelineUs);
    void finishJob(const QString &message, const QString &severity);
    // Returns an error to show, or empty. Does not push undo.
    QString applyTargets(const QString &clipId, const QString &trackerId, const QString &onlyTarget = {});
    void unapplyTarget(const drift::Clip &host, const drift::MotionTracker &tracker, const drift::TrackerTarget &target);

    AppController *m_app = nullptr;
    bool m_busy = false;
    double m_progress = 0.0;
    QString m_busyTrackerId;
    TrackerListModel *m_trackerList = nullptr;
    QAtomicInt m_cancel = 0;
    // The run's undo target: only this tracker is restored, so unrelated edits made meanwhile survive.
    QString m_jobClipId;
    QString m_jobTrackerId;
    drift::MotionTracker m_jobTrackerBefore;
    bool m_jobChanged = false;
};
