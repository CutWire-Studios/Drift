#include "PrprojReader.h"

#include "Clip.h"
#include "MediaAsset.h"
#include "Project.h"
#include "Track.h"

#include <QDir>
#include <QDirIterator>
#include <QDomDocument>
#include <QFile>
#include <QFileInfo>
#include <QUrl>
#include <QUuid>
#include <QtMath>
#include <cstring>
#include <zlib.h>

namespace drift::prproj {

namespace {

constexpr qint64 kPremiereTicksPerSecond = 254016000000LL;
constexpr qint64 kTicksPerMicrosecond = 254016LL;

bool isGzip(const QByteArray &data)
{
    return data.size() >= 2
        && static_cast<uint8_t>(data[0]) == 0x1f
        && static_cast<uint8_t>(data[1]) == 0x8b;
}

QByteArray decompressGzip(const QByteArray &data)
{
    if (!isGzip(data))
        return data;

    z_stream strm;
    std::memset(&strm, 0, sizeof(strm));
    if (inflateInit2(&strm, 32 + MAX_WBITS) != Z_OK)
        return {};

    strm.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(data.constData()));
    strm.avail_in = static_cast<uInt>(data.size());

    QByteArray out;
    char buffer[65536];

    int ret = Z_OK;
    while (ret == Z_OK) {
        strm.next_out = reinterpret_cast<Bytef *>(buffer);
        strm.avail_out = sizeof(buffer);

        ret = inflate(&strm, Z_NO_FLUSH);
        if (ret != Z_OK && ret != Z_STREAM_END && ret != Z_BUF_ERROR) {
            inflateEnd(&strm);
            return {};
        }

        const int have = sizeof(buffer) - strm.avail_out;
        if (have > 0)
            out.append(buffer, have);
    }

