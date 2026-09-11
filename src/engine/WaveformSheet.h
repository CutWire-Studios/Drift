#pragma once

#include <QImage>
#include <QList>
#include <QPair>
#include <QVector>

// Audio-as-image for agents: peak lanes, silence shading, onset/beat marks, an optional
// spectrogram and a time axis in one picture. Pure — the caller mixes the audio.
namespace drift::waveformsheet {

struct Input
{
    double startSeconds = 0.0;
    double durationSeconds = 0.0;
    QVector<float> mixed;                   // 0..1 peaks, any bucket count
    QVector<float> speech;                  // 0..1 speech-band peaks; empty hides the lane
    QList<QPair<double, double>> silence;   // absolute seconds
    QList<double> onsets;                   // absolute seconds
    QList<double> beats;                    // absolute seconds
    QVector<QVector<float>> spectrogram;    // [column][bin] 0..1, bin 0 lowest; empty hides it
};

struct Options
{
    int width = 1400;
    int height = 300;
    int spectrogramHeight = 120;
    int axisHeight = 24;
};

inline constexpr QRgb kSilenceShade = 0xFF283040;

// Lanes top to bottom: mixed (55%), speech (45%), spectrogram, axis. The mixed lane takes
// the whole peak area when there is no speech lane.
QImage render(const Input &in, const Options &opt);

// The smallest of 0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300, 600 s that is at least
// `minPixels` wide at this scale.
double axisStepSeconds(double durationSeconds, int width, int minPixels = 80);

// Log-frequency magnitude spectrogram: `columns` columns of `bins` log-spaced bands from
// `minHz` to Nyquist, log-compressed and normalised so the global maximum is 1.
QVector<QVector<float>> spectrogram(const float *mono, qint64 frameCount, int sampleRate,
                                    int bins, int columns, double minHz = 50.0);

} // namespace drift::waveformsheet
