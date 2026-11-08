#pragma once

#include <QtGlobal>

namespace drift::platform {

// This process's resident and peak resident memory in bytes, each -1 where the platform does not
// say. Kept in its own file so <windows.h> and its macros (min, max, near) stay out of callers.
struct ProcessMemory
{
    qint64 residentBytes = -1;
    qint64 peakResidentBytes = -1;
};

ProcessMemory processMemory();

} // namespace drift::platform
