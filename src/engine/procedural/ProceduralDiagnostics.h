#pragma once

#include "engine/procedural/ProceduralScene.h"

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace drift {

// What the last evaluation of a clip produced, for the console and MCP results. Written by the
// compositor worker each time a procedural clip is built; read from any thread.
struct ProceduralClipReport
{
    std::string packageId;
    // Script log, errors and stats of the scene last built for the clip; null when the package was
    // missing (see `error`).
    std::shared_ptr<const ProceduralScene> scene;
    // A failure outside the script: "package 'x' is not installed", an invalid package.
    std::string error;
    std::uint64_t serial = 0; // bumps on every update, so a poller can tell a new frame from an old one
};

// How one shader pair of a package compiled on the GL thread.
struct ProceduralShaderStatus
{
    bool ok = false;
    std::string log; // driver output for vertex, fragment and link; empty on a clean build
};

// Process-wide registry of the places errors and logs land. Thread-safe.
class ProceduralDiagnostics
{
public:
    static ProceduralDiagnostics &shared();

    void setClip(const std::string &clipId, ProceduralClipReport report);
    // False when the clip has not been evaluated.
    bool clip(const std::string &clipId, ProceduralClipReport *out) const;
    void clearClip(const std::string &clipId);

    // Keyed by the package's sourceHash and the shader pair's basename, so an edited package starts
    // with a clean log. Reported once a program has been compiled; nothing before the first draw.
    void setShader(const std::string &sourceHash, const std::string &shader, ProceduralShaderStatus status);
    std::map<std::string, ProceduralShaderStatus> shaders(const std::string &sourceHash) const;

private:
    mutable std::mutex m_mutex;
    std::unordered_map<std::string, ProceduralClipReport> m_clips;
    std::map<std::string, std::map<std::string, ProceduralShaderStatus>> m_shaders;
    std::uint64_t m_serial = 0;
};

} // namespace drift
