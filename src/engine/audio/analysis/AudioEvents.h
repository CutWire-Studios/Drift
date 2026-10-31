#pragma once

#include <QList>
#include <QString>
#include <QVector>

namespace drift {

struct AudioEvent
{
    enum class Kind { Hit, Swell };

    double seconds = 0.0; // absolute timeline seconds; for a swell, the moment it peaks
    float strength = 0.0f; // 0..1, prominence over the bed / 30 dB
    double levelDbfs = -120.0; // peak of the event's first stretch of sound
    Kind kind = Kind::Hit;
    QString band; // "full", "low" (< 200 Hz) or "high" (> 2 kHz): where it stands out most
    double prominenceDb = 0.0; // rise of the K-weighted level over the bed just before
    double contrastDb = 0.0;   // the same in the band where it stands out most
    double bedDb = -80.0;
    double z = 0.0; // onset novelty in robust deviations against the surrounding 2 s
    int lengthMs = 0;
};

// 10 ms RMS levels in dB (floored at -80), smoothed over 90 ms; frame i sits at t0 + i * 0.01.
// Kept so a cut with no onset near it can still be matched to a soft whoosh peaking there.
struct AudioLevelEnvelopes
{
    double t0 = 0.0;
    QVector<float> full;
    QVector<float> high; // above 2 kHz
};

struct AudioEventsResult
{
    QList<AudioEvent> events; // chronological, hits and swells together
    AudioLevelEnvelopes levels;
};

struct AudioEventsOptions
{
    double zThreshold = 3.0;
    double minContrastDb = 3.0;
    double minGapSeconds = 0.03;
    // The span starts where the sound does (a clip or file start): sound from the first
    // millisecond then counts as an event, since nothing precedes it to rise over.
    bool leadIn = false;
};

// Transient onsets and loudness swells. Superflux on a 96-band log-mel (n_fft 1024, 5 ms hop,
// 30 Hz - 16 kHz), thresholded on a robust z over 2 s, then each onset is walked back to its
// attack on 1 ms band envelopes. Mono PCM at 48 kHz; anything else returns no events.
class AudioEvents
{
public:
    static constexpr int kSampleRate = 48000;
    static constexpr double kMinAnalysisSec = 0.1;

    static AudioEventsResult analyze(const float *mono, qint64 frameCount, int sampleRate,
                                     double startSeconds, const AudioEventsOptions &options = {});
};

} // namespace drift
