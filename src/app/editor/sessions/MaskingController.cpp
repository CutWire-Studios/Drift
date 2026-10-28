#include "app/editor/sessions/MaskingController.h"

#include "app/bin/AssetLibrary.h"
#include "app/editor/AppController.h"
#include "app/editor/sessions/MaskEditSession.h"
#include "app/editor/sessions/MaskStackModel.h"
#include "core/edit/TimelineOps.h"
#include "core/effects/Mask.h"
#include "core/project/Project.h"
#include "core/project/Track.h"
#include "engine/encode/writers/MatteWriter.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QList>
#include <QPair>
#include <QScopeGuard>
#include <QSet>
#include <QStringList>
#include <QUuid>
#include <QtConcurrent>

#include <algorithm>
#include <cmath>
#include <utility>

namespace {

// Where a media mask came from, so the panel offers "refine" only on an AI object matte: "brush"
// (painted PNG), "people" (RVM cutout, the only backend writing a foreground pass), "object" (a
// SAM2 matte video in the matte cache) or "file" (anything the user pointed at).
QString mediaSourceKind(const drift::Mask &mask)
{
    if (!mask.isMedia())
        return {};
    const QString mattes = QDir(drift::matteCacheDir()).absolutePath() + QLatin1Char('/');
    if (!QFileInfo(mask.mediaPath).absoluteFilePath().startsWith(mattes))
        return QStringLiteral("file");
    if (mask.mediaPath.endsWith(QStringLiteral(".png")))
        return QStringLiteral("brush");
    if (!mask.mediaFgrPath.isEmpty())
        return QStringLiteral("people");
    return QStringLiteral("object");
}


drift::Mask mediaMask(const QString &path, drift::TimeUs srcOffsetUs, drift::MaskMediaChannel channel,
                      const QString &name)
{
    drift::Mask mask = drift::fullFrameMediaMask(path, srcOffsetUs);
    mask.mediaChannel = channel;
    mask.mediaTimeBase = drift::MaskMediaTimeBase::Adjustment;
    mask.name = name;
    return mask;
}

// The picture a matte on `trackIndex` acts on: the nearest video track below it, i.e. the next
// higher index.
int trackBelow(const drift::Project &project, int trackIndex)
{
    const QList<drift::Track> &tracks = project.tracks();
    for (int t = trackIndex + 1; t < tracks.size(); ++t) {
        if (tracks.at(t).type == drift::TrackType::Video)
            return t;
    }
    return -1;
}

// AppController::deleteSelectedClip's removal, minus the ripple: the matte takes the span the clip
// held, so the clips after it must stay where they are.
void removeClipWithPartners(drift::Project &project, int trackIndex, int clipIndex)
{
    QList<QPair<int, int>> pairs{{trackIndex, clipIndex}};
    const drift::Clip &clip = project.tracks().at(trackIndex).clips.at(clipIndex);
    for (const drift::ClipRef &ref : drift::linkedPartners(project, clip)) {
        const QPair<int, int> partner(ref.trackIndex, ref.clipIndex);
        if (!pairs.contains(partner))
            pairs.append(partner);
    }
    std::sort(pairs.begin(), pairs.end(), [](const QPair<int, int> &a, const QPair<int, int> &b) {
        if (a.first != b.first)
            return a.first > b.first;
        return a.second > b.second;
    });

    QSet<QString> removedClipIds;
    for (const QPair<int, int> &pair : std::as_const(pairs)) {
        removedClipIds.insert(project.tracks().at(pair.first).clips.at(pair.second).id);
        project.tracks()[pair.first].clips.removeAt(pair.second);
    }
    for (drift::Track &track : project.tracks()) {
        for (int i = track.transitions.size() - 1; i >= 0; --i) {
            const drift::Transition &transition = track.transitions.at(i);
            if (removedClipIds.contains(transition.fromClipId) || removedClipIds.contains(transition.toClipId))
                track.transitions.removeAt(i);
        }
    }
}

QVector<QPointF> verticesFrom(const QVariantList &points)
{
    QVector<QPointF> vertices;
    vertices.reserve(points.size());
    for (const QVariant &point : points) {
        const QVariantMap map = point.toMap();
        vertices.append(QPointF(map.value(QStringLiteral("x")).toDouble(),
                                map.value(QStringLiteral("y")).toDouble()));
    }
    return vertices;
}

// Writes `vertices` as the key at mask time `maskUs`. A key within half a frame of it is the same
// key, so it is replaced rather than duplicated.
void writePathKey(drift::Mask &mask, drift::TimeUs maskUs, drift::TimeUs halfFrameUs,
                  const QVector<QPointF> &vertices)
{
    const auto candidate = mask.pathKeys.lowerBound(maskUs - halfFrameUs);
    if (candidate != mask.pathKeys.end() && qAbs(candidate.key() - maskUs) <= halfFrameUs)
        candidate.value() = vertices;
    else
        mask.pathKeys.insert(maskUs, vertices);
    if (mask.pathKeys.size() == 1)
        mask.points = vertices;
}

const QStringList kMaskKeys{QStringLiteral("name"), QStringLiteral("op"), QStringLiteral("invert"),
                            QStringLiteral("enabled"), QStringLiteral("feather"),
                            QStringLiteral("mediaChannel")};

QVariantMap targetMap(const QString &kind, int trackIndex, int clipIndex, const QString &name)
{
    return QVariantMap{{QStringLiteral("kind"), kind},
                       {QStringLiteral("track"), trackIndex},
                       {QStringLiteral("clip"), clipIndex},
                       {QStringLiteral("name"), name}};
}

} // namespace

