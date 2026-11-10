#pragma once

#include "engine/procedural/LuaInclude.h"

#include <chrono>
#include <cstddef>
#include <deque>
#include <string>
#include <vector>

namespace drift {

// A Lua 5.4 state that can only compute: base, table, string, math and utf8, with the escape hatches
// (files, bytecode, os, debug, the clock, GC control) removed, a hard memory cap and a wall-clock
// deadline enforced by a count hook. One sandbox is one evaluation; nothing is reused.
class LuaSandbox
{
public:
    struct Limits
    {
        std::size_t memoryBytes = 64u * 1024u * 1024u;
        std::chrono::milliseconds deadline{250};
        std::size_t maxLogLines = 200;
    };

    explicit LuaSandbox(const Limits &limits);
    ~LuaSandbox();
    LuaSandbox(const LuaSandbox &) = delete;
    LuaSandbox &operator=(const LuaSandbox &) = delete;

    lua_State *state() const { return m_L; }
    static LuaSandbox *from(lua_State *L);

    // Compiles and runs a text chunk. On failure returns false and fills `error`.
    bool run(const std::string &source, const char *chunkName, std::string *error);

    // Marks the evaluation as dead. pcall/xpcall rethrow once this is set, so a script cannot swallow it.
    void abort(const std::string &reason);
    bool aborted() const { return m_aborted; }
    const std::string &abortReason() const { return m_abortReason; }

    void appendLog(const std::string &line);
    std::vector<std::string> takeLog();

    bool deadlineExceeded() const { return m_deadlineHit; }
    bool memoryExceeded() const { return m_alloc.exceeded; }
    std::size_t memoryUsed() const { return m_alloc.used; }

private:
    struct Alloc
    {
        std::size_t used = 0;
        std::size_t cap = 0;
        bool exceeded = false;
    };
    static void *allocate(void *ud, void *ptr, std::size_t osize, std::size_t nsize);
    static void hook(lua_State *L, lua_Debug *ar);

    lua_State *m_L = nullptr;
    Alloc m_alloc;
    Limits m_limits;
    std::chrono::steady_clock::time_point m_deadline;
    bool m_aborted = false;
    bool m_deadlineHit = false;
    std::string m_abortReason;
    std::deque<std::string> m_log;
    std::size_t m_droppedLog = 0;
};

} // namespace drift
