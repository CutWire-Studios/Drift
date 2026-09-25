#pragma once

#include "engine/ModelAsset.h"

#include <QMatrix4x4>
#include <QVector>

#include <cstdint>

// Pure CPU animation for a ModelRig: sample the file's channels at a time, walk the node tree,
// and produce the palette the skinning shader reads. No GL, so it runs on the compositor worker
// and is unit-testable.

namespace drift {

struct ModelPose
{
    QVector<QMatrix4x4> palette; // rig.paletteSize rows, rest normalisation already applied
};

// Value of one sampler at `tSec`, clamped to the sampler's own range. `out` takes
// sampler.components floats. Quaternions come out normalised.
void sampleModelAnimSampler(const ModelAnimSampler &sampler, double tSec, float *out);

// Pose for animation `animIndex` at `tSec`; a negative or out-of-range index is the rest pose.
ModelPose evaluateModelPose(const ModelRig &rig, int animIndex, double tSec);

// Length of animation `animIndex` in microseconds, 0 when the asset has none at that index.
int64_t modelAnimationDurationUs(const ModelAsset &asset, int animIndex);

} // namespace drift
