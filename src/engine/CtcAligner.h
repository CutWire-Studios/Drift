#pragma once

#include "core/Time.h"

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>
#include <vector>

namespace drift {

struct AlignedWord
{
    TimeUs startUs = 0;
    TimeUs endUs = 0;
    float score = 0.0f;
    // False when the word had nothing the model can spell (a number in a language without
    // expansion, CJK in an English model) or the chunk failed; the caller interpolates it.
    bool aligned = false;
};

// Forced alignment with a character CTC model (wav2vec2 fine-tuned for ASR): given the words
// Whisper heard and the audio, finds when each was spoken. Whisper's ONNX export has no
// cross-attention output, so this is where real word timings come from. One model per language
// (addon kind "align-model"). Synchronous; call it off the GUI thread.
class CtcAligner
{
public:
    static CtcAligner &instance();
    // Languages with an installed model, e.g. {"en"}.
    static QStringList installedLanguages();
    static bool modelPresent(const QString &language);

    bool available(const QString &language);
    QString lastError() const;

    // pcm: 16 kHz mono, at most ~30 s (attention cost is quadratic). Times are relative to pcm[0].
    QList<AlignedWord> align(const float *pcm, size_t samples, const QStringList &words,
                             const QString &language);

    void unload();

    CtcAligner(const CtcAligner &) = delete;
    CtcAligner &operator=(const CtcAligner &) = delete;

private:
    CtcAligner();
    ~CtcAligner();
    struct Impl;
    std::unique_ptr<Impl> d;
};

// The characters of `word` the model can emit, upper-cased when `upper`; digits and a few symbols
// are spelled out first for English. Empty when nothing is alignable.
QString normalizeForAligner(const QString &word, const QHash<QChar, int> &vocab, bool upper,
                            const QString &language);

// CTC Viterbi forced alignment. logProbs is [frames x vocab] log-softmax; tokens the target label
// sequence. Returns, per token, the [first, last] frame the best path spends on it; empty when
// the audio is too short to hold the sequence.
QList<QPair<int, int>> ctcForcedAlign(const float *logProbs, int frames, int vocab,
                                      const std::vector<int> &tokens, int blank);

} // namespace drift
