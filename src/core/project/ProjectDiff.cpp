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
    const Clip *clip = nullptr;
};

QHash<QString, Located> clipsById(const Project &project)
{
    QHash<QString, Located> out;
    for (int t = 0; t < project.tracks().size(); ++t)
        for (const Clip &clip : project.tracks().at(t).clips)
            out.insert(clip.id, {t, &clip});
    return out;
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
    return fields;
}

} // namespace

ProjectDiff diffProjects(const Project &from, const Project &to)
{
    ProjectDiff diff;
    const QHash<QString, Located> before = clipsById(from);
    const QHash<QString, Located> after = clipsById(to);

    // In timeline order: by track, then by start.
    for (int t = 0; t < to.tracks().size(); ++t) {
        QList<const Clip *> clips;
        for (const Clip &clip : to.tracks().at(t).clips)
            clips.append(&clip);
        std::stable_sort(clips.begin(), clips.end(),
                         [](const Clip *a, const Clip *b) { return a->timelineStart < b->timelineStart; });
        for (const Clip *clip : std::as_const(clips)) {
            const auto was = before.constFind(clip->id);
            if (was == before.constEnd()) {
                ClipDiff added{clip->id, QStringLiteral("added"), t, -1, {}, clip->name, {}};
                // A split: a clip that stayed (and got shorter) on this track, on the same media,
                // now ends where this one starts.
                for (const Clip &other : to.tracks().at(t).clips) {
                    const auto otherWas = before.constFind(other.id);
                    if (other.id == clip->id || otherWas == before.constEnd() || other.assetId != clip->assetId
                        || other.timelineEnd() != clip->timelineStart
                        || otherWas->clip->timelineDuration <= other.timelineDuration)
                        continue;
                    added.splitFrom = other.id;
                    break;
                }
                diff.clips.append(added);
                continue;
            }
            const QStringList fields = changedFields(*was->clip, *clip);
            if (fields.isEmpty() && was->track == t)
                continue;
            ClipDiff changed{clip->id, QStringLiteral("changed"), t, -1, fields, clip->name, {}};
            if (was->track != t) {
                changed.fromTrack = was->track;
                changed.fields.prepend(QStringLiteral("track"));
            }
            diff.clips.append(changed);
        }
    }
    for (int t = 0; t < from.tracks().size(); ++t)
        for (const Clip &clip : from.tracks().at(t).clips)
            if (!after.contains(clip.id))
                diff.clips.append({clip.id, QStringLiteral("removed"), t, -1, {}, clip.name, {}});

    QHash<QString, int> trackBefore;
    for (int t = 0; t < from.tracks().size(); ++t)
        trackBefore.insert(from.tracks().at(t).id, t);
    QSet<QString> trackAfter;
    for (int t = 0; t < to.tracks().size(); ++t) {
        const Track &track = to.tracks().at(t);
        trackAfter.insert(track.id);
        const auto was = trackBefore.constFind(track.id);
        if (track.id.isEmpty() || was == trackBefore.constEnd()) {
            diff.tracks.append({track.id, QStringLiteral("added"), t, {}});
            continue;
        }
        const QStringList fields = changedTrackFields(from.tracks().at(*was), track, *was, t);
        if (!fields.isEmpty())
            diff.tracks.append({track.id, QStringLiteral("changed"), t, fields});
    }
    for (int t = 0; t < from.tracks().size(); ++t) {
        const Track &track = from.tracks().at(t);
        if (!track.id.isEmpty() && !trackAfter.contains(track.id))
            diff.tracks.append({track.id, QStringLiteral("removed"), t, {}});
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
