#pragma once

#include "core/base/Time.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QPointF>
#include <QSizeF>
#include <QString>
#include <QVector>

#include <array>

namespace drift {

// One tracked position. `pos` is normalized to the analysed clip's frame (the same space as mask
// coordinates). `userKey` marks a position the user placed by hand: re-tracking starts from it and
// never overwrites it.
struct TrackSample
{
    QPointF pos;
    float confidence = 1.0f;
    bool userKey = false;
};

struct TrackPoint
{
    // Normalized to the clip frame, like `pos`.
    QSizeF patternSize{0.05, 0.05};
    QSizeF searchSize{0.15, 0.15};
    // Keyed by the clip's SOURCE time, so trims and speed changes do not shift the track.
    QMap<TimeUs, TrackSample> samples;
};

struct TrackerTarget
{
    enum class Mode { Mask, FollowClip };
    QString clipId; // the Mask adjustment clip, or the clip made to follow
    Mode mode = Mode::Mask;
};

struct MotionTracker
{
    enum class Kind { Point, Planar };
    enum class Solve { Translate, TranslateRotate, TranslateRotateScale, Perspective };

    QString id;
    QString name;
    Kind kind = Kind::Point;
    Solve solve = Solve::Translate;
    QList<TrackPoint> points;
    // Planar only: the surface outline at referenceUs, normalized.
    QVector<QPointF> planarRegion;
    // Planar only: row-major 3x3 homography mapping the reference frame to each source time.
    QMap<TimeUs, std::array<double, 9>> homographies;
    TimeUs referenceUs = 0;
    double minConfidence = 0.7;
    QList<TrackerTarget> targets;
};

inline QString motionTrackerKindToString(MotionTracker::Kind kind)
{
    return kind == MotionTracker::Kind::Planar ? QStringLiteral("planar") : QStringLiteral("point");
}

inline MotionTracker::Kind motionTrackerKindFromString(const QString &kind)
{
    return kind == QStringLiteral("planar") ? MotionTracker::Kind::Planar : MotionTracker::Kind::Point;
}

inline QString motionTrackerSolveToString(MotionTracker::Solve solve)
{
    switch (solve) {
    case MotionTracker::Solve::TranslateRotate:
        return QStringLiteral("translateRotate");
    case MotionTracker::Solve::TranslateRotateScale:
        return QStringLiteral("translateRotateScale");
    case MotionTracker::Solve::Perspective:
        return QStringLiteral("perspective");
    case MotionTracker::Solve::Translate:
        break;
    }
    return QStringLiteral("translate");
}

inline MotionTracker::Solve motionTrackerSolveFromString(const QString &solve)
{
    if (solve == QStringLiteral("translateRotate"))
        return MotionTracker::Solve::TranslateRotate;
    if (solve == QStringLiteral("translateRotateScale"))
        return MotionTracker::Solve::TranslateRotateScale;
    if (solve == QStringLiteral("perspective"))
        return MotionTracker::Solve::Perspective;
    return MotionTracker::Solve::Translate;
}

inline QString trackerTargetModeToString(TrackerTarget::Mode mode)
{
    return mode == TrackerTarget::Mode::FollowClip ? QStringLiteral("followClip") : QStringLiteral("mask");
}

inline TrackerTarget::Mode trackerTargetModeFromString(const QString &mode)
{
    return mode == QStringLiteral("followClip") ? TrackerTarget::Mode::FollowClip : TrackerTarget::Mode::Mask;
}

inline QJsonObject toJson(const MotionTracker &tracker)
{
    QJsonArray points;
    for (const TrackPoint &point : tracker.points) {
        QJsonArray samples;
        for (auto it = point.samples.constBegin(); it != point.samples.constEnd(); ++it) {
            const TrackSample &sample = it.value();
            samples.append(QJsonArray{static_cast<double>(it.key()), sample.pos.x(), sample.pos.y(),
                                      static_cast<double>(sample.confidence), sample.userKey ? 1 : 0});
        }
        points.append(QJsonObject{
            {QStringLiteral("pattern"), QJsonArray{point.patternSize.width(), point.patternSize.height()}},
            {QStringLiteral("search"), QJsonArray{point.searchSize.width(), point.searchSize.height()}},
            {QStringLiteral("samples"), samples},
        });
    }

    QJsonArray region;
    for (const QPointF &pt : tracker.planarRegion)
        region.append(QJsonArray{pt.x(), pt.y()});

    QJsonArray homographies;
    for (auto it = tracker.homographies.constBegin(); it != tracker.homographies.constEnd(); ++it) {
        QJsonArray entry{static_cast<double>(it.key())};
        for (double value : it.value())
            entry.append(value);
        homographies.append(entry);
    }

    QJsonArray targets;
    for (const TrackerTarget &target : tracker.targets) {
        targets.append(QJsonObject{
            {QStringLiteral("clipId"), target.clipId},
            {QStringLiteral("mode"), trackerTargetModeToString(target.mode)},
        });
    }

    return QJsonObject{
        {QStringLiteral("id"), tracker.id},
        {QStringLiteral("name"), tracker.name},
        {QStringLiteral("kind"), motionTrackerKindToString(tracker.kind)},
        {QStringLiteral("solve"), motionTrackerSolveToString(tracker.solve)},
        {QStringLiteral("referenceUs"), static_cast<double>(tracker.referenceUs)},
        {QStringLiteral("minConfidence"), tracker.minConfidence},
        {QStringLiteral("points"), points},
        {QStringLiteral("region"), region},
        {QStringLiteral("homographies"), homographies},
        {QStringLiteral("targets"), targets},
    };
}

inline MotionTracker motionTrackerFromJson(const QJsonObject &object)
{
    MotionTracker tracker;
    tracker.id = object.value(QStringLiteral("id")).toString();
    tracker.name = object.value(QStringLiteral("name")).toString();
    tracker.kind = motionTrackerKindFromString(object.value(QStringLiteral("kind")).toString());
    tracker.solve = motionTrackerSolveFromString(object.value(QStringLiteral("solve")).toString());
    tracker.referenceUs = static_cast<TimeUs>(object.value(QStringLiteral("referenceUs")).toDouble());
    tracker.minConfidence = object.value(QStringLiteral("minConfidence")).toDouble(tracker.minConfidence);

    for (const QJsonValue &pointValue : object.value(QStringLiteral("points")).toArray()) {
        const QJsonObject pointObject = pointValue.toObject();
        TrackPoint point;
        const QJsonArray pattern = pointObject.value(QStringLiteral("pattern")).toArray();
        if (pattern.size() >= 2)
            point.patternSize = QSizeF(pattern.at(0).toDouble(), pattern.at(1).toDouble());
        const QJsonArray search = pointObject.value(QStringLiteral("search")).toArray();
        if (search.size() >= 2)
            point.searchSize = QSizeF(search.at(0).toDouble(), search.at(1).toDouble());
        for (const QJsonValue &sampleValue : pointObject.value(QStringLiteral("samples")).toArray()) {
            const QJsonArray fields = sampleValue.toArray();
            if (fields.size() < 5)
                continue;
            TrackSample sample;
            sample.pos = QPointF(fields.at(1).toDouble(), fields.at(2).toDouble());
            sample.confidence = static_cast<float>(fields.at(3).toDouble());
            sample.userKey = fields.at(4).toInt() != 0;
            point.samples.insert(static_cast<TimeUs>(fields.at(0).toDouble()), sample);
        }
        tracker.points.append(point);
    }

    for (const QJsonValue &pairValue : object.value(QStringLiteral("region")).toArray()) {
        const QJsonArray pair = pairValue.toArray();
        if (pair.size() >= 2)
            tracker.planarRegion.append(QPointF(pair.at(0).toDouble(), pair.at(1).toDouble()));
    }

    for (const QJsonValue &entryValue : object.value(QStringLiteral("homographies")).toArray()) {
        const QJsonArray entry = entryValue.toArray();
        if (entry.size() < 10)
            continue;
        std::array<double, 9> matrix{};
        for (int i = 0; i < 9; ++i)
            matrix[i] = entry.at(i + 1).toDouble();
        tracker.homographies.insert(static_cast<TimeUs>(entry.at(0).toDouble()), matrix);
    }

    for (const QJsonValue &targetValue : object.value(QStringLiteral("targets")).toArray()) {
        const QJsonObject targetObject = targetValue.toObject();
        TrackerTarget target;
        target.clipId = targetObject.value(QStringLiteral("clipId")).toString();
        target.mode = trackerTargetModeFromString(targetObject.value(QStringLiteral("mode")).toString());
        tracker.targets.append(target);
    }

    return tracker;
}

} // namespace drift
