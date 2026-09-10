#include "PrprojReader.h"

#include "TimelineOps.h"

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
#include <algorithm>
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
    if (inflateInit2(&strm, 16 + MAX_WBITS) != Z_OK)
        return {};

    QByteArray result;
    result.resize(qMax(data.size() * 4, 65536));

    strm.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(data.constData()));
    strm.avail_in = static_cast<uInt>(data.size());

    while (true) {
        strm.next_out = reinterpret_cast<Bytef *>(result.data() + strm.total_out);
        strm.avail_out = static_cast<uInt>(result.size() - strm.total_out);

        const int ret = inflate(&strm, Z_NO_FLUSH);
        if (ret == Z_STREAM_END) {
            result.resize(static_cast<int>(strm.total_out));
            inflateEnd(&strm);
            return result;
        }
        if (ret != Z_OK && ret != Z_BUF_ERROR) {
            inflateEnd(&strm);
            return {};
        }
        result.resize(result.size() * 2);
    }
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

QString resolveMedia(const QString &normalizedPath, const QString &sourceDir, const QString &origProjectDir = QString())
{
    if (normalizedPath.isEmpty())
        return {};

    if (QFileInfo::exists(normalizedPath))
        return QFileInfo(normalizedPath).canonicalFilePath();

    if (sourceDir.isEmpty())
        return normalizedPath;

    // 1. Direct relative path against sourceDir
    const QString directRel = QDir(sourceDir).filePath(normalizedPath);
    if (QFileInfo::exists(directRel))
        return QFileInfo(directRel).canonicalFilePath();

    // 2. Relative path calculated from the project's original directory
    if (!origProjectDir.isEmpty()) {
        const QString relFromOrig = QDir(origProjectDir).relativeFilePath(normalizedPath);
        if (!relFromOrig.isEmpty() && !relFromOrig.startsWith(QLatin1String("../"))) {
            const QString testPath = QDir(sourceDir).filePath(relFromOrig);
            if (QFileInfo::exists(testPath))
                return QFileInfo(testPath).canonicalFilePath();
        }
    }

    // 3. Fallback: match by file name directly in sourceDir
    const QString fileName = QFileInfo(normalizedPath).fileName();
    if (fileName.isEmpty())
        return normalizedPath;

    const QString testDirect = QDir(sourceDir).filePath(fileName);
    if (QFileInfo::exists(testDirect))
        return QFileInfo(testDirect).canonicalFilePath();

    // 4. Recursive search within sourceDir for the file name
    QDirIterator it(sourceDir, QStringList() << fileName, QDir::Files, QDirIterator::Subdirectories);
    if (it.hasNext()) {
        return QFileInfo(it.next()).canonicalFilePath();
    }

    return normalizedPath;
}

bool isAdjustmentLayerItem(const QDomElement &itemEl,
                           const QHash<QString, QDomElement> &objects,
                           const QString &clipName)
{
    if (clipName.contains(QStringLiteral("adjustment"), Qt::CaseInsensitive)
        || clipName.contains(QStringLiteral("color correct"), Qt::CaseInsensitive)) {
        return true;
    }
    if (itemEl.firstChildElement(QStringLiteral("IsAdjustmentLayer")).text() == QLatin1String("true")
        || itemEl.firstChildElement(QStringLiteral("AdjustmentLayer")).text() == QLatin1String("true")) {
        return true;
    }

    // Check referenced SubClip -> MasterClip / VideoClip
    QString scRef = itemEl.firstChildElement(QStringLiteral("ClipTrackItem"))
                          .firstChildElement(QStringLiteral("SubClip")).attribute(QStringLiteral("ObjectRef"));
    if (scRef.isEmpty())
        scRef = itemEl.firstChildElement(QStringLiteral("ClipTrackItem"))
                      .firstChildElement(QStringLiteral("SubClip")).attribute(QStringLiteral("ObjectURef"));
    if (scRef.isEmpty())
        scRef = itemEl.elementsByTagName(QStringLiteral("SubClip")).item(0).toElement().attribute(QStringLiteral("ObjectRef"));
    if (scRef.isEmpty())
        scRef = itemEl.elementsByTagName(QStringLiteral("SubClip")).item(0).toElement().attribute(QStringLiteral("ObjectURef"));

    if (!scRef.isEmpty() && objects.contains(scRef)) {
        const QDomElement subClipElem = objects.value(scRef);
        if (subClipElem.firstChildElement(QStringLiteral("IsAdjustmentLayer")).text() == QLatin1String("true")
            || subClipElem.firstChildElement(QStringLiteral("AdjustmentLayer")).text() == QLatin1String("true")) {
            return true;
        }

        // Check MasterClip
        for (const QString &attr : {QStringLiteral("ObjectURef"), QStringLiteral("ObjectRef")}) {
            const QString mcRef = subClipElem.firstChildElement(QStringLiteral("MasterClip")).attribute(attr);
            if (!mcRef.isEmpty() && objects.contains(mcRef)) {
                const QDomElement mcElem = objects.value(mcRef);
                if (mcElem.firstChildElement(QStringLiteral("IsAdjustmentLayer")).text() == QLatin1String("true")
                    || mcElem.elementsByTagName(QStringLiteral("IsAdjustmentLayer")).item(0).toElement().text() == QLatin1String("true")) {
                    return true;
                }
            }
        }
        // Check Clip / VideoClip
        for (const QString &attr : {QStringLiteral("ObjectRef"), QStringLiteral("ObjectURef")}) {
            const QString cRef = subClipElem.firstChildElement(QStringLiteral("Clip")).attribute(attr);
            if (!cRef.isEmpty() && objects.contains(cRef)) {
                const QDomElement cElem = objects.value(cRef);
                if (cElem.firstChildElement(QStringLiteral("AdjustmentLayer")).text() == QLatin1String("true")
                    || cElem.elementsByTagName(QStringLiteral("AdjustmentLayer")).item(0).toElement().text() == QLatin1String("true")) {
                    return true;
                }
            }
        }
    }
    return false;
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
        if (name.contains(QStringLiteral("black"), Qt::CaseInsensitive)
            || name.contains(QStringLiteral("color matte"), Qt::CaseInsensitive)
            || name.contains(QStringLiteral("solid"), Qt::CaseInsensitive))
            return ClipType::Shape;
        return ClipType::Video;
    }

    const QString ext = QFileInfo(mediaPath).suffix().toLower();
    if (ext == QLatin1String("mp3") || ext == QLatin1String("wav") || ext == QLatin1String("aac")
        || ext == QLatin1String("flac") || ext == QLatin1String("m4a") || ext == QLatin1String("ogg"))
        return ClipType::Audio;

    if (imageExtensions().contains(ext))
        return ClipType::Image;

    return ClipType::Video;
}