namespace drift::masking {

void paintBrushStroke(QImage &image, const QVector<QPointF> &pixelPoints, double radiusPx, double hardness,
                      double opacity, bool erase)
{
    paintBrushStroke(image, pixelPoints, QVector<double>(pixelPoints.size(), radiusPx), hardness, opacity, erase);
}

void paintBrushStroke(QImage &image, const QVector<QPointF> &pixelPoints, const QVector<double> &radiiPx,
                      double hardness, double opacity, bool erase)
{
    if (image.isNull() || pixelPoints.isEmpty() || radiiPx.size() != pixelPoints.size())
        return;
    if (image.format() != QImage::Format_Grayscale8)
        image = image.convertToFormat(QImage::Format_Grayscale8);

    // Dabs accumulate by max into a per-stroke coverage map, so overlapping dabs never build past
    // the stroke's own shape; opacity is applied once when the map is composited.
    QImage coverage(image.size(), QImage::Format_Grayscale8);
    coverage.fill(0);
    // Below 1 so the falloff keeps a ramp at full hardness.
    const double solidStop = std::clamp(hardness, 0.0, 0.99);
    const int width = image.width();
    const int height = image.height();

    int minX = width;
    int minY = height;
    int maxX = -1;
    int maxY = -1;
    const auto dab = [&](const QPointF &centre, double radiusPx) {
        if (radiusPx <= 0)
            return;
        const int x0 = std::max(0, static_cast<int>(std::floor(centre.x() - radiusPx)));
        const int x1 = std::min(width - 1, static_cast<int>(std::ceil(centre.x() + radiusPx)));
        const int y0 = std::max(0, static_cast<int>(std::floor(centre.y() - radiusPx)));
        const int y1 = std::min(height - 1, static_cast<int>(std::ceil(centre.y() + radiusPx)));
        if (x0 > x1 || y0 > y1)
            return;
        minX = std::min(minX, x0);
        maxX = std::max(maxX, x1);
        minY = std::min(minY, y0);
        maxY = std::max(maxY, y1);
        for (int y = y0; y <= y1; ++y) {
            uchar *row = coverage.scanLine(y);
            const double dy = y + 0.5 - centre.y();
            for (int x = x0; x <= x1; ++x) {
                const double dx = x + 0.5 - centre.x();
                const double distance = std::hypot(dx, dy);
                const double d = distance / radiusPx;
                if (d >= 1.0)
                    continue;
                double v = d <= solidStop ? 1.0 : 1.0 - (d - solidStop) / (1.0 - solidStop);
                v *= std::clamp(radiusPx - distance + 0.5, 0.0, 1.0);
                const uchar value = static_cast<uchar>(std::lround(v * 255.0));
                if (value > row[x])
                    row[x] = value;
            }
        }
    };

    dab(pixelPoints.constFirst(), radiiPx.constFirst());
    for (int i = 1; i < pixelPoints.size(); ++i) {
        const QPointF from = pixelPoints.at(i - 1);
        const QPointF to = pixelPoints.at(i);
        const double r0 = radiiPx.at(i - 1);
        const double r1 = radiiPx.at(i);
        const double spacing = std::max(1.0, std::min(r0, r1) * 0.25);
        const double length = std::hypot(to.x() - from.x(), to.y() - from.y());
        const int steps = std::max(1, static_cast<int>(std::ceil(length / spacing)));
        for (int s = 1; s <= steps; ++s) {
            const double t = static_cast<double>(s) / steps;
            dab(from + (to - from) * t, r0 + (r1 - r0) * t);
        }
    }
    if (maxX < 0)
        return;

    const double strength = std::clamp(opacity, 0.0, 1.0) / 255.0;
    for (int y = minY; y <= maxY; ++y) {
        const uchar *src = coverage.constScanLine(y);
        uchar *dst = image.scanLine(y);
        for (int x = minX; x <= maxX; ++x) {
            if (src[x] == 0)
                continue;
            const double a = src[x] * strength;
            dst[x] = static_cast<uchar>(std::lround(erase ? dst[x] * (1.0 - a) : dst[x] + (255 - dst[x]) * a));
        }
    }
}

} // namespace drift::masking

MaskingController::MaskingController(AppController *app, QObject *parent)
    : QObject(parent)
    , m_app(app)
{
    m_brushPool.setMaxThreadCount(1);
    m_editSession = new MaskEditSession(app, this);
    m_stackModel = new MaskStackModel(app, this);
}

MaskingController::~MaskingController()
{
    m_brushPool.waitForDone();
}

void MaskingController::setBrushSize(double size)
{
    const double clamped = std::clamp(size, 0.002, 0.5);
    if (clamped == m_brushSize)
        return;
    m_brushSize = clamped;
    emit brushSettingsChanged();
}

void MaskingController::setBrushHardness(double hardness)
{
    const double clamped = std::clamp(hardness, 0.0, 1.0);
    if (clamped == m_brushHardness)
        return;
    m_brushHardness = clamped;
    emit brushSettingsChanged();
}

void MaskingController::setBrushOpacity(double opacity)
{
    const double clamped = std::clamp(opacity, 0.05, 1.0);
    if (clamped == m_brushOpacity)
        return;
    m_brushOpacity = clamped;
    emit brushSettingsChanged();
}

void MaskingController::setBrushPressureSize(bool on)
{
    if (on == m_brushPressureSize)
        return;
    m_brushPressureSize = on;
    emit brushSettingsChanged();
}

void MaskingController::setBrushErase(bool erase)
{
    if (erase == m_brushErase)
        return;
    m_brushErase = erase;
    emit brushSettingsChanged();
}

const drift::MediaAsset *MaskingController::assetAt(int assetIndex) const
{
    const QString id = m_app->m_assetLibrary ? m_app->m_assetLibrary->assetIdAt(assetIndex) : QString{};
    return id.isEmpty() ? nullptr : m_app->m_project.asset(id);
}

QString MaskingController::assetMaskRole(int assetIndex) const
{
    const drift::MediaAsset *asset = assetAt(assetIndex);
    return asset ? asset->maskRole : QString{};
}

void MaskingController::setAssetMaskRole(int assetIndex, const QString &role)
{
    const bool knownRole = role.isEmpty() || role == QLatin1String("luma") || role == QLatin1String("alpha");
    if (!m_app->m_assetLibrary || !knownRole)
        return;

    const drift::Project before = m_app->m_project.detachedCopy();
    if (!m_app->m_assetLibrary->setAssetMaskRole(assetIndex, role))
        return;

    m_app->pushProjectEdit(before, tr("Use as mask"));
    m_app->finishEdit(tr("Mask source updated"));
}

