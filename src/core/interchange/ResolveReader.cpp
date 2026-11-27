#include "core/interchange/ResolveReader.h"

#include "core/project/clip/Clip.h"
#include "core/project/media/MediaAsset.h"
#include "core/project/Project.h"
#include "core/project/Track.h"
#include "core/interchange/ZipEntries.h"

#include <QCoreApplication>
#include <QDir>
#include <QDomDocument>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QRegularExpression>
#include <QUrl>
#include <QUuid>
#include <QtMath>
#include <functional>

namespace drift::resolve {

namespace {

constexpr quint32 kZipLocalHeaderMagic = 0x04034b50;

quint32 readU32(const char *ptr)
{
    const auto *b = reinterpret_cast<const uint8_t *>(ptr);
    return static_cast<quint32>(b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24));
}

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

TimeUs parseFcpxmlTime(const QString &str)
{
    QString s = str.trimmed();
    if (s.isEmpty())
        return 0;

    if (s.endsWith(QLatin1Char('s'), Qt::CaseInsensitive))
        s.chop(1);

    const int slashIdx = s.indexOf(QLatin1Char('/'));
    if (slashIdx > 0) {
        const double num = s.left(slashIdx).toDouble();
        const double den = s.mid(slashIdx + 1).toDouble();
        if (den > 0.0)
            return static_cast<TimeUs>(llround((num / den) * 1000000.0));
    }

    bool ok = false;
    const double val = s.toDouble(&ok);
    if (ok)
        return static_cast<TimeUs>(llround(val * 1000000.0));

    return 0;
}

using Severity = drift::interchange::InterchangeIssue::Severity;

void note(drift::interchange::InterchangeReport *report, Severity severity, const char *category,
          const char *message, const QString &itemName = {}, TimeUs at = -1)
{
    if (report)
        report->add(severity, QString::fromLatin1(category), QCoreApplication::translate("Interchange", message),
                    itemName, at);
}

void reportFcpxmlElement(drift::interchange::InterchangeReport *report, const QDomElement &el, int depth,
                         const QString &name, TimeUs at)
{
    if (!report)
        return;
    const QString tag = el.tagName();
    if (tag == QLatin1String("transition")) {
        note(report, Severity::Dropped, "transition", "Transitions are not imported.", name, at);
    } else if (tag.startsWith(QLatin1String("filter-"))) {
        note(report, Severity::Dropped, "effect", "Effects and filters are not imported.", name, at);
    } else if (tag == QLatin1String("adjust-volume") || tag == QLatin1String("adjust-panner")
               || tag == QLatin1String("adjust-loudness") || tag == QLatin1String("adjust-noiseReduction")
               || tag == QLatin1String("adjust-EQ") || tag == QLatin1String("adjust-humReduction")) {
        note(report, Severity::Dropped, "audio", "Audio levels and pan are not imported.", name, at);
    } else if (tag.startsWith(QLatin1String("adjust-")) && tag != QLatin1String("adjust-conform")) {
        note(report, Severity::Dropped, "effect", "Transform, crop and other adjustments are not imported.", name,
             at);
    } else if (tag == QLatin1String("keyframeAnimation")) {
        note(report, Severity::Dropped, "keyframes", "Keyframes are not imported.", name, at);
    } else if (tag == QLatin1String("timeMap")) {
        note(report, Severity::Dropped, "speed", "Speed changes and retiming are not imported.", name, at);
    } else if (tag == QLatin1String("marker") || tag == QLatin1String("chapter-marker")) {
        note(report, Severity::Dropped, "marker", "Markers are not imported.", name, at);
    } else if (tag == QLatin1String("ref-clip") || tag == QLatin1String("mc-clip") || tag == QLatin1String("sync-clip")
               || tag == QLatin1String("audition")) {
        note(report, Severity::Dropped, "nested",
             "Compound, multicam and synchronized clips are not expanded; they import as a single clip.", name, at);
    } else if (depth > 0 && el.hasAttribute(QStringLiteral("lane"))) {
        note(report, Severity::Dropped, "format",
             "Connected clips on other lanes are not imported; only the primary storyline is kept.", name, at);
    }
    for (QDomElement c = el.firstChildElement(); !c.isNull(); c = c.nextSiblingElement())
        reportFcpxmlElement(report, c, depth + 1, name, at);
}

struct FcpxmlFormat
{
    int width = 1920;
    int height = 1080;
    double fps = 24.0;
};

struct FcpxmlAsset
{
    QString id;
    QString name;
    QString src;
    TimeUs durationUs = 0;
    bool hasVideo = true;
    bool hasAudio = true;
};

} // namespace

