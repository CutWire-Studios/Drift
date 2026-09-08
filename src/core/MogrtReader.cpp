#include "MogrtReader.h"

#include "Clip.h"
#include "MediaAsset.h"
#include "PrprojReader.h"
#include "Project.h"
#include "Track.h"

#include <QColor>
#include <QDir>
#include <QDirIterator>
#include <QDomDocument>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QUuid>
#include <QtMath>
#include <cstring>
#include <zlib.h>

namespace drift::mogrt {
namespace {

constexpr quint32 kZipLocalHeaderMagic = 0x04034b50;
constexpr quint32 kZipCentralDirMagic = 0x02014b50;
constexpr quint32 kZipEOCDMagic = 0x06054b50;

quint16 readU16(const char *p)
{
    const auto *b = reinterpret_cast<const uchar *>(p);
    return static_cast<quint16>(b[0] | (b[1] << 8));
}

quint32 readU32(const char *p)
{
    const auto *b = reinterpret_cast<const uchar *>(p);
    return static_cast<quint32>(b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24));
}

bool safeRelativePath(const QString &path)
{
    if (path.isEmpty() || path.startsWith(QLatin1Char('/')) || path.startsWith(QLatin1Char('\\')))
        return false;
    if (path.contains(QStringLiteral("..")) || path.contains(QLatin1Char(':')))
        return false;
    return true;
}

qint64 findEOCD(QFile &file)
{
    const qint64 fileSize = file.size();
    if (fileSize < 22)
        return -1;

    const qint64 scanLen = qMin(fileSize, static_cast<qint64>(65535 + 22));
    if (!file.seek(fileSize - scanLen))
        return -1;

    const QByteArray buf = file.read(scanLen);
    for (int i = buf.size() - 22; i >= 0; --i) {
        if (static_cast<uchar>(buf[i]) == 0x50
            && static_cast<uchar>(buf[i + 1]) == 0x4b
            && static_cast<uchar>(buf[i + 2]) == 0x05
            && static_cast<uchar>(buf[i + 3]) == 0x06) {
            return fileSize - scanLen + i;
        }
    }
    return -1;
}

QByteArray decompressRawDeflate(const QByteArray &compressed, quint32 uncompressedSize)
{
    z_stream strm;
    std::memset(&strm, 0, sizeof(strm));
    // -MAX_WBITS tells zlib to decode raw deflate without zlib/gzip headers
    if (inflateInit2(&strm, -MAX_WBITS) != Z_OK)
        return {};

    strm.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(compressed.constData()));
    strm.avail_in = static_cast<uInt>(compressed.size());

    QByteArray out;
    const int initialCapacity = uncompressedSize > 0 ? static_cast<int>(uncompressedSize)
                                                     : static_cast<int>(compressed.size() * 3);
    out.resize(initialCapacity);
    strm.next_out = reinterpret_cast<Bytef *>(out.data());
    strm.avail_out = static_cast<uInt>(out.size());

    int ret = inflate(&strm, Z_FINISH);
    if (ret == Z_STREAM_END) {
        out.resize(out.size() - strm.avail_out);
        inflateEnd(&strm);
        return out;
    }

    // Dynamic buffer loop if size estimation didn't match
    inflateEnd(&strm);
    std::memset(&strm, 0, sizeof(strm));
    if (inflateInit2(&strm, -MAX_WBITS) != Z_OK)
        return {};

    strm.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(compressed.constData()));
    strm.avail_in = static_cast<uInt>(compressed.size());

    out.clear();
    char buffer[32768];
    ret = Z_OK;
    while (ret == Z_OK || ret == Z_BUF_ERROR) {
        strm.next_out = reinterpret_cast<Bytef *>(buffer);
        strm.avail_out = sizeof(buffer);
        ret = inflate(&strm, Z_NO_FLUSH);
        const int have = sizeof(buffer) - strm.avail_out;
        if (have > 0)
            out.append(buffer, have);
        if (ret == Z_STREAM_END)
            break;
    }
    inflateEnd(&strm);
    return (ret == Z_STREAM_END || !out.isEmpty()) ? out : QByteArray{};
}