void MaskingController::addAssetMaskToClip(int trackIndex, int clipIndex, int assetIndex)
{
    const drift::MediaAsset *asset = assetAt(assetIndex);
    if (!asset)
        return;

    const drift::Mask mask = mediaMask(asset->path, 0,
                                       asset->maskRole == QLatin1String("alpha")
                                           ? drift::MaskMediaChannel::Alpha
                                           : drift::MaskMediaChannel::Luma,
                                       asset->name);
    const drift::Project before = m_app->m_project;
    const drift::ClipRef added = drift::addLinkedMask(m_app->m_project, trackIndex, clipIndex, mask);
    if (added.trackIndex < 0)
        return;
    const QString addedId = m_app->m_project.tracks().at(added.trackIndex).clips.at(added.clipIndex).id;

    m_app->pushProjectEdit(before, tr("Add mask"));
    m_app->finishEdit(tr("Mask added"));
    m_app->selectClipById(addedId);
}

void MaskingController::addAssetLaneMask(int trackIndex, int assetIndex, double atSeconds)
{
    const drift::MediaAsset *asset = assetAt(assetIndex);
    if (!asset)
        return;

    const drift::Mask mask = mediaMask(asset->path, 0,
                                       asset->maskRole == QLatin1String("alpha")
                                           ? drift::MaskMediaChannel::Alpha
                                           : drift::MaskMediaChannel::Luma,
                                       asset->name);
    const drift::Project before = m_app->m_project;
    const drift::ClipRef added = drift::addLaneMask(m_app->m_project, trackIndex, mask,
                                                    drift::secondsToUs(atSeconds),
                                                    assetMaskDurationUs(assetIndex));
    if (added.trackIndex < 0)
        return;
    const QString addedId = m_app->m_project.tracks().at(added.trackIndex).clips.at(added.clipIndex).id;

    m_app->pushProjectEdit(before, tr("Add mask"));
    m_app->finishEdit(tr("Mask added"));
    m_app->selectClipById(addedId);
}

drift::TimeUs MaskingController::assetMaskDurationUs(int assetIndex) const
{
    const drift::MediaAsset *asset = assetAt(assetIndex);
    if (asset && asset->kind == drift::MediaKind::Video && asset->durationUs > 0)
        return asset->durationUs;
    return drift::imageClipDurationUs();
}

bool MaskingController::canConvertClipToMask(int trackIndex, int clipIndex) const
{
    const drift::Project &project = m_app->m_project;
    if (trackIndex < 0 || trackIndex >= project.tracks().size())
        return false;
    const QList<drift::Clip> &clips = project.tracks().at(trackIndex).clips;
    if (clipIndex < 0 || clipIndex >= clips.size())
        return false;
    const drift::Clip &clip = clips.at(clipIndex);
    if (clip.type != drift::ClipType::Video && clip.type != drift::ClipType::Image)
        return false;
    // The matte plays at 1x forward from srcIn, so a retimed clip would convert to different timing.
    if (clip.effectiveSpeed() != 1.0 || clip.reverse || clip.hasSpeedCurve())
        return false;
    return trackBelow(project, trackIndex) >= 0;
}

QString MaskingController::convertClipToMaskBlocker(int trackIndex, int clipIndex) const
{
    if (canConvertClipToMask(trackIndex, clipIndex))
        return {};
    const drift::Project &project = m_app->m_project;
    if (trackIndex < 0 || trackIndex >= project.tracks().size())
        return {};
    const QList<drift::Clip> &clips = project.tracks().at(trackIndex).clips;
    if (clipIndex < 0 || clipIndex >= clips.size())
        return {};
    const drift::Clip &clip = clips.at(clipIndex);
    if (clip.type != drift::ClipType::Video && clip.type != drift::ClipType::Image)
        return {};
    if (clip.effectiveSpeed() != 1.0 || clip.reverse || clip.hasSpeedCurve())
        return tr("Remove speed changes first");
    return tr("Needs a video track below");
}

void MaskingController::convertClipToMask(int trackIndex, int clipIndex)
{
    if (!canConvertClipToMask(trackIndex, clipIndex))
        return;

    const drift::Clip clip = m_app->m_project.tracks().at(trackIndex).clips.at(clipIndex);
    const drift::Mask mask = mediaMask(clip.path, clip.srcIn, drift::MaskMediaChannel::Luma, clip.name);

    const drift::Project before = m_app->m_project;
    removeClipWithPartners(m_app->m_project, trackIndex, clipIndex);
    const int matteTrack = trackBelow(m_app->m_project, trackIndex);
    const drift::ClipRef added = drift::addLaneMask(m_app->m_project, matteTrack, mask,
                                                    clip.timelineStart, clip.timelineDuration);
    if (added.trackIndex < 0)
        return;
    const QString addedId = m_app->m_project.tracks().at(added.trackIndex).clips.at(added.clipIndex).id;

    m_app->pushProjectEdit(before, tr("Convert to mask"));
    m_app->finishEdit(tr("Clip converted to mask"));
    m_app->selectClipById(addedId);
}

QVariantMap MaskingController::maskTarget() const
{
    const drift::Project &project = m_app->m_project;
    const int trackIndex = m_app->m_selectedTrack;
    const int clipIndex = m_app->m_selectedClip;
    if (!m_app->isValidClipIndex(trackIndex, clipIndex))
        return targetMap(QStringLiteral("none"), -1, -1, {});

    const drift::Track &track = project.tracks().at(trackIndex);
    const drift::Clip &clip = track.clips.at(clipIndex);
    if (clip.type != drift::ClipType::Adjustment) {
        if (track.type != drift::TrackType::Video)
            return targetMap(QStringLiteral("none"), -1, -1, {});
        return targetMap(QStringLiteral("clip"), trackIndex, clipIndex, clip.name);
    }

    if (track.isAdjustmentLane()) {
        if (clip.adjustmentKind != drift::AdjustmentKind::Mask)
            return targetMap(QStringLiteral("none"), -1, -1, {});
        const int parent = drift::adjustmentLaneParentIndex(project, trackIndex);
        if (parent < 0)
            return targetMap(QStringLiteral("none"), -1, -1, {});
        const drift::Track &host = project.tracks().at(parent);
        if (!clip.linkedClipId.isEmpty()) {
            for (int c = 0; c < host.clips.size(); ++c) {
                if (host.clips.at(c).id == clip.linkedClipId)
                    return targetMap(QStringLiteral("clip"), parent, c, host.clips.at(c).name);
            }
            return targetMap(QStringLiteral("none"), -1, -1, {});
        }
        for (int c = 0; c < host.clips.size(); ++c) {
            const drift::Clip &media = host.clips.at(c);
            if (media.type != drift::ClipType::Adjustment && media.containsTime(m_app->m_playheadUs))
                return targetMap(QStringLiteral("clip"), parent, c, media.name);
        }
        return targetMap(QStringLiteral("none"), -1, -1, {});
    }

    if (clip.adjustmentKind != drift::AdjustmentKind::Mask
        && clip.adjustmentKind != drift::AdjustmentKind::VideoEffects) {
        return targetMap(QStringLiteral("none"), -1, -1, {});
    }
    return targetMap(QStringLiteral("adjustment"), trackIndex, clipIndex, maskLabel(clip.mask));
}