    inflateEnd(&strm);
    return (ret == Z_STREAM_END || !out.isEmpty()) ? out : QByteArray{};
}

TimeUs ticksToTimeUs(qint64 ticks, int fps = 30)
{
    if (ticks <= 0)
        return 0;

    // Ticks > 10,000,000 are Premiere integer ticks
    if (ticks > 10000000LL) {
        return static_cast<TimeUs>(ticks / kTicksPerMicrosecond);
    }

    // Small numbers represent frame counts (from FCP XML or frame-based export)
    const int effectiveFps = fps > 0 ? fps : 30;
    return static_cast<TimeUs>(llround((static_cast<double>(ticks) * 1000000.0) / effectiveFps));
}

QString normalizeMediaPath(const QString &rawPath)
{
    if (rawPath.isEmpty())
        return {};

    QString path = rawPath.trimmed();
    if (path.startsWith(QLatin1String("file://"), Qt::CaseInsensitive)) {
        QUrl url(path);
        const QString local = url.toLocalFile();
        if (!local.isEmpty())
            path = local;
        else
            path = path.mid(7);
    }

    path.replace(QLatin1Char('\\'), QLatin1Char('/'));

    if (path.startsWith(QLatin1String("localhost/"), Qt::CaseInsensitive))
        path = path.mid(9);

    return path;
}

QString resolveMedia(const QString &normalizedPath, const QString &sourceDir)
{
    if (normalizedPath.isEmpty())
        return {};

    if (QFileInfo::exists(normalizedPath))
        return QFileInfo(normalizedPath).canonicalFilePath();

    if (sourceDir.isEmpty())
        return normalizedPath;

    const QString fileName = QFileInfo(normalizedPath).fileName();
    if (fileName.isEmpty())
        return normalizedPath;

    const QStringList candidateDirs = {
        sourceDir,
        QDir(sourceDir).filePath(QStringLiteral("Footage")),
        QDir(sourceDir).filePath(QStringLiteral("Media")),
        QDir(sourceDir).filePath(QStringLiteral("Video")),
        QDir(sourceDir).filePath(QStringLiteral("Audio")),
        QDir(sourceDir).filePath(QStringLiteral("Assets")),
        QDir(sourceDir).filePath(QStringLiteral("clips")),
        QDir(sourceDir).filePath(QStringLiteral("Music")),
        QDir(sourceDir).filePath(QStringLiteral("Images")),
        QDir(sourceDir).filePath(QStringLiteral("placeholders")),
    };

    for (const QString &dir : candidateDirs) {
        const QString testPath = QDir(dir).filePath(fileName);
        if (QFileInfo::exists(testPath))
            return QFileInfo(testPath).canonicalFilePath();
    }

    // Recursive search up to shallow depth
    QDirIterator it(sourceDir, QStringList() << fileName, QDir::Files, QDirIterator::Subdirectories);
    if (it.hasNext()) {
        return QFileInfo(it.next()).canonicalFilePath();
    }

    return normalizedPath;
}

ClipType detectClipType(const QString &mediaPath, const QString &name, bool isAudioTrack, bool isAdjustment)
{
    if (isAdjustment || name.contains(QStringLiteral("adjustment"), Qt::CaseInsensitive))
        return ClipType::Adjustment;

    if (isAudioTrack)
        return ClipType::Audio;

    if (mediaPath.isEmpty()) {
        if (name.contains(QStringLiteral("title"), Qt::CaseInsensitive)
            || name.contains(QStringLiteral("text"), Qt::CaseInsensitive))
            return ClipType::Text;
        return ClipType::Video;
    }

    const QString ext = QFileInfo(mediaPath).suffix().toLower();
    if (ext == QLatin1String("mp3") || ext == QLatin1String("wav") || ext == QLatin1String("aac")
        || ext == QLatin1String("flac") || ext == QLatin1String("m4a") || ext == QLatin1String("ogg"))
        return ClipType::Audio;

    if (ext == QLatin1String("png") || ext == QLatin1String("jpg") || ext == QLatin1String("jpeg")
        || ext == QLatin1String("webp") || ext == QLatin1String("bmp") || ext == QLatin1String("svg"))
        return ClipType::Image;

    return ClipType::Video;
}

void indexObjects(const QDomElement &elem, QHash<QString, QDomElement> &objects)
{
    if (elem.hasAttribute(QStringLiteral("ObjectID"))) {
        objects.insert(elem.attribute(QStringLiteral("ObjectID")), elem);
    }
    for (QDomNode n = elem.firstChild(); !n.isNull(); n = n.nextSibling()) {
        if (n.isElement())
            indexObjects(n.toElement(), objects);
    }
}

QString resolveMediaPathFromElement(const QDomElement &startElem,
                                   const QHash<QString, QDomElement> &objects,
                                   QString *resolvedName = nullptr)
{
    QDomElement current = startElem;
    for (int depth = 0; depth < 6; ++depth) {
        QString path = current.firstChildElement(QStringLiteral("ActualMediaFilePath")).text();
        if (path.isEmpty())
            path = current.firstChildElement(QStringLiteral("FilePath")).text();
        if (path.isEmpty())
            path = current.firstChildElement(QStringLiteral("OriginalFilePath")).text();
        if (path.isEmpty())
            path = current.firstChildElement(QStringLiteral("pathurl")).text();

        if (resolvedName && resolvedName->isEmpty()) {
            const QString name = current.firstChildElement(QStringLiteral("Name")).text();
            if (!name.isEmpty())
                *resolvedName = name;
        }

        if (!path.isEmpty())
            return path;

        QString nextRef;
        const QStringList refTags = {QStringLiteral("SubClip"), QStringLiteral("MasterClip"),
                                     QStringLiteral("Clip"), QStringLiteral("Media"),
                                     QStringLiteral("Source"), QStringLiteral("ProjectItem"),
                                     QStringLiteral("file")};

        for (const QString &tag : refTags) {
            const QDomElement child = current.firstChildElement(tag);
            if (!child.isNull()) {
                if (child.hasAttribute(QStringLiteral("ObjectRef")))
                    nextRef = child.attribute(QStringLiteral("ObjectRef"));
                else if (child.hasAttribute(QStringLiteral("ObjectURef")))
                    nextRef = child.attribute(QStringLiteral("ObjectURef"));
                else if (child.hasAttribute(QStringLiteral("id")))
                    nextRef = child.attribute(QStringLiteral("id"));
                else if (!child.text().isEmpty())
                    nextRef = child.text().trimmed();
                if (!nextRef.isEmpty())
                    break;
            }
        }

        if (nextRef.isEmpty()) {
            if (current.hasAttribute(QStringLiteral("ObjectRef")))
                nextRef = current.attribute(QStringLiteral("ObjectRef"));
            else if (current.hasAttribute(QStringLiteral("ObjectURef")))
                nextRef = current.attribute(QStringLiteral("ObjectURef"));
            else if (current.hasAttribute(QStringLiteral("id")))
                nextRef = current.attribute(QStringLiteral("id"));
        }

        if (nextRef.isEmpty() || !objects.contains(nextRef))
            break;

        current = objects.value(nextRef);
    }

    return {};
}

void parseTrackClips(const QDomElement &trackElem,
                     Track &track,
                     bool isAudio,
                     int fps,
                     const QHash<QString, QDomElement> &objects,
                     const QString &sourceDir,
                     Project &project)
{
    // Collect track item elements: <TrackItem>, <VideoClipTrackItem>, <AudioClipTrackItem>, <clipitem>
    QList<QDomElement> items;
    for (QDomNode n = trackElem.firstChild(); !n.isNull(); n = n.nextSibling()) {
        if (!n.isElement())
            continue;
        const QDomElement el = n.toElement();
        const QString tag = el.tagName().toLower();
        if (tag == QLatin1String("trackitem") || tag == QLatin1String("videocliptrackitem")
            || tag == QLatin1String("audiocliptrackitem") || tag == QLatin1String("clipitem")) {
            if (el.hasAttribute(QStringLiteral("ObjectRef")) && objects.contains(el.attribute(QStringLiteral("ObjectRef")))) {
                items.append(objects.value(el.attribute(QStringLiteral("ObjectRef"))));
            } else if (el.hasAttribute(QStringLiteral("ObjectURef")) && objects.contains(el.attribute(QStringLiteral("ObjectURef")))) {
                items.append(objects.value(el.attribute(QStringLiteral("ObjectURef"))));
            } else {
                items.append(el);
            }
        } else if (tag == QLatin1String("trackitems") || tag == QLatin1String("clips")) {
            for (QDomNode m = el.firstChild(); !m.isNull(); m = m.nextSibling()) {
                if (!m.isElement())
                    continue;
                QDomElement itemEl = m.toElement();
                if (itemEl.hasAttribute(QStringLiteral("ObjectRef"))) {
                    const QString ref = itemEl.attribute(QStringLiteral("ObjectRef"));
                    if (objects.contains(ref))
                        items.append(objects.value(ref));
                } else if (itemEl.hasAttribute(QStringLiteral("ObjectURef"))) {
                    const QString ref = itemEl.attribute(QStringLiteral("ObjectURef"));
                    if (objects.contains(ref))
                        items.append(objects.value(ref));
                } else {
                    items.append(itemEl);
                }
            }
        }
    }

    for (const QDomElement &itemEl : items) {
        const QString itemType = itemEl.firstChildElement(QStringLiteral("TrackItemType")).text();
        if (itemType == QLatin1String("2") || itemType == QLatin1String("3")
            || itemType.compare(QLatin1String("gap"), Qt::CaseInsensitive) == 0) {
            continue;
        }
        if (itemEl.tagName().compare(QLatin1String("transitionitem"), Qt::CaseInsensitive) == 0)
            continue;

        const qint64 startTicks = itemEl.firstChildElement(QStringLiteral("Start")).isNull()
            ? itemEl.firstChildElement(QStringLiteral("start")).text().toLongLong()
            : itemEl.firstChildElement(QStringLiteral("Start")).text().toLongLong();

        const qint64 endTicks = itemEl.firstChildElement(QStringLiteral("End")).isNull()
            ? itemEl.firstChildElement(QStringLiteral("end")).text().toLongLong()
            : itemEl.firstChildElement(QStringLiteral("End")).text().toLongLong();

        const qint64 inTicks = itemEl.firstChildElement(QStringLiteral("In")).isNull()
            ? itemEl.firstChildElement(QStringLiteral("in")).text().toLongLong()
            : itemEl.firstChildElement(QStringLiteral("In")).text().toLongLong();

        const qint64 outTicks = itemEl.firstChildElement(QStringLiteral("Out")).isNull()
            ? itemEl.firstChildElement(QStringLiteral("out")).text().toLongLong()
            : itemEl.firstChildElement(QStringLiteral("Out")).text().toLongLong();

        if (endTicks <= startTicks)
            continue;

        QString clipName = itemEl.firstChildElement(QStringLiteral("Name")).isNull()
            ? itemEl.firstChildElement(QStringLiteral("name")).text()
            : itemEl.firstChildElement(QStringLiteral("Name")).text();

        const QString rawPath = resolveMediaPathFromElement(itemEl, objects, &clipName);
        const QString normPath = normalizeMediaPath(rawPath);
        const QString resolvedPath = resolveMedia(normPath, sourceDir);

        const bool isAdjustment = itemEl.firstChildElement(QStringLiteral("IsAdjustmentLayer")).text() == QLatin1String("true")
            || clipName.contains(QStringLiteral("adjustment"), Qt::CaseInsensitive);

        const ClipType clipType = detectClipType(resolvedPath, clipName, isAudio, isAdjustment);

        Clip clip;
        clip.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        clip.type = clipType;
        clip.name = clipName.isEmpty() ? (resolvedPath.isEmpty() ? QObject::tr("Clip") : QFileInfo(resolvedPath).fileName()) : clipName;
        clip.path = resolvedPath;
        clip.timelineStart = ticksToTimeUs(startTicks, fps);
        clip.timelineDuration = ticksToTimeUs(endTicks - startTicks, fps);
        clip.srcIn = ticksToTimeUs(inTicks, fps);
        clip.srcOut = ticksToTimeUs(outTicks, fps);

        if (clip.timelineDuration <= 0)
            clip.timelineDuration = 1000000;
        if (clip.srcOut <= clip.srcIn)
            clip.srcOut = clip.srcIn + clip.timelineDuration;

        // Retime / Speed
        const QString speedStr = itemEl.firstChildElement(QStringLiteral("Speed")).text();
        if (!speedStr.isEmpty()) {
            const double spd = speedStr.toDouble();
            if (spd > 0.01 && spd <= 10.0) {
                clip.speed = spd;
            } else if (spd > 10.0 && spd <= 1000.0) {
                clip.speed = spd / 100.0;
            }
        }
        const QString revStr = itemEl.firstChildElement(QStringLiteral("Reverse")).text();
        if (revStr == QLatin1String("true") || revStr == QLatin1String("1"))
            clip.reverse = true;

        // If visual, assign default layout
        if (clipType != ClipType::Audio) {
            clip.transformX = {};
            clip.transformY = {};
            clip.transformW = {};
            clip.transformH = {};
            clip.rotation = {};
            clip.opacity = {};
        }

        // Link/create MediaAsset
        if (!resolvedPath.isEmpty()) {
            QString assetId;
            for (auto it = project.assets().constBegin(); it != project.assets().constEnd(); ++it) {
                if (it.value().path == resolvedPath) {
                    assetId = it.key();
                    break;
                }
            }
            if (assetId.isEmpty()) {
                assetId = QUuid::createUuid().toString(QUuid::WithoutBraces);
                MediaAsset asset;
                asset.id = assetId;
                asset.name = clip.name;
                asset.path = resolvedPath;
                project.assets().insert(assetId, asset);
                project.assetOrder().append(assetId);
            }
            clip.assetId = assetId;
        }

        track.clips.append(clip);
    }
}

void collectTracks(const QDomDocument &doc,
                   const QHash<QString, QDomElement> &objects,
                   QList<QDomElement> &outVideoTracks,
                   QList<QDomElement> &outAudioTracks)
{
    // 1. Check Premiere TrackGroup hierarchy
    QSet<QString> visitedGroupIds;
    QSet<QString> visitedTrackIds;

    // First prefer TrackGroups referenced from the primary Sequence
    QDomElement seqElem = doc.elementsByTagName(QStringLiteral("Sequence")).item(0).toElement();
    if (seqElem.isNull())
        seqElem = doc.elementsByTagName(QStringLiteral("sequence")).item(0).toElement();

    QList<QDomElement> groupElements;
    if (!seqElem.isNull()) {
        const QDomElement trackGroupsElem = seqElem.firstChildElement(QStringLiteral("TrackGroups"));
        if (!trackGroupsElem.isNull()) {
            for (QDomNode n = trackGroupsElem.firstChild(); !n.isNull(); n = n.nextSibling()) {
                if (!n.isElement())
                    continue;
                QDomElement gEl = n.toElement();
                if (gEl.hasAttribute(QStringLiteral("ObjectRef")) && objects.contains(gEl.attribute(QStringLiteral("ObjectRef"))))
                    gEl = objects.value(gEl.attribute(QStringLiteral("ObjectRef")));
                else if (gEl.hasAttribute(QStringLiteral("ObjectURef")) && objects.contains(gEl.attribute(QStringLiteral("ObjectURef"))))
                    gEl = objects.value(gEl.attribute(QStringLiteral("ObjectURef")));
                const QString gId = gEl.attribute(QStringLiteral("ObjectID"));
                if (!gId.isEmpty())
                    visitedGroupIds.insert(gId);
                groupElements.append(gEl);
            }
        }
    }

    // If no TrackGroups found in Sequence, fallback to doc.elementsByTagName
    if (groupElements.isEmpty()) {
        const QDomNodeList groupNodes = doc.elementsByTagName(QStringLiteral("TrackGroup"));
        for (int i = 0; i < groupNodes.size(); ++i) {
            QDomElement groupEl = groupNodes.at(i).toElement();
            if (groupEl.hasAttribute(QStringLiteral("ObjectRef")) && objects.contains(groupEl.attribute(QStringLiteral("ObjectRef"))))
                groupEl = objects.value(groupEl.attribute(QStringLiteral("ObjectRef")));
            else if (groupEl.hasAttribute(QStringLiteral("ObjectURef")) && objects.contains(groupEl.attribute(QStringLiteral("ObjectURef"))))
                groupEl = objects.value(groupEl.attribute(QStringLiteral("ObjectURef")));

            const QString groupId = groupEl.attribute(QStringLiteral("ObjectID"));
            if (!groupId.isEmpty() && visitedGroupIds.contains(groupId))
                continue;
            if (!groupId.isEmpty())
                visitedGroupIds.insert(groupId);
            groupElements.append(groupEl);
        }
    }

    for (const QDomElement &groupEl : groupElements) {
        const QString mediaType = groupEl.firstChildElement(QStringLiteral("MediaType")).text();
        const QString groupType = groupEl.firstChildElement(QStringLiteral("TrackGroupType")).text();
        const QString groupName = groupEl.firstChildElement(QStringLiteral("Name")).text();

        // Premiere Video MediaType: 228cda6f-b111-49a9-9904-32ab2e6577b5
        // Premiere Audio MediaType: c8ee8ef5-0814-49ee-b4c6-be11311ff12e
        const bool isAudio = mediaType.contains(QLatin1String("c8ee8ef5"), Qt::CaseInsensitive)
            || mediaType.contains(QLatin1String("audio"), Qt::CaseInsensitive)
            || groupType == QLatin1String("1")
            || groupName.contains(QLatin1String("audio"), Qt::CaseInsensitive);

        const QDomElement tracksParent = groupEl.firstChildElement(QStringLiteral("Tracks"));
        if (!tracksParent.isNull()) {
            for (QDomNode n = tracksParent.firstChild(); !n.isNull(); n = n.nextSibling()) {
                if (!n.isElement())
                    continue;
                QDomElement tEl = n.toElement();
                if (tEl.hasAttribute(QStringLiteral("ObjectRef")) && objects.contains(tEl.attribute(QStringLiteral("ObjectRef"))))
                    tEl = objects.value(tEl.attribute(QStringLiteral("ObjectRef")));
                else if (tEl.hasAttribute(QStringLiteral("ObjectURef")) && objects.contains(tEl.attribute(QStringLiteral("ObjectURef"))))
                    tEl = objects.value(tEl.attribute(QStringLiteral("ObjectURef")));

                const QString trackId = tEl.attribute(QStringLiteral("ObjectID"));
                if (!trackId.isEmpty() && visitedTrackIds.contains(trackId))
                    continue;
                if (!trackId.isEmpty())
                    visitedTrackIds.insert(trackId);

                if (isAudio)
                    outAudioTracks.append(tEl);
                else
                    outVideoTracks.append(tEl);
            }
        }
    }

    // 2. Direct VideoTrack / AudioTrack tags
    if (outVideoTracks.isEmpty()) {
        const QDomNodeList vNodes = doc.elementsByTagName(QStringLiteral("VideoTrack"));
        for (int i = 0; i < vNodes.size(); ++i)
            outVideoTracks.append(vNodes.at(i).toElement());
    }
    if (outAudioTracks.isEmpty()) {
        const QDomNodeList aNodes = doc.elementsByTagName(QStringLiteral("AudioTrack"));
        for (int i = 0; i < aNodes.size(); ++i)
            outAudioTracks.append(aNodes.at(i).toElement());
    }

    // 3. FCP XML <video><track> and <audio><track>
    if (outVideoTracks.isEmpty()) {
        const QDomElement vParent = doc.elementsByTagName(QStringLiteral("video")).item(0).toElement();
        if (!vParent.isNull()) {
            const QDomNodeList vTracks = vParent.elementsByTagName(QStringLiteral("track"));
            for (int i = 0; i < vTracks.size(); ++i)
                outVideoTracks.append(vTracks.at(i).toElement());
        }
    }
    if (outAudioTracks.isEmpty()) {
        const QDomElement aParent = doc.elementsByTagName(QStringLiteral("audio")).item(0).toElement();
        if (!aParent.isNull()) {
            const QDomNodeList aTracks = aParent.elementsByTagName(QStringLiteral("track"));
            for (int i = 0; i < aTracks.size(); ++i)
                outAudioTracks.append(aTracks.at(i).toElement());
        }
    }
}

} // namespace

