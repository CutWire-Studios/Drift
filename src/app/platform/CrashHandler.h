#pragma once

#include <QString>

// Where a crash handler exists: Windows, and the Linux and macOS desktops. Not Android or wasm.
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS) || (defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID) && !defined(Q_OS_WASM))
#define DRIFT_HAS_CRASH_HANDLER 1
#endif

namespace drift::crash {

#ifdef DRIFT_HAS_CRASH_HANDLER
// Windows: installs the unhandled-exception filter and starts the dump-writer thread. A crash writes
// <crashesDir>/<new uuid>/{upload.dmp,full.dmp,crash.json}; see CrashStore for what reads them.
// Chains to whichever filter was installed before, so the one-line log entry still happens.
// Linux and macOS: installs signal handlers; a crash writes <crashesDir>/<new uuid>/{crash.json,
// stack.txt} and appends the signal and backtrace to the log set with setCrashLogFd. No minidumps.
void installCrashHandler(const QString &crashesDir);

// Whether a process with this id is still running.
bool processAlive(qint64 pid);

#ifndef Q_OS_WIN
// The open drift.log; call before installCrashHandler. -1 means none.
void setCrashLogFd(int fd);
#endif
#else
inline void installCrashHandler(const QString &) {}
inline bool processAlive(qint64) { return false; }
#endif

} // namespace drift::crash
