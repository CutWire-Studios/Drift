#include "app/bin/SourceLabController.h"

#include "app/editor/AppController.h"
#include "app/bin/AssetLibrary.h"
#include "app/bin/MediaBinController.h"
#include "app/jobs/JobRegistry.h"
#include "app/jobs/audio/DenoiseController.h"
#include "app/jobs/vision/RestoreController.h"
#include "app/jobs/vision/SceneDetectController.h"
#include "app/jobs/vision/StabilizeController.h"
#include "core/project/Project.h"
#include "engine/decode/probe/MediaProbe.h"
#include "engine/decode/readers/ClipReaderPool.h"
#include "engine/encode/MediaEditor.h"
#include "engine/ml/speech/DenoiseAudio.h"
#include "engine/ml/vision/Restorer.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QScopeGuard>
#include <QUrl>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

QString newId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

void sortRanges(QList<drift::SourceRange> &ranges)
{
    std::stable_sort(ranges.begin(), ranges.end(),
                     [](const drift::SourceRange &a, const drift::SourceRange &b) {
                         return a.inUs != b.inUs ? a.inUs < b.inUs : a.outUs < b.outUs;
                     });
}

void sortMarkers(QList<drift::SourceMarker> &markers)
{
    std::stable_sort(markers.begin(), markers.end(),
                     [](const drift::SourceMarker &a, const drift::SourceMarker &b) {
                         return a.timeUs < b.timeUs;
                     });
}

// False when the span is empty once clamped to the asset's length.
bool clampSpan(double inSeconds, double outSeconds, drift::TimeUs duration, drift::TimeUs *inUs,
               drift::TimeUs *outUs)
{
    if (!std::isfinite(inSeconds) || !std::isfinite(outSeconds))
        return false;
    *inUs = qBound<drift::TimeUs>(0, drift::secondsToUs(inSeconds), duration);
    *outUs = qBound<drift::TimeUs>(0, drift::secondsToUs(outSeconds), duration);
    return *outUs > *inUs;
}

drift::SourceRange *findRange(QList<drift::SourceRange> &ranges, const QString &id)
{
    for (drift::SourceRange &range : ranges) {
        if (range.id == id)
            return &range;
    }
    return nullptr;
}

drift::SourceMarker *findMarker(QList<drift::SourceMarker> &markers, const QString &id)
{
    for (drift::SourceMarker &marker : markers) {
        if (marker.id == id)
            return &marker;
    }
    return nullptr;
}

// Where each clip of the asset lands in a render that starts at the recipe's in-point and plays at
// the recipe's speed. A clip left with nothing is kept as one frame at the nearest end rather than
// removed, so the timeline keeps its place; clips whose source span changed get a timeline
// duration to match (the neighbours do not ripple).
void rebaseClips(drift::Project &project, const QString &assetId, drift::TimeUs trimIn, double speed,
                 drift::TimeUs newDuration, double fps)
{
    const drift::TimeUs frame =
        fps > 0.0 ? drift::TimeUs(std::llround(1e6 / fps)) : drift::frameDurationUs(30);
    project.forEachTrackList([&](QList<drift::Track> &tracks) {
        for (drift::Track &track : tracks) {
            for (drift::Clip &clip : track.clips) {
                if (clip.assetId != assetId)
                    continue;
                const drift::TimeUs oldSpan = clip.srcOut - clip.srcIn;
                clip.srcIn = drift::rebaseSourceTime(clip.srcIn, trimIn, speed, newDuration);
                clip.srcOut = drift::rebaseSourceTime(clip.srcOut, trimIn, speed, newDuration);
                if (clip.srcOut <= clip.srcIn) {
                    const drift::TimeUs length = qMin(frame, newDuration);
                    clip.srcIn = qMin(clip.srcIn, qMax<drift::TimeUs>(0, newDuration - length));
                    clip.srcOut = clip.srcIn + length;
                }
                if (clip.srcOut - clip.srcIn == oldSpan)
                    continue;
                if (clip.hasSpeedCurve()) {
                    clip.syncDurationFromSpeedCurve();
                } else {
                    clip.timelineDuration = qMax<drift::TimeUs>(
                        1, drift::TimeUs(std::llround(double(clip.srcOut - clip.srcIn)
                                                      / clip.effectiveSpeed())));
                }
            }
        }
    });
}

// The part of the asset the recipe keeps: in/out in source time, out never open-ended.
bool keptRange(const drift::MediaAsset &asset, drift::TimeUs trimIn, drift::TimeUs trimOut,
               drift::TimeUs *inUs, drift::TimeUs *outUs, bool *toEnd)
{
    const drift::TimeUs duration = asset.durationUs;
    *inUs = qBound<drift::TimeUs>(0, trimIn, duration);
    *toEnd = trimOut < 0 || trimOut >= duration;
    *outUs = *toEnd ? duration : trimOut;
    return *outUs > *inUs;
}

} // namespace

struct SourceLabController::Pipeline
{
    drift::MediaEditSpec spec;
    // The kept range in the same microseconds the stabilize analysis and the denoise pre-pass read,
    // so every pass sees the frames the render will.
    drift::TimeUs rangeIn = 0;
    drift::TimeUs rangeOut = 0;
    bool toEnd = true;
    bool stabilize = false;
    bool denoise = false;
    bool streamCopy = false;
    // Decompress model then upscale model, empty ids dropped.
    QStringList models;
    QStringList steps;
    double speed = 1.0;
    double enhanceSeconds = -1.0;
};

