#include "engine/procedural/ProceduralSceneCache.h"

#include <cstring>
#include <exception>
#include <functional>

namespace drift {

namespace {

struct Hasher
{
    std::uint64_t h = 0xcbf29ce484222325ull;

    void word(std::uint64_t w)
    {
        h ^= w;
        h *= 0x100000001b3ull;
        h ^= h >> 29;
    }
    void bytes(const void *data, std::size_t n)
    {
        const auto *p = static_cast<const unsigned char *>(data);
        std::size_t i = 0;
        for (; i + 8 <= n; i += 8) {
            std::uint64_t w;
            std::memcpy(&w, p + i, 8);
            word(w);
        }
        std::uint64_t tail = 0;
        if (i < n)
            std::memcpy(&tail, p + i, n - i);
        word(tail ^ (std::uint64_t(n) << 56));
    }
    template <typename T> void vec(const std::vector<T> &v)
    {
        word(v.size());
        if (!v.empty())
            bytes(v.data(), v.size() * sizeof(T));
    }
};

void appendBits(std::string &out, const void *data, std::size_t n)
{
    out.append(static_cast<const char *>(data), n);
}

template <typename T> void append(std::string &out, const T &value)
{
    appendBits(out, &value, sizeof(T));
}

void appendString(std::string &out, const std::string &s)
{
    append(out, std::uint32_t(s.size()));
    out += s;
}

std::string baseKey(const ProceduralEvalRequest &r)
{
    std::string k;
    appendString(k, r.sourceHash.empty() ? std::to_string(std::hash<std::string>{}(r.source)) : r.sourceHash);
    append(k, r.time);
    append(k, r.frame);
    append(k, r.seed);
    append(k, r.canvas.width);
    append(k, r.canvas.height);
    append(k, r.budgets.maxVertices);
    append(k, r.budgets.maxInstances);
    append(k, r.budgets.luaMemoryBytes);
    append(k, r.budgets.deadlineMs);
    for (const auto &[name, p] : r.params) {
        appendString(k, name);
        append(k, int(p.kind));
        append(k, p.number);
        append(k, p.boolean);
        appendString(k, p.string);
        append(k, std::uint32_t(p.vector.size()));
        for (double d : p.vector)
            append(k, d);
    }
    return k;
}

std::string cameraKey(const ProceduralCameraInput &c)
{
    std::string k = "|cam";
    appendBits(k, c.eye.data(), sizeof(c.eye));
    appendBits(k, c.right.data(), sizeof(c.right));
    appendBits(k, c.up.data(), sizeof(c.up));
    appendBits(k, c.forward.data(), sizeof(c.forward));
    append(k, c.fov);
    append(k, c.nearPlane);
    append(k, c.farPlane);
    appendBits(k, c.view.data(), sizeof(c.view));
    appendBits(k, c.proj.data(), sizeof(c.proj));
    return k;
}

bool failedOnTime(const ProceduralScene &scene)
{
    for (const std::string &e : scene.errors) {
        if (e.find("time limit") != std::string::npos)
            return true;
    }
    return false;
}

} // namespace

std::uint64_t proceduralMeshHash(const ProceduralMesh &mesh)
{
    Hasher h;
    h.word(std::uint64_t(mesh.topology));
    h.vec(mesh.position);
    h.vec(mesh.normal);
    h.vec(mesh.uv);
    h.vec(mesh.color);
    h.word(mesh.custom.size());
    for (const ProceduralMesh::Custom &c : mesh.custom) {
        h.bytes(c.name.data(), c.name.size());
        h.word(std::uint64_t(c.components));
        h.vec(c.data);
    }
    h.vec(mesh.indices);
    h.vec(mesh.instances);
    return h.h ? h.h : 1; // 0 is reserved for "not hashed"
}

std::size_t proceduralSceneBytes(const ProceduralScene &scene)
{
    std::size_t b = sizeof(ProceduralScene) + scene.lights.size() * sizeof(ProceduralLight);
    for (const ProceduralMeshEntry &m : scene.meshes) {
        if (m.mesh)
            b += m.mesh->byteSize();
        b += sizeof(ProceduralMeshEntry);
        for (const auto &[name, v] : m.uniforms)
            b += name.size() + v.size() * sizeof(float) + 64;
    }
    for (const std::string &line : scene.log)
        b += line.size() + 32;
    for (const std::string &line : scene.errors)
        b += line.size() + 32;
    return b;
}

ProceduralSceneCache::ProceduralSceneCache(std::size_t maxBytes)
    : m_maxBytes(maxBytes)
{
}

ProceduralSceneCache &ProceduralSceneCache::shared()
{
    static ProceduralSceneCache cache;
    return cache;
}

void ProceduralSceneCache::touchLocked(Entry &entry)
{
    m_lru.splice(m_lru.begin(), m_lru, entry.lru);
}

void ProceduralSceneCache::evictLocked()
{
    while (m_bytes > m_maxBytes && !m_lru.empty()) {
        const auto it = m_entries.find(m_lru.back());
        m_bytes -= it->second.bytes;
        m_entries.erase(it);
        m_lru.pop_back();
    }
}

void ProceduralSceneCache::insertLocked(const std::string &key, ScenePtr scene, bool cameraRead)
{
    if (const auto old = m_entries.find(key); old != m_entries.end()) {
        m_bytes -= old->second.bytes;
        m_lru.erase(old->second.lru);
        m_entries.erase(old);
    }
    m_lru.push_front(key);
    Entry entry;
    entry.bytes = scene ? proceduralSceneBytes(*scene) : key.size() + 64;
    entry.scene = std::move(scene);
    entry.cameraRead = cameraRead;
    entry.lru = m_lru.begin();
    m_bytes += entry.bytes;
    m_entries.emplace(key, std::move(entry));
    evictLocked();
}

ProceduralSceneCache::ScenePtr ProceduralSceneCache::evaluate(const ProceduralEvalRequest &request)
{
    const std::string base = baseKey(request);
    const std::string camera = cameraKey(request.camera);
    const std::string full = base + camera;

    std::unique_lock lock(m_mutex);
    std::shared_ptr<InFlight> mine;
    for (;;) {
        if (const auto it = m_entries.find(base); it != m_entries.end()) {
            if (!it->second.cameraRead) {
                touchLocked(it->second);
                ++m_stats.hits;
                return it->second.scene;
            }
            if (const auto jt = m_entries.find(full); jt != m_entries.end()) {
                touchLocked(it->second);
                touchLocked(jt->second);
                ++m_stats.hits;
                return jt->second.scene;
            }
        }
        const auto flying = m_inflight.find(base);
        if (flying == m_inflight.end()) {
            mine = std::make_shared<InFlight>();
            mine->fullKey = full;
            m_inflight.emplace(base, mine);
            break;
        }
        // Someone is evaluating this key. If the script ignores the camera its result is ours too;
        // if it reads it, only the same camera shares it, and otherwise we go round again.
        const std::shared_ptr<InFlight> other = flying->second;
        ++m_stats.joined;
        m_done.wait(lock, [&] { return other->done; });
        if (other->result && (!other->result->cameraRead || other->fullKey == full))
            return other->result;
    }
    ++m_stats.evaluations;
    lock.unlock();

    std::shared_ptr<ProceduralScene> scene;
    try {
        scene = std::make_shared<ProceduralScene>(ProceduralEvaluator::evaluate(
            request.source, request.params, request.time, request.camera, request.canvas, request.budgets,
            request.frame, request.seed));
    } catch (const std::exception &e) {
        scene = std::make_shared<ProceduralScene>();
        scene->errors.push_back(std::string("evaluation failed: ") + e.what());
    }
    scene->meshHashes.reserve(scene->meshes.size());
    for (const ProceduralMeshEntry &m : scene->meshes)
        scene->meshHashes.push_back(m.mesh ? proceduralMeshHash(*m.mesh) : 0);
    ScenePtr result = std::move(scene);

    lock.lock();
    if (!failedOnTime(*result)) {
        if (result->cameraRead) {
            insertLocked(base, nullptr, true);
            insertLocked(full, result, true);
        } else {
            insertLocked(base, result, false);
        }
    }
    mine->result = result;
    mine->done = true;
    m_inflight.erase(base);
    m_done.notify_all();
    return result;
}

void ProceduralSceneCache::setMaxBytes(std::size_t maxBytes)
{
    std::lock_guard lock(m_mutex);
    m_maxBytes = maxBytes;
    evictLocked();
}

void ProceduralSceneCache::clear()
{
    std::lock_guard lock(m_mutex);
    m_entries.clear();
    m_lru.clear();
    m_bytes = 0;
}

ProceduralSceneCache::Stats ProceduralSceneCache::stats() const
{
    std::lock_guard lock(m_mutex);
    Stats s = m_stats;
    s.entries = m_entries.size();
    s.bytes = m_bytes;
    return s;
}

} // namespace drift
