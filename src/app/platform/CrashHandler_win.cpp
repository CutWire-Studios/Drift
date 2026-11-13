// Windows crash capture. Everything that touches Win32 for crashes lives in this file.
//
// The exception filter runs on the faulting thread, which after a stack overflow has almost no
// stack and after heap corruption cannot allocate. So it does nothing but hand the exception to a
// thread created at startup and wait; that thread has its own stack and its paths are already
// built, and it writes the dumps and crash.json with Win32 calls and stack buffers only.
// Looking up the module's version needs the heap, so it comes last, after the dumps are safe.

#include "app/platform/CrashHandler.h"

#include <QDir>
#include <QUuid>

#include <windows.h>
#include <dbghelp.h>

#include <cstdio>
#include <cstring>
#include <cwchar>

namespace drift::crash {

namespace {

wchar_t g_dir[MAX_PATH];
wchar_t g_uploadDump[MAX_PATH];
wchar_t g_fullDump[MAX_PATH];
wchar_t g_json[MAX_PATH];

LPTOP_LEVEL_EXCEPTION_FILTER g_previousFilter = nullptr;
HANDLE g_requested = nullptr;
HANDLE g_finished = nullptr;
volatile LONG g_crashing = 0;
EXCEPTION_POINTERS *g_info = nullptr;
DWORD g_faultingThread = 0;
ULONGLONG g_startTick = 0;

constexpr DWORD kFilterWaitMs = 60 * 1000;

void copyPath(wchar_t *destination, const QString &path)
{
    const QString native = QDir::toNativeSeparators(path);
    const qsizetype count = qMin<qsizetype>(native.size(), MAX_PATH - 1);
    native.left(count).toWCharArray(destination);
    destination[count] = L'\0';
}

bool writeDump(const wchar_t *path, MINIDUMP_TYPE type)
{
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    MINIDUMP_EXCEPTION_INFORMATION exception;
    exception.ThreadId = g_faultingThread;
    exception.ExceptionPointers = g_info;
    exception.ClientPointers = FALSE;
    const BOOL ok = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, type, &exception,
                                      nullptr, nullptr);
    CloseHandle(file);
    return ok != FALSE;
}

// Escapes into `out` for a JSON string; stops when full.
void appendJsonString(char *out, size_t capacity, const char *text)
{
    size_t used = strlen(out);
    for (; *text && used + 7 < capacity; ++text) {
        const unsigned char c = static_cast<unsigned char>(*text);
        if (c == '"' || c == '\\') {
            out[used++] = '\\';
            out[used++] = char(c);
        } else if (c < 0x20) {
            used += size_t(snprintf(out + used, capacity - used, "\\u%04x", c));
        } else {
            out[used++] = char(c);
        }
    }
    out[used] = '\0';
}

struct ModuleInfo
{
    char name[MAX_PATH * 3];
    char path[MAX_PATH * 3];
    wchar_t widePath[MAX_PATH];
    unsigned long long offset;
};

void describeModule(const EXCEPTION_RECORD *record, ModuleInfo *module)
{
    module->name[0] = module->path[0] = '\0';
    module->widePath[0] = L'\0';
    module->offset = 0;
    HMODULE handle = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            static_cast<LPCWSTR>(record->ExceptionAddress), &handle))
        return;
    GetModuleFileNameW(handle, module->widePath, MAX_PATH);
    WideCharToMultiByte(CP_UTF8, 0, module->widePath, -1, module->path, int(sizeof(module->path)), nullptr, nullptr);
    const wchar_t *base = wcsrchr(module->widePath, L'\\');
    WideCharToMultiByte(CP_UTF8, 0, base ? base + 1 : module->widePath, -1, module->name, int(sizeof(module->name)),
                        nullptr, nullptr);
    module->offset = static_cast<unsigned long long>(static_cast<const char *>(record->ExceptionAddress)
                                                    - reinterpret_cast<const char *>(handle));
}

