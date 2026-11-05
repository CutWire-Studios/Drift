#include "engine/render3d/models/ModelClipRenderer.h"

#include "engine/render3d/models/ModelAnimation.h"
#include "engine/render3d/models/ModelAsset.h"
#include "engine/render3d/splats/SplatAsset.h"

#include <QDebug>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>

#include <list>

namespace drift::model3d {

namespace {

struct SplatCacheEntry
{
    QString path;
    qint64 mtime = 0;
    std::shared_ptr<const SplatData> data; // null: the load failed, remembered so it is not retried
    QString error;                         // why, when data is null
};

QMutex g_splatMutex;
std::list<SplatCacheEntry> g_splatCache; // most recent first

// A scene is up to ~100 MB of floats, so only a couple stay resident. Held under the lock while
// loading so concurrent workers asking for the same file parse it once. With classify set, a
// path that is not a splat returns false without being cached, and a cache hit skips the sniff
// (which can open a zip), so steady-state frames never read the file.
bool loadSplatCached(const QString &path, bool classify, std::shared_ptr<const SplatData> *out,
                     QString *error = nullptr)
{
    constexpr size_t kMaxScenes = 3;
    const QFileInfo info(path);
    const QString abs = info.absoluteFilePath();
    const qint64 mtime = info.lastModified().toMSecsSinceEpoch();

    QMutexLocker lock(&g_splatMutex);
    for (auto it = g_splatCache.begin(); it != g_splatCache.end(); ++it) {
        if (it->path == abs && it->mtime == mtime) {
            g_splatCache.splice(g_splatCache.begin(), g_splatCache, it);
            *out = it->data;
            if (error)
                *error = it->error;
            return true;
        }
    }
    if (classify && !isSplatPath(path))
        return false;
    SplatData loaded = loadSplatAsset(abs, kSplatBudget);
    const QString loadError = loaded.error;
    std::shared_ptr<const SplatData> data;
    if (loaded.error.isEmpty())
        data = std::make_shared<const SplatData>(std::move(loaded));
    else
        qWarning().noquote() << "splat load failed:" << abs << loaded.error;
    g_splatCache.push_front({abs, mtime, data, loadError});
    while (g_splatCache.size() > kMaxScenes)
        g_splatCache.pop_back();
    *out = data;
    if (error)
        *error = loadError;
    return true;
}

} // namespace

std::shared_ptr<const SplatData> loadSplatSceneCached(const QString &path, QString *error)
{
    std::shared_ptr<const SplatData> data;
    loadSplatCached(path, false, &data, error);
    return data;
}

std::shared_ptr<const ModelDrawRequest> makeDrawRequest(const RenderRequest &request)
{
    if (request.path.isEmpty())
        return nullptr;
    auto out = std::make_shared<ModelDrawRequest>();
    out->path = request.path;
    out->rect = request.rect;
    out->rotation = request.rotation;
    out->pose3d = request.pose;
    out->canvas = request.canvas;

    out->params = modelClipParamsFromSource(request.source);
    if (loadSplatCached(request.path, true, &out->splat)) {
        if (!out->splat)
            return nullptr;
        return out;
    }

    auto asset = loadModelAssetCached(request.path);
    if (!asset)
        return nullptr;
    out->asset = asset;

    if (asset->rig && !asset->rig->animations.isEmpty()) {
        const int animIndex =
            qBound(0, request.source.animation, asset->rig->animations.size() - 1);
        const TimeUs durationUs = modelAnimationDurationUs(*asset, animIndex);
        TimeUs animUs = 0;
        // Offset first, then fold by the loop mode against the chosen animation's length.
        if (!foldVectorTime(request.animUs + request.source.startOffsetUs, durationUs,
                            request.source.loop, &animUs))
            return nullptr;
        auto pose = std::make_shared<ModelPose>(
            evaluateModelPose(*asset->rig, animIndex, usToSeconds(animUs)));
        out->pose = std::move(pose);
    }
    return out;
}

} // namespace drift::model3d
