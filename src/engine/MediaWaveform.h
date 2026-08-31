#pragma once

#include <QString>
#include <QVariantList>
#include <QVector>

#include <functional>

class MediaWaveform
{
public:
    // Max-abs amplitude peaks for the whole file at a fixed rate, plus the duration they
    // span so callers can map a source second back to an index. Values are raw [0, 1];
    // the display floor is applied by the caller.
    struct Dense
    {
        QVector<float> peaks;
        double durationSeconds = 0.0;
    };

    // Resolution is duration-proportional so a multi-hour file does not collapse into the
    // same handful of buckets as a short one. `maxPeaks` bounds memory (1M floats = 4 MB).
    // Decodes the whole file, so cost scales with duration — prefer peaksForRange() for
    // anything that only needs part of a long source.
    static Dense densePeaks(const QString &sourcePath, int peaksPerSecond = 100,
                            int maxPeaks = 1 << 20, int streamOrdinal = 0);

    // Peaks for [startSeconds, endSeconds) only. Seeks first, so the cost is proportional to
    // the span asked for rather than to the file length. Returns exactly
    // ceil((end - start) * peaksPerSecond) values, or empty if nothing decoded (past the end
    // of the media, no audio stream, unreadable file).
    static QVector<float> peaksForRange(const QString &sourcePath, double startSeconds,
                                        double endSeconds, int peaksPerSecond, int streamOrdinal = 0);

    // Writes up to `maxFrames` frames of interleaved-stereo float PCM starting `frameOffset`
    // frames into the span, and returns how many it wrote. Return 0 to stop early.
    using FillChunk = std::function<int(float *out, qint64 frameOffset, int maxFrames)>;

    // Voice-emphasized peaks over `totalFrames` of interleaved-stereo float PCM, pulled one
    // window at a time so the caller never materializes the whole span — a feature-length
    // timeline is hundreds of MB of PCM to draw a few thousand columns.
    //
    // Collapses to mono, band-passes to the speech range (300 Hz - 3 kHz, 4th-order at both
    // ends so music outside the band is genuinely rejected rather than just tilted), then
    // buckets max-abs amplitude into `buckets` normalized peaks in [0.05, 1.0]. Filter state
    // and the normalization carry across windows, so the result is what feeding it the whole
    // span in one call would have produced.
    static QVariantList voicePeaks(qint64 totalFrames, int sampleRate, int buckets,
                                   const FillChunk &fill);

    // voicePeaks without the speech band-pass, the normalization or the display floor: plain
    // max-abs amplitude per bucket, in [0, 1], pulled a window at a time the same way.
    //
    // For callers that want the signal rather than a drawing. Silence reads as a true 0 here,
    // which is the whole point — the floor voicePeaks applies keeps a quiet passage visible in
    // a lane, but it makes "is there anything here" unanswerable.
    static QVector<float> mixedPeaks(qint64 totalFrames, int sampleRate, int buckets,
                                     const FillChunk &fill);

    // voicePeaks' speech band-pass without the display floor or loudness normalisation:
    // true 0 for silence, which is what detect_silence needs. Same 300 Hz–3 kHz 4th-order
    // path so breath and room tone don't read as speech.
    static QVector<float> speechPeaks(qint64 totalFrames, int sampleRate, int buckets,
                                      const FillChunk &fill);
};
