#pragma once

#include <QElapsedTimer>

// Opt-in timing for on-device benchmarks. Off unless DRIFT_PERF_LOG is set, or on Android the
// debug.drift.perf system property is "1" (adb shell setprop debug.drift.perf 1). Samples are
// summarised once a second to the log as "DriftPerf <name> n= p50= p95= max=".
namespace drift::perf {

bool enabled();

// Starts the once-a-second summary. No-op when disabled.
void install();
// Thread-safe.
void record(const char *name, double ms);

class Scope
{
public:
    explicit Scope(const char *name)
        : m_name(name)
    {
        if (enabled())
            m_timer.start();
    }
    ~Scope()
    {
        if (m_timer.isValid())
            record(m_name, double(m_timer.nsecsElapsed()) / 1'000'000.0);
    }
    Scope(const Scope &) = delete;
    Scope &operator=(const Scope &) = delete;

private:
    const char *m_name;
    QElapsedTimer m_timer;
};

} // namespace drift::perf
