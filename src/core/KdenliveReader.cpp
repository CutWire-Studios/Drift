#include "KdenliveReader.h"

#include "Clip.h"
#include "MediaAsset.h"
#include "Project.h"
#include "Track.h"

#include <QColor>
#include <QDir>
#include <QDomDocument>
#include <QDomElement>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QUrl>
#include <QUuid>
#include <QtMath>
#include <cstring>
#include <zlib.h>

namespace drift::kdenlive {
namespace {

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

TimeUs parseMltTime(const QString &rawStr, double fps)
{
    if (rawStr.isEmpty())
        return 0;

    const double effectiveFps = fps > 0.01 ? fps : 30.0;
    QString s = rawStr.trimmed();

    // Timecode format: hh:mm:ss.ms or hh:mm:ss:ff or hh:mm:ss,ms
    if (s.contains(QLatin1Char(':'))) {
        const QStringList parts = s.split(QLatin1Char(':'));
        if (parts.size() >= 4) {
            // hh:mm:ss:ff
            const double h = parts[0].toDouble();
            const double m = parts[1].toDouble();
            const double sec = parts[2].toDouble();
            const double f = parts[3].toDouble();
            const double totalSec = h * 3600.0 + m * 60.0 + sec + (f / effectiveFps);
            return static_cast<TimeUs>(llround(totalSec * 1000000.0));
        } else if (parts.size() == 3) {
            // hh:mm:ss.ms or hh:mm:ss,ms
            const double h = parts[0].toDouble();
            const double m = parts[1].toDouble();
            QString secStr = parts[2];
            secStr.replace(QLatin1Char(','), QLatin1Char('.'));
            const double sec = secStr.toDouble();
            const double totalSec = h * 3600.0 + m * 60.0 + sec;
            return static_cast<TimeUs>(llround(totalSec * 1000000.0));
        } else if (parts.size() == 2) {
            // mm:ss
            const double m = parts[0].toDouble();
            QString secStr = parts[1];
            secStr.replace(QLatin1Char(','), QLatin1Char('.'));
            const double sec = secStr.toDouble();
            return static_cast<TimeUs>(llround((m * 60.0 + sec) * 1000000.0));
        }
    }

    // Seconds with suffix: "12.5s"
    if (s.endsWith(QLatin1Char('s'), Qt::CaseInsensitive)) {
        s.chop(1);
        return static_cast<TimeUs>(llround(s.toDouble() * 1000000.0));
    }

    // Floating-point seconds: contains decimal separator
    if (s.contains(QLatin1Char('.')) || s.contains(QLatin1Char(','))) {
        s.replace(QLatin1Char(','), QLatin1Char('.'));
        return static_cast<TimeUs>(llround(s.toDouble() * 1000000.0));
    }

    // Integer frames
    bool ok = false;
    const qint64 frames = s.toLongLong(&ok);
    if (ok && frames >= 0) {
        return static_cast<TimeUs>(llround((static_cast<double>(frames) * 1000000.0) / effectiveFps));
    }

    return 0;
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

    // Check relative to project sourceDir
    const QString relativeToSource = QDir(sourceDir).filePath(normalizedPath);
    if (QFileInfo::exists(relativeToSource))
        return QFileInfo(relativeToSource).canonicalFilePath();

    // Check fileName directly in sourceDir
    const QString fileName = QFileInfo(normalizedPath).fileName();
    const QString inSourceDir = QDir(sourceDir).filePath(fileName);
    if (QFileInfo::exists(inSourceDir))
        return QFileInfo(inSourceDir).canonicalFilePath();

    return normalizedPath;
}

struct ParsedTitleInfo
{
    QString text;
    QString fontFamily = QStringLiteral("Inter");
    int fontSize = 64;
    int fontWeight = 700;
    QColor color = Qt::white;
};

ParsedTitleInfo parseKdenliveTitleXml(const QString &titleXml)
{
    ParsedTitleInfo info;
    QDomDocument doc;
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    if (!doc.setContent(titleXml))
        return info;
#else
    if (!doc.setContent(titleXml, nullptr))
        return info;
#endif

    const QDomNodeList items = doc.elementsByTagName(QStringLiteral("item"));
    for (int i = 0; i < items.size(); ++i) {
        const QDomElement itemEl = items.at(i).toElement();
        const QString itemType = itemEl.attribute(QStringLiteral("type"));
        if (itemType == QLatin1String("QGraphicsTextItem") || itemType.isEmpty()) {
            const QDomElement contentEl = itemEl.firstChildElement(QStringLiteral("content"));
            if (!contentEl.isNull()) {
                info.text = contentEl.text();
                if (contentEl.hasAttribute(QStringLiteral("font")))
                    info.fontFamily = contentEl.attribute(QStringLiteral("font"));
                if (contentEl.hasAttribute(QStringLiteral("font-size")))
                    info.fontSize = contentEl.attribute(QStringLiteral("font-size")).toInt();
                if (contentEl.hasAttribute(QStringLiteral("font-weight"))) {
                    const int w = contentEl.attribute(QStringLiteral("font-weight")).toInt();
                    info.fontWeight = (w >= 75 || w >= 600) ? 700 : 400;
                }
                if (contentEl.hasAttribute(QStringLiteral("font-color"))) {
                    const QString colorStr = contentEl.attribute(QStringLiteral("font-color"));
                    const QStringList parts = colorStr.split(QLatin1Char(','));
                    if (parts.size() >= 3) {
                        const int r = parts[0].toInt();
                        const int g = parts[1].toInt();
                        const int b = parts[2].toInt();
                        const int a = parts.size() >= 4 ? parts[3].toInt() : 255;
                        info.color = QColor(r, g, b, a);
                    } else if (QColor::isValidColorName(colorStr)) {
                        info.color = QColor::fromString(colorStr);
                    }
                }
                break;
            }
        }
    }

    if (info.text.isEmpty()) {
        const QDomElement textEl = doc.firstChildElement(QStringLiteral("kdenlivetitle")).firstChildElement(QStringLiteral("text"));
        if (!textEl.isNull())
            info.text = textEl.text();
    }

    return info;
}

struct MltProducer
{
    QString id;
    QString resource;
    QString name;
    QString folderId;
    TimeUs durationUs = 0;
    int videoIndex = 0;
    int audioIndex = 0;
    bool isAudioOnly = false;
    bool isImage = false;
    bool isText = false;
    ParsedTitleInfo titleInfo;
    QString assetId;
};

} // namespace

bool isKdenliveProject(const QString &filePath)
{
    const QString ext = QFileInfo(filePath).suffix().toLower();
    if (ext == QLatin1String("kdenlive") || ext == QLatin1String("mlt"))
        return true;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return false;

    const QByteArray header = file.read(4096);
    return isKdenliveData(header);
}

bool isKdenliveData(const QByteArray &data)
{
    if (data.isEmpty())
        return false;

    if (isGzip(data)) {
        const QByteArray decompressed = decompressGzip(data.left(4096));
        return decompressed.contains("<mlt");
    }

    return data.contains("<mlt");
}

std::optional<Project> readProject(const QString &filePath, QString *error)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QObject::tr("Could not open file: %1").arg(filePath);
        return std::nullopt;
    }

    const QByteArray data = file.readAll();
    const QString sourceDir = QFileInfo(filePath).canonicalPath();
    return readProjectData(data, sourceDir, error);
}

