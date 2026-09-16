#pragma once

#include <QImage>
#include <QMatrix4x4>
#include <QQuaternion>
#include <QString>
#include <QVector>
#include <QVector3D>
#include <QVector4D>

#include <cstdint>
#include <memory>

namespace drift {

// CPU-side glTF / GLB mesh after node transforms are baked and the AABB is normalised to one
// head-width wide. No cgltf type escapes this header — GlModelRenderer uploads from these fields
// alone, and the parse path is unit-testable without a GL context.

struct ModelMaterial
{
    enum class AlphaMode { Opaque, Mask, Blend };

    QVector4D baseColorFactor{1.f, 1.f, 1.f, 1.f};
    int baseColorTexture = -1; // index into ModelAsset::images, or -1
    float metallicFactor = 1.f;
    float roughnessFactor = 1.f;
    QVector3D emissiveFactor{0.f, 0.f, 0.f};
    AlphaMode alphaMode = AlphaMode::Opaque;
    float alphaCutoff = 0.5f;
    bool doubleSided = false;
    // Sampler wrap for the base-color texture (GL_REPEAT / MIRRORED_REPEAT / CLAMP_TO_EDGE).
    int wrapS = 10497; // GL_REPEAT
    int wrapT = 10497;
};

struct ModelPrimitive
{
    int firstIndex = 0;
    int indexCount = 0;
    int material = 0;
    int node = -1; // rig only: the node this primitive is instanced under
};

// --- Rig: the unbaked scene for animated files -------------------------------------------------
//
// Kept alongside the baked buffer, never instead of it: the face-prop effect draws the baked
// vertices and must stay bit-identical, and a static file pays nothing. The rig exists only when
// the file has animations or skins. Its vertices are node-local and carry joint/weight attributes
// for every vertex: a rigid primitive's vertices point at their node's own palette row with
// weight one, a skinned primitive's at the absolute rows of its skin's joints. One vertex
// shader then covers both.

struct ModelNode
{
    int parent = -1; // index into ModelRig::nodes; parents always precede children
    bool hasMatrix = false; // a matrix node cannot be animated; kept verbatim
    QMatrix4x4 matrix;
    QVector3D translation{0.f, 0.f, 0.f};
    QQuaternion rotation;
    QVector3D scale{1.f, 1.f, 1.f};
    QString name;
};

struct ModelSkin
{
    QVector<int> joints; // node indices
    QVector<QMatrix4x4> inverseBind; // parallel to joints (identity when the file has none)
    int paletteBase = 0; // first palette row of this skin's joints
};

struct ModelAnimSampler
{
    enum class Interp { Step, Linear, CubicSpline };
    Interp interp = Interp::Linear;
    QVector<float> times; // seconds, ascending
    // times.size() * components values; CubicSpline stores in-tangent, value, out-tangent per key.
    QVector<float> values;
    int components = 3;
};

struct ModelAnimChannel
{
    enum class Path { Translation, Rotation, Scale, Weights };
    Path path = Path::Translation;
    int sampler = 0;
    int node = -1;
};

struct ModelAnimation
{
    QString name;
    QList<ModelAnimSampler> samplers;
    QList<ModelAnimChannel> channels; // Weights channels are parsed but not evaluated (no morphs)
    double durationSec = 0.0;
};

struct ModelRig
{
    QList<ModelNode> nodes;
    QList<ModelSkin> skins;
    QList<ModelAnimation> animations;

    // Interleaved pos3 / nrm3 / uv2 / joints4 / weights4, node-local. Stride is 16 floats. Joint
    // indices are stored as floats (exact below 2^24) so the VAO needs no integer attribute.
    QVector<float> vertices;
    QVector<uint32_t> indices;
    QList<ModelPrimitive> primitives; // opaque and MASK first, BLEND last, like the baked list

    int paletteSize = 0; // nodes.size() + every skin's joint count
    // Rest-pose normalisation, folded into every palette row: recentre, then scale the largest
    // axis to one unit. restAabb* is the rest pose after that (centred, max extent 1).
    QVector3D restCentre;
    float restInvScale = 1.f;
    QVector3D restAabbMin{-0.5f, -0.5f, -0.5f};
    QVector3D restAabbMax{0.5f, 0.5f, 0.5f};

    int vertexCount() const { return vertices.size() / 16; }
};

inline constexpr int kRigVertStride = 16;

struct ModelAnimationInfo
{
    QString name;
    qint64 durationUs = 0;
};

struct ModelAsset
{
    // Interleaved pos3 / nrm3 / uv2. Stride is always 8 floats.
    QVector<float> vertices;
    QVector<uint32_t> indices;
    // Opaque and MASK first, BLEND last (unsorted within each group — v1 does not depth-sort).
    QList<ModelPrimitive> primitives;
    QList<ModelMaterial> materials;
    QList<QImage> images; // RGBA8888, already capped at 2048 on the long edge

    QVector3D aabbMin{-0.5f, -0.5f, -0.5f};
    QVector3D aabbMax{0.5f, 0.5f, 0.5f};

    // Every animation the file declares, in file order, so a clip can pick one by index.
    QList<ModelAnimationInfo> animations;

    // Null for a static file. See ModelRig.
    std::shared_ptr<const ModelRig> rig;

    // Non-fatal notes (skinned bind-pose warn, unsupported extensions that were ignored, …).
    // Empty when the load was clean. Fatal failures return a null shared_ptr instead and put the
    // message in the negative-cache warning via loadModelAssetCached's last-warn channel.
    QString warning;

    int vertexCount() const { return vertices.size() / 8; }
};

// Soft caps. Enforced before cgltf_parse (file size) and after unpack (vertex count) so a hostile
// or accidental file cannot allocate unbounded memory on the compositor thread.
inline constexpr qint64 kMaxModelFileBytes = 128LL * 1024 * 1024;
inline constexpr int kMaxModelVertices = 500'000;
inline constexpr int kMaxModelTextureEdge = 2048;
// Palette rows live in a 4×N RGBA32F texture; ES 3.0 guarantees 2048 texels per side.
inline constexpr int kMaxModelPaletteRows = 2048;

// Rest-pose bounds the clip camera frames: the rig's when there is one, else the baked AABB.
void modelClipBounds(const ModelAsset &asset, QVector3D *aabbMin, QVector3D *aabbMax);

// Parse a .glb / .gltf from disk. Returns null on any hard failure; *warningOut carries either
// the fatal reason or a non-fatal note. Always uses cgltf's bounds-checked accessors.
std::shared_ptr<const ModelAsset> loadModelAsset(const QString &path, QString *warningOut = nullptr);

// Keyed on (absolutePath, mtimeMs, fileSize). Caches null results too so a broken path is not
// re-parsed every frame. LRU-bounded. Thread-safe.
std::shared_ptr<const ModelAsset> loadModelAssetCached(const QString &path);

// Warning recorded for the most recent failed (or warned) load of `path`. Empty when the last
// successful load of that path had no notes. Used by effectToMap to surface Draco / missing-file
// messages in the inspector without re-parsing.
QString modelAssetWarning(const QString &path);

void clearModelAssetCache();

} // namespace drift
