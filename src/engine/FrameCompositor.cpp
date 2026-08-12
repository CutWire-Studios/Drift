#include "FrameCompositor.h"

#include "ClipReaderPool.h"
#include "CompositorFrameHistory.h"
#include "EffectCatalog.h"
#include "EffectProcessor.h"
#include "FaceTrack.h"
#include "GpuCompositor.h"
#include "GpuEffectExecutor.h"
#include "MaskApplier.h"
#include "MediaProbe.h"
#include "ReverseProxyCache.h"
#include "TextRaster.h"
#include "TransitionCatalog.h"
#include "core/Clip.h"
#include "core/ClipAnimation.h"
#include "core/ShapePath.h"
#include "core/SubtitleCue.h"
#include "core/Time.h"
#include "core/Transition.h"

#include <QBrush>
#include <QColor>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QImageReader>
#include <QMutex>
#include <QPainter>
#include <QPainterPath>
#include <QSet>
#include <cmath>
#include <QtMath>

#include <unordered_map>

namespace {

// A mask's media can be a still as easily as a video, and the two decode through different
// paths. Suffix rather than header sniffing: this is consulted per clip per frame, and a stat
// plus a header read on every one of them would cost more than the answer is worth.
bool maskMediaIsStillImage(const QString &path)
{
    static const QSet<QString> suffixes = [] {
        QSet<QString> out;
        for (const QByteArray &format : QImageReader::supportedImageFormats())
            out.insert(QString::fromLatin1(format).toLower());
        return out;
    }();
    const int dot = path.lastIndexOf(QLatin1Char('.'));
    if (dot < 0)
        return false;
    return suffixes.contains(path.mid(dot + 1).toLower());
}

// Length of a looping mask video. Probing opens the file, which is far too expensive to repeat
// per frame, so the answer is cached per path — keyed on mtime and size like decodedStillImage,
// since the same path can hold different media over time.
drift::TimeUs maskMediaDurationUs(const QString &path)
{
    struct Entry
    {
        qint64 mtimeMs = 0;
        qint64 fileSize = 0;
        drift::TimeUs durationUs = 0;
    };
    static QMutex mutex;
    static std::unordered_map<QString, Entry> cache;

    const QFileInfo info(path);
    if (!info.exists())
        return 0;

    const QMutexLocker lock(&mutex);
    const auto it = cache.find(path);
    if (it != cache.end() && it->second.mtimeMs == info.lastModified().toMSecsSinceEpoch()
        && it->second.fileSize == info.size()) {
        return it->second.durationUs;
    }

    const MediaInfo probed = MediaProbe::probe(path);
    Entry entry;
    entry.mtimeMs = info.lastModified().toMSecsSinceEpoch();
    entry.fileSize = info.size();
    entry.durationUs = drift::TimeUs(probed.durationUs);
    cache[path] = entry;
    return entry.durationUs;
}

void collectActivePaths(const drift::Project *project, drift::TimeUs timelineUs, QSet<QString> &videoPaths,
                        QSet<QString> &audioPaths)
{
    if (!project)
        return;

    for (const drift::Track &track : project->tracks()) {
        if (track.hidden)
            continue;

        // Retained separately from clip.path: mask media has its own reader, and dropping it
        // here would tear the worker down and re-open the file every frame.
        for (const drift::Mask &mask : track.masks) {
            if (mask.contributes() && mask.isMedia() && mask.containsTime(timelineUs)
                && !maskMediaIsStillImage(mask.mediaPath)) {
                videoPaths.insert(mask.mediaPath);
            }
        }

        for (const drift::Clip &clip : track.clips) {
            if (!clip.containsTime(timelineUs))
                continue;

            if (clip.path.isEmpty())
                continue;
            if (clip.type == drift::ClipType::Shape)
                continue;

            if ((track.type == drift::TrackType::Video || track.type == drift::TrackType::Shape)
                && clip.type != drift::ClipType::Text) {
                // The reversed proxy, when there is one, is what the composite actually reads —
                // retaining clip.path instead would tear down the proxy's worker every frame.
                videoPaths.insert(drift::videoReadPath(clip));
            }
            if (track.type == drift::TrackType::Audio
                || (track.type == drift::TrackType::Video && clip.type == drift::ClipType::Video)) {
                audioPaths.insert(clip.path);
            }
        }
    }
}

// Every video frame this composite will need, so the readers can decode them
// concurrently on their own threads instead of one clip at a time on ours.
QList<ClipReaderPool::VideoRequest> collectVideoRequests(const drift::Project *project,
                                                         drift::TimeUs timelineUs, int maxWidth,
                                                         int maxHeight)
{
    QList<ClipReaderPool::VideoRequest> requests;
    if (!project)
        return requests;

    for (const drift::Track &track : project->tracks()) {
        if (track.hidden || track.type == drift::TrackType::Audio)
            continue;

        // Mask media decodes like any other video, so warm it alongside the sources rather than
        // stalling the composite on a serial read later.
        for (const drift::Mask &mask : track.masks) {
            if (!mask.contributes() || !mask.isMedia() || !mask.containsTime(timelineUs)
                || maskMediaIsStillImage(mask.mediaPath)) {
                continue;
            }
            requests.append(ClipReaderPool::VideoRequest{
                mask.mediaPath,
                qMax<drift::TimeUs>(0, (timelineUs - mask.timelineStart) + mask.mediaOffsetUs),
                maxWidth, maxHeight});
        }

        for (const drift::Clip &clip : track.clips) {
            if (!clip.containsTime(timelineUs))
                continue;

            if (clip.type != drift::ClipType::Video || clip.path.isEmpty())
                continue;

            const drift::VideoRead read = drift::resolveVideoRead(clip, timelineUs);
            requests.append(
                ClipReaderPool::VideoRequest{read.path, read.sourceUs, maxWidth, maxHeight});
        }
    }
    return requests;
}

const drift::Effect *findTimeEchoEffect(const QList<drift::Effect> &effects)
{
    for (const drift::Effect &effect : effects) {
        if (!effect.enabled)
            continue;
        if (effect.catalogId == QStringLiteral("time_echo"))
            return &effect;
    }
    return nullptr;
}

// Only worth touching the face track when something in the chain actually consumes it, so a clip
// that has been detected but is running ordinary effects pays nothing.
bool chainNeedsFace(const QList<drift::Effect> &effects)
{
    for (const drift::Effect &effect : effects) {
        if (!effect.enabled)
            continue;
        const EffectPresetEntry *def =
            effect.catalogId.isEmpty() ? nullptr : effectDefForId(effect.catalogId);
        if (def && def->needsFace)
            return true;
    }
    return false;
}

// This frame's anchors for a clip, or an empty list when nothing in the chain wants them. Shared by
// the CPU and GPU compositing paths so a face warp cannot come out differently between preview and
// export depending on which one ran.
QList<drift::FaceAnchors> faceSlotsForClip(const drift::Clip &clip,
                                           const QList<drift::Effect> &effects,
                                           drift::TimeUs timelineUs)
{
    if (clip.faceTrackPath.isEmpty() || !chainNeedsFace(effects))
        return {};
    const auto track = drift::loadFaceTrackCached(clip.faceTrackPath);
    if (!track)
        return {};
    return track->sampleAll(clip.timelineToSourceUs(timelineUs) - clip.faceTrackSrcOffsetUs);
}

// The clip's chain as it should render *this* frame: time_echo dropped (its trail is assembled
// before the chain runs) and every keyframed parameter baked down to its value at clipTimeUs.
// Both the CPU and GPU paths go through here, so an animated parameter cannot come out different
// between preview and export.
QList<drift::Effect> resolvedClipEffects(const drift::Clip &clip, drift::TimeUs clipTimeUs)
{
    QList<drift::Effect> filtered;
    filtered.reserve(clip.effects.size());
    for (const drift::Effect &effect : clip.effects) {
        if (!effect.enabled)
            continue;
        if (effect.catalogId != QStringLiteral("time_echo"))
            filtered.append(effect.resolvedAt(clipTimeUs));
    }
    return filtered;
}

// Still images never change frame to frame, but decodeClipMediaFrame used to
// re-read and re-decode the file on every composited frame. Cache the scaled
// result per (path, size).
QImage decodedStillImage(const QString &path, int maxWidth, int maxHeight)
{
    // Keyed on mtime and size as well as path: the same path can hold different
    // pixels over time, and serving a stale decode would silently render the old
    // image.
    struct Key
    {
        QString path;
        qint64 mtimeMs = 0;
        qint64 fileSize = 0;
        int w = 0;
        int h = 0;
        bool operator==(const Key &other) const
        {
            return path == other.path && mtimeMs == other.mtimeMs && fileSize == other.fileSize
                   && w == other.w && h == other.h;
        }
    };
    struct KeyHash
    {
        size_t operator()(const Key &k) const
        {
            return qHash(k.path) ^ size_t(k.mtimeMs) ^ (size_t(k.fileSize) << 7)
                   ^ (size_t(k.w) << 1) ^ (size_t(k.h) << 17);
        }
    };

    static QMutex mutex;
    static std::unordered_map<Key, QImage, KeyHash> cache;

    const QFileInfo info(path);
    const Key key{path, info.lastModified().toMSecsSinceEpoch(), info.size(), maxWidth, maxHeight};
    {
        QMutexLocker lock(&mutex);
        const auto it = cache.find(key);
        if (it != cache.end())
            return it->second;
    }

    QImageReader reader(path);
    QImage image = reader.read();
    if (image.isNull())
        return {};
    image = image.convertToFormat(QImage::Format_RGBA8888)
                .scaled(maxWidth, maxHeight, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    QMutexLocker lock(&mutex);
    if (cache.size() > 32)
        cache.clear();
    cache.emplace(key, image);
    return image;
}

// maxWidth/maxHeight bound the decode buffer. They are deliberately *not* the
// clip's layout rect: the layout rect moves every frame under a scale keyframe,
// and a changing decode size invalidates the decoder's frame cache and forces a
// keyframe seek per frame. Decoding to a stable, canvas-bounded size and letting
// the draw step scale is both stable and cheaper.
QImage decodeClipMediaFrame(const drift::Clip &clip, drift::TimeUs timelineUs, int maxWidth, int maxHeight)
{
    if (clip.path.isEmpty())
        return {};

    if (clip.type == drift::ClipType::Image)
        return decodedStillImage(clip.path, maxWidth, maxHeight);

    if (clip.type == drift::ClipType::Video) {
        const drift::VideoRead read = drift::resolveVideoRead(clip, timelineUs);
        return ClipReaderPool::instance().readVideoFrame(read.path, read.sourceUs, maxWidth, maxHeight);
    }

    return {};
}

QImage shapeImageForClip(const drift::Clip &clip, int width, int height, double renderScale);

// maxWidth/maxHeight bound the decoded frame; the returned image may be smaller
// (source-limited) and is scaled to the clip's layout rect at draw time.
QImage imageForClip(const drift::Clip &clip, const QList<drift::Mask> &trackMasks,
                    drift::TimeUs timelineUs, int maxWidth, int maxHeight,
                    int projectFps, int maxTimeEchoHistoryFrames)
{
    if (clip.type == drift::ClipType::Shape)
        return shapeImageForClip(clip, maxWidth, maxHeight, 1.0);

    if (clip.path.isEmpty())
        return {};

    const drift::TimeUs clipTimeUs = timelineUs - clip.timelineStart;
    const drift::Effect *timeEcho = findTimeEchoEffect(clip.effects);
    const QList<drift::Effect> otherEffects = resolvedClipEffects(clip, clipTimeUs);

    QImage image;
    if (timeEcho) {
        const EffectPresetEntry *def = effectDefForId(timeEcho->catalogId);
        if (!def)
            return {};

        const QMap<QString, QVariant> params =
            resolvedEffectParameters(timeEcho->resolvedAt(clipTimeUs), *def);
        int frameCount = qBound(1, params.value(QStringLiteral("frames"), 4).toInt(), 10);
        if (maxTimeEchoHistoryFrames >= 0)
            frameCount = qMin(frameCount, maxTimeEchoHistoryFrames);
        const double decay = qBound(0.0, params.value(QStringLiteral("decay"), 0.55).toDouble(), 1.0);
        const auto blendMode =
            CompositorFrameHistory::parseEchoBlendMode(params.value(QStringLiteral("blendMode")).toString());

        const drift::TimeUs frameStepUs = drift::frameDurationUs(projectFps);
        QList<QImage> samples;
        samples.reserve(frameCount + 1);

        const QImage current = decodeClipMediaFrame(clip, timelineUs, maxWidth, maxHeight);
        if (current.isNull())
            return {};
        samples.append(current);

        for (int i = 1; i <= frameCount; ++i) {
            const drift::TimeUs pastClipUs = clipTimeUs - static_cast<drift::TimeUs>(i) * frameStepUs;
            if (pastClipUs < 0)
                break;
            const drift::TimeUs pastTimelineUs = clip.timelineStart + pastClipUs;
            const QImage past = decodeClipMediaFrame(clip, pastTimelineUs, maxWidth, maxHeight);
            if (!past.isNull())
                samples.append(past);
        }

        image = CompositorFrameHistory::applyTimeEcho(samples, decay, blendMode);
    } else {
        image = decodeClipMediaFrame(clip, timelineUs, maxWidth, maxHeight);
    }

    if (image.isNull())
        return image;

    // Mask geometry is normalized, so it applies at whatever size the decode
    // actually produced.
    if (!otherEffects.isEmpty()) {
        // Baked anchors, so this is a lookup rather than an inference: no ONNX ever runs on the
        // compositor thread, and preview and export read the same numbers.
        image = EffectProcessor::applyEffects(image, otherEffects, clipTimeUs,
                                              faceSlotsForClip(clip, otherEffects, timelineUs));
    }
    const QList<drift::Mask> active = drift::masksActiveAt(trackMasks, timelineUs);
    if (!drift::masksAreInert(active))
        image = drift::applyMask(image, active, image.width(), image.height());
    return image;
}

Qt::PenStyle penStyleFor(drift::ShapeStrokeStyle style)
{
    switch (style) {
    case drift::ShapeStrokeStyle::None:
        return Qt::NoPen;
    case drift::ShapeStrokeStyle::Solid:
        return Qt::SolidLine;
    case drift::ShapeStrokeStyle::Dash:
        return Qt::DashLine;
    case drift::ShapeStrokeStyle::Dot:
        return Qt::DotLine;
    case drift::ShapeStrokeStyle::DashDot:
        return Qt::DashDotLine;
    }
    return Qt::SolidLine;
}

QBrush shapeBrush(const drift::ShapeStyle &style, const QRectF &bounds)
{
    switch (style.fillKind) {
    case drift::ShapeFillKind::None:
        return Qt::NoBrush;
    case drift::ShapeFillKind::Solid:
        return style.fill;
    case drift::ShapeFillKind::LinearGradient: {
        // Angle sweeps the gradient axis across the shape's own bounding box, so the same angle
        // reads the same whatever the clip is scaled to.
        const double radians = qDegreesToRadians(style.gradientAngle);
        const QPointF centre = bounds.center();
        const QPointF half(qCos(radians) * bounds.width() / 2.0,
                           qSin(radians) * bounds.height() / 2.0);
        QLinearGradient gradient(centre - half, centre + half);
        gradient.setColorAt(0.0, style.fill);
        gradient.setColorAt(1.0, style.fillSecondary);
        return gradient;
    }
    case drift::ShapeFillKind::RadialGradient: {
        QRadialGradient gradient(bounds.center(),
                                 qMax(bounds.width(), bounds.height()) / 2.0);
        gradient.setColorAt(0.0, style.fill);
        gradient.setColorAt(1.0, style.fillSecondary);
        return gradient;
    }
    }
    return style.fill;
}

// Rasterized at exactly the destination size: the GPU quad is the layout rect and samples this
// texture 0..1, so anything smaller is upscaled and the stroke stretches with it.
QImage shapeImageForClip(const drift::Clip &clip, int width, int height, double renderScale)
{
    if (clip.type != drift::ClipType::Shape)
        return {};

    const int w = qMax(1, width);
    const int h = qMax(1, height);

    QImage image(w, h, QImage::Format_RGBA8888);
    image.fill(Qt::transparent);

    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);

    const drift::ShapeStyle &style = clip.shapeStyle;
    // Stroke width and corner radius are authored in project pixels, so they scale with the render
    // just like the layout rect does — otherwise preview and export disagree.
    const double strokeWidth =
        style.strokeStyle == drift::ShapeStrokeStyle::None ? 0.0 : style.strokeWidth * renderScale;
    const double inset = strokeWidth / 2.0;
    const QRectF bounds =
        QRectF(0, 0, w, h).adjusted(inset, inset, -inset, -inset).normalized();

    drift::ShapeStyle scaled = style;
    scaled.cornerRadius = style.cornerRadius * renderScale;

    p.setBrush(shapeBrush(scaled, bounds));
    p.setPen(strokeWidth <= 0.0
                 ? QPen(Qt::NoPen)
                 : QPen(style.stroke, strokeWidth, penStyleFor(style.strokeStyle), Qt::RoundCap,
                        Qt::RoundJoin));
    p.drawPath(drift::shapePath(scaled, bounds));

    p.end();
    return image;
}

double opacityForClip(const drift::Clip &clip, drift::TimeUs timelineUs)
{
    double value = 1.0;
    if (!clip.opacity.isEmpty()) {
        const drift::TimeUs relative = timelineUs - clip.timelineStart;
        value = qBound(0.0, clip.opacity.evaluateAt(relative), 1.0);
    }
    // Edge-relative fades ride on top of any opacity keyframes.
    return value * clip.fadeMultiplier(timelineUs);
}

double transformValue(const drift::KeyframeTrack<double> &track, drift::TimeUs relative, double defaultValue)
{
    if (track.isEmpty())
        return defaultValue;
    return track.evaluateAt(relative);
}

// Layout is stored in project pixels. Preview/export canvases may be scaled
// via renderScale — always map project → canvas here so WYSIWYG handles match.
void layoutRectForClip(const drift::Clip &clip, drift::TimeUs timelineUs, int projectWidth, int projectHeight,
                       double renderScale, double extraScale, double *xOut, double *yOut, double *wOut, double *hOut,
                       double *rotationOut = nullptr)
{
    const drift::TimeUs relative = timelineUs - clip.timelineStart;
    const double scale = renderScale * extraScale;
    *xOut = transformValue(clip.transformX, relative, 0.0) * renderScale;
    *yOut = transformValue(clip.transformY, relative, 0.0) * renderScale;
    *wOut = transformValue(clip.transformW, relative, static_cast<double>(projectWidth)) * scale;
    *hOut = transformValue(clip.transformH, relative, static_cast<double>(projectHeight)) * scale;
    if (rotationOut)
        *rotationOut = transformValue(clip.rotation, relative, 0.0);
}

// The bottommost active video/image frame at this time, used to derive a blur fill.
// Track 0 is topmost, so walk tracks back-to-front and take the first hit.
QImage bottommostVisualFrame(const drift::Project &project, drift::TimeUs timelineUs, int width, int height)
{
    const QList<drift::Track> &tracks = project.tracks();
    for (int ti = tracks.size() - 1; ti >= 0; --ti) {
        const drift::Track &track = tracks.at(ti);
        if (track.hidden || track.type == drift::TrackType::Audio)
            continue;
        for (const drift::Clip &clip : track.clips) {
            if (!clip.containsTime(timelineUs))
                continue;
            if (clip.type != drift::ClipType::Video && clip.type != drift::ClipType::Image)
                continue;
            QImage frame =
                imageForClip(clip, track.masks, timelineUs, width, height, project.fps(), -1);
            if (!frame.isNull())
                return frame;
        }
    }
    return {};
}

// ---------------------------------------------------------------------------
// GPU scene building
//
// The pixels a clip contributes before the GPU takes over: decode, plus the
// time_echo trail (which needs several decoded frames). Effects and the mask are
// deliberately left to the GPU.
QImage gpuSourceForClip(const drift::Clip &clip, drift::TimeUs timelineUs, int maxWidth, int maxHeight,
                        int projectFps, int maxTimeEchoHistoryFrames)
{
    if (clip.path.isEmpty())
        return {};

    const drift::Effect *timeEcho = findTimeEchoEffect(clip.effects);
    if (!timeEcho)
        return decodeClipMediaFrame(clip, timelineUs, maxWidth, maxHeight);

    const EffectPresetEntry *def = effectDefForId(timeEcho->catalogId);
    if (!def)
        return {};

    const drift::TimeUs clipTimeUs = timelineUs - clip.timelineStart;
    const QMap<QString, QVariant> params =
        resolvedEffectParameters(timeEcho->resolvedAt(clipTimeUs), *def);
    int frameCount = qBound(1, params.value(QStringLiteral("frames"), 4).toInt(), 10);
    if (maxTimeEchoHistoryFrames >= 0)
        frameCount = qMin(frameCount, maxTimeEchoHistoryFrames);
    const double decay = qBound(0.0, params.value(QStringLiteral("decay"), 0.55).toDouble(), 1.0);
    const auto blendMode =
        CompositorFrameHistory::parseEchoBlendMode(params.value(QStringLiteral("blendMode")).toString());

    const drift::TimeUs frameStepUs = drift::frameDurationUs(projectFps);
    QList<QImage> samples;
    samples.reserve(frameCount + 1);

    const QImage current = decodeClipMediaFrame(clip, timelineUs, maxWidth, maxHeight);
    if (current.isNull())
        return {};
    samples.append(current);

    for (int i = 1; i <= frameCount; ++i) {
        const drift::TimeUs pastClipUs = clipTimeUs - static_cast<drift::TimeUs>(i) * frameStepUs;
        if (pastClipUs < 0)
            break;
        const QImage past =
            decodeClipMediaFrame(clip, clip.timelineStart + pastClipUs, maxWidth, maxHeight);
        if (!past.isNull())
            samples.append(past);
    }

    return CompositorFrameHistory::applyTimeEcho(samples, decay, blendMode);
}

// Prefer NV12 for plain video (preview upload path); fall back to RGBA QImage
// when time_echo needs CPU blending or NV12 decode fails.
void fillGpuLayerPixels(GpuLayer &layer, const drift::Clip &clip, drift::TimeUs timelineUs, int maxWidth,
                        int maxHeight, int projectFps, int maxTimeEchoHistoryFrames)
{
    if (clip.path.isEmpty())
        return;

    const drift::Effect *timeEcho = findTimeEchoEffect(clip.effects);
    if (!timeEcho && clip.type == drift::ClipType::Video) {
        const drift::VideoRead read = drift::resolveVideoRead(clip, timelineUs);
        const Nv12Frame nv12 =
            ClipReaderPool::instance().readVideoFrameNv12(read.path, read.sourceUs, maxWidth, maxHeight);
        if (nv12.isValid()) {
            layer.nv12 = nv12.data;
            layer.nv12Width = nv12.width;
            layer.nv12Height = nv12.height;
            return;
        }
    }

    layer.source = gpuSourceForClip(clip, timelineUs, maxWidth, maxHeight, projectFps,
                                    maxTimeEchoHistoryFrames);
}

// The word the playhead sits on, for styles whose accent rule follows the speech. -1 for every
// other rule, which keeps their raster time-independent and therefore cached across the clip.
int karaokeWordIndex(const drift::Clip &clip, drift::TimeUs timelineUs)
{
    if (clip.textStyle.accent.rule != drift::WordAccentRule::Karaoke)
        return -1;
    const QString text = clip.textContent.isEmpty() ? clip.name : clip.textContent;
    return drift::activeWordIndexAt(text, clip.timelineStart,
                                    clip.timelineStart + clip.timelineDuration, timelineUs);
}

int karaokeWordIndex(const drift::Clip &clip, const drift::SubtitleCue &cue, drift::TimeUs localUs)
{
    if (clip.textStyle.accent.rule != drift::WordAccentRule::Karaoke)
        return -1;
    return drift::activeWordIndexAt(cue.text, cue.startUs, cue.endUs, localUs);
}

// CapCut-style body intro/outro: opacity/offset/scale/rotation on top of fades and text anims.
void applyClipBodyAnimation(const drift::Clip &clip, drift::TimeUs timelineUs, double layoutW,
                            double layoutH, QRectF *destRect, double *opacity, double *rotation)
{
    if (!destRect || !opacity || !rotation)
        return;
    if (clip.type == drift::ClipType::Audio || clip.type == drift::ClipType::Subtitle)
        return;
    if (clip.animIn.kind == drift::ClipAnimKind::None && clip.animOut.kind == drift::ClipAnimKind::None)
        return;

    const drift::ClipAnimSample body =
        drift::evaluateClipAnimation(clip.timelineStart, clip.timelineDuration, clip.animIn,
                                     clip.animOut, timelineUs, layoutW, layoutH);
    *opacity *= body.opacity;
    destRect->translate(body.dx, body.dy);
    if (!qFuzzyCompare(body.scale, 1.0)) {
        const QPointF centre = destRect->center();
        destRect->setSize(destRect->size() * body.scale);
        destRect->moveCenter(centre);
    }
    *rotation += body.rotationDeg;
}

// `trackMasks` is the whole track's mask lane; the ones covering this frame are picked out below.
// Masks belong to the track rather than the clip, so a mask spanning a cut reaches both clips and
// each rasterizes it in its own frame space.
GpuLayer buildGpuLayer(const drift::Clip &clip, const QList<drift::Mask> &trackMasks,
                       drift::TimeUs timelineUs, int projectWidth,
                       int projectHeight, double renderScale, int canvasWidth, int canvasHeight,
                       int projectFps, int maxTimeEchoHistoryFrames)
{
    GpuLayer layer;

    const drift::TimeUs clipTimeUs = timelineUs - clip.timelineStart;

    double x = 0.0;
    double y = 0.0;
    double w = 0.0;
    double h = 0.0;
    double rotation = 0.0;
    layoutRectForClip(clip, timelineUs, projectWidth, projectHeight, renderScale, 1.0, &x, &y, &w, &h,
                      &rotation);
    if (w <= 0.5 || h <= 0.5)
        return layer;

    const int layoutW = qMax(1, qRound(w));
    const int layoutH = qMax(1, qRound(h));

    const QRectF layoutRect(x, y, w, h);
    QRectF destRect = layoutRect;
    double opacity = opacityForClip(clip, timelineUs);

    if (clip.type == drift::ClipType::Text) {
        // The raster carries a bleed margin for the stroke, shadow and box, so its destination rect
        // is wider than the layout rect. Entrance/exit motion rides on the layer, not the pixels.
        const TextRasterResult raster =
            rasterizeText(clip, layoutRect, renderScale, karaokeWordIndex(clip, timelineUs));
        const TextAnimSample anim = sampleTextAnimation(clip, timelineUs, layoutRect, renderScale);

        layer.source = raster.image;
        layer.effects = resolvedClipEffects(clip, clipTimeUs);

        destRect = raster.rect.translated(anim.dx, anim.dy);
        if (!qFuzzyCompare(anim.scale, 1.0)) {
            const QPointF centre = destRect.center();
            destRect.setSize(destRect.size() * anim.scale);
            destRect.moveCenter(centre);
        }
        opacity *= anim.opacity;

        if (anim.blurPx > 0.5) {
            drift::Effect blur;
            blur.catalogId = QStringLiteral("builtin.effects.gaussian_blur");
            blur.parameters.insert(QStringLiteral("u_blurRadius"), anim.blurPx);
            layer.effects.append(blur);
        }
    } else if (clip.type == drift::ClipType::Subtitle) {
        const drift::TimeUs localUs = timelineUs - clip.timelineStart;
        const drift::SubtitleCue *cue = activeSubtitleCueAt(clip.subtitleCues, localUs);
        if (!cue || cue->text.trimmed().isEmpty())
            return layer;

        const TextRasterResult raster =
            rasterizeText(clip, cue->text, layoutRect, renderScale,
                          karaokeWordIndex(clip, *cue, localUs));
        if (raster.image.isNull())
            return layer;

        // Each cue animates in and out on its own window, so cues play one after another.
        const TextAnimSample anim = sampleSubtitleCueAnimation(clip, *cue, timelineUs, layoutRect,
                                                               renderScale);

        layer.source = raster.image;
        layer.effects = resolvedClipEffects(clip, clipTimeUs);

        destRect = raster.rect.translated(anim.dx, anim.dy);
        if (!qFuzzyCompare(anim.scale, 1.0)) {
            const QPointF centre = destRect.center();
            destRect.setSize(destRect.size() * anim.scale);
            destRect.moveCenter(centre);
        }
        opacity *= anim.opacity;

        if (anim.blurPx > 0.5) {
            drift::Effect blur;
            blur.catalogId = QStringLiteral("builtin.effects.gaussian_blur");
            blur.parameters.insert(QStringLiteral("u_blurRadius"), anim.blurPx);
            layer.effects.append(blur);
        }
    } else if (clip.type == drift::ClipType::Shape) {
        layer.source = shapeImageForClip(clip, layoutW, layoutH, renderScale);
        layer.effects = resolvedClipEffects(clip, clipTimeUs);
    } else {
        // Bounded by the canvas, not the layout rect — see decodeClipMediaFrame.
        fillGpuLayerPixels(layer, clip, timelineUs, canvasWidth, canvasHeight, projectFps,
                           maxTimeEchoHistoryFrames);
        layer.effects = resolvedClipEffects(clip, clipTimeUs);
    }

    if (!layer.hasPixels())
        return layer;

    applyClipBodyAnimation(clip, timelineUs, w, h, &destRect, &opacity, &rotation);

    // Only the masks covering this frame apply; outside its span a mask leaves the clip alone,
    // which is what makes its bar on the lane mean the stretch of time it covers.
    const QList<drift::Mask> active = drift::masksActiveAt(trackMasks, timelineUs);

    // Bake every animated mask property down to this frame's value, the way resolvedClipEffects
    // does for effects, so the rasterizer and the GPU fold only ever see plain numbers and
    // preview cannot diverge from export. Mask keyframes are relative to the mask's own start,
    // not the clip's — a mask outlives the clip it was drawn over.
    layer.masks.reserve(active.size());
    for (const drift::Mask &mask : active) {
        const drift::TimeUs maskTimeUs = timelineUs - mask.timelineStart;
        layer.masks.append(mask.isAnimated() ? mask.resolvedAt(maskTimeUs) : mask);
    }
    layer.maskMedia.resize(layer.masks.size());
    for (int i = 0; i < layer.masks.size(); ++i) {
        const drift::Mask &mask = layer.masks.at(i);
        if (!mask.contributes() || !mask.isMedia())
            continue;

        QImage coverage;
        if (maskMediaIsStillImage(mask.mediaPath)) {
            coverage = decodedStillImage(mask.mediaPath, canvasWidth, canvasHeight);
        } else {
            // Video coverage runs with the mask's own bar, offset by mediaOffsetUs — that offset
            // is how a segmentation matte lines up with the source range it was traced from.
            drift::TimeUs mediaUs = (timelineUs - mask.timelineStart) + mask.mediaOffsetUs;
            if (mask.mediaLoop) {
                // Wrapping needs the media's length, which only a probe knows; asking for it per
                // frame is a cache hit after the first.
                const drift::TimeUs span = maskMediaDurationUs(mask.mediaPath);
                if (span > 0)
                    mediaUs = ((mediaUs % span) + span) % span;
            }
            coverage = ClipReaderPool::instance().readVideoFrame(
                mask.mediaPath, qMax<drift::TimeUs>(0, mediaUs), canvasWidth, canvasHeight);
        }
        // Media that failed to decode must not silently blank the clip — leave that entry
        // contributing nothing rather than covering nothing.
        if (!coverage.isNull())
            layer.maskMedia[i] = coverage;
    }
    layer.rect = destRect;
    layer.rotation = rotation;
    layer.flipH = clip.flipH;
    layer.flipV = clip.flipV;
    layer.opacity = opacity;
    layer.clipTimeUs = timelineUs - clip.timelineStart;
    layer.faceSlots = faceSlotsForClip(clip, layer.effects, timelineUs);
    layer.valid = true;
    return layer;
}

// The reveal granularity in effect for a text clip: the entrance's unit, or the exit's if the
// entrance is whole-block. TextAnimUnit::Block means the whole-layer path (buildGpuLayer) is used.
drift::TextAnimUnit activeSpanUnit(const drift::TextStyle &style)
{
    if (style.animIn.kind != drift::TextAnimKind::None && style.animIn.unit != drift::TextAnimUnit::Block)
        return style.animIn.unit;
    if (style.animOut.kind != drift::TextAnimKind::None && style.animOut.unit != drift::TextAnimUnit::Block)
        return style.animOut.unit;
    return drift::TextAnimUnit::Block;
}

// Build one GpuItem per reveal span (character / word / line) of a text clip, so the entrance/exit
// staggers across the block. Mirrors the text branch of buildGpuLayer, but each span is its own
// layer carrying its own sampled transform. Returns empty for whole-block text (use buildGpuLayer).
QList<GpuItem> buildTextSpanItems(const drift::Clip &clip, drift::TimeUs timelineUs, int projectWidth,
                                  int projectHeight, double renderScale, drift::TextAnimUnit unit)
{
    QList<GpuItem> items;

    const drift::TimeUs clipTimeUs = timelineUs - clip.timelineStart;

    double x = 0.0, y = 0.0, w = 0.0, h = 0.0, rotation = 0.0;
    layoutRectForClip(clip, timelineUs, projectWidth, projectHeight, renderScale, 1.0, &x, &y, &w, &h,
                      &rotation);
    if (w <= 0.5 || h <= 0.5)
        return items;
    const QRectF layoutRect(x, y, w, h);

    const QString text = clip.textContent.isEmpty() ? clip.name : clip.textContent;
    const QList<TextSpanRaster> spans = rasterizeTextSpans(clip, text, layoutRect, renderScale, unit,
                                                           karaokeWordIndex(clip, timelineUs));
    if (spans.isEmpty())
        return items;

    const double clipOpacity = opacityForClip(clip, timelineUs);
    const QList<drift::Effect> baseEffects = resolvedClipEffects(clip, clipTimeUs);
    int spanCount = 0;
    for (const TextSpanRaster &s : spans)
        spanCount = qMax(spanCount, s.count);

    for (const TextSpanRaster &span : spans) {
        if (span.image.isNull())
            continue;

        GpuItem item;
        item.blend = clip.blendMode;
        GpuLayer &layer = item.layer;
        layer.source = span.image;
        layer.effects = baseEffects;

        QRectF destRect = span.rect;
        double opacity = clipOpacity;

        // index == -1 is the static box background: no per-span motion, always visible behind glyphs.
        if (span.index >= 0) {
            const TextAnimSample anim =
                sampleTextSpanAnimation(clip, timelineUs, span.index, spanCount, layoutRect, renderScale);
            destRect.translate(anim.dx, anim.dy);
            if (!qFuzzyCompare(anim.scale, 1.0)) {
                const QPointF centre = destRect.center();
                destRect.setSize(destRect.size() * anim.scale);
                destRect.moveCenter(centre);
            }
            opacity *= anim.opacity;
            if (anim.blurPx > 0.5) {
                drift::Effect blur;
                blur.catalogId = QStringLiteral("builtin.effects.gaussian_blur");
                blur.parameters.insert(QStringLiteral("u_blurRadius"), anim.blurPx);
                layer.effects.append(blur);
            }
            if (opacity <= 0.001)
                continue; // a span that has not entered (or has fully exited) draws nothing
        }

        double spanRotation = rotation;
        applyClipBodyAnimation(clip, timelineUs, w, h, &destRect, &opacity, &spanRotation);
        if (opacity <= 0.001)
            continue;

        // Clip masks are layer-relative, so applying one here would stamp the whole shape onto every
        // span. Kinetic text + mask is rare; spans are left unmasked rather than mask each glyph.
        layer.rect = destRect;
        layer.rotation = spanRotation;
        layer.flipH = clip.flipH;
        layer.flipV = clip.flipV;
        layer.opacity = opacity;
        layer.clipTimeUs = clipTimeUs;
        layer.faceSlots = faceSlotsForClip(clip, layer.effects, timelineUs);
        layer.valid = true;
        items.append(item);
    }
    return items;
}

GpuScene buildGpuScene(const drift::Project &project, drift::TimeUs timelineUs, int width, int height,
                       double renderScale, const FrameCompositor::RenderOptions &options)
{
    GpuScene scene;
    scene.canvasSize = QSize(width, height);

    const int projectWidth = project.width();
    const int projectHeight = project.height();
    const int fps = project.fps();

    // Mask view: show one clip's coverage instead of the composite. Rather than rasterize the
    // stack a second way, this builds the clip's normal layer and swaps its pixels for opaque
    // white — the existing mask pipeline (including the GPU fold and the sole-matte fast path)
    // then paints exactly the coverage map, white where the clip shows through, over black.
    // It costs one wasted decode per frame, which is the price of having only one masking path.
    if (!options.maskViewClipId.isEmpty()) {
        for (const drift::Track &track : project.tracks()) {
            if (track.type == drift::TrackType::Audio)
                continue;
            for (const drift::Clip &clip : track.clips) {
                if (clip.id != options.maskViewClipId || !clip.containsTime(timelineUs))
                    continue;

                GpuItem item;
                item.blend = drift::BlendMode::Normal;
                item.layer = buildGpuLayer(clip, track.masks, timelineUs, projectWidth,
                                           projectHeight, renderScale, width, height, fps,
                                           options.maxTimeEchoHistoryFrames);
                if (!item.layer.valid)
                    break;

                // Effects can alter alpha, and opacity would dim the readout; neither is part
                // of what the mask covers.
                item.layer.effects.clear();
                item.layer.opacity = 1.0;
                item.layer.nv12.clear();
                const QSize coverageSize(qMax(1, qRound(item.layer.rect.width())),
                                         qMax(1, qRound(item.layer.rect.height())));
                QImage white(coverageSize, QImage::Format_RGBA8888);
                white.fill(Qt::white);
                item.layer.source = white;

                scene.backgroundColor = Qt::black;
                scene.items.append(item);
                return scene;
            }
        }
        // The clip is not under the playhead: a black frame is the honest answer, and matches
        // what the composite would show there anyway.
        scene.backgroundColor = Qt::black;
        return scene;
    }

    const drift::Background &bg = project.background();
    if (bg.kind == drift::BackgroundKind::Blur) {
        scene.backgroundColor = Qt::black;
        scene.backgroundBlur = true;
        scene.blurStrengthPx = bg.blurStrength;
        // The bottommost visual frame, decoded once — the CPU path decoded it a
        // second time here, effects and all.
        scene.blurSource = bottommostVisualFrame(project, timelineUs, width, height);
    } else {
        scene.backgroundColor = bg.color.isValid() ? bg.color : QColor(Qt::black);
    }

    // Track 0 is topmost and composites in front, so emit back-to-front.
    const QList<drift::Track> &tracks = project.tracks();
    for (int ti = tracks.size() - 1; ti >= 0; --ti) {
        const drift::Track &track = tracks.at(ti);
        if (track.hidden || track.type == drift::TrackType::Audio)
            continue;

        QSet<QString> transitionClipIds;
        drift::TimeUs transitionStart = 0;
        drift::TimeUs transitionEnd = 0;
        const drift::Transition *activeTransition =
            drift::activeTransitionAt(track, timelineUs, transitionStart, transitionEnd);
        if (activeTransition) {
            const drift::Clip *fromClip = drift::clipById(track, activeTransition->fromClipId);
            const drift::Clip *toClip = drift::clipById(track, activeTransition->toClipId);
            if (fromClip && toClip) {
                GpuItem item;
                item.isTransition = true;
                item.from = buildGpuLayer(*fromClip, track.masks, timelineUs, projectWidth,
                                          projectHeight, renderScale, width, height, fps,
                                          options.maxTimeEchoHistoryFrames);
                item.to = buildGpuLayer(*toClip, track.masks, timelineUs, projectWidth,
                                        projectHeight, renderScale, width, height, fps,
                                        options.maxTimeEchoHistoryFrames);
                item.progress = drift::transitionProgress(timelineUs, transitionStart, transitionEnd);
                // Time is measured from the start of the transition window so a
                // shader's u_time is a pure function of window position, like
                // u_progress.
                item.transitionTimeUs = timelineUs - transitionStart;
                if (const TransitionPresetEntry *def = transitionDefForId(activeTransition->kindId);
                    def && def->gpu.valid) {
                    item.transitionKey = QLatin1String(kTransitionCacheKeyPrefix) + activeTransition->kindId;
                    item.transitionGpu = &def->gpu;
                    item.transitionParams = resolvedTransitionParameters(*activeTransition, *def);
                }
                scene.items.append(item);

                transitionClipIds.insert(fromClip->id);
                transitionClipIds.insert(toClip->id);
            }
        }

        for (const drift::Clip &clip : track.clips) {
            if (transitionClipIds.contains(clip.id) || !clip.containsTime(timelineUs))
                continue;
            // The clip being edited in place on the preview is hidden here so the
            // QML inline editor shows in its stead (true WYSIWYG, single path).
            if (!options.skipClipId.isEmpty() && clip.id == options.skipClipId)
                continue;

            // Text with a per-span reveal expands into one layer per character/word/line so the
            // entrance/exit can stagger across the block; everything else is a single layer.
            if (clip.type == drift::ClipType::Text) {
                const drift::TextAnimUnit unit = activeSpanUnit(clip.textStyle);
                if (unit != drift::TextAnimUnit::Block) {
                    scene.items.append(buildTextSpanItems(clip, timelineUs, projectWidth, projectHeight,
                                                          renderScale, unit));
                    continue;
                }
            }

            GpuItem item;
            item.blend = clip.blendMode;
            item.layer = buildGpuLayer(clip, track.masks, timelineUs, projectWidth, projectHeight,
                                       renderScale, width, height, fps,
                                       options.maxTimeEchoHistoryFrames);
            if (item.layer.valid)
                scene.items.append(item);
        }
    }

    return scene;
}

} // namespace

