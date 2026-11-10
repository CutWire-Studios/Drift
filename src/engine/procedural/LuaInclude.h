#pragma once

// The one place that knows how Lua was built. The vendored copy is compiled as C++ on wasm (so
// lua_error unwinds with a C++ exception, which is what Qt for wasm's -fexceptions understands) and
// then carries C++ linkage; everywhere else, and for the system Lua, it is plain C.
#ifdef DRIFT_LUA_AS_CXX
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
#else
extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}
#endif
