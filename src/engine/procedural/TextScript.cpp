#include "engine/procedural/TextScript.h"

#include "engine/procedural/LuaSandbox.h"

#include <algorithm>
#include <cmath>

namespace drift {

namespace {

constexpr const char *kHelpers = R"LUA(
function lerp(a, b, t) return a + (b - a) * t end
function clamp(x, lo, hi) return math.max(lo, math.min(hi, x)) end
function pad(n, width) return string.format("%0" .. (width or 2) .. "d", math.floor(n)) end
function timecode(seconds, fps)
  fps = math.max(1, math.floor((fps or ctx.fps) + 0.5))
  local frames = math.floor(math.max(0, seconds) * fps + 1e-6)
  local f = frames % fps
  local s = (frames // fps) % 60
  local m = (frames // (fps * 60)) % 60
  local h = frames // (fps * 3600)
  return string.format("%02d:%02d:%02d:%02d", h, m, s, f)
end
function commas(n)
  local s = string.format("%d", math.floor(n + 0.5))
  local sign = ""
  if s:sub(1, 1) == "-" then sign = "-"; s = s:sub(2) end
  local out = s:reverse():gsub("(%d%d%d)", "%1,"):reverse()
  if out:sub(1, 1) == "," then out = out:sub(2) end
  return sign .. out
end
local easings = {
  linear = function(t) return t end,
  inQuad = function(t) return t * t end,
  outQuad = function(t) return 1 - (1 - t) * (1 - t) end,
  inOutQuad = function(t) if t < 0.5 then return 2 * t * t end return 1 - (-2 * t + 2) ^ 2 / 2 end,
  inCubic = function(t) return t * t * t end,
  outCubic = function(t) return 1 - (1 - t) ^ 3 end,
  inOutCubic = function(t) if t < 0.5 then return 4 * t * t * t end return 1 - (-2 * t + 2) ^ 3 / 2 end,
  outExpo = function(t) if t >= 1 then return 1 end return 1 - 2 ^ (-10 * t) end,
  inOutSine = function(t) return -(math.cos(math.pi * t) - 1) / 2 end,
}
function ease(name, t)
  local f = easings[name] or easings.linear
  return f(clamp(t, 0, 1))
end
)LUA";

void setNumber(lua_State *L, const char *key, double v)
{
    lua_pushnumber(L, v);
    lua_setfield(L, -2, key);
}

void setInteger(lua_State *L, const char *key, std::int64_t v)
{
    lua_pushinteger(L, v);
    lua_setfield(L, -2, key);
}

void setString(lua_State *L, const char *key, const std::string &v)
{
    lua_pushlstring(L, v.data(), v.size());
    lua_setfield(L, -2, key);
}

void pushContext(lua_State *L, const TextScriptContext &c)
{
    lua_newtable(L);
    const double fps = c.fps > 0.0 ? c.fps : 30.0;
    setNumber(L, "time", c.clipTime);
    setNumber(L, "timeline", c.timelineTime);
    setNumber(L, "duration", c.duration);
    setNumber(L, "progress", c.duration > 0.0 ? std::clamp(c.clipTime / c.duration, 0.0, 1.0) : 0.0);
    setNumber(L, "fps", fps);
    setInteger(L, "frame", std::int64_t(std::floor(c.clipTime * fps + 1e-6)));
    setInteger(L, "timelineFrame", std::int64_t(std::floor(c.timelineTime * fps + 1e-6)));
    setInteger(L, "width", c.width);
    setInteger(L, "height", c.height);
    setString(L, "project", c.projectName);
    setString(L, "clip", c.clipName);
    if (c.hasSource)
        setNumber(L, "source", c.sourceTime);
    lua_newtable(L);
    setInteger(L, "year", c.now.year);
    setInteger(L, "month", c.now.month);
    setInteger(L, "day", c.now.day);
    setInteger(L, "hour", c.now.hour);
    setInteger(L, "min", c.now.minute);
    setInteger(L, "sec", c.now.second);
    setInteger(L, "wday", c.now.weekday);
    lua_setfield(L, -2, "now");
    lua_setglobal(L, "ctx");
}

} // namespace

TextScriptResult evaluateTextScript(const std::string &script, const TextScriptContext &context)
{
    TextScriptResult result;
    LuaSandbox::Limits limits;
    limits.memoryBytes = 8u * 1024u * 1024u;
    limits.deadline = std::chrono::milliseconds(60);
    limits.maxLogLines = 0;
    LuaSandbox sandbox(limits);
    lua_State *L = sandbox.state();

    pushContext(L, context);
    std::string error;
    if (!sandbox.run(kHelpers, "=helpers", &error)) {
        result.error = error;
        return result;
    }
    // The prefix stays on one line so the user's line numbers are the script's own.
    const std::string wrapped = "__result = (function() " + script + "\nend)()";
    if (!sandbox.run(wrapped, "=script", &error)) {
        result.error = error;
        const std::size_t trace = result.error.find("\nstack traceback");
        if (trace != std::string::npos)
            result.error.resize(trace);
        return result;
    }
    lua_getglobal(L, "__result");
    if (lua_type(L, -1) == LUA_TSTRING || lua_type(L, -1) == LUA_TNUMBER) {
        std::size_t len = 0;
        const char *s = luaL_tolstring(L, -1, &len);
        result.text.assign(s, len);
        lua_pop(L, 1);
    } else if (lua_istable(L, -1)) {
        const lua_Integer n = luaL_len(L, -1);
        for (lua_Integer i = 1; i <= n; ++i) {
            lua_geti(L, -1, i);
            std::size_t len = 0;
            const char *s = luaL_tolstring(L, -1, &len);
            if (i > 1)
                result.text += '\n';
            result.text.append(s, len);
            lua_pop(L, 2);
        }
    } else if (!lua_isnil(L, -1)) {
        result.error = std::string("the script must return a string, a number or a list of strings, not a ") + luaL_typename(L, -1);
    }
    lua_pop(L, 1);
    return result;
}

} // namespace drift