bool isResolveProject(const QString &filePath)
{
    const QString ext = QFileInfo(filePath).suffix().toLower();
    if (ext == QLatin1String("drp") || ext == QLatin1String("fcpxml"))
        return true;
    if (ext == QLatin1String("mogrt") || ext == QLatin1String("drift") || ext == QLatin1String("zip"))
        return false;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return false;

    const QByteArray header = file.read(2048);
    if (header.contains("<fcpxml") || header.contains("<xmeml"))
        return true;

    if (header.size() >= 4 && readU32(header.constData()) == kZipLocalHeaderMagic) {
        const QStringList names = drift::interchange::zipEntryNames(filePath);
        for (const QString &name : names) {
            if (name.compare(QLatin1String("project.xml"), Qt::CaseInsensitive) == 0
                || name.startsWith(QLatin1String("SeqContainer"), Qt::CaseInsensitive)
                || name.startsWith(QLatin1String("MediaPool"), Qt::CaseInsensitive)) {
                return true;
            }
        }
    }

    return false;
}

bool isResolveData(const QByteArray &data)
{
    if (data.size() < 4)
        return false;

    // FCPXML header
    const QString text = QString::fromUtf8(data.left(1024)).trimmed();
    if (text.contains(QLatin1String("<fcpxml"), Qt::CaseInsensitive))
        return true;

    // DRP archive
    if (readU32(data.constData()) == kZipLocalHeaderMagic) {
        return data.contains("project.xml") || data.contains("SeqContainer") || data.contains("MediaPool");
    }

    return false;
}

std::optional<Project> readProject(const QString &filePath, QString *error, drift::interchange::InterchangeReport *report)
{
    if (filePath.endsWith(QLatin1String(".drp"), Qt::CaseInsensitive)) {
        return readDrpArchive(filePath, error, report);
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QObject::tr("Cannot open file: %1").arg(filePath);
        return std::nullopt;
    }

    const QByteArray data = file.readAll();
    const QString sourceDir = QFileInfo(filePath).absolutePath();

    if (isResolveData(data) && readU32(data.constData()) == kZipLocalHeaderMagic) {
        return readDrpArchive(filePath, error, report);
    }

    return readProjectData(data, sourceDir, error, report);
}

std::optional<Project> readProjectData(const QByteArray &data, const QString &sourceDir, QString *error,
                                      drift::interchange::InterchangeReport *report)
{
    if (data.isEmpty()) {
        if (error)
            *error = QObject::tr("File is empty");
        return std::nullopt;
    }

    return readFcpxmlData(data, sourceDir, error, report);
}

