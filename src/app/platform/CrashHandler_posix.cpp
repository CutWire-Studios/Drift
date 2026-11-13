// Linux and macOS crash capture, with no third-party crash library.
//
// Everything the handler needs is prepared in installCrashHandler(): the crash folder's paths as
// plain char buffers, an alternate signal stack (so a stack overflow can still be handled) and a
// warmed-up backtrace(). The handler then writes crash.json, stack.txt and a log entry with
// open/write only, restores the default action and raises the signal again so the OS still makes
// its core dump or crash report.

#include "app/platform/CrashHandler.h"

#include <QDir>
#include <QFile>
#include <QUuid>

#include <cerrno>
#include <climits>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include <dlfcn.h>
#include <execinfo.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace drift::crash {

namespace {

constexpr int kSignals[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT};
constexpr size_t kAltStackSize = 128 * 1024;
constexpr int kMaxFrames = 64;

char g_dir[PATH_MAX];
char g_json[PATH_MAX];
char g_stack[PATH_MAX];
int g_logFd = -1;
volatile sig_atomic_t g_crashing = 0;
timespec g_start;

void setPath(char *destination, const QString &path)
{
    const QByteArray bytes = QFile::encodeName(path);
    const size_t count = qMin<size_t>(size_t(bytes.size()), PATH_MAX - 1);
    memcpy(destination, bytes.constData(), count);
    destination[count] = '\0';
}

void writeAll(int fd, const char *data, size_t size)
{
    while (size > 0) {
        const ssize_t written = write(fd, data, size);
        if (written < 0) {
            if (errno == EINTR)
                continue;
            return;
        }
        data += written;
        size -= size_t(written);
    }
}

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

const void *faultingPc(const ucontext_t *context, const siginfo_t *info)
{
#if defined(Q_OS_LINUX) && defined(__x86_64__)
    return reinterpret_cast<const void *>(context->uc_mcontext.gregs[REG_RIP]);
#elif defined(Q_OS_LINUX) && defined(__aarch64__)
    return reinterpret_cast<const void *>(context->uc_mcontext.pc);
#elif defined(Q_OS_MACOS) && defined(__x86_64__)
    return reinterpret_cast<const void *>(context->uc_mcontext->__ss.__rip);
#elif defined(Q_OS_MACOS) && defined(__aarch64__)
    return reinterpret_cast<const void *>(__darwin_arm_thread_state64_get_pc(context->uc_mcontext->__ss));
#else
    (void)context;
    return info->si_addr;
#endif
}

const char *baseName(const char *path)
{
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

// abort() and friends fault inside the C library; the interesting module is its caller.
bool isSystemRuntime(const char *name)
{
    static const char *const prefixes[] = {"libc.", "libc-", "libstdc++", "libgcc_s", "libpthread", "libsystem_",
                                           "libc++", "libdyld", "ld-linux"};
    for (const char *prefix : prefixes) {
        if (strncmp(name, prefix, strlen(prefix)) == 0)
            return true;
    }
    return false;
}

const char *signalName(int signal)
{
    switch (signal) {
    case SIGSEGV: return "SIGSEGV";
    case SIGBUS: return "SIGBUS";
    case SIGILL: return "SIGILL";
    case SIGFPE: return "SIGFPE";
    case SIGABRT: return "SIGABRT";
    }
    return "?";
}

struct ModuleInfo
{
    char name[PATH_MAX];
    char path[PATH_MAX];
    unsigned long long offset;
    const void *address;
};

// dladdr is not strictly async-signal-safe (it takes the loader lock), but it is what crash
// reporters do in practice and the alternative is no module name at all.
bool describeAddress(const void *address, ModuleInfo *module)
{
    Dl_info info;
    if (!address || !dladdr(address, &info) || !info.dli_fname)
        return false;
    snprintf(module->path, sizeof(module->path), "%s", info.dli_fname);
    snprintf(module->name, sizeof(module->name), "%s", baseName(info.dli_fname));
    module->offset = static_cast<unsigned long long>(static_cast<const char *>(address)
                                                    - static_cast<const char *>(info.dli_fbase));
    module->address = address;
    return true;
}

void describeFault(const ucontext_t *context, const siginfo_t *info, void *const *frames, int frameCount,
                   ModuleInfo *module)
{
    module->name[0] = module->path[0] = '\0';
    module->offset = 0;
    module->address = nullptr;

    const void *pc = faultingPc(context, info);
    if (describeAddress(pc, module) && !isSystemRuntime(module->name))
        return;
    for (int i = 0; i < frameCount; ++i) {
        ModuleInfo candidate;
        if (describeAddress(frames[i], &candidate) && !isSystemRuntime(candidate.name)) {
            *module = candidate;
            return;
        }
    }
}

void writeCrashJson(int signal, const ModuleInfo &module)
{
    timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    timespec monotonic;
    clock_gettime(CLOCK_MONOTONIC, &monotonic);
    tm utc;
    const time_t seconds = now.tv_sec;
    gmtime_r(&seconds, &utc);

    char name[PATH_MAX * 2] = "";
    char path[PATH_MAX * 2] = "";
    appendJsonString(name, sizeof(name), module.name);
    appendJsonString(path, sizeof(path), module.path);

    char json[4096 + 2 * PATH_MAX * 2];
    snprintf(json, sizeof(json),
             "{\"code\":\"0x%X\",\"module\":\"%s\",\"modulePath\":\"%s\",\"moduleVersion\":\"\","
             "\"offset\":\"0x%llX\",\"crashedAt\":\"%04d-%02d-%02dT%02d:%02d:%02dZ\",\"uptimeSeconds\":%lld}",
             unsigned(signal), name, path, module.offset, utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday,
             utc.tm_hour, utc.tm_min, utc.tm_sec, static_cast<long long>(monotonic.tv_sec - g_start.tv_sec));

    mkdir(g_dir, 0755);
    const int fd = open(g_json, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
        return;
    writeAll(fd, json, strlen(json));
    close(fd);
}

void crashSignalHandler(int signal, siginfo_t *info, void *context)
{
    if (g_crashing == 0) {
        g_crashing = 1;

        void *frames[kMaxFrames];
        const int frameCount = backtrace(frames, kMaxFrames);

        ModuleInfo module;
        describeFault(static_cast<const ucontext_t *>(context), info, frames, frameCount, &module);
        writeCrashJson(signal, module);

        const int stackFd = open(g_stack, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (stackFd >= 0) {
            backtrace_symbols_fd(frames, frameCount, stackFd);
            close(stackFd);
        }

        if (g_logFd >= 0) {
            char line[2 * PATH_MAX + 128];
            const int length = snprintf(line, sizeof(line), "crash: signal %d (%s) at %p (+0x%llX in %s)\n", signal,
                                        signalName(signal), module.address, module.offset, module.path);
            if (length > 0)
                writeAll(g_logFd, line, size_t(length) < sizeof(line) ? size_t(length) : sizeof(line) - 1);
            backtrace_symbols_fd(frames, frameCount, g_logFd);
        }
    }

    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = SIG_DFL;
    sigemptyset(&action.sa_mask);
    sigaction(signal, &action, nullptr);
    raise(signal);
}

} // namespace

void setCrashLogFd(int fd)
{
    g_logFd = fd;
}

void installCrashHandler(const QString &crashesDir)
{
    QDir().mkpath(crashesDir);
    const QString dir = crashesDir + QLatin1Char('/') + QUuid::createUuid().toString(QUuid::WithoutBraces);
    setPath(g_dir, dir);
    setPath(g_json, dir + QStringLiteral("/crash.json"));
    setPath(g_stack, dir + QStringLiteral("/stack.txt"));
    clock_gettime(CLOCK_MONOTONIC, &g_start);

    // The first backtrace() loads libgcc, which allocates; that has to happen now, not in the handler.
    void *warmup[4];
    backtrace(warmup, 4);

    stack_t alternate;
    alternate.ss_sp = malloc(kAltStackSize);
    alternate.ss_size = kAltStackSize;
    alternate.ss_flags = 0;
    sigaltstack(&alternate, nullptr);

    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_sigaction = crashSignalHandler;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&action.sa_mask);
    for (const int signal : kSignals)
        sigaction(signal, &action, nullptr);
}

bool processAlive(qint64 pid)
{
    return kill(pid_t(pid), 0) == 0 || errno == EPERM;
}

} // namespace drift::crash