struct ZipEntryInfo
{
    QString path;
    quint16 method = 0;
    quint32 compressedSize = 0;
    quint32 uncompressedSize = 0;
    quint32 localOffset = 0;
    bool isDir = false;
};

QList<ZipEntryInfo> readZipEntries(QFile &file, QString *error)
{
    const qint64 eocdPos = findEOCD(file);
    if (eocdPos < 0) {
        if (error)
            *error = QObject::tr("Not a valid ZIP archive");
        return {};
    }

    if (!file.seek(eocdPos))
        return {};

    const QByteArray eocdData = file.read(22);
    if (eocdData.size() < 22)
        return {};

    const quint16 totalEntries = readU16(eocdData.constData() + 10);
    const quint32 cdOffset = readU32(eocdData.constData() + 16);

    if (!file.seek(cdOffset))
        return {};

    QList<ZipEntryInfo> entries;
    entries.reserve(totalEntries);

    for (int i = 0; i < totalEntries; ++i) {
        const QByteArray cdHeader = file.read(46);
        if (cdHeader.size() < 46)
            break;

        if (readU32(cdHeader.constData()) != kZipCentralDirMagic)
            break;

        ZipEntryInfo entry;
        entry.method = readU16(cdHeader.constData() + 10);
        entry.compressedSize = readU32(cdHeader.constData() + 20);
        entry.uncompressedSize = readU32(cdHeader.constData() + 24);
        const quint16 fileNameLen = readU16(cdHeader.constData() + 28);
        const quint16 extraLen = readU16(cdHeader.constData() + 30);
        const quint16 commentLen = readU16(cdHeader.constData() + 32);
        entry.localOffset = readU32(cdHeader.constData() + 42);

        const QByteArray nameData = file.read(fileNameLen);
        entry.path = QString::fromUtf8(nameData);
        entry.isDir = entry.path.endsWith(QLatin1Char('/')) || entry.path.endsWith(QLatin1Char('\\'));

        // Skip extra and comment bytes in Central Directory
        if (extraLen + commentLen > 0)
            file.seek(file.pos() + extraLen + commentLen);

        if (safeRelativePath(entry.path))
            entries.append(entry);
    }

    return entries;
}

bool extractEntryData(QFile &file, const ZipEntryInfo &entry, QByteArray &outData)
{
    if (entry.isDir)
        return true;

    if (!file.seek(entry.localOffset))
        return false;

    const QByteArray localHeader = file.read(30);
    if (localHeader.size() < 30 || readU32(localHeader.constData()) != kZipLocalHeaderMagic)
        return false;

    const quint16 localNameLen = readU16(localHeader.constData() + 26);
    const quint16 localExtraLen = readU16(localHeader.constData() + 28);

    const qint64 dataOffset = entry.localOffset + 30 + localNameLen + localExtraLen;
    if (!file.seek(dataOffset))
        return false;

    const QByteArray rawData = file.read(entry.compressedSize);
    if (rawData.size() != static_cast<int>(entry.compressedSize))
        return false;

    if (entry.method == 0) {
        // Stored
        outData = rawData;
        return true;
    } else if (entry.method == 8) {
        // Deflated
        outData = decompressRawDeflate(rawData, entry.uncompressedSize);
        return !outData.isEmpty() || entry.uncompressedSize == 0;
    }

    return false;
}

QColor parseColorValue(const QJsonValue &val)
{
    if (val.isString()) {
        const QString str = val.toString();
        if (QColor::isValidColorName(str))
            return QColor::fromString(str);
    } else if (val.isArray()) {
        const QJsonArray arr = val.toArray();
        if (arr.size() >= 3) {
            double r = arr.at(0).toDouble();
            double g = arr.at(1).toDouble();
            double b = arr.at(2).toDouble();
            double a = arr.size() >= 4 ? arr.at(3).toDouble() : 1.0;
            // Scale if in 0.0 - 1.0 range
            if (r <= 1.0 && g <= 1.0 && b <= 1.0) {
                return QColor::fromRgbF(qBound(0.0, r, 1.0), qBound(0.0, g, 1.0),
                                        qBound(0.0, b, 1.0), qBound(0.0, a, 1.0));
            }
            return QColor(qBound(0, static_cast<int>(r), 255),
                          qBound(0, static_cast<int>(g), 255),
                          qBound(0, static_cast<int>(b), 255),
                          qBound(0, static_cast<int>(a), 255));
        }
    }
    return Qt::white;
}

