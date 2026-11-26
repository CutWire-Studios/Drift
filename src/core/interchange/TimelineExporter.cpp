#include "core/interchange/TimelineExporter.h"

#include "core/base/Time.h"
#include "core/project/Project.h"
#include "core/project/Track.h"
#include "core/project/clip/Clip.h"
#include "core/project/media/MediaAsset.h"

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>
#include <QXmlStreamWriter>
#include <QtMath>

#include <algorithm>

namespace drift::interchange {
namespace {

double projectFpsOrDefault(const Project &project)
{
    return project.fps() > 0 ? static_cast<double>(project.fps()) : 30.0;
}

QString fcpxmlTimeString(TimeUs us, double fps)
{
    if (us <= 0)
        return QStringLiteral("0/1s");
    const qint64 denom = qMax<qint64>(1, llround(fps * 100.0));
    const qint64 num = llround((static_cast<double>(us) / static_cast<double>(kUsPerSecond)) * denom);
    return QStringLiteral("%1/%2s").arg(num).arg(denom);
}

QString edlTimecode(TimeUs us, double fps)
{
    const double effFps = fps > 0.0 ? fps : 30.0;
    const qint64 totalFrames = qMax<qint64>(
        0, llround((static_cast<double>(us) / static_cast<double>(kUsPerSecond)) * effFps));
    const int fpsInt = qMax(1, qRound(effFps));
    const qint64 f = totalFrames % fpsInt;
    const qint64 totalSec = totalFrames / fpsInt;
    const qint64 s = totalSec % 60;
    const qint64 totalMin = totalSec / 60;
    const qint64 m = totalMin % 60;
    const qint64 h = totalMin / 60;
    return QStringLiteral("%1:%2:%3:%4")
        .arg(h, 2, 10, QLatin1Char('0'))
        .arg(m, 2, 10, QLatin1Char('0'))
        .arg(s, 2, 10, QLatin1Char('0'))
        .arg(f, 2, 10, QLatin1Char('0'));
}

QString vegasTsvTimestamp(TimeUs us)
{
    const qint64 clamped = qMax<qint64>(0, us);
    const qint64 ms = (clamped / 1000) % 1000;
    const qint64 totalSec = clamped / kUsPerSecond;
    const qint64 s = totalSec % 60;
    const qint64 totalMin = totalSec / 60;
    const qint64 m = totalMin % 60;
    const qint64 h = totalMin / 60;
    return QStringLiteral("%1:%2:%3.%4")
        .arg(h, 2, 10, QLatin1Char('0'))
        .arg(m, 2, 10, QLatin1Char('0'))
        .arg(s, 2, 10, QLatin1Char('0'))
        .arg(ms, 3, 10, QLatin1Char('0'));
}

QJsonObject otioRationalTime(TimeUs us, double fps)
{
    const qint64 frames = llround((static_cast<double>(us) / static_cast<double>(kUsPerSecond)) * fps);
    return QJsonObject{
        {QStringLiteral("OTIO_SCHEMA"), QStringLiteral("RationalTime.1")},
        {QStringLiteral("value"), static_cast<double>(frames)},
        {QStringLiteral("rate"), fps},
    };
}

QJsonObject otioTimeRange(TimeUs startUs, TimeUs durUs, double fps)
{
    return QJsonObject{
        {QStringLiteral("OTIO_SCHEMA"), QStringLiteral("TimeRange.1")},
        {QStringLiteral("start_time"), otioRationalTime(startUs, fps)},
        {QStringLiteral("duration"), otioRationalTime(durUs, fps)},
    };
}

QString mediaSrcUrl(const QString &path)
{
    if (path.isEmpty())
        return {};
    if (path.startsWith(QLatin1String("file://"), Qt::CaseInsensitive))
        return path;
    return QUrl::fromLocalFile(path).toString();
}

} // namespace

QByteArray exportToFcpxml(const Project &project, QString *error)
{
    Q_UNUSED(error)
    const double fps = projectFpsOrDefault(project);
    const QString projName = project.name().isEmpty() ? QStringLiteral("Drift Project") : project.name();

    QByteArray out;
    QXmlStreamWriter xml(&out);
    xml.setAutoFormatting(true);
    xml.setAutoFormattingIndent(2);
    xml.writeStartDocument();
    xml.writeDTD(QStringLiteral("<!DOCTYPE fcpxml>"));

    xml.writeStartElement(QStringLiteral("fcpxml"));
    xml.writeAttribute(QStringLiteral("version"), QStringLiteral("1.10"));

    xml.writeStartElement(QStringLiteral("resources"));
    xml.writeStartElement(QStringLiteral("format"));
    xml.writeAttribute(QStringLiteral("id"), QStringLiteral("r1"));
    xml.writeAttribute(QStringLiteral("name"),
                       QStringLiteral("FFVideoFormat%1p%2").arg(project.height()).arg(qRound(fps)));
    xml.writeAttribute(QStringLiteral("frameDuration"),
                       QStringLiteral("100/%1s").arg(qMax<qint64>(1, llround(fps * 100.0))));
    xml.writeAttribute(QStringLiteral("width"), QString::number(qMax(1, project.width())));
    xml.writeAttribute(QStringLiteral("height"), QString::number(qMax(1, project.height())));
    xml.writeEndElement(); // format

    QHash<QString, QString> pathToResId;
    int nextRes = 2;
    auto registerAsset = [&](const QString &path, const QString &name, TimeUs durUs, bool hasVideo, bool hasAudio) {
        if (path.isEmpty())
            return QString();
        auto existing = pathToResId.constFind(path);
        if (existing != pathToResId.constEnd())
            return existing.value();
        const QString rid = QStringLiteral("r%1").arg(nextRes++);
        pathToResId.insert(path, rid);

        xml.writeStartElement(QStringLiteral("asset"));
        xml.writeAttribute(QStringLiteral("id"), rid);
        xml.writeAttribute(QStringLiteral("name"),
                           name.isEmpty() ? QFileInfo(path).fileName() : name);
        xml.writeAttribute(QStringLiteral("src"), mediaSrcUrl(path));
        xml.writeAttribute(QStringLiteral("start"), QStringLiteral("0/1s"));
        xml.writeAttribute(QStringLiteral("duration"),
                           fcpxmlTimeString(durUs > 0 ? durUs : 10 * kUsPerSecond, fps));
        xml.writeAttribute(QStringLiteral("hasVideo"), hasVideo ? QStringLiteral("1") : QStringLiteral("0"));
        xml.writeAttribute(QStringLiteral("hasAudio"), hasAudio ? QStringLiteral("1") : QStringLiteral("0"));
        xml.writeEndElement(); // asset
        return rid;
    };

    for (const QString &aid : project.assetOrder()) {
        if (const MediaAsset *asset = project.asset(aid)) {
            const bool isAudio = (asset->kind == MediaKind::Audio);
            const bool hasVideo = !isAudio;
            const bool hasAudio = isAudio || asset->hasAudio;
            registerAsset(asset->path, asset->name, asset->durationUs, hasVideo, hasAudio);
        }
    }
    for (const Track &track : project.tracks()) {
        for (const Clip &clip : track.clips) {
            if (!clip.path.isEmpty() && !pathToResId.contains(clip.path)) {
                const bool isAudio = (clip.type == ClipType::Audio);
                registerAsset(clip.path, clip.name, qMax(clip.srcOut, clip.timelineDuration), !isAudio, isAudio);
            }
        }
    }
    xml.writeEndElement(); // resources

    xml.writeStartElement(QStringLiteral("library"));
    xml.writeStartElement(QStringLiteral("event"));
    xml.writeAttribute(QStringLiteral("name"), projName);
    xml.writeStartElement(QStringLiteral("project"));
    xml.writeAttribute(QStringLiteral("name"), projName);

    xml.writeStartElement(QStringLiteral("sequence"));
    xml.writeAttribute(QStringLiteral("format"), QStringLiteral("r1"));
    xml.writeAttribute(QStringLiteral("duration"), fcpxmlTimeString(qMax<TimeUs>(0, project.durationUs()), fps));
    xml.writeAttribute(QStringLiteral("tcStart"), QStringLiteral("0/1s"));
    xml.writeAttribute(QStringLiteral("tcFormat"), QStringLiteral("NDF"));

    xml.writeStartElement(QStringLiteral("spine"));

    const Track *primaryTrack = nullptr;
    for (const Track &track : project.tracks()) {
        if (!track.isAdjustment() && !track.hidden && track.type == TrackType::Video) {
            primaryTrack = &track;
            break;
        }
    }
    if (!primaryTrack) {
        for (const Track &track : project.tracks()) {
            if (!track.isAdjustment() && !track.hidden) {
                primaryTrack = &track;
                break;
            }
        }
    }

    struct Connected {
        const Clip *clip;
        int lane;
    };
    QList<Connected> connected;
    int nextVideoLane = 1;
    int nextAudioLane = -1;
    for (const Track &track : project.tracks()) {
        if (&track == primaryTrack || track.isAdjustment() || track.hidden)
            continue;
        const bool audio = track.type == TrackType::Audio;
        const int lane = audio ? nextAudioLane-- : nextVideoLane++;
        for (const Clip &clip : track.clips) {
            if (clip.type == ClipType::Text || pathToResId.value(clip.path).isEmpty())
                continue;
            connected.append({&clip, lane});
        }
    }

    QList<const Clip *> primaryClips;
    if (primaryTrack) {
        for (const Clip &clip : primaryTrack->clips)
            primaryClips.append(&clip);
    }
    std::sort(primaryClips.begin(), primaryClips.end(), [](const Clip *a, const Clip *b) {
        return a->timelineStart < b->timelineStart;
    });

    struct SpineItem {
        const Clip *clip = nullptr; // null for a gap
        TimeUs start = 0;
        TimeUs duration = 0;
    };
    QList<SpineItem> spine;
    TimeUs cursorUs = 0;
    for (const Clip *clip : primaryClips) {
        const bool usable = clip->type != ClipType::Text && !pathToResId.value(clip->path).isEmpty();
        const TimeUs start = qMax(clip->timelineStart, cursorUs);
        if (start > cursorUs)
            spine.append({nullptr, cursorUs, start - cursorUs});
        const TimeUs dur = qMax<TimeUs>(0, clip->timelineEnd() - start);
        if (dur <= 0)
            continue;
        spine.append({usable ? clip : nullptr, start, dur});
        cursorUs = start + dur;
    }

    const TimeUs frameUs = static_cast<TimeUs>(kUsPerSecond / fps);
    TimeUs neededEnd = cursorUs;
    for (const Connected &c : connected)
        neededEnd = qMax(neededEnd, qMax<TimeUs>(0, c.clip->timelineStart) + c.clip->timelineDuration);
    for (const Bookmark &bm : project.bookmarks())
        neededEnd = qMax(neededEnd, qMax<TimeUs>(0, bm.timeUs) + frameUs);
    if (neededEnd > cursorUs)
        spine.append({nullptr, cursorUs, neededEnd - cursorUs});

    auto itemIndexAt = [&](TimeUs t) {
        for (int i = 0; i < spine.size(); ++i) {
            if (t >= spine.at(i).start && t < spine.at(i).start + spine.at(i).duration)
                return i;
        }
        return spine.isEmpty() ? -1 : static_cast<int>(spine.size()) - 1;
    };
    auto localStart = [](const SpineItem &item) { return item.clip ? item.clip->srcIn : TimeUs(0); };

    QList<QList<Connected>> itemConnected(spine.size());
    QList<QList<const Bookmark *>> itemMarkers(spine.size());
    for (const Connected &c : connected) {
        const int idx = itemIndexAt(qMax<TimeUs>(0, c.clip->timelineStart));
        if (idx >= 0)
            itemConnected[idx].append(c);
    }
    for (const Bookmark &bm : project.bookmarks()) {
        const int idx = itemIndexAt(qMax<TimeUs>(0, bm.timeUs));
        if (idx >= 0)
            itemMarkers[idx].append(&bm);
    }

    const QString frameDur = QStringLiteral("100/%1s").arg(qMax<qint64>(1, llround(fps * 100.0)));
    auto writeAssetClip = [&](const Clip &clip, TimeUs offset, TimeUs start, int lane) {
        xml.writeStartElement(QStringLiteral("asset-clip"));
        xml.writeAttribute(QStringLiteral("ref"), pathToResId.value(clip.path));
        xml.writeAttribute(QStringLiteral("name"), clip.name.isEmpty() ? QFileInfo(clip.path).fileName() : clip.name);
        if (lane != 0)
            xml.writeAttribute(QStringLiteral("lane"), QString::number(lane));
        xml.writeAttribute(QStringLiteral("offset"), fcpxmlTimeString(offset, fps));
        xml.writeAttribute(QStringLiteral("start"), fcpxmlTimeString(start, fps));
        xml.writeAttribute(QStringLiteral("duration"), fcpxmlTimeString(clip.timelineDuration, fps));
        xml.writeAttribute(QStringLiteral("enabled"), clip.enabled ? QStringLiteral("1") : QStringLiteral("0"));
        xml.writeEndElement();
    };

    for (int i = 0; i < spine.size(); ++i) {
        const SpineItem &item = spine.at(i);
        if (item.clip) {
            xml.writeStartElement(QStringLiteral("asset-clip"));
            xml.writeAttribute(QStringLiteral("ref"), pathToResId.value(item.clip->path));
            xml.writeAttribute(QStringLiteral("name"),
                               item.clip->name.isEmpty() ? QFileInfo(item.clip->path).fileName() : item.clip->name);
            xml.writeAttribute(QStringLiteral("enabled"),
                               item.clip->enabled ? QStringLiteral("1") : QStringLiteral("0"));
        } else {
            xml.writeStartElement(QStringLiteral("gap"));
            xml.writeAttribute(QStringLiteral("name"), QStringLiteral("Gap"));
        }
        xml.writeAttribute(QStringLiteral("offset"), fcpxmlTimeString(item.start, fps));
        xml.writeAttribute(QStringLiteral("start"), fcpxmlTimeString(localStart(item), fps));
        xml.writeAttribute(QStringLiteral("duration"), fcpxmlTimeString(item.duration, fps));

        for (const Connected &c : itemConnected.at(i)) {
            const TimeUs local = localStart(item) + qMax<TimeUs>(0, c.clip->timelineStart) - item.start;
            writeAssetClip(*c.clip, local, c.clip->srcIn, c.lane);
        }
        for (const Bookmark *bm : itemMarkers.at(i)) {
            xml.writeStartElement(QStringLiteral("marker"));
            xml.writeAttribute(QStringLiteral("start"),
                               fcpxmlTimeString(localStart(item) + qMax<TimeUs>(0, bm->timeUs) - item.start, fps));
            xml.writeAttribute(QStringLiteral("duration"), frameDur);
            xml.writeAttribute(QStringLiteral("value"), bm->label.isEmpty() ? QStringLiteral("Marker") : bm->label);
            xml.writeEndElement();
        }
        xml.writeEndElement();
    }

    xml.writeEndElement(); // spine
    xml.writeEndElement(); // sequence
    xml.writeEndElement(); // project
    xml.writeEndElement(); // event
    xml.writeEndElement(); // library
    xml.writeEndElement(); // fcpxml
    xml.writeEndDocument();
    return out;
}

QByteArray exportToEdl(const Project &project, QString *error)
{
    Q_UNUSED(error)
    const double fps = projectFpsOrDefault(project);
    const QString projName = project.name().isEmpty() ? QStringLiteral("DRIFT_PROJECT") : project.name();

    QStringList lines;
    lines << QStringLiteral("TITLE: %1").arg(projName);
    lines << QStringLiteral("FCM: NON-DROP FRAME");
    lines << QStringLiteral("* FPS: %1").arg(fps, 0, 'f', 3);
    lines << QString();

    struct EdlRow {
        const Track *track = nullptr;
        const Clip *clip = nullptr;
    };
    QList<EdlRow> rows;
    for (const Track &track : project.tracks()) {
        if (track.isAdjustment() || track.hidden)
            continue;
        if (track.type != TrackType::Video && track.type != TrackType::Audio)
            continue;
        for (const Clip &clip : track.clips) {
            if (!clip.enabled || clip.timelineDuration <= 0)
                continue;
            rows.append({&track, &clip});
        }
    }

    std::stable_sort(rows.begin(), rows.end(), [](const EdlRow &a, const EdlRow &b) {
        if (a.clip->timelineStart != b.clip->timelineStart)
            return a.clip->timelineStart < b.clip->timelineStart;
        return static_cast<int>(a.track->type) < static_cast<int>(b.track->type);
    });

    int evNum = 1;
    for (const EdlRow &r : rows) {
        const QString chan = (r.track->type == TrackType::Audio) ? QStringLiteral("A") : QStringLiteral("V");
        const QString reel = QStringLiteral("AX");
        const QString srcIn = edlTimecode(r.clip->srcIn, fps);
        const QString srcOut = edlTimecode(qMax(r.clip->srcIn, r.clip->srcOut), fps);
        const QString recIn = edlTimecode(r.clip->timelineStart, fps);
        const QString recOut = edlTimecode(r.clip->timelineEnd(), fps);

        lines << QStringLiteral("%1  %2       %3     C        %4 %5 %6 %7")
                     .arg(evNum, 3, 10, QLatin1Char('0'))
                     .arg(reel, -8)
                     .arg(chan, -5)
                     .arg(srcIn, srcOut, recIn, recOut);

        const QString clipName = r.clip->name.isEmpty() ? QFileInfo(r.clip->path).fileName() : r.clip->name;
        if (!clipName.isEmpty())
            lines << QStringLiteral("* FROM CLIP NAME: %1").arg(clipName);
        if (!r.clip->path.isEmpty())
            lines << QStringLiteral("* SOURCE FILE: %1").arg(r.clip->path);
        ++evNum;
    }

    for (const Bookmark &bm : project.bookmarks()) {
        lines << QStringLiteral("* LOC: %1 BLUE %2")
                     .arg(edlTimecode(bm.timeUs, fps),
                          bm.label.isEmpty() ? QStringLiteral("Marker") : bm.label);
    }

    return lines.join(QLatin1Char('\n')).toUtf8() + '\n';
}

QByteArray exportToOtio(const Project &project, QString *error)
{
    Q_UNUSED(error)
    const double fps = projectFpsOrDefault(project);

    QJsonArray markerArr;
    for (const Bookmark &bm : project.bookmarks()) {
        markerArr.append(QJsonObject{
            {QStringLiteral("OTIO_SCHEMA"), QStringLiteral("Marker.2")},
            {QStringLiteral("name"), bm.label.isEmpty() ? QStringLiteral("Marker") : bm.label},
            {QStringLiteral("color"), QStringLiteral("BLUE")},
            {QStringLiteral("marked_range"), otioTimeRange(bm.timeUs, 0, fps)},
        });
    }

    QJsonArray trackArr;
    int tIdx = 1;
    for (const Track &track : project.tracks()) {
        if (track.isAdjustment() || track.hidden)
            continue;
        const bool isAudio = (track.type == TrackType::Audio);
        QList<Clip> sortedClips = track.clips;
        std::sort(sortedClips.begin(), sortedClips.end(), [](const Clip &a, const Clip &b) {
            return a.timelineStart < b.timelineStart;
        });

        QJsonArray children;
        TimeUs cursorUs = 0;
        for (const Clip &clip : sortedClips) {
            if (clip.timelineStart > cursorUs) {
                children.append(QJsonObject{
                    {QStringLiteral("OTIO_SCHEMA"), QStringLiteral("Gap.1")},
                    {QStringLiteral("name"), QStringLiteral("Gap")},
                    {QStringLiteral("source_range"), otioTimeRange(0, clip.timelineStart - cursorUs, fps)},
                });
            }
            const TimeUs srcDur = clip.srcOut > clip.srcIn ? clip.srcOut - clip.srcIn : clip.timelineDuration;
            QJsonObject mediaRef{
                {QStringLiteral("OTIO_SCHEMA"), QStringLiteral("ExternalReference.1")},
                {QStringLiteral("name"), clip.name.isEmpty() ? QFileInfo(clip.path).fileName() : clip.name},
                {QStringLiteral("target_url"), mediaSrcUrl(clip.path)},
                {QStringLiteral("available_range"), otioTimeRange(0, qMax(clip.srcOut, srcDur), fps)},
            };
            children.append(QJsonObject{
                {QStringLiteral("OTIO_SCHEMA"), QStringLiteral("Clip.1")},
                {QStringLiteral("name"), clip.name.isEmpty() ? QStringLiteral("Clip") : clip.name},
                {QStringLiteral("enabled"), clip.enabled},
                {QStringLiteral("source_range"), otioTimeRange(clip.srcIn, clip.timelineDuration, fps)},
                {QStringLiteral("media_reference"), mediaRef},
            });
            cursorUs = qMax(cursorUs, clip.timelineEnd());
        }

        trackArr.append(QJsonObject{
            {QStringLiteral("OTIO_SCHEMA"), QStringLiteral("Track.1")},
            {QStringLiteral("name"), track.name.isEmpty() ? QStringLiteral("Track %1").arg(tIdx) : track.name},
            {QStringLiteral("kind"), isAudio ? QStringLiteral("Audio") : QStringLiteral("Video")},
            {QStringLiteral("children"), children},
        });
        ++tIdx;
    }

    const QJsonObject root{
        {QStringLiteral("OTIO_SCHEMA"), QStringLiteral("Timeline.1")},
        {QStringLiteral("name"), project.name().isEmpty() ? QStringLiteral("Drift Project") : project.name()},
        {QStringLiteral("global_start_time"), otioRationalTime(0, fps)},
        {QStringLiteral("metadata"),
         QJsonObject{{QStringLiteral("drift"),
                      QJsonObject{{QStringLiteral("width"), project.width()},
                                  {QStringLiteral("height"), project.height()},
                                  {QStringLiteral("rate"), fps}}}}},
        {QStringLiteral("markers"), markerArr},
        {QStringLiteral("tracks"),
         QJsonObject{
             {QStringLiteral("OTIO_SCHEMA"), QStringLiteral("Stack.1")},
             {QStringLiteral("name"), QStringLiteral("tracks")},
             {QStringLiteral("children"), trackArr},
         }},
    };
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

QByteArray exportToVegasMarkersTsv(const Project &project, QString *error)
{
    Q_UNUSED(error)
    QStringList lines;
    lines << QStringLiteral("ID\tPosition\tLength\tName\tType");

    int nextId = 1;
    for (const Bookmark &bm : project.bookmarks()) {
        QString label = bm.label;
        label.replace(QLatin1Char('\t'), QLatin1Char(' ')).replace(QLatin1Char('\n'), QLatin1Char(' '));
        lines << QStringLiteral("%1\t%2\t00:00:00.000\t%3\tMarker")
                     .arg(nextId++)
                     .arg(vegasTsvTimestamp(bm.timeUs), label);
    }
    return lines.join(QLatin1Char('\n')).toUtf8() + '\n';
}

bool exportProjectInterchange(const Project &project, const QString &filePath,
                              const QString &format, QString *error)
{
    if (filePath.trimmed().isEmpty()) {
        if (error)
            *error = QStringLiteral("Output path is empty");
        return false;
    }

    QString fmt = format.trimmed().toLower();
    if (fmt.isEmpty()) {
        const QString ext = QFileInfo(filePath).suffix().toLower();
        if (ext == QLatin1String("fcpxml") || ext == QLatin1String("xml"))
            fmt = QStringLiteral("fcpxml");
        else if (ext == QLatin1String("edl"))
            fmt = QStringLiteral("edl");
        else if (ext == QLatin1String("otio"))
            fmt = QStringLiteral("otio");
        else if (ext == QLatin1String("txt") || ext == QLatin1String("tsv"))
            fmt = QStringLiteral("txt");
    }

    QByteArray payload;
    if (fmt == QLatin1String("fcpxml")) {
        payload = exportToFcpxml(project, error);
    } else if (fmt == QLatin1String("edl")) {
        payload = exportToEdl(project, error);
    } else if (fmt == QLatin1String("otio")) {
        payload = exportToOtio(project, error);
    } else if (fmt == QLatin1String("txt") || fmt == QLatin1String("vegas_tsv")) {
        payload = exportToVegasMarkersTsv(project, error);
    } else {
        if (error)
            *error = QStringLiteral("Unsupported timeline interchange format: %1").arg(fmt);
        return false;
    }

    if (payload.isEmpty()) {
        if (error && error->isEmpty())
            *error = QStringLiteral("Failed to serialize timeline interchange data");
        return false;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error)
            *error = QStringLiteral("Could not write file: %1").arg(file.errorString());
        return false;
    }
    if (file.write(payload) != payload.size()) {
        if (error)
            *error = QStringLiteral("Incomplete write to %1").arg(filePath);
        return false;
    }
    file.close();
    return true;
}

} // namespace drift::interchange
