#include "engine/procedural/ProceduralEvaluator.h"

#include "engine/procedural/LuaSandbox.h"
#include "engine/procedural/ProceduralGeometry.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>

namespace drift {

ProceduralBudgets ProceduralBudgets::forPlatform(bool exportMode)
{
    ProceduralBudgets b;
#if defined(__EMSCRIPTEN__)
    b.maxVertices = 150000;
    b.maxInstances = 15000;
    b.luaMemoryBytes = 24u * 1024u * 1024u;
    b.nativeMemoryBytes = 192u * 1024u * 1024u;
#elif defined(__ANDROID__)
    b.maxVertices = 300000;
    b.maxInstances = 30000;
    b.luaMemoryBytes = 24u * 1024u * 1024u;
    b.nativeMemoryBytes = 192u * 1024u * 1024u;
#endif
    b.maxWork = b.maxVertices * 16;
    b.deadlineMs = exportMode ? 5000 : 250;
    return b;
}

namespace {

constexpr std::size_t kMaxLights = 32;

struct EvalCtx
{
    ProceduralScene *out = nullptr;
    const ProceduralBudgets *budgets = nullptr;
    std::size_t vertexTotal = 0;
    std::size_t instanceTotal = 0;
    bool cameraRead = false;
};

EvalCtx *ctxOf(lua_State *L)
{
    return static_cast<EvalCtx *>(lua_touserdata(L, lua_upvalueindex(1)));
}

[[noreturn]] void budgetFail(lua_State *L, const char *what, std::size_t have, std::size_t limit)
{
    const char *msg = lua_pushfstring(L, "%s budget exceeded (%I of %I)", what, lua_Integer(have), lua_Integer(limit));
    if (LuaSandbox *sb = LuaSandbox::from(L))
        sb->abort(msg);
    for (;;)
        lua_error(L);
}

bool validName(const char *s, std::size_t max)
{
    if (!s || !(*s == '_' || std::isalpha(static_cast<unsigned char>(*s))) || std::strlen(s) > max)
        return false;
    for (; *s; ++s)
        if (!(*s == '_' || std::isalnum(static_cast<unsigned char>(*s))))
            return false;
    return true;
}

// Returns the number of floats a uniform value holds (1, 2, 3, 4, 16), or 0 if it is not a valid
// uniform. Writes them to `out` (16 floats) when non-null.
int uniformValue(lua_State *L, int idx, float *out)
{
    float tmp[16];
    float *dst = out ? out : tmp;
    switch (lua_type(L, idx)) {
    case LUA_TNUMBER:
        dst[0] = float(lua_tonumber(L, idx));
        return 1;
    case LUA_TBOOLEAN:
        dst[0] = lua_toboolean(L, idx) ? 1.f : 0.f;
        return 1;
    case LUA_TUSERDATA:
        return toProceduralMat(L, idx, dst) ? 16 : 0;
    case LUA_TTABLE: {
        idx = lua_absindex(L, idx);
        int n = int(lua_rawlen(L, idx));
        if (n == 0) {
            lua_getfield(L, idx, "w");
            const bool w = !lua_isnil(L, -1);
            lua_pop(L, 1);
            lua_getfield(L, idx, "z");
            const bool z = !lua_isnil(L, -1);
            lua_pop(L, 1);
            lua_getfield(L, idx, "y");
            const bool y = !lua_isnil(L, -1);
            lua_pop(L, 1);
            n = w ? 4 : z ? 3 : y ? 2 : 0;
        }
        if (n != 2 && n != 3 && n != 4 && n != 16)
            return 0;
        for (int i = 0; i < n; ++i) {
            lua_rawgeti(L, idx, i + 1);
            if (lua_isnil(L, -1) && i < 4) {
                lua_pop(L, 1);
                lua_getfield(L, idx, i == 0 ? "x" : i == 1 ? "y" : i == 2 ? "z" : "w");
            }
            if (!lua_isnumber(L, -1)) {
                lua_pop(L, 1);
                return 0;
            }
            dst[i] = float(lua_tonumber(L, -1));
            lua_pop(L, 1);
        }
        return n;
    }
    default:
        return 0;
    }
}

int sceneAdd(lua_State *L)
{
    EvalCtx *ctx = ctxOf(L);
    ProceduralMesh *mesh = toProceduralMesh(L, 2);
    if (!mesh)
        return luaL_error(L, "scene:add: first argument must be a mesh");
    const char *shader = luaL_checkstring(L, 3);
    if (!validName(shader, 64))
        return luaL_error(L, "scene:add: invalid shader name '%s'", shader);
    const bool hasUniforms = !lua_isnoneornil(L, 4);
    if (hasUniforms) {
        luaL_checktype(L, 4, LUA_TTABLE);
        lua_pushnil(L);
        while (lua_next(L, 4)) {
            if (lua_type(L, -2) != LUA_TSTRING || !validName(lua_tostring(L, -2), 64))
                return luaL_error(L, "scene:add: uniform names must be identifiers");
            if (!uniformValue(L, -1, nullptr))
                return luaL_error(L, "scene:add: uniform '%s' must be a number, boolean, vec2-4 table or mat", lua_tostring(L, -2));
            lua_pop(L, 1);
        }
    }

    const std::size_t n = mesh->vertexCount();
    if (n == 0)
        return luaL_error(L, "scene:add: mesh has no vertices");
    if ((!mesh->normal.empty() && mesh->normal.size() != n * 3) || (!mesh->uv.empty() && mesh->uv.size() != n * 2)
        || (!mesh->color.empty() && mesh->color.size() != n * 4))
        return luaL_error(L, "scene:add: attribute sizes do not match the vertex count");
    for (const auto &c : mesh->custom)
        if (c.data.size() != n * std::size_t(c.components))
            return luaL_error(L, "scene:add: attribute '%s' does not match the vertex count", c.name.c_str());

    if (ctx->vertexTotal + n > ctx->budgets->maxVertices)
        budgetFail(L, "vertex", ctx->vertexTotal + n, ctx->budgets->maxVertices);
    const std::size_t inst = mesh->instanceCount();
    if (ctx->instanceTotal + inst > ctx->budgets->maxInstances)
        budgetFail(L, "instance", ctx->instanceTotal + inst, ctx->budgets->maxInstances);
    ctx->vertexTotal += n;
    ctx->instanceTotal += inst;

    ProceduralMeshEntry &entry = ctx->out->meshes.emplace_back();
    entry.mesh = std::make_shared<const ProceduralMesh>(*mesh);
    entry.shader = shader;
    if (hasUniforms) {
        lua_pushnil(L);
        while (lua_next(L, 4)) {
            float v[16];
            const int count = uniformValue(L, -1, v);
            entry.uniforms[lua_tostring(L, -2)] = std::vector<float>(v, v + count);
            lua_pop(L, 1);
        }
    }
    return 0;
}

int sceneLight(lua_State *L)
{
    EvalCtx *ctx = ctxOf(L);
    luaL_checktype(L, 2, LUA_TTABLE);
    if (ctx->out->lights.size() >= kMaxLights)
        return luaL_error(L, "scene:light: at most %d lights", int(kMaxLights));
    ProceduralLight light;
    lua_getfield(L, 2, "type");
    const char *type = lua_isnil(L, -1) ? "point" : luaL_checkstring(L, -1);
    if (!std::strcmp(type, "point")) light.type = ProceduralLight::Type::Point;
    else if (!std::strcmp(type, "directional")) light.type = ProceduralLight::Type::Directional;
    else if (!std::strcmp(type, "spot")) light.type = ProceduralLight::Type::Spot;
    else return luaL_error(L, "scene:light: type must be point, directional or spot");
    lua_pop(L, 1);
    auto vec = [&](const char *key, std::array<float, 3> &dst) {
        lua_getfield(L, 2, key);
        if (!lua_isnil(L, -1) && !readProceduralVec3(L, -1, dst.data()))
            luaL_error(L, "scene:light: '%s' must be a {x, y, z} vector", key);
        lua_pop(L, 1);
    };
    auto num = [&](const char *key, float &dst) {
        lua_getfield(L, 2, key);
        if (!lua_isnil(L, -1))
            dst = float(luaL_checknumber(L, -1));
        lua_pop(L, 1);
    };
    vec("pos", light.pos);
    vec("dir", light.dir);
    vec("color", light.color);
    num("intensity", light.intensity);
    num("range", light.range);
    light.cone = 0.5f;
    num("cone", light.cone);
    const float l = std::sqrt(light.dir[0] * light.dir[0] + light.dir[1] * light.dir[1] + light.dir[2] * light.dir[2]);
    if (l > 0.f)
        for (float &c : light.dir)
            c /= l;
    ctx->out->lights.push_back(light);
    return 0;
}

int sceneCamera(lua_State *L)
{
    EvalCtx *ctx = ctxOf(L);
    luaL_checktype(L, 2, LUA_TTABLE);
    ProceduralSceneCamera cam;
    auto vec = [&](const char *key, std::array<float, 3> &dst, bool required) {
        lua_getfield(L, 2, key);
        if (lua_isnil(L, -1) ? required : !readProceduralVec3(L, -1, dst.data()))
            luaL_error(L, "scene:camera: '%s' must be a {x, y, z} vector", key);
        lua_pop(L, 1);
    };
    auto num = [&](const char *key, float &dst) {
        lua_getfield(L, 2, key);
        if (!lua_isnil(L, -1))
            dst = float(luaL_checknumber(L, -1));
        lua_pop(L, 1);
    };
    vec("eye", cam.eye, true);
    vec("target", cam.target, true);
    vec("up", cam.up, false);
    num("fov", cam.fov);
    num("near", cam.nearPlane);
    num("far", cam.farPlane);
    if (!(cam.fov > 0.f && cam.fov < 3.14159f) || cam.nearPlane <= 0.f || cam.farPlane <= cam.nearPlane)
        return luaL_error(L, "scene:camera: invalid fov or clip range");
    ctx->out->camera = cam;
    ctx->out->hasCamera = true;
    return 0;
}

int cameraIndex(lua_State *L)
{
    ctxOf(L)->cameraRead = true;
    lua_pushvalue(L, 2);
    lua_rawget(L, lua_upvalueindex(2));
    return 1;
}

int cameraPairs(lua_State *L)
{
    ctxOf(L)->cameraRead = true;
    lua_getglobal(L, "next");
    lua_pushvalue(L, lua_upvalueindex(2));
    lua_pushnil(L);
    return 3;
}

int readOnly(lua_State *L)
{
    return luaL_error(L, "this table is read-only");
}

void pushVec3(lua_State *L, const std::array<float, 3> &v)
{
    pushProceduralVec3(L, v[0], v[1], v[2]);
}

void pushParams(lua_State *L, const ProceduralParams &params)
{
    lua_createtable(L, 0, int(params.size()));
    for (const auto &[key, p] : params) {
        switch (p.kind) {
        case ProceduralParam::Kind::Number: lua_pushnumber(L, p.number); break;
        case ProceduralParam::Kind::Bool: lua_pushboolean(L, p.boolean); break;
        case ProceduralParam::Kind::String: lua_pushlstring(L, p.string.data(), p.string.size()); break;
        case ProceduralParam::Kind::Vector: {
            lua_createtable(L, int(p.vector.size()), 4);
            static const char *const names[4] = {"x", "y", "z", "w"};
            for (std::size_t i = 0; i < p.vector.size(); ++i) {
                lua_pushnumber(L, p.vector[i]);
                lua_rawseti(L, -2, lua_Integer(i + 1));
                if (i < 4) {
                    lua_pushnumber(L, p.vector[i]);
                    lua_setfield(L, -2, names[i]);
                }
            }
            break;
        }
        }
        lua_setfield(L, -2, key.c_str());
    }
}

void pushCamera(lua_State *L, EvalCtx *ctx, const ProceduralCameraInput &c)
{
    lua_createtable(L, 0, 9); // backing
    pushVec3(L, c.eye);
    lua_setfield(L, -2, "eye");
    pushVec3(L, c.right);
    lua_setfield(L, -2, "right");
    pushVec3(L, c.up);
    lua_setfield(L, -2, "up");
    pushVec3(L, c.forward);
    lua_setfield(L, -2, "forward");
    lua_pushnumber(L, c.fov);
    lua_setfield(L, -2, "fov");
    lua_pushnumber(L, c.nearPlane);
    lua_setfield(L, -2, "near");
    lua_pushnumber(L, c.farPlane);
    lua_setfield(L, -2, "far");
    pushProceduralMat(L, c.view.data());
    lua_setfield(L, -2, "view");
    pushProceduralMat(L, c.proj.data());
    lua_setfield(L, -2, "proj");

    // The proxy is empty so every access goes through __index and is seen.
    lua_newtable(L);
    lua_newtable(L); // metatable
    lua_pushlightuserdata(L, ctx);
    lua_pushvalue(L, -4);
    lua_pushcclosure(L, cameraIndex, 2);
    lua_setfield(L, -2, "__index");
    lua_pushlightuserdata(L, ctx);
    lua_pushvalue(L, -4);
    lua_pushcclosure(L, cameraPairs, 2);
    lua_setfield(L, -2, "__pairs");
    lua_pushcfunction(L, readOnly);
    lua_setfield(L, -2, "__newindex");
    lua_pushboolean(L, 0);
    lua_setfield(L, -2, "__metatable");
    lua_setmetatable(L, -2);
    lua_remove(L, -2); // backing
}

void pushScene(lua_State *L, EvalCtx *ctx)
{
    lua_newtable(L);
    lua_newtable(L); // __index methods
    const struct { const char *name; lua_CFunction fn; } methods[] = {
        {"add", sceneAdd}, {"light", sceneLight}, {"camera", sceneCamera}};
    for (const auto &m : methods) {
        lua_pushlightuserdata(L, ctx);
        lua_pushcclosure(L, m.fn, 1);
        lua_setfield(L, -2, m.name);
    }
    lua_newtable(L); // metatable
    lua_insert(L, -2);
    lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, readOnly);
    lua_setfield(L, -2, "__newindex");
    lua_pushboolean(L, 0);
    lua_setfield(L, -2, "__metatable");
    lua_setmetatable(L, -2);
}

void transformPoint(const float *m, const float *p, float *o)
{
    for (int r = 0; r < 3; ++r)
        o[r] = m[r] * p[0] + m[4 + r] * p[1] + m[8 + r] * p[2] + m[12 + r];
}

void finalize(ProceduralScene &scene)
{
    ProceduralStats &st = scene.stats;
    st.meshCount = scene.meshes.size();
    for (const ProceduralMeshEntry &e : scene.meshes) {
        const ProceduralMesh &m = *e.mesh;
        st.vertexCount += m.vertexCount();
        st.instanceCount += std::max<std::size_t>(1, m.instanceCount());
        if (m.topology == ProceduralMesh::Topology::Triangles)
            st.triangleCount += m.indices.size() / 3;
        std::array<float, 3> mn, mx;
        if (!m.bounds(mn, mx))
            continue;
        auto grow = [&](const float *p) {
            if (!scene.aabbValid) {
                scene.aabbMin = {p[0], p[1], p[2]};
                scene.aabbMax = scene.aabbMin;
                scene.aabbValid = true;
                return;
            }
            for (int a = 0; a < 3; ++a) {
                scene.aabbMin[a] = std::min(scene.aabbMin[a], p[a]);
                scene.aabbMax[a] = std::max(scene.aabbMax[a], p[a]);
            }
        };
        const std::size_t n = std::max<std::size_t>(1, m.instanceCount());
        for (std::size_t i = 0; i < n; ++i)
            for (int corner = 0; corner < 8; ++corner) {
                const float c[3] = {(corner & 1) ? mx[0] : mn[0], (corner & 2) ? mx[1] : mn[1], (corner & 4) ? mx[2] : mn[2]};
                if (m.instanceCount() == 0) {
                    grow(c);
                } else {
                    float o[3];
                    transformPoint(&m.instances[i * 16], c, o);
                    grow(o);
                }
            }
    }
}

} // namespace

