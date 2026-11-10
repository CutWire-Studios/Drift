#include "engine/procedural/ProceduralGeometry.h"

#include "engine/procedural/LuaSandbox.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <new>
#include <unordered_map>

namespace drift {

// --- mesh helpers ---------------------------------------------------------------------------------

std::size_t ProceduralMesh::byteSize() const
{
    std::size_t b = (position.capacity() + normal.capacity() + uv.capacity() + color.capacity()
                     + instances.capacity()) * sizeof(float)
        + indices.capacity() * sizeof(std::uint32_t);
    for (const Custom &c : custom)
        b += c.data.capacity() * sizeof(float);
    return b;
}

bool ProceduralMesh::bounds(std::array<float, 3> &mn, std::array<float, 3> &mx) const
{
    const std::size_t n = vertexCount();
    if (n == 0)
        return false;
    mn = {position[0], position[1], position[2]};
    mx = mn;
    for (std::size_t i = 1; i < n; ++i)
        for (int a = 0; a < 3; ++a) {
            mn[a] = std::min(mn[a], position[i * 3 + a]);
            mx[a] = std::max(mx[a], position[i * 3 + a]);
        }
    return true;
}

// --- noise ----------------------------------------------------------------------------------------

namespace {

std::uint64_t splitmix64(std::uint64_t &s)
{
    s += 0x9E3779B97F4A7C15ull;
    std::uint64_t z = s;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

const std::array<std::uint8_t, 512> &permutation()
{
    static const std::array<std::uint8_t, 512> table = [] {
        std::array<std::uint8_t, 512> t{};
        std::array<std::uint8_t, 256> p{};
        for (int i = 0; i < 256; ++i)
            p[i] = std::uint8_t(i);
        std::uint64_t s = 0x5EEDF00DCAFEull;
        for (int i = 255; i > 0; --i)
            std::swap(p[i], p[splitmix64(s) % std::uint64_t(i + 1)]);
        for (int i = 0; i < 512; ++i)
            t[i] = p[i & 255];
        return t;
    }();
    return table;
}

float fade(float t) { return t * t * t * (t * (t * 6.f - 15.f) + 10.f); }
float lerpf(float t, float a, float b) { return a + t * (b - a); }

float grad(int hash, float x, float y, float z)
{
    const int h = hash & 15;
    const float u = h < 8 ? x : y;
    const float v = h < 4 ? y : (h == 12 || h == 14 ? x : z);
    return ((h & 1) ? -u : u) + ((h & 2) ? -v : v);
}

} // namespace

float proceduralPerlin(float x, float y, float z)
{
    const auto &p = permutation();
    const float fx = std::floor(x), fy = std::floor(y), fz = std::floor(z);
    const int X = int(std::int64_t(fx) & 255), Y = int(std::int64_t(fy) & 255), Z = int(std::int64_t(fz) & 255);
    x -= fx;
    y -= fy;
    z -= fz;
    const float u = fade(x), v = fade(y), w = fade(z);
    const int A = p[X] + Y, AA = p[A] + Z, AB = p[A + 1] + Z;
    const int B = p[X + 1] + Y, BA = p[B] + Z, BB = p[B + 1] + Z;
    return lerpf(w,
                 lerpf(v, lerpf(u, grad(p[AA], x, y, z), grad(p[BA], x - 1, y, z)),
                       lerpf(u, grad(p[AB], x, y - 1, z), grad(p[BB], x - 1, y - 1, z))),
                 lerpf(v, lerpf(u, grad(p[AA + 1], x, y, z - 1), grad(p[BA + 1], x - 1, y, z - 1)),
                       lerpf(u, grad(p[AB + 1], x, y - 1, z - 1), grad(p[BB + 1], x - 1, y - 1, z - 1))));
}

float proceduralSimplex(float x, float y, float z)
{
    static const int g3[12][3] = {{1, 1, 0}, {-1, 1, 0}, {1, -1, 0}, {-1, -1, 0}, {1, 0, 1}, {-1, 0, 1},
                                  {1, 0, -1}, {-1, 0, -1}, {0, 1, 1}, {0, -1, 1}, {0, 1, -1}, {0, -1, -1}};
    const auto &perm = permutation();
    constexpr float F3 = 1.f / 3.f, G3 = 1.f / 6.f;
    const float s = (x + y + z) * F3;
    const int i = int(std::floor(x + s)), j = int(std::floor(y + s)), k = int(std::floor(z + s));
    const float t = float(i + j + k) * G3;
    const float x0 = x - (float(i) - t), y0 = y - (float(j) - t), z0 = z - (float(k) - t);
    int i1, j1, k1, i2, j2, k2;
    if (x0 >= y0) {
        if (y0 >= z0) { i1 = 1; j1 = 0; k1 = 0; i2 = 1; j2 = 1; k2 = 0; }
        else if (x0 >= z0) { i1 = 1; j1 = 0; k1 = 0; i2 = 1; j2 = 0; k2 = 1; }
        else { i1 = 0; j1 = 0; k1 = 1; i2 = 1; j2 = 0; k2 = 1; }
    } else {
        if (y0 < z0) { i1 = 0; j1 = 0; k1 = 1; i2 = 0; j2 = 1; k2 = 1; }
        else if (x0 < z0) { i1 = 0; j1 = 1; k1 = 0; i2 = 0; j2 = 1; k2 = 1; }
        else { i1 = 0; j1 = 1; k1 = 0; i2 = 1; j2 = 1; k2 = 0; }
    }
    const float x1 = x0 - i1 + G3, y1 = y0 - j1 + G3, z1 = z0 - k1 + G3;
    const float x2 = x0 - i2 + 2.f * G3, y2 = y0 - j2 + 2.f * G3, z2 = z0 - k2 + 2.f * G3;
    const float x3 = x0 - 1.f + 3.f * G3, y3 = y0 - 1.f + 3.f * G3, z3 = z0 - 1.f + 3.f * G3;
    const int ii = i & 255, jj = j & 255, kk = k & 255;
    auto corner = [&](int di, int dj, int dk, float cx, float cy, float cz) {
        float tt = 0.6f - cx * cx - cy * cy - cz * cz;
        if (tt < 0.f)
            return 0.f;
        const int *g = g3[perm[ii + di + perm[jj + dj + perm[kk + dk]]] % 12];
        tt *= tt;
        return tt * tt * (float(g[0]) * cx + float(g[1]) * cy + float(g[2]) * cz);
    };
    return 32.f * (corner(0, 0, 0, x0, y0, z0) + corner(i1, j1, k1, x1, y1, z1)
                   + corner(i2, j2, k2, x2, y2, z2) + corner(1, 1, 1, x3, y3, z3));
}

float proceduralFbm(float x, float y, float z, int octaves, float lacunarity, float gain)
{
    float sum = 0.f, amp = 1.f, norm = 0.f, f = 1.f;
    for (int o = 0; o < octaves; ++o) {
        sum += amp * proceduralPerlin(x * f, y * f, z * f);
        norm += amp;
        amp *= gain;
        f *= lacunarity;
    }
    return norm > 0.f ? sum / norm : 0.f;
}

// --- Lua bindings -----------------------------------------------------------------------------------

namespace {

char kStateKey;
const char *const kMeshMT = "drift.mesh";
const char *const kBufMT = "drift.buffer";
const char *const kMatMT = "drift.mat";
const char *const kRandMT = "drift.rand";

struct GeoState
{
    GeometryLimits *limits;
    std::size_t work;
    std::size_t nativeBytes;
};

struct MeshUD
{
    ProceduralMesh mesh;
    std::size_t charged;
};

struct BufUD
{
    ProceduralBuffer buf;
    std::size_t charged;
};

struct MatUD
{
    float m[16];
};

struct RandUD
{
    std::uint64_t state;
};

GeoState *geo(lua_State *L)
{
    lua_rawgetp(L, LUA_REGISTRYINDEX, &kStateKey);
    auto *s = static_cast<GeoState *>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    return s;
}

[[noreturn]] void budgetFail(lua_State *L, const char *what, std::size_t have, std::size_t limit)
{
    const char *msg = lua_pushfstring(L, "%s budget exceeded (%I of %I)", what, lua_Integer(have), lua_Integer(limit));
    if (LuaSandbox *sb = LuaSandbox::from(L))
        sb->abort(msg);
    for (;;)
        lua_error(L);
}

void needVertices(lua_State *L, std::size_t n)
{
    GeoState *s = geo(L);
    if (n > s->limits->maxVertices)
        budgetFail(L, "vertex", n, s->limits->maxVertices);
}

void needInstances(lua_State *L, std::size_t n)
{
    GeoState *s = geo(L);
    if (n > s->limits->maxInstances)
        budgetFail(L, "instance", n, s->limits->maxInstances);
}

void addWork(lua_State *L, std::size_t n)
{
    GeoState *s = geo(L);
    s->work += n;
    if (s->work > s->limits->maxWork)
        budgetFail(L, "work", s->work, s->limits->maxWork);
}

void chargeBytes(lua_State *L, std::size_t bytes)
{
    GeoState *s = geo(L);
    s->nativeBytes += bytes;
    if (s->nativeBytes > s->limits->maxNativeBytes)
        budgetFail(L, "memory", s->nativeBytes, s->limits->maxNativeBytes);
}

void releaseBytes(lua_State *L, std::size_t bytes)
{
    if (GeoState *s = geo(L))
        s->nativeBytes = s->nativeBytes > bytes ? s->nativeBytes - bytes : 0;
}

MeshUD *newMesh(lua_State *L)
{
    auto *ud = static_cast<MeshUD *>(lua_newuserdatauv(L, sizeof(MeshUD), 0));
    new (ud) MeshUD{};
    luaL_setmetatable(L, kMeshMT);
    return ud;
}

void recharge(lua_State *L, MeshUD *ud)
{
    const std::size_t now = ud->mesh.byteSize();
    if (now > ud->charged) {
        const std::size_t add = now - ud->charged;
        ud->charged = now;
        chargeBytes(L, add);
    } else {
        releaseBytes(L, ud->charged - now);
        ud->charged = now;
    }
}

BufUD *newBuf(lua_State *L, BufferKind kind, std::size_t n)
{
    needVertices(L, n);
    addWork(L, n);
    const std::size_t bytes = n * std::size_t(bufferComponents(kind)) * sizeof(float);
    chargeBytes(L, bytes);
    auto *ud = static_cast<BufUD *>(lua_newuserdatauv(L, sizeof(BufUD), 0));
    new (ud) BufUD{};
    ud->charged = bytes;
    luaL_setmetatable(L, kBufMT);
    ud->buf.kind = kind;
    ud->buf.data.assign(n * std::size_t(bufferComponents(kind)), 0.f);
    return ud;
}

int meshGc(lua_State *L)
{
    auto *ud = static_cast<MeshUD *>(lua_touserdata(L, 1));
    releaseBytes(L, ud->charged);
    ud->~MeshUD();
    return 0;
}

int bufGc(lua_State *L)
{
    auto *ud = static_cast<BufUD *>(lua_touserdata(L, 1));
    releaseBytes(L, ud->charged);
    ud->~BufUD();
    return 0;
}

MeshUD *checkMesh(lua_State *L, int i) { return static_cast<MeshUD *>(luaL_checkudata(L, i, kMeshMT)); }
BufUD *checkBuf(lua_State *L, int i) { return static_cast<BufUD *>(luaL_checkudata(L, i, kBufMT)); }
MatUD *checkMat(lua_State *L, int i) { return static_cast<MatUD *>(luaL_checkudata(L, i, kMatMT)); }

int optInt(lua_State *L, int i, int def, int lo, int hi)
{
    const lua_Integer v = luaL_optinteger(L, i, def);
    if (v < lo || v > hi)
        return luaL_error(L, "argument #%d must be between %d and %d", i, lo, hi);
    return int(v);
}

bool readVecN(lua_State *L, int idx, float *out, int n)
{
    if (!lua_istable(L, idx))
        return false;
    idx = lua_absindex(L, idx);
    static const char *const names[4] = {"x", "y", "z", "w"};
    for (int i = 0; i < n; ++i) {
        lua_rawgeti(L, idx, i + 1);
        if (lua_isnil(L, -1) && i < 4) {
            lua_pop(L, 1);
            lua_getfield(L, idx, names[i]);
        }
        if (!lua_isnumber(L, -1)) {
            lua_pop(L, 1);
            return false;
        }
        out[i] = float(lua_tonumber(L, -1));
        lua_pop(L, 1);
    }
    return true;
}

void checkVec3(lua_State *L, int idx, float *out)
{
    if (!readVecN(L, idx, out, 3))
        luaL_error(L, "argument #%d must be a {x, y, z} vector", idx);
}

bool optVec3(lua_State *L, int idx, float *out)
{
    if (lua_isnoneornil(L, idx))
        return false;
    checkVec3(L, idx, out);
    return true;
}

float fieldNumber(lua_State *L, int idx, const char *key, float def)
{
    lua_getfield(L, idx, key);
    const float v = lua_isnil(L, -1) ? def : float(luaL_checknumber(L, -1));
    lua_pop(L, 1);
    return v;
}

// --- matrices (column-major: m[col * 4 + row]) -------------------------------------------------------

void matIdentity(float *m)
{
    std::memset(m, 0, 16 * sizeof(float));
    m[0] = m[5] = m[10] = m[15] = 1.f;
}

void matMul(const float *a, const float *b, float *out)
{
    float r[16];
    for (int c = 0; c < 4; ++c)
        for (int row = 0; row < 4; ++row) {
            float s = 0.f;
            for (int k = 0; k < 4; ++k)
                s += a[k * 4 + row] * b[c * 4 + k];
            r[c * 4 + row] = s;
        }
    std::memcpy(out, r, sizeof(r));
}

void matEuler(float rx, float ry, float rz, float *m)
{
    const float cx = std::cos(rx), sx = std::sin(rx), cy = std::cos(ry), sy = std::sin(ry);
    const float cz = std::cos(rz), sz = std::sin(rz);
    matIdentity(m);
    // R = Rz * Ry * Rx
    m[0] = cz * cy;
    m[1] = sz * cy;
    m[2] = -sy;
    m[4] = cz * sy * sx - sz * cx;
    m[5] = sz * sy * sx + cz * cx;
    m[6] = cy * sx;
    m[8] = cz * sy * cx + sz * sx;
    m[9] = sz * sy * cx - cz * sx;
    m[10] = cy * cx;
}

bool matInvert(const float *m, float *out)
{
    float inv[16];
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
    const float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (det == 0.f)
        return false;
    const float d = 1.f / det;
    for (int i = 0; i < 16; ++i)
        out[i] = inv[i] * d;
    return true;
}

void matApply(const float *m, float x, float y, float z, float *out)
{
    float ox = m[0] * x + m[4] * y + m[8] * z + m[12];
    float oy = m[1] * x + m[5] * y + m[9] * z + m[13];
    float oz = m[2] * x + m[6] * y + m[10] * z + m[14];
    const float w = m[3] * x + m[7] * y + m[11] * z + m[15];
    if (w != 1.f && w != 0.f) {
        ox /= w;
        oy /= w;
        oz /= w;
    }
    out[0] = ox;
    out[1] = oy;
    out[2] = oz;
}

float *newMat(lua_State *L)
{
    auto *ud = static_cast<MatUD *>(lua_newuserdatauv(L, sizeof(MatUD), 0));
    luaL_setmetatable(L, kMatMT);
    matIdentity(ud->m);
    return ud->m;
}

bool readMat(lua_State *L, int idx, float *out)
{
    if (void *p = luaL_testudata(L, idx, kMatMT)) {
        std::memcpy(out, static_cast<MatUD *>(p)->m, 16 * sizeof(float));
        return true;
    }
    return readVecN(L, idx, out, 16);
}

int matTranslate(lua_State *L)
{
    float *m = newMat(L);
    m[12] = float(luaL_optnumber(L, 1, 0));
    m[13] = float(luaL_optnumber(L, 2, 0));
    m[14] = float(luaL_optnumber(L, 3, 0));
    return 1;
}

int matRotate(lua_State *L)
{
    const float rx = float(luaL_optnumber(L, 1, 0)), ry = float(luaL_optnumber(L, 2, 0)), rz = float(luaL_optnumber(L, 3, 0));
    matEuler(rx, ry, rz, newMat(L));
    return 1;
}

int matScale(lua_State *L)
{
    const float sx = float(luaL_optnumber(L, 1, 1));
    const float sy = float(luaL_optnumber(L, 2, sx));
    const float sz = float(luaL_optnumber(L, 3, sx));
    float *m = newMat(L);
    m[0] = sx;
    m[5] = sy;
    m[10] = sz;
    return 1;
}

int matCompose(lua_State *L)
{
    float t[3] = {0, 0, 0}, r[3] = {0, 0, 0}, s[3] = {1, 1, 1};
    optVec3(L, 1, t);
    optVec3(L, 2, r);
    if (lua_isnumber(L, 3))
        s[0] = s[1] = s[2] = float(lua_tonumber(L, 3));
    else
        optVec3(L, 3, s);
    float *m = newMat(L);
    matEuler(r[0], r[1], r[2], m);
    for (int c = 0; c < 3; ++c)
        for (int row = 0; row < 3; ++row)
            m[c * 4 + row] *= s[c];
    m[12] = t[0];
    m[13] = t[1];
    m[14] = t[2];
    return 1;
}

// View matrix for a camera looking down +Z in its own space, +X right, +Y up; `up` is given in world space.
int matLookAt(lua_State *L)
{
    float eye[3], target[3], up[3] = {0, -1, 0};
    checkVec3(L, 1, eye);
    checkVec3(L, 2, target);
    optVec3(L, 3, up);
    auto norm = [](float *v) {
        const float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        if (l > 0.f)
            for (int i = 0; i < 3; ++i)
                v[i] /= l;
    };
    auto cross = [](const float *a, const float *b, float *o) {
        o[0] = a[1] * b[2] - a[2] * b[1];
        o[1] = a[2] * b[0] - a[0] * b[2];
        o[2] = a[0] * b[1] - a[1] * b[0];
    };
    float f[3] = {target[0] - eye[0], target[1] - eye[1], target[2] - eye[2]};
    norm(f);
    float r[3];
    cross(f, up, r);
    norm(r);
    float u[3];
    cross(r, f, u);
    float *m = newMat(L);
    const float *rows[3] = {r, u, f};
    for (int row = 0; row < 3; ++row) {
        for (int c = 0; c < 3; ++c)
            m[c * 4 + row] = rows[row][c];
        m[12 + row] = -(rows[row][0] * eye[0] + rows[row][1] * eye[1] + rows[row][2] * eye[2]);
    }
    return 1;
}

// OpenGL-style clip space (z in [-1, 1]) for a camera looking down +Z; fovy in radians.
int matPerspective(lua_State *L)
{
    const float fovy = float(luaL_checknumber(L, 1));
    const float aspect = float(luaL_checknumber(L, 2));
    const float n = float(luaL_checknumber(L, 3));
    const float f = float(luaL_checknumber(L, 4));
    if (!(fovy > 0.f && fovy < 3.14159f) || aspect <= 0.f || n <= 0.f || f <= n)
        return luaL_error(L, "perspective: invalid fov, aspect or clip range");
    const float t = 1.f / std::tan(fovy * 0.5f);
    float *m = newMat(L);
    m[0] = t / aspect;
    m[5] = t;
    m[10] = (f + n) / (f - n);
    m[11] = 1.f;
    m[14] = -2.f * f * n / (f - n);
    m[15] = 0.f;
    return 1;
}

int matIdentityFn(lua_State *L)
{
    newMat(L);
    return 1;
}

int matMulMeta(lua_State *L)
{
    const float *a = checkMat(L, 1)->m;
    const float *b = checkMat(L, 2)->m;
    float *out = newMat(L);
    matMul(a, b, out);
    return 1;
}

int matApplyFn(lua_State *L)
{
    const float *m = checkMat(L, 1)->m;
    const float x = float(luaL_checknumber(L, 2)), y = float(luaL_checknumber(L, 3)), z = float(luaL_checknumber(L, 4));
    float o[3];
    matApply(m, x, y, z, o);
    lua_pushnumber(L, o[0]);
    lua_pushnumber(L, o[1]);
    lua_pushnumber(L, o[2]);
    return 3;
}

int matInverseFn(lua_State *L)
{
    const float *m = checkMat(L, 1)->m;
    float inv[16];
    if (!matInvert(m, inv))
        return luaL_error(L, "matrix is not invertible");
    std::memcpy(newMat(L), inv, sizeof(inv));
    return 1;
}

int matToTable(lua_State *L)
{
    const float *m = checkMat(L, 1)->m;
    lua_createtable(L, 16, 0);
    for (int i = 0; i < 16; ++i) {
        lua_pushnumber(L, m[i]);
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

int matToString(lua_State *L)
{
    lua_pushliteral(L, "mat4");
    return 1;
}

// --- buffers -----------------------------------------------------------------------------------------

bool parseKind(const char *s, BufferKind *out)
{
    if (!std::strcmp(s, "float")) *out = BufferKind::Float;
    else if (!std::strcmp(s, "vec2")) *out = BufferKind::Vec2;
    else if (!std::strcmp(s, "vec3")) *out = BufferKind::Vec3;
    else if (!std::strcmp(s, "vec4")) *out = BufferKind::Vec4;
    else if (!std::strcmp(s, "mat4")) *out = BufferKind::Mat4;
    else return false;
    return true;
}

const char *kindName(BufferKind k)
{
    switch (k) {
    case BufferKind::Float: return "float";
    case BufferKind::Vec2: return "vec2";
    case BufferKind::Vec3: return "vec3";
    case BufferKind::Vec4: return "vec4";
    case BufferKind::Mat4: return "mat4";
    }
    return "float";
}

BufferKind checkKind(lua_State *L, int i)
{
    BufferKind k;
    if (!parseKind(luaL_checkstring(L, i), &k))
        luaL_error(L, "unknown buffer kind '%s' (float, vec2, vec3, vec4 or mat4)", lua_tostring(L, i));
    return k;
}

void pushElement(lua_State *L, const ProceduralBuffer &b, std::size_t i)
{
    const int n = bufferComponents(b.kind);
    const float *p = &b.data[i * std::size_t(n)];
    if (n == 1) {
        lua_pushnumber(L, p[0]);
        return;
    }
    lua_createtable(L, n, 0);
    for (int c = 0; c < n; ++c) {
        lua_pushnumber(L, p[c]);
        lua_rawseti(L, -2, c + 1);
    }
}

bool readElement(lua_State *L, int idx, BufferKind kind, float *dst)
{
    if (kind == BufferKind::Float) {
        if (!lua_isnumber(L, idx))
            return false;
        dst[0] = float(lua_tonumber(L, idx));
        return true;
    }
    if (kind == BufferKind::Mat4)
        return readMat(L, idx, dst);
    return readVecN(L, idx, dst, bufferComponents(kind));
}

int bufNew(lua_State *L)
{
    const BufferKind kind = checkKind(L, 1);
    const lua_Integer n = luaL_checkinteger(L, 2);
    if (n < 0)
        return luaL_error(L, "buffer length must not be negative");
    newBuf(L, kind, std::size_t(n));
    return 1;
}

int bufFrom(lua_State *L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    const std::size_t n = lua_rawlen(L, 1);
    BufferKind kind = BufferKind::Float;
    bool flat = false;
    if (!lua_isnoneornil(L, 2)) {
        kind = checkKind(L, 2);
        flat = true;
    } else if (n > 0) {
        lua_rawgeti(L, 1, 1);
        if (lua_isnumber(L, -1)) {
            kind = BufferKind::Float;
        } else if (luaL_testudata(L, -1, kMatMT)) {
            kind = BufferKind::Mat4;
        } else if (lua_istable(L, -1)) {
            const std::size_t len = lua_rawlen(L, -1);
            if (len == 2) kind = BufferKind::Vec2;
            else if (len == 3) kind = BufferKind::Vec3;
            else if (len == 4) kind = BufferKind::Vec4;
            else if (len == 16) kind = BufferKind::Mat4;
            else {
                lua_pop(L, 1);
                return luaL_error(L, "buf.from: cannot infer a buffer kind from an element of length %d", int(len));
            }
        } else {
            lua_pop(L, 1);
            return luaL_error(L, "buf.from: elements must be numbers, vectors or matrices");
        }
        lua_pop(L, 1);
    }
    const int comps = bufferComponents(kind);
    std::size_t count = n;
    if (flat) {
        if (n % std::size_t(comps) != 0)
            return luaL_error(L, "buf.from: %d values do not divide into %s elements", int(n), kindName(kind));
        count = n / std::size_t(comps);
    }
    BufUD *ud = newBuf(L, kind, count);
    float *d = ud->buf.data.data();
    if (flat || kind == BufferKind::Float) {
        for (std::size_t i = 0; i < n; ++i) {
            lua_rawgeti(L, 1, lua_Integer(i + 1));
            if (!lua_isnumber(L, -1))
                return luaL_error(L, "buf.from: element %d is not a number", int(i + 1));
            d[i] = float(lua_tonumber(L, -1));
            lua_pop(L, 1);
        }
    } else {
        for (std::size_t i = 0; i < n; ++i) {
            lua_rawgeti(L, 1, lua_Integer(i + 1));
            if (!readElement(L, -1, kind, d + i * std::size_t(comps)))
                return luaL_error(L, "buf.from: element %d is not a valid %s", int(i + 1), kindName(kind));
            lua_pop(L, 1);
        }
    }
    return 1;
}

int bufLen(lua_State *L)
{
    lua_pushinteger(L, lua_Integer(checkBuf(L, 1)->buf.count()));
    return 1;
}

int bufToString(lua_State *L)
{
    const BufUD *b = checkBuf(L, 1);
    lua_pushfstring(L, "buffer<%s>[%I]", kindName(b->buf.kind), lua_Integer(b->buf.count()));
    return 1;
}

int bufIndex(lua_State *L)
{
    BufUD *b = checkBuf(L, 1);
    if (lua_isinteger(L, 2)) {
        const lua_Integer i = lua_tointeger(L, 2);
        if (i < 1 || std::size_t(i) > b->buf.count())
            lua_pushnil(L);
        else
            pushElement(L, b->buf, std::size_t(i - 1));
        return 1;
    }
    lua_pushvalue(L, 2);
    lua_rawget(L, lua_upvalueindex(1));
    return 1;
}

int bufNewIndex(lua_State *L)
{
    BufUD *b = checkBuf(L, 1);
    const lua_Integer i = luaL_checkinteger(L, 2);
    if (i < 1 || std::size_t(i) > b->buf.count())
        return luaL_error(L, "buffer index %d out of range", int(i));
    float *dst = &b->buf.data[std::size_t(i - 1) * std::size_t(bufferComponents(b->buf.kind))];
    if (!readElement(L, 3, b->buf.kind, dst))
        return luaL_error(L, "value is not a valid %s", kindName(b->buf.kind));
    return 0;
}

int bufFill(lua_State *L)
{
    BufUD *b = checkBuf(L, 1);
    const int comps = bufferComponents(b->buf.kind);
    float v[16];
    if (lua_isnumber(L, 2) && b->buf.kind != BufferKind::Float) {
        std::fill(v, v + comps, float(lua_tonumber(L, 2)));
    } else if (!readElement(L, 2, b->buf.kind, v)) {
        return luaL_error(L, "fill: value is not a valid %s", kindName(b->buf.kind));
    }
    addWork(L, b->buf.count());
    for (std::size_t i = 0; i < b->buf.count(); ++i)
        std::memcpy(&b->buf.data[i * std::size_t(comps)], v, std::size_t(comps) * sizeof(float));
    lua_settop(L, 1);
    return 1;
}

int bufMap(lua_State *L)
{
    BufUD *b = checkBuf(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    const std::size_t n = b->buf.count();
    const int comps = bufferComponents(b->buf.kind);
    addWork(L, n);
    BufUD *out = newBuf(L, b->buf.kind, n); // stack: self, fn, out
    for (std::size_t i = 0; i < n; ++i) {
        lua_pushvalue(L, 2);
        pushElement(L, b->buf, i);
        lua_pushinteger(L, lua_Integer(i + 1));
        lua_call(L, 2, 1);
        if (!readElement(L, -1, b->buf.kind, &out->buf.data[i * std::size_t(comps)]))
            return luaL_error(L, "map: callback returned an invalid %s at element %d", kindName(b->buf.kind), int(i + 1));
        lua_pop(L, 1);
    }
    return 1;
}

enum class BinOp { Add, Mul };

int bufBinary(lua_State *L, BinOp op)
{
    BufUD *a = checkBuf(L, 1);
    const std::size_t n = a->buf.count();
    const int comps = bufferComponents(a->buf.kind);
    const BufUD *other = nullptr;
    float scalar = 0.f;
    if (lua_isnumber(L, 2)) {
        scalar = float(lua_tonumber(L, 2));
    } else {
        other = checkBuf(L, 2);
        if (other->buf.count() != n)
            return luaL_error(L, "buffers differ in length (%d vs %d)", int(n), int(other->buf.count()));
        if (other->buf.kind != a->buf.kind && other->buf.kind != BufferKind::Float)
            return luaL_error(L, "cannot combine %s with %s", kindName(a->buf.kind), kindName(other->buf.kind));
    }
    BufUD *out = newBuf(L, a->buf.kind, n);
    const bool perElement = other && other->buf.kind == BufferKind::Float && a->buf.kind != BufferKind::Float;
    for (std::size_t i = 0; i < n; ++i)
        for (int c = 0; c < comps; ++c) {
            const std::size_t k = i * std::size_t(comps) + std::size_t(c);
            const float rhs = !other ? scalar : (perElement ? other->buf.data[i] : other->buf.data[k]);
            out->buf.data[k] = op == BinOp::Add ? a->buf.data[k] + rhs : a->buf.data[k] * rhs;
        }
    return 1;
}

int bufAdd(lua_State *L) { return bufBinary(L, BinOp::Add); }
int bufMul(lua_State *L) { return bufBinary(L, BinOp::Mul); }

int bufLerp(lua_State *L)
{
    BufUD *a = checkBuf(L, 1);
    const BufUD *b = checkBuf(L, 2);
    const std::size_t n = a->buf.count();
    const int comps = bufferComponents(a->buf.kind);
    if (b->buf.kind != a->buf.kind || b->buf.count() != n)
        return luaL_error(L, "lerp: buffers must have the same kind and length");
    const BufUD *tb = nullptr;
    float t = 0.f;
    if (lua_isnumber(L, 3)) {
        t = float(lua_tonumber(L, 3));
    } else {
        tb = checkBuf(L, 3);
        if (tb->buf.kind != BufferKind::Float || tb->buf.count() != n)
            return luaL_error(L, "lerp: t must be a number or a float buffer of the same length");
    }
    BufUD *out = newBuf(L, a->buf.kind, n);
    for (std::size_t i = 0; i < n; ++i) {
        const float tt = tb ? tb->buf.data[i] : t;
        for (int c = 0; c < comps; ++c) {
            const std::size_t k = i * std::size_t(comps) + std::size_t(c);
            out->buf.data[k] = a->buf.data[k] + (b->buf.data[k] - a->buf.data[k]) * tt;
        }
    }
    return 1;
}

int bufSlice(lua_State *L)
{
    BufUD *a = checkBuf(L, 1);
    const std::size_t n = a->buf.count();
    const lua_Integer first = luaL_optinteger(L, 2, 1);
    const lua_Integer last = luaL_optinteger(L, 3, lua_Integer(n));
    if (first < 1 || last > lua_Integer(n) || first > last + 1)
        return luaL_error(L, "slice: range %d..%d is outside 1..%d", int(first), int(last), int(n));
    const std::size_t count = std::size_t(last - first + 1);
    const int comps = bufferComponents(a->buf.kind);
    BufUD *out = newBuf(L, a->buf.kind, count);
    if (count)
        std::memcpy(out->buf.data.data(), &a->buf.data[std::size_t(first - 1) * std::size_t(comps)],
                    count * std::size_t(comps) * sizeof(float));
    return 1;
}

int bufGetKind(lua_State *L)
{
    lua_pushstring(L, kindName(checkBuf(L, 1)->buf.kind));
    return 1;
}

// Shared by buf:noise and noise.field: fbm noise sampled at each element treated as a position.
// Arguments after the buffer: freq, octaves, ox, oy, oz.
int noiseField(lua_State *L)
{
    BufUD *a = checkBuf(L, 1);
    if (a->buf.kind == BufferKind::Vec4 || a->buf.kind == BufferKind::Mat4)
        return luaL_error(L, "noise: buffer must be float, vec2 or vec3");
    const float freq = float(luaL_optnumber(L, 2, 1.0));
    const int octaves = optInt(L, 3, 1, 1, 8);
    const float ox = float(luaL_optnumber(L, 4, 0)), oy = float(luaL_optnumber(L, 5, 0)), oz = float(luaL_optnumber(L, 6, 0));
    const std::size_t n = a->buf.count();
    const int comps = bufferComponents(a->buf.kind);
    addWork(L, n * std::size_t(octaves));
    BufUD *out = newBuf(L, BufferKind::Float, n);
    for (std::size_t i = 0; i < n; ++i) {
        const float *p = &a->buf.data[i * std::size_t(comps)];
        const float x = p[0] * freq + ox, y = (comps > 1 ? p[1] : 0.f) * freq + oy, z = (comps > 2 ? p[2] : 0.f) * freq + oz;
        out->buf.data[i] = octaves == 1 ? proceduralPerlin(x, y, z) : proceduralFbm(x, y, z, octaves, 2.f, 0.5f);
    }
    return 1;
}

// --- noise / rand ------------------------------------------------------------------------------------

int noisePerlin(lua_State *L)
{
    lua_pushnumber(L, proceduralPerlin(float(luaL_checknumber(L, 1)), float(luaL_optnumber(L, 2, 0)), float(luaL_optnumber(L, 3, 0))));
    return 1;
}

int noiseSimplex(lua_State *L)
{
    lua_pushnumber(L, proceduralSimplex(float(luaL_checknumber(L, 1)), float(luaL_optnumber(L, 2, 0)), float(luaL_optnumber(L, 3, 0))));
    return 1;
}

int noiseFbm(lua_State *L)
{
    const float x = float(luaL_checknumber(L, 1)), y = float(luaL_optnumber(L, 2, 0)), z = float(luaL_optnumber(L, 3, 0));
    const int octaves = optInt(L, 4, 4, 1, 8);
    const float lac = float(luaL_optnumber(L, 5, 2.0)), gain = float(luaL_optnumber(L, 6, 0.5));
    lua_pushnumber(L, proceduralFbm(x, y, z, octaves, lac, gain));
    return 1;
}

std::uint64_t nextRand(RandUD *r) { return splitmix64(r->state); }

double nextUnit(RandUD *r) { return double(nextRand(r) >> 11) * (1.0 / 9007199254740992.0); }

RandUD *checkRand(lua_State *L, int i) { return static_cast<RandUD *>(luaL_checkudata(L, i, kRandMT)); }

int randNew(lua_State *L)
{
    auto *r = static_cast<RandUD *>(lua_newuserdatauv(L, sizeof(RandUD), 0));
    luaL_setmetatable(L, kRandMT);
    std::uint64_t seed;
    if (lua_isinteger(L, 1)) {
        seed = std::uint64_t(lua_tointeger(L, 1));
    } else {
        const double d = luaL_checknumber(L, 1);
        std::memcpy(&seed, &d, sizeof(seed));
    }
    r->state = seed ^ 0xD1B54A32D192ED03ull;
    return 1;
}

int randFloat(lua_State *L)
{
    lua_pushnumber(L, nextUnit(checkRand(L, 1)));
    return 1;
}

int randRange(lua_State *L)
{
    RandUD *r = checkRand(L, 1);
    const double a = luaL_optnumber(L, 2, 0), b = luaL_optnumber(L, 3, 1);
    lua_pushnumber(L, a + (b - a) * nextUnit(r));
    return 1;
}

int randInt(lua_State *L)
{
    RandUD *r = checkRand(L, 1);
    const lua_Integer a = luaL_checkinteger(L, 2), b = luaL_checkinteger(L, 3);
    if (b < a)
        return luaL_error(L, "int: empty range");
    lua_pushinteger(L, a + lua_Integer(nextRand(r) % (std::uint64_t(b) - std::uint64_t(a) + 1)));
    return 1;
}

int randNormal(lua_State *L)
{
    RandUD *r = checkRand(L, 1);
    const double u1 = std::max(nextUnit(r), 1e-300), u2 = nextUnit(r);
    lua_pushnumber(L, std::sqrt(-2.0 * std::log(u1)) * std::cos(6.283185307179586 * u2));
    return 1;
}

int randUnit(lua_State *L)
{
    RandUD *r = checkRand(L, 1);
    const double z = 2.0 * nextUnit(r) - 1.0, a = 6.283185307179586 * nextUnit(r);
    const double s = std::sqrt(1.0 - z * z);
    lua_pushnumber(L, s * std::cos(a));
    lua_pushnumber(L, s * std::sin(a));
    lua_pushnumber(L, z);
    return 3;
}

// --- mesh generation -----------------------------------------------------------------------------------

// Flips every triangle whose winding disagrees with its vertex normals, so generators only have to
// produce correct normals.
void fixWinding(ProceduralMesh &m)
{
    if (m.normal.empty())
        return;
    for (std::size_t t = 0; t + 2 < m.indices.size(); t += 3) {
        const std::uint32_t i0 = m.indices[t], i1 = m.indices[t + 1], i2 = m.indices[t + 2];
        const float *p0 = &m.position[i0 * 3], *p1 = &m.position[i1 * 3], *p2 = &m.position[i2 * 3];
        const float e1[3] = {p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]};
        const float e2[3] = {p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2]};
        const float n[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
        float d = 0.f;
        for (int a = 0; a < 3; ++a)
            d += n[a] * (m.normal[i0 * 3 + a] + m.normal[i1 * 3 + a] + m.normal[i2 * 3 + a]);
        if (d < 0.f)
            std::swap(m.indices[t + 1], m.indices[t + 2]);
    }
}

void addVertex(ProceduralMesh &m, const float *p, const float *n, float u, float v)
{
    m.position.insert(m.position.end(), p, p + 3);
    m.normal.insert(m.normal.end(), n, n + 3);
    m.uv.push_back(u);
    m.uv.push_back(v);
}

void addGridIndices(ProceduralMesh &m, int rows, int cols)
{
    for (int i = 0; i < rows; ++i)
        for (int j = 0; j < cols; ++j) {
            const std::uint32_t a = std::uint32_t(i * (cols + 1) + j), b = a + 1, c = a + std::uint32_t(cols + 1), d = c + 1;
            m.indices.insert(m.indices.end(), {a, c, b, b, c, d});
        }
}

int geoGrid(lua_State *L)
{
    const float w = float(luaL_checknumber(L, 1)), h = float(luaL_checknumber(L, 2));
    const int nx = optInt(L, 3, 1, 1, 10000), ny = optInt(L, 4, 1, 1, 10000);
    const std::size_t verts = std::size_t(nx + 1) * std::size_t(ny + 1);
    needVertices(L, verts);
    addWork(L, verts);
    MeshUD *ud = newMesh(L);
    ProceduralMesh &m = ud->mesh;
    const float n[3] = {0, 0, -1};
    for (int i = 0; i <= ny; ++i)
        for (int j = 0; j <= nx; ++j) {
            const float u = float(j) / float(nx), v = float(i) / float(ny);
            const float p[3] = {(u - 0.5f) * w, (v - 0.5f) * h, 0.f};
            addVertex(m, p, n, u, v);
        }
    addGridIndices(m, ny, nx);
    fixWinding(m);
    recharge(L, ud);
    return 1;
}

int geoBox(lua_State *L)
{
    const float hw = float(luaL_checknumber(L, 1)) * 0.5f, hh = float(luaL_checknumber(L, 2)) * 0.5f, hd = float(luaL_checknumber(L, 3)) * 0.5f;
    needVertices(L, 24);
    addWork(L, 24);
    MeshUD *ud = newMesh(L);
    ProceduralMesh &m = ud->mesh;
    struct Face { float n[3], u[3], v[3]; };
    const Face faces[6] = {
        {{1, 0, 0}, {0, 0, 1}, {0, 1, 0}},  {{-1, 0, 0}, {0, 0, -1}, {0, 1, 0}},
        {{0, 1, 0}, {1, 0, 0}, {0, 0, 1}},  {{0, -1, 0}, {1, 0, 0}, {0, 0, -1}},
        {{0, 0, 1}, {-1, 0, 0}, {0, 1, 0}}, {{0, 0, -1}, {1, 0, 0}, {0, 1, 0}},
    };
    const float half[3] = {hw, hh, hd};
    for (const Face &f : faces) {
        const std::uint32_t base = std::uint32_t(m.vertexCount());
        for (int q = 0; q < 4; ++q) {
            const float s = (q & 1) ? 1.f : -1.f, t = (q & 2) ? 1.f : -1.f;
            float p[3];
            for (int a = 0; a < 3; ++a)
                p[a] = (f.n[a] + s * f.u[a] + t * f.v[a]) * half[a];
            addVertex(m, p, f.n, (s + 1.f) * 0.5f, (t + 1.f) * 0.5f);
        }
        m.indices.insert(m.indices.end(), {base, base + 1, base + 2, base + 2, base + 1, base + 3});
    }
    fixWinding(m);
    recharge(L, ud);
    return 1;
}

int geoSphere(lua_State *L)
{
    const float r = float(luaL_checknumber(L, 1));
    const int seg = optInt(L, 2, 32, 3, 4096), rings = optInt(L, 3, 16, 2, 4096);
    const std::size_t verts = std::size_t(seg + 1) * std::size_t(rings + 1);
    needVertices(L, verts);
    addWork(L, verts);
    MeshUD *ud = newMesh(L);
    ProceduralMesh &m = ud->mesh;
    for (int i = 0; i <= rings; ++i) {
        const float th = 3.14159265358979f * float(i) / float(rings);
        const float st = (i == 0 || i == rings) ? 0.f : std::sin(th), ct = i == rings ? -1.f : std::cos(th);
        for (int j = 0; j <= seg; ++j) {
            const float ph = 6.28318530717959f * float(j % seg) / float(seg);
            const float n[3] = {st * std::cos(ph), ct, st * std::sin(ph)};
            const float p[3] = {n[0] * r, n[1] * r, n[2] * r};
            addVertex(m, p, n, float(j) / float(seg), float(i) / float(rings));
        }
    }
    addGridIndices(m, rings, seg);
    fixWinding(m);
    recharge(L, ud);
    return 1;
}

int geoCylinder(lua_State *L)
{
    const float r = float(luaL_checknumber(L, 1)), h = float(luaL_checknumber(L, 2));
    const int seg = optInt(L, 3, 32, 3, 4096);
    const std::size_t verts = std::size_t(seg + 1) * 2 + 2 * std::size_t(seg + 1);
    needVertices(L, verts);
    addWork(L, verts);
    MeshUD *ud = newMesh(L);
    ProceduralMesh &m = ud->mesh;
    for (int i = 0; i < 2; ++i)
        for (int j = 0; j <= seg; ++j) {
            const float ph = 6.28318530717959f * float(j % seg) / float(seg);
            const float n[3] = {std::cos(ph), 0.f, std::sin(ph)};
            const float p[3] = {n[0] * r, (i ? 0.5f : -0.5f) * h, n[2] * r};
            addVertex(m, p, n, float(j) / float(seg), float(i));
        }
    addGridIndices(m, 1, seg);
    for (int cap = 0; cap < 2; ++cap) {
        const float y = (cap ? 0.5f : -0.5f) * h;
        const float n[3] = {0.f, cap ? 1.f : -1.f, 0.f};
        const std::uint32_t center = std::uint32_t(m.vertexCount());
        const float c[3] = {0.f, y, 0.f};
        addVertex(m, c, n, 0.5f, 0.5f);
        for (int j = 0; j < seg; ++j) {
            const float ph = 6.28318530717959f * float(j) / float(seg);
            const float p[3] = {std::cos(ph) * r, y, std::sin(ph) * r};
            addVertex(m, p, n, 0.5f + 0.5f * std::cos(ph), 0.5f + 0.5f * std::sin(ph));
        }
        for (int j = 0; j < seg; ++j)
            m.indices.insert(m.indices.end(), {center, center + 1 + std::uint32_t(j), center + 1 + std::uint32_t((j + 1) % seg)});
    }
    fixWinding(m);
    recharge(L, ud);
    return 1;
}

int geoTorus(lua_State *L)
{
    const float R = float(luaL_checknumber(L, 1)), r = float(luaL_checknumber(L, 2));
    const int seg = optInt(L, 3, 32, 3, 4096), tube = optInt(L, 4, 16, 3, 4096);
    const std::size_t verts = std::size_t(seg + 1) * std::size_t(tube + 1);
    needVertices(L, verts);
    addWork(L, verts);
    MeshUD *ud = newMesh(L);
    ProceduralMesh &m = ud->mesh;
    for (int i = 0; i <= tube; ++i) {
        const float phi = 6.28318530717959f * float(i % tube) / float(tube);
        const float cp = std::cos(phi), sp = std::sin(phi);
        for (int j = 0; j <= seg; ++j) {
            const float th = 6.28318530717959f * float(j % seg) / float(seg);
            const float ct = std::cos(th), st = std::sin(th);
            const float n[3] = {cp * ct, sp, cp * st};
            const float p[3] = {(R + r * cp) * ct, r * sp, (R + r * cp) * st};
            addVertex(m, p, n, float(j) / float(seg), float(i) / float(tube));
        }
    }
    addGridIndices(m, tube, seg);
    fixWinding(m);
    recharge(L, ud);
    return 1;
}

int geoLine(lua_State *L)
{
    const BufUD *buf = static_cast<BufUD *>(luaL_testudata(L, 1, kBufMT));
    std::size_t n;
    if (buf) {
        if (buf->buf.kind != BufferKind::Vec3)
            return luaL_error(L, "line: buffer must be vec3");
        n = buf->buf.count();
    } else {
        luaL_checktype(L, 1, LUA_TTABLE);
        n = lua_rawlen(L, 1);
    }
    if (n < 2)
        return luaL_error(L, "line needs at least two points");
    needVertices(L, n);
    addWork(L, n);
    MeshUD *ud = newMesh(L);
    ProceduralMesh &m = ud->mesh;
    m.topology = ProceduralMesh::Topology::Lines;
    m.position.resize(n * 3);
    if (buf) {
        std::memcpy(m.position.data(), buf->buf.data.data(), n * 3 * sizeof(float));
    } else {
        for (std::size_t i = 0; i < n; ++i) {
            lua_rawgeti(L, 1, lua_Integer(i + 1));
            if (!readVecN(L, -1, &m.position[i * 3], 3))
                return luaL_error(L, "line: point %d is not a {x, y, z} vector", int(i + 1));
            lua_pop(L, 1);
        }
    }
    for (std::size_t i = 0; i + 1 < n; ++i) {
        m.indices.push_back(std::uint32_t(i));
        m.indices.push_back(std::uint32_t(i + 1));
    }
    recharge(L, ud);
    return 1;
}

int geoPoints(lua_State *L)
{
    const BufUD *buf = static_cast<BufUD *>(luaL_testudata(L, 1, kBufMT));
    std::size_t n;
    if (buf) {
        if (buf->buf.kind != BufferKind::Vec3)
            return luaL_error(L, "points: buffer must be vec3");
        n = buf->buf.count();
    } else {
        const lua_Integer c = luaL_checkinteger(L, 1);
        if (c < 0)
            return luaL_error(L, "points: count must not be negative");
        n = std::size_t(c);
    }
    needVertices(L, n);
    addWork(L, n);
    MeshUD *ud = newMesh(L);
    ProceduralMesh &m = ud->mesh;
    m.topology = ProceduralMesh::Topology::Points;
    if (buf)
        m.position = buf->buf.data;
    else
        m.position.assign(n * 3, 0.f);
    recharge(L, ud);
    return 1;
}

int geoMerge(lua_State *L)
{
    const int argc = lua_gettop(L);
    if (argc < 1)
        return luaL_error(L, "merge needs at least one mesh");
    std::size_t total = 0;
    for (int i = 1; i <= argc; ++i) {
        const MeshUD *a = checkMesh(L, i);
        if (a->mesh.topology != checkMesh(L, 1)->mesh.topology)
            return luaL_error(L, "merge: all meshes must have the same topology");
        if (a->mesh.instanceCount() > 0)
            return luaL_error(L, "merge: meshes with instances cannot be merged");
        total += a->mesh.vertexCount();
    }
    needVertices(L, total);
    addWork(L, total);
    MeshUD *ud = newMesh(L);
    ProceduralMesh &m = ud->mesh;
    m.topology = checkMesh(L, 1)->mesh.topology;
    bool anyNormal = false, anyUv = false, anyColor = false;
    for (int i = 1; i <= argc; ++i) {
        const ProceduralMesh &s = checkMesh(L, i)->mesh;
        anyNormal |= !s.normal.empty();
        anyUv |= !s.uv.empty();
        anyColor |= !s.color.empty();
        for (const auto &c : s.custom) {
            auto it = std::find_if(m.custom.begin(), m.custom.end(), [&](const auto &x) { return x.name == c.name; });
            if (it == m.custom.end()) {
                if (int(m.custom.size()) >= kMaxCustomAttributes)
                    return luaL_error(L, "merge: more than %d custom attributes", kMaxCustomAttributes);
                m.custom.push_back({c.name, c.components, {}});
            } else if (it->components != c.components) {
                return luaL_error(L, "merge: attribute '%s' has different sizes", c.name.c_str());
            }
        }
    }
    for (int i = 1; i <= argc; ++i) {
        const ProceduralMesh &s = checkMesh(L, i)->mesh;
        const std::size_t n = s.vertexCount();
        const std::uint32_t base = std::uint32_t(m.vertexCount());
        m.position.insert(m.position.end(), s.position.begin(), s.position.end());
        if (anyNormal) {
            if (s.normal.empty()) m.normal.resize(m.normal.size() + n * 3, 0.f);
            else m.normal.insert(m.normal.end(), s.normal.begin(), s.normal.end());
        }
        if (anyUv) {
            if (s.uv.empty()) m.uv.resize(m.uv.size() + n * 2, 0.f);
            else m.uv.insert(m.uv.end(), s.uv.begin(), s.uv.end());
        }
        if (anyColor) {
            if (s.color.empty()) m.color.resize(m.color.size() + n * 4, 1.f);
            else m.color.insert(m.color.end(), s.color.begin(), s.color.end());
        }
        for (auto &mc : m.custom) {
            auto it = std::find_if(s.custom.begin(), s.custom.end(), [&](const auto &x) { return x.name == mc.name; });
            if (it == s.custom.end()) mc.data.resize(mc.data.size() + n * std::size_t(mc.components), 0.f);
            else mc.data.insert(mc.data.end(), it->data.begin(), it->data.end());
        }
        for (std::uint32_t idx : s.indices)
            m.indices.push_back(idx + base);
    }
    recharge(L, ud);
    return 1;
}

// --- mesh methods ----------------------------------------------------------------------------------------

struct PosKey
{
    std::uint32_t b[3];
    bool operator==(const PosKey &o) const { return b[0] == o.b[0] && b[1] == o.b[1] && b[2] == o.b[2]; }
};

struct PosKeyHash
{
    std::size_t operator()(const PosKey &k) const
    {
        std::uint64_t h = 1469598103934665603ull;
        for (std::uint32_t v : k.b)
            h = (h ^ v) * 1099511628211ull;
        return std::size_t(h);
    }
};

// Smooth normals, welding vertices that share a position (sphere/torus seams) so they shade alike.
void recomputeNormals(ProceduralMesh &m)
{
    const std::size_t n = m.vertexCount();
    std::vector<std::uint32_t> canon(n);
    std::unordered_map<PosKey, std::uint32_t, PosKeyHash> seen;
    seen.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        PosKey k;
        for (int a = 0; a < 3; ++a) {
            const float v = m.position[i * 3 + std::size_t(a)] + 0.f; // folds -0 into +0
            std::memcpy(&k.b[a], &v, sizeof(float));
        }
        canon[i] = seen.emplace(k, std::uint32_t(i)).first->second;
    }
    std::vector<float> acc(n * 3, 0.f);
    for (std::size_t t = 0; t + 2 < m.indices.size(); t += 3) {
        const std::uint32_t i0 = m.indices[t], i1 = m.indices[t + 1], i2 = m.indices[t + 2];
        const float *p0 = &m.position[i0 * 3], *p1 = &m.position[i1 * 3], *p2 = &m.position[i2 * 3];
        const float e1[3] = {p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]};
        const float e2[3] = {p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2]};
        const float f[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
        for (std::uint32_t v : {i0, i1, i2})
            for (int a = 0; a < 3; ++a)
                acc[canon[v] * 3 + std::size_t(a)] += f[a];
    }
    m.normal.assign(n * 3, 0.f);
    for (std::size_t i = 0; i < n; ++i) {
        const float *s = &acc[canon[i] * 3];
        const float l = std::sqrt(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]);
        if (l > 0.f)
            for (int a = 0; a < 3; ++a)
                m.normal[i * 3 + std::size_t(a)] = s[a] / l;
    }
}

int meshRecomputeNormals(lua_State *L)
{
    MeshUD *ud = checkMesh(L, 1);
    if (ud->mesh.topology != ProceduralMesh::Topology::Triangles)
        return luaL_error(L, "recomputeNormals needs a triangle mesh");
    addWork(L, ud->mesh.vertexCount());
    recomputeNormals(ud->mesh);
    recharge(L, ud);
    lua_settop(L, 1);
    return 1;
}

int meshTransform(lua_State *L)
{
    MeshUD *ud = checkMesh(L, 1);
    const float *m = checkMat(L, 2)->m;
    ProceduralMesh &mesh = ud->mesh;
    const std::size_t n = mesh.vertexCount();
    addWork(L, n);
    float nm[9]; // inverse transpose of the upper 3x3, row-major
    const float a = m[0], b = m[4], c = m[8], d = m[1], e = m[5], f = m[9], g = m[2], h = m[6], i = m[10];
    const float det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    const float id = det != 0.f ? 1.f / det : 0.f;
    nm[0] = (e * i - f * h) * id; nm[1] = (f * g - d * i) * id; nm[2] = (d * h - e * g) * id;
    nm[3] = (c * h - b * i) * id; nm[4] = (a * i - c * g) * id; nm[5] = (b * g - a * h) * id;
    nm[6] = (b * f - c * e) * id; nm[7] = (c * d - a * f) * id; nm[8] = (a * e - b * d) * id;
    for (std::size_t v = 0; v < n; ++v) {
        float *p = &mesh.position[v * 3];
        float o[3];
        matApply(m, p[0], p[1], p[2], o);
        std::memcpy(p, o, sizeof(o));
    }
    if (mesh.normal.size() == n * 3)
        for (std::size_t v = 0; v < n; ++v) {
            float *p = &mesh.normal[v * 3];
            float o[3] = {nm[0] * p[0] + nm[1] * p[1] + nm[2] * p[2], nm[3] * p[0] + nm[4] * p[1] + nm[5] * p[2],
                          nm[6] * p[0] + nm[7] * p[1] + nm[8] * p[2]};
            const float l = std::sqrt(o[0] * o[0] + o[1] * o[1] + o[2] * o[2]);
            for (int k = 0; k < 3; ++k)
                p[k] = l > 0.f ? o[k] / l : 0.f;
        }
    if (det < 0.f && mesh.topology == ProceduralMesh::Topology::Triangles)
        for (std::size_t t = 0; t + 2 < mesh.indices.size(); t += 3)
            std::swap(mesh.indices[t + 1], mesh.indices[t + 2]);
    lua_settop(L, 1);
    return 1;
}

int meshSet(lua_State *L)
{
    MeshUD *ud = checkMesh(L, 1);
    const char *name = luaL_checkstring(L, 2);
    const BufUD *b = checkBuf(L, 3);
    ProceduralMesh &m = ud->mesh;
    const bool isPos = !std::strcmp(name, "position");
    if (b->buf.count() != m.vertexCount() && !(isPos && m.vertexCount() == 0))
        return luaL_error(L, "set '%s': buffer has %d elements but the mesh has %d vertices", name, int(b->buf.count()), int(m.vertexCount()));
    auto wantKind = [&](BufferKind k) {
        if (b->buf.kind != k)
            luaL_error(L, "set '%s': expected a %s buffer, got %s", name, kindName(k), kindName(b->buf.kind));
    };
    if (isPos) {
        wantKind(BufferKind::Vec3);
        needVertices(L, b->buf.count());
        m.position = b->buf.data;
    } else if (!std::strcmp(name, "normal")) {
        wantKind(BufferKind::Vec3);
        m.normal = b->buf.data;
    } else if (!std::strcmp(name, "uv")) {
        wantKind(BufferKind::Vec2);
        m.uv = b->buf.data;
    } else if (!std::strcmp(name, "color")) {
        wantKind(BufferKind::Vec4);
        m.color = b->buf.data;
    } else {
        if (b->buf.kind == BufferKind::Mat4)
            return luaL_error(L, "set '%s': custom attributes are float, vec2, vec3 or vec4", name);
        bool ok = (name[0] == '_' || std::isalpha(static_cast<unsigned char>(name[0]))) && std::strlen(name) <= 32;
        for (const char *p = name; *p && ok; ++p)
            ok = *p == '_' || std::isalnum(static_cast<unsigned char>(*p));
        if (!ok)
            return luaL_error(L, "set: invalid attribute name '%s'", name);
        auto it = std::find_if(m.custom.begin(), m.custom.end(), [&](const auto &x) { return x.name == name; });
        if (it == m.custom.end()) {
            if (int(m.custom.size()) >= kMaxCustomAttributes)
                return luaL_error(L, "a mesh can have at most %d custom attributes", kMaxCustomAttributes);
            m.custom.push_back({name, bufferComponents(b->buf.kind), b->buf.data});
        } else {
            it->components = bufferComponents(b->buf.kind);
            it->data = b->buf.data;
        }
    }
    recharge(L, ud);
    lua_settop(L, 1);
    return 1;
}

int meshGet(lua_State *L)
{
    const ProceduralMesh &m = checkMesh(L, 1)->mesh;
    const char *name = luaL_checkstring(L, 2);
    const std::vector<float> *src = nullptr;
    BufferKind kind = BufferKind::Float;
    if (!std::strcmp(name, "position")) { src = &m.position; kind = BufferKind::Vec3; }
    else if (!std::strcmp(name, "normal")) { src = &m.normal; kind = BufferKind::Vec3; }
    else if (!std::strcmp(name, "uv")) { src = &m.uv; kind = BufferKind::Vec2; }
    else if (!std::strcmp(name, "color")) { src = &m.color; kind = BufferKind::Vec4; }
    else {
        for (const auto &c : m.custom)
            if (c.name == name) {
                src = &c.data;
                kind = c.components == 1 ? BufferKind::Float : c.components == 2 ? BufferKind::Vec2 : c.components == 3 ? BufferKind::Vec3 : BufferKind::Vec4;
            }
    }
    if (!src || src->empty()) {
        lua_pushnil(L);
        return 1;
    }
    BufUD *out = newBuf(L, kind, src->size() / std::size_t(bufferComponents(kind)));
    out->buf.data = *src;
    return 1;
}

int meshBounds(lua_State *L)
{
    std::array<float, 3> mn, mx;
    if (!checkMesh(L, 1)->mesh.bounds(mn, mx)) {
        lua_pushnil(L);
        return 1;
    }
    pushProceduralVec3(L, mn[0], mn[1], mn[2]);
    pushProceduralVec3(L, mx[0], mx[1], mx[2]);
    return 2;
}

int meshDisplace(lua_State *L)
{
    MeshUD *ud = checkMesh(L, 1);
    ProceduralMesh &m = ud->mesh;
    float freq = 1.f, off[3] = {0, 0, 0};
    int octaves = 1;
    enum { Perlin, Simplex } kind = Perlin;
    if (lua_istable(L, 2)) {
        freq = fieldNumber(L, 2, "freq", 1.f);
        octaves = int(std::clamp(fieldNumber(L, 2, "octaves", 1.f), 1.f, 8.f));
        lua_getfield(L, 2, "offset");
        if (!lua_isnil(L, -1))
            checkVec3(L, -1, off);
        lua_pop(L, 1);
        lua_getfield(L, 2, "kind");
        if (lua_isstring(L, -1) && !std::strcmp(lua_tostring(L, -1), "simplex"))
            kind = Simplex;
        lua_pop(L, 1);
    } else {
        freq = float(luaL_checknumber(L, 2));
    }
    const float amount = float(luaL_checknumber(L, 3));
    const std::size_t n = m.vertexCount();
    addWork(L, n * std::size_t(octaves));
    if (m.normal.size() != n * 3)
        recomputeNormals(m);
    for (std::size_t i = 0; i < n; ++i) {
        float *p = &m.position[i * 3];
        const float x = p[0] * freq + off[0], y = p[1] * freq + off[1], z = p[2] * freq + off[2];
        float v;
        if (kind == Simplex)
            v = proceduralSimplex(x, y, z);
        else
            v = octaves == 1 ? proceduralPerlin(x, y, z) : proceduralFbm(x, y, z, octaves, 2.f, 0.5f);
        for (int a = 0; a < 3; ++a)
            p[a] += m.normal[i * 3 + std::size_t(a)] * v * amount;
    }
    recharge(L, ud);
    lua_settop(L, 1);
    return 1;
}

int meshInstances(lua_State *L)
{
    MeshUD *ud = checkMesh(L, 1);
    if (lua_isnoneornil(L, 2)) {
        lua_pushinteger(L, lua_Integer(ud->mesh.instanceCount()));
        return 1;
    }
    const BufUD *b = checkBuf(L, 2);
    if (b->buf.kind != BufferKind::Mat4 && b->buf.kind != BufferKind::Vec4)
        return luaL_error(L, "instances: expected a mat4 buffer or a vec4 buffer of (x, y, z, scale)");
    const std::size_t n = b->buf.count();
    needInstances(L, n);
    addWork(L, n);
    ProceduralMesh &m = ud->mesh;
    m.instances.resize(n * 16);
    if (b->buf.kind == BufferKind::Mat4) {
        std::memcpy(m.instances.data(), b->buf.data.data(), n * 16 * sizeof(float));
    } else {
        for (std::size_t i = 0; i < n; ++i) {
            float *o = &m.instances[i * 16];
            const float *s = &b->buf.data[i * 4];
            matIdentity(o);
            o[0] = o[5] = o[10] = s[3];
            o[12] = s[0];
            o[13] = s[1];
            o[14] = s[2];
        }
    }
    recharge(L, ud);
    lua_settop(L, 1);
    return 1;
}

int meshCopy(lua_State *L)
{
    const MeshUD *src = checkMesh(L, 1);
    needVertices(L, src->mesh.vertexCount());
    addWork(L, src->mesh.vertexCount());
    MeshUD *ud = newMesh(L);
    ud->mesh = src->mesh;
    recharge(L, ud);
    return 1;
}

int meshCount(lua_State *L)
{
    const ProceduralMesh &m = checkMesh(L, 1)->mesh;
    lua_pushinteger(L, lua_Integer(m.vertexCount()));
    lua_pushinteger(L, lua_Integer(m.topology == ProceduralMesh::Topology::Triangles ? m.indices.size() / 3 : m.indices.size() / 2));
    return 2;
}

int meshToString(lua_State *L)
{
    lua_pushfstring(L, "mesh[%I vertices]", lua_Integer(checkMesh(L, 1)->mesh.vertexCount()));
    return 1;
}

void setFuncs(lua_State *L, const luaL_Reg *funcs)
{
    luaL_setfuncs(L, funcs, 0);
}

// Locks the metatable so scripts cannot read or replace it.
void lockMetatable(lua_State *L)
{
    lua_pushboolean(L, 0);
    lua_setfield(L, -2, "__metatable");
}

} // namespace

ProceduralMesh *toProceduralMesh(lua_State *L, int idx)
{
    auto *ud = static_cast<MeshUD *>(luaL_testudata(L, idx, kMeshMT));
    return ud ? &ud->mesh : nullptr;
}

bool toProceduralMat(lua_State *L, int idx, float out[16])
{
    auto *ud = static_cast<MatUD *>(luaL_testudata(L, idx, kMatMT));
    if (!ud)
        return false;
    std::memcpy(out, ud->m, 16 * sizeof(float));
    return true;
}

void pushProceduralMat(lua_State *L, const float m[16])
{
    std::memcpy(newMat(L), m, 16 * sizeof(float));
}

bool readProceduralVec3(lua_State *L, int idx, float out[3])
{
    return readVecN(L, idx, out, 3);
}

void pushProceduralVec3(lua_State *L, float x, float y, float z)
{
    lua_createtable(L, 3, 3);
    const float v[3] = {x, y, z};
    static const char *const names[3] = {"x", "y", "z"};
    for (int i = 0; i < 3; ++i) {
        lua_pushnumber(L, v[i]);
        lua_rawseti(L, -2, i + 1);
        lua_pushnumber(L, v[i]);
        lua_setfield(L, -2, names[i]);
    }
}

void checkProceduralVertices(lua_State *L, std::size_t count)
{
    needVertices(L, count);
}

void registerProceduralGeometry(lua_State *L, GeometryLimits *limits)
{
    auto *state = static_cast<GeoState *>(lua_newuserdatauv(L, sizeof(GeoState), 0));
    *state = GeoState{limits, 0, 0};
    lua_rawsetp(L, LUA_REGISTRYINDEX, &kStateKey);

    // mesh
    static const luaL_Reg meshMethods[] = {
        {"transform", meshTransform}, {"set", meshSet}, {"get", meshGet}, {"displace", meshDisplace},
        {"recomputeNormals", meshRecomputeNormals}, {"bounds", meshBounds}, {"instances", meshInstances},
        {"copy", meshCopy}, {"count", meshCount}, {nullptr, nullptr}};
    luaL_newmetatable(L, kMeshMT);
    lua_newtable(L);
    setFuncs(L, meshMethods);
    lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, meshGc);
    lua_setfield(L, -2, "__gc");
    lua_pushcfunction(L, meshToString);
    lua_setfield(L, -2, "__tostring");
    lockMetatable(L);
    lua_pop(L, 1);