void parseDefinitionJson(const QByteArray &jsonData, MogrtTemplate &tmpl)
{
    const QJsonDocument doc = QJsonDocument::fromJson(jsonData);
    if (!doc.isObject())
        return;

    const QJsonObject root = doc.object();

    // Template Name
    if (root.contains(QStringLiteral("name")))
        tmpl.title = root.value(QStringLiteral("name")).toString();
    else if (root.contains(QStringLiteral("templateTitle")))
        tmpl.title = root.value(QStringLiteral("templateTitle")).toString();
    else if (root.contains(QStringLiteral("title")))
        tmpl.title = root.value(QStringLiteral("title")).toString();

    // Author & Description
    tmpl.author = root.value(QStringLiteral("author")).toString();
    tmpl.description = root.value(QStringLiteral("description")).toString();

    // Sequence / Dimensions
    QJsonObject seqObj = root;
    if (root.value(QStringLiteral("sequence")).isObject())
        seqObj = root.value(QStringLiteral("sequence")).toObject();

    if (seqObj.contains(QStringLiteral("width")) && seqObj.value(QStringLiteral("width")).toInt() > 0)
        tmpl.width = seqObj.value(QStringLiteral("width")).toInt();
    if (seqObj.contains(QStringLiteral("height")) && seqObj.value(QStringLiteral("height")).toInt() > 0)
        tmpl.height = seqObj.value(QStringLiteral("height")).toInt();

    // FPS / Timebase
    if (seqObj.contains(QStringLiteral("fps")) && seqObj.value(QStringLiteral("fps")).toDouble() > 0)
        tmpl.fps = qBound(1, static_cast<int>(seqObj.value(QStringLiteral("fps")).toDouble()), 120);
    else if (seqObj.contains(QStringLiteral("framerate")) && seqObj.value(QStringLiteral("framerate")).toDouble() > 0)
        tmpl.fps = qBound(1, static_cast<int>(seqObj.value(QStringLiteral("framerate")).toDouble()), 120);

    // Duration
    if (seqObj.contains(QStringLiteral("duration"))) {
        const double dur = seqObj.value(QStringLiteral("duration")).toDouble();
        if (dur > 0 && dur < 1000.0) {
            tmpl.durationUs = static_cast<TimeUs>(llround(dur * 1000000.0));
        } else if (dur >= 1000.0) {
            // Frames or ticks or milliseconds
            if (dur < 100000.0) {
                // Frames
                tmpl.durationUs = static_cast<TimeUs>(llround((dur * 1000000.0) / qMax(1, tmpl.fps)));
            } else {
                // Microseconds or ticks
                tmpl.durationUs = static_cast<TimeUs>(dur);
            }
        }
    }

    // Properties / Controls
    QJsonArray propArray;
    if (root.value(QStringLiteral("properties")).isArray())
        propArray = root.value(QStringLiteral("properties")).toArray();
    else if (root.value(QStringLiteral("controls")).isArray())
        propArray = root.value(QStringLiteral("controls")).toArray();

    for (const QJsonValue &val : propArray) {
        if (!val.isObject())
            continue;
        const QJsonObject pObj = val.toObject();
        MogrtProperty prop;
        prop.id = pObj.value(QStringLiteral("id")).toString();
        prop.name = pObj.value(QStringLiteral("name")).toString();
        prop.type = pObj.value(QStringLiteral("type")).toString().toLower();

        QJsonValue valObj = pObj.value(QStringLiteral("value"));
        if (valObj.isUndefined() && pObj.value(QStringLiteral("ui")).isObject())
            valObj = pObj.value(QStringLiteral("ui")).toObject().value(QStringLiteral("text"));

        if (prop.type == QLatin1String("text") || prop.type == QLatin1String("string")) {
            prop.stringValue = valObj.toString();
            if (pObj.contains(QStringLiteral("color")))
                prop.colorValue = parseColorValue(pObj.value(QStringLiteral("color")));
            else if (pObj.contains(QStringLiteral("textColor")))
                prop.colorValue = parseColorValue(pObj.value(QStringLiteral("textColor")));
            else if (pObj.contains(QStringLiteral("fillColor")))
                prop.colorValue = parseColorValue(pObj.value(QStringLiteral("fillColor")));
        } else if (prop.type == QLatin1String("color")) {
            prop.colorValue = parseColorValue(valObj);
        } else if (prop.type == QLatin1String("slider") || prop.type == QLatin1String("number")) {
            prop.numberValue = valObj.toDouble();
        } else if (prop.type == QLatin1String("checkbox") || prop.type == QLatin1String("boolean")) {
            prop.boolValue = valObj.toBool();
        } else {
            prop.stringValue = valObj.toString();
        }

        if (pObj.contains(QStringLiteral("fontFamily"))) {
            prop.fontFamily = pObj.value(QStringLiteral("fontFamily")).toString();
        } else if (pObj.contains(QStringLiteral("font"))) {
            const QJsonValue fVal = pObj.value(QStringLiteral("font"));
            if (fVal.isString())
                prop.fontFamily = fVal.toString();
            else if (fVal.isObject())
                prop.fontFamily = fVal.toObject().value(QStringLiteral("name")).toString();
        }

        if (pObj.contains(QStringLiteral("fontSize")))
            prop.fontSize = pObj.value(QStringLiteral("fontSize")).toInt();
        else if (pObj.contains(QStringLiteral("size")))
            prop.fontSize = pObj.value(QStringLiteral("size")).toInt();

        tmpl.properties.append(prop);
    }
}