namespace {

struct PipelineHooks
{
    std::function<void(double, const QString &)> progress;
    std::function<bool()> cancelled;
};

// Runs the passes in order, weighting the shared 0..1 by how long each tends to take. Blocking;
// temp files are gone on every way out.
bool runPipeline(drift::MediaEditSpec spec, drift::TimeUs rangeIn, drift::TimeUs rangeOut, bool toEnd,
                 bool stabilize, bool denoise, bool streamCopy, const QStringList &models,
                 const PipelineHooks &hooks, QString *error)
{
    if (streamCopy) {
        drift::TimeUs actualIn = 0;
        return drift::remuxRange(spec.inputPath, spec.outputPath, rangeIn, toEnd ? -1 : rangeOut,
                                 &actualIn, error, [&](double fraction) {
                                     hooks.progress(fraction, QObject::tr("Copying streams…"));
                                     return !hooks.cancelled();
                                 });
    }

    QList<std::shared_ptr<drift::Restorer>> chain;
    if (!models.isEmpty()) {
        hooks.progress(0.0, QObject::tr("Loading the models…"));
        if (!drift::Restorer::loadChain(models, &chain, nullptr, error))
            return false;
    }

    QString trf;
    if (stabilize) {
        trf = StabilizeController::analysisPath(spec.inputPath, rangeIn, rangeOut);
        if (trf.isEmpty()) {
            *error = QObject::tr("Could not create stabilization cache directory");
            return false;
        }
    }
    const bool analyse = stabilize && !QFile::exists(trf);
    const double analyseWeight = analyse ? 2.0 : 0.0;
    const double denoiseWeight = denoise ? 2.0 : 0.0;
    const double encodeWeight = models.isEmpty() ? 4.0 : 12.0;
    const double total = analyseWeight + denoiseWeight + encodeWeight;
    double used = 0.0;
    const auto stage = [&](double weight, const QString &status) {
        const double from = used / total;
        const double span = weight / total;
        used += weight;
        return [&hooks, from, span, status](double fraction) {
            hooks.progress(from + span * fraction, status);
            return !hooks.cancelled();
        };
    };

    if (analyse) {
        const QString part = trf + QStringLiteral(".part");
        QFile::remove(part);
        const bool ok = drift::analyzeVideo(
            spec.inputPath, rangeIn, rangeOut, StabilizeController::detectFilter(),
            {{"vidstabdetect", "result", part}}, error,
            stage(analyseWeight, QObject::tr("Analysing camera motion…")));
        if (!ok || !QFile::exists(part)) {
            QFile::remove(part);
            if (error->isEmpty())
                *error = QObject::tr("Stabilization analysis failed");
            return false;
        }
        QFile::remove(trf);
        QFile::rename(part, trf);
    }
    if (stabilize) {
        spec.videoFilter = StabilizeController::transformFilter(15, false);
        spec.videoFilterOptions = {{"vidstabtransform", "input", trf}};
    }

    QString denoised;
    const auto cleanup = qScopeGuard([&] {
        if (!denoised.isEmpty())
            QFile::remove(denoised);
    });
    if (denoise) {
        denoised = QDir::temp().filePath(
            QStringLiteral("drift-sourcelab-%1.flac").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
        if (!drift::denoiseAudioRange(spec.inputPath, rangeIn, rangeOut, denoised, error,
                                      stage(denoiseWeight, QObject::tr("Reducing noise…"))))
            return false;
        spec.audioSourcePath = denoised;
    }

    if (!chain.isEmpty()) {
        spec.frameHook = [&](QImage &frame, QString *frameError) {
            return drift::Restorer::processChain(
                chain, frame, [&](double) { return !hooks.cancelled(); }, frameError);
        };
    }
    return drift::editMedia(spec, error,
                            stage(encodeWeight, models.isEmpty() ? QObject::tr("Rendering…")
                                                                 : QObject::tr("Enhancing…")));
}

PipelineHooks hooksFor(JobContext &ctx)
{
    return {[&ctx](double fraction, const QString &status) { ctx.progress(fraction, status); },
            [&ctx] { return ctx.cancelled(); }};
}

} // namespace

SourceLabController::SourceLabController(AppController *app, QObject *parent)
    : QObject(parent)
    , m_app(app)
{
    if (AssetLibrary *library = m_app->m_assetLibrary) {
        connect(library, &QAbstractItemModel::modelReset, this, &SourceLabController::refresh);
        connect(library, &QAbstractItemModel::rowsRemoved, this, &SourceLabController::refresh);
    }
    if (m_app->m_bin) {
        connect(m_app->m_bin, &MediaBinController::replaceFinished, this,
                [this](bool ok, const QString &message, int) {
                    if (!m_replaceAssetId.isEmpty())
                        finishReplace(ok, message);
                });
    }
    m_poll.setInterval(200);
    connect(&m_poll, &QTimer::timeout, this, &SourceLabController::pollJob);
}

SourceLabController::~SourceLabController()
{
    if (!m_previewPath.isEmpty())
        QFile::remove(m_previewPath);
}

const drift::MediaAsset *SourceLabController::current() const
{
    return m_assetId.isEmpty() ? nullptr : m_app->m_project.asset(m_assetId);
}

QString SourceLabController::name() const
{
    const drift::MediaAsset *asset = current();
    return asset ? asset->name : QString();
}

QString SourceLabController::kind() const
{
    const drift::MediaAsset *asset = current();
    return asset ? drift::mediaKindToString(asset->kind) : QString();
}

double SourceLabController::durationSeconds() const
{
    const drift::MediaAsset *asset = current();
    return asset ? drift::usToSeconds(asset->durationUs) : 0.0;
}

double SourceLabController::fps() const
{
    const drift::MediaAsset *asset = current();
    return asset ? asset->fps : 0.0;
}

int SourceLabController::width() const
{
    const drift::MediaAsset *asset = current();
    return asset ? asset->width : 0;
}

int SourceLabController::height() const
{
    const drift::MediaAsset *asset = current();
    return asset ? asset->height : 0;
}

bool SourceLabController::hasAudio() const
{
    const drift::MediaAsset *asset = current();
    return asset && (asset->hasAudio || asset->kind == drift::MediaKind::Audio);
}

int SourceLabController::rating() const
{
    const drift::MediaAsset *asset = current();
    return asset ? asset->rating : 0;
}

bool SourceLabController::load(const QString &assetId)
{
    if (!m_app->m_project.asset(assetId))
        return false;
    if (m_assetId == assetId)
        return true;
    discardPreview();
    m_assetId = assetId;
    if (!m_recipes.contains(assetId))
        m_recipes.insert(assetId, recipeFor(*m_app->m_project.asset(assetId)));
    // The id goes last: its listeners read kind and recipe, which must already be the new asset's.
    refresh();
    emit recipeChanged();
    emit availabilityChanged();
    emit currentAssetIdChanged();
    return true;
}

SourceLabController::SourceRecipe SourceLabController::recipeFor(const drift::MediaAsset &asset)
{
    SourceRecipe recipe;
    recipe.trimInUs = asset.trimInUs;
    recipe.trimOutUs = asset.trimOutUs;
    recipe.crop = asset.sourceFrame;
    return recipe;
}

void SourceLabController::resetRecipeToAsset(const QString &assetId)
{
    const drift::MediaAsset *asset = m_app->m_project.asset(assetId);
    if (!asset)
        return;
    m_recipes.insert(assetId, recipeFor(*asset));
    if (assetId == m_assetId)
        emit recipeChanged();
}

void SourceLabController::clear()
{
    if (m_assetId.isEmpty())
        return;
    discardPreview();
    m_assetId.clear();
    refresh();
    emit recipeChanged();
    emit currentAssetIdChanged();
}

void SourceLabController::refresh()
{
    if (!m_assetId.isEmpty() && !m_app->m_project.asset(m_assetId)) {
        clear();
        return;
    }
    // Recipes of assets that left the project would otherwise come back if a new asset reused
    // the id after an undo and redo.
    for (auto it = m_recipes.begin(); it != m_recipes.end();) {
        it = m_app->m_project.asset(it.key()) ? std::next(it) : m_recipes.erase(it);
    }

    QVariantList ranges;
    QVariantList markers;
    if (const drift::MediaAsset *asset = current()) {
        for (const drift::SourceRange &range : asset->ranges) {
            ranges.append(QVariantMap{{QStringLiteral("id"), range.id},
                                      {QStringLiteral("in"), drift::usToSeconds(range.inUs)},
                                      {QStringLiteral("out"), drift::usToSeconds(range.outUs)},
                                      {QStringLiteral("rating"), range.rating},
                                      {QStringLiteral("name"), range.name},
                                      {QStringLiteral("note"), range.note}});
        }
        for (const drift::SourceMarker &marker : asset->markers) {
            markers.append(QVariantMap{{QStringLiteral("id"), marker.id},
                                       {QStringLiteral("time"), drift::usToSeconds(marker.timeUs)},
                                       {QStringLiteral("name"), marker.name},
                                       {QStringLiteral("note"), marker.note}});
        }
    }
    m_ranges = ranges;
    m_markers = markers;
    emit assetChanged();
    emit rangesChanged();
    emit markersChanged();
}

template <typename Fn>
bool SourceLabController::edit(const QString &text, Fn &&change)
{
    drift::MediaAsset *asset = m_assetId.isEmpty() ? nullptr : m_app->m_project.asset(m_assetId);
    if (!asset)
        return false;
    const drift::Project before = m_app->m_project.detachedCopy();
    if (!change(*asset))
        return false;
    m_app->pushProjectEdit(before, text);
    m_app->settleUndoRefresh();
    if (m_app->m_assetLibrary)
        m_app->m_assetLibrary->refreshLabRoles();
    refresh();
    return true;
}

QString SourceLabController::addRange(double inSeconds, double outSeconds)
{
    QString id;
    edit(tr("Add range"), [&](drift::MediaAsset &asset) {
        drift::SourceRange range;
        if (!clampSpan(inSeconds, outSeconds, asset.durationUs, &range.inUs, &range.outUs))
            return false;
        range.id = newId();
        id = range.id;
        asset.ranges.append(range);
        sortRanges(asset.ranges);
        return true;
    });
    return id;
}

bool SourceLabController::updateRange(const QString &id, double inSeconds, double outSeconds)
{
    return edit(tr("Edit range"), [&](drift::MediaAsset &asset) {
        drift::SourceRange *range = findRange(asset.ranges, id);
        drift::TimeUs inUs = 0;
        drift::TimeUs outUs = 0;
        if (!range || !clampSpan(inSeconds, outSeconds, asset.durationUs, &inUs, &outUs)
            || (range->inUs == inUs && range->outUs == outUs))
            return false;
        range->inUs = inUs;
        range->outUs = outUs;
        sortRanges(asset.ranges);
        return true;
    });
}

bool SourceLabController::setRangeRating(const QString &id, int rating)
{
    return edit(tr("Rate range"), [&](drift::MediaAsset &asset) {
        drift::SourceRange *range = findRange(asset.ranges, id);
        const int clamped = qBound(-1, rating, 1);
        if (!range || range->rating == clamped)
            return false;
        range->rating = clamped;
        return true;
    });
}

bool SourceLabController::renameRange(const QString &id, const QString &name)
{
    return edit(tr("Rename range"), [&](drift::MediaAsset &asset) {
        drift::SourceRange *range = findRange(asset.ranges, id);
        if (!range || range->name == name)
            return false;
        range->name = name;
        return true;
    });
}

bool SourceLabController::setRangeNote(const QString &id, const QString &note)
{
    return edit(tr("Edit range note"), [&](drift::MediaAsset &asset) {
        drift::SourceRange *range = findRange(asset.ranges, id);
        if (!range || range->note == note)
            return false;
        range->note = note;
        return true;
    });
}

bool SourceLabController::removeRange(const QString &id)
{
    return edit(tr("Remove range"), [&](drift::MediaAsset &asset) {
        return asset.ranges.removeIf([&](const drift::SourceRange &r) { return r.id == id; }) > 0;
    });
}

bool SourceLabController::replaceRanges(const QVariantList &ranges)
{
    return edit(tr("Replace ranges"), [&](drift::MediaAsset &asset) {
        QList<drift::SourceRange> next;
        for (const QVariant &entry : ranges) {
            const QVariantMap map = entry.toMap();
            drift::SourceRange range;
            if (!clampSpan(map.value(QStringLiteral("in")).toDouble(),
                           map.value(QStringLiteral("out")).toDouble(), asset.durationUs,
                           &range.inUs, &range.outUs))
                continue;
            range.id = map.value(QStringLiteral("id")).toString();
            if (range.id.isEmpty())
                range.id = newId();
            range.rating = qBound(-1, map.value(QStringLiteral("rating")).toInt(), 1);
            range.name = map.value(QStringLiteral("name")).toString();
            range.note = map.value(QStringLiteral("note")).toString();
            next.append(range);
        }
        sortRanges(next);
        asset.ranges = next;
        return true;
    });
}

QString SourceLabController::addMarker(double timeSeconds)
{
    QString id;
    edit(tr("Add marker"), [&](drift::MediaAsset &asset) {
        if (!std::isfinite(timeSeconds))
            return false;
        drift::SourceMarker marker;
        marker.id = newId();
        marker.timeUs = qBound<drift::TimeUs>(0, drift::secondsToUs(timeSeconds), asset.durationUs);
        id = marker.id;
        asset.markers.append(marker);
        sortMarkers(asset.markers);
        return true;
    });
    return id;
}

bool SourceLabController::moveMarker(const QString &id, double timeSeconds)
{
    return edit(tr("Move marker"), [&](drift::MediaAsset &asset) {
        drift::SourceMarker *marker = findMarker(asset.markers, id);
        if (!marker || !std::isfinite(timeSeconds))
            return false;
        const drift::TimeUs timeUs =
            qBound<drift::TimeUs>(0, drift::secondsToUs(timeSeconds), asset.durationUs);
        if (marker->timeUs == timeUs)
            return false;
        marker->timeUs = timeUs;
        sortMarkers(asset.markers);
        return true;
    });
}

bool SourceLabController::renameMarker(const QString &id, const QString &name)
{
    return edit(tr("Rename marker"), [&](drift::MediaAsset &asset) {
        drift::SourceMarker *marker = findMarker(asset.markers, id);
        if (!marker || marker->name == name)
            return false;
        marker->name = name;
        return true;
    });
}

bool SourceLabController::setMarkerNote(const QString &id, const QString &note)
{
    return edit(tr("Edit marker note"), [&](drift::MediaAsset &asset) {
        drift::SourceMarker *marker = findMarker(asset.markers, id);
        if (!marker || marker->note == note)
            return false;
        marker->note = note;
        return true;
    });
}

bool SourceLabController::removeMarker(const QString &id)
{
    return edit(tr("Remove marker"), [&](drift::MediaAsset &asset) {
        return asset.markers.removeIf([&](const drift::SourceMarker &m) { return m.id == id; }) > 0;
    });
}

double SourceLabController::nearestZeroCrossing(double seconds, double windowSeconds) const
{
    constexpr quint64 kZeroCrossStreamId = 0xA5'11'5C'A4'00'00'00'03ull;
    constexpr int kRate = 48000;
    const drift::MediaAsset *asset = current();
    if (!asset || !hasAudio() || windowSeconds <= 0.0)
        return seconds;
    const double start = std::max(0.0, seconds - windowSeconds);
    const int frames = int(std::ceil((seconds + windowSeconds - start) * kRate));
    std::vector<float> pcm(size_t(frames) * 2, 0.0f);
    const int got = ClipReaderPool::instance().readAudioInterleaved(
        asset->path, kZeroCrossStreamId, drift::secondsToUs(start), frames, kRate, pcm.data());
    if (got < 2)
        return seconds;
    const auto mono = [&](int i) { return pcm[size_t(i) * 2] + pcm[size_t(i) * 2 + 1]; };
    const int center = std::clamp(int(std::lround((seconds - start) * kRate)), 0, got - 1);
    for (int d = 0; d < got; ++d) {
        for (const int i : {center - d, center + d}) {
            if (i < 1 || i >= got)
                continue;
            const float a = mono(i - 1);
            const float b = mono(i);
            if ((a <= 0.0f && b > 0.0f) || (a >= 0.0f && b < 0.0f))
                return start + double(i) / kRate;
        }
    }
    return seconds;
}

bool SourceLabController::setAssetRating(int rating)
{
    return edit(tr("Rate source"), [&](drift::MediaAsset &asset) {
        const int clamped = qBound(-1, rating, 1);
        if (asset.rating == clamped)
            return false;
        asset.rating = clamped;
        return true;
    });
}

QStringList SourceLabController::makeSubclips(const QStringList &rangeIds)
{
    QStringList created;
    const drift::MediaAsset *source = current();
    if (!source || !m_app->m_bin)
        return created;

    // Copied: appendSubclip inserts into the project's asset table, which can invalidate `source`.
    const QList<drift::SourceRange> ranges = source->ranges;
    const drift::Project before = m_app->m_project.detachedCopy();
    for (const drift::SourceRange &range : ranges) {
        if (!rangeIds.contains(range.id))
            continue;
        const QString id = m_app->m_bin->appendSubclip(m_assetId, range.inUs, range.outUs,
                                                       range.name, range.rating);
        if (!id.isEmpty())
            created.append(id);
    }
    if (created.isEmpty())
        return created;
    m_app->pushProjectEdit(before, tr("Make sub-clips"));
    m_app->settleUndoRefresh();
    if (m_app->m_assetLibrary)
        m_app->m_assetLibrary->refreshLabRoles();
    return created;
}

QVariantMap SourceLabController::recipe() const
{
    const auto it = m_recipes.constFind(m_assetId);
    if (it == m_recipes.constEnd())
        return {};
    const SourceRecipe &r = it.value();
    return {{QStringLiteral("trimIn"), drift::usToSeconds(r.trimInUs)},
            {QStringLiteral("trimOut"), r.trimOutUs < 0 ? -1.0 : drift::usToSeconds(r.trimOutUs)},
            {QStringLiteral("crop"), r.crop},
            {QStringLiteral("aspect"), r.aspect},
            {QStringLiteral("speed"), r.speed},
            {QStringLiteral("keepPitch"), r.keepPitch},
            {QStringLiteral("gainDb"), r.gainDb},
            {QStringLiteral("normalizeLufs"), r.normalizeLufs},
            {QStringLiteral("denoise"), r.denoise},
            {QStringLiteral("stabilize"), r.stabilize},
            {QStringLiteral("decompressModel"), r.decompressModel},
            {QStringLiteral("upscaleModel"), r.upscaleModel}};
}

bool SourceLabController::recipeIsPureTrimCrop() const
{
    const auto it = m_recipes.constFind(m_assetId);
    if (it == m_recipes.constEnd())
        return true;
    const SourceRecipe &r = it.value();
    return r.speed == 1.0 && r.gainDb == 0.0 && std::isnan(r.normalizeLufs) && !r.denoise
           && !r.stabilize && r.decompressModel.isEmpty() && r.upscaleModel.isEmpty();
}

bool SourceLabController::recipeDirty() const
{
    const auto it = m_recipes.constFind(m_assetId);
    const drift::MediaAsset *asset = current();
    if (it == m_recipes.constEnd() || !asset)
        return false;
    const SourceRecipe &r = it.value();
    return !recipeIsPureTrimCrop() || r.trimInUs != asset->trimInUs
           || r.trimOutUs != asset->trimOutUs || r.crop != asset->sourceFrame;
}

bool SourceLabController::setRecipeValue(const QString &key, const QVariant &value)
{
    const auto it = m_recipes.find(m_assetId);
    if (it == m_recipes.end())
        return false;
    SourceRecipe &r = it.value();

    if (key == QLatin1String("trimIn") || key == QLatin1String("trimOut")) {
        const double seconds = value.toDouble();
        if (!std::isfinite(seconds))
            return false;
        if (key == QLatin1String("trimIn"))
            r.trimInUs = qMax<drift::TimeUs>(0, drift::secondsToUs(seconds));
        else
            r.trimOutUs = seconds < 0.0 ? -1 : drift::secondsToUs(seconds);
    } else if (key == QLatin1String("crop")) {
        QRectF rect = value.toRectF();
        if (value.typeId() == QMetaType::QVariantMap) {
            const QVariantMap map = value.toMap();
            rect = QRectF(map.value(QStringLiteral("x")).toDouble(),
                          map.value(QStringLiteral("y")).toDouble(),
                          map.value(QStringLiteral("width")).toDouble(),
                          map.value(QStringLiteral("height")).toDouble());
        }
        r.crop = drift::normalizedSourceFrame(rect.x(), rect.y(), rect.width(), rect.height());
    } else if (key == QLatin1String("aspect")) {
        r.aspect = value.toString();
    } else if (key == QLatin1String("speed")) {
        const double speed = value.toDouble();
        if (!std::isfinite(speed) || speed <= 0.0)
            return false;
        r.speed = speed;
    } else if (key == QLatin1String("keepPitch")) {
        r.keepPitch = value.toBool();
    } else if (key == QLatin1String("gainDb")) {
        const double gain = value.toDouble();
        if (!std::isfinite(gain))
            return false;
        r.gainDb = gain;
    } else if (key == QLatin1String("normalizeLufs")) {
        r.normalizeLufs = value.isNull() || !value.isValid() ? std::numeric_limits<double>::quiet_NaN()
                                                              : value.toDouble();
    } else if (key == QLatin1String("denoise")) {
        r.denoise = value.toBool();
    } else if (key == QLatin1String("stabilize")) {
        r.stabilize = value.toBool();
    } else if (key == QLatin1String("decompressModel")) {
        r.decompressModel = value.toString();
    } else if (key == QLatin1String("upscaleModel")) {
        r.upscaleModel = value.toString();
    } else {
        return false;
    }
    emit recipeChanged();
    return true;
}

void SourceLabController::resetRecipe(const QString &section)
{
    const auto it = m_recipes.find(m_assetId);
    if (it == m_recipes.end())
        return;
    SourceRecipe &r = it.value();
    const SourceRecipe defaults;
    const bool all = section == QLatin1String("all");
    if (all || section == QLatin1String("trim")) {
        r.trimInUs = defaults.trimInUs;
        r.trimOutUs = defaults.trimOutUs;
    }
    if (all || section == QLatin1String("crop")) {
        r.crop = defaults.crop;
        r.aspect = defaults.aspect;
    }
    if (all || section == QLatin1String("speed")) {
        r.speed = defaults.speed;
        r.keepPitch = defaults.keepPitch;
    }
    if (all || section == QLatin1String("audio")) {
        r.gainDb = defaults.gainDb;
        r.normalizeLufs = defaults.normalizeLufs;
        r.denoise = defaults.denoise;
    }
    if (all || section == QLatin1String("stabilize"))
        r.stabilize = defaults.stabilize;
    if (all || section == QLatin1String("enhance")) {
        r.decompressModel = defaults.decompressModel;
        r.upscaleModel = defaults.upscaleModel;
    }
    emit recipeChanged();
}

// --- applying the recipe ----------------------------------------------------------------------

double SourceLabController::etaSeconds() const
{
    return m_busy && m_etaTotal > 0.0 ? m_etaTotal * (1.0 - m_progress) : -1.0;
}

bool SourceLabController::enhanceAvailable() const
{
    return !drift::Restorer::models().isEmpty();
}

bool SourceLabController::stabilizeAvailable() const
{
    return drift::hasVideoFilter("vidstabdetect") && drift::hasVideoFilter("vidstabtransform");
}

bool SourceLabController::denoiseAvailable() const
{
    return m_app->m_denoise && m_app->m_denoise->available();
}

QStringList SourceLabController::availableVideoCodecs() const
{
    return drift::availableVideoCodecs();
}

void SourceLabController::setLastError(const QString &message)
{
    if (m_lastError == message)
        return;
    m_lastError = message;
    emit lastErrorChanged();
    if (!message.isEmpty())
        m_app->setLastMessage(message, QStringLiteral("error"));
}

void SourceLabController::beginBusy(const QString &status)
{
    m_busy = true;
    m_progress = 0.0;
    m_status = status;
    emit busyChanged();
    emit progressChanged();
}

void SourceLabController::endBusy()
{
    m_poll.stop();
    m_jobId.clear();
    m_splitCancel.reset();
    m_etaTotal = -1.0;
    m_busy = false;
    emit busyChanged();
    emit progressChanged();
}

void SourceLabController::pollJob()
{
    if (m_jobId.isEmpty())
        return;
    const QJsonObject job = m_app->m_jobs->job(m_jobId);
    if (job.isEmpty() || !job.value(QStringLiteral("active")).toBool())
        return;
    m_progress = job.value(QStringLiteral("progress")).toDouble();
    m_status = job.value(QStringLiteral("status")).toString();
    emit progressChanged();
}

void SourceLabController::cancel()
{
    if (m_splitCancel)
        m_splitCancel->storeRelaxed(1);
    if (!m_jobId.isEmpty())
        m_app->m_jobs->cancel(m_jobId);
}

bool SourceLabController::buildPipeline(const QString &mode, const QVariantMap &exportOptions,
                                        Pipeline *out, QString *error) const
{
    const drift::MediaAsset *asset = current();
    if (!asset) {
        *error = tr("Open a source first");
        return false;
    }
    const bool video = asset->kind == drift::MediaKind::Video;
    if (!video && asset->kind != drift::MediaKind::Audio) {
        *error = tr("Only video and audio can be processed");
        return false;
    }
    if (!QFileInfo(asset->path).isFile()) {
        *error = tr("Could not open the media file");
        return false;
    }
    const SourceRecipe r = m_recipes.value(m_assetId);

    Pipeline p;
    drift::TimeUs inUs = 0;
    drift::TimeUs outUs = 0;
    if (!keptRange(*asset, r.trimInUs, r.trimOutUs, &inUs, &outUs, &p.toEnd)) {
        *error = tr("The trim leaves nothing to keep");
        return false;
    }
    p.speed = r.speed;
    drift::MediaEditSpec &spec = p.spec;
    spec.inputPath = asset->path;
    spec.kind = drift::mediaKindToString(asset->kind);
    spec.inSeconds = drift::usToSeconds(inUs);
    spec.outSeconds = p.toEnd ? -1.0 : drift::usToSeconds(outUs);
    p.rangeIn = drift::secondsToUs(spec.inSeconds);
    p.rangeOut = outUs;

    const bool fullCrop = r.crop == QRectF(0, 0, 1, 1);
    const bool exporting = mode == QLatin1String("export");
    if (exporting && exportOptions.value(QStringLiteral("streamCopy")).toBool()) {
        if (!recipeIsPureTrimCrop() || !fullCrop) {
            *error = tr("Only a plain trim can be copied without re-encoding");
            return false;
        }
        p.streamCopy = true;
        p.steps << tr("Trim by copying the streams (no re-encode)");
    }

    if (!p.streamCopy) {
        if (video) {
            spec.cropX = r.crop.x();
            spec.cropY = r.crop.y();
            spec.cropW = r.crop.width();
            spec.cropH = r.crop.height();
            // The crop was drawn against the bin's rotation correction, as in MediaBinController::saveEdit.
            spec.rotationOverride = drift::effectiveRotation(*asset);
        }
        spec.speed = r.speed;
        spec.keepPitch = r.keepPitch;
        spec.gainDb = r.gainDb;
        spec.normalizeLufs = r.normalizeLufs;
        p.stabilize = video && r.stabilize;
        p.denoise = r.denoise && hasAudio();
        if (p.stabilize && !stabilizeAvailable()) {
            *error = tr("This build of Drift has no video stabilization support");
            return false;
        }
        if (p.denoise && !denoiseAvailable()) {
            *error = tr("The noise-removal model is not installed");
            return false;
        }
        if (video) {
            for (const QString &model : {r.decompressModel, r.upscaleModel}) {
                if (!model.isEmpty())
                    p.models << model;
            }
        }

        if (p.stabilize)
            p.steps << tr("Stabilize the camera motion");
        if (p.denoise)
            p.steps << tr("Reduce audio noise");
        p.steps << tr("Trim to the kept range");
        if (video && !fullCrop)
            p.steps << tr("Crop the picture");
        if (!r.decompressModel.isEmpty())
            p.steps << tr("Remove compression artifacts");
        if (!r.upscaleModel.isEmpty())
            p.steps << tr("Upscale the picture");
        if (r.speed != 1.0)
            p.steps << tr("Change the speed to %1x").arg(r.speed);
        if (r.gainDb != 0.0)
            p.steps << tr("Change the volume by %1 dB").arg(r.gainDb);
        if (!std::isnan(r.normalizeLufs))
            p.steps << tr("Normalize the loudness to %1 LUFS").arg(r.normalizeLufs);
        p.steps << tr("Re-encode the result");
    }

    if (exporting) {
        drift::MediaOutputOptions &output = spec.output;
        output.container = exportOptions.value(QStringLiteral("container")).toString();
        output.videoCodec = exportOptions.value(QStringLiteral("videoCodec")).toString();
        output.crf = exportOptions.value(QStringLiteral("crf"), -1).toInt();
        output.maxHeight = exportOptions.value(QStringLiteral("maxHeight"), 0).toInt();
        output.audioBitrateKbps = exportOptions.value(QStringLiteral("audioBitrateKbps"), 0).toInt();
        output.wavBitDepth = exportOptions.value(QStringLiteral("wavBitDepth"), 16).toInt();
    }

    if (!p.models.isEmpty()) {
        double secondsPerMegapixel = 0.0;
        for (const QString &id : std::as_const(p.models)) {
            for (const drift::Restorer::Model &model : drift::Restorer::models()) {
                if (model.id == id)
                    secondsPerMegapixel += model.secondsPerMegapixel;
            }
        }
        if (secondsPerMegapixel > 0.0) {
            double width = asset->width;
            double height = asset->height;
            if (drift::effectiveRotation(*asset) % 180 == 90)
                std::swap(width, height);
            const double megapixels = width * r.crop.width() * height * r.crop.height() / 1e6;
            const double fps = asset->fps > 0.0 ? asset->fps : m_app->m_project.fps();
            const double frames = drift::usToSeconds(p.rangeOut - p.rangeIn) / r.speed * fps;
            const double factor =
                m_app->m_restore->speed().value(QStringLiteral("factor"), 1.0).toDouble();
            p.enhanceSeconds = secondsPerMegapixel * factor * megapixels * frames;
        }
    }
    *out = p;
    return true;
}

QVariantMap SourceLabController::plan(const QString &mode, const QVariantMap &exportOptions) const
{
    const drift::MediaAsset *asset = current();
    QVariantMap result{{QStringLiteral("encode"), false},
                       {QStringLiteral("streamCopy"), false},
                       {QStringLiteral("snappedIn"), 0.0},
                       {QStringLiteral("steps"), QStringList()},
                       {QStringLiteral("outputDuration"), 0.0}};
    if (!asset)
        return result;
    const SourceRecipe r = m_recipes.value(m_assetId);
    drift::TimeUs inUs = 0;
    drift::TimeUs outUs = 0;
    bool toEnd = false;
    keptRange(*asset, r.trimInUs, r.trimOutUs, &inUs, &outUs, &toEnd);
    result.insert(QStringLiteral("snappedIn"), drift::usToSeconds(inUs));
    result.insert(QStringLiteral("outputDuration"),
                  drift::usToSeconds(outUs - inUs) / (r.speed > 0.0 ? r.speed : 1.0));

    if (mode != QLatin1String("export") && recipeIsPureTrimCrop()) {
        result.insert(QStringLiteral("steps"),
                      QStringList{mode == QLatin1String("copy")
                                      ? tr("Add a sub-clip with the trim and crop (no re-encode)")
                                      : tr("Set the trim and crop on the source (no re-encode)")});
        return result;
    }

    Pipeline p;
    QString error;
    if (!buildPipeline(mode, exportOptions, &p, &error)) {
        result.insert(QStringLiteral("error"), error);
        return result;
    }
    result.insert(QStringLiteral("encode"), !p.streamCopy);
    result.insert(QStringLiteral("streamCopy"), p.streamCopy);
    result.insert(QStringLiteral("steps"), p.steps);
    result.insert(QStringLiteral("enhanceSeconds"), p.enhanceSeconds);
    if (p.streamCopy) {
        const drift::TimeUs snapped = drift::keyframeAtOrBefore(asset->path, p.rangeIn);
        result.insert(QStringLiteral("snappedIn"), drift::usToSeconds(snapped));
        result.insert(QStringLiteral("outputDuration"), drift::usToSeconds(p.rangeOut - snapped));
    }
    return result;
}

bool SourceLabController::applyWithoutEncode(const QString &mode)
{
    const drift::MediaAsset *asset = current();
    if (!asset)
        return false;
    const SourceRecipe r = m_recipes.value(m_assetId);
    const bool image = asset->kind == drift::MediaKind::Image;
    const QString assetId = m_assetId;
    drift::TimeUs inUs = 0;
    drift::TimeUs outUs = 0;
    bool toEnd = false;
    if (!image && !keptRange(*asset, r.trimInUs, r.trimOutUs, &inUs, &outUs, &toEnd)) {
        setLastError(tr("The trim leaves nothing to keep"));
        return false;
    }

    const drift::Project before = m_app->m_project.detachedCopy();
    if (mode == QLatin1String("replace")) {
        bool changed = false;
        if (!image) {
            changed = m_app->m_assetLibrary
                      && m_app->m_assetLibrary->setAssetTrim(m_app->m_assetLibrary->indexOfId(assetId),
                                                             inUs, toEnd ? -1 : outUs);
        }
        drift::MediaAsset *target = m_app->m_project.asset(assetId);
        if (asset->kind != drift::MediaKind::Audio && target->sourceFrame != r.crop) {
            target->sourceFrame = r.crop;
            changed = true;
        }
        if (!changed)
            return false;
        m_app->pushProjectEdit(before, tr("Apply source edit"));
        m_app->settleUndoRefresh();
    } else {
        const QString name = tr("%1 (edited)").arg(asset->name);
        const QString id = m_app->m_bin->appendSubclip(assetId, inUs, outUs, name, 0);
        if (id.isEmpty()) {
            setLastError(tr("Could not add the edited copy"));
            return false;
        }
        drift::MediaAsset *copy = m_app->m_project.asset(id);
        if (copy->kind == drift::MediaKind::Video)
            copy->sourceFrame = r.crop;
        m_app->pushProjectEdit(before, tr("Add edited copy"));
        m_app->settleUndoRefresh();
    }
    if (m_app->m_assetLibrary)
        m_app->m_assetLibrary->refreshLabRoles();
    resetRecipeToAsset(assetId);
    refresh();
    return true;
}

QString SourceLabController::apply(const QString &mode, const QVariantMap &exportOptions)
{
    setLastError({});
    const bool exporting = mode == QLatin1String("export");
    if (!exporting && mode != QLatin1String("replace") && mode != QLatin1String("copy")) {
        setLastError(tr("Unknown action"));
        return {};
    }
    if (m_busy) {
        setLastError(tr("Another operation is still running"));
        return {};
    }
    const drift::MediaAsset *asset = current();
    if (!asset) {
        setLastError(tr("Open a source first"));
        return {};
    }
    if (!exporting && recipeIsPureTrimCrop()) {
        if (asset->kind == drift::MediaKind::Image && mode == QLatin1String("copy")) {
            setLastError(tr("Copying an image is not supported"));
            return {};
        }
        applyWithoutEncode(mode);
        return {};
    }

    Pipeline p;
    QString error;
    if (!buildPipeline(mode, exportOptions, &p, &error)) {
        setLastError(error);
        return {};
    }
    if (exporting) {
        QString path = exportOptions.value(QStringLiteral("path")).toString();
        if (path.startsWith(QLatin1String("file:")))
            path = QUrl(path).toLocalFile();
        if (path.isEmpty()) {
            setLastError(tr("Choose where to save the export"));
            return {};
        }
        p.spec.outputPath = path;
    } else {
        p.spec.outputPath = drift::newEditedMediaPath(m_app->m_project.id(), p.spec.kind);
        if (p.spec.outputPath.isEmpty()) {
            setLastError(tr("Could not create an output file"));
            return {};
        }
    }

    m_app->setPlaying(false);
    m_etaTotal = p.enhanceSeconds;
    beginBusy(tr("Starting…"));
    const QString assetId = m_assetId;
    auto info = std::make_shared<MediaInfo>();
    QPointer<SourceLabController> self(this);
    m_jobId = m_app->m_jobs->start(
        QStringLiteral("sourceLab.process"), assetId, JobRegistry::Lane::Model,
        [p, exporting, info](JobContext &ctx) {
            QString error;
            const bool ok = runPipeline(p.spec, p.rangeIn, p.rangeOut, p.toEnd, p.stabilize, p.denoise,
                                        p.streamCopy, p.models, hooksFor(ctx), &error);
            if (ctx.cancelled())
                return;
            if (!ok)
                return ctx.fail(QStringLiteral("render_error"), error);
            if (!exporting) {
                *info = MediaProbe::probe(p.spec.outputPath);
                if (!info->ok)
                    return ctx.fail(QStringLiteral("render_error"),
                                    QObject::tr("The rendered file could not be read"));
            }
            ctx.succeed({{QStringLiteral("path"), p.spec.outputPath}});
        },
        [self, p, mode, assetId, info](const QJsonObject &job) {
            if (self)
                self->finishJob(job, p, mode, assetId, *info);
            return QJsonObject{};
        });
    m_poll.start();
    return m_jobId;
}

void SourceLabController::finishJob(const QJsonObject &job, const Pipeline &pipeline,
                                    const QString &mode, const QString &assetId,
                                    const MediaInfo &info)
{
    m_poll.stop();
    const QString output = pipeline.spec.outputPath;
    const bool exporting = mode == QLatin1String("export");
    if (!job.value(QStringLiteral("ok")).toBool()) {
        if (!exporting)
            QFile::remove(output);
        const QJsonObject error = job.value(QStringLiteral("error")).toObject();
        const QString message = error.value(QStringLiteral("message")).toString();
        m_status = error.value(QStringLiteral("code")).toString() == QLatin1String("cancelled")
                       ? tr("Cancelled")
                       : message;
        endBusy();
        if (m_status != tr("Cancelled"))
            setLastError(message);
        return;
    }

    m_progress = 1.0;
    if (exporting) {
        m_status = tr("Done");
        endBusy();
        m_app->setLastMessage(tr("Exported to %1").arg(QFileInfo(output).fileName()));
        emit exported(output);
        return;
    }

    const drift::MediaAsset *asset = m_app->m_project.asset(assetId);
    if (!asset) {
        QFile::remove(output);
        endBusy();
        setLastError(tr("The source left the project while it was being processed"));
        return;
    }

    if (mode == QLatin1String("copy")) {
        const QString name = tr("%1 (edited)").arg(asset->name);
        const QString folderId = asset->folderId;
        if (asset->kind == drift::MediaKind::Video)
            m_app->addRenderedVideoAsset(output, name, folderId, info, tr("Add edited copy"));
        else
            m_app->addRenderedAudioAsset(output, name, folderId, info, tr("Add edited copy"));
        if (m_app->m_assetLibrary)
            m_app->m_assetLibrary->refreshLabRoles();
        resetRecipeToAsset(assetId);
        m_status = tr("Done");
        endBusy();
        m_app->setLastMessage(tr("Edited copy added to the media bin"));
        return;
    }

    m_status = tr("Updating the library…");
    emit progressChanged();
    startReplace(assetId, output, pipeline.rangeIn, pipeline.speed);
}

void SourceLabController::startReplace(const QString &assetId, const QString &outputPath,
                                       drift::TimeUs trimIn, double speed)
{
    m_replaceAssetId = assetId;
    m_replaceOutput = outputPath;
    const int index = m_app->m_assetLibrary ? m_app->m_assetLibrary->indexOfId(assetId) : -1;
    const bool started = index >= 0
        && m_app->m_bin->replaceSourceWith(
            index, QUrl::fromLocalFile(outputPath), [this, assetId, trimIn, speed](drift::MediaAsset &replacement) {
                if (const drift::MediaAsset *old = m_app->m_project.asset(assetId)) {
                    replacement.name = old->name;
                    replacement.rating = old->rating;
                    replacement.editFriendly = old->editFriendly;
                    replacement.ranges = drift::rebaseRanges(old->ranges, trimIn, speed, replacement.durationUs);
                    replacement.markers = drift::rebaseMarkers(old->markers, trimIn, speed, replacement.durationUs);
                }
                // The render is on a constant-rate grid.
                replacement.frameRateKnown = true;
                replacement.variableFrameRate = false;
                rebaseClips(m_app->m_project, assetId, trimIn, speed, replacement.durationUs,
                            replacement.fps);
            });
    // A refusal has already reported through replaceFinished, which cleared this.
    if (!started && !m_replaceAssetId.isEmpty())
        finishReplace(false, tr("Could not update the library"));
}

void SourceLabController::finishReplace(bool ok, const QString &message)
{
    const QString assetId = std::exchange(m_replaceAssetId, QString());
    const QString output = std::exchange(m_replaceOutput, QString());
    if (!ok) {
        QFile::remove(output);
        endBusy();
        setLastError(message);
        return;
    }
    if (m_app->m_assetLibrary)
        m_app->m_assetLibrary->refreshLabRoles();
    resetRecipeToAsset(assetId);
    refresh();
    m_status = tr("Done");
    endBusy();
    m_app->setLastMessage(tr("Source replaced"));
}

// --- A/B preview ------------------------------------------------------------------------------

void SourceLabController::discardPreview()
{
    if (!m_previewJobId.isEmpty())
        m_app->m_jobs->cancel(m_previewJobId);
    ++m_previewSerial;
    m_previewJobId.clear();
    if (m_previewBusy) {
        m_previewBusy = false;
        emit previewBusyChanged();
    }
    if (!m_previewPath.isEmpty())
        QFile::remove(m_previewPath);
    m_previewPath.clear();
    m_previewKey.clear();
}

QString SourceLabController::renderPreviewSegment(double centerSeconds, double lengthSeconds)
{
    if (!std::isfinite(centerSeconds) || !std::isfinite(lengthSeconds))
        return {};
    Pipeline p;
    QString error;
    if (!buildPipeline(QStringLiteral("preview"), {}, &p, &error)) {
        setLastError(error);
        return {};
    }
    // Upscaling a clip just to compare it is the enhance panel's single-frame preview instead.
    p.models.clear();

    const drift::TimeUs length = drift::secondsToUs(qMax(0.5, lengthSeconds));
    const drift::TimeUs lo = p.rangeIn;
    const drift::TimeUs hi = p.rangeOut;
    const drift::TimeUs start =
        qBound(lo, drift::secondsToUs(centerSeconds) - length / 2, qMax(lo, hi - length));
    const drift::TimeUs end = qMin(hi, start + length);
    p.spec.inSeconds = drift::usToSeconds(start);
    p.spec.outSeconds = drift::usToSeconds(end);
    p.rangeIn = drift::secondsToUs(p.spec.inSeconds);
    p.rangeOut = end;
    p.toEnd = false;

    const QString key = QStringList{p.spec.inputPath, QString::number(p.rangeIn), QString::number(p.rangeOut),
                                    QString::number(p.spec.cropX), QString::number(p.spec.cropY),
                                    QString::number(p.spec.cropW), QString::number(p.spec.cropH),
                                    QString::number(p.spec.speed), QString::number(p.spec.keepPitch),
                                    QString::number(p.spec.gainDb), QString::number(p.spec.normalizeLufs),
                                    QString::number(p.denoise), QString::number(p.stabilize)}
                            .join(QLatin1Char('|'));
    if (key == m_previewKey && QFile::exists(m_previewPath)) {
        emit previewSegmentReady(m_previewPath, drift::usToSeconds(m_previewStartUs));
        return {};
    }

    if (!m_previewJobId.isEmpty())
        m_app->m_jobs->cancel(m_previewJobId);
    const quint64 serial = ++m_previewSerial;
    const QString extension = p.spec.kind == QLatin1String("video") ? QStringLiteral("mp4")
                                                                    : QStringLiteral("flac");
    p.spec.outputPath = QDir::temp().filePath(
        QStringLiteral("drift-sourcelab-preview-%1.%2")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces), extension));

    if (!m_previewBusy) {
        m_previewBusy = true;
        emit previewBusyChanged();
    }
    QPointer<SourceLabController> self(this);
    m_previewJobId = m_app->m_jobs->start(
        QStringLiteral("sourceLab.preview"), m_assetId, JobRegistry::Lane::Model,
        [p](JobContext &ctx) {
            QString error;
            const bool ok = runPipeline(p.spec, p.rangeIn, p.rangeOut, p.toEnd, p.stabilize, p.denoise,
                                        false, {}, hooksFor(ctx), &error);
            if (ctx.cancelled())
                return;
            if (!ok)
                return ctx.fail(QStringLiteral("render_error"), error);
            ctx.succeed({{QStringLiteral("path"), p.spec.outputPath}});
        },
        [self, serial, key, output = p.spec.outputPath, start](const QJsonObject &job) {
            if (!self || serial != self->m_previewSerial) {
                QFile::remove(output);
                return QJsonObject{};
            }
            self->m_previewJobId.clear();
            self->m_previewBusy = false;
            emit self->previewBusyChanged();
            if (!job.value(QStringLiteral("ok")).toBool()) {
                QFile::remove(output);
                const QJsonObject error = job.value(QStringLiteral("error")).toObject();
                if (error.value(QStringLiteral("code")).toString() != QLatin1String("cancelled"))
                    self->setLastError(error.value(QStringLiteral("message")).toString());
                return QJsonObject{};
            }
            if (!self->m_previewPath.isEmpty() && self->m_previewPath != output)
                QFile::remove(self->m_previewPath);
            self->m_previewPath = output;
            self->m_previewKey = key;
            self->m_previewStartUs = start;
            emit self->previewSegmentReady(output, drift::usToSeconds(start));
            return QJsonObject{};
        });
    return m_previewJobId;
}