std::optional<Project> readFcpxmlData(const QByteArray &data, const QString &sourceDir, QString *error,
                                      drift::interchange::InterchangeReport *report)
{
    QDomDocument doc;
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    const QDomDocument::ParseResult parseResult = doc.setContent(data);
    if (!parseResult) {
        if (error)
            *error = QObject::tr("XML parse error at line %1, column %2: %3")
                         .arg(parseResult.errorLine)
                         .arg(parseResult.errorColumn)
                         .arg(parseResult.errorMessage);
        return std::nullopt;
    }
#else
    QString parseErrMsg;
    int errLine = 0;
    int errCol = 0;
    if (!doc.setContent(data, &parseErrMsg, &errLine, &errCol)) {
        if (error)
            *error = QObject::tr("XML parse error at line %1: %2").arg(errLine).arg(parseErrMsg);
        return std::nullopt;
    }
#endif

    const QDomElement root = doc.documentElement();
    if (root.tagName() != QLatin1String("fcpxml")) {
        if (error)
            *error = QObject::tr("Root element is <%1>, expected <fcpxml>").arg(root.tagName());
        return std::nullopt;
    }

    Project project;
    project.setName(QStringLiteral("DaVinci Resolve Timeline"));

    // 1. Resources
    QMap<QString, FcpxmlFormat> formats;
    QMap<QString, FcpxmlAsset> assets;

    const QDomElement resEl = root.firstChildElement(QStringLiteral("resources"));
    if (!resEl.isNull()) {
        for (auto el = resEl.firstChildElement(); !el.isNull(); el = el.nextSiblingElement()) {
            const QString tag = el.tagName();
            const QString id = el.attribute(QStringLiteral("id"));

            if (tag == QLatin1String("format")) {
                FcpxmlFormat fmt;
                if (el.hasAttribute(QStringLiteral("width")))
                    fmt.width = el.attribute(QStringLiteral("width")).toInt();
                if (el.hasAttribute(QStringLiteral("height")))
                    fmt.height = el.attribute(QStringLiteral("height")).toInt();

                const QString durStr = el.attribute(QStringLiteral("frameDuration"));
                const TimeUs frameDurUs = parseFcpxmlTime(durStr);
                if (frameDurUs > 0)
                    fmt.fps = 1000000.0 / static_cast<double>(frameDurUs);

                formats.insert(id, fmt);
            } else if (tag == QLatin1String("asset")) {
                FcpxmlAsset a;
                a.id = id;
                a.name = el.attribute(QStringLiteral("name"));
                a.src = el.attribute(QStringLiteral("src"));
                a.durationUs = parseFcpxmlTime(el.attribute(QStringLiteral("duration")));
                if (el.hasAttribute(QStringLiteral("hasVideo")))
                    a.hasVideo = (el.attribute(QStringLiteral("hasVideo")) != QLatin1String("0"));
                if (el.hasAttribute(QStringLiteral("hasAudio")))
                    a.hasAudio = (el.attribute(QStringLiteral("hasAudio")) != QLatin1String("0"));

                assets.insert(id, a);
            }
        }
    }

    // Register media assets with project
    QMap<QString, QString> assetIdToProjectAssetId;
    for (auto it = assets.constBegin(); it != assets.constEnd(); ++it) {
        const FcpxmlAsset &a = it.value();
        const QString resolved = resolveMediaPath(a.src, sourceDir);

        MediaAsset ma;
        ma.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        ma.name = !a.name.isEmpty() ? a.name : QFileInfo(resolved).fileName();
        ma.path = resolved;
        ma.durationUs = a.durationUs;
        ma.kind = a.hasVideo ? MediaKind::Video : MediaKind::Audio;
        project.addAsset(ma);

        assetIdToProjectAssetId.insert(a.id, ma.id);
    }

    // 2. Sequence & Project settings
    const QDomElement projEl = root.elementsByTagName(QStringLiteral("project")).item(0).toElement();
    if (!projEl.isNull() && projEl.hasAttribute(QStringLiteral("name")))
        project.setName(projEl.attribute(QStringLiteral("name")));

    const QDomElement seqEl = root.elementsByTagName(QStringLiteral("sequence")).item(0).toElement();
    for (int i = 1; i < root.elementsByTagName(QStringLiteral("sequence")).size(); ++i)
        note(report, Severity::Dropped, "sequence", "Only the first sequence is imported.",
             root.elementsByTagName(QStringLiteral("sequence")).item(i).toElement().attribute(QStringLiteral("name")));
    if (!seqEl.isNull()) {
        const QString fmtId = seqEl.attribute(QStringLiteral("format"));
        if (formats.contains(fmtId)) {
            const FcpxmlFormat &f = formats.value(fmtId);
            project.setResolution(f.width, f.height);
            project.setFps(qMax(1, qRound(f.fps)));
        } else if (!formats.isEmpty()) {
            const FcpxmlFormat &f = formats.first();
            project.setResolution(f.width, f.height);
            project.setFps(qMax(1, qRound(f.fps)));
        }
    }

    // 3. Spine and clips
    const QDomElement spineEl = root.elementsByTagName(QStringLiteral("spine")).item(0).toElement();
    if (spineEl.isNull()) {
        note(report, Severity::Error, "format", "The file has no timeline (no spine); an empty timeline was created.");
        project.tracks().clear();
        Track vTrack;
        vTrack.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        vTrack.type = TrackType::Video;
        vTrack.name = QStringLiteral("V1");
        project.tracks().append(vTrack);

        Track aTrack;
        aTrack.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        aTrack.type = TrackType::Audio;
        aTrack.name = QStringLiteral("A1");
        project.tracks().append(aTrack);
        project.ensureTrackIds();
        return project;
    }

    QList<Clip> videoClips;
    QList<Clip> audioClips;
    TimeUs spineCursorUs = 0;

    for (auto child = spineEl.firstChildElement(); !child.isNull(); child = child.nextSiblingElement()) {
        const QString tag = child.tagName();

        const TimeUs durUs = parseFcpxmlTime(child.attribute(QStringLiteral("duration")));
        const TimeUs startUs = child.hasAttribute(QStringLiteral("start"))
            ? parseFcpxmlTime(child.attribute(QStringLiteral("start"))) : 0LL;

        TimeUs offsetUs = spineCursorUs;
        if (child.hasAttribute(QStringLiteral("offset")))
            offsetUs = parseFcpxmlTime(child.attribute(QStringLiteral("offset")));

        if (tag == QLatin1String("gap")) {
            spineCursorUs = offsetUs + durUs;
            for (QDomElement c = child.firstChildElement(); !c.isNull(); c = c.nextSiblingElement())
                reportFcpxmlElement(report, c, 1, child.attribute(QStringLiteral("name")), offsetUs);
            continue;
        }

        reportFcpxmlElement(report, child, 0, child.attribute(QStringLiteral("name")), offsetUs);

        if (tag == QLatin1String("title")) {
            Clip clip;
            clip.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            clip.type = ClipType::Text;
            clip.name = child.hasAttribute(QStringLiteral("name")) ? child.attribute(QStringLiteral("name")) : QStringLiteral("Title");
            clip.timelineStart = offsetUs;
            clip.timelineDuration = durUs > 0 ? durUs : 5000000LL;

            // Text content and styling
            const QDomElement textEl = child.firstChildElement(QStringLiteral("text"));
            if (!textEl.isNull()) {
                clip.textContent = textEl.text().trimmed();

                const QDomElement styleEl = textEl.firstChildElement(QStringLiteral("text-style"));
                if (!styleEl.isNull()) {
                    if (styleEl.hasAttribute(QStringLiteral("font")))
                        clip.textStyle.fontFamily = styleEl.attribute(QStringLiteral("font"));
                    if (styleEl.hasAttribute(QStringLiteral("fontSize")))
                        clip.textStyle.pixelSize = styleEl.attribute(QStringLiteral("fontSize")).toInt();
                    if (styleEl.hasAttribute(QStringLiteral("bold")))
                        clip.textStyle.fontWeight = (styleEl.attribute(QStringLiteral("bold")) == QLatin1String("1")) ? 700 : 400;

                    if (styleEl.hasAttribute(QStringLiteral("fontColor"))) {
                        const QStringList parts = styleEl.attribute(QStringLiteral("fontColor")).split(QLatin1Char(' '), Qt::SkipEmptyParts);
                        if (parts.size() >= 3) {
                            const int r = qBound(0, qRound(parts[0].toDouble() * 255.0), 255);
                            const int g = qBound(0, qRound(parts[1].toDouble() * 255.0), 255);
                            const int b = qBound(0, qRound(parts[2].toDouble() * 255.0), 255);
                            const int a = (parts.size() >= 4) ? qBound(0, qRound(parts[3].toDouble() * 255.0), 255) : 255;
                            drift::setSolidFill(clip.textStyle, QColor(r, g, b, a));
                        }
                    }
                }
            }

            videoClips.append(clip);
            spineCursorUs = offsetUs + clip.timelineDuration;
            continue;
        }

        // <asset-clip>, <video>, <audio>, or <clip>
        const QString assetRef = child.attribute(QStringLiteral("ref"));
        const QString clipName = child.attribute(QStringLiteral("name"));

        const bool hasAsset = assets.contains(assetRef);
        const FcpxmlAsset fa = hasAsset ? assets.value(assetRef) : FcpxmlAsset{};

        const bool isAudioOnly = (tag == QLatin1String("audio") || (hasAsset && !fa.hasVideo && fa.hasAudio));

        Clip clip;
        clip.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        clip.name = !clipName.isEmpty() ? clipName : (!fa.name.isEmpty() ? fa.name : QStringLiteral("Clip"));
        clip.timelineStart = offsetUs;
        clip.timelineDuration = durUs > 0 ? durUs : (fa.durationUs > 0 ? fa.durationUs : 5000000LL);
        clip.srcIn = startUs;
        clip.srcOut = startUs + clip.timelineDuration;
        clip.type = isAudioOnly ? ClipType::Audio : ClipType::Video;

        if (assetIdToProjectAssetId.contains(assetRef))
            clip.assetId = assetIdToProjectAssetId.value(assetRef);

        if (isAudioOnly)
            audioClips.append(clip);
        else
            videoClips.append(clip);

        spineCursorUs = offsetUs + clip.timelineDuration;
    }

    project.tracks().clear();

    if (!videoClips.isEmpty()) {
        Track vTrack;
        vTrack.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        vTrack.type = TrackType::Video;
        vTrack.name = QStringLiteral("V1");
        vTrack.clips = videoClips;
        project.tracks().append(vTrack);
    }

    if (!audioClips.isEmpty()) {
        Track aTrack;
        aTrack.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        aTrack.type = TrackType::Audio;
        aTrack.name = QStringLiteral("A1");
        aTrack.clips = audioClips;
        project.tracks().append(aTrack);
    }

    if (project.tracks().isEmpty()) {
        note(report, Severity::Error, "format", "The timeline has no clips; the default timeline was used.");
        project.resetToDefaultTimeline();
    }

    project.ensureTrackIds();
    return project;
}

