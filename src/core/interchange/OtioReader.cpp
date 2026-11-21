#include "core/interchange/OtioReader.h"

#include "core/project/clip/Clip.h"
#include "core/project/media/MediaAsset.h"
#include "core/project/Project.h"
#include "core/project/Track.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QUrl>
#include <QUuid>
#include <QtMath>
#include <algorithm>

namespace drift::otio {

namespace {

QString normalizePath(const QString &rawPath)
{
    if (rawPath.isEmpty())
        return {};

    QString p = rawPath.trimmed();
    if (p.startsWith(QLatin1String("file://"), Qt::CaseInsensitive)) {
        QUrl url(p);
        const QString local = url.toLocalFile();
        if (!local.isEmpty())
            p = local;
        else
            p = p.mid(7);
    }

    p.replace(QLatin1Char('\\'), QLatin1Char('/'));
    if (p.startsWith(QLatin1String("localhost/"), Qt::CaseInsensitive))
        p = p.mid(9);

    return p;
}

QString resolveMediaPath(const QString &path, const QString &sourceDir)
{
    const QString norm = normalizePath(path);
    if (norm.isEmpty())
        return {};

    if (QFileInfo::exists(norm))
        return norm;

    if (!sourceDir.isEmpty()) {
        const QString relative = QDir(sourceDir).filePath(norm);
        if (QFileInfo(norm).isRelative() && QFileInfo::exists(relative))
            return QDir::cleanPath(relative);
        const QString candidate = QDir(sourceDir).filePath(QFileInfo(norm).fileName());
        if (QFileInfo::exists(candidate))
            return candidate;
    }

    return norm;
}

TimeUs parseRationalTime(const QJsonObject &obj, double fallbackRate)
{
    const double val = obj.value(QStringLiteral("value")).toDouble();
    double rate = obj.value(QStringLiteral("rate")).toDouble();
    if (rate <= 0.0)
        rate = fallbackRate > 0.0 ? fallbackRate : 24.0;
    return static_cast<TimeUs>(llround((val / rate) * 1000000.0));
}

const QStringList &audioExtensions()
{
    static const QStringList exts = {QStringLiteral("mp3"), QStringLiteral("wav"), QStringLiteral("aac"),
                                     QStringLiteral("m4a"), QStringLiteral("flac"), QStringLiteral("ogg"),
                                     QStringLiteral("opus"), QStringLiteral("aif"), QStringLiteral("aiff")};
    return exts;
}

bool parseTimeRange(const QJsonObject &obj, TimeUs *outStartUs, TimeUs *outDurUs, double fallbackRate)
{
    if (obj.isEmpty())
        return false;

    const QJsonObject startObj = obj.value(QStringLiteral("start_time")).toObject();
    const QJsonObject durObj = obj.value(QStringLiteral("duration")).toObject();

    if (outStartUs)
        *outStartUs = parseRationalTime(startObj, fallbackRate);
    if (outDurUs)
        *outDurUs = parseRationalTime(durObj, fallbackRate);

    return true;
}

} // namespace

bool isOtioTimeline(const QString &filePath)
{
    if (filePath.endsWith(QLatin1String(".otio"), Qt::CaseInsensitive))
        return true;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return false;

    const QByteArray header = file.read(1024);
    return isOtioData(header);
}

bool isOtioData(const QByteArray &data)
{
    if (data.isEmpty())
        return false;

    const QString text = QString::fromUtf8(data.left(1024)).trimmed();
    return text.contains(QLatin1String("\"OTIO_SCHEMA\""), Qt::CaseInsensitive)
        && (text.contains(QLatin1String("Timeline"), Qt::CaseInsensitive)
            || text.contains(QLatin1String("Stack"), Qt::CaseInsensitive)
            || text.contains(QLatin1String("Track"), Qt::CaseInsensitive));
}

std::optional<Project> readProject(const QString &filePath, QString *error, int *skippedOut)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QObject::tr("Cannot open file: %1").arg(filePath);
        return std::nullopt;
    }

    const QByteArray data = file.readAll();
    const QString sourceDir = QFileInfo(filePath).absolutePath();
    return readProjectData(data, sourceDir, error, skippedOut);
}