void parseManifestXml(const QByteArray &xmlData, MogrtTemplate &tmpl)
{
    QDomDocument doc;
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    if (!doc.setContent(xmlData))
        return;
#else
    if (!doc.setContent(xmlData, nullptr))
        return;
#endif

    const QDomElement root = doc.documentElement();
    if (tmpl.title.isEmpty()) {
        const QString title = root.attribute(QStringLiteral("title"));
        if (!title.isEmpty())
            tmpl.title = title;
        else
            tmpl.title = root.firstChildElement(QStringLiteral("name")).text();
    }

    if (root.hasAttribute(QStringLiteral("width")) && root.attribute(QStringLiteral("width")).toInt() > 0)
        tmpl.width = root.attribute(QStringLiteral("width")).toInt();
    if (root.hasAttribute(QStringLiteral("height")) && root.attribute(QStringLiteral("height")).toInt() > 0)
        tmpl.height = root.attribute(QStringLiteral("height")).toInt();
    if (root.hasAttribute(QStringLiteral("fps")) && root.attribute(QStringLiteral("fps")).toDouble() > 0)
        tmpl.fps = static_cast<int>(root.attribute(QStringLiteral("fps")).toDouble());

    const QDomNodeList propNodes = root.elementsByTagName(QStringLiteral("property"));
    for (int i = 0; i < propNodes.size(); ++i) {
        const QDomElement el = propNodes.at(i).toElement();
        MogrtProperty prop;
        prop.name = el.attribute(QStringLiteral("name"));
        prop.type = el.attribute(QStringLiteral("type")).toLower();
        prop.stringValue = el.attribute(QStringLiteral("value"));
        if (prop.stringValue.isEmpty())
            prop.stringValue = el.text();
        if (el.hasAttribute(QStringLiteral("color")))
            prop.colorValue = QColor::fromString(el.attribute(QStringLiteral("color")));
        if (el.hasAttribute(QStringLiteral("font")))
            prop.fontFamily = el.attribute(QStringLiteral("font"));
        if (el.hasAttribute(QStringLiteral("size")))
            prop.fontSize = el.attribute(QStringLiteral("size")).toInt();
        tmpl.properties.append(prop);
    }
}

