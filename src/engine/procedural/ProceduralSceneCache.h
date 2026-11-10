#pragma once

#include "engine/procedural/ProceduralEvaluator.h"

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace drift {

// FNV-style hash of everything that reaches the GPU: topology, every attribute array, indices and
// instance matrices. Equal geometry from different evaluations hashes equal.
std::uint64_t proceduralMeshHash(const ProceduralMesh &mesh);
// Approximate heap footprint of a scene, for the cache's byte cap.
std::size_t proceduralSceneBytes(const ProceduralScene &scene);

struct ProceduralEvalRequest
{
    std::string source;
    // Identifies the package revision (ProceduralPackage::sourceHash). Empty falls back to a hash of `source`.
    std::string sourceHash;
    ProceduralParams params;
    double time = 0.0; // clip source time, seconds
    int frame = 0;
    std::int64_t seed = 0;
    ProceduralCanvas canvas;
    ProceduralCameraInput camera;
    ProceduralBudgets budgets;
};

// A byte-capped LRU of evaluated scenes. The key is the package revision, params, time, frame, seed,
// canvas and budgets; the camera matrices join it only for a scene whose script read `camera`
// (ProceduralScene::cameraRead), so a camera move does not invalidate a scene that ignored it.
// Concurrent requests for one key share a single evaluation. Thread-safe.
class ProceduralSceneCache
{
public:
    using ScenePtr = std::shared_ptr<const ProceduralScene>;

    struct Stats
    {
        std::size_t entries = 0;
        std::size_t bytes = 0;
        std::size_t hits = 0;
        std::size_t evaluations = 0;
        std::size_t joined = 0; // requests that waited on another thread's evaluation
    };

    explicit ProceduralSceneCache(std::size_t maxBytes = 192u * 1024u * 1024u);

    // The process-wide instance the compositor uses.
    static ProceduralSceneCache &shared();

    // Never null. Blocks while this or another thread evaluates the key. Scenes that failed on the
    // wall-clock limit are returned but not kept, since a retry may succeed.
    ScenePtr evaluate(const ProceduralEvalRequest &request);

    void setMaxBytes(std::size_t maxBytes);
    void clear();
    Stats stats() const;

private:
    struct Entry
    {
        ScenePtr scene; // null for the marker that says "this base key depends on the camera"
        bool cameraRead = false;
        std::size_t bytes = 0;
        std::list<std::string>::iterator lru;
    };
    struct InFlight
    {
        std::string fullKey;
        ScenePtr result;
        bool done = false;
    };

    void insertLocked(const std::string &key, ScenePtr scene, bool cameraRead);
    void touchLocked(Entry &entry);
    void evictLocked();

    mutable std::mutex m_mutex;
    std::condition_variable m_done;
    std::unordered_map<std::string, Entry> m_entries;
    std::list<std::string> m_lru; // most recent first
    std::unordered_map<std::string, std::shared_ptr<InFlight>> m_inflight; // by base key
    std::size_t m_bytes = 0;
    std::size_t m_maxBytes;
    Stats m_stats;
};

} // namespace drift
