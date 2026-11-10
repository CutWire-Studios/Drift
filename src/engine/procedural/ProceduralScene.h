#pragma once

#include "engine/procedural/ProceduralMesh.h"

#include <array>
#include <cstddef>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace drift {

struct ProceduralLight
{
    enum class Type { Point, Directional, Spot };

    Type type = Type::Point;
    std::array<float, 3> pos{0, 0, 0};
    std::array<float, 3> dir{0, 0, 1};
    std::array<float, 3> color{1, 1, 1};
    float intensity = 1.f;
    float range = 0.f; // 0 = unlimited
    float cone = 0.f;  // spot half-angle, radians
};

// A camera the script chose itself with scene:camera{}. Angles are radians; fov is vertical.
struct ProceduralSceneCamera
{
    std::array<float, 3> eye{0, 0, 0};
    std::array<float, 3> target{0, 0, 1};
    std::array<float, 3> up{0, -1, 0};
    float fov = 1.0f;
    float nearPlane = 1.f;
    float farPlane = 100000.f;
};

struct ProceduralMeshEntry
{
    std::shared_ptr<const ProceduralMesh> mesh;
    std::string shader; // basename of a vert/frag pair in the package
    // Each uniform is 1, 2, 3, 4 or 16 floats (float/vecN/mat4); the shader declares the type.
    std::map<std::string, std::vector<float>> uniforms;

    std::size_t instanceCount() const { return mesh ? mesh->instanceCount() : 0; }
};

struct ProceduralStats
{
    std::size_t meshCount = 0;
    std::size_t vertexCount = 0;    // sum over meshes, before instancing
    std::size_t instanceCount = 0;  // sum over meshes of max(1, instances)
    std::size_t triangleCount = 0;  // before instancing
    std::size_t luaBytesPeak = 0;
    double elapsedMs = 0.0;
};

struct ProceduralScene
{
    std::vector<ProceduralMeshEntry> meshes;
    std::vector<ProceduralLight> lights; // renderers use the first 8
    bool hasCamera = false;
    ProceduralSceneCamera camera;

    // World-space bounds over every mesh including instance transforms; empty scenes have valid == false.
    bool aabbValid = false;
    std::array<float, 3> aabbMin{0, 0, 0};
    std::array<float, 3> aabbMax{0, 0, 0};

    std::vector<std::string> log;    // print() output, oldest first
    std::vector<std::string> errors; // non-empty means an error scene: no meshes, lights or camera
    ProceduralStats stats;

    // True when the script read any field of the `camera` global; the scene cache keys on the camera
    // matrices only then.
    bool cameraRead = false;

    // Content hash per mesh (parallel to `meshes`), filled by ProceduralSceneCache so the GL side
    // can key its buffer cache without rehashing every frame. Empty for a scene built elsewhere.
    std::vector<std::uint64_t> meshHashes;

    bool ok() const { return errors.empty(); }
};

} // namespace drift
