#include "engine/procedural/LuaSandbox.h"

#include <cstdlib>
#include <cstring>

namespace drift {

namespace {

constexpr int kHookInterval = 1000;
constexpr std::size_t kMaxLogLineChars = 1024;

void rethrowIfDead(lua_State *L)
{
    LuaSandbox *sb = LuaSandbox::from(L);
    if (!sb)
        return;
    if (sb->aborted())
        luaL_error(L, "%s", sb->abortReason().c_str());
    if (sb->memoryExceeded())
        luaL_error(L, "script exceeded the memory limit");
}

// pcall/xpcall must not be able to swallow the deadline or a budget error: without this a script
// could loop forever on `while true do pcall(spin) end`.
int guardedCall(lua_State *L)
{
    const int n = lua_gettop(L);
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_insert(L, 1);
    lua_call(L, n, LUA_MULTRET);
    rethrowIfDead(L);
    return lua_gettop(L);
}

int restrictedLoad(lua_State *L)
{
    const int n = lua_gettop(L);
    if (lua_type(L, 1) == LUA_TSTRING) {
        std::size_t len = 0;
        const char *s = lua_tolstring(L, 1, &len);
        if (len > 0 && s[0] == '\x1b') {
            lua_pushnil(L);
            lua_pushliteral(L, "binary chunks are not allowed");
            return 2;
        }
    }
    lua_settop(L, 4);
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_insert(L, 1); // chunk=2 name=3 mode=4 env=5
    lua_pushliteral(L, "t");
    lua_replace(L, 4);
    lua_settop(L, n >= 4 ? 5 : 4); // drop the env slot unless the caller passed one
    lua_call(L, lua_gettop(L) - 1, LUA_MULTRET);
    return lua_gettop(L);
}

int restrictedSetmetatable(lua_State *L)
{
    if (lua_istable(L, 2)) {
        lua_pushliteral(L, "__gc");
        lua_rawget(L, 2);
        const bool hasGc = !lua_isnil(L, -1);
        lua_pop(L, 1);
        if (hasGc)
            return luaL_error(L, "__gc metamethods are not allowed");
    }
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_insert(L, 1);
    lua_call(L, lua_gettop(L) - 1, LUA_MULTRET);
    return lua_gettop(L);
}

int sandboxPrint(lua_State *L)
{
    const int n = lua_gettop(L);
    std::string line;
    for (int i = 1; i <= n; ++i) {
        std::size_t len = 0;
        const char *s = luaL_tolstring(L, i, &len);
        if (i > 1)
            line += '\t';
        line.append(s, len);
        lua_pop(L, 1);
        if (line.size() > kMaxLogLineChars)
            break;
    }
    if (LuaSandbox *sb = LuaSandbox::from(L))
        sb->appendLog(line);
    return 0;
}

int messageHandler(lua_State *L)
{
    const char *msg = lua_tostring(L, 1);
    if (!msg) {
        if (luaL_callmeta(L, 1, "__tostring") && lua_type(L, -1) == LUA_TSTRING)
            return 1;
        msg = lua_pushfstring(L, "(error object is a %s value)", luaL_typename(L, 1));
    }
    luaL_traceback(L, L, msg, 1);
    return 1;
}

void wrapGlobal(lua_State *L, const char *name, lua_CFunction wrapper)
{
    lua_getglobal(L, name);
    lua_pushcclosure(L, wrapper, 1);
    lua_setglobal(L, name);
}

void removeField(lua_State *L, const char *table, const char *field)
{
    lua_getglobal(L, table);
    lua_pushnil(L);
    lua_setfield(L, -2, field);
    lua_pop(L, 1);
}

void removeGlobal(lua_State *L, const char *name)
{
    lua_pushnil(L);
    lua_setglobal(L, name);
}

} // namespace

LuaSandbox *LuaSandbox::from(lua_State *L)
{
    return *static_cast<LuaSandbox **>(lua_getextraspace(L));
}

void *LuaSandbox::allocate(void *ud, void *ptr, std::size_t osize, std::size_t nsize)
{
    auto *a = static_cast<Alloc *>(ud);
    const std::size_t old = ptr ? osize : 0;
    if (nsize == 0) {
        a->used -= old;
        std::free(ptr);
        return nullptr;
    }
    if (nsize > old && a->used + (nsize - old) > a->cap) {
        a->exceeded = true;
        return nullptr;
    }
    void *p = std::realloc(ptr, nsize);
    if (!p)
        return nullptr;
    a->used = a->used - old + nsize;
    return p;
}

void LuaSandbox::hook(lua_State *L, lua_Debug *)
{
    LuaSandbox *sb = from(L);
    if (!sb)
        return;
    if (!sb->m_aborted && std::chrono::steady_clock::now() > sb->m_deadline) {
        sb->m_deadlineHit = true;
        sb->abort("script exceeded the time limit");
    }
    if (sb->m_aborted)
        luaL_error(L, "%s", sb->m_abortReason.c_str());
}

LuaSandbox::LuaSandbox(const Limits &limits)
    : m_limits(limits)
{
    m_alloc.cap = limits.memoryBytes;
    m_L = lua_newstate(&LuaSandbox::allocate, &m_alloc);
    *static_cast<LuaSandbox **>(lua_getextraspace(m_L)) = this;

    lua_State *L = m_L;
    luaL_requiref(L, "_G", luaopen_base, 1);
    luaL_requiref(L, LUA_TABLIBNAME, luaopen_table, 1);
    luaL_requiref(L, LUA_STRLIBNAME, luaopen_string, 1);
    luaL_requiref(L, LUA_MATHLIBNAME, luaopen_math, 1);
    luaL_requiref(L, LUA_UTF8LIBNAME, luaopen_utf8, 1);
    lua_pop(L, 5);

    removeGlobal(L, "dofile");
    removeGlobal(L, "loadfile");
    removeGlobal(L, "collectgarbage");
    removeGlobal(L, "warn");
    removeField(L, "string", "dump");
    // The generator seeds from the clock, which would make evaluation nondeterministic; scripts use rand.new(seed).
    removeField(L, "math", "random");
    removeField(L, "math", "randomseed");

    wrapGlobal(L, "pcall", guardedCall);
    wrapGlobal(L, "xpcall", guardedCall);
    wrapGlobal(L, "load", restrictedLoad);
    wrapGlobal(L, "setmetatable", restrictedSetmetatable);
    lua_pushcfunction(L, sandboxPrint);
    lua_setglobal(L, "print");

    lua_sethook(L, &LuaSandbox::hook, LUA_MASKCOUNT, kHookInterval);
    m_deadline = std::chrono::steady_clock::now() + m_limits.deadline;
}

LuaSandbox::~LuaSandbox()
{
    if (m_L) {
        lua_sethook(m_L, nullptr, 0, 0);
        lua_close(m_L);
    }
}

bool LuaSandbox::run(const std::string &source, const char *chunkName, std::string *error)
{
    lua_State *L = m_L;
    lua_settop(L, 0);
    lua_pushcfunction(L, messageHandler);
    int status = luaL_loadbufferx(L, source.data(), source.size(), chunkName, "t");
    if (status == LUA_OK)
        status = lua_pcall(L, 0, 0, 1);
    if (status == LUA_OK && !m_aborted && !m_alloc.exceeded)
        return true;

    if (error) {
        if (m_aborted)
            *error = m_abortReason;
        else if (m_alloc.exceeded)
            *error = "script exceeded the memory limit";
        else
            *error = lua_tostring(L, -1) ? lua_tostring(L, -1) : "unknown error";
    }
    return false;
}

void LuaSandbox::abort(const std::string &reason)
{
    if (m_aborted)
        return;
    m_aborted = true;
    m_abortReason = reason;
}

void LuaSandbox::appendLog(const std::string &line)
{
    if (m_limits.maxLogLines == 0)
        return;
    if (m_log.size() >= m_limits.maxLogLines) {
        m_log.pop_front();
        ++m_droppedLog;
    }
    m_log.push_back(line.size() > kMaxLogLineChars ? line.substr(0, kMaxLogLineChars) : line);
}

std::vector<std::string> LuaSandbox::takeLog()
{
    std::vector<std::string> out;
    if (m_droppedLog)
        out.push_back("... " + std::to_string(m_droppedLog) + " earlier log lines dropped");
    out.insert(out.end(), m_log.begin(), m_log.end());
    m_log.clear();
    m_droppedLog = 0;
    return out;
}

} // namespace drift