// --- auto-split -------------------------------------------------------------------------------

void SourceLabController::autoSplit(const QVariantMap &options)
{
    setLastError({});
    if (m_busy) {
        setLastError(tr("Another operation is still running"));
        return;
    }
    const drift::MediaAsset *asset = current();
    if (!asset || asset->kind != drift::MediaKind::Video || !QFileInfo(asset->path).isFile()) {
        setLastError(tr("Auto-split needs a video"));
        return;
    }
    const SourceRecipe r = m_recipes.value(m_assetId);
    drift::TimeUs inUs = 0;
    drift::TimeUs outUs = 0;
    bool toEnd = false;
    if (!keptRange(*asset, r.trimInUs, r.trimOutUs, &inUs, &outUs, &toEnd)) {
        setLastError(tr("The trim leaves nothing to scan"));
        return;
    }

    m_splitCancel = QSharedPointer<QAtomicInt>::create(0);
    beginBusy(tr("Looking for shots…"));
    const QString assetId = m_assetId;
    QPointer<SourceLabController> self(this);
    m_app->m_sceneDetect->detectSource(
        asset->path, drift::rotationCorrectionOf(*asset), inUs, outUs,
        options.value(QStringLiteral("minSceneSeconds"), 0.0).toDouble(),
        options.value(QStringLiteral("threshold"), 0.0).toDouble(), m_splitCancel,
        [self](double fraction, const QString &status) {
            if (!self || !self->m_busy)
                return;
            self->m_progress = fraction;
            if (!status.isEmpty())
                self->m_status = status;
            emit self->progressChanged();
        },
        [self, assetId](bool ok, const drift::SceneAnalysis &analysis, const QString &error) {
            if (!self)
                return;
            self->endBusy();
            if (!ok) {
                if (!error.isEmpty())
                    self->setLastError(error);
                return;
            }
            if (self->m_assetId != assetId)
                return;
            QVariantList ranges;
            int shot = 1;
            for (const drift::Scene &scene : analysis.scenes) {
                ranges.append(QVariantMap{{QStringLiteral("in"), drift::usToSeconds(scene.sourceIn)},
                                          {QStringLiteral("out"), drift::usToSeconds(scene.sourceOut)},
                                          {QStringLiteral("name"), tr("Shot %1").arg(shot++)}});
            }
            self->replaceRanges(ranges);
        });
}

#include "moc_SourceLabController.cpp"