QVariantList MaskingController::maskStack(int trackIndex, int clipIndex) const
{
    const drift::Project &project = m_app->m_project;
    QVariantList out;
    if (!m_app->isValidClipIndex(trackIndex, clipIndex))
        return out;
    const drift::Clip &target = project.tracks().at(trackIndex).clips.at(clipIndex);

    const auto entry = [this](const drift::Clip &adjustment, int t, int c, bool pinned) {
        const drift::Mask &mask = adjustment.mask;
        const bool selected = t == m_app->m_selectedTrack && c == m_app->m_selectedClip;
        return QVariantMap{
            {QStringLiteral("track"), t},
            {QStringLiteral("clip"), c},
            {QStringLiteral("id"), adjustment.id},
            {QStringLiteral("name"), maskLabel(mask)},
            {QStringLiteral("shape"), drift::maskShapeToString(mask.shape)},
            {QStringLiteral("op"), drift::maskOpToString(mask.op)},
            {QStringLiteral("invert"), mask.invert},
            {QStringLiteral("enabled"), mask.enabled},
            {QStringLiteral("feather"), mask.feather},
            {QStringLiteral("mediaChannel"), drift::maskMediaChannelToString(mask.mediaChannel)},
            {QStringLiteral("isMedia"), mask.isMedia()},
            {QStringLiteral("mediaSource"), mediaSourceKind(mask)},
            {QStringLiteral("pinned"), pinned},
            {QStringLiteral("pathAnimated"), !mask.pathKeys.isEmpty()},
            {QStringLiteral("pathKeyCount"), int(mask.pathKeys.size())},
            {QStringLiteral("selected"), selected},
        };
    };

    if (target.type != drift::ClipType::Adjustment) {
        for (const int laneIndex : drift::adjustmentLaneIndexes(project, trackIndex)) {
            const drift::Track &lane = project.tracks().at(laneIndex);
            for (int c = 0; c < lane.clips.size(); ++c) {
                const drift::Clip &adjustment = lane.clips.at(c);
                if (adjustment.adjustmentKind != drift::AdjustmentKind::Mask)
                    continue;
                const bool pinned = !adjustment.linkedClipId.isEmpty() && adjustment.linkedClipId == target.id;
                const bool unpinnedOverlap = adjustment.linkedClipId.isEmpty()
                                             && adjustment.timelineStart < target.timelineEnd()
                                             && adjustment.timelineEnd() > target.timelineStart;
                if (pinned || unpinnedOverlap)
                    out.append(entry(adjustment, laneIndex, c, pinned));
            }
        }
        return out;
    }

    if (target.mask.shape != drift::MaskShape::None)
        out.append(entry(target, trackIndex, clipIndex, false));
    return out;
}

void MaskingController::addShapeMask(int trackIndex, int clipIndex, const QString &shape, double x, double y,
                                     double w, double h)
{
    if (!m_app->isValidClipIndex(trackIndex, clipIndex))
        return;
    if (shape != QLatin1String("rectangle") && shape != QLatin1String("ellipse"))
        return;

    drift::Mask mask = m_app->maskFromCatalogId(shape);
    mask.x = x;
    mask.y = y;
    mask.w = std::max(0.01, w);
    mask.h = std::max(0.01, h);

    const drift::Project before = m_app->m_project;
    if (m_app->m_project.tracks().at(trackIndex).clips.at(clipIndex).type == drift::ClipType::Adjustment) {
        // Assigned directly: writeClipMask would carry the replaced mask's keyframes onto the new one.
        m_app->m_project.tracks()[trackIndex].clips[clipIndex].mask = mask;
        m_app->pushProjectEdit(before, tr("Draw mask"));
        m_app->finishEdit(tr("Mask added"));
        return;
    }

    const drift::ClipRef added = drift::addLinkedMask(m_app->m_project, trackIndex, clipIndex, mask);
    if (added.trackIndex < 0)
        return;
    const QString addedId = m_app->m_project.tracks().at(added.trackIndex).clips.at(added.clipIndex).id;

    m_app->pushProjectEdit(before, tr("Draw mask"));
    m_app->finishEdit(tr("Mask added"));
    m_app->selectClipById(addedId);
}

void MaskingController::brushStroke(int trackIndex, int clipIndex, const QVariantList &points, bool erase)
{
    if (!isBrushTarget(trackIndex, clipIndex) || points.isEmpty())
        return;

    BrushPreviewStroke stroke;
    stroke.points = verticesFrom(points);
    stroke.pressures = QVector<double>(stroke.points.size(), 1.0);
    stroke.erase = erase;
    stroke.size = m_brushSize;
    stroke.hardness = m_brushHardness;
    stroke.opacity = m_brushOpacity;
    paintStroke(trackIndex, clipIndex, stroke, 0);
}

void MaskingController::paintStroke(int trackIndex, int clipIndex, const BrushPreviewStroke &stroke,
                                    quint64 strokeSeq)
{
    QImage image = brushImage(trackIndex, clipIndex);
    QVector<QPointF> pixels;
    QVector<double> radii;
    pixels.reserve(stroke.points.size());
    radii.reserve(stroke.points.size());
    for (int i = 0; i < stroke.points.size(); ++i) {
        pixels.append(QPointF(stroke.points.at(i).x() * image.width(), stroke.points.at(i).y() * image.height()));
        radii.append(stroke.size * image.width() / 2.0
                     * brushPressureScale(stroke.pressures.value(i, 1.0), stroke.pressureSize));
    }
    drift::masking::paintBrushStroke(image, pixels, radii, stroke.hardness, stroke.opacity, stroke.erase);
    commitBrushMatte(trackIndex, clipIndex, image, tr("Brush stroke"), strokeSeq);
}

