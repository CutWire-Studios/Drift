#pragma once

#include "engine/procedural/ProceduralScene.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace drift {

// A resolved clip parameter as the script sees it in `params`.
struct ProceduralParam
{
    enum class Kind { Number, Bool, String, Vector };

    Kind kind = Kind::Number;
    double number = 0.0;
    bool boolean = false;
    std::string string;
    std::vector<double> vector; // 2-4 components; exposed as {x, y, z, w} and [1..n]

    static ProceduralParam fromNumber(double v) { ProceduralParam p; p.number = v; return p; }
    static ProceduralParam fromBool(bool v) { ProceduralParam p; p.kind = Kind::Bool; p.boolean = v; return p; }
    static ProceduralParam fromString(std::string v) { ProceduralParam p; p.kind = Kind::String; p.string = std::move(v); return p; }
    static ProceduralParam fromVector(std::vector<double> v) { ProceduralParam p; p.kind = Kind::Vector; p.vector = std::move(v); return p; }
};

using ProceduralParams = std::map<std::string, ProceduralParam>;

// What the script sees as `camera`. Matrices are column-major; angles are radians.
struct ProceduralCameraInput
{
    std::array<float, 3> eye{0, 0, 0};
    std::array<float, 3> right{1, 0, 0};
    std::array<float, 3> up{0, -1, 0};
    std::array<float, 3> forward{0, 0, 1};
    float fov = 1.0f;
    float nearPlane = 1.f;
    float farPlane = 100000.f;
    std::array<float, 16> view{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    std::array<float, 16> proj{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
};

struct ProceduralCanvas
{
    int width = 1920;
    int height = 1080;
};

struct ProceduralBudgets
{
    std::size_t maxVertices = 1000000;
    std::size_t maxInstances = 100000;
    std::size_t luaMemoryBytes = 64u * 1024u * 1024u;
    std::size_t nativeMemoryBytes = 512u * 1024u * 1024u;
    std::size_t maxWork = 16000000; // elements touched by native ops and buf:map callbacks
    int deadlineMs = 250;
    std::size_t maxLogLines = 200;

    // Platform defaults: desktop 1M/100k, Android 300k/30k, wasm 150k/15k; export gets 5 s.
    static ProceduralBudgets forPlatform(bool exportMode = false);
};

class ProceduralEvaluator
{
public:
    // Runs `source` in a fresh sandboxed Lua state on the calling thread. Never throws; a Lua error,
    // timeout or budget overflow yields an error scene (no meshes, `errors` populated).
    // `t` is clip source time in seconds. Equal inputs give identical scenes.
    static ProceduralScene evaluate(const std::string &source, const ProceduralParams &params, double t,
                                    const ProceduralCameraInput &camera, const ProceduralCanvas &canvas,
                                    const ProceduralBudgets &budgets, int frame = 0, std::int64_t seed = 0);
};

} // namespace drift