void indexObjects(const QDomElement &elem, QHash<QString, QDomElement> &objects)
{
    if (elem.hasAttribute(QStringLiteral("ObjectID"))) {
        objects.insert(elem.attribute(QStringLiteral("ObjectID")), elem);
    }
    if (elem.hasAttribute(QStringLiteral("ObjectUID"))) {
        objects.insert(elem.attribute(QStringLiteral("ObjectUID")), elem);
    }
    for (QDomNode n = elem.firstChild(); !n.isNull(); n = n.nextSibling()) {
        if (n.isElement())
            indexObjects(n.toElement(), objects);
    }
}

QList<QDomElement> findSequenceTrackItems(const QDomElement &seqElem,
                                         const QHash<QString, QDomElement> &objects)
{
    QList<QDomElement> result;
    const QDomElement tg = seqElem.firstChildElement(QStringLiteral("TrackGroups"));
    if (tg.isNull())
        return result;

    for (QDomNode n = tg.firstChild(); !n.isNull(); n = n.nextSibling()) {
        if (!n.isElement())
            continue;
        const QDomElement c = n.toElement();
        QString ref = c.attribute(QStringLiteral("ObjectRef"));
        if (ref.isEmpty())
            ref = c.attribute(QStringLiteral("ObjectURef"));
        const QDomElement sec = c.firstChildElement(QStringLiteral("Second"));
        if (!sec.isNull()) {
            if (sec.hasAttribute(QStringLiteral("ObjectRef")))
                ref = sec.attribute(QStringLiteral("ObjectRef"));
            else if (sec.hasAttribute(QStringLiteral("ObjectURef")))
                ref = sec.attribute(QStringLiteral("ObjectURef"));
        }
        if (ref.isEmpty() || !objects.contains(ref))
            continue;

        const QDomElement grp = objects.value(ref);
        QDomElement subTg = grp.firstChildElement(QStringLiteral("TrackGroup"));
        if (subTg.isNull())
            subTg = grp;
        const QDomElement tracksElem = subTg.firstChildElement(QStringLiteral("Tracks"));
        if (tracksElem.isNull())
            continue;

        for (QDomNode tn = tracksElem.firstChild(); !tn.isNull(); tn = tn.nextSibling()) {
            if (!tn.isElement())
                continue;
            const QDomElement tNode = tn.toElement();
            QString tRef = tNode.attribute(QStringLiteral("ObjectRef"));
            if (tRef.isEmpty())
                tRef = tNode.attribute(QStringLiteral("ObjectURef"));
            if (tRef.isEmpty() || !objects.contains(tRef))
                continue;

            const QDomElement trackElem = objects.value(tRef);
            const QDomNodeList tiList = trackElem.elementsByTagName(QStringLiteral("TrackItem"));
            for (int i = 0; i < tiList.size(); ++i) {
                const QDomElement ti = tiList.at(i).toElement();
                QString tiRef = ti.attribute(QStringLiteral("ObjectRef"));
                if (tiRef.isEmpty())
                    tiRef = ti.attribute(QStringLiteral("ObjectURef"));
                if (!tiRef.isEmpty() && objects.contains(tiRef))
                    result.append(objects.value(tiRef));
            }
        }
    }
    return result;
}

