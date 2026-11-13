#pragma once

#include <QString>

namespace drift::crash {

#ifdef Q_OS_WIN
// Installs the unhandled-exception filter and starts the dump-writer thread. A crash writes
// <crashesDir>/<new uuid>/{upload.dmp,full.dmp,crash.json}; see CrashStore for what reads them.
// Chains to whichever filter was installed before, so the one-line log entry still happens.
void installCrashHandler(const QString &crashesDir);

// Whether a process with this id is still running.
bool processAlive(qint64 pid);
#else
inline void installCrashHandler(const QString &) {}
inline bool processAlive(qint64) { return false; }
#endif

} // namespace drift::crash