QImage FrameCompositor::compositeAt(drift::TimeUs timelineUs) const
{
    return compositeAt(timelineUs, RenderOptions{});
}

bool FrameCompositor::prepare(drift::TimeUs timelineUs, const RenderOptions &options, GpuScene *sceneOut,
                              int *widthOut, int *heightOut, double *renderScaleOut) const
{
    if (!m_project)
        return false;

    const int projectWidth = m_project->width();
    const int projectHeight = m_project->height();
    const double renderScale = qBound(0.1, options.previewScale, 1.0);
    const int width = qMax(1, static_cast<int>(std::lround(projectWidth * renderScale)));
    const int height = qMax(1, static_cast<int>(std::lround(projectHeight * renderScale)));
    if (width <= 0 || height <= 0)
        return false;

    QSet<QString> videoPaths;
    QSet<QString> audioPaths;
    collectActivePaths(m_project, timelineUs, videoPaths, audioPaths);
    ClipReaderPool::instance().retainActivePaths(videoPaths, audioPaths);
    ClipReaderPool::instance().setReadAheadUs(options.readAheadUs);

    // Start every clip's decode before compositing anything, so they run in
    // parallel across the per-path worker threads rather than serially below.
    ClipReaderPool::instance().warmVideoFrames(
        collectVideoRequests(m_project, timelineUs, width, height));

    *widthOut = width;
    *heightOut = height;
    *renderScaleOut = renderScale;
    if (sceneOut)
        *sceneOut = buildGpuScene(*m_project, timelineUs, width, height, renderScale, options);
    return true;
}

QImage FrameCompositor::compositeAt(drift::TimeUs timelineUs, const RenderOptions &options) const
{
    GpuScene scene;
    int width = 0;
    int height = 0;
    double renderScale = 1.0;
    if (!prepare(timelineUs, options, &scene, &width, &height, &renderScale))
        return {};

    return GpuCompositor::render(scene);
}

GpuFrameTexture FrameCompositor::compositeToTextureAt(drift::TimeUs timelineUs,
                                                      const RenderOptions &options) const
{
    if (!GpuCompositor::isAvailable())
        return {};

    GpuScene scene;
    int width = 0;
    int height = 0;
    double renderScale = 1.0;
    if (!prepare(timelineUs, options, &scene, &width, &height, &renderScale))
        return {};

    return GpuCompositor::renderToTexture(scene);
}

