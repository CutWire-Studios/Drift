#include "ModelAnimation.h"

#include <algorithm>
#include <cmath>

namespace drift {

namespace {

void normaliseQuat(float *q)
{
    const float len = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    if (len > 1e-8f) {
        for (int i = 0; i < 4; ++i)
            q[i] /= len;
    } else {
        q[0] = q[1] = q[2] = 0.f;
        q[3] = 1.f;
    }
}

QMatrix4x4 localMatrix(const ModelNode &node, const QVector3D &t, const QQuaternion &r,
                       const QVector3D &s)
{
    if (node.hasMatrix)
        return node.matrix;
    QMatrix4x4 m;
    m.translate(t);
    m.rotate(r);
    m.scale(s);
    return m;
}

} // namespace

void sampleModelAnimSampler(const ModelAnimSampler &sampler, double tSec, float *out)
{
    const int n = sampler.components;
    const int keys = sampler.times.size();
    const int perKey = sampler.interp == ModelAnimSampler::Interp::CubicSpline ? 3 * n : n;
    if (keys <= 0 || sampler.values.size() < keys * perKey) {
        for (int i = 0; i < n; ++i)
            out[i] = 0.f;
        if (n == 4)
            out[3] = 1.f;
        return;
    }
    // Value of key k (the middle element for CubicSpline).
    auto value = [&](int k, int c) {
        return sampler.values[k * perKey + (perKey == 3 * n ? n : 0) + c];
    };

    const float t = float(tSec);
    if (keys == 1 || t <= sampler.times.first()) {
        for (int c = 0; c < n; ++c)
            out[c] = value(0, c);
        if (n == 4)
            normaliseQuat(out);
        return;
    }
    if (t >= sampler.times.last()) {
        for (int c = 0; c < n; ++c)
            out[c] = value(keys - 1, c);
        if (n == 4)
            normaliseQuat(out);
        return;
    }

    const auto upper = std::upper_bound(sampler.times.begin(), sampler.times.end(), t);
    const int k1 = int(upper - sampler.times.begin());
    const int k0 = k1 - 1;
    const float t0 = sampler.times[k0];
    const float t1 = sampler.times[k1];
    const float td = std::max(t1 - t0, 1e-6f);
    const float u = std::clamp((t - t0) / td, 0.f, 1.f);

    switch (sampler.interp) {
    case ModelAnimSampler::Interp::Step:
        for (int c = 0; c < n; ++c)
            out[c] = value(k0, c);
        break;
    case ModelAnimSampler::Interp::Linear:
        if (n == 4) {
            // Shortest-arc slerp, as the spec asks for rotations.
            float a[4], b[4];
            for (int c = 0; c < 4; ++c) {
                a[c] = value(k0, c);
                b[c] = value(k1, c);
            }
            float d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
            if (d < 0.f) {
                d = -d;
                for (int c = 0; c < 4; ++c)
                    b[c] = -b[c];
            }
            float wa = 1.f - u;
            float wb = u;
            if (d < 0.9995f) {
                const float theta = std::acos(std::clamp(d, -1.f, 1.f));
                const float s = std::sin(theta);
                wa = std::sin((1.f - u) * theta) / s;
                wb = std::sin(u * theta) / s;
            }
            for (int c = 0; c < 4; ++c)
                out[c] = wa * a[c] + wb * b[c];
        } else {
            for (int c = 0; c < n; ++c)
                out[c] = value(k0, c) * (1.f - u) + value(k1, c) * u;
        }
        break;
    case ModelAnimSampler::Interp::CubicSpline: {
        // Hermite with the spec's tangent scaling by the key interval.
        const float u2 = u * u;
        const float u3 = u2 * u;
        const float h00 = 2.f * u3 - 3.f * u2 + 1.f;
        const float h10 = u3 - 2.f * u2 + u;
        const float h01 = -2.f * u3 + 3.f * u2;
        const float h11 = u3 - u2;
        for (int c = 0; c < n; ++c) {
            const float p0 = sampler.values[k0 * perKey + n + c];
            const float m0 = sampler.values[k0 * perKey + 2 * n + c] * td;
            const float p1 = sampler.values[k1 * perKey + n + c];
            const float m1 = sampler.values[k1 * perKey + c] * td;
            out[c] = h00 * p0 + h10 * m0 + h01 * p1 + h11 * m1;
        }
        break;
    }
    }
    if (n == 4)
        normaliseQuat(out);
}

ModelPose evaluateModelPose(const ModelRig &rig, int animIndex, double tSec)
{
    const int nodeCount = rig.nodes.size();
    QVector<QVector3D> t(nodeCount);
    QVector<QQuaternion> r(nodeCount);
    QVector<QVector3D> s(nodeCount);
    for (int i = 0; i < nodeCount; ++i) {
        t[i] = rig.nodes[i].translation;
        r[i] = rig.nodes[i].rotation;
        s[i] = rig.nodes[i].scale;
    }

    if (animIndex >= 0 && animIndex < rig.animations.size()) {
        const ModelAnimation &anim = rig.animations[animIndex];
        float v[4];
        for (const ModelAnimChannel &ch : anim.channels) {
            if (ch.node < 0 || ch.node >= nodeCount || ch.sampler < 0
                || ch.sampler >= anim.samplers.size() || rig.nodes[ch.node].hasMatrix)
                continue;
            const ModelAnimSampler &sampler = anim.samplers[ch.sampler];
            switch (ch.path) {
            case ModelAnimChannel::Path::Translation:
                if (sampler.components != 3)
                    break;
                sampleModelAnimSampler(sampler, tSec, v);
                t[ch.node] = QVector3D(v[0], v[1], v[2]);
                break;
            case ModelAnimChannel::Path::Rotation:
                if (sampler.components != 4)
                    break;
                sampleModelAnimSampler(sampler, tSec, v);
                r[ch.node] = QQuaternion(v[3], v[0], v[1], v[2]);
                break;
            case ModelAnimChannel::Path::Scale:
                if (sampler.components != 3)
                    break;
                sampleModelAnimSampler(sampler, tSec, v);
                s[ch.node] = QVector3D(v[0], v[1], v[2]);
                break;
            case ModelAnimChannel::Path::Weights:
                break;
            }
        }
    }

    // Parents precede children, so one pass resolves the world matrices.
    QVector<QMatrix4x4> world(nodeCount);
    for (int i = 0; i < nodeCount; ++i) {
        const QMatrix4x4 local = localMatrix(rig.nodes[i], t[i], r[i], s[i]);
        const int parent = rig.nodes[i].parent;
        world[i] = (parent >= 0 && parent < i) ? world[parent] * local : local;
    }

    QMatrix4x4 norm;
    norm.scale(rig.restInvScale);
    norm.translate(-rig.restCentre);

    ModelPose pose;
    pose.palette.resize(rig.paletteSize);
    for (int i = 0; i < nodeCount && i < rig.paletteSize; ++i)
        pose.palette[i] = norm * world[i];
    for (const ModelSkin &skin : rig.skins) {
        for (int j = 0; j < skin.joints.size(); ++j) {
            const int row = skin.paletteBase + j;
            const int joint = skin.joints[j];
            if (row < 0 || row >= rig.paletteSize || joint < 0 || joint >= nodeCount)
                continue;
            const QMatrix4x4 ibm = j < skin.inverseBind.size() ? skin.inverseBind[j] : QMatrix4x4();
            pose.palette[row] = norm * world[joint] * ibm;
        }
    }
    return pose;
}

int64_t modelAnimationDurationUs(const ModelAsset &asset, int animIndex)
{
    if (animIndex < 0 || animIndex >= asset.animations.size())
        return 0;
    return asset.animations.at(animIndex).durationUs;
}

} // namespace drift