std::optional<Project> readProjectData(const QByteArray &data, const QString &sourceDir, QString *error,
                                      int *skippedOut)
{
    if (data.isEmpty()) {
        if (error)
            *error = QObject::tr("File is empty");
        return std::nullopt;
    }

    QJsonParseError parseErr;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseErr);
    if (doc.isNull() || !doc.isObject()) {
        if (error)
            *error = QObject::tr("Failed to parse OpenTimelineIO JSON: %1").arg(parseErr.errorString());
        return std::nullopt;
    }

    const QJsonObject root = doc.object();
    const QString schema = root.value(QStringLiteral("OTIO_SCHEMA")).toString();
    if (!schema.startsWith(QLatin1String("Timeline"), Qt::CaseInsensitive)
        && !schema.startsWith(QLatin1String("Stack"), Qt::CaseInsensitive)) {
        if (error)
            *error = QObject::tr("Not an OpenTimelineIO Timeline or Stack (schema: %1)").arg(schema);
        return std::nullopt;
    }

    Project project;

    // Project Name
    const QString projName = root.value(QStringLiteral("name")).toString();
    if (!projName.isEmpty())
        project.setName(projName);

    // Frame rate detection
    double projectFps = 24.0;
    const QJsonObject globalStart = root.value(QStringLiteral("global_start_time")).toObject();
    if (globalStart.contains(QStringLiteral("rate"))) {
        const double r = globalStart.value(QStringLiteral("rate")).toDouble();
        if (r > 0.0)
            projectFps = r;
    }

    // Resolution detection from metadata (or default 1920x1080)
    int width = 1920;
    int height = 1080;
    const QJsonObject meta = root.value(QStringLiteral("metadata")).toObject();
    for (auto it = meta.constBegin(); it != meta.constEnd(); ++it) {
        if (it.value().isObject()) {
            const QJsonObject sub = it.value().toObject();
            if (sub.contains(QStringLiteral("width")) && sub.value(QStringLiteral("width")).toInt() > 0)
                width = sub.value(QStringLiteral("width")).toInt();
            if (sub.contains(QStringLiteral("height")) && sub.value(QStringLiteral("height")).toInt() > 0)
                height = sub.value(QStringLiteral("height")).toInt();
            if (sub.contains(QStringLiteral("rate")) && sub.value(QStringLiteral("rate")).toDouble() > 0.0)
                projectFps = sub.value(QStringLiteral("rate")).toDouble();
        }
    }

    project.setResolution(width, height);
    project.setFps(qMax(1, qRound(projectFps)));

    // Timeline Markers
    const QJsonArray markers = root.value(QStringLiteral("markers")).toArray();
    for (const QJsonValue &mVal : markers) {
        if (!mVal.isObject())
            continue;
        const QJsonObject mObj = mVal.toObject();
        const QString mName = mObj.value(QStringLiteral("name")).toString();
        const QString mColor = mObj.value(QStringLiteral("color")).toString().toLower();
        const QJsonObject mRange = mObj.value(QStringLiteral("marked_range")).toObject();
        TimeUs mStartUs = 0;
        parseTimeRange(mRange, &mStartUs, nullptr, projectFps);

        Bookmark bm;
        bm.timeUs = mStartUs;
        bm.label = mName.isEmpty() ? QStringLiteral("Marker") : mName;
        project.bookmarks().append(bm);
    }

    // Tracks navigation
    QJsonArray trackArray;
    if (root.contains(QStringLiteral("tracks"))) {
        const QJsonObject tracksObj = root.value(QStringLiteral("tracks")).toObject();
        trackArray = tracksObj.value(QStringLiteral("children")).toArray();
    } else if (root.contains(QStringLiteral("children"))) {
        trackArray = root.value(QStringLiteral("children")).toArray();
    }

    QMap<QString, QString> mediaUrlToAssetId;
    QList<Track> builtTracks;
    int skipped = 0;

    for (const QJsonValue &tVal : trackArray) {
        if (!tVal.isObject())
            continue;
        const QJsonObject tObj = tVal.toObject();
        const QString kind = tObj.value(QStringLiteral("kind")).toString();
        const QString tName = tObj.value(QStringLiteral("name")).toString();

        const bool isAudio = (kind.compare(QLatin1String("Audio"), Qt::CaseInsensitive) == 0);
        const TrackType trackType = isAudio ? TrackType::Audio : TrackType::Video;
        Track track;
        track.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        track.type = trackType;
        track.name = tName;
        track.hidden = !isAudio && !tObj.value(QStringLiteral("enabled")).toBool(true);
        track.muted = isAudio && !tObj.value(QStringLiteral("enabled")).toBool(true);
        skipped += tObj.value(QStringLiteral("effects")).toArray().size();

        const QJsonArray children = tObj.value(QStringLiteral("children")).toArray();
        TimeUs currentTimelineUs = 0;

        for (const QJsonValue &cVal : children) {
            if (!cVal.isObject())
                continue;

            const QJsonObject cObj = cVal.toObject();
            const QString itemSchema = cObj.value(QStringLiteral("OTIO_SCHEMA")).toString();

            // Transitions overlap their neighbours rather than taking track time.
            if (itemSchema.startsWith(QLatin1String("Transition"), Qt::CaseInsensitive)) {
                ++skipped;
                continue;
            }

            TimeUs srcInUs = 0;
            TimeUs durUs = 0;
            const QJsonObject rangeObj = cObj.value(QStringLiteral("source_range")).toObject();
            if (!rangeObj.isEmpty())
                parseTimeRange(rangeObj, &srcInUs, &durUs, projectFps);

            if (itemSchema.startsWith(QLatin1String("Gap"), Qt::CaseInsensitive)) {
                currentTimelineUs += durUs;
                continue;
            }

            // Nested stacks/tracks: keep their time so the rest of the track stays in place.
            if (!itemSchema.startsWith(QLatin1String("Clip"), Qt::CaseInsensitive)) {
                ++skipped;
                currentTimelineUs += durUs;
                continue;
            }

            // OTIO 0.15+ keeps several references keyed by name; older files have one.
            QJsonObject mRef = cObj.value(QStringLiteral("media_reference")).toObject();
            if (mRef.isEmpty()) {
                const QJsonObject refs = cObj.value(QStringLiteral("media_references")).toObject();
                QString key = cObj.value(QStringLiteral("active_media_reference_key")).toString();
                if (key.isEmpty())
                    key = QStringLiteral("DEFAULT_MEDIA");
                mRef = refs.value(key).toObject();
            }

            if (rangeObj.isEmpty())
                parseTimeRange(mRef.value(QStringLiteral("available_range")).toObject(), &srcInUs, &durUs, projectFps);
            if (durUs <= 0)
                durUs = 5000000LL; // default 5.0s

            const QString targetUrl = mRef.value(QStringLiteral("target_url")).toString();
            // Missing/generator references (solid colours, offline media) have nothing to point at.
            if (targetUrl.isEmpty()) {
                ++skipped;
                currentTimelineUs += durUs;
                continue;
            }

            double speed = 1.0;
            for (const QJsonValue &eVal : cObj.value(QStringLiteral("effects")).toArray()) {
                const QJsonObject eObj = eVal.toObject();
                if (eObj.value(QStringLiteral("OTIO_SCHEMA")).toString().startsWith(QLatin1String("LinearTimeWarp"))
                    && eObj.value(QStringLiteral("time_scalar")).toDouble() != 0.0)
                    speed = eObj.value(QStringLiteral("time_scalar")).toDouble();
                else
                    ++skipped;
            }

            const QString clipName = cObj.value(QStringLiteral("name")).toString();
            const QString resolvedPath = resolveMediaPath(targetUrl, sourceDir);
            const QString ext = QFileInfo(resolvedPath).suffix().toLower();
            const bool imageFile = imageExtensions().contains(ext);
            const bool audioFile = audioExtensions().contains(ext);

            // OTIO durations are in track time; a time warp only changes how much source plays.
            Clip clip;
            clip.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            clip.name = clipName.isEmpty() ? QFileInfo(resolvedPath).fileName() : clipName;
            clip.timelineStart = currentTimelineUs;
            clip.timelineDuration = durUs;
            clip.srcIn = srcInUs;
            clip.srcOut = srcInUs + static_cast<TimeUs>(llround(static_cast<double>(durUs) * qAbs(speed)));
            clip.speed = qAbs(speed);
            clip.reverse = speed < 0.0;
            clip.type = (isAudio || audioFile) ? ClipType::Audio : (imageFile ? ClipType::Image : ClipType::Video);
            clip.path = resolvedPath;

            QString assetId = mediaUrlToAssetId.value(resolvedPath);
            if (assetId.isEmpty()) {
                MediaAsset asset;
                asset.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
                asset.name = QFileInfo(resolvedPath).fileName();
                asset.path = resolvedPath;
                // By file, not by track: a video file's sound sits on an audio track.
                asset.kind = audioFile ? MediaKind::Audio : (imageFile ? MediaKind::Image : MediaKind::Video);
                assetId = project.addAsset(asset);
                mediaUrlToAssetId.insert(resolvedPath, assetId);
            }
            clip.assetId = assetId;

            track.clips.append(clip);
            currentTimelineUs += durUs;
        }

        builtTracks.append(track);
    }

    // OTIO stacks list tracks bottom to top; Drift's track 0 is the top, with audio below all video.
    std::reverse(builtTracks.begin(), builtTracks.end());
    std::stable_partition(builtTracks.begin(), builtTracks.end(),
                          [](const Track &t) { return t.type != TrackType::Audio; });
    project.tracks() = builtTracks;
    if (skippedOut)
        *skippedOut = skipped;

    if (project.tracks().isEmpty()) {
        project.resetToDefaultTimeline();
    }

    project.ensureTrackIds();
    return project;
}

} // namespace drift::otio
