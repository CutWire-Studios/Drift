#pragma once

#include "engine/audio/analysis/AudioEvents.h"
#include "engine/audio/analysis/AudioSync.h"
#include "engine/audio/analysis/LoudnessMeter.h"

#include <QImage>
#include <QList>
#include <QRgb>
#include <QString>
#include <QStringList>
#include <QVector>

// A soundtrack as one annotated picture for agents: frames at the cuts, labelled events, a
// spectrogram, the waveform, loudness and sync on one shared time axis. Pure — the caller mixes
// the audio, renders the frames and runs the analyses.
namespace drift::audiosheet {

struct FrameAt
{
    double seconds = 0.0; // absolute; drawn right of the cut it follows, with a tick at this time
    QImage image;
};

// Lanes that stay hidden while empty.
struct Section
{
    double start = 0.0;
    double end = 0.0;
    QString label;
};

struct PresenceSpan
{
    double start = 0.0;
    double end = 0.0;
    QString kind; // "speech", "music", ...
};

// A label hung from an event. `kind` picks the colour; `weak` draws it pale.
struct EventLabel
{
    double seconds = 0.0;
    QString kind;
    QString text;
    bool weak = false;
    int priority = 1; // lower is placed first when labels compete for room
    double rank = 0.0; // within a priority, larger is placed first
};

struct Input
{
    QString title;
    double startSeconds = 0.0;
    double durationSeconds = 0.0;

    const float *mono = nullptr; // mono PCM of the span, for the spectrogram and the waveform
    qint64 monoFrames = 0;
    int sampleRate = 48000;

    LoudnessReport loudness;
    QList<AudioEvent> events;
    QList<EventLabel> labels; // empty: derived from `events`
    QList<double> beats;
    QList<double> downbeats;
    double bpm = 0.0;
    QList<double> cuts;
    AudioSyncResult sync;
    bool hasSync = false;
    QList<FrameAt> frames;
    QList<Section> sections;
    QList<PresenceSpan> presence;
    QStringList notes; // extra small header lines
};

struct Options
{
    int width = 2000; // clamped to 1000-2400
};

inline constexpr int kMinWidth = 1000;
inline constexpr int kMaxWidth = 2400;

QImage render(const Input &in, const Options &opt);

// Log-frequency magnitude spectrogram in dB: n_fft 4096, hop = frames / columns, `rows`
// triangular bands geometrically spaced between minHz and maxHz. db is [column][row], row 0 lowest.
struct Spectrogram
{
    int rows = 0;
    int columns = 0;
    QVector<float> db;
    QVector<double> centreHz;
    float at(int column, int row) const { return db[column * rows + row]; }
};
Spectrogram logSpectrogram(const float *mono, qint64 frames, int sampleRate, int columns,
                           int rows = 240, double minHz = 30.0, double maxHz = 16000.0);

// Magma colour map, 0..1.
QRgb magma(float v);

struct PlacedLabel
{
    int row = 0;
    double seconds = 0.0; // the event
    double left = 0.0;    // where the text starts
    int index = 0;        // into the input list
};
// Greedy staggering: labels are placed in priority order into the first of `rows` rows where
// they do not overlap horizontally. Widths are in pixels, `plotPx` spans start..end.
QList<PlacedLabel> placeLabels(const QList<EventLabel> &labels, const QList<int> &widthsPx,
                               double start, double end, double plotPx, int rows);

// The smallest of 0.1, 0.25, 0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300 s (else 600) that fits
// `maxTicks` ticks across the span.
double tickStep(double span, int maxTicks = 24);

} // namespace drift::audiosheet
