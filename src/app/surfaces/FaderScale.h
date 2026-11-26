#pragma once

#include <algorithm>
#include <cmath>

namespace drift::surface {

// Faders sit at 0.75 for unity and reach +6 dB at the top, the way hardware faders are cut; the
// bottom of travel is silence.
inline constexpr double kUnityFader = 0.75;
inline constexpr double kFaderMinDb = -60.0;
inline constexpr double kFaderMaxDb = 6.0;

inline double gainFromFader(double fader)
{
    fader = std::clamp(fader, 0.0, 1.0);
    const double db = fader <= kUnityFader ? kFaderMinDb * (1.0 - fader / kUnityFader)
                                           : (fader - kUnityFader) / (1.0 - kUnityFader) * kFaderMaxDb;
    return db <= kFaderMinDb + 0.5 ? 0.0 : std::pow(10.0, db / 20.0);
}

inline double faderFromGain(double gain)
{
    if (gain <= 0.0009)
        return 0.0;
    const double db = std::clamp(20.0 * std::log10(gain), kFaderMinDb, kFaderMaxDb);
    return db <= 0.0 ? (1.0 - db / kFaderMinDb) * kUnityFader
                     : kUnityFader + db / kFaderMaxDb * (1.0 - kUnityFader);
}

} // namespace drift::surface
