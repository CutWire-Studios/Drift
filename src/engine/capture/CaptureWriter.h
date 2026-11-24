#pragma once

#include "core/base/Time.h"

#include <QImage>
#include <QSize>
#include <QString>

#include <memory>

namespace drift {

// Writes a screen or camera recording: H.264 in MP4, with optional AAC audio.
//
// This exists instead of QMediaRecorder because a recorder has to be told what to produce. Left to
// choose, Qt's FFmpeg backend encodes HEVC and hands x265 a 4:4:4 12-bit profile — minutes of CPU
// for seconds of screen, and a file many players refuse. yuv420p H.264 is what every editor, phone
// and browser reads.
//
// The other reason is time. A screen grab does not produce frames on a grid: nothing changes for a
// second, then three frames arrive inside a millisecond. So frames carry the timestamp they were
// captured at and the stream is timed in microseconds, which is what keeps a recording in sync
// with its own audio over a long session. Handing those same irregular frames to a muxer set up
// for a fixed rate is what produces "packet with invalid duration" and sound that drifts.
//
// Frames must arrive in order. A frame at or before the previous timestamp is dropped rather than
// muxed, because a backwards PTS makes the file unseekable.
//
// Not thread-safe, and nothing here blocks on a device: a capture backend calls it from whichever
// thread it delivers frames on, as long as that is only ever one thread at a time.
class CaptureWriter
{
public:
    CaptureWriter();
    ~CaptureWriter();
    CaptureWriter(const CaptureWriter &) = delete;
    CaptureWriter &operator=(const CaptureWriter &) = delete;

    struct Settings
    {
        QSize size;
        // The rate written into the container. Playback timing comes from the frame timestamps, so
        // this only has to be close: it is what a player shows before it has decoded anything, and
        // what a conservative decoder uses to size its buffers.
        int fps = 30;
        // Constant-quality rate control. 20 is visually clean for desktop content, where flat
        // panels of colour and crisp text punish a bitrate cap far more than camera footage does.
        int crf = 20;
        bool audio = false;
        int audioSampleRate = 48000;
        int audioChannels = 2;
    };

    // Writes to `path` + ".part" and renames on finish(), so an interrupted recording never leaves
    // a file that looks complete. An odd width or height is rejected: H.264 chroma is half
    // resolution.
    bool open(const QString &path, const Settings &settings, QString *errorOut);

    // `timestampUs` is measured from the start of the recording and must increase. The image is
    // converted and scaled if it does not already match the opened size.
    bool writeVideoFrame(const QImage &image, TimeUs timestampUs, QString *errorOut);

    // Interleaved float samples, `frames` per channel, at the opened rate and channel count.
    // Audio is timed by how much has been written rather than by a timestamp: a capture backend
    // delivers a continuous stream, and counting samples is what keeps it from drifting against
    // the video's wall-clock timestamps.
    bool writeAudio(const float *interleaved, int frames, QString *errorOut);

    // Flushes both encoders, writes the trailer and moves the file into place.
    bool finish(QString *errorOut);
    // Closes and removes the partial file. Safe after finish().
    void abort();

    // Frames handed in with a timestamp that did not advance, for diagnostics.
    int droppedFrames() const;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace drift
