#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace drift {

// First root under the search paths that holds a complete Chatterbox Multilingual export (all four
// graphs with their weights, tokenizer, Cangjie table, default voice and constants.json). Empty
// when none does, so a half-downloaded folder never counts as installed.
QString resolveChatterboxModelDir();

// What the speech encoder derives from a reference recording. Computing it costs a model run, so
// it is kept apart from synthesis: a voice is encoded once and reused for every sentence.
struct VoiceConditioning
{
    std::vector<float> condEmb;      // [1, T, 1024], prepended to the text embeddings
    std::vector<int64_t> condEmbShape;
    std::vector<int64_t> promptToken; // [1, N], speech tokens of the reference
    std::vector<int64_t> promptTokenShape;
    std::vector<float> refXVector; // [1, 192], speaker embedding
    std::vector<int64_t> refXVectorShape;
    std::vector<float> promptFeat; // [1, F, 80], reference mel features for the decoder
    std::vector<int64_t> promptFeatShape;

    QByteArray serialize() const;
    static std::optional<VoiceConditioning> deserialize(const QByteArray &data);
};

// Chatterbox Multilingual (q4 language model) text-to-speech on ONNX Runtime. Synchronous and not
// thread-safe: callers run it off the GUI thread and keep one instance per worker.
class ChatterboxTts
{
public:
    static constexpr int kSampleRate = 24000;

    ChatterboxTts();
    ~ChatterboxTts();

    // Resolves the model directory and loads the sessions. Safe to call repeatedly.
    bool load(QString *err);

    // Language codes the model was trained for and this build can prepare text for.
    static QStringList supportedLanguages();

    QString modelDir() const;

    // pcm: 24 kHz mono float. Only the first 20 s are used.
    std::optional<VoiceConditioning> encodeVoice(const std::vector<float> &pcm24k, QString *err);

    // 24 kHz mono float. `text` is split into sentence-sized chunks, each generated separately and
    // joined with a short pause. progress(0..1) returning false cancels: the result is then empty
    // and *err is left untouched. Failures return empty with *err set.
    std::vector<float> synthesize(const QString &text, const QString &lang,
                                  const VoiceConditioning &voice, float exaggeration,
                                  const std::function<bool(double)> &progress, QString *err);

    ChatterboxTts(const ChatterboxTts &) = delete;
    ChatterboxTts &operator=(const ChatterboxTts &) = delete;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace drift
