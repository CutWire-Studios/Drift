#pragma once

#include "core/SubtitleCue.h"

#include <QList>
#include <QString>
#include <QVariantList>

#include <functional>
#include <memory>
#include <vector>

namespace drift {

struct WhisperResult
{
    QList<SubtitleCue> cues; // times relative to the audio start (µs)
    bool cancelled = false;
    bool ok = false;
    QString error;
    // The language decoded with: the forced one, or what auto-detect picked.
    QString language;
};

// Whisper (openai/whisper-small) speech-to-text on ONNX Runtime. Lazily loads the ~750 MB
// model set once and reuses it. All work is synchronous on the calling thread — callers run
// it off the GUI thread (see AppController::generateSubtitlesForClip).
class WhisperTranscriber
{
public:
    static WhisperTranscriber &instance();

    // Whether the model files are on disk. Cheap — it only looks for them, where available()
    // goes on to load three quarters of a gigabyte of ONNX sessions. Use this to report or
    // gate on availability; use available() when about to transcribe.
    static bool modelPresent();

    // Resolves the model directory and loads the sessions on first use. False if the models
    // are missing or failed to load (see lastError()).
    bool available();
    QString lastError() const;

    // Languages the model can force, as [{code, label}, ...] sorted by label. Does not load
    // the ONNX sessions — only needs generation_config.json next to the model files.
    QVariantList supportedLanguages();

    // pcm: 16 kHz mono float32.
    // progress(fraction in [0,1], status) returns false to request cancel. status is a short
    // human-readable line for the UI (may be empty to leave the last message unchanged).
    // languageCode: ISO-ish Whisper code ("en", "si", …). Empty = auto-detect from audio.
    // maxWordsPerCue: cap on words per subtitle cue; 0 leaves the default packing alone.
    WhisperResult transcribe(const std::vector<float> &pcm,
                             const std::function<bool(double, const QString &)> &progress,
                             const QString &languageCode = QString(), int maxWordsPerCue = 0);

    // Whisper's own timestamped segments, unpacked: what word alignment starts from.
    WhisperResult transcribeSegments(const std::vector<float> &pcm,
                                     const std::function<bool(double, const QString &)> &progress,
                                     const QString &languageCode = QString());

    // Frees the sessions (~750 MB) so another model can load; the next call reloads them.
    void unload();

    WhisperTranscriber(const WhisperTranscriber &) = delete;
    WhisperTranscriber &operator=(const WhisperTranscriber &) = delete;

private:
    WhisperTranscriber();
    ~WhisperTranscriber();

    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace drift
