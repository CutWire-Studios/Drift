#pragma once

#include "engine/procedural/LuaInclude.h"
#include "engine/procedural/ProceduralMesh.h"

#include <cstddef>

namespace drift {

// Per-evaluation limits seen by the native ops; the evaluator fills this and registers it on the state.
struct GeometryLimits
{
    std::size_t maxVertices = 1000000;
    std::size_t maxInstances = 100000;
    // Total elements created or visited by native ops and Lua callbacks (buf:map). Stops a script from
    // looping allocations of individually legal size.
    std::size_t maxWork = 8000000;
    std::size_t maxNativeBytes = 512u * 1024u * 1024u;
};

// Adds geo, buf, mat, noise and rand to the state's globals and attaches `limits` (must outlive the state).
void registerProceduralGeometry(lua_State *L, GeometryLimits *limits);

// Reads a mesh/mat userdata from the Lua stack; null/false if the value is not one.
ProceduralMesh *toProceduralMesh(lua_State *L, int idx);
bool toProceduralMat(lua_State *L, int idx, float out[16]);
void pushProceduralMat(lua_State *L, const float m[16]);
// Accepts {x,y,z} (array or named fields).
bool readProceduralVec3(lua_State *L, int idx, float out[3]);
// Pushes {x,y,z} with both [1..3] and x/y/z fields.
void pushProceduralVec3(lua_State *L, float x, float y, float z);
// Raises a Lua error, and aborts the evaluation, when `count` more vertices would pass the budget.
void checkProceduralVertices(lua_State *L, std::size_t count);

} // namespace drift
