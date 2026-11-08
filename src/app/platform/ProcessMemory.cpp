#include "app/platform/ProcessMemory.h"

#if defined(Q_OS_WIN)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// K32GetProcessMemoryInfo lives in kernel32, so no psapi.lib is needed.
#include <psapi.h>
#elif defined(Q_OS_MACOS)
#include <mach/mach.h>
#elif defined(Q_OS_LINUX) || defined(Q_OS_ANDROID)
#include <QByteArray>
#include <QFile>
#include <QList>
#endif

namespace drift::platform {

ProcessMemory processMemory()
{
    ProcessMemory memory;
#if defined(Q_OS_WIN)
    PROCESS_MEMORY_COUNTERS counters{};
    if (K32GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))) {
        memory.residentBytes = qint64(counters.WorkingSetSize);
        memory.peakResidentBytes = qint64(counters.PeakWorkingSetSize);
    }
#elif defined(Q_OS_MACOS)
    mach_task_basic_info info{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count)
        == KERN_SUCCESS) {
        memory.residentBytes = qint64(info.resident_size);
        memory.peakResidentBytes = qint64(info.resident_size_max);
    }
#elif defined(Q_OS_LINUX) || defined(Q_OS_ANDROID)
    QFile status(QStringLiteral("/proc/self/status"));
    if (status.open(QIODevice::ReadOnly)) {
        const auto kib = [](const QByteArray &line) {
            const QList<QByteArray> parts = line.simplified().split(' ');
            return parts.size() >= 2 ? parts.at(1).toLongLong() * 1024 : qint64(-1);
        };
        // /proc files report a size of 0, so atEnd() is true before anything is read.
        const QList<QByteArray> lines = status.readAll().split('\n');
        for (const QByteArray &line : lines) {
            if (line.startsWith("VmRSS:"))
                memory.residentBytes = kib(line);
            else if (line.startsWith("VmHWM:"))
                memory.peakResidentBytes = kib(line);
        }
    }
#endif
    return memory;
}

} // namespace drift::platform
