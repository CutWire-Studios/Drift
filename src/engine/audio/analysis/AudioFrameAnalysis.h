#pragma once

#include "core/base/Time.h"

#include <QByteArray>
#include <QString>
#include <QVector>

#include <array>

namespace drift {
class Project;
}

// The audio an "audio" param reads, for one analysis window. Every field is a pure function of
// timeline time, so preview, scrubbing and export agree. See docs/gpu-effects.md, Shared audio
// contract.
struct AudioFrame
{
    std::array<float, 512> spectrum{}; // 0..1, 512 log-spaced bins from 20 Hz to 20 kHz
    std::array<float, 512> wave{};     // 0..1, the window decimated to 512 points
    float level = 0.0f;
    float bass = 0.0f;
    float beat = 0.0f;

    // 512x2 RGBA8: row 0 is the spectrum, row 1 the waveform. The value sits in R, G and B repeat
    // it, and A is 255.
    QByteArray rgba() const;
};

// One 2048-sample mono window at 48 kHz. beat is left at 0.
AudioFrame analyzeWindow(const float *mono2048, int sampleRate);

// The window of `trackId` (empty = Timeline mix, as heard: mute, hide and solo apply) centred on
// timelineUs, with its beat. With waitForOnsets false, onsets not yet computed for this source
// count as no beat, and they are computed in the background.
AudioFrame analyzeAt(const drift::Project &project, const QString &trackId, drift::TimeUs timelineUs,
                     bool waitForOnsets);

// Changes whenever the clips or gains the source reads change; keys the onset cache.
quint64 audioSourceFingerprint(const drift::Project &project, const QString &trackId);

// Mono (L+R)/2 of one source over [startUs, startUs + frames) at sampleRate, after clip gain and
// fades. Track volume and pan are not applied.
QVector<float> readSourceMono(const drift::Project &project, const QString &trackId, drift::TimeUs startUs,
                              int frames, int sampleRate);
