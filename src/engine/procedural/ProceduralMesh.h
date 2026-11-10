#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace drift {

// CPU geometry for procedural clips. No Qt types: the evaluator runs on any thread and the GL side
// uploads straight from these arrays. Conventions: pixels, origin at the canvas top-left, +X right,
// +Y down, +Z away from the viewer. Triangles are wound so (v1-v0)x(v2-v0) is the outward normal.

enum class BufferKind { Float, Vec2, Vec3, Vec4, Mat4 };

constexpr int bufferComponents(BufferKind k)
{
    switch (k) {
    case BufferKind::Float: return 1;
    case BufferKind::Vec2: return 2;
    case BufferKind::Vec3: return 3;
    case BufferKind::Vec4: return 4;
    case BufferKind::Mat4: return 16;
    }
    return 1;
}

struct ProceduralBuffer
{
    BufferKind kind = BufferKind::Float;
    std::vector<float> data;

    std::size_t count() const { return data.size() / std::size_t(bufferComponents(kind)); }
};

struct ProceduralMesh
{
    enum class Topology { Triangles, Lines, Points };

    struct Custom
    {
        std::string name;
        int components = 1; // 1..4
        std::vector<float> data;
    };

    Topology topology = Topology::Triangles;
    std::vector<float> position; // xyz
    std::vector<float> normal;   // xyz, empty or one per vertex
    std::vector<float> uv;       // xy
    std::vector<float> color;    // rgba
    std::vector<Custom> custom;  // attribute locations 4.. in this order
    std::vector<std::uint32_t> indices;
    std::vector<float> instances; // column-major mat4 per instance; empty means a single draw

    std::size_t vertexCount() const { return position.size() / 3; }
    std::size_t instanceCount() const { return instances.size() / 16; }
    std::size_t byteSize() const;
    // Local-space bounds of the vertices; false for an empty mesh.
    bool bounds(std::array<float, 3> &mn, std::array<float, 3> &mx) const;
};

constexpr int kMaxCustomAttributes = 8;

// Deterministic gradient noise shared by the Lua API and tests. Fixed permutation table, so the
// result depends only on the arguments.
float proceduralPerlin(float x, float y, float z);
float proceduralSimplex(float x, float y, float z);
float proceduralFbm(float x, float y, float z, int octaves, float lacunarity, float gain);

} // namespace drift