const BrushPreviewStroke *MaskingController::previewStroke(int index) const
{
    if (index < 0)
        return m_stroke.active ? &m_stroke.preview : nullptr;
    return index < m_pendingStrokes.size() ? &m_pendingStrokes.at(index) : nullptr;
}

QString MaskingController::clipIdAt(int trackIndex, int clipIndex) const
{
    return m_app->isValidClipIndex(trackIndex, clipIndex)
               ? m_app->m_project.tracks().at(trackIndex).clips.at(clipIndex).id
               : QString();
}

bool MaskingController::strokeBegin(const QString &clipId, double x, double y, double frameW, double frameH,
                                    bool erase, double pressure)
{
    strokeCancel();
    bool target = false;
    const QList<drift::Track> &tracks = m_app->m_project.tracks();
    for (int t = 0; t < tracks.size() && !target; ++t) {
        for (int c = 0; c < tracks.at(t).clips.size(); ++c) {
            if (tracks.at(t).clips.at(c).id == clipId) {
                target = isBrushTarget(t, c);
                break;
            }
        }
    }
    if (!target)
        return false;
    m_stroke = LiveStroke();
    m_stroke.active = true;
    m_stroke.clipId = clipId;
    m_stroke.frameW = std::max(1.0, frameW);
    m_stroke.frameH = std::max(1.0, frameH);
    BrushPreviewStroke &p = m_stroke.preview;
    p.erase = erase;
    p.pressureSize = m_brushPressureSize;
    p.size = m_brushSize;
    p.hardness = m_brushHardness;
    p.opacity = m_brushOpacity;
    p.points.append(QPointF(x / m_stroke.frameW, y / m_stroke.frameH));
    p.pressures.append(pressure);
    emit strokePreviewChanged();
    return true;
}

void MaskingController::strokeAdd(double x, double y, double pressure)
{
    if (!m_stroke.active)
        return;
    BrushPreviewStroke &p = m_stroke.preview;
    const QPointF last = p.points.constLast();
    if (std::hypot(x - last.x() * m_stroke.frameW, y - last.y() * m_stroke.frameH) < 2.0)
        return;
    p.points.append(QPointF(x / m_stroke.frameW, y / m_stroke.frameH));
    p.pressures.append(pressure);
    emit strokePreviewChanged();
}

void MaskingController::strokeEnd()
{
    if (!m_stroke.active)
        return;
    BrushPreviewStroke stroke = std::move(m_stroke.preview);
    const QString clipId = m_stroke.clipId;
    m_stroke = LiveStroke();

    int trackIndex = -1;
    int clipIndex = -1;
    const QList<drift::Track> &tracks = m_app->m_project.tracks();
    for (int t = 0; t < tracks.size() && trackIndex < 0; ++t) {
        for (int c = 0; c < tracks.at(t).clips.size(); ++c) {
            if (tracks.at(t).clips.at(c).id == clipId) {
                trackIndex = t;
                clipIndex = c;
                break;
            }
        }
    }
    if (trackIndex < 0 || !isBrushTarget(trackIndex, clipIndex)) {
        emit strokePreviewChanged();
        return;
    }
    stroke.seq = m_nextStrokeSeq++;
    m_pendingStrokes.append(stroke);
    emit strokePreviewChanged();
    paintStroke(trackIndex, clipIndex, stroke, stroke.seq);
}

void MaskingController::strokeCancel()
{
    if (!m_stroke.active)
        return;
    m_stroke = LiveStroke();
    emit strokePreviewChanged();
}

void MaskingController::clearStrokePreview()
{
    if (m_pendingStrokes.isEmpty())
        return;
    m_pendingStrokes.clear();
    emit strokePreviewChanged();
}

bool MaskingController::hasSelectedBrushMask(int trackIndex, int clipIndex) const
{
    if (!isBrushTarget(trackIndex, clipIndex))
        return false;
    return selectedBrushMask(trackIndex, clipIndex).first >= 0;
}

void MaskingController::brushFill(int trackIndex, int clipIndex, bool clear)
{
    if (!isBrushTarget(trackIndex, clipIndex))
        return;
    // Clearing a mask that does not exist would create one that hides the whole clip.
    if (clear && !hasSelectedBrushMask(trackIndex, clipIndex))
        return;

    QImage image = brushImage(trackIndex, clipIndex);
    image.fill(clear ? 0 : 255);
    commitBrushMatte(trackIndex, clipIndex, image, clear ? tr("Clear mask") : tr("Fill mask"));
}

bool MaskingController::isBrushTarget(int trackIndex, int clipIndex) const
{
    return m_app->isValidClipIndex(trackIndex, clipIndex)
           && m_app->m_project.tracks().at(trackIndex).type == drift::TrackType::Video
           && m_app->m_project.tracks().at(trackIndex).clips.at(clipIndex).type != drift::ClipType::Adjustment;
}

QSize MaskingController::brushMatteSize(int trackIndex, int clipIndex) const
{
    const drift::Project &project = m_app->m_project;
    const drift::Clip &clip = project.tracks().at(trackIndex).clips.at(clipIndex);
    // The matte is stretched over the clip's frame, not its media, so its aspect has to be the
    // frame's or round dabs come out oval. Same resolution as PreviewController::maskEditorState.
    const drift::TimeUs relative = m_app->m_playheadUs - clip.timelineStart;
    const auto value = [&](const drift::KeyframeTrack<double> &kt, double fallback) {
        return kt.isEmpty() ? fallback : kt.evaluateAt(relative);
    };
    double frameW = value(clip.transformW, project.width());
    double frameH = value(clip.transformH, project.height());
    if (frameW <= 0.0 || frameH <= 0.0) {
        frameW = project.width();
        frameH = project.height();
    }

    constexpr int kMaxSide = 2048;
    const int width = std::max(1, static_cast<int>(std::lround(std::min<double>(kMaxSide, frameW))));
    const int height = std::max(1, static_cast<int>(std::lround(width * frameH / frameW)));
    return QSize(width, height);
}

