#pragma once

#include <QtGlobal>

#include <memory>

class QLockFile;

namespace drift::instances {

bool processAlive(qint64 pid);

// Marks this process as a running Drift until it quits, with a file named by its pid under
// <AppData>/instances. Idempotent per AppData location.
void registerThisInstance();

// Whether any other live process has registered. Markers left by processes that died are
// removed on the way. A pid reused by an unrelated process only makes this answer true when
// it need not — the safe direction for its callers, which hold back destructive cleanup.
bool otherInstanceRunning();

// Serializes startup between instances. Registering and the startup cleanup of shared
// storage happen under it, so a peer starting at the same moment cannot register — and so
// cannot start using that storage — until the cleanup is over, and then sees this instance
// running. Released when the returned lock is destroyed.
//
// By default it waits for as long as a live process holds it: giving up and carrying on
// would let this instance start extracting while the holder is still deleting. A holder
// that died is recognised by its pid and taken over at once. Null when the lock cannot be
// used at all (no writable AppData) — then no instance can hold it, so none may sweep — or
// when a finite timeoutMs ran out; callers must then assume someone else is running.
std::unique_ptr<QLockFile> lockStartup(int timeoutMs = -1);

} // namespace drift::instances