std::optional<Project> readDrpArchive(const QString &drpFilePath, QString *error,
                                      drift::interchange::InterchangeReport *report)
{
    const auto isDrpEntry = [](const QString &name) {
        const QString lower = name.toLower();
        return lower.endsWith(QLatin1String("project.xml"))
            || (lower.contains(QLatin1String("seqcontainer/")) && lower.endsWith(QLatin1String(".xml")))
            || (lower.contains(QLatin1String("mediapool/")) && lower.endsWith(QLatin1String(".xml")))
            || lower.endsWith(QLatin1String(".fcpxml"))
            || (lower.contains(QLatin1String("timeline")) && lower.endsWith(QLatin1String(".xml")));
    };
    QString zipError;
    const QList<drift::interchange::ZipEntryData> entries
        = drift::interchange::readZipEntries(drpFilePath, isDrpEntry, &zipError);
    if (!zipError.isEmpty()) {
        if (error)
            *error = zipError;
        return std::nullopt;
    }

    // Look for project.xml and any sequence XMLs in SeqContainer/
    const drift::interchange::ZipEntryData *projXmlEntry = nullptr;
    const drift::interchange::ZipEntryData *fcpxmlEntry = nullptr;
    QList<const drift::interchange::ZipEntryData *> seqEntries;
    QList<const drift::interchange::ZipEntryData *> mediaPoolEntries;

    for (const drift::interchange::ZipEntryData &e : entries) {
        const QString lower = e.name.toLower();
        if (lower.endsWith(QLatin1String("project.xml"))) {
            projXmlEntry = &e;
        } else if (lower.contains(QLatin1String("seqcontainer/")) && lower.endsWith(QLatin1String(".xml"))) {
            seqEntries.append(&e);
        } else if (lower.contains(QLatin1String("mediapool/")) && lower.endsWith(QLatin1String(".xml"))) {
            mediaPoolEntries.append(&e);
        } else if (lower.endsWith(QLatin1String(".fcpxml")) || (lower.contains(QLatin1String("timeline")) && lower.endsWith(QLatin1String(".xml")))) {
            fcpxmlEntry = &e;
        }
    }

    // If an embedded FCPXML or timeline XML was found, extract and parse it directly
    if (fcpxmlEntry != nullptr) {
        const QString sourceDir = QFileInfo(drpFilePath).absolutePath();
        return readFcpxmlData(fcpxmlEntry->data, sourceDir, error, report);
    }

    Project project;
    project.setName(QFileInfo(drpFilePath).completeBaseName());
    project.setResolution(1920, 1080);
    project.setFps(24);

    double fps = 24.0;
    bool sizeRead = false;
    bool fpsRead = false;
    const QString sourceDir = QFileInfo(drpFilePath).absolutePath();

    // 1. Parse project.xml if present
    if (projXmlEntry != nullptr) {
        QDomDocument doc;
        if (doc.setContent(projXmlEntry->data)) {
            const QDomElement root = doc.documentElement();

            // Project Name
            const QDomElement nameEl = root.elementsByTagName(QStringLiteral("Name")).item(0).toElement();
            if (!nameEl.isNull() && !nameEl.text().isEmpty())
                project.setName(nameEl.text().trimmed());

            // Resolution
            const QDomElement wEl = root.elementsByTagName(QStringLiteral("TimelineResolutionWidth")).item(0).toElement();
            const QDomElement hEl = root.elementsByTagName(QStringLiteral("TimelineResolutionHeight")).item(0).toElement();
            int w = 1920;
            int h = 1080;
            if (!wEl.isNull() && wEl.text().toInt() > 0)
                w = wEl.text().toInt();
            if (!hEl.isNull() && hEl.text().toInt() > 0)
                h = hEl.text().toInt();
            project.setResolution(w, h);
            sizeRead = wEl.text().toInt() > 0 && hEl.text().toInt() > 0;

            // Frame rate
            const QDomElement fpsEl = root.elementsByTagName(QStringLiteral("TimelineFrameRate")).item(0).toElement();
            if (!fpsEl.isNull() && fpsEl.text().toDouble() > 0.0) {
                fps = fpsEl.text().toDouble();
                fpsRead = true;
                project.setFps(qMax(1, qRound(fps)));
            }
        }
    }

    // 2. Parse MediaPool
    struct ResolveMediaItem {
        QString id;
        QString name;
        QString path;
        QString folderId;
    };
    QMap<QString, ResolveMediaItem> mediaItems;

    for (const auto *mpEntry : mediaPoolEntries) {
        QDomDocument doc;
        if (!doc.setContent(mpEntry->data))
            continue;

        const QDomNodeList clipNodes = doc.elementsByTagName(QStringLiteral("Clip"));
        for (int i = 0; i < clipNodes.size(); ++i) {
            const QDomElement cEl = clipNodes.at(i).toElement();
            ResolveMediaItem item;
            item.id = cEl.firstChildElement(QStringLiteral("Id")).text().trimmed();
            item.name = cEl.firstChildElement(QStringLiteral("Name")).text().trimmed();
            item.path = cEl.firstChildElement(QStringLiteral("FilePath")).text().trimmed();
            item.folderId = cEl.firstChildElement(QStringLiteral("FolderId")).text().trimmed();

            if (item.name.isEmpty())
                item.name = cEl.attribute(QStringLiteral("name"));
            if (item.path.isEmpty())
                item.path = cEl.attribute(QStringLiteral("path"));

            if (!item.id.isEmpty() || !item.name.isEmpty()) {
                const QString key = !item.id.isEmpty() ? item.id : item.name;
                mediaItems.insert(key, item);
            }
        }

        // Bin folders: <Folder>, <MpFolder>, <Bin>
        const auto folderTagNames = { QStringLiteral("Folder"), QStringLiteral("MpFolder"), QStringLiteral("Bin") };
        for (const auto &tag : folderTagNames) {
            const QDomNodeList folderNodes = doc.elementsByTagName(tag);
            for (int i = 0; i < folderNodes.size(); ++i) {
                const QDomElement fEl = folderNodes.at(i).toElement();
                const QString fId = fEl.firstChildElement(QStringLiteral("Id")).text().trimmed();
                const QString fName = fEl.firstChildElement(QStringLiteral("Name")).text().trimmed();
                if (!fName.isEmpty()) {
                    bool alreadyPresent = false;
                    for (const auto &existing : project.binFolders()) {
                        if (existing.id == fId || existing.name == fName) {
                            alreadyPresent = true;
                            break;
                        }
                    }
                    if (!alreadyPresent) {
                        BinFolder folder;
                        folder.id = !fId.isEmpty() ? fId : QUuid::createUuid().toString(QUuid::WithoutBraces);
                        folder.name = fName;
                        project.addBinFolder(folder);
                    }
                }
            }
        }
    }

    // Register MediaAssets
    QMap<QString, QString> resolveIdToAssetId;
    for (auto it = mediaItems.constBegin(); it != mediaItems.constEnd(); ++it) {
        const ResolveMediaItem &item = it.value();
        const QString resolved = resolveMediaPath(item.path, sourceDir);

        MediaAsset ma;
        ma.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        ma.name = !item.name.isEmpty() ? item.name : QFileInfo(resolved).fileName();
        ma.path = resolved;
        ma.kind = MediaKind::Video;
        ma.folderId = item.folderId;
        project.addAsset(ma);

        resolveIdToAssetId.insert(it.key(), ma.id);
        if (!item.name.isEmpty())
            resolveIdToAssetId.insert(item.name, ma.id);
    }

    // 3. Parse Timeline Sequences in SeqContainer/
    if (!sizeRead)
        note(report, Severity::Degraded, "format", "The project resolution was not found; 1920x1080 was assumed.");
    if (!fpsRead)
        note(report, Severity::Degraded, "format", "The project frame rate was not found; 24 fps was assumed.");
    if (seqEntries.size() > 1)
        note(report, Severity::Degraded, "sequence", "Several timelines were merged onto a single track.");

    QList<Clip> videoClips;
    QList<Clip> audioClips;
    int videoTrackCount = 0;
    int audioTrackCount = 0;

    for (const auto *seqEntry : seqEntries) {
        QDomDocument doc;
        if (!doc.setContent(seqEntry->data))
            continue;

        const QDomElement root = doc.documentElement();

        // Sequence Name
        const QDomElement sNameEl = root.elementsByTagName(QStringLiteral("Name")).item(0).toElement();
        if (!sNameEl.isNull() && !sNameEl.text().isEmpty()) {
            project.setName(sNameEl.text().trimmed());
        }

        // Process Tracks: <Track> or <SM_Track>
        const auto trackNodes = root.elementsByTagName(QStringLiteral("Track"));
        const auto smTrackNodes = root.elementsByTagName(QStringLiteral("SM_Track"));

        QList<QDomElement> tracksToProcess;
        for (int i = 0; i < trackNodes.size(); ++i)
            tracksToProcess.append(trackNodes.at(i).toElement());
        for (int i = 0; i < smTrackNodes.size(); ++i)
            tracksToProcess.append(smTrackNodes.at(i).toElement());

        for (const QDomElement &tEl : tracksToProcess) {
            const QString typeStr = tEl.attribute(QStringLiteral("type")).toLower();
            const QString tagStr = tEl.tagName().toLower();
            const bool isAudio = (typeStr.contains(QLatin1String("audio"))
                                  || tagStr.contains(QLatin1String("audiotrack"))
                                  || tEl.attribute(QStringLiteral("trackType")).compare(QLatin1String("audio"), Qt::CaseInsensitive) == 0);
            ++(isAudio ? audioTrackCount : videoTrackCount);

            // Parse Clips in track
            const auto clipNodes = tEl.elementsByTagName(QStringLiteral("Clip"));
            const auto smClipNodes = tEl.elementsByTagName(QStringLiteral("SM_Clip"));

            QList<QDomElement> clipsToProcess;
            for (int j = 0; j < clipNodes.size(); ++j)
                clipsToProcess.append(clipNodes.at(j).toElement());
            for (int j = 0; j < smClipNodes.size(); ++j)
                clipsToProcess.append(smClipNodes.at(j).toElement());

            for (const QDomElement &cEl : clipsToProcess) {
                const QString cName = cEl.firstChildElement(QStringLiteral("Name")).text().trimmed();
                const QString mediaId = cEl.firstChildElement(QStringLiteral("MediaId")).text().trimmed();

                const qint64 startFrame = cEl.firstChildElement(QStringLiteral("StartFrame")).text().toLongLong();
                qint64 durFrame = cEl.firstChildElement(QStringLiteral("DurationFrame")).text().toLongLong();
                if (durFrame <= 0) {
                    const qint64 endFrame = cEl.firstChildElement(QStringLiteral("EndFrame")).text().toLongLong();
                    if (endFrame > startFrame)
                        durFrame = endFrame - startFrame;
                }
                const qint64 inFrame = cEl.firstChildElement(QStringLiteral("InFrame")).text().toLongLong();

                const double effectiveFps = fps > 0.0 ? fps : 24.0;
                const TimeUs startUs = static_cast<TimeUs>(llround((static_cast<double>(startFrame) * 1000000.0) / effectiveFps));
                const TimeUs durUs = (durFrame > 0)
                    ? static_cast<TimeUs>(llround((static_cast<double>(durFrame) * 1000000.0) / effectiveFps))
                    : 5000000LL;
                const TimeUs inUs = static_cast<TimeUs>(llround((static_cast<double>(inFrame) * 1000000.0) / effectiveFps));

                Clip clip;
                clip.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
                clip.name = !cName.isEmpty() ? cName : QStringLiteral("Clip");
                clip.timelineStart = startUs;
                clip.timelineDuration = durUs;
                clip.srcIn = inUs;
                clip.srcOut = inUs + durUs;
                clip.type = isAudio ? ClipType::Audio : ClipType::Video;

                if (resolveIdToAssetId.contains(mediaId)) {
                    clip.assetId = resolveIdToAssetId.value(mediaId);
                } else if (resolveIdToAssetId.contains(cName)) {
                    clip.assetId = resolveIdToAssetId.value(cName);
                }

                if (report) {
                    const std::function<void(const QDomElement &)> scan = [&](const QDomElement &el) {
                        for (QDomElement c = el.firstChildElement(); !c.isNull(); c = c.nextSiblingElement()) {
                            const QString t = c.tagName().toLower();
                            if (t.contains(QLatin1String("transition")))
                                note(report, Severity::Dropped, "transition", "Transitions are not imported.", clip.name,
                                     clip.timelineStart);
                            else if (t.contains(QLatin1String("keyframe")))
                                note(report, Severity::Dropped, "keyframes", "Keyframes are not imported.", clip.name,
                                     clip.timelineStart);
                            else if (t.contains(QLatin1String("speed")) || t.contains(QLatin1String("retime")))
                                note(report, Severity::Dropped, "speed", "Speed changes and retiming are not imported.",
                                     clip.name, clip.timelineStart);
                            else if (t.contains(QLatin1String("effect")) || t.contains(QLatin1String("filter"))
                                     || t.contains(QLatin1String("fusion")))
                                note(report, Severity::Dropped, "effect", "Effects and filters are not imported.",
                                     clip.name, clip.timelineStart);
                            else if (t.contains(QLatin1String("volume")) || t.contains(QLatin1String("pan")))
                                note(report, Severity::Dropped, "audio", "Audio levels and pan are not imported.",
                                     clip.name, clip.timelineStart);
                            else if (t.contains(QLatin1String("marker")))
                                note(report, Severity::Dropped, "marker", "Markers are not imported.", clip.name,
                                     clip.timelineStart);
                            scan(c);
                        }
                    };
                    scan(cEl);
                }

                if (isAudio)
                    audioClips.append(clip);
                else
                    videoClips.append(clip);
            }
        }
    }

    if (videoTrackCount > 1 || audioTrackCount > 1)
        note(report, Severity::Degraded, "format", "All video tracks were merged into one, and all audio tracks into one.");

    if (!videoClips.isEmpty() || !audioClips.isEmpty()) {
        project.tracks().clear();
    }

    if (!videoClips.isEmpty()) {
        Track vTrack;
        vTrack.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        vTrack.type = TrackType::Video;
        vTrack.name = QStringLiteral("V1");
        vTrack.clips = videoClips;
        project.tracks().append(vTrack);
    }

    if (!audioClips.isEmpty()) {
        Track aTrack;
        aTrack.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        aTrack.type = TrackType::Audio;
        aTrack.name = QStringLiteral("A1");
        aTrack.clips = audioClips;
        project.tracks().append(aTrack);
    }

    if (project.tracks().isEmpty()) {
        note(report, Severity::Error, "format", "The timeline has no clips; the default timeline was used.");
        project.resetToDefaultTimeline();
    }

    project.ensureTrackIds();
    return project;
}

} // namespace drift::resolve