ProceduralScene ProceduralEvaluator::evaluate(const std::string &source, const ProceduralParams &params, double t,
                                              const ProceduralCameraInput &camera, const ProceduralCanvas &canvas,
                                              const ProceduralBudgets &budgets, int frame, std::int64_t seed)
{
    const auto started = std::chrono::steady_clock::now();
    ProceduralScene scene;
    EvalCtx ctx;
    ctx.out = &scene;
    ctx.budgets = &budgets;
    std::string error;
    std::size_t luaPeak = 0;
    {
        LuaSandbox::Limits limits;
        limits.memoryBytes = budgets.luaMemoryBytes;
        limits.deadline = std::chrono::milliseconds(budgets.deadlineMs);
        limits.maxLogLines = budgets.maxLogLines;
        LuaSandbox sandbox(limits);
        lua_State *L = sandbox.state();

        GeometryLimits geometry;
        geometry.maxVertices = budgets.maxVertices;
        geometry.maxInstances = budgets.maxInstances;
        geometry.maxWork = budgets.maxWork;
        geometry.maxNativeBytes = budgets.nativeMemoryBytes;
        registerProceduralGeometry(L, &geometry);

        lua_pushnumber(L, t);
        lua_setglobal(L, "time");
        lua_pushinteger(L, frame);
        lua_setglobal(L, "frame");
        lua_pushinteger(L, lua_Integer(seed));
        lua_setglobal(L, "seed");
        pushParams(L, params);
        lua_setglobal(L, "params");
        lua_createtable(L, 0, 2);
        lua_pushinteger(L, canvas.width);
        lua_setfield(L, -2, "w");
        lua_pushinteger(L, canvas.height);
        lua_setfield(L, -2, "h");
        lua_setglobal(L, "canvas");
        pushCamera(L, &ctx, camera);
        lua_setglobal(L, "camera");
        pushScene(L, &ctx);
        lua_setglobal(L, "scene");

        const bool ok = sandbox.run(source, "=main.lua", &error);
        scene.log = sandbox.takeLog();
        luaPeak = sandbox.memoryUsed();
        if (!ok) {
            scene.meshes.clear();
            scene.lights.clear();
            scene.hasCamera = false;
            scene.errors.push_back(error);
        }
    }
    scene.cameraRead = ctx.cameraRead;
    if (scene.ok())
        finalize(scene);
    scene.stats.luaBytesPeak = luaPeak;
    scene.stats.elapsedMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    return scene;
}

} // namespace drift