QPair<int, int> MaskingController::selectedBrushMask(int trackIndex, int clipIndex) const
{
    const QPair<int, int> none(-1, -1);
    const int selTrack = m_app->m_selectedTrack;
    const int selClip = m_app->m_selectedClip;
    if (!m_app->isValidClipIndex(selTrack, selClip))
        return none;

    const drift::Project &project = m_app->m_project;
    const drift::Clip &adjustment = project.tracks().at(selTrack).clips.at(selClip);
    if (adjustment.type != drift::ClipType::Adjustment || adjustment.adjustmentKind != drift::AdjustmentKind::Mask)
        return none;
    const drift::Clip &target = project.tracks().at(trackIndex).clips.at(clipIndex);
    if (adjustment.linkedClipId.isEmpty() || adjustment.linkedClipId != target.id)
        return none;
    if (!adjustment.mask.isMedia() || !adjustment.mask.mediaPath.endsWith(QStringLiteral(".png")))
        return none;

    const QString mattes = QDir(drift::matteCacheDir()).absolutePath() + QLatin1Char('/');
    if (!QFileInfo(adjustment.mask.mediaPath).absoluteFilePath().startsWith(mattes))
        return none;
    return {selTrack, selClip};
}

QImage MaskingController::brushImage(int trackIndex, int clipIndex) const
{
    // Saves in flight mean the project's mask still points at an older file, so m_brushImage is the
    // only up-to-date copy. It is also the only copy before the first save creates the mask.
    if ((m_brushSavesInFlight > 0 || m_brushSaveFailed) && !m_brushImage.isNull()
        && m_app->m_project.tracks().at(trackIndex).clips.at(clipIndex).id == m_brushClipId)
        return m_brushImage;

    const QPair<int, int> brush = selectedBrushMask(trackIndex, clipIndex);

    QImage image;
    if (brush.first >= 0) {
        const QString &path = m_app->m_project.tracks().at(brush.first).clips.at(brush.second).mask.mediaPath;
        if (path == m_brushPendingPath && !m_brushImage.isNull())
            image = m_brushImage;
        else
            image = QImage(path).convertToFormat(QImage::Format_Grayscale8);
    }
    // An existing matte keeps its own size: rescaling it on every stroke resamples the paint
    // whenever the clip's frame size is keyframed.
    if (image.isNull()) {
        image = QImage(brushMatteSize(trackIndex, clipIndex), QImage::Format_Grayscale8);
        image.fill(0);
    }
    return image;
}

void MaskingController::commitBrushMatte(int trackIndex, int clipIndex, const QImage &image, const QString &undoText,
                                         quint64 strokeSeq)
{
    const QString path = QDir(drift::matteCacheDir())
                             .filePath(QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral(".png"));
    const QString clipId = m_app->m_project.tracks().at(trackIndex).clips.at(clipIndex).id;
    m_brushImage = image;
    m_brushPendingPath = path;
    m_brushClipId = clipId;
    m_sessionBrushMattes.insert(QFileInfo(path).absoluteFilePath());
    setBrushSavesInFlight(m_brushSavesInFlight + 1);
    (void)QtConcurrent::run(&m_brushPool, [this, image, path, clipId, undoText, strokeSeq] {
        const bool ok = image.convertToFormat(QImage::Format_Grayscale8).save(path, "PNG");
        QMetaObject::invokeMethod(this, [this, ok, path, clipId, undoText, strokeSeq] {
            finishBrushSave(ok, path, clipId, undoText, strokeSeq);
        }, Qt::QueuedConnection);
    });
}

void MaskingController::setBrushSavesInFlight(int count)
{
    const bool wasSaving = m_brushSavesInFlight > 0;
    m_brushSavesInFlight = count;
    if (wasSaving != (count > 0))
        emit brushSavingChanged();
}

bool MaskingController::waitForBrushSaves(int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    m_brushPool.waitForDone(timeoutMs);
    while (m_brushSavesInFlight > 0 && timer.elapsed() < timeoutMs)
        QCoreApplication::processEvents();
    return m_brushSavesInFlight == 0;
}

void MaskingController::finishBrushSave(bool ok, const QString &path, const QString &clipId, const QString &undoText,
                                        quint64 strokeSeq)
{
    setBrushSavesInFlight(m_brushSavesInFlight - 1);
    const auto applied = qScopeGuard([this, &clipId, strokeSeq] {
        for (int i = 0; strokeSeq != 0 && i < m_pendingStrokes.size(); ++i) {
            if (m_pendingStrokes.at(i).seq == strokeSeq) {
                m_pendingStrokes.removeAt(i);
                emit strokePreviewChanged();
                break;
            }
        }
        emit brushApplied(clipId);
    });
    if (path == m_brushPendingPath)
        m_brushSaveFailed = !ok;
    if (!ok) {
        qWarning() << "Could not save brush matte" << path;
        m_app->setLastMessage(tr("Could not save the brush mask. The next stroke will try again"),
                              QStringLiteral("error"));
        return;
    }

    // The clip's indices may have moved since the stroke was made.
    int trackIndex = -1;
    int clipIndex = -1;
    const QList<drift::Track> &tracks = m_app->m_project.tracks();
    for (int t = 0; t < tracks.size() && trackIndex < 0; ++t) {
        for (int c = 0; c < tracks.at(t).clips.size(); ++c) {
            if (tracks.at(t).clips.at(c).id == clipId) {
                trackIndex = t;
                clipIndex = c;
                break;
            }
        }
    }
    if (trackIndex < 0)
        return;

    const drift::Project before = m_app->m_project;
    const QPair<int, int> brush = selectedBrushMask(trackIndex, clipIndex);
    if (brush.first >= 0) {
        drift::Mask mask = m_app->m_project.tracks().at(brush.first).clips.at(brush.second).mask;
        mask.mediaPath = path;
        m_app->writeClipMask(brush.first, brush.second, mask);
        m_app->pushProjectEdit(before, undoText);
        m_app->finishEdit(tr("Mask painted"));
        return;
    }

    drift::Mask mask = drift::fullFrameMediaMask(path);
    mask.name = tr("Brush");
    const drift::ClipRef added = drift::addLinkedMask(m_app->m_project, trackIndex, clipIndex, mask);
    if (added.trackIndex < 0)
        return;
    const QString addedId = m_app->m_project.tracks().at(added.trackIndex).clips.at(added.clipIndex).id;

    m_app->pushProjectEdit(before, undoText);
    m_app->finishEdit(tr("Mask added"));
    m_app->selectClipById(addedId);
}

