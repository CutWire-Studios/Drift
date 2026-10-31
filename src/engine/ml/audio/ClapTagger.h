#pragma once

#include "engine/ml/audio/AstClassifier.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <array>
#include <functional>
#include <memory>
#include <vector>

namespace drift {

using ClapEmbedding = std::array<float, 512>;

struct ClapSpan
{
    qint64 begin = 0; // samples at 48 kHz
    qint64 end = 0;
};

struct ClapTag
{
    QString text;
    QString kind;     // WHOOSH, HIT, ... for an event phrase; empty for a vocabulary term
    QString category; // vocabulary category; empty for an event phrase
    float z = 0.0f;
    float cosine = 0.0f;
};

// earshot's verdicts on a calibrated vocabulary z: >= 3 strong, >= 2.5 plausible, >= 2 weak.
QString clapVerdict(float z);

enum ClapSectionCategory { Genre, Mood, Instruments, Ambience, Voice, kClapSectionCategories };

struct ClapSection
{
    double start = 0.0; // seconds from the start of the analysed audio
    double end = 0.0;
    int windows = 0;
    float coherence = 0.0f;
    ClapEmbedding embedding{};
    QList<ClapTag> tags[kClapSectionCategories]; // top two per category, plain per-sound z
    double levelDb = -120.0;                      // approximate LUFS; -120 when too short to tell
    bool digitalSilence = false;
    bool hasAst = false;
    AstSummary ast;
    QString label;
};

struct ClapSections
{
    QList<ClapSection> sections;
    QList<double> novelty; // strong timbre or level changes, seconds
    int windows = 0;
};

// LAION CLAP (HTSAT audio encoder, laion/larger_clap_general) on ONNX Runtime plus earshot's
// calibrated scoring against a vocabulary whose text embeddings ship with the model. There is no
// text encoder: only the 322 vocabulary terms and 49 event phrases can be asked about. 48 kHz
// mono in, with the gain of ffmpeg's downmix (0.707 * (L + R)), which the calibration was measured
// with. Synchronous; call it off the GUI thread.
class ClapTagger
{
public:
    static constexpr int kSampleRate = 48000;
    static constexpr qint64 kWindowSamples = 480000; // 10 s

    static ClapTagger &instance();
    static bool modelPresent();

    // Loads the ONNX session; needs the runtime.
    bool available();
    QString lastError() const;

    // The vocabulary and its scoring need no runtime, so completion and error hints work with the
    // addon installed but nothing loaded.
    bool loadVocabulary();
    QStringList vocabularyTerms() const;
    QString termCategory(int term) const;
    // The vocabulary index of `text` (case, spacing and a leading article ignored), else the
    // shortest term containing all of its words, or -1.
    int termIndex(const QString &text) const;
    QStringList closestTerms(const QString &text, int count) const;

    // One unit embedding per span, in order. Each span is cut to its first 10 s and repeat-padded
    // to 10 s inside the graph. Empty on failure or cancel.
    std::vector<ClapEmbedding> embed(const float *pcm, qint64 frames, const std::vector<ClapSpan> &spans,
                                     const std::function<bool(double)> &progress = {});
    // A whole clip: one window when it is 10 s or less, else up to `maxWindows` of 10 s windows
    // spread evenly (hop 5 s), averaged. False on failure.
    bool embedClip(const float *pcm, qint64 frames, ClapEmbedding *out, int maxWindows = 6);
    static ClapEmbedding meanOf(const std::vector<ClapEmbedding> &embeddings);
    // The windows embedClip() uses for a clip of `frames` samples.
    static std::vector<ClapSpan> clipSpans(qint64 frames, int maxWindows = 6);

    // Top `count` of the 49 event phrases: cosine minus half the phrase's background mean, z within
    // the event.
    QList<ClapTag> eventTags(const ClapEmbedding &embedding, int count = 3) const;
    // Calibrated z of every vocabulary term for this sound, in vocabulary order.
    struct TermScore
    {
        float z = 0.0f;
        float cosine = 0.0f;
    };
    std::vector<TermScore> termScores(const ClapEmbedding &embedding) const;
    // Best calibrated terms, skipping the listed categories.
    QList<ClapTag> topTerms(const ClapEmbedding &embedding, int count,
                            const QStringList &excludeCategories = {}) const;

    // 3 s windows merged while neighbours stay at cosine >= 0.84, split at strong timbre or level
    // changes, each tagged (genre, mood, instruments, ambience, voice). Empty under 1 s.
    ClapSections sections(const float *pcm, qint64 frames, double window, double hop, int maxWindows,
                          const std::function<bool(double)> &progress = {});

    ClapTagger(const ClapTagger &) = delete;
    ClapTagger &operator=(const ClapTagger &) = delete;

private:
    ClapTagger();
    ~ClapTagger();
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace drift
