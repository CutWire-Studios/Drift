#pragma once

#include <cstdint>

namespace drift {

using TimeUs = int64_t;

constexpr TimeUs kUsPerSecond = 1'000'000;
constexpr TimeUs kUsPerMs = 1'000;

// Nearest µs, halves away from zero (qRound64's rule). Truncating turned 8.04 s into
// 8'039'999 µs (8.04 * 1e6 == 8039999.999999999), so a seconds value missed the µs edge it named.
// Hand-rolled because std::llround is not constexpr before C++23.
constexpr TimeUs secondsToUs(double seconds)
{
    const double us = seconds * static_cast<double>(kUsPerSecond);
    return static_cast<TimeUs>(us < 0.0 ? us - 0.5 : us + 0.5);
}

constexpr double usToSeconds(TimeUs us)
{
    return static_cast<double>(us) / static_cast<double>(kUsPerSecond);
}

constexpr TimeUs frameDurationUs(int fps)
{
    return fps > 0 ? kUsPerSecond / fps : kUsPerSecond / 30;
}

} // namespace drift
