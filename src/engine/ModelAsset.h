#pragma once

#include <QImage>
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
