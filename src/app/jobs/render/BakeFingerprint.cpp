#include "app/jobs/render/BakeFingerprint.h"

#include "core/project/Project.h"
#include "core/project/Transition.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <cmath>
#include <vector>

namespace drift {

namespace {

struct ClipEntry
{
    double start = 0;
    double end = 0;
    QString id;
    QString sequenceId;
    QByteArray bytes;
};

struct TransitionEntry
{
    QString from;
    QString to;
    QByteArray bytes;
    double durationUs = 0;
};

struct TrackEntry
{
    QByteArray head;
    QString id;
    QString parentId;
    QString spanEndId;
    std::vector<ClipEntry> clips;
    std::vector<TransitionEntry> transitions;
};

using Timeline = std::vector<TrackEntry>;

QByteArray compact(const QJsonObject &object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

QByteArray sourceStamp(const QString &path, QHash<QString, QByteArray> &cache)
{
    if (path.isEmpty())
        return {};
    const auto it = cache.constFind(path);
    if (it != cache.constEnd())
        return it.value();
    const QFileInfo info(path);
    const QByteArray stamp = QByteArray::number(info.size()) + '@'
        + QByteArray::number(info.lastModified().toMSecsSinceEpoch());
    cache.insert(path, stamp);
    return stamp;
}

Timeline prepare(const QJsonArray &tracks, QHash<QString, QByteArray> &stamps)
{
    // Left out of a track's head: labels, view state, and what only the mix or the export reads.
    // excludeFromExport only changes what export renders, and export already skips bakes over such
    // a track (BakeController::exportPool's skip spans). muted and solo are NOT left out on Audio
    // and Video tracks: the Timeline audio mix that audio-reactive procedural clips and effects read
    // (AudioFrameAnalysis, forEachSourceClip) follows both, so they can change the picture.
    // Dropping them only when no such consumer is in range would need package and effect lookups
    // here, so they stay hashed there.
    static const QStringList trackUiKeys{
        QStringLiteral("name"),         QStringLiteral("number"),
        QStringLiteral("locked"),       QStringLiteral("volume"),
        QStringLiteral("pan"),          QStringLiteral("clipDisplay"),
        QStringLiteral("showChannelWaveforms"),
        QStringLiteral("heightScale"),  QStringLiteral("lanesCollapsed"),
        QStringLiteral("muted"),        QStringLiteral("solo"),
        QStringLiteral("excludeFromExport"),
        QStringLiteral("clips"),        QStringLiteral("transitions"),
    };
    static const QStringList clipUiKeys{
        QStringLiteral("name"),          QStringLiteral("thumbnailPath"),
        QStringLiteral("filmstripPath"), QStringLiteral("volume"),
        QStringLiteral("pan"),           QStringLiteral("audioEffects"),
        QStringLiteral("audioFadeInUs"), QStringLiteral("audioFadeOutUs"),
        QStringLiteral("audioStreamIndex"), QStringLiteral("suppressEmbeddedAudio"),
        QStringLiteral("linkId"),        QStringLiteral("linkLoose"),
    };

    Timeline out;
    for (const QJsonValue &trackValue : tracks) {
        const QJsonObject track = trackValue.toObject();
        TrackEntry entry;
        QJsonObject head = track;
        for (const QString &key : trackUiKeys)
            head.remove(key);
        const QString type = track.value(QStringLiteral("type")).toString();
        if (type == QLatin1String("audio") || type == QLatin1String("video")) {
            head.insert(QStringLiteral("muted"), track.value(QStringLiteral("muted")));
            head.insert(QStringLiteral("solo"), track.value(QStringLiteral("solo")));
        }
        entry.head = compact(head);
        entry.id = track.value(QStringLiteral("id")).toString();
        entry.parentId = track.value(QStringLiteral("parentTrackId")).toString();
        entry.spanEndId = track.value(QStringLiteral("spanEndTrackId")).toString();

        for (const QJsonValue &clipValue : track.value(QStringLiteral("clips")).toArray()) {
            QJsonObject clip = clipValue.toObject();
            ClipEntry c;
            c.id = clip.value(QStringLiteral("id")).toString();
            c.sequenceId = clip.value(QStringLiteral("sequenceId")).toString();
            c.start = clip.value(QStringLiteral("timelineStartUs")).toDouble();
            c.end = c.start + clip.value(QStringLiteral("timelineDurationUs")).toDouble();
            const QString path = clip.value(QStringLiteral("path")).toString();
            for (const QString &key : clipUiKeys)
                clip.remove(key);
            c.bytes = compact(clip) + sourceStamp(path, stamps);
            entry.clips.push_back(std::move(c));
        }
        for (const QJsonValue &transitionValue : track.value(QStringLiteral("transitions")).toArray()) {
            const QJsonObject transition = transitionValue.toObject();
            entry.transitions.push_back({transition.value(QStringLiteral("fromClipId")).toString(),
                                         transition.value(QStringLiteral("toClipId")).toString(),
                                         compact(transition),
                                         transition.value(QStringLiteral("durationUs"))
                                             .toDouble(double(Transition{}.durationUs))});
        }
        out.push_back(std::move(entry));
    }
    return out;
}

struct Prepared
{
    QByteArray header;
    Timeline root;
    QHash<QString, Timeline> sequences;
};

Prepared prepare(const Project &project)
{
    const QJsonObject json = project.toJson(false);
    QHash<QString, QByteArray> stamps;
    Prepared p;
    p.header = QByteArray::number(kBakeSchemaVersion) + '|' + QByteArray::number(project.width()) + 'x'
        + QByteArray::number(project.height()) + '@' + QByteArray::number(project.fps()) + '|'
        + compact(json.value(QStringLiteral("background")).toObject());
    p.root = prepare(json.value(QStringLiteral("tracks")).toArray(), stamps);
    for (const QJsonValue &sequence : json.value(QStringLiteral("sequences")).toArray()) {
        const QJsonObject object = sequence.toObject();
        p.sequences.insert(object.value(QStringLiteral("id")).toString(),
                           prepare(object.value(QStringLiteral("tracks")).toArray(), stamps));
    }
    return p;
}

// The clip with this id on the track, or null.
const ClipEntry *clipIn(const TrackEntry &track, const QString &id)
{
    for (const ClipEntry &clip : track.clips) {
        if (clip.id == id)
            return &clip;
    }
    return nullptr;
}

void hashTimeline(QCryptographicHash &hash, const Prepared &p, const Timeline &timeline, double startUs,
                  double endUs, QSet<QString> &visiting)
{
    const bool all = std::isinf(endUs);
    const auto inRange = [&](const TrackEntry &track) {
        if (all)
            return true;
        for (const ClipEntry &clip : track.clips) {
            if (!(clip.start >= endUs || clip.end <= startUs))
                return true;
        }
        return false;
    };
    // A transform layer covers the tracks from below it down to its span-end track, found by
    // position. A skipped track can still be that end (or the parent of a lane that is), so its id
    // stays in the sequence: moving it past a track in the range changes what the layer covers.
    QSet<QString> anchors;
    if (!all) {
        for (const TrackEntry &track : timeline) {
            if (!track.spanEndId.isEmpty() && inRange(track))
                anchors.insert(track.spanEndId);
        }
        for (const TrackEntry &track : timeline) {
            if (anchors.contains(track.id) && !track.parentId.isEmpty())
                anchors.insert(track.parentId);
        }
    }
    for (const TrackEntry &track : timeline) {
        // A track with nothing in the range draws nothing there, whatever its own properties: its
        // head (hidden, its kind, its span) is hashed only with a clip it applies to.
        if (!inRange(track)) {
            if (anchors.contains(track.id)) {
                hash.addData(track.id.toUtf8());
                hash.addData("#", 1);
            }
            continue;
        }
        hash.addData(track.head);
        QSet<QString> included;
        const auto include = [&](const ClipEntry &clip) {
            included.insert(clip.id);
            hash.addData(clip.bytes);
            if (!clip.sequenceId.isEmpty() && !visiting.contains(clip.sequenceId)) {
                const auto it = p.sequences.constFind(clip.sequenceId);
                if (it != p.sequences.constEnd()) {
                    visiting.insert(clip.sequenceId);
                    hashTimeline(hash, p, it.value(), 0, INFINITY, visiting);
                    visiting.remove(clip.sequenceId);
                }
            }
        };
        for (const ClipEntry &clip : track.clips) {
            if (!all && (clip.start >= endUs || clip.end <= startUs))
                continue;
            include(clip);
        }
        // A transition draws both its clips across its window, which reaches past the cut into the
        // other clip's span. So when the window overlaps the range but only one side does, the
        // other side's clip decides frames in range too.
        std::vector<const ClipEntry *> partners;
        for (const TransitionEntry &transition : track.transitions) {
            const bool hasFrom = included.contains(transition.from);
            const bool hasTo = included.contains(transition.to);
            if (!hasFrom && !hasTo)
                continue;
            hash.addData(transition.bytes);
            if (hasFrom && hasTo)
                continue;
            const ClipEntry *from = clipIn(track, transition.from);
            const ClipEntry *to = clipIn(track, transition.to);
            TimeUs windowStart = 0;
            TimeUs windowEnd = 0;
            if (!from || !to
                || !transitionWindowOf(TimeUs(from->start), TimeUs(from->end), TimeUs(to->start), TimeUs(to->end),
                                       TimeUs(transition.durationUs), windowStart, windowEnd)
                || double(windowStart) >= endUs || double(windowEnd) <= startUs)
                continue;
            partners.push_back(hasFrom ? to : from);
        }
        for (const ClipEntry *partner : partners) {
            if (!included.contains(partner->id))
                include(*partner);
        }
        hash.addData("|", 1);
    }
}

QByteArray fingerprintOf(const Prepared &p, int fps, qint64 startFrame, qint64 endFrame)
{
    const double perFrame = 1e6 / qMax(1, fps);
    const double startUs = std::llround(double(startFrame) * perFrame);
    const double endUs = std::llround(double(endFrame) * perFrame);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(p.header);
    QSet<QString> visiting;
    hashTimeline(hash, p, p.root, startUs, endUs, visiting);
    return hash.result();
}

} // namespace

QByteArray bakeFingerprint(const Project &project, qint64 startFrame, qint64 endFrame)
{
    return fingerprintOf(prepare(project), project.fps(), startFrame, endFrame);
}

QList<QByteArray> bakeFingerprints(const Project &project, const QList<QPair<qint64, qint64>> &ranges)
{
    const Prepared p = prepare(project);
    QList<QByteArray> out;
    out.reserve(ranges.size());
    for (const auto &range : ranges)
        out.append(fingerprintOf(p, project.fps(), range.first, range.second));
    return out;
}

} // namespace drift