bool isPremiereProject(const QString &filePath)
{
    const QString ext = QFileInfo(filePath).suffix().toLower();
    if (ext == QLatin1String("prproj"))
        return true;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return false;

    const QByteArray header = file.read(256);
    return isPremiereData(header);
}

bool isPremiereData(const QByteArray &data)
{
    if (data.size() < 2)
        return false;

    if (isGzip(data))
        return true;

    const QString text = QString::fromUtf8(data.left(512)).trimmed();
    return text.contains(QLatin1String("<PremiereData"), Qt::CaseInsensitive)
        || text.contains(QLatin1String("<xmeml"), Qt::CaseInsensitive);
}

std::optional<Project> readProject(const QString &filePath, QString *error)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QObject::tr("Cannot open file: %1").arg(filePath);
        return std::nullopt;
    }

    const QByteArray rawData = file.readAll();
    const QString sourceDir = QFileInfo(filePath).absolutePath();
    return readProjectData(rawData, sourceDir, error);
}

std::optional<Project> readProjectData(const QByteArray &data, const QString &sourceDir, QString *error)
{
    if (data.isEmpty()) {
        if (error)
            *error = QObject::tr("File is empty");
        return std::nullopt;
    }

    const QByteArray xmlBytes = decompressGzip(data);
    if (xmlBytes.isEmpty()) {
        if (error)
            *error = QObject::tr("Failed to decompress Premiere project archive");
        return std::nullopt;
    }

    QDomDocument doc;
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    const QDomDocument::ParseResult parseResult = doc.setContent(xmlBytes);
    if (!parseResult) {
        if (error)
            *error = QObject::tr("XML parse error at line %1: %2")
                         .arg(parseResult.errorLine)
                         .arg(parseResult.errorMessage);
        return std::nullopt;
    }
#else
    QString parseErrMsg;
    int parseErrLine = 0;
    int parseErrCol = 0;
    if (!doc.setContent(xmlBytes, &parseErrMsg, &parseErrLine, &parseErrCol)) {
        if (error)
            *error = QObject::tr("XML parse error at line %1: %2").arg(parseErrLine).arg(parseErrMsg);
        return std::nullopt;
    }
#endif

    QHash<QString, QDomElement> objects;
    indexObjects(doc.documentElement(), objects);

    Project project;
    project.setId(QUuid::createUuid().toString(QUuid::WithoutBraces));

    // Project Name
    QString projectName;
    const QDomElement projElem = doc.elementsByTagName(QStringLiteral("Project")).item(0).toElement();
    if (!projElem.isNull()) {
        projectName = projElem.firstChildElement(QStringLiteral("Name")).text();
        if (projectName.isEmpty())
            projectName = projElem.firstChildElement(QStringLiteral("name")).text();
    }
    if (projectName.isEmpty()) {
        const QDomElement xmemlProj = doc.elementsByTagName(QStringLiteral("project")).item(0).toElement();
        if (!xmemlProj.isNull()) {
            projectName = xmemlProj.firstChildElement(QStringLiteral("name")).text();
            if (projectName.isEmpty())
                projectName = xmemlProj.firstChildElement(QStringLiteral("Name")).text();
        }
    }
    if (projectName.isEmpty()) {
        const QDomElement seqEl = doc.elementsByTagName(QStringLiteral("sequence")).item(0).toElement();
        if (!seqEl.isNull()) {
            projectName = seqEl.firstChildElement(QStringLiteral("name")).text();
            if (projectName.isEmpty())
                projectName = seqEl.firstChildElement(QStringLiteral("Name")).text();
        }
    }
    if (projectName.isEmpty()) {
        const QDomElement seqEl = doc.elementsByTagName(QStringLiteral("Sequence")).item(0).toElement();
        if (!seqEl.isNull()) {
            projectName = seqEl.firstChildElement(QStringLiteral("Name")).text();
            if (projectName.isEmpty())
                projectName = seqEl.firstChildElement(QStringLiteral("name")).text();
        }
    }
    if (projectName.isEmpty())
        projectName = QObject::tr("Imported Premiere Project");

    project.setName(projectName);

    // Sequence properties (Width, Height, FPS)
    QDomElement seqElem = doc.elementsByTagName(QStringLiteral("Sequence")).item(0).toElement();
    if (seqElem.isNull())
        seqElem = doc.elementsByTagName(QStringLiteral("sequence")).item(0).toElement();

    int width = 1920;
    int height = 1080;
    int fps = 30;

    if (!seqElem.isNull()) {
        // Dimensions
        QDomElement wElem = seqElem.firstChildElement(QStringLiteral("VideoCanvasWidth"));
        if (wElem.isNull())
            wElem = seqElem.firstChildElement(QStringLiteral("Width"));
        if (wElem.isNull())
            wElem = seqElem.elementsByTagName(QStringLiteral("width")).item(0).toElement();

        QDomElement hElem = seqElem.firstChildElement(QStringLiteral("VideoCanvasHeight"));
        if (hElem.isNull())
            hElem = seqElem.firstChildElement(QStringLiteral("Height"));
        if (hElem.isNull())
            hElem = seqElem.elementsByTagName(QStringLiteral("height")).item(0).toElement();

        if (!wElem.isNull() && wElem.text().toInt() > 0)
            width = wElem.text().toInt();
        if (!hElem.isNull() && hElem.text().toInt() > 0)
            height = hElem.text().toInt();

        // Timebase / FPS
        QDomElement tbElem = seqElem.firstChildElement(QStringLiteral("Timebase"));
        if (tbElem.isNull())
            tbElem = seqElem.elementsByTagName(QStringLiteral("timebase")).item(0).toElement();

        if (!tbElem.isNull()) {
            const qint64 tb = tbElem.text().toLongLong();
            if (tb > 1000000LL) {
                fps = static_cast<int>(llround(static_cast<double>(kPremiereTicksPerSecond) / static_cast<double>(tb)));
            } else if (tb > 0 && tb <= 240) {
                fps = static_cast<int>(tb);
            }
        }
    }

    project.setResolution(width, height);
    project.setFps(qBound(1, fps, 120));

    // Clear default tracks to build exact tracks from project
    project.tracks().clear();

    QList<QDomElement> vTrackNodes;
    QList<QDomElement> aTrackNodes;
    collectTracks(doc, objects, vTrackNodes, aTrackNodes);

    // 1. Video tracks
    for (int i = 0; i < vTrackNodes.size(); ++i) {
        const QDomElement &vtElem = vTrackNodes.at(i);
        Track track;
        track.type = TrackType::Video;
        const QString name = vtElem.firstChildElement(QStringLiteral("Name")).text();
        track.name = name.isEmpty() ? QObject::tr("V%1").arg(i + 1) : name;

        parseTrackClips(vtElem, track, false, fps, objects, sourceDir, project);
        project.tracks().append(track);
    }

    // 2. Audio tracks
    for (int i = 0; i < aTrackNodes.size(); ++i) {
        const QDomElement &atElem = aTrackNodes.at(i);
        Track track;
        track.type = TrackType::Audio;
        const QString name = atElem.firstChildElement(QStringLiteral("Name")).text();
        track.name = name.isEmpty() ? QObject::tr("A%1").arg(i + 1) : name;

        parseTrackClips(atElem, track, true, fps, objects, sourceDir, project);
        project.tracks().append(track);
    }

    // If no tracks were imported, provide a default timeline
    if (project.tracks().isEmpty()) {
        project.resetToDefaultTimeline();
    }

    return project;
}

} // namespace drift::prproj
