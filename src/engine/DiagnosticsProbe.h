#pragma once

#include <QString>

#include <optional>

#include "HwAccel.h"

namespace drift::diag {

// Brackets the diagnostics benchmark's decoding and uploading.
//
// "What decoded the last frame" and "how the last frame reached the GPU" are process-wide,
// last-writer-wins facts: any ClipReader that opens a decoder and any frame the GL runtime
// imports overwrites them. The benchmark run from the debug dialog opens its own throwaway
// readers and drives the same GL runtime, so it used to leave those globals describing its own
// sweep — and the report then printed the sweep's answers in one section and playback's in
// another, of the same machine, in the same paste.
//
// Constructing this snapshots the live values; destroying it puts them back. Inside the scope
// the globals describe the probe, which is what the benchmark's own rows want to say. It does
// not make the globals thread-safe against a playback that keeps decoding — the caller pauses
// playback for the duration, which is also the only way to measure anything meaningful.
class ProbeScope
{
public:
    ProbeScope();
    ~ProbeScope();

    ProbeScope(const ProbeScope &) = delete;
    ProbeScope &operator=(const ProbeScope &) = delete;

private:
    int m_decodeBackend = -1;
    quint64 m_hwFallbackCount = 0;
    int m_uploadPath = 0;
    QString m_declineReason;
};

} // namespace drift::diag
