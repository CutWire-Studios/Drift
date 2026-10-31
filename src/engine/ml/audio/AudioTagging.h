#pragma once

#include "engine/audio/analysis/AudioEvents.h"
#include "engine/ml/audio/AstClassifier.h"
#include "engine/ml/audio/ClapTagger.h"

#include <QList>
#include <QString>

#include <functional>

// What a stretch of audio sounds like, from CLAP (tags, sections, vocabulary search) and AST
// (speech and music), following earshot's timeline. Pure: the caller hands in mono 48 kHz at the
// models' gain (0.707 * (L + R), ffmpeg's downmix) and the events the detector found.
namespace drift {

struct AudioTagOptions
{
    int maxTagged = 20;   // events that get a CLAP tag
    int astEvents = 1;    // of those, the most important that also get AST labels
    int astSections = 3;  // section crops AST labels
    double sectionWindow = 3.0;
    double sectionHop = 2.0;
    int sectionMaxWindows = 32;
    bool sections = true;
    QList<double> near; // cut or cue times (seconds from the start): events within 100 ms are always tagged
    std::function<bool(double)> progress;
};

struct TaggedEvent
{
    int index = 0; // into the events handed in
    QList<ClapTag> tags; // top three event phrases
    QString kind;   // the best phrase's kind, or VOICE when AST hears speech
    QString label;  // short name of the best phrase
    bool exposed = false; // >= 12 dB over its bed, so it dominates its excerpt
    bool hasAst = false;
    QList<AstLabel> ast;
    float speech = 0.0f;
    QList<ClapTag> soundsLike; // best two calibrated vocabulary terms
};

struct AudioTags
{
    bool clap = false; // CLAP ran
    bool ast = false;  // AST ran
    QString clapError; // why not, when the model is installed but failed
    QString astError;
    QList<ClapSection> sections;
    QList<TaggedEvent> events;
    int clapWindows = 0;
    bool cancelled = false;
};

// Drops a leading "a ", "an " or "the ".
QString shortTagName(const QString &text);

// Events are in absolute seconds; startSeconds is where `mono` begins.
AudioTags tagAudio(const float *mono, qint64 frames, double startSeconds, const QList<AudioEvent> &events,
                   const AudioTagOptions &options);

// Candidate stretches of an audio span, embedded once so any number of vocabulary terms can be
// searched without running the model again: sliding windows without gaps (about 40, growing to
// 10 s before there are more) plus excerpts around the 48 most prominent events.
struct MomentIndex
{
    struct Entry
    {
        double start = 0.0; // seconds from the start of the audio
        double end = 0.0;
        bool event = false;
    };
    std::vector<Entry> entries;
    std::vector<ClapEmbedding> embeddings;
    bool valid() const { return !entries.empty() && entries.size() == embeddings.size(); }
};

// Empty on failure or cancel; check ClapTagger::lastError().
MomentIndex indexMoments(ClapTagger &clap, const float *mono, qint64 frames, double startSeconds,
                         const QList<AudioEvent> &events, const std::function<bool(double)> &progress = {});

struct AudioMoment
{
    double start = 0.0; // seconds from the start of the audio
    double end = 0.0;
    float z = 0.0f;
    float cosine = 0.0f;
    bool event = false; // an excerpt around a detected event, else a sliding window
};

// The best non-overlapping moments for one vocabulary term, by calibrated z.
QList<AudioMoment> findMoments(ClapTagger &clap, const MomentIndex &index, int term, int top);

} // namespace drift
