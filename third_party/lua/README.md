# Lua

Lua 5.4.9 interpreter core, used by Drift's procedural 3D clips.

- Upstream: https://www.lua.org/ftp/lua-5.4.9.tar.gz
- SHA-256: `2335b6c582a52654f94612bf10d2f4672805d05329aa6568b1d8cd9e5c6fb8e6`
- License: MIT (see `LICENSE`)

Unmodified `src/` from the tarball, minus `lua.c`, `luac.c`, `loadlib.c` and the libraries Drift
never opens (`linit.c`, `liolib.c`, `loslib.c`, `ldblib.c`); the sandbox opens base, table, string,
math and utf8 itself. Built as a static library by the root CMakeLists.txt; on Emscripten it is
compiled as C++ so errors unwind with C++ exceptions, matching Qt for wasm's `-fexceptions`.
Flatpak builds use the system Lua instead (`DRIFT_SYSTEM_LUA=ON`).
