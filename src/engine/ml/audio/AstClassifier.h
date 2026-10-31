#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>
#include <vector>

namespace drift {

struct AstLabel
{
    QString label;
    float probability = 0.0f;
};

// Probabilities of the three labels audio analysis names a section by, plus the strongest others.
struct AstSummary
{
    float speech = 0.0f;
    float music = 0.0f;
    float silence = 0.0f;
    QList<AstLabel> top;
};

struct AstSpan
{
    qint64 begin = 0; // samples at 16 kHz
    qint64 end = 0;
};

// Audio Spectrogram Transformer fine-tuned on AudioSet (MIT/ast-finetuned-audioset-10-10-0.4593)
// on ONNX Runtime: 527 independent class probabilities per window of up to 10.24 s of 16 kHz
// mono. The graph computes the Kaldi fbank itself, so callers hand it raw samples. Mono is
// ffmpeg's downmix, 0.707 * (L + R): that is the gain the model was calibrated with, not the mean.
// Synchronous; call it off the GUI thread.
class AstClassifier
{
public:
    static constexpr int kSampleRate = 16000;
    static constexpr int kClasses = 527;
    static constexpr qint64 kWindowSamples = 163840; // 10.24 s

    static AstClassifier &instance();
    static bool modelPresent();

    bool available();
    QString lastError() const;

    // One probability row per span, in order; spans longer than a window use their first 10.24 s,
    // shorter ones are zero-padded by the graph. Empty on failure or cancel.
    std::vector<std::vector<float>> probabilities(const float *pcm, qint64 frames,
                                                  const std::vector<AstSpan> &spans,
                                                  const std::function<bool(double)> &progress = {});

    // Windows of 10.24 s every 5.12 s (the last flush with the end) reduced with max, the way
    // earshot labels a whole recording. A clip of one window or less is one run.
    std::vector<float> probabilitiesOfClip(const float *pcm, qint64 frames);

    QString labelName(int index) const;
    int labelIndex(const QString &label) const; // -1 when unknown
    AstSummary summarise(const std::vector<float> &probabilities, int topCount, float minProbability) const;

    // 48 kHz -> 16 kHz with a windowed-sinc polyphase filter (cutoff 7.6 kHz).
    static std::vector<float> resample48kTo16k(const float *pcm, qint64 frames);

    AstClassifier(const AstClassifier &) = delete;
    AstClassifier &operator=(const AstClassifier &) = delete;

private:
    AstClassifier();
    ~AstClassifier();
    struct Impl;
    std::unique_ptr<Impl> d;
};

// Speech, music and silence along a recording.
struct AstPresenceSpan
{
    double start = 0.0; // seconds from the start of the audio
    double end = 0.0;
    QString kind; // "speech", "music", "speech+music", "silence" or "other"
};

struct AstPresence
{
    QList<AstPresenceSpan> spans;
    double speechSeconds = 0.0; // covered by speech or speech+music
    double musicSeconds = 0.0;
    double silenceSeconds = 0.0;
    int windows = 0;
    bool ok = false;
    QString error;
};

// 16 kHz mono in the model's gain. Windows of 5.12 s every 2.56 s (the hop grows on long audio to
// keep at most `maxWindows`); each window votes for its middle `hop`, and equal neighbours merge.
AstPresence astPresence(AstClassifier &ast, const float *pcm16k, qint64 frames, int maxWindows = 120,
                        const std::function<bool(double)> &progress = {});

} // namespace drift