void MaskingController::setMaskProperty(int trackIndex, int clipIndex, const QString &key, const QVariant &value)
{
    if (!canEditMask(trackIndex, clipIndex, key))
        return;
    const drift::Project before = m_app->m_project;
    applyMaskProperty(trackIndex, clipIndex, key, value);
    m_app->pushProjectEdit(before, tr("Edit mask"));
    m_app->finishEdit(tr("Mask changed"));
}

void MaskingController::previewSetMaskProperty(int trackIndex, int clipIndex, const QString &key,
                                               const QVariant &value)
{
    if (!canEditMask(trackIndex, clipIndex, key))
        return;
    if (!m_app->previewDragActive())
        m_app->beginPreviewDrag(tr("Edit mask"));
    applyMaskProperty(trackIndex, clipIndex, key, value);
    // Not emitPreviewFrame: a preview write has to mark the drag dirty, or commitPreviewDrag
    // drops it and the drag leaves no undo step.
    m_app->emitPreviewEdit(trackIndex, clipIndex, {QStringLiteral("mask.") + key});
}

void MaskingController::removeMask(int trackIndex, int clipIndex)
{
    if (!m_app->isValidClipIndex(trackIndex, clipIndex))
        return;
    const drift::Clip &clip = m_app->m_project.tracks().at(trackIndex).clips.at(clipIndex);
    if (clip.type != drift::ClipType::Adjustment)
        return;

    const drift::Project before = m_app->m_project;
    if (clip.adjustmentKind == drift::AdjustmentKind::VideoEffects) {
        drift::Mask cleared = clip.mask;
        cleared.shape = drift::MaskShape::None;
        m_app->writeClipMask(trackIndex, clipIndex, cleared);
        m_app->pushProjectEdit(before, tr("Delete mask"));
        m_app->finishEdit(tr("Mask removed"));
        return;
    }
    if (clip.adjustmentKind != drift::AdjustmentKind::Mask)
        return;

    // Removing shifts the clips after it, so the selection is carried by id. A mask removed while
    // selected hands the selection to the clip it was pinned to, which is what the panel is showing.
    const QString removedId = clip.id;
    const QString linkedId = clip.linkedClipId;
    QString keepId;
    if (m_app->isValidClipIndex(m_app->m_selectedTrack, m_app->m_selectedClip)) {
        const QString selectedId =
            m_app->m_project.tracks().at(m_app->m_selectedTrack).clips.at(m_app->m_selectedClip).id;
        keepId = selectedId == removedId ? linkedId : selectedId;
    }

    m_app->m_project.tracks()[trackIndex].clips.removeAt(clipIndex);
    m_app->pushProjectEdit(before, tr("Delete mask"));
    m_app->finishEdit(tr("Mask deleted"));
    if (keepId.isEmpty())
        m_app->clearSelection();
    else
        m_app->selectClipById(keepId);
}

void MaskingController::addPolygonMask(int trackIndex, int clipIndex, const QVariantList &points)
{
    if (!m_app->isValidClipIndex(trackIndex, clipIndex) || points.size() < 3)
        return;

    drift::Mask mask;
    mask.shape = drift::MaskShape::Freeform;
    mask.points = verticesFrom(points);

    const drift::Project before = m_app->m_project;
    if (m_app->m_project.tracks().at(trackIndex).clips.at(clipIndex).type == drift::ClipType::Adjustment) {
        // Assigned directly: writeClipMask would carry the replaced mask's keyframes onto the new one.
        m_app->m_project.tracks()[trackIndex].clips[clipIndex].mask = mask;
        m_app->pushProjectEdit(before, tr("Draw polygon mask"));
        m_app->finishEdit(tr("Mask added"));
        return;
    }

    const drift::ClipRef added = drift::addLinkedMask(m_app->m_project, trackIndex, clipIndex, mask);
    if (added.trackIndex < 0)
        return;
    const QString addedId = m_app->m_project.tracks().at(added.trackIndex).clips.at(added.clipIndex).id;

    m_app->pushProjectEdit(before, tr("Draw polygon mask"));
    m_app->finishEdit(tr("Mask added"));
    m_app->selectClipById(addedId);
}

QVariantList MaskingController::maskPathKeyTimes(int trackIndex, int clipIndex) const
{
    QVariantList out;
    if (!isPolygonAdjustment(trackIndex, clipIndex))
        return out;
    const drift::Clip &clip = m_app->m_project.tracks().at(trackIndex).clips.at(clipIndex);
    for (auto it = clip.mask.pathKeys.constBegin(); it != clip.mask.pathKeys.constEnd(); ++it)
        out.append(drift::usToSeconds(clip.timelineStart + it.key()));
    return out;
}

void MaskingController::setMaskPathAnimated(int trackIndex, int clipIndex, bool animated)
{
    if (!isPolygonAdjustment(trackIndex, clipIndex))
        return;
    drift::Clip &clip = m_app->m_project.tracks()[trackIndex].clips[clipIndex];
    const bool alreadyAnimated = !clip.mask.pathKeys.isEmpty();
    if (animated == alreadyAnimated)
        return;

    const drift::Project before = m_app->m_project;
    const drift::TimeUs maskUs = m_app->m_playheadUs - clip.timelineStart;
    if (animated) {
        clip.mask.pathKeys.insert(maskUs, clip.mask.points);
    } else {
        clip.mask.points = clip.mask.resolvedAt(maskUs).points;
        clip.mask.pathKeys.clear();
    }

    m_app->pushProjectEdit(before, animated ? tr("Animate mask shape") : tr("Stop animating mask shape"));
    m_app->finishEdit(animated ? tr("Mask shape animated") : tr("Mask shape static"));
}

