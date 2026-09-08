#include "OtioReader.h"

#include "Clip.h"
#include "MediaAsset.h"
#include "Project.h"
#include "Track.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QUrl>
#include <QUuid>
#include <QtMath>

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

std::optional<Project> readProject(const QString &filePath, QString *error)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QObject::tr("Cannot open file: %1").arg(filePath);
        return std::nullopt;
    }

    const QByteArray data = file.readAll();
    const QString sourceDir = QFileInfo(filePath).absolutePath();
    return readProjectData(data, sourceDir, error);
}

std::optional<Project> readProjectData(const QByteArray &data, const QString &sourceDir, QString *error)
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

    if (!trackArray.isEmpty()) {
        project.tracks().clear();
    }

    QMap<QString, QString> mediaUrlToAssetId;

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
        track.name = tName.isEmpty() ? (isAudio ? QStringLiteral("Audio Track") : QStringLiteral("Video Track")) : tName;

        const QJsonArray children = tObj.value(QStringLiteral("children")).toArray();
        TimeUs currentTimelineUs = 0;

        for (const QJsonValue &cVal : children) {
            if (!cVal.isObject())
                continue;

            const QJsonObject cObj = cVal.toObject();
            const QString itemSchema = cObj.value(QStringLiteral("OTIO_SCHEMA")).toString();

            // Gap
            if (itemSchema.startsWith(QLatin1String("Gap"), Qt::CaseInsensitive)) {
                TimeUs gapDurUs = 0;
                const QJsonObject rangeObj = cObj.value(QStringLiteral("source_range")).toObject();
                parseTimeRange(rangeObj, nullptr, &gapDurUs, projectFps);
                currentTimelineUs += gapDurUs;
                continue;
            }

            // Clip
            if (itemSchema.startsWith(QLatin1String("Clip"), Qt::CaseInsensitive)) {
                const QString clipName = cObj.value(QStringLiteral("name")).toString();

                TimeUs srcInUs = 0;
                TimeUs durUs = 0;
                const QJsonObject rangeObj = cObj.value(QStringLiteral("source_range")).toObject();
                if (!rangeObj.isEmpty()) {
                    parseTimeRange(rangeObj, &srcInUs, &durUs, projectFps);
                }

                if (durUs <= 0)
                    durUs = 5000000LL; // default 5.0s

                Clip clip;
                clip.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
                clip.name = clipName.isEmpty() ? QStringLiteral("Clip") : clipName;
                clip.timelineStart = currentTimelineUs;
                clip.timelineDuration = durUs;
                clip.srcIn = srcInUs;
                clip.srcOut = srcInUs + durUs;
                clip.type = isAudio ? ClipType::Audio : ClipType::Video;

                // Media Reference
                const QJsonObject mRef = cObj.value(QStringLiteral("media_reference")).toObject();
                QString targetUrl = mRef.value(QStringLiteral("target_url")).toString();
                if (targetUrl.isEmpty())
                    targetUrl = mRef.value(QStringLiteral("name")).toString();

                if (!targetUrl.isEmpty()) {
                    const QString resolvedPath = resolveMediaPath(targetUrl, sourceDir);

                    QString assetId;
                    if (mediaUrlToAssetId.contains(resolvedPath)) {
                        assetId = mediaUrlToAssetId.value(resolvedPath);
                    } else {
                        assetId = QUuid::createUuid().toString(QUuid::WithoutBraces);
                        MediaAsset asset;
                        asset.id = assetId;
                        asset.name = QFileInfo(resolvedPath).fileName();
                        if (asset.name.isEmpty())
                            asset.name = clip.name;
                        asset.path = resolvedPath;
                        asset.kind = isAudio ? MediaKind::Audio : MediaKind::Video;
                        project.addAsset(asset);
                        mediaUrlToAssetId.insert(resolvedPath, assetId);
                    }

                    clip.assetId = assetId;
                }

                track.clips.append(clip);
                currentTimelineUs += durUs;
            }
        }

        project.tracks().append(track);
    }

    if (project.tracks().isEmpty()) {
        project.resetToDefaultTimeline();
    }

    project.ensureTrackIds();
    return project;
}

} // namespace drift::otio
