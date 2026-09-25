#pragma once

#include "core/Time.h"

#include <QString>

#include <functional>
#include <vector>

namespace drift {

constexpr int kSpeechSampleRate = 16000;

// Decodes [inUs, outUs) of `path` to 16 kHz mono float, the input every speech model here takes.
// `progress` gets 0–1 and returns false to cancel; a cancelled read returns an empty buffer and
// sets *cancelled. outUs < 0 reads to the end of the file.
std::vector<float> readMono16k(const QString &path, TimeUs inUs, TimeUs outUs,
                               const std::function<bool(double)> &progress = {},
                               bool *cancelled = nullptr);

// Writes 16 kHz mono PCM to a FLAC file in the temp directory for upload. Empty on failure.
QString writeTempFlac16k(const std::vector<float> &pcm, QString *errorOut);

inline TimeUs speechSamplesToUs(size_t samples)
{
    return static_cast<TimeUs>((static_cast<int64_t>(samples) * kUsPerSecond) / kSpeechSampleRate);
}

inline size_t speechUsToSamples(TimeUs us)
{
    return us <= 0 ? 0 : static_cast<size_t>((us * kSpeechSampleRate) / kUsPerSecond);
}

} // namespace drift