bool isMediaExtension(const QString &ext)
{
    static const QSet<QString> kExtensions = {
        QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
        QStringLiteral("webp"), QStringLiteral("svg"), QStringLiteral("bmp"),
        QStringLiteral("mp4"), QStringLiteral("mov"), QStringLiteral("mkv"),
        QStringLiteral("webm"), QStringLiteral("avi"),
        QStringLiteral("mp3"), QStringLiteral("wav"), QStringLiteral("aac"),
        QStringLiteral("m4a"), QStringLiteral("flac"), QStringLiteral("ogg")
    };
    return kExtensions.contains(ext.toLower());
}

} // namespace

bool isMogrtFile(const QString &filePath)
{
    const QString ext = QFileInfo(filePath).suffix().toLower();
    if (ext == QLatin1String("mogrt"))
        return true;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return false;

    const QByteArray magic = file.read(4);
    return isMogrtData(magic);
}

bool isMogrtData(const QByteArray &data)
{
    return data.size() >= 4 && readU32(data.constData()) == kZipLocalHeaderMagic;
}

std::optional<MogrtTemplate> readTemplate(const QString &filePath,
                                         const QString &destDir,
                                         QString *error)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QObject::tr("Cannot open file: %1").arg(filePath);
        return std::nullopt;
    }

    const QList<ZipEntryInfo> entries = readZipEntries(file, error);
    if (entries.isEmpty()) {
        if (error && error->isEmpty())
            *error = QObject::tr("MOGRT archive is empty or invalid");
        return std::nullopt;
    }

    QString extractRoot = destDir;
    if (extractRoot.isEmpty()) {
        extractRoot = QDir::temp().filePath(QStringLiteral("drift_mogrt_%1")
                                            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    }
    QDir().mkpath(extractRoot);

    MogrtTemplate tmpl;
    tmpl.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    tmpl.extractDir = extractRoot;
    tmpl.title = QFileInfo(filePath).completeBaseName();

    QByteArray definitionJsonData;
    QByteArray manifestXmlData;

    for (const ZipEntryInfo &entry : entries) {
        if (entry.isDir) {
            QDir(extractRoot).mkpath(entry.path);
            continue;
        }

        const QString targetPath = QDir(extractRoot).filePath(entry.path);
        QFileInfo(targetPath).dir().mkpath(QStringLiteral("."));

        QByteArray data;
        if (!extractEntryData(file, entry, data))
            continue;

        QFile outFile(targetPath);
        if (outFile.open(QIODevice::WriteOnly)) {
            outFile.write(data);
            outFile.close();
        }

        const QString fileName = QFileInfo(entry.path).fileName();
        const QString ext = QFileInfo(entry.path).suffix().toLower();

        if (fileName.compare(QStringLiteral("thumb.png"), Qt::CaseInsensitive) == 0
            || fileName.compare(QStringLiteral("thumbnail.png"), Qt::CaseInsensitive) == 0) {
            tmpl.thumbnailPath = targetPath;
        } else if (fileName.compare(QStringLiteral("definition.json"), Qt::CaseInsensitive) == 0) {
            definitionJsonData = data;
        } else if (fileName.compare(QStringLiteral("manifest.xml"), Qt::CaseInsensitive) == 0) {
            manifestXmlData = data;
        } else if (ext == QLatin1String("prproj")) {
            tmpl.prprojFilePath = targetPath;
        } else if (isMediaExtension(ext)) {
            tmpl.assetPaths.append(targetPath);
        }
    }

    // Parse metadata
    if (!definitionJsonData.isEmpty())
        parseDefinitionJson(definitionJsonData, tmpl);
    else if (!manifestXmlData.isEmpty())
        parseManifestXml(manifestXmlData, tmpl);

    if (tmpl.title.isEmpty())
        tmpl.title = QFileInfo(filePath).completeBaseName();

    return tmpl;
}

