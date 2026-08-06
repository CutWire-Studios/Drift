#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

#include "ModelAsset.h"

#include <QByteArray>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <list>
#include <unordered_map>
#include <vector>

namespace drift {
namespace {

constexpr int kVertStride = 8; // pos3 nrm3 uv2

struct CacheKey
{
    QString path;
    qint64 mtimeMs = 0;
    qint64 size = 0;

    bool operator==(const CacheKey &o) const
    {
        return path == o.path && mtimeMs == o.mtimeMs && size == o.size;
    }
};

struct CacheEntry
{
    CacheKey key;
    std::shared_ptr<const ModelAsset> asset; // null = remembered failure
    QString warning;
    size_t bytes = 0;
};

QMutex g_cacheMutex;
std::list<CacheEntry> g_lru;
QHash<QString, std::list<CacheEntry>::iterator> g_index; // path → LRU node
constexpr size_t kMaxEntries = 8;
constexpr size_t kMaxBytes = 256ull * 1024 * 1024;
size_t g_totalBytes = 0;

size_t estimateBytes(const ModelAsset *a)
{
    if (!a)
        return 64;
    size_t n = size_t(a->vertices.size()) * sizeof(float)
               + size_t(a->indices.size()) * sizeof(uint32_t)
               + size_t(a->materials.size()) * 64
               + size_t(a->primitives.size()) * 16;
    for (const QImage &img : a->images)
        n += size_t(img.width()) * size_t(img.height()) * 4;
    return n;
}

void touchLocked(std::list<CacheEntry>::iterator it)
{
    g_lru.splice(g_lru.begin(), g_lru, it);
}

void evictLocked()
{
    while ((g_lru.size() > kMaxEntries || g_totalBytes > kMaxBytes) && !g_lru.empty()) {
        auto last = std::prev(g_lru.end());
        g_totalBytes -= last->bytes;
        g_index.remove(last->key.path);
        g_lru.erase(last);
    }
}

void insertLocked(CacheEntry entry)
{
    // Replace any prior entry for this path (different mtime/size).
    const auto existing = g_index.find(entry.key.path);
    if (existing != g_index.end()) {
        g_totalBytes -= existing.value()->bytes;
        g_lru.erase(existing.value());
        g_index.erase(existing);
    }
    entry.bytes = estimateBytes(entry.asset.get());
    g_totalBytes += entry.bytes;
    g_lru.push_front(std::move(entry));
    g_index.insert(g_lru.front().key.path, g_lru.begin());
    evictLocked();
}

bool hasExtension(const cgltf_data *data, const char *name)
{
    for (cgltf_size i = 0; i < data->extensions_required_count; ++i) {
        if (data->extensions_required[i] && std::strcmp(data->extensions_required[i], name) == 0)
            return true;
    }
    return false;
}

QString decodeDataUri(const char *uri, QByteArray *out)
{
    // data:[<mediatype>][;base64],<data>
    const char *comma = std::strchr(uri, ',');
    if (!comma)
        return QStringLiteral("malformed data URI");
    const QByteArray header = QByteArray(uri, int(comma - uri));
    const QByteArray payload = QByteArray(comma + 1);
    if (header.contains(";base64"))
        *out = QByteArray::fromBase64(payload);
    else
        *out = QByteArray::fromPercentEncoding(payload);
    if (out->isEmpty())
        return QStringLiteral("empty data URI payload");
    return {};
}

// Reject any path that escapes the .gltf directory (absolute, .., symlink).
QString resolveSafePath(const QString &baseDir, const char *uri, QString *resolved)
{
    if (!uri || !*uri)
        return QStringLiteral("empty URI");
    if (std::strncmp(uri, "data:", 5) == 0)
        return QStringLiteral("internal: data URI"); // caller handles

    const QString raw = QString::fromUtf8(uri);
    if (QFileInfo(raw).isAbsolute() || raw.contains(QLatin1String("://")))
        return QStringLiteral("absolute or remote URI rejected");

    const QString joined = QDir(baseDir).filePath(raw);
    const QString canonical = QFileInfo(joined).canonicalFilePath();
    const QString baseCanon = QFileInfo(baseDir).canonicalFilePath();
    if (!canonical.isEmpty() && !baseCanon.isEmpty()) {
        if (canonical == baseCanon
            || canonical.startsWith(baseCanon + QLatin1Char('/'))) {
            *resolved = canonical;
            return {};
        }
        return QStringLiteral("path escapes package directory");
    }
    // Missing files have an empty canonical path — fall back to a cleaned relative check.
    const QString cleaned = QDir::cleanPath(joined);
    const QString cleanedBase = QDir::cleanPath(baseDir);
    if (cleaned != cleanedBase && !cleaned.startsWith(cleanedBase + QLatin1Char('/')))
        return QStringLiteral("path escapes package directory");
    *resolved = cleaned;
    return {};
}

QImage decodeImageBytes(const QByteArray &bytes)
{
    QImage img = QImage::fromData(bytes);
    if (img.isNull())
        return {};
    img = img.convertToFormat(QImage::Format_RGBA8888);
    if (img.width() > kMaxModelTextureEdge || img.height() > kMaxModelTextureEdge) {
        img = img.scaled(kMaxModelTextureEdge, kMaxModelTextureEdge, Qt::KeepAspectRatio,
                         Qt::SmoothTransformation);
    }
    return img;
}

int glWrap(cgltf_wrap_mode mode)
{
    switch (mode) {
    case cgltf_wrap_mode_clamp_to_edge:
        return 33071; // GL_CLAMP_TO_EDGE
    case cgltf_wrap_mode_mirrored_repeat:
        return 33648; // GL_MIRRORED_REPEAT
    case cgltf_wrap_mode_repeat:
    default:
        return 10497; // GL_REPEAT
    }
}

void transformPoint(const float m[16], float x, float y, float z, float *ox, float *oy, float *oz)
{
    *ox = m[0] * x + m[4] * y + m[8] * z + m[12];
    *oy = m[1] * x + m[5] * y + m[9] * z + m[13];
    *oz = m[2] * x + m[6] * y + m[10] * z + m[14];
}

void transformDir(const float m[16], float x, float y, float z, float *ox, float *oy, float *oz)
{
    *ox = m[0] * x + m[4] * y + m[8] * z;
    *oy = m[1] * x + m[5] * y + m[9] * z;
    *oz = m[2] * x + m[6] * y + m[10] * z;
}

void nodeWorldMatrix(const cgltf_node *node, float out[16])
{
    cgltf_node_transform_world(node, out);
}

bool readVec3(const cgltf_accessor *acc, cgltf_size index, float out[3])
{
    if (!acc || index >= acc->count)
        return false;
    float tmp[4] = {0, 0, 0, 0};
    if (!cgltf_accessor_read_float(acc, index, tmp, 3))
        return false;
    out[0] = tmp[0];
    out[1] = tmp[1];
    out[2] = tmp[2];
    return true;
}

bool readVec2(const cgltf_accessor *acc, cgltf_size index, float out[2])
{
    if (!acc || index >= acc->count)
        return false;
    float tmp[4] = {0, 0, 0, 0};
    if (!cgltf_accessor_read_float(acc, index, tmp, 2))
        return false;
    out[0] = tmp[0];
    out[1] = tmp[1];
    return true;
}

} // namespace

std::shared_ptr<const ModelAsset> loadModelAsset(const QString &path, QString *warningOut)
{
    if (warningOut)
        warningOut->clear();

    if (path.isEmpty()) {
        if (warningOut)
            *warningOut = QStringLiteral("empty model path");
        return nullptr;
    }

    QFileInfo info(path);
    if (!info.exists() || !info.isFile()) {
        if (warningOut)
            *warningOut = QStringLiteral("model file not found");
        return nullptr;
    }
    if (info.size() > kMaxModelFileBytes) {
        if (warningOut)
            *warningOut = QStringLiteral("model exceeds 128 MB limit");
        return nullptr;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (warningOut)
            *warningOut = QStringLiteral("could not open model file");
        return nullptr;
    }
    const QByteArray bytes = file.readAll();
    file.close();
    if (bytes.isEmpty()) {
        if (warningOut)
            *warningOut = QStringLiteral("empty model file");
        return nullptr;
    }

    cgltf_options options = {};
    cgltf_data *data = nullptr;
    cgltf_result result =
        cgltf_parse(&options, bytes.constData(), cgltf_size(bytes.size()), &data);
    if (result != cgltf_result_success || !data) {
        if (warningOut)
            *warningOut = QStringLiteral("failed to parse glTF");
        return nullptr;
    }

    // Own the cgltf_data for the rest of this function.
    struct DataGuard
    {
        cgltf_data *d;
        ~DataGuard()
        {
            if (d)
                cgltf_free(d);
        }
    } guard{data};

    if (hasExtension(data, "KHR_draco_mesh_compression")
        || hasExtension(data, "EXT_meshopt_compression")) {
        if (warningOut) {
            *warningOut = QStringLiteral(
                "compressed mesh (Draco) is not supported — re-export without mesh compression");
        }
        return nullptr;
    }

    if (cgltf_validate(data) != cgltf_result_success) {
        if (warningOut)
            *warningOut = QStringLiteral("glTF failed validation");
        return nullptr;
    }

    // Load buffer payloads (GLB has them inline; .gltf may reference external .bin).
    result = cgltf_load_buffers(&options, data, path.toUtf8().constData());
    if (result != cgltf_result_success) {
        if (warningOut)
            *warningOut = QStringLiteral("failed to load glTF buffers");
        return nullptr;
    }

    auto asset = std::make_shared<ModelAsset>();
    QStringList notes;

    if (data->skins_count > 0)
        notes.append(QStringLiteral("skinned mesh rendered in bind pose"));

    const QString baseDir = info.absolutePath();

    // Decode images first so materials can index them.
    QHash<const cgltf_image *, int> imageIndex;
    for (cgltf_size i = 0; i < data->images_count; ++i) {
        const cgltf_image *img = &data->images[i];
        QByteArray imgBytes;
        if (img->buffer_view) {
            const cgltf_buffer_view *bv = img->buffer_view;
            if (!bv->buffer || !bv->buffer->data
                || bv->offset + bv->size > bv->buffer->size) {
                notes.append(QStringLiteral("image buffer_view out of range"));
                continue;
            }
            imgBytes = QByteArray(static_cast<const char *>(bv->buffer->data) + bv->offset,
                                  int(bv->size));
        } else if (img->uri) {
            if (std::strncmp(img->uri, "data:", 5) == 0) {
                const QString err = decodeDataUri(img->uri, &imgBytes);
                if (!err.isEmpty()) {
                    notes.append(err);
                    continue;
                }
            } else {
                QString resolved;
                const QString err = resolveSafePath(baseDir, img->uri, &resolved);
                if (!err.isEmpty()) {
                    notes.append(err);
                    continue;
                }
                QFile f(resolved);
                if (!f.open(QIODevice::ReadOnly)) {
                    notes.append(QStringLiteral("missing image file"));
                    continue;
                }
                imgBytes = f.readAll();
            }
        } else {
            continue;
        }

        QImage decoded = decodeImageBytes(imgBytes);
        if (decoded.isNull()) {
            notes.append(QStringLiteral("undecodable image"));
            continue;
        }
        imageIndex.insert(img, asset->images.size());
        asset->images.append(decoded);
    }

    // Materials.
    QHash<const cgltf_material *, int> materialIndex;
    for (cgltf_size i = 0; i < data->materials_count; ++i) {
        const cgltf_material *src = &data->materials[i];
        ModelMaterial mat;
        if (src->has_pbr_metallic_roughness) {
            const auto &pbr = src->pbr_metallic_roughness;
            mat.baseColorFactor = QVector4D(pbr.base_color_factor[0], pbr.base_color_factor[1],
                                            pbr.base_color_factor[2], pbr.base_color_factor[3]);
            mat.metallicFactor = pbr.metallic_factor;
            mat.roughnessFactor = pbr.roughness_factor;
            if (pbr.base_color_texture.texture && pbr.base_color_texture.texture->image) {
                const auto it = imageIndex.constFind(pbr.base_color_texture.texture->image);
                if (it != imageIndex.cend())
                    mat.baseColorTexture = it.value();
                if (pbr.base_color_texture.texture->sampler) {
                    mat.wrapS = glWrap(pbr.base_color_texture.texture->sampler->wrap_s);
                    mat.wrapT = glWrap(pbr.base_color_texture.texture->sampler->wrap_t);
                }
            }
            if (pbr.base_color_texture.texcoord != 0)
                notes.append(QStringLiteral("TEXCOORD_1 ignored"));
        }
        mat.emissiveFactor =
            QVector3D(src->emissive_factor[0], src->emissive_factor[1], src->emissive_factor[2]);
        switch (src->alpha_mode) {
        case cgltf_alpha_mode_mask:
            mat.alphaMode = ModelMaterial::AlphaMode::Mask;
            break;
        case cgltf_alpha_mode_blend:
            mat.alphaMode = ModelMaterial::AlphaMode::Blend;
            break;
        default:
            mat.alphaMode = ModelMaterial::AlphaMode::Opaque;
            break;
        }
        mat.alphaCutoff = src->alpha_cutoff;
        mat.doubleSided = src->double_sided;

        if (src->normal_texture.texture || src->occlusion_texture.texture
            || src->emissive_texture.texture
            || (src->has_pbr_metallic_roughness
                && src->pbr_metallic_roughness.metallic_roughness_texture.texture)) {
            notes.append(QStringLiteral("extra material textures ignored"));
        }

        materialIndex.insert(src, asset->materials.size());
        asset->materials.append(mat);
    }
    if (asset->materials.isEmpty()) {
        ModelMaterial fallback;
        asset->materials.append(fallback);
    }

    // Walk every node that has a mesh; bake the world matrix into positions/normals.
    struct PendingPrim
    {
        ModelPrimitive prim;
        ModelMaterial::AlphaMode mode;
    };
    std::vector<PendingPrim> opaquePrims;
    std::vector<PendingPrim> blendPrims;

    const auto appendMesh = [&](const cgltf_mesh *mesh, const float world[16]) {
        for (cgltf_size pi = 0; pi < mesh->primitives_count; ++pi) {
            const cgltf_primitive *prim = &mesh->primitives[pi];
            if (prim->type != cgltf_primitive_type_triangles)
                continue;

            const cgltf_accessor *posAcc = nullptr;
            const cgltf_accessor *nrmAcc = nullptr;
            const cgltf_accessor *uvAcc = nullptr;
            for (cgltf_size a = 0; a < prim->attributes_count; ++a) {
                const cgltf_attribute *attr = &prim->attributes[a];
                if (attr->type == cgltf_attribute_type_position)
                    posAcc = attr->data;
                else if (attr->type == cgltf_attribute_type_normal)
                    nrmAcc = attr->data;
                else if (attr->type == cgltf_attribute_type_texcoord && attr->index == 0)
                    uvAcc = attr->data;
            }
            if (!posAcc || posAcc->count == 0)
                continue;

            const int baseVertex = asset->vertexCount();
            if (baseVertex + int(posAcc->count) > kMaxModelVertices) {
                if (warningOut)
                    *warningOut = QStringLiteral("model exceeds 500k vertex limit");
                asset.reset();
                return;
            }

            asset->vertices.reserve(asset->vertices.size() + int(posAcc->count) * kVertStride);
            for (cgltf_size vi = 0; vi < posAcc->count; ++vi) {
                float p[3] = {0, 0, 0};
                if (!readVec3(posAcc, vi, p))
                    continue;
                float wp[3];
                transformPoint(world, p[0], p[1], p[2], &wp[0], &wp[1], &wp[2]);

                float n[3] = {0, 0, 1};
                if (nrmAcc && readVec3(nrmAcc, vi, n)) {
                    float wn[3];
                    transformDir(world, n[0], n[1], n[2], &wn[0], &wn[1], &wn[2]);
                    const float len = std::sqrt(wn[0] * wn[0] + wn[1] * wn[1] + wn[2] * wn[2]);
                    if (len > 1e-8f) {
                        wn[0] /= len;
                        wn[1] /= len;
                        wn[2] /= len;
                    }
                    n[0] = wn[0];
                    n[1] = wn[1];
                    n[2] = wn[2];
                }

                float uv[2] = {0, 0};
                if (uvAcc)
                    readVec2(uvAcc, vi, uv);

                asset->vertices << wp[0] << wp[1] << wp[2] << n[0] << n[1] << n[2] << uv[0]
                                << uv[1];
            }

            // Flat normals when the source had none: average face normals per vertex later is
            // expensive; a constant +Z in model space is what the plan allows for v1.
            if (!nrmAcc) {
                // Leave the +Z written above; after normalisation the lighting still works.
            }

            ModelPrimitive outPrim;
            outPrim.firstIndex = asset->indices.size();
            outPrim.material = 0;
            if (prim->material) {
                const auto it = materialIndex.constFind(prim->material);
                if (it != materialIndex.cend())
                    outPrim.material = it.value();
            }

            const int vertCount = asset->vertexCount() - baseVertex;
            if (prim->indices) {
                const cgltf_accessor *idxAcc = prim->indices;
                std::vector<cgltf_uint> unpacked(idxAcc->count);
                if (!cgltf_accessor_unpack_indices(idxAcc, unpacked.data(), sizeof(cgltf_uint),
                                                   idxAcc->count)) {
                    notes.append(QStringLiteral("failed to unpack indices"));
                    asset->vertices.resize(baseVertex * kVertStride);
                    continue;
                }
                for (cgltf_size ii = 0; ii < idxAcc->count; ++ii) {
                    const cgltf_uint idx = unpacked[ii];
                    if (idx >= cgltf_uint(vertCount)) {
                        if (warningOut)
                            *warningOut = QStringLiteral("index out of range");
                        asset.reset();
                        return;
                    }
                    asset->indices.append(uint32_t(baseVertex) + uint32_t(idx));
                }
                outPrim.indexCount = int(idxAcc->count);
            } else {
                for (int i = 0; i < vertCount; ++i)
                    asset->indices.append(uint32_t(baseVertex + i));
                outPrim.indexCount = vertCount;
            }

            if (outPrim.indexCount <= 0)
                continue;

            PendingPrim pending;
            pending.prim = outPrim;
            pending.mode = asset->materials.at(outPrim.material).alphaMode;
            if (pending.mode == ModelMaterial::AlphaMode::Blend)
                blendPrims.push_back(pending);
            else
                opaquePrims.push_back(pending);
        }
    };

    if (!asset)
        return nullptr;

    if (data->scenes_count > 0 && data->scene) {
        for (cgltf_size ni = 0; ni < data->scene->nodes_count; ++ni) {
            // Walk the full subtree of each scene root.
            std::vector<const cgltf_node *> stack;
            stack.push_back(data->scene->nodes[ni]);
            while (!stack.empty()) {
                const cgltf_node *node = stack.back();
                stack.pop_back();
                if (!node)
                    continue;
                if (node->mesh) {
                    float world[16];
                    nodeWorldMatrix(node, world);
                    appendMesh(node->mesh, world);
                    if (!asset)
                        return nullptr;
                }
                for (cgltf_size c = 0; c < node->children_count; ++c)
                    stack.push_back(node->children[c]);
            }
        }
    } else {
        // No scene: every mesh at identity.
        float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        for (cgltf_size mi = 0; mi < data->meshes_count; ++mi) {
            appendMesh(&data->meshes[mi], identity);
            if (!asset)
                return nullptr;
        }
    }

    if (asset->vertexCount() == 0 || asset->indices.isEmpty()) {
        if (warningOut)
            *warningOut = QStringLiteral("model has no triangulated geometry");
        return nullptr;
    }

    for (const PendingPrim &p : opaquePrims)
        asset->primitives.append(p.prim);
    for (const PendingPrim &p : blendPrims)
        asset->primitives.append(p.prim);

    // AABB → centre → uniform scale by 1/extent.x so scale=1 is one head-width wide.
    QVector3D bmin(asset->vertices[0], asset->vertices[1], asset->vertices[2]);
    QVector3D bmax = bmin;
    for (int i = 0; i < asset->vertexCount(); ++i) {
        const float *v = asset->vertices.constData() + i * kVertStride;
        bmin.setX(std::min(bmin.x(), v[0]));
        bmin.setY(std::min(bmin.y(), v[1]));
        bmin.setZ(std::min(bmin.z(), v[2]));
        bmax.setX(std::max(bmax.x(), v[0]));
        bmax.setY(std::max(bmax.y(), v[1]));
        bmax.setZ(std::max(bmax.z(), v[2]));
    }
    const QVector3D extent = bmax - bmin;
    if (extent.x() < 1e-8f && extent.y() < 1e-8f && extent.z() < 1e-8f) {
        if (warningOut)
            *warningOut = QStringLiteral("degenerate model AABB");
        return nullptr;
    }
    const QVector3D centre = (bmin + bmax) * 0.5f;
    const float sx = std::max({extent.x(), extent.y(), extent.z(), 1e-8f});
    // Plan says scale by 1/extent.x specifically so the model is one head-width wide. Prefer x;
    // fall back to the largest axis when x is degenerate (a flat card viewed edge-on).
    const float inv = 1.f / (extent.x() > 1e-6f ? extent.x() : sx);

    for (int i = 0; i < asset->vertexCount(); ++i) {
        float *v = asset->vertices.data() + i * kVertStride;
        v[0] = (v[0] - centre.x()) * inv;
        v[1] = (v[1] - centre.y()) * inv;
        v[2] = (v[2] - centre.z()) * inv;
    }

    bmin = QVector3D(asset->vertices[0], asset->vertices[1], asset->vertices[2]);
    bmax = bmin;
    for (int i = 0; i < asset->vertexCount(); ++i) {
        const float *v = asset->vertices.constData() + i * kVertStride;
        bmin.setX(std::min(bmin.x(), v[0]));
        bmin.setY(std::min(bmin.y(), v[1]));
        bmin.setZ(std::min(bmin.z(), v[2]));
        bmax.setX(std::max(bmax.x(), v[0]));
        bmax.setY(std::max(bmax.y(), v[1]));
        bmax.setZ(std::max(bmax.z(), v[2]));
    }
    asset->aabbMin = bmin;
    asset->aabbMax = bmax;

    notes.removeDuplicates();
    asset->warning = notes.join(QLatin1String("; "));
    if (warningOut && !asset->warning.isEmpty())
        *warningOut = asset->warning;
    return asset;
}

std::shared_ptr<const ModelAsset> loadModelAssetCached(const QString &path)
{
    if (path.isEmpty())
        return nullptr;

    const QFileInfo info(QFileInfo(path).absoluteFilePath());
    CacheKey key{info.absoluteFilePath(), info.lastModified().toMSecsSinceEpoch(), info.size()};

    {
        QMutexLocker lock(&g_cacheMutex);
        const auto it = g_index.constFind(key.path);
        if (it != g_index.cend() && it.value()->key == key) {
            touchLocked(it.value());
            return it.value()->asset;
        }
    }

    QString warning;
    auto asset = info.exists() ? loadModelAsset(key.path, &warning) : nullptr;
    if (!info.exists() && warning.isEmpty())
        warning = QStringLiteral("model file not found");

    {
        QMutexLocker lock(&g_cacheMutex);
        CacheEntry entry;
        entry.key = key;
        entry.asset = asset;
        entry.warning = warning;
        insertLocked(std::move(entry));
    }
    return asset;
}

QString modelAssetWarning(const QString &path)
{
    if (path.isEmpty())
        return {};
    const QString abs = QFileInfo(path).absoluteFilePath();
    QMutexLocker lock(&g_cacheMutex);
    const auto it = g_index.constFind(abs);
    if (it == g_index.cend())
        return {};
    return it.value()->warning;
}

void clearModelAssetCache()
{
    QMutexLocker lock(&g_cacheMutex);
    g_lru.clear();
    g_index.clear();
    g_totalBytes = 0;
}

} // namespace drift
