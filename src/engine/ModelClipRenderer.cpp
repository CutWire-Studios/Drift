#include "ModelClipRenderer.h"

#include "engine/ModelAnimation.h"
#include "engine/ModelAsset.h"

namespace drift::model3d {

std::shared_ptr<const ModelDrawRequest> makeDrawRequest(const RenderRequest &request)
{
    if (request.path.isEmpty())
        return nullptr;
    auto asset = loadModelAssetCached(request.path);
    if (!asset)
        return nullptr;

    auto out = std::make_shared<ModelDrawRequest>();
    out->path = request.path;
    out->asset = asset;
    out->params = modelClipParamsFromSource(request.source, request.centre.x(), request.centre.y());

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