std::optional<Project> readProjectData(const QByteArray &data, const QString &sourceDir, QString *error)
{
    const QByteArray xmlData = isGzip(data) ? decompressGzip(data) : data;
    if (xmlData.isEmpty()) {
        if (error)
            *error = QObject::tr("Invalid or corrupt MLT / Kdenlive project");
        return std::nullopt;
    }

    QDomDocument doc;
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    if (!doc.setContent(xmlData)) {
#else
    if (!doc.setContent(xmlData, nullptr)) {
#endif
        if (error)
            *error = QObject::tr("Failed to parse MLT XML document");
        return std::nullopt;
    }

    const QDomElement root = doc.documentElement();
    if (root.tagName() != QLatin1String("mlt")) {
        if (error)
            *error = QObject::tr("Root element is not <mlt>");
        return std::nullopt;
    }

    Project project;

    // 1. Profile (Resolution and Frame Rate)
    double fps = 30.0;
    int width = 1920;
    int height = 1080;

    const QDomElement profileEl = root.firstChildElement(QStringLiteral("profile"));
    if (!profileEl.isNull()) {
        if (profileEl.hasAttribute(QStringLiteral("width")) && profileEl.attribute(QStringLiteral("width")).toInt() > 0)
            width = profileEl.attribute(QStringLiteral("width")).toInt();
        if (profileEl.hasAttribute(QStringLiteral("height")) && profileEl.attribute(QStringLiteral("height")).toInt() > 0)
            height = profileEl.attribute(QStringLiteral("height")).toInt();

        double num = profileEl.attribute(QStringLiteral("frame_rate_num")).toDouble();
        double den = profileEl.attribute(QStringLiteral("frame_rate_den")).toDouble();
        if (num > 0 && den > 0) {
            fps = num / den;
        }
    }

    project.setResolution(width, height);
    project.setFps(qMax(1, qRound(fps)));
    const TimeUs frameUs = static_cast<TimeUs>(llround(1000000.0 / (fps > 0.0 ? fps : 30.0)));

    // Project Name
    QString projectName;
    for (auto el = root.firstChildElement(QStringLiteral("property")); !el.isNull(); el = el.nextSiblingElement(QStringLiteral("property"))) {
        const QString pName = el.attribute(QStringLiteral("name"));
        if (pName == QLatin1String("kdenlive:docproperties.documentid") && projectName.isEmpty()) {
            projectName = el.text();
        }
    }
    if (!projectName.isEmpty())
        project.setName(projectName);

    // 2. Parse Bin Folders
    // Format 1: kdenlive:docproperties.folders="1:B-Roll,2:Audio"
    // Format 2: kdenlive:folder.1.name="B-Roll", kdenlive:folder.1.parent="0"
    QMap<QString, QString> folderIdToUuid;
    QMap<QString, QString> folderIdToName;
    QMap<QString, QString> folderIdToParent;

    const auto inspectFolderProperty = [&](const QString &pName, const QString &val) {
        if (pName == QLatin1String("kdenlive:docproperties.folders")) {
            const QStringList pairs = val.split(QLatin1Char(','), Qt::SkipEmptyParts);
            for (const QString &pair : pairs) {
                const int colon = pair.indexOf(QLatin1Char(':'));
                if (colon > 0) {
                    const QString fid = pair.left(colon).trimmed();
                    const QString fname = pair.mid(colon + 1).trimmed();
                    folderIdToName.insert(fid, fname);
                }
            }
        } else if (pName.startsWith(QLatin1String("kdenlive:folder."))) {
            const QString sub = pName.mid(16); // after "kdenlive:folder."
            const int dot = sub.indexOf(QLatin1Char('.'));
            if (dot > 0) {
                const QString fid = sub.left(dot);
                const QString attr = sub.mid(dot + 1);
                if (attr == QLatin1String("name"))
                    folderIdToName.insert(fid, val);
                else if (attr == QLatin1String("parent"))
                    folderIdToParent.insert(fid, val);
            }
        }
    };

    for (auto el = root.firstChildElement(QStringLiteral("property")); !el.isNull(); el = el.nextSiblingElement(QStringLiteral("property"))) {
        inspectFolderProperty(el.attribute(QStringLiteral("name")), el.text());
    }

    // Also check properties inside main_bin playlist
    for (auto pl = root.firstChildElement(QStringLiteral("playlist")); !pl.isNull(); pl = pl.nextSiblingElement(QStringLiteral("playlist"))) {
        const QString plId = pl.attribute(QStringLiteral("id"));
        if (plId == QLatin1String("main_bin") || plId == QLatin1String("main bin") || plId == QLatin1String("bin")) {
            for (auto el = pl.firstChildElement(QStringLiteral("property")); !el.isNull(); el = el.nextSiblingElement(QStringLiteral("property"))) {
                inspectFolderProperty(el.attribute(QStringLiteral("name")), el.text());
            }
        }
    }

    for (auto it = folderIdToName.constBegin(); it != folderIdToName.constEnd(); ++it) {
        const QString fid = it.key();
        const QString fname = it.value();
        const QString uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
        folderIdToUuid.insert(fid, uuid);

        BinFolder folder;
        folder.id = uuid;
        folder.name = fname;
        project.addBinFolder(folder);
    }

    // Set parent relationships for nested bin folders
    for (auto it = folderIdToParent.constBegin(); it != folderIdToParent.constEnd(); ++it) {
        const QString fid = it.key();
        const QString parentFid = it.value();
        if (folderIdToUuid.contains(fid) && folderIdToUuid.contains(parentFid)) {
            const QString folderUuid = folderIdToUuid.value(fid);
            const QString parentUuid = folderIdToUuid.value(parentFid);
            auto fIt = project.binFolders().find(folderUuid);
            if (fIt != project.binFolders().end()) {
                fIt->parentId = parentUuid;
            }
        }
    }

    // 3. Scan Producers and Chains
    QMap<QString, MltProducer> producers;

    const auto processProducerElement = [&](const QDomElement &pEl) {
        const QString id = pEl.attribute(QStringLiteral("id"));
        if (id.isEmpty())
            return;

        // Skip internal/special producers like "black_track"
        if (id == QLatin1String("black_track") || id == QLatin1String("black"))
            return;

        MltProducer prod;
        prod.id = id;

        // In / out duration
        const QString inStr = pEl.attribute(QStringLiteral("in"));
        const QString outStr = pEl.attribute(QStringLiteral("out"));
        const QString lengthStr = pEl.attribute(QStringLiteral("length"));

        if (!outStr.isEmpty()) {
            const TimeUs inUs = parseMltTime(inStr, fps);
            const TimeUs outUs = parseMltTime(outStr, fps);
            prod.durationUs = (outUs >= inUs) ? (outUs - inUs + frameUs) : 5000000LL;
        } else if (!lengthStr.isEmpty()) {
            prod.durationUs = parseMltTime(lengthStr, fps);
        } else {
            prod.durationUs = 5000000LL;
        }

        QString titleXml;
        QString caption;

        for (auto p = pEl.firstChildElement(QStringLiteral("property")); !p.isNull(); p = p.nextSiblingElement(QStringLiteral("property"))) {
            const QString key = p.attribute(QStringLiteral("name"));
            const QString val = p.text();

            if (key == QLatin1String("resource")) {
                prod.resource = val;
            } else if (key == QLatin1String("kdenlive:clipname") || key == QLatin1String("shotcut:caption")) {
                prod.name = val;
            } else if (key == QLatin1String("kdenlive:folderid")) {
                prod.folderId = val;
            } else if (key == QLatin1String("video_index")) {
                prod.videoIndex = val.toInt();
            } else if (key == QLatin1String("audio_index")) {
                prod.audioIndex = val.toInt();
            } else if (key == QLatin1String("kdenlive:kdenlivetitle")) {
                titleXml = val;
            } else if (key == QLatin1String("caption") && prod.name.isEmpty()) {
                prod.name = val;
            }
        }

        // Check if title clip
        if (!titleXml.isEmpty()) {
            prod.isText = true;
            prod.titleInfo = parseKdenliveTitleXml(titleXml);
            if (prod.name.isEmpty())
                prod.name = prod.titleInfo.text.left(24);
        } else if (prod.resource.endsWith(QLatin1String(".kdenlivetitle"), Qt::CaseInsensitive)) {
            prod.isText = true;
            const QString titlePath = resolveMedia(normalizeMediaPath(prod.resource), sourceDir);
            QFile tFile(titlePath);
            if (tFile.open(QIODevice::ReadOnly)) {
                prod.titleInfo = parseKdenliveTitleXml(QString::fromUtf8(tFile.readAll()));
            }
            if (prod.name.isEmpty())
                prod.name = prod.titleInfo.text.left(24);
        }

        // Normalize resource path if media file
        if (!prod.resource.isEmpty() && !prod.isText && !prod.resource.startsWith(QLatin1Char('#'))) {
            const QString norm = normalizeMediaPath(prod.resource);
            prod.resource = resolveMedia(norm, sourceDir);
            const QString ext = QFileInfo(prod.resource).suffix().toLower();

            if (prod.videoIndex == -1
                || ext == QLatin1String("mp3") || ext == QLatin1String("wav") || ext == QLatin1String("aac")
                || ext == QLatin1String("m4a") || ext == QLatin1String("flac") || ext == QLatin1String("ogg")) {
                prod.isAudioOnly = true;
            } else if (imageExtensions().contains(ext)) {
                prod.isImage = true;
            }

            if (prod.name.isEmpty())
                prod.name = QFileInfo(prod.resource).fileName();

            // Register in project assets
            MediaAsset asset;
            asset.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            asset.name = prod.name;
            asset.path = prod.resource;
            asset.durationUs = prod.durationUs;
            if (folderIdToUuid.contains(prod.folderId))
                asset.folderId = folderIdToUuid.value(prod.folderId);

            if (prod.isAudioOnly)
                asset.kind = MediaKind::Audio;
            else if (prod.isImage)
                asset.kind = MediaKind::Image;
            else
                asset.kind = MediaKind::Video;

            project.addAsset(asset);
            prod.assetId = asset.id;
        }

        producers.insert(id, prod);
    };

    for (auto el = root.firstChildElement(QStringLiteral("producer")); !el.isNull(); el = el.nextSiblingElement(QStringLiteral("producer"))) {
        processProducerElement(el);
    }
    for (auto el = root.firstChildElement(QStringLiteral("chain")); !el.isNull(); el = el.nextSiblingElement(QStringLiteral("chain"))) {
        processProducerElement(el);
    }

    // 4. Discover Sequence Playlists
    // Check if there is a main tractor with a multitrack container
    QStringList trackPlaylistIds;
    for (auto tr = root.firstChildElement(QStringLiteral("tractor")); !tr.isNull(); tr = tr.nextSiblingElement(QStringLiteral("tractor"))) {
        const QDomElement mt = tr.firstChildElement(QStringLiteral("multitrack"));
        if (!mt.isNull()) {
            for (auto t = mt.firstChildElement(QStringLiteral("track")); !t.isNull(); t = t.nextSiblingElement(QStringLiteral("track"))) {
                const QString pid = t.attribute(QStringLiteral("producer"));
                if (!pid.isEmpty() && pid != QLatin1String("black_track") && pid != QLatin1String("black"))
                    trackPlaylistIds.append(pid);
            }
            if (!trackPlaylistIds.isEmpty())
                break;
        }
    }

    // If no multitrack tractor, discover all playlists that are not the project bin
    QMap<QString, QDomElement> playlistsById;
    for (auto pl = root.firstChildElement(QStringLiteral("playlist")); !pl.isNull(); pl = pl.nextSiblingElement(QStringLiteral("playlist"))) {
        const QString pid = pl.attribute(QStringLiteral("id"));
        if (!pid.isEmpty())
            playlistsById.insert(pid, pl);
    }

    if (trackPlaylistIds.isEmpty()) {
        for (auto it = playlistsById.constBegin(); it != playlistsById.constEnd(); ++it) {
            const QString pid = it.key();
            if (pid != QLatin1String("main_bin") && pid != QLatin1String("main bin") && pid != QLatin1String("bin")
                && pid != QLatin1String("timeline_preview")) {
                trackPlaylistIds.append(pid);
            }
        }
    }

    // 5. Construct Project Tracks and Clips
    project.tracks().clear();

    for (int tIdx = 0; tIdx < trackPlaylistIds.size(); ++tIdx) {
        const QString pid = trackPlaylistIds.at(tIdx);
        if (!playlistsById.contains(pid))
            continue;

        const QDomElement plEl = playlistsById.value(pid);

        // Track Name and properties
        QString trackName;
        bool isAudioTrack = false;

        for (auto p = plEl.firstChildElement(QStringLiteral("property")); !p.isNull(); p = p.nextSiblingElement(QStringLiteral("property"))) {
            const QString key = p.attribute(QStringLiteral("name"));
            const QString val = p.text();
            if (key == QLatin1String("kdenlive:track_name") || key == QLatin1String("shotcut:name")) {
                trackName = val;
            } else if (key == QLatin1String("kdenlive:audio_track") && val == QLatin1String("1")) {
                isAudioTrack = true;
            }
        }

        // Build clips for this playlist
        QList<Clip> clips;
        TimeUs currentTimelineUs = 0;
        bool hasVideoClips = false;

        for (auto child = plEl.firstChildElement(); !child.isNull(); child = child.nextSiblingElement()) {
            const QString tag = child.tagName();

            if (tag == QLatin1String("blank")) {
                const QString lenStr = child.attribute(QStringLiteral("length"));
                const TimeUs blankDur = parseMltTime(lenStr, fps);
                currentTimelineUs += blankDur;
            } else if (tag == QLatin1String("entry")) {
                const QString prodId = child.attribute(QStringLiteral("producer"));
                if (!producers.contains(prodId))
                    continue;

                const MltProducer &prod = producers.value(prodId);

                TimeUs inUs = 0;
                TimeUs outUs = 0;

                const bool hasIn = child.hasAttribute(QStringLiteral("in"));
                const bool hasOut = child.hasAttribute(QStringLiteral("out"));
                const bool hasLength = child.hasAttribute(QStringLiteral("length"));

                if (hasIn)
                    inUs = parseMltTime(child.attribute(QStringLiteral("in")), fps);
                if (hasOut)
                    outUs = parseMltTime(child.attribute(QStringLiteral("out")), fps);

                TimeUs durationUs = 0;
                if (hasLength) {
                    durationUs = parseMltTime(child.attribute(QStringLiteral("length")), fps);
                } else if (hasOut && outUs >= inUs) {
                    durationUs = (outUs - inUs) + frameUs;
                } else if (prod.durationUs > 0) {
                    durationUs = prod.durationUs;
                } else {
                    durationUs = 5000000LL;
                }

                Clip clip;
                clip.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
                clip.name = prod.name;
                clip.timelineStart = currentTimelineUs;
                clip.timelineDuration = durationUs;
                clip.srcIn = inUs;
                clip.srcOut = inUs + durationUs;

                if (prod.isText) {
                    clip.type = ClipType::Text;
                    clip.textContent = prod.titleInfo.text;
                    clip.textStyle.fontFamily = prod.titleInfo.fontFamily;
                    clip.textStyle.pixelSize = prod.titleInfo.fontSize;
                    clip.textStyle.fontWeight = prod.titleInfo.fontWeight;
                    drift::setSolidFill(clip.textStyle, prod.titleInfo.color);

                    clip.transformX.setKeyframe(0, 0);
                    clip.transformY.setKeyframe(0, height * 0.35);
                    clip.transformW.setKeyframe(0, width);
                    clip.transformH.setKeyframe(0, height * 0.30);
                    hasVideoClips = true;
                } else if (prod.isAudioOnly) {
                    clip.type = ClipType::Audio;
                    clip.assetId = prod.assetId;
                    clip.path = prod.resource;
                } else {
                    clip.type = prod.isImage ? ClipType::Image : ClipType::Video;
                    clip.assetId = prod.assetId;
                    clip.path = prod.resource;

                    clip.transformX.setKeyframe(0, 0);
                    clip.transformY.setKeyframe(0, 0);
                    clip.transformW.setKeyframe(0, width);
                    clip.transformH.setKeyframe(0, height);
                    hasVideoClips = true;
                }

                clips.append(clip);
                currentTimelineUs += durationUs;
            }
        }

        // If the playlist has clips or is declared as a track
        Track track;
        track.id = QUuid::createUuid().toString(QUuid::WithoutBraces);

        if (isAudioTrack || (!hasVideoClips && !clips.isEmpty())) {
            track.type = TrackType::Audio;
            track.name = trackName.isEmpty() ? QStringLiteral("A%1").arg(project.tracks().size() + 1) : trackName;
        } else {
            track.type = TrackType::Video;
            track.name = trackName.isEmpty() ? QStringLiteral("V%1").arg(project.tracks().size() + 1) : trackName;
        }

        track.clips = clips;
        project.tracks().append(track);
    }

    // If no tracks were found, ensure default tracks
    if (project.tracks().isEmpty()) {
        project.resetToDefaultTimeline();
    }

    project.ensureTrackIds();
    return project;
}

} // namespace drift::kdenlive