bool applyTemplateToProject(const MogrtTemplate &tmpl,
                            Project &project,
                            TimeUs insertTimeUs,
                            QString *error)
{
    // Check if the project currently has any clips
    bool hasExistingClips = false;
    for (const Track &t : project.tracks()) {
        if (!t.clips.isEmpty()) {
            hasExistingClips = true;
            break;
        }
    }

    if (!hasExistingClips && tmpl.width > 0 && tmpl.height > 0) {
        project.setResolution(tmpl.width, tmpl.height);
        if (tmpl.fps > 0)
            project.setFps(tmpl.fps);
    }

    // 1. Create BinFolder in project
    QString templatesFolderId;
    for (const auto &folder : project.binFolders()) {
        if (folder.name.compare(QStringLiteral("Templates"), Qt::CaseInsensitive) == 0) {
            templatesFolderId = folder.id;
            break;
        }
    }
    if (templatesFolderId.isEmpty()) {
        templatesFolderId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        BinFolder topFolder;
        topFolder.id = templatesFolderId;
        topFolder.name = QStringLiteral("Templates");
        project.addBinFolder(topFolder);
    }

    const QString templateFolderId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    BinFolder templateFolder;
    templateFolder.id = templateFolderId;
    templateFolder.name = tmpl.title;
    templateFolder.parentId = templatesFolderId;
    project.addBinFolder(templateFolder);

    // 2. Register media assets
    QHash<QString, QString> pathToAssetId;
    for (const QString &path : tmpl.assetPaths) {
        const QString ext = QFileInfo(path).suffix().toLower();
        MediaAsset asset;
        asset.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        asset.name = QFileInfo(path).fileName();
        asset.path = path;
        asset.folderId = templateFolderId;
        asset.durationUs = tmpl.durationUs;

        if (ext == QLatin1String("png") || ext == QLatin1String("jpg") || ext == QLatin1String("jpeg")
            || ext == QLatin1String("webp") || ext == QLatin1String("svg")) {
            asset.kind = MediaKind::Image;
        } else if (ext == QLatin1String("mp3") || ext == QLatin1String("wav") || ext == QLatin1String("aac")
                   || ext == QLatin1String("m4a") || ext == QLatin1String("ogg") || ext == QLatin1String("flac")) {
            asset.kind = MediaKind::Audio;
        } else {
            asset.kind = MediaKind::Video;
        }

        project.assets().insert(asset.id, asset);
        project.assetOrder().append(asset.id);
        pathToAssetId.insert(path, asset.id);
    }

    // 3. If an inner Premiere project was bundled in the MOGRT, import its tracks
    if (!tmpl.prprojFilePath.isEmpty()) {
        QString prprojError;
        const auto prproj = drift::prproj::readProject(tmpl.prprojFilePath, &prprojError);
        if (prproj) {
            for (const Track &t : prproj->tracks()) {
                Track copy = t;
                for (Clip &c : copy.clips) {
                    c.timelineStart += insertTimeUs;
                }
                project.tracks().append(copy);
            }
            project.ensureTrackIds();
            return true;
        }
    }

    // 4. Instantiate visual and text clips on timeline
    // Find or create video track for background/overlay graphics
    int visualTrackIdx = -1;
    int textTrackIdx = -1;
    int audioTrackIdx = -1;

    for (int i = 0; i < project.tracks().size(); ++i) {
        if (project.tracks().at(i).type == TrackType::Video) {
            if (visualTrackIdx < 0)
                visualTrackIdx = i;
            else if (textTrackIdx < 0)
                textTrackIdx = i;
        } else if (project.tracks().at(i).type == TrackType::Audio) {
            if (audioTrackIdx < 0)
                audioTrackIdx = i;
        }
    }

    if (visualTrackIdx < 0) {
        Track vTrack;
        vTrack.type = TrackType::Video;
        vTrack.name = QStringLiteral("V1 (Template)");
        project.tracks().append(vTrack);
        visualTrackIdx = project.tracks().size() - 1;
    }

    if (textTrackIdx < 0) {
        Track tTrack;
        tTrack.type = TrackType::Video;
        tTrack.name = QStringLiteral("V2 (Text)");
        project.tracks().append(tTrack);
        textTrackIdx = project.tracks().size() - 1;
    }

    // Place visual assets
    for (const QString &path : tmpl.assetPaths) {
        const QString ext = QFileInfo(path).suffix().toLower();
        const QString assetId = pathToAssetId.value(path);

        if (ext == QLatin1String("mp3") || ext == QLatin1String("wav") || ext == QLatin1String("aac")
            || ext == QLatin1String("m4a") || ext == QLatin1String("ogg") || ext == QLatin1String("flac")) {
            if (audioTrackIdx < 0) {
                Track aTrack;
                aTrack.type = TrackType::Audio;
                aTrack.name = QStringLiteral("A1 (Template Audio)");
                project.tracks().append(aTrack);
                audioTrackIdx = project.tracks().size() - 1;
            }
            Clip clip;
            clip.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            clip.assetId = assetId;
            clip.name = QFileInfo(path).fileName();
            clip.path = path;
            clip.type = ClipType::Audio;
            clip.timelineStart = insertTimeUs;
            clip.timelineDuration = tmpl.durationUs;
            clip.srcIn = 0;
            clip.srcOut = tmpl.durationUs;
            project.tracks()[audioTrackIdx].clips.append(clip);
        } else {
            Clip clip;
            clip.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            clip.assetId = assetId;
            clip.name = QFileInfo(path).fileName();
            clip.path = path;
            clip.type = (ext == QLatin1String("png") || ext == QLatin1String("jpg") || ext == QLatin1String("jpeg")
                         || ext == QLatin1String("webp") || ext == QLatin1String("svg"))
                ? ClipType::Image
                : ClipType::Video;
            clip.timelineStart = insertTimeUs;
            clip.timelineDuration = tmpl.durationUs;
            clip.srcIn = 0;
            clip.srcOut = tmpl.durationUs;

            clip.transformX.setKeyframe(0, 0);
            clip.transformY.setKeyframe(0, 0);
            clip.transformW.setKeyframe(0, project.width());
            clip.transformH.setKeyframe(0, project.height());

            project.tracks()[visualTrackIdx].clips.append(clip);
        }
    }

    // Determine font color from color properties if available
    QColor textColor = Qt::white;
    for (const MogrtProperty &p : tmpl.properties) {
        if (p.type == QLatin1String("color") && p.colorValue.isValid()) {
            textColor = p.colorValue;
            break;
        }
    }

    // Place text properties
    int textCount = 0;
    for (const MogrtProperty &p : tmpl.properties) {
        if ((p.type == QLatin1String("text") || p.type == QLatin1String("string"))
            && !p.stringValue.trimmed().isEmpty())
            textCount++;
    }

    int textIndex = 0;
    for (const MogrtProperty &p : tmpl.properties) {
        if (p.type != QLatin1String("text") && p.type != QLatin1String("string"))
            continue;
        if (p.stringValue.trimmed().isEmpty())
            continue;

        Clip textClip;
        textClip.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        textClip.type = ClipType::Text;
        textClip.name = p.name.isEmpty() ? QStringLiteral("Text") : p.name;
        textClip.textContent = p.stringValue;
        textClip.timelineStart = insertTimeUs;
        textClip.timelineDuration = tmpl.durationUs;
        textClip.srcIn = 0;
        textClip.srcOut = tmpl.durationUs;

        textClip.textStyle.fontFamily = p.fontFamily.isEmpty() ? QStringLiteral("Inter") : p.fontFamily;
        textClip.textStyle.pixelSize = p.fontSize > 0 ? p.fontSize : ((textIndex == 0) ? 64 : 40);
        textClip.textStyle.color = p.colorValue.isValid() ? p.colorValue : textColor;
        textClip.textStyle.fontWeight = (textIndex == 0) ? 700 : 400;

        const double yPos = (textCount <= 1)
            ? (project.height() * 0.38)
            : (project.height() * 0.35 + textIndex * 70);
        textClip.transformX.setKeyframe(0, 0);
        textClip.transformY.setKeyframe(0, yPos);
        textClip.transformW.setKeyframe(0, project.width());
        textClip.transformH.setKeyframe(0, project.height() * 0.25);

        project.tracks()[textTrackIdx].clips.append(textClip);
        textIndex++;
    }

    project.ensureTrackIds();
    return true;
}

} // namespace drift::mogrt