void MaskingController::setMaskPathKey(int trackIndex, int clipIndex, double atSeconds, const QVariantList &points)
{
    const QVector<QPointF> vertices = verticesFrom(points);
    if (vertices.size() < 3 || !isPolygonAdjustment(trackIndex, clipIndex))
        return;

    const drift::Project before = m_app->m_project;
    drift::Clip &clip = m_app->m_project.tracks()[trackIndex].clips[clipIndex];
    const drift::TimeUs maskUs = drift::secondsToUs(atSeconds) - clip.timelineStart;
    writePathKey(clip.mask, maskUs, halfFrameUs(), vertices);

    m_app->pushProjectEdit(before, tr("Edit mask shape"));
    m_app->finishEdit(tr("Mask shape changed"));
}

void MaskingController::previewSetMaskPathKey(int trackIndex, int clipIndex, double atSeconds,
                                              const QVariantList &points)
{
    const QVector<QPointF> vertices = verticesFrom(points);
    if (vertices.size() < 3 || !isPolygonAdjustment(trackIndex, clipIndex))
        return;

    if (!m_app->previewDragActive())
        m_app->beginPreviewDrag(tr("Edit mask shape"));
    drift::Clip &clip = m_app->m_project.tracks()[trackIndex].clips[clipIndex];
    const drift::TimeUs maskUs = drift::secondsToUs(atSeconds) - clip.timelineStart;
    writePathKey(clip.mask, maskUs, halfFrameUs(), vertices);
    // Not emitPreviewFrame: see previewSetMaskProperty.
    m_app->emitPreviewEdit(trackIndex, clipIndex, {QStringLiteral("mask.pathKeys")});
}

void MaskingController::removeMaskPathKey(int trackIndex, int clipIndex, double atSeconds)
{
    if (!isPolygonAdjustment(trackIndex, clipIndex))
        return;
    drift::Clip &clip = m_app->m_project.tracks()[trackIndex].clips[clipIndex];
    const drift::TimeUs halfFrame = halfFrameUs();
    const drift::TimeUs maskUs = drift::secondsToUs(atSeconds) - clip.timelineStart;
    const auto key = clip.mask.pathKeys.lowerBound(maskUs - halfFrame);
    if (key == clip.mask.pathKeys.end() || qAbs(key.key() - maskUs) > halfFrame)
        return;

    const drift::Project before = m_app->m_project;
    // Deleting the last key would otherwise snap the shape back to the stale static points.
    if (clip.mask.pathKeys.size() == 1)
        clip.mask.points = key.value();
    clip.mask.pathKeys.erase(key);

    m_app->pushProjectEdit(before, tr("Delete shape key"));
    m_app->finishEdit(tr("Mask shape key removed"));
}

void MaskingController::toggleMaskPathKeyAtPlayhead(const QString &clipId)
{
    const drift::Project &project = m_app->m_project;
    for (int t = 0; t < project.tracks().size(); ++t) {
        const auto &clips = project.tracks().at(t).clips;
        for (int c = 0; c < clips.size(); ++c) {
            if (clips.at(c).id != clipId)
                continue;
            if (!isPolygonAdjustment(t, c))
                return;
            const drift::Clip &clip = clips.at(c);
            const drift::TimeUs maskUs = m_app->m_playheadUs - clip.timelineStart;
            const drift::TimeUs half = halfFrameUs();
            const auto key = clip.mask.pathKeys.lowerBound(maskUs - half);
            if (key != clip.mask.pathKeys.end() && qAbs(key.key() - maskUs) <= half) {
                removeMaskPathKey(t, c, m_app->playheadSeconds());
                return;
            }
            QVariantList points;
            for (const QPointF &p : clip.mask.resolvedAt(maskUs).points)
                points.append(QVariantMap{{QStringLiteral("x"), p.x()}, {QStringLiteral("y"), p.y()}});
            setMaskPathKey(t, c, m_app->playheadSeconds(), points);
            return;
        }
    }
}

drift::TimeUs MaskingController::halfFrameUs() const
{
    return drift::frameDurationUs(m_app->m_projectFile->projectFps()) / 2;
}

bool MaskingController::isPolygonAdjustment(int trackIndex, int clipIndex) const
{
    if (!m_app->isValidClipIndex(trackIndex, clipIndex))
        return false;
    const drift::Clip &clip = m_app->m_project.tracks().at(trackIndex).clips.at(clipIndex);
    return clip.type == drift::ClipType::Adjustment && clip.mask.shape == drift::MaskShape::Freeform;
}

QString MaskingController::maskLabel(const drift::Mask &mask) const
{
    if (!mask.name.isEmpty())
        return mask.name;
    switch (mask.shape) {
    case drift::MaskShape::Rectangle:
        return tr("Rectangle");
    case drift::MaskShape::Ellipse:
        return tr("Ellipse");
    case drift::MaskShape::Freeform:
        return tr("Polygon");
    case drift::MaskShape::Star:
        return tr("Star");
    case drift::MaskShape::Heart:
        return tr("Heart");
    case drift::MaskShape::Bars:
        return tr("Bars");
    case drift::MaskShape::Media:
        return tr("Matte");
    case drift::MaskShape::None:
        break;
    }
    return tr("Mask");
}

bool MaskingController::canEditMask(int trackIndex, int clipIndex, const QString &key) const
{
    return kMaskKeys.contains(key) && m_app->isValidClipIndex(trackIndex, clipIndex)
           && m_app->m_project.tracks().at(trackIndex).clips.at(clipIndex).type == drift::ClipType::Adjustment;
}

void MaskingController::applyMaskProperty(int trackIndex, int clipIndex, const QString &key, const QVariant &value)
{
    drift::Mask mask = m_app->m_project.tracks().at(trackIndex).clips.at(clipIndex).mask;
    if (key == QLatin1String("name"))
        mask.name = value.toString();
    else if (key == QLatin1String("op"))
        mask.op = drift::maskOpFromString(value.toString());
    else if (key == QLatin1String("invert"))
        mask.invert = value.toBool();
    else if (key == QLatin1String("enabled"))
        mask.enabled = value.toBool();
    else if (key == QLatin1String("feather"))
        mask.feather = value.toDouble();
    else
        mask.mediaChannel = drift::maskMediaChannelFromString(value.toString());
    m_app->writeClipMask(trackIndex, clipIndex, mask);
}
