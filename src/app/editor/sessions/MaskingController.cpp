#include "app/editor/sessions/MaskingController.h"

#include "app/bin/AssetLibrary.h"
#include "app/editor/AppController.h"
#include "core/edit/TimelineOps.h"
#include "core/effects/Mask.h"
#include "core/project/Project.h"
#include "core/project/Track.h"
#include "engine/encode/writers/MatteWriter.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QList>
#include <QPainter>
#include <QPair>
#include <QRadialGradient>
#include <QSet>
#include <QStringList>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <utility>

namespace {

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
    if (image.isNull() || pixelPoints.isEmpty() || radiusPx <= 0)
        return;

    const QColor colour = erase ? QColor(Qt::black) : QColor(Qt::white);
    QColor solid = colour;
    solid.setAlphaF(std::clamp(opacity, 0.0, 1.0));
    QColor transparent = colour;
    transparent.setAlpha(0);
    // Below 1 so the falloff keeps a ramp at full hardness.
    const double solidStop = std::clamp(hardness, 0.0, 0.99);
    const double spacing = std::max(1.0, radiusPx * 0.25);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    const auto dab = [&](const QPointF &centre) {
        QRadialGradient gradient(centre, radiusPx);
        gradient.setColorAt(0.0, solid);
        gradient.setColorAt(solidStop, solid);
        gradient.setColorAt(1.0, transparent);
        painter.setBrush(gradient);
        painter.drawEllipse(centre, radiusPx, radiusPx);
    };

    dab(pixelPoints.constFirst());
    for (int i = 1; i < pixelPoints.size(); ++i) {
        const QPointF from = pixelPoints.at(i - 1);
        const QPointF to = pixelPoints.at(i);
        const double length = std::hypot(to.x() - from.x(), to.y() - from.y());
        const int steps = std::max(1, static_cast<int>(std::ceil(length / spacing)));
        for (int s = 1; s <= steps; ++s)
            dab(from + (to - from) * (static_cast<double>(s) / steps));
    }
}

} // namespace drift::masking

MaskingController::MaskingController(AppController *app, QObject *parent)
    : QObject(parent)
    , m_app(app)
{
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
    const drift::ClipType type = clips.at(clipIndex).type;
    if (type != drift::ClipType::Video && type != drift::ClipType::Image)
        return false;
    return trackBelow(project, trackIndex) >= 0;
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

    QImage image = brushImage(trackIndex, clipIndex);
    QVector<QPointF> pixels;
    pixels.reserve(points.size());
    for (const QPointF &normalized : verticesFrom(points))
        pixels.append(QPointF(normalized.x() * image.width(), normalized.y() * image.height()));

    drift::masking::paintBrushStroke(image, pixels, m_brushSize * image.width() / 2.0, m_brushHardness,
                                     m_brushOpacity, erase);
    commitBrushMatte(trackIndex, clipIndex, image, tr("Brush stroke"));
}

void MaskingController::brushFill(int trackIndex, int clipIndex, bool clear)
{
    if (!isBrushTarget(trackIndex, clipIndex))
        return;

    QImage image = brushImage(trackIndex, clipIndex);
    image.fill(clear ? Qt::black : Qt::white);
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
    const drift::MediaAsset *asset = project.asset(clip.assetId);
    int width = asset && asset->width > 0 ? asset->width : project.width();
    int height = asset && asset->height > 0 ? asset->height : project.height();

    constexpr int kMaxSide = 2048;
    const int longest = std::max(width, height);
    if (longest > kMaxSide) {
        const double scale = static_cast<double>(kMaxSide) / longest;
        width = std::max(1, static_cast<int>(std::lround(width * scale)));
        height = std::max(1, static_cast<int>(std::lround(height * scale)));
    }
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
    const QSize size = brushMatteSize(trackIndex, clipIndex);
    const QPair<int, int> brush = selectedBrushMask(trackIndex, clipIndex);

    QImage image;
    if (brush.first >= 0) {
        const QString &path = m_app->m_project.tracks().at(brush.first).clips.at(brush.second).mask.mediaPath;
        if (path == m_brushImagePath && !m_brushImage.isNull())
            image = m_brushImage;
        else
            image = QImage(path).convertToFormat(QImage::Format_ARGB32_Premultiplied);
    }
    if (image.isNull()) {
        image = QImage(size, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::black);
        return image;
    }
    if (image.size() != size)
        image = image.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    return image;
}

void MaskingController::commitBrushMatte(int trackIndex, int clipIndex, const QImage &image, const QString &undoText)
{
    const QString path = QDir(drift::matteCacheDir())
                             .filePath(QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral(".png"));
    if (!image.convertToFormat(QImage::Format_Grayscale8).save(path, "PNG")) {
        qWarning() << "Could not save brush matte" << path;
        return;
    }
    m_brushImage = image;
    m_brushImagePath = path;

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
