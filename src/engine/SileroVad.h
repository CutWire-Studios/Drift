#pragma once

#include "core/Time.h"

#include <QList>
#include <QString>

#include <functional>
#include <memory>
#include <vector>

namespace drift {

struct VadParams
{
    float threshold = 0.5f;
    // Below this a speech run ends; <0 = max(threshold - 0.15, 0.01), Silero's own hysteresis.
    float negThreshold = -1.0f;
    int minSpeechMs = 250;
    int minSilenceMs = 100;
    int speechPadMs = 30;
};

struct VadRange
{
    TimeUs startUs = 0;
    TimeUs endUs = 0;
};

// Silero VAD v5 (snakers4/silero-vad) on ONNX Runtime: one speech probability per 32 ms of
// 16 kHz mono. The model is tiny, so it always runs on the CPU. Synchronous; call it off the GUI
// thread.
class SileroVad
{
public:
    static SileroVad &instance();
    static bool modelPresent();
    static constexpr int kWindowSamples = 512;

    bool available();
    QString lastError() const;

    // One probability per kWindowSamples of pcm (16 kHz mono). Empty on failure or cancel;
    // `progress` gets 0–1 and returns false to cancel.
    std::vector<float> probabilities(const std::vector<float> &pcm,
                                     const std::function<bool(double)> &progress = {});

    SileroVad(const SileroVad &) = delete;
    SileroVad &operator=(const SileroVad &) = delete;

private:
    SileroVad();
    ~SileroVad();
    struct Impl;
    std::unique_ptr<Impl> d;
};

// Speech ranges from per-window probabilities: Silero's get_speech_timestamps (hysteresis, minimum
// speech and silence, padding split across short gaps). Times are relative to the first window.
QList<VadRange> vadSpeechRanges(const std::vector<float> &probabilities, size_t totalSamples,
                                const VadParams &params);

} // namespace drift