QString resolveMediaPathFromElement(const QDomElement &startElem,
                                   const QHash<QString, QDomElement> &objects,
                                   QString *resolvedName = nullptr,
                                   int recursionDepth = 0)
{
    if (recursionDepth > 15)
        return {};

    QDomElement current = startElem;
    for (int depth = 0; depth < 12; ++depth) {
        QString path = current.firstChildElement(QStringLiteral("RelativePath")).text();
        if (path.isEmpty())
            path = current.elementsByTagName(QStringLiteral("RelativePath")).item(0).toElement().text();
        if (path.isEmpty())
            path = current.firstChildElement(QStringLiteral("ActualMediaFilePath")).text();
        if (path.isEmpty())
            path = current.elementsByTagName(QStringLiteral("ActualMediaFilePath")).item(0).toElement().text();
        if (path.isEmpty())
            path = current.firstChildElement(QStringLiteral("FilePath")).text();
        if (path.isEmpty())
            path = current.elementsByTagName(QStringLiteral("FilePath")).item(0).toElement().text();
        if (path.isEmpty())
            path = current.firstChildElement(QStringLiteral("OriginalFilePath")).text();
        if (path.isEmpty())
            path = current.elementsByTagName(QStringLiteral("OriginalFilePath")).item(0).toElement().text();
        if (path.isEmpty())
            path = current.firstChildElement(QStringLiteral("pathurl")).text();
        if (path.isEmpty())
            path = current.elementsByTagName(QStringLiteral("pathurl")).item(0).toElement().text();

        path = path.trimmed();
        while (path.startsWith(QLatin1String("./")) || path.startsWith(QLatin1String(".\\"))) {
            path = path.mid(2);
        }

        if (resolvedName && resolvedName->isEmpty()) {
            const QString name = current.firstChildElement(QStringLiteral("Name")).isNull()
                ? current.elementsByTagName(QStringLiteral("Name")).item(0).toElement().text()
                : current.firstChildElement(QStringLiteral("Name")).text();
            if (!name.isEmpty())
                *resolvedName = name;
        }

        if (!path.isEmpty()) {
            bool isNumber = false;
            path.toLongLong(&isNumber);
            if (!isNumber)
                return path;
        }

        if (current.tagName().compare(QStringLiteral("Sequence"), Qt::CaseInsensitive) == 0) {
            const QList<QDomElement> items = findSequenceTrackItems(current, objects);
            for (const QDomElement &subItem : items) {
                const QString subPath = resolveMediaPathFromElement(subItem, objects, resolvedName, recursionDepth + 1);
                if (!subPath.isEmpty())
                    return subPath;
            }
        }

        QString nextRef;
        const QStringList refTags = {QStringLiteral("Source"), QStringLiteral("Media"),
                                     QStringLiteral("MasterClip"), QStringLiteral("SubClip"),
                                     QStringLiteral("Clip"), QStringLiteral("Sequence"),
                                     QStringLiteral("ProjectItem"), QStringLiteral("file")};

        for (const QString &tag : refTags) {
            const QDomNodeList list = current.elementsByTagName(tag);
            for (int i = 0; i < list.size(); ++i) {
                const QDomElement child = list.at(i).toElement();
                if (child.hasAttribute(QStringLiteral("ObjectRef"))) {
                    const QString ref = child.attribute(QStringLiteral("ObjectRef"));
                    if (objects.contains(ref) && ref != current.attribute(QStringLiteral("ObjectID"))) {
                        nextRef = ref;
                        break;
                    }
                } else if (child.hasAttribute(QStringLiteral("ObjectURef"))) {
                    const QString ref = child.attribute(QStringLiteral("ObjectURef"));
                    if (objects.contains(ref) && ref != current.attribute(QStringLiteral("ObjectUID"))) {
                        nextRef = ref;
                        break;
                    }
                } else if (child.hasAttribute(QStringLiteral("id"))) {
                    const QString ref = child.attribute(QStringLiteral("id"));
                    if (objects.contains(ref)) {
                        nextRef = ref;
                        break;
                    }
                }
            }
            if (!nextRef.isEmpty())
                break;
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

void parseClipComponents(const QDomElement &itemEl,
                         Clip &clip,
                         const QHash<QString, QDomElement> &objects,
                         int projectWidth,
                         int projectHeight)
{
    QDomElement compsElem = itemEl.firstChildElement(QStringLiteral("ClipTrackItem"))
                                   .firstChildElement(QStringLiteral("ComponentOwner"))
                                   .firstChildElement(QStringLiteral("Components"));
    if (compsElem.isNull())
        compsElem = itemEl.firstChildElement(QStringLiteral("Components"));
    if (compsElem.isNull()) {
        const QDomNodeList cList = itemEl.elementsByTagName(QStringLiteral("Components"));
        if (!cList.isEmpty())
            compsElem = cList.item(0).toElement();
    }

    if (compsElem.isNull())
        return;

    QString chainRef = compsElem.attribute(QStringLiteral("ObjectRef"));
    if (chainRef.isEmpty())
        chainRef = compsElem.attribute(QStringLiteral("ObjectURef"));
    if (chainRef.isEmpty() || !objects.contains(chainRef))
        return;

    const QDomElement chainElem = objects.value(chainRef);
    const QDomNodeList compNodes = chainElem.elementsByTagName(QStringLiteral("Component"));

    for (int i = 0; i < compNodes.size(); ++i) {
        const QDomElement cRefEl = compNodes.at(i).toElement();
        QString compRef = cRefEl.attribute(QStringLiteral("ObjectRef"));
        if (compRef.isEmpty())
            compRef = cRefEl.attribute(QStringLiteral("ObjectURef"));
        if (compRef.isEmpty() || !objects.contains(compRef))
            continue;

        const QDomElement compElem = objects.value(compRef);
        const QString matchName = compElem.firstChildElement(QStringLiteral("MatchName")).isNull()
            ? compElem.elementsByTagName(QStringLiteral("MatchName")).item(0).toElement().text()
            : compElem.firstChildElement(QStringLiteral("MatchName")).text();
        const QString displayName = compElem.firstChildElement(QStringLiteral("DisplayName")).isNull()
            ? compElem.elementsByTagName(QStringLiteral("DisplayName")).item(0).toElement().text()
            : compElem.firstChildElement(QStringLiteral("DisplayName")).text();

        if (matchName == QLatin1String("AE.ADBE Opacity") || displayName.compare(QLatin1String("Opacity"), Qt::CaseInsensitive) == 0) {
            const QDomNodeList params = compElem.elementsByTagName(QStringLiteral("Param"));
            for (int p = 0; p < params.size(); ++p) {
                QString pRef = params.at(p).toElement().attribute(QStringLiteral("ObjectRef"));
                if (pRef.isEmpty())
                    pRef = params.at(p).toElement().attribute(QStringLiteral("ObjectURef"));
                if (!objects.contains(pRef))
                    continue;
                const QDomElement paramElem = objects.value(pRef);
                const QString pName = paramElem.firstChildElement(QStringLiteral("Name")).text();
                const int pId = paramElem.firstChildElement(QStringLiteral("ParameterID")).text().toInt();
                const QString kfStr = paramElem.firstChildElement(QStringLiteral("StartKeyframe")).text();
                const QStringList kfParts = kfStr.split(QLatin1Char(','), Qt::SkipEmptyParts);
                if (kfParts.size() < 2)
                    continue;

                const double val = kfParts.at(1).trimmed().toDouble();
                if (pId == 3 || pName.contains(QStringLiteral("Blend Mode"), Qt::CaseInsensitive)) {
                    const int modeVal = static_cast<int>(val);
                    switch (modeVal) {
                    case 4: clip.blendMode = BlendMode::Darken; break;
                    case 5: clip.blendMode = BlendMode::Multiply; break;
                    case 10: clip.blendMode = BlendMode::Add; break;
                    case 11: clip.blendMode = BlendMode::Lighten; break;
                    case 12: clip.blendMode = BlendMode::Screen; break;
                    case 17: clip.blendMode = BlendMode::Overlay; break;
                    default: break;
                    }
                } else if (pId == 1 || pName.compare(QStringLiteral("Opacity"), Qt::CaseInsensitive) == 0) {
                    if (val < 99.9 && val >= 0.0) {
                        clip.opacity.setKeyframe(0, qBound(0.0, val / 100.0, 1.0));
                    }
                }
            }
        } else if (matchName == QLatin1String("AE.ADBE Motion") || displayName.compare(QLatin1String("Motion"), Qt::CaseInsensitive) == 0) {
            double posX = 0.5;
            double posY = 0.5;
            double scale = 100.0;
            double rotation = 0.0;
            bool hasPos = false;
            bool hasScale = false;
            bool hasRot = false;

            const QDomNodeList params = compElem.elementsByTagName(QStringLiteral("Param"));
            for (int p = 0; p < params.size(); ++p) {
                QString pRef = params.at(p).toElement().attribute(QStringLiteral("ObjectRef"));
                if (pRef.isEmpty())
                    pRef = params.at(p).toElement().attribute(QStringLiteral("ObjectURef"));
                if (!objects.contains(pRef))
                    continue;
                const QDomElement paramElem = objects.value(pRef);
                const int pId = paramElem.firstChildElement(QStringLiteral("ParameterID")).text().toInt();
                const QString kfStr = paramElem.firstChildElement(QStringLiteral("StartKeyframe")).text();
                const QStringList kfParts = kfStr.split(QLatin1Char(','), Qt::SkipEmptyParts);
                if (kfParts.size() < 2)
                    continue;
                const QString valStr = kfParts.at(1).trimmed();

                if (pId == 1) {
                    const QStringList posParts = valStr.split(QLatin1Char(':'));
                    if (posParts.size() >= 2) {
                        posX = posParts.at(0).toDouble();
                        posY = posParts.at(1).toDouble();
                        hasPos = true;
                    }
                } else if (pId == 2) {
                    scale = valStr.toDouble();
                    hasScale = true;
                } else if (pId == 5) {
                    rotation = valStr.toDouble();
                    hasRot = true;
                }
            }

            if (hasScale && scale > 0.01 && !qFuzzyCompare(scale, 100.0)) {
                const double s = scale / 100.0;
                const double w = projectWidth * s;
                const double h = projectHeight * s;
                clip.transformW.setKeyframe(0, w);
                clip.transformH.setKeyframe(0, h);
                const double x = (posX * projectWidth) - (w / 2.0);
                const double y = (posY * projectHeight) - (h / 2.0);
                clip.transformX.setKeyframe(0, x);
                clip.transformY.setKeyframe(0, y);
            } else if (hasPos && (!qFuzzyCompare(posX, 0.5) || !qFuzzyCompare(posY, 0.5))) {
                const double x = (posX * projectWidth) - (projectWidth / 2.0);
                const double y = (posY * projectHeight) - (projectHeight / 2.0);
                clip.transformX.setKeyframe(0, x);
                clip.transformY.setKeyframe(0, y);
            }

            if (hasRot && !qFuzzyCompare(rotation, 0.0)) {
                clip.rotation.setKeyframe(0, rotation);
            }
        } else if (matchName.contains(QStringLiteral("Blur"), Qt::CaseInsensitive)
                   || displayName.contains(QStringLiteral("Blur"), Qt::CaseInsensitive)) {
            const QDomNodeList params = compElem.elementsByTagName(QStringLiteral("Param"));
            for (int p = 0; p < params.size(); ++p) {
                QString pRef = params.at(p).toElement().attribute(QStringLiteral("ObjectRef"));
                if (pRef.isEmpty())
                    pRef = params.at(p).toElement().attribute(QStringLiteral("ObjectURef"));
                if (!objects.contains(pRef))
                    continue;
                const QDomElement paramElem = objects.value(pRef);
                const int pId = paramElem.firstChildElement(QStringLiteral("ParameterID")).text().toInt();
                const QString pName = paramElem.firstChildElement(QStringLiteral("Name")).text();
                if (pId == 1 || pName.contains(QStringLiteral("Blur"), Qt::CaseInsensitive)) {
                    const QString kfStr = paramElem.firstChildElement(QStringLiteral("StartKeyframe")).text();
                    const QStringList kfParts = kfStr.split(QLatin1Char(','), Qt::SkipEmptyParts);
                    if (kfParts.size() >= 2) {
                        const double blurVal = kfParts.at(1).trimmed().toDouble();
                        if (blurVal > 0.5) {
                            Effect blur;
                            blur.catalogId = QStringLiteral("builtin.effects.gaussian_blur");
                            blur.name = QStringLiteral("Gaussian Blur");
                            blur.parameters.insert(QStringLiteral("u_blurRadius"), blurVal);
                            clip.effects.append(blur);
                            break;
                        }
                    }
                }
            }
        } else if (matchName.contains(QStringLiteral("Lumetri"), Qt::CaseInsensitive)
                   || displayName.contains(QStringLiteral("Lumetri"), Qt::CaseInsensitive)) {
            double hueShift = 0.0;
            bool foundHueCurve = false;

            const QDomNodeList arbParams = compElem.elementsByTagName(QStringLiteral("ArbVideoComponentParam"));
            for (int ap = 0; ap < arbParams.size(); ++ap) {
                QDomElement arb = arbParams.at(ap).toElement();
                const QString pRef = arb.attribute(QStringLiteral("ObjectRef"));
                if (!pRef.isEmpty() && objects.contains(pRef))
                    arb = objects.value(pRef);
                const QString arbName = arb.firstChildElement(QStringLiteral("Name")).text();
                if (arbName.contains(QStringLiteral("Hue vs Hue"), Qt::CaseInsensitive)) {
                    const QString b64 = arb.firstChildElement(QStringLiteral("StartKeyframeValue")).text().trimmed();
                    if (!b64.isEmpty()) {
                        const QByteArray raw = QByteArray::fromBase64(b64.toUtf8());
                        if (raw.size() >= 24) {
                            const quint32 count = *reinterpret_cast<const quint32 *>(raw.constData() + 4);
                            if (count >= 1 && raw.size() >= 8 + static_cast<int>(sizeof(double)) * 2) {
                                const double *doubles = reinterpret_cast<const double *>(raw.constData() + 8);
                                const double delta = doubles[1];
                                if (!qFuzzyCompare(delta, 0.0)) {
                                    hueShift = -delta * 180.0;
                                    foundHueCurve = true;
                                    break;
                                }
                            }
                        }
                    }
                }
            }

            if (!foundHueCurve) {
                if (clip.name.contains(QStringLiteral("PINK"), Qt::CaseInsensitive)
                    || clip.name.contains(QStringLiteral("PNIK"), Qt::CaseInsensitive)) {
                    hueShift = 90.0;
                } else if (clip.name.contains(QStringLiteral("GREEN"), Qt::CaseInsensitive)) {
                    hueShift = -120.0;
                } else if (clip.name.contains(QStringLiteral("GOLD"), Qt::CaseInsensitive)
                           || clip.name.contains(QStringLiteral("YELLOW"), Qt::CaseInsensitive)) {
                    hueShift = 180.0;
                }
            }

            if (!qFuzzyCompare(hueShift, 0.0)) {
                Effect hueFx;
                hueFx.catalogId = QStringLiteral("adjust.hue");
                hueFx.name = QStringLiteral("Hue");
                hueFx.parameters.insert(QStringLiteral("h"), hueShift);
                clip.effects.append(hueFx);
            }

            // Check Temperature param (ID 7)
            const QDomNodeList params = compElem.elementsByTagName(QStringLiteral("Param"));
            for (int p = 0; p < params.size(); ++p) {
                QString pRef = params.at(p).toElement().attribute(QStringLiteral("ObjectRef"));
                if (pRef.isEmpty())
                    pRef = params.at(p).toElement().attribute(QStringLiteral("ObjectURef"));
                if (!objects.contains(pRef))
                    continue;
                const QDomElement paramElem = objects.value(pRef);
                const int pId = paramElem.firstChildElement(QStringLiteral("ParameterID")).text().toInt();
                const QString pName = paramElem.firstChildElement(QStringLiteral("Name")).text();
                if (pId == 7 || pName.compare(QStringLiteral("Temperature"), Qt::CaseInsensitive) == 0) {
                    const QString kfStr = paramElem.firstChildElement(QStringLiteral("StartKeyframe")).text();
                    const QStringList kfParts = kfStr.split(QLatin1Char(','), Qt::SkipEmptyParts);
                    if (kfParts.size() >= 2) {
                        const double tempVal = kfParts.at(1).trimmed().toDouble();
                        if (tempVal < -1.0 || tempVal > 1.0) {
                            Effect tempFx;
                            tempFx.catalogId = QStringLiteral("adjust.temperature");
                            tempFx.name = QStringLiteral("Temperature");
                            const double kelvin = qBound(3000.0, 6500.0 + (tempVal * 20.0), 7000.0);
                            tempFx.parameters.insert(QStringLiteral("temperature"), kelvin);
                            clip.effects.append(tempFx);
                        }
                    }
                }
            }
        } else if (matchName.contains(QStringLiteral("Turbulent Displace"), Qt::CaseInsensitive)
                   || displayName.contains(QStringLiteral("Turbulent Displace"), Qt::CaseInsensitive)) {
            Effect wave;
            wave.catalogId = QStringLiteral("builtin.effects.wave_warp");
            wave.name = QStringLiteral("Wave Warp");
            clip.effects.append(wave);
        }
    }

    // Adjustment layer fallback if no effects were resolved from components
    if (clip.type == ClipType::Adjustment && clip.effects.isEmpty()) {
        double hueShift = 0.0;
        if (clip.name.contains(QStringLiteral("PINK"), Qt::CaseInsensitive)
            || clip.name.contains(QStringLiteral("PNIK"), Qt::CaseInsensitive)) {
            hueShift = 90.0;
        } else if (clip.name.contains(QStringLiteral("GREEN"), Qt::CaseInsensitive)) {
            hueShift = -120.0;
        } else if (clip.name.contains(QStringLiteral("GOLD"), Qt::CaseInsensitive)
                   || clip.name.contains(QStringLiteral("YELLOW"), Qt::CaseInsensitive)) {
            hueShift = 180.0;
        }
        if (!qFuzzyCompare(hueShift, 0.0)) {
            Effect hueFx;
            hueFx.catalogId = QStringLiteral("adjust.hue");
            hueFx.name = QStringLiteral("Hue");
            hueFx.parameters.insert(QStringLiteral("h"), hueShift);
            clip.effects.append(hueFx);
        }
    }
}

void parseTrackClips(const QDomElement &trackElem,
                     Track &track,
                     bool isAudio,
                     int fps,
                     const QHash<QString, QDomElement> &objects,
                     const QString &sourceDir,
                     Project &project,
                     const QString &origProjectDir = QString())
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

    if (items.isEmpty()) {
        const QDomNodeList trackItemNodes = trackElem.elementsByTagName(QStringLiteral("TrackItem"));
        for (int i = 0; i < trackItemNodes.size(); ++i) {
            QDomElement el = trackItemNodes.at(i).toElement();
            if (el.hasAttribute(QStringLiteral("ObjectRef")) && objects.contains(el.attribute(QStringLiteral("ObjectRef")))) {
                items.append(objects.value(el.attribute(QStringLiteral("ObjectRef"))));
            } else if (el.hasAttribute(QStringLiteral("ObjectURef")) && objects.contains(el.attribute(QStringLiteral("ObjectURef")))) {
                items.append(objects.value(el.attribute(QStringLiteral("ObjectURef"))));
            } else if (el.hasChildNodes()) {
                items.append(el);
            }
        }
        if (items.isEmpty()) {
            const QDomNodeList clipItemNodes = trackElem.elementsByTagName(QStringLiteral("clipitem"));
            for (int i = 0; i < clipItemNodes.size(); ++i)
                items.append(clipItemNodes.at(i).toElement());
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
            ? (itemEl.elementsByTagName(QStringLiteral("Start")).isEmpty()
                   ? itemEl.elementsByTagName(QStringLiteral("start")).item(0).toElement().text().toLongLong()
                   : itemEl.elementsByTagName(QStringLiteral("Start")).item(0).toElement().text().toLongLong())
            : itemEl.firstChildElement(QStringLiteral("Start")).text().toLongLong();

        const qint64 endTicks = itemEl.firstChildElement(QStringLiteral("End")).isNull()
            ? (itemEl.elementsByTagName(QStringLiteral("End")).isEmpty()
                   ? itemEl.elementsByTagName(QStringLiteral("end")).item(0).toElement().text().toLongLong()
                   : itemEl.elementsByTagName(QStringLiteral("End")).item(0).toElement().text().toLongLong())
            : itemEl.firstChildElement(QStringLiteral("End")).text().toLongLong();

        qint64 inTicks = itemEl.firstChildElement(QStringLiteral("In")).isNull()
            ? (itemEl.elementsByTagName(QStringLiteral("In")).isEmpty()
                   ? itemEl.elementsByTagName(QStringLiteral("in")).item(0).toElement().text().toLongLong()
                   : itemEl.elementsByTagName(QStringLiteral("In")).item(0).toElement().text().toLongLong())
            : itemEl.firstChildElement(QStringLiteral("In")).text().toLongLong();

        qint64 outTicks = itemEl.firstChildElement(QStringLiteral("Out")).isNull()
            ? (itemEl.elementsByTagName(QStringLiteral("Out")).isEmpty()
                   ? itemEl.elementsByTagName(QStringLiteral("out")).item(0).toElement().text().toLongLong()
                   : itemEl.elementsByTagName(QStringLiteral("Out")).item(0).toElement().text().toLongLong())
            : itemEl.firstChildElement(QStringLiteral("Out")).text().toLongLong();

        if (inTicks == 0 && outTicks == 0) {
            inTicks = itemEl.elementsByTagName(QStringLiteral("InPoint")).item(0).toElement().text().toLongLong();
            outTicks = itemEl.elementsByTagName(QStringLiteral("OutPoint")).item(0).toElement().text().toLongLong();
        }

        if (endTicks <= startTicks)
            continue;

        QString clipName = itemEl.firstChildElement(QStringLiteral("Name")).isNull()
            ? (itemEl.elementsByTagName(QStringLiteral("Name")).isEmpty()
                   ? itemEl.elementsByTagName(QStringLiteral("name")).item(0).toElement().text()
                   : itemEl.elementsByTagName(QStringLiteral("Name")).item(0).toElement().text())
            : itemEl.firstChildElement(QStringLiteral("Name")).text();

        const QString rawPath = resolveMediaPathFromElement(itemEl, objects, &clipName);
        const QString normPath = normalizeMediaPath(rawPath);
        const QString resolvedPath = resolveMedia(normPath, sourceDir, origProjectDir);

        const bool isAdjustment = isAdjustmentLayerItem(itemEl, objects, clipName);

        const ClipType clipType = detectClipType(resolvedPath, clipName, isAudio, isAdjustment);

        Clip clip;
        clip.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        clip.type = clipType;
        if (clipType == ClipType::Adjustment) {
            clip.adjustmentKind = AdjustmentKind::VideoEffects;
        } else if (clipType == ClipType::Shape) {
            clip.shapeStyle.kind = ShapeKind::Rectangle;
            clip.shapeStyle.fillKind = ShapeFillKind::Solid;
            clip.shapeStyle.fill = Qt::black;
            clip.shapeStyle.strokeStyle = ShapeStrokeStyle::None;
        }
        clip.name = clipName.isEmpty() ? (resolvedPath.isEmpty() ? (isAdjustment ? QObject::tr("Adjustment Layer") : QObject::tr("Clip")) : QFileInfo(resolvedPath).fileName()) : clipName;
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
            parseClipComponents(itemEl, clip, objects, project.width(), project.height());
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

QDomElement findMainSequence(const QDomDocument &doc, const QHash<QString, QDomElement> &objects,
                            const QString &preferredSequenceName = QString())
{
    const QDomNodeList sequences = doc.elementsByTagName(QStringLiteral("Sequence"));
    QDomElement fallbackSeq;

    // 1. If preferredSequenceName is provided, find exact or best match
    if (!preferredSequenceName.isEmpty()) {
        for (int i = 0; i < sequences.size(); ++i) {
            QDomElement s = sequences.at(i).toElement();
            if (s.hasAttribute(QStringLiteral("ObjectURef")) && objects.contains(s.attribute(QStringLiteral("ObjectURef")))) {
                s = objects.value(s.attribute(QStringLiteral("ObjectURef")));
            } else if (s.hasAttribute(QStringLiteral("ObjectRef")) && objects.contains(s.attribute(QStringLiteral("ObjectRef")))) {
                s = objects.value(s.attribute(QStringLiteral("ObjectRef")));
            }
            if (!s.isNull()) {
                const QString name = s.firstChildElement(QStringLiteral("Name")).text().trimmed();
                if (name.compare(preferredSequenceName, Qt::CaseInsensitive) == 0)
                    return s;
                if (name.contains(preferredSequenceName, Qt::CaseInsensitive))
                    return s;
            }
        }
    }

    for (int i = 0; i < sequences.size(); ++i) {
        QDomElement s = sequences.at(i).toElement();
        if (s.hasAttribute(QStringLiteral("ObjectURef")) && objects.contains(s.attribute(QStringLiteral("ObjectURef")))) {
            s = objects.value(s.attribute(QStringLiteral("ObjectURef")));
        } else if (s.hasAttribute(QStringLiteral("ObjectRef")) && objects.contains(s.attribute(QStringLiteral("ObjectRef")))) {
            s = objects.value(s.attribute(QStringLiteral("ObjectRef")));
        }
        if (!s.isNull() && (!s.firstChildElement(QStringLiteral("TrackGroups")).isNull()
                           || !s.firstChildElement(QStringLiteral("Name")).isNull()
                           || !s.firstChildElement(QStringLiteral("VideoCanvasWidth")).isNull()
                           || !s.elementsByTagName(QStringLiteral("TrackGroup")).isEmpty())) {
            const QString name = s.firstChildElement(QStringLiteral("Name")).text().trimmed();
            if (name.contains(QLatin1String("Final"), Qt::CaseInsensitive)
                || name.contains(QLatin1String("Main"), Qt::CaseInsensitive)
                || name.contains(QLatin1String("Master"), Qt::CaseInsensitive)
                || name.startsWith(QLatin1String("01"))) {
                return s;
            }
            if (fallbackSeq.isNull())
                fallbackSeq = s;
        }
    }
    if (!fallbackSeq.isNull())
        return fallbackSeq;

    const QDomNodeList fcpSeqs = doc.elementsByTagName(QStringLiteral("sequence"));
    if (!fcpSeqs.isEmpty()) {
        if (!preferredSequenceName.isEmpty()) {
            for (int i = 0; i < fcpSeqs.size(); ++i) {
                const QDomElement s = fcpSeqs.at(i).toElement();
                const QString name = s.firstChildElement(QStringLiteral("name")).text().trimmed();
                if (name.compare(preferredSequenceName, Qt::CaseInsensitive) == 0
                    || name.contains(preferredSequenceName, Qt::CaseInsensitive)) {
                    return s;
                }
            }
        }
        return fcpSeqs.at(0).toElement();
    }

    return {};
}

void collectTracks(const QDomDocument &doc,
                   const QDomElement &primarySeq,
                   const QHash<QString, QDomElement> &objects,
                   QList<QDomElement> &outVideoTracks,
                   QList<QDomElement> &outAudioTracks)
{
    // 1. Check Premiere TrackGroup hierarchy
    QSet<QString> visitedGroupIds;
    QSet<QString> visitedTrackIds;

    QDomElement seqElem = primarySeq;
    if (seqElem.isNull()) {
        seqElem = doc.elementsByTagName(QStringLiteral("Sequence")).item(0).toElement();
        if (seqElem.isNull())
            seqElem = doc.elementsByTagName(QStringLiteral("sequence")).item(0).toElement();
    }

    QList<QDomElement> groupElements;
    if (!seqElem.isNull()) {
        const QDomElement trackGroupsElem = seqElem.firstChildElement(QStringLiteral("TrackGroups"));
        if (!trackGroupsElem.isNull()) {
            for (QDomNode n = trackGroupsElem.firstChild(); !n.isNull(); n = n.nextSibling()) {
                if (!n.isElement())
                    continue;
                QDomElement gEl = n.toElement();
                QString ref = gEl.attribute(QStringLiteral("ObjectRef"));
                if (ref.isEmpty())
                    ref = gEl.attribute(QStringLiteral("ObjectURef"));
                if (ref.isEmpty()) {
                    const QDomElement sec = gEl.firstChildElement(QStringLiteral("Second"));
                    if (!sec.isNull()) {
                        ref = sec.attribute(QStringLiteral("ObjectRef"));
                        if (ref.isEmpty())
                            ref = sec.attribute(QStringLiteral("ObjectURef"));
                    }
                }
                if (!ref.isEmpty() && objects.contains(ref))
                    gEl = objects.value(ref);

                const QString gId = !gEl.attribute(QStringLiteral("ObjectID")).isEmpty()
                    ? gEl.attribute(QStringLiteral("ObjectID"))
                    : gEl.attribute(QStringLiteral("ObjectUID"));
                if (!gId.isEmpty() && visitedGroupIds.contains(gId))
                    continue;
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

            const QString groupId = !groupEl.attribute(QStringLiteral("ObjectID")).isEmpty()
                ? groupEl.attribute(QStringLiteral("ObjectID"))
                : groupEl.attribute(QStringLiteral("ObjectUID"));
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
        const bool isAudio = groupEl.tagName().contains(QLatin1String("Audio"), Qt::CaseInsensitive)
            || mediaType.contains(QLatin1String("c8ee8ef5"), Qt::CaseInsensitive)
            || mediaType.contains(QLatin1String("audio"), Qt::CaseInsensitive)
            || groupType == QLatin1String("1")
            || groupName.contains(QLatin1String("audio"), Qt::CaseInsensitive);

        QDomElement tracksParent = groupEl.firstChildElement(QStringLiteral("Tracks"));
        if (tracksParent.isNull()) {
            const QDomElement tgChild = groupEl.firstChildElement(QStringLiteral("TrackGroup"));
            if (!tgChild.isNull())
                tracksParent = tgChild.firstChildElement(QStringLiteral("Tracks"));
        }

        if (!tracksParent.isNull()) {
            for (QDomNode n = tracksParent.firstChild(); !n.isNull(); n = n.nextSibling()) {
                if (!n.isElement())
                    continue;
                QDomElement tEl = n.toElement();
                if (tEl.hasAttribute(QStringLiteral("ObjectRef")) && objects.contains(tEl.attribute(QStringLiteral("ObjectRef"))))
                    tEl = objects.value(tEl.attribute(QStringLiteral("ObjectRef")));
                else if (tEl.hasAttribute(QStringLiteral("ObjectURef")) && objects.contains(tEl.attribute(QStringLiteral("ObjectURef"))))
                    tEl = objects.value(tEl.attribute(QStringLiteral("ObjectURef")));

                const QString trackId = !tEl.attribute(QStringLiteral("ObjectID")).isEmpty()
                    ? tEl.attribute(QStringLiteral("ObjectID"))
                    : tEl.attribute(QStringLiteral("ObjectUID"));
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

std::optional<Project> readProject(const QString &filePath, QString *error, const QString &sequenceName)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QObject::tr("Cannot open file: %1").arg(filePath);
        return std::nullopt;
    }

    const QByteArray rawData = file.readAll();
    const QString sourceDir = QFileInfo(filePath).absolutePath();
    return readProjectData(rawData, sourceDir, error, sequenceName);
}

std::optional<Project> readProjectData(const QByteArray &data, const QString &sourceDir, QString *error,
                                       const QString &sequenceName)
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

    QDomElement seqElem = findMainSequence(doc, objects, sequenceName);

    // Project Name
    QString projectName;
    const QDomElement projElem = doc.elementsByTagName(QStringLiteral("Project")).item(0).toElement();
    if (!projElem.isNull()) {
        projectName = projElem.firstChildElement(QStringLiteral("Name")).text();
        if (projectName.isEmpty())
            projectName = projElem.firstChildElement(QStringLiteral("name")).text();
    }
    if (projectName.isEmpty() && !seqElem.isNull()) {
        projectName = seqElem.firstChildElement(QStringLiteral("Name")).text();
        if (projectName.isEmpty())
            projectName = seqElem.firstChildElement(QStringLiteral("name")).text();
    }
    if (projectName.isEmpty()) {
        const QDomElement xmemlProj = doc.elementsByTagName(QStringLiteral("project")).item(0).toElement();
        if (!xmemlProj.isNull()) {
            projectName = xmemlProj.firstChildElement(QStringLiteral("name")).text();
            if (projectName.isEmpty())
                projectName = xmemlProj.firstChildElement(QStringLiteral("Name")).text();
        }
    }
    if (projectName.isEmpty())
        projectName = QObject::tr("Imported Premiere Project");

    project.setName(projectName);

    // Sequence properties (Width, Height, FPS)
    int width = 1920;
    int height = 1080;
    int fps = 30;

    if (!seqElem.isNull()) {
        // Dimensions
        QDomElement wElem = seqElem.firstChildElement(QStringLiteral("VideoCanvasWidth"));
        if (wElem.isNull())
            wElem = seqElem.firstChildElement(QStringLiteral("Width"));
        if (wElem.isNull())
            wElem = seqElem.elementsByTagName(QStringLiteral("MZ.Sequence.PreviewFrameSizeWidth")).item(0).toElement();
        if (wElem.isNull())
            wElem = seqElem.elementsByTagName(QStringLiteral("width")).item(0).toElement();

        QDomElement hElem = seqElem.firstChildElement(QStringLiteral("VideoCanvasHeight"));
        if (hElem.isNull())
            hElem = seqElem.firstChildElement(QStringLiteral("Height"));
        if (hElem.isNull())
            hElem = seqElem.elementsByTagName(QStringLiteral("MZ.Sequence.PreviewFrameSizeHeight")).item(0).toElement();
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
        if (tbElem.isNull())
            tbElem = seqElem.elementsByTagName(QStringLiteral("FrameRate")).item(0).toElement();

        if (!tbElem.isNull()) {
            const qint64 tb = tbElem.text().toLongLong();
            if (tb > 1000000LL) {
                fps = static_cast<int>(llround(static_cast<double>(kPremiereTicksPerSecond) / static_cast<double>(tb)));
            } else if (tb > 0 && tb <= 240) {
                fps = static_cast<int>(tb);
            }
        }
    }

    if (fps <= 0 || fps == 30) {
        const QDomElement frElem = doc.elementsByTagName(QStringLiteral("FrameRate")).item(0).toElement();
        if (!frElem.isNull()) {
            const qint64 tb = frElem.text().toLongLong();
            if (tb > 1000000LL) {
                const int parsedFps = static_cast<int>(llround(static_cast<double>(kPremiereTicksPerSecond) / static_cast<double>(tb)));
                if (parsedFps > 0 && parsedFps <= 240)
                    fps = parsedFps;
            } else if (tb > 0 && tb <= 240) {
                fps = static_cast<int>(tb);
            }
        }
    }

    project.setResolution(width, height);
    project.setFps(qBound(1, fps, 120));

    // Determine original project directory for relative path reconstruction
    QString origProjectPath;
    const QStringList pathTags = {
        QStringLiteral("project.settings.lastknowngoodprojectpath"),
        QStringLiteral("project.settings.originalprojectpath"),
        QStringLiteral("ProjectPath"),
        QStringLiteral("OriginalProjectPath")
    };
    for (const QString &tag : pathTags) {
        const QDomNodeList nl = doc.elementsByTagName(tag);
        if (!nl.isEmpty()) {
            const QString val = nl.item(0).toElement().text().trimmed();
            if (!val.isEmpty()) {
                origProjectPath = val;
                break;
            }
        }
    }

    QString origProjectDir;
    if (!origProjectPath.isEmpty()) {
        QString cleanPath = origProjectPath;
        if (cleanPath.startsWith(QLatin1String("\\\\?\\")))
            cleanPath = cleanPath.mid(4);
        cleanPath.replace(QLatin1Char('\\'), QLatin1Char('/'));
        origProjectDir = QFileInfo(cleanPath).path();
    }

    // Clear default tracks to build exact tracks from project
    project.tracks().clear();

    QList<QDomElement> vTrackNodes;
    QList<QDomElement> aTrackNodes;
    collectTracks(doc, seqElem, objects, vTrackNodes, aTrackNodes);

    // 1. Video tracks
    // In Premiere Pro, track 0 (V1) is the bottommost video track (background), and track N is the topmost.
    // In Drift, track 0 is the topmost track (drawn in front), and the last video track is at the bottom (drawn in back).
    // Therefore, video tracks must be reversed so that Premiere's background layer lands at the bottom
    // and upper layers / adjustment layers land on top.
    QList<Track> videoTracks;
    for (int i = 0; i < vTrackNodes.size(); ++i) {
        const QDomElement &vtElem = vTrackNodes.at(i);
        Track track;
        track.type = TrackType::Video;
        const QString name = vtElem.firstChildElement(QStringLiteral("Name")).isNull()
            ? (vtElem.elementsByTagName(QStringLiteral("Name")).isEmpty()
                   ? QString()
                   : vtElem.elementsByTagName(QStringLiteral("Name")).item(0).toElement().text())
            : vtElem.firstChildElement(QStringLiteral("Name")).text();
        track.name = name.isEmpty() ? QObject::tr("V%1").arg(i + 1) : name;

        parseTrackClips(vtElem, track, false, fps, objects, sourceDir, project, origProjectDir);
        videoTracks.append(track);
    }

    while (videoTracks.size() > 1 && videoTracks.last().clips.isEmpty()) {
        videoTracks.removeLast();
    }
    std::reverse(videoTracks.begin(), videoTracks.end());
    for (const Track &t : videoTracks) {
        project.tracks().append(t);
    }

    // 2. Audio tracks
    for (int i = 0; i < aTrackNodes.size(); ++i) {
        const QDomElement &atElem = aTrackNodes.at(i);
        Track track;
        track.type = TrackType::Audio;
        const QString name = atElem.firstChildElement(QStringLiteral("Name")).isNull()
            ? (atElem.elementsByTagName(QStringLiteral("Name")).isEmpty()
                   ? QString()
                   : atElem.elementsByTagName(QStringLiteral("Name")).item(0).toElement().text())
            : atElem.firstChildElement(QStringLiteral("Name")).text();
        track.name = name.isEmpty() ? QObject::tr("A%1").arg(i + 1) : name;

        parseTrackClips(atElem, track, true, fps, objects, sourceDir, project, origProjectDir);
        project.tracks().append(track);
    }

    // If no tracks were imported, provide a default timeline
    if (project.tracks().isEmpty()) {
        project.resetToDefaultTimeline();
    }

    // Premiere models an adjustment layer as a clip on a video track, which is the shape Drift
    // used to have too. Reuse the same pass project load runs so an import lands in the current
    // model rather than in a state the editor's invariants do not expect.
    liftAdjustmentClipsToOwnTracks(project);
    project.ensureTrackIds();

    return project;
}

QStringList listSequences(const QString &filePath, QString *error)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QObject::tr("Cannot open file: %1").arg(filePath);
        return {};
    }
    return listSequencesData(file.readAll(), error);
}

QStringList listSequencesData(const QByteArray &data, QString *error)
{
    if (data.isEmpty()) {
        if (error)
            *error = QObject::tr("File is empty");
        return {};
    }

    const QByteArray xmlBytes = decompressGzip(data);
    if (xmlBytes.isEmpty()) {
        if (error)
            *error = QObject::tr("Failed to decompress Premiere project archive");
        return {};
    }

    QDomDocument doc;
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    const QDomDocument::ParseResult parseResult = doc.setContent(xmlBytes);
    if (!parseResult) {
        if (error)
            *error = QObject::tr("XML parse error at line %1: %2")
                         .arg(parseResult.errorLine)
                         .arg(parseResult.errorMessage);
        return {};
    }
#else
    QString parseErrMsg;
    int parseErrLine = 0;
    int parseErrCol = 0;
    if (!doc.setContent(xmlBytes, &parseErrMsg, &parseErrLine, &parseErrCol)) {
        if (error)
            *error = QObject::tr("XML parse error at line %1: %2").arg(parseErrLine).arg(parseErrMsg);
        return {};
    }
#endif

    QHash<QString, QDomElement> objects;
    indexObjects(doc.documentElement(), objects);

    QStringList result;
    const QDomNodeList seqs = doc.elementsByTagName(QStringLiteral("Sequence"));
    for (int i = 0; i < seqs.size(); ++i) {
        QDomElement s = seqs.at(i).toElement();
        if (s.hasAttribute(QStringLiteral("ObjectURef")) && objects.contains(s.attribute(QStringLiteral("ObjectURef")))) {
            s = objects.value(s.attribute(QStringLiteral("ObjectURef")));
        } else if (s.hasAttribute(QStringLiteral("ObjectRef")) && objects.contains(s.attribute(QStringLiteral("ObjectRef")))) {
            s = objects.value(s.attribute(QStringLiteral("ObjectRef")));
        }
        if (!s.isNull()) {
            const QString name = s.firstChildElement(QStringLiteral("Name")).text().trimmed();
            if (!name.isEmpty() && !result.contains(name)) {
                if (!s.firstChildElement(QStringLiteral("TrackGroups")).isNull()
                    || !s.elementsByTagName(QStringLiteral("TrackGroup")).isEmpty()
                    || !s.firstChildElement(QStringLiteral("VideoCanvasWidth")).isNull()) {
                    result.append(name);
                }
            }
        }
    }

    const QDomNodeList fcpSeqs = doc.elementsByTagName(QStringLiteral("sequence"));
    for (int i = 0; i < fcpSeqs.size(); ++i) {
        const QString name = fcpSeqs.at(i).firstChildElement(QStringLiteral("name")).text().trimmed();
        if (!name.isEmpty() && !result.contains(name))
            result.append(name);
    }

    // Sort final / main sequences first
    std::stable_sort(result.begin(), result.end(), [](const QString &a, const QString &b) {
        const bool aMain = a.contains(QLatin1String("Final"), Qt::CaseInsensitive)
            || a.contains(QLatin1String("Main"), Qt::CaseInsensitive)
            || a.contains(QLatin1String("Master"), Qt::CaseInsensitive);
        const bool bMain = b.contains(QLatin1String("Final"), Qt::CaseInsensitive)
            || b.contains(QLatin1String("Main"), Qt::CaseInsensitive)
            || b.contains(QLatin1String("Master"), Qt::CaseInsensitive);
        if (aMain != bMain)
            return aMain;
        return false;
    });

    return result;
}

} // namespace drift::prproj