    // buffer
    static const luaL_Reg bufMethods[] = {
        {"fill", bufFill}, {"map", bufMap}, {"add", bufAdd}, {"mul", bufMul}, {"lerp", bufLerp},
        {"noise", noiseField}, {"slice", bufSlice}, {"kind", bufGetKind}, {nullptr, nullptr}};
    luaL_newmetatable(L, kBufMT);
    lua_newtable(L);
    setFuncs(L, bufMethods);
    lua_pushcclosure(L, bufIndex, 1);
    lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, bufNewIndex);
    lua_setfield(L, -2, "__newindex");
    lua_pushcfunction(L, bufLen);
    lua_setfield(L, -2, "__len");
    lua_pushcfunction(L, bufGc);
    lua_setfield(L, -2, "__gc");
    lua_pushcfunction(L, bufToString);
    lua_setfield(L, -2, "__tostring");
    lockMetatable(L);
    lua_pop(L, 1);

    // mat
    static const luaL_Reg matMethods[] = {{"apply", matApplyFn}, {"inverse", matInverseFn}, {"toTable", matToTable}, {nullptr, nullptr}};
    luaL_newmetatable(L, kMatMT);
    lua_newtable(L);
    setFuncs(L, matMethods);
    lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, matMulMeta);
    lua_setfield(L, -2, "__mul");
    lua_pushcfunction(L, matToString);
    lua_setfield(L, -2, "__tostring");
    lockMetatable(L);
    lua_pop(L, 1);

    // rand
    static const luaL_Reg randMethods[] = {
        {"float", randFloat}, {"range", randRange}, {"int", randInt}, {"normal", randNormal}, {"unit", randUnit}, {nullptr, nullptr}};
    luaL_newmetatable(L, kRandMT);
    lua_newtable(L);
    setFuncs(L, randMethods);
    lua_setfield(L, -2, "__index");
    lockMetatable(L);
    lua_pop(L, 1);

    static const luaL_Reg geoFuncs[] = {
        {"grid", geoGrid}, {"box", geoBox}, {"sphere", geoSphere}, {"cylinder", geoCylinder}, {"torus", geoTorus},
        {"line", geoLine}, {"points", geoPoints}, {"merge", geoMerge}, {nullptr, nullptr}};
    luaL_newlib(L, geoFuncs);
    lua_setglobal(L, "geo");

    static const luaL_Reg bufFuncs[] = {{"new", bufNew}, {"from", bufFrom}, {nullptr, nullptr}};
    luaL_newlib(L, bufFuncs);
    lua_setglobal(L, "buf");

    static const luaL_Reg matFuncs[] = {
        {"identity", matIdentityFn}, {"translate", matTranslate}, {"rotate", matRotate}, {"scale", matScale},
        {"compose", matCompose}, {"lookAt", matLookAt}, {"perspective", matPerspective}, {nullptr, nullptr}};
    luaL_newlib(L, matFuncs);
    lua_setglobal(L, "mat");

    static const luaL_Reg noiseFuncs[] = {
        {"perlin", noisePerlin}, {"simplex", noiseSimplex}, {"fbm", noiseFbm}, {"field", noiseField}, {nullptr, nullptr}};
    luaL_newlib(L, noiseFuncs);
    lua_setglobal(L, "noise");

    static const luaL_Reg randFuncs[] = {{"new", randNew}, {nullptr, nullptr}};
    luaL_newlib(L, randFuncs);
    lua_setglobal(L, "rand");
}

} // namespace drift