void writeCrashJson(const ModuleInfo &module, const char *version)
{
    SYSTEMTIME now;
    GetSystemTime(&now);

    char json[4096];
    char name[MAX_PATH * 4] = "";
    char path[MAX_PATH * 4] = "";
    appendJsonString(name, sizeof(name), module.name);
    appendJsonString(path, sizeof(path), module.path);
    snprintf(json, sizeof(json),
             "{\"code\":\"0x%08lX\",\"module\":\"%s\",\"modulePath\":\"%s\",\"moduleVersion\":\"%s\","
             "\"offset\":\"0x%llX\",\"crashedAt\":\"%04d-%02d-%02dT%02d:%02d:%02dZ\",\"uptimeSeconds\":%llu,"
             "\"threadId\":%lu}",
             g_info->ExceptionRecord->ExceptionCode, name, path, version, module.offset, now.wYear, now.wMonth,
             now.wDay, now.wHour, now.wMinute, now.wSecond,
             (GetTickCount64() - g_startTick) / 1000ULL, g_faultingThread);

    HANDLE file = CreateFileW(g_json, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return;
    DWORD written = 0;
    WriteFile(file, json, DWORD(strlen(json)), &written, nullptr);
    CloseHandle(file);
}

// VS_FIXEDFILEINFO, "14.29.30133.0". Allocates, so only after the dumps exist.
void moduleVersion(const wchar_t *path, char *out, size_t capacity)
{
    out[0] = '\0';
    if (!path[0])
        return;
    const DWORD size = GetFileVersionInfoSizeW(path, nullptr);
    if (!size)
        return;
    BYTE *block = static_cast<BYTE *>(HeapAlloc(GetProcessHeap(), 0, size));
    if (!block)
        return;
    VS_FIXEDFILEINFO *fixed = nullptr;
    UINT length = 0;
    if (GetFileVersionInfoW(path, 0, size, block) && VerQueryValueW(block, L"\\", reinterpret_cast<LPVOID *>(&fixed), &length)
        && fixed) {
        snprintf(out, capacity, "%u.%u.%u.%u", HIWORD(fixed->dwFileVersionMS), LOWORD(fixed->dwFileVersionMS),
                 HIWORD(fixed->dwFileVersionLS), LOWORD(fixed->dwFileVersionLS));
    }
    HeapFree(GetProcessHeap(), 0, block);
}

DWORD WINAPI dumpWriterMain(LPVOID)
{
    WaitForSingleObject(g_requested, INFINITE);

    // The existing filter's one-line log entry; it does not touch the faulting thread's stack.
    if (g_previousFilter)
        g_previousFilter(g_info);

    CreateDirectoryW(g_dir, nullptr);

    ModuleInfo module;
    describeModule(g_info->ExceptionRecord, &module);
    writeCrashJson(module, "");

    writeDump(g_uploadDump, MiniDumpNormal);
    // Never full memory: a project with a few clips decoded can hold gigabytes.
    writeDump(g_fullDump, static_cast<MINIDUMP_TYPE>(MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithThreadInfo
                                                    | MiniDumpWithUnloadedModules | MiniDumpWithHandleData));

    char version[64];
    moduleVersion(module.widePath, version, sizeof(version));
    if (version[0])
        writeCrashJson(module, version);

    SetEvent(g_finished);
    return 0;
}

LONG WINAPI crashFilter(EXCEPTION_POINTERS *info)
{
    if (InterlockedCompareExchange(&g_crashing, 1, 0) == 0) {
        g_info = info;
        g_faultingThread = GetCurrentThreadId();
        SetEvent(g_requested);
    }
    // Other threads that fault meanwhile wait too, so the process is not torn down under the writer.
    WaitForSingleObject(g_finished, kFilterWaitMs);
    return EXCEPTION_CONTINUE_SEARCH;
}

} // namespace

void installCrashHandler(const QString &crashesDir)
{
    QDir().mkpath(crashesDir);
    const QString dir = crashesDir + QLatin1Char('/') + QUuid::createUuid().toString(QUuid::WithoutBraces);
    copyPath(g_dir, dir);
    copyPath(g_uploadDump, dir + QStringLiteral("/upload.dmp"));
    copyPath(g_fullDump, dir + QStringLiteral("/full.dmp"));
    copyPath(g_json, dir + QStringLiteral("/crash.json"));

    g_startTick = GetTickCount64();
    g_requested = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_finished = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE thread = CreateThread(nullptr, 0, dumpWriterMain, nullptr, 0, nullptr);
    if (thread)
        CloseHandle(thread);
    g_previousFilter = SetUnhandledExceptionFilter(crashFilter);
}

bool processAlive(qint64 pid)
{
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, DWORD(pid));
    if (!process)
        return false;
    DWORD code = 0;
    const bool alive = GetExitCodeProcess(process, &code) && code == STILL_ACTIVE;
    CloseHandle(process);
    return alive;
}

} // namespace drift::crash
