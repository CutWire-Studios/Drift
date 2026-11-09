#include "core/project/ProjectDiff.h"

#include <QHash>
#include <QJsonArray>
#include <QSet>

namespace drift {

namespace {

// The project file's clip keys, grouped under the names an editor thinks in. Keys missing here
// are reported as they are; caches that follow the media (thumbnails) are not reported at all.
QString fieldName(const QString &key)
{
    static const QHash<QString, QString> names{
        {QStringLiteral("timelineStartUs"), QStringLiteral("start")},
        {QStringLiteral("timelineDurationUs"), QStringLiteral("duration")},
        {QStringLiteral("srcInUs"), QStringLiteral("in")},
        {QStringLiteral("srcOutUs"), QStringLiteral("out")},
        {QStringLiteral("sourceFrame"), QStringLiteral("crop")},
        {QStringLiteral("sourceFrameAngle"), QStringLiteral("crop")},
        {QStringLiteral("effects"), QStringLiteral("effects")},
        {QStringLiteral("audioEffects"), QStringLiteral("effects")},
        {QStringLiteral("x"), QStringLiteral("transform")},
        {QStringLiteral("y"), QStringLiteral("transform")},
        {QStringLiteral("width"), QStringLiteral("transform")},
        {QStringLiteral("height"), QStringLiteral("transform")},
        {QStringLiteral("rotation"), QStringLiteral("transform")},
        {QStringLiteral("flipH"), QStringLiteral("transform")},
        {QStringLiteral("flipV"), QStringLiteral("transform")},
        {QStringLiteral("rotationX"), QStringLiteral("transform")},
        {QStringLiteral("rotationY"), QStringLiteral("transform")},
        {QStringLiteral("z"), QStringLiteral("transform")},
        {QStringLiteral("anchorX"), QStringLiteral("transform")},
        {QStringLiteral("anchorY"), QStringLiteral("transform")},
        {QStringLiteral("anchorZ"), QStringLiteral("transform")},
        {QStringLiteral("perspective"), QStringLiteral("transform")},
        {QStringLiteral("assetId"), QStringLiteral("media")},
        {QStringLiteral("path"), QStringLiteral("media")},
        {QStringLiteral("textContent"), QStringLiteral("text")},
        {QStringLiteral("textStyle"), QStringLiteral("text")},
        {QStringLiteral("speedCurve"), QStringLiteral("speed")},
        {QStringLiteral("reverse"), QStringLiteral("speed")},
        {QStringLiteral("fadeInUs"), QStringLiteral("fade")},
        {QStringLiteral("fadeOutUs"), QStringLiteral("fade")},
        {QStringLiteral("fadeCurve"), QStringLiteral("fade")},
        {QStringLiteral("fadeShape"), QStringLiteral("fade")},
        {QStringLiteral("audioFadeInUs"), QStringLiteral("fade")},
        {QStringLiteral("audioFadeOutUs"), QStringLiteral("fade")},
        {QStringLiteral("linkId"), QStringLiteral("link")},
        {QStringLiteral("linkLoose"), QStringLiteral("link")},
        {QStringLiteral("thumbnailPath"), QString()},
        {QStringLiteral("filmstripPath"), QString()},
    };
    const auto it = names.constFind(key);
    return it == names.constEnd() ? key : it.value();
}

struct Located
{
    int track = -1;
    int index = -1;
    const QList<Clip> *list = nullptr;
    const Clip *clip = nullptr;
};

// Each clip's key: its id, or for a duplicated id (an imported file can carry one) the id and its
// occurrence across the whole timeline, in track order.
QList<QStringList> clipKeys(const QList<Track> &tracks)
{
    QList<QStringList> keys;
    QHash<QString, int> seen;
    for (const Track &track : tracks) {
        QStringList row;
        for (const Clip &clip : track.clips) {
            const int n = seen[clip.id]++;
            row.append(n == 0 ? clip.id : clip.id + QLatin1Char('#') + QString::number(n));
        }
        keys.append(row);
    }
    return keys;
}

QHash<QString, Located> clipsById(const QList<Track> &tracks, const QList<QStringList> &keys)
{
    QHash<QString, Located> out;
    for (int t = 0; t < tracks.size(); ++t) {
        const QList<Clip> &clips = tracks.at(t).clips;
        for (int c = 0; c < clips.size(); ++c)
            out.insert(keys.at(t).at(c), {t, c, &clips, &clips.at(c)});
    }
    return out;
}

QString trackKey(const Track &track, int index)
{
    return track.id.isEmpty() ? QStringLiteral("#%1").arg(index) : track.id;
}

QStringList changedFields(const Clip &a, const Clip &b)
{
    const QJsonObject ja = clipJson(a);
    const QJsonObject jb = clipJson(b);
    QStringList fields;
    QSet<QString> keys;
    for (auto it = ja.constBegin(); it != ja.constEnd(); ++it)
        keys.insert(it.key());
    for (auto it = jb.constBegin(); it != jb.constEnd(); ++it)
        keys.insert(it.key());
    for (const QString &key : std::as_const(keys)) {
        if (ja.value(key) == jb.value(key))
            continue;
        const QString name = fieldName(key);
        if (!name.isEmpty() && !fields.contains(name))
            fields.append(name);
    }
    std::sort(fields.begin(), fields.end());
    return fields;
}

bool sameTransitions(const Track &a, const Track &b)
{
    if (a.transitions.size() != b.transitions.size())
        return false;
    for (int i = 0; i < a.transitions.size(); ++i)
        if (transitionJson(a.transitions.at(i)) != transitionJson(b.transitions.at(i)))
            return false;
    return true;
}

QStringList changedTrackFields(const Track &a, const Track &b, int fromIndex, int toIndex)
{
    QStringList fields;
    if (fromIndex != toIndex)
        fields.append(QStringLiteral("index"));
    if (a.name != b.name)
        fields.append(QStringLiteral("name"));
    if (a.locked != b.locked)
        fields.append(QStringLiteral("locked"));
    if (a.muted != b.muted)
        fields.append(QStringLiteral("muted"));
    if (a.hidden != b.hidden)
        fields.append(QStringLiteral("hidden"));
    if (a.solo != b.solo)
        fields.append(QStringLiteral("solo"));
    if (!qFuzzyCompare(a.volume, b.volume) || !qFuzzyCompare(1.0 + a.pan, 1.0 + b.pan))
        fields.append(QStringLiteral("audio"));
    if (!sameTransitions(a, b))
        fields.append(QStringLiteral("transitions"));
    if (a.adjustmentScope != b.adjustmentScope || a.parentTrackId != b.parentTrackId || a.spanEndTrackId != b.spanEndTrackId)
        fields.append(QStringLiteral("scope"));
    // How the track draws: saved, but not part of the cut.
    if (!qFuzzyCompare(a.heightScale, b.heightScale) || a.clipDisplay != b.clipDisplay
        || a.showChannelWaveforms != b.showChannelWaveforms)
        fields.append(QStringLiteral("view"));
    return fields;
}

void diffTimeline(const QString &sequence, const QList<Track> &from, const QList<Track> &to, ProjectDiff &diff)
{
    const QList<QStringList> fromKeys = clipKeys(from);
    const QList<QStringList> toKeys = clipKeys(to);
    const QHash<QString, Located> before = clipsById(from, fromKeys);
    const QHash<QString, Located> after = clipsById(to, toKeys);

    // Split heads: clips that stayed and got shorter from the end, by (track, media, new end).
    QHash<QString, QString> splitHeads;
    const auto headKey = [](int track, const QString &asset, TimeUs end) {
        return QStringLiteral("%1|%2|%3").arg(track).arg(asset).arg(end);
    };
    for (auto it = after.constBegin(); it != after.constEnd(); ++it) {
        const auto was = before.constFind(it.key());
        const Clip &now = *it->clip;
        if (was == before.constEnd() || was->track != it->track
            || was->clip->timelineStart != now.timelineStart || was->clip->timelineEnd() <= now.timelineEnd())
            continue;
        splitHeads.insert(headKey(it->track, now.assetId, now.timelineEnd()), it.key());
    }

    // In timeline order: by track, then by start.
    for (int t = 0; t < to.size(); ++t) {
        QList<QPair<QString, const Clip *>> clips;
        for (int c = 0; c < to.at(t).clips.size(); ++c)
            clips.append({toKeys.at(t).at(c), &to.at(t).clips.at(c)});
        std::stable_sort(clips.begin(), clips.end(), [](const auto &a, const auto &b) {
            return a.second->timelineStart < b.second->timelineStart;
        });
        for (const auto &[key, clip] : std::as_const(clips)) {
            const auto was = before.constFind(key);
            if (was == before.constEnd()) {
                ClipDiff added{clip->id, sequence, QStringLiteral("added"), t, -1, {}, clip->name, {}};
                // A split: the head kept its start, ends where this one starts, and this one carries
                // on its media from there to where the head used to end.
                const QString head = splitHeads.value(headKey(t, clip->assetId, clip->timelineStart));
                if (!head.isEmpty()) {
                    const Located &headWas = before.value(head);
                    const Clip &headNow = *after.value(head).clip;
                    if (clip->type == headNow.type && clip->timelineEnd() == headWas.clip->timelineEnd()
                        && clip->srcIn == headNow.srcOut && clip->srcOut == headWas.clip->srcOut)
                        added.splitFrom = headNow.id;
                }
                diff.clips.append(added);
                continue;
            }
            const QStringList fields = changedFields(*was->clip, *clip);
            if (fields.isEmpty() && was->track == t)
                continue;
            ClipDiff changed{clip->id, sequence, QStringLiteral("changed"), t, -1, fields, clip->name, {}};
            if (was->track != t) {
                changed.fromTrack = was->track;
                changed.fields.prepend(QStringLiteral("track"));
            }
            diff.clips.append(changed);
        }
    }
    for (int t = 0; t < from.size(); ++t)
        for (int c = 0; c < from.at(t).clips.size(); ++c)
            if (!after.contains(fromKeys.at(t).at(c))) {
                const Clip &clip = from.at(t).clips.at(c);
                diff.clips.append({clip.id, sequence, QStringLiteral("removed"), t, -1, {}, clip.name, {}});
            }

    QHash<QString, int> trackBefore;
    for (int t = 0; t < from.size(); ++t)
        trackBefore.insert(trackKey(from.at(t), t), t);
    QSet<QString> trackAfter;
    for (int t = 0; t < to.size(); ++t) {
        const Track &track = to.at(t);
        const QString key = trackKey(track, t);
        trackAfter.insert(key);
        const auto was = trackBefore.constFind(key);
        if (was == trackBefore.constEnd()) {
            diff.tracks.append({track.id, sequence, QStringLiteral("added"), t, {}});
            continue;
        }
        const QStringList fields = changedTrackFields(from.at(*was), track, *was, t);
        if (!fields.isEmpty())
            diff.tracks.append({track.id, sequence, QStringLiteral("changed"), t, fields});
    }
    for (int t = 0; t < from.size(); ++t)
        if (!trackAfter.contains(trackKey(from.at(t), t)))
            diff.tracks.append({from.at(t).id, sequence, QStringLiteral("removed"), t, {}});
}

} // namespace

ProjectDiff diffProjects(const Project &from, const Project &to)
{
    ProjectDiff diff;
    QStringList sequences{QString()};
    QSet<QString> seen;
    for (const QString &id : to.sequenceIds() + from.sequenceIds())
        if (!seen.contains(id)) {
            seen.insert(id);
            sequences.append(id);
        }
    bool sequencesChanged = false;
    for (const QString &id : std::as_const(sequences)) {
        if (!id.isEmpty() && (!from.hasSequence(id) || !to.hasSequence(id))) {
            sequencesChanged = true;
            continue;
        }
        diffTimeline(id, from.sequenceTracks(id), to.sequenceTracks(id), diff);
    }

    for (auto it = to.assets().constBegin(); it != to.assets().constEnd(); ++it) {
        const auto was = from.assets().constFind(it.key());
        if (was == from.assets().constEnd())
            diff.assets.append({it.key(), QStringLiteral("added"), it->name});
        else if (was->name != it->name || was->path != it->path)
            diff.assets.append({it.key(), QStringLiteral("changed"), it->name});
    }
    for (auto it = from.assets().constBegin(); it != from.assets().constEnd(); ++it)
        if (!to.assets().contains(it.key()))
            diff.assets.append({it.key(), QStringLiteral("removed"), it->name});

    if (from.fps() != to.fps())
        diff.settings.append(QStringLiteral("fps"));
    if (from.width() != to.width() || from.height() != to.height())
        diff.settings.append(QStringLiteral("resolution"));
    if (from.sampleRate() != to.sampleRate())
        diff.settings.append(QStringLiteral("sample_rate"));
    const Background &bgA = from.background();
    const Background &bgB = to.background();
    if (bgA.kind != bgB.kind || bgA.color != bgB.color || !qFuzzyCompare(bgA.blurStrength, bgB.blurStrength))
        diff.settings.append(QStringLiteral("background"));
    bool marks = from.bookmarks().size() != to.bookmarks().size();
    for (int i = 0; !marks && i < to.bookmarks().size(); ++i)
        marks = from.bookmarks().at(i).timeUs != to.bookmarks().at(i).timeUs
                || from.bookmarks().at(i).label != to.bookmarks().at(i).label;
    if (marks)
        diff.settings.append(QStringLiteral("bookmarks"));
    if (from.workAreaInUs() != to.workAreaInUs() || from.workAreaOutUs() != to.workAreaOutUs())
        diff.settings.append(QStringLiteral("work_area"));
    if (from.name() != to.name() || from.author() != to.author() || from.description() != to.description())
        diff.settings.append(QStringLiteral("project_info"));
    if (sequencesChanged)
        diff.settings.append(QStringLiteral("sequences"));
    return diff;
}

QJsonObject projectDiffToJson(const ProjectDiff &diff)
{
    QJsonObject out;
    if (!diff.clips.isEmpty()) {
        QJsonArray clips;
        for (const ClipDiff &clip : diff.clips) {
            QJsonObject row{{QStringLiteral("id"), clip.id},
                            {QStringLiteral("change"), clip.change},
                            {QStringLiteral("track"), clip.track}};
            if (!clip.sequence.isEmpty())
                row.insert(QStringLiteral("sequence"), clip.sequence);
            if (!clip.name.isEmpty())
                row.insert(QStringLiteral("name"), clip.name);
            if (!clip.fields.isEmpty())
                row.insert(QStringLiteral("fields"), QJsonArray::fromStringList(clip.fields));
            if (clip.fromTrack >= 0)
                row.insert(QStringLiteral("from_track"), clip.fromTrack);
            if (!clip.splitFrom.isEmpty())
                row.insert(QStringLiteral("split_from"), clip.splitFrom);
            clips.append(row);
        }
        out.insert(QStringLiteral("clips"), clips);
    }
    if (!diff.tracks.isEmpty()) {
        QJsonArray tracks;
        for (const TrackDiff &track : diff.tracks) {
            QJsonObject row{{QStringLiteral("id"), track.id},
                            {QStringLiteral("change"), track.change},
                            {QStringLiteral("index"), track.index}};
            if (!track.sequence.isEmpty())
                row.insert(QStringLiteral("sequence"), track.sequence);
            if (!track.fields.isEmpty())
                row.insert(QStringLiteral("fields"), QJsonArray::fromStringList(track.fields));
            tracks.append(row);
        }
        out.insert(QStringLiteral("tracks"), tracks);
    }
    if (!diff.assets.isEmpty()) {
        QJsonArray assets;
        for (const AssetDiff &asset : diff.assets)
            assets.append(QJsonObject{{QStringLiteral("id"), asset.id},
                                      {QStringLiteral("change"), asset.change},
                                      {QStringLiteral("name"), asset.name}});
        out.insert(QStringLiteral("assets"), assets);
    }
    if (!diff.settings.isEmpty())
        out.insert(QStringLiteral("settings"), QJsonArray::fromStringList(diff.settings));
    return out;
}

} // namespace drift
