#pragma once

#include <QString>

#include <memory>

namespace drift {

// Writes interleaved float PCM to a lossless FLAC file, consumed afterwards as ordinary media by
// ClipReaderPool (and, for denoise previews, by QtMultimedia).
//
// FLAC rather than WAV because these files are clip-length audio renders that live in the app's
// cache directory: lossless either way, and roughly half the bytes. 16-bit is the encoder's input
// format — the whole playback path already lands on s16, so the extra depth would never survive to
// a speaker.
class AudioFileWriter
{
public:
    AudioFileWriter();
    ~AudioFileWriter();

    bool open(const QString &path, int sampleRate, int channels, QString *errorOut);

    // Frames must be appended in order. `interleaved` holds `frames * channels` samples.
    bool writeFrames(const float *interleaved, int frames, QString *errorOut);

    // Flushes and moves the temp file into place. Without this the file stays a ".part".
    bool finish(QString *errorOut);

    // Closes and removes the partial file. Safe to call after finish().
    void abort();

    AudioFileWriter(const AudioFileWriter &) = delete;
    AudioFileWriter &operator=(const AudioFileWriter &) = delete;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

// <AppDataLocation>/denoised, created on demand.
QString denoiseCacheDir();

// A fresh, unused absolute path inside denoiseCacheDir(). `suffix` distinguishes the two preview
// snippets from each other; pass an empty string for a committed render.
QString newDenoisePath(const QString &suffix = QString());

// <AppDataLocation>/generated-audio, created on demand: voiceovers and sound effects made by the
// cloud providers. Referenced by projects like any imported media, so nothing sweeps it.
QString generatedAudioDir();

// A fresh, unused absolute path inside generatedAudioDir() for voiceover recordings.
QString newVoiceoverPath();
void sweepStaleVoiceovers();

// Deletes leftover A/B preview snippets. Committed renders are referenced by clips in saved
// projects and are never touched — only the "-preview"/"-original" pairs, which are scratch and
// are orphaned by a crash or a closed window. Called once at startup.
void sweepDenoisePreviews();

} // namespace drift
