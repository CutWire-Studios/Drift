#pragma once

#include "SubtitleCue.h"
#include "Time.h"

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QString>

#include <cmath>
#include <memory>

namespace drift {

enum class TranscriptTokenType : quint8 { Word, Spacing, AudioEvent, Filler };

// One token of a transcript, in the ASSET's source time (not timeline time), so it survives any
// trim, split or reorder of the clips cut from that asset.
struct TranscriptWord
{
    TimeUs startUs = 0;
    TimeUs endUs = 0;
    QString text;
    qint16 speaker = -1; // index into Transcript::speakers; -1 = unknown
    TranscriptTokenType type = TranscriptTokenType::Word;
    float confidence = NAN;
    // The aligner could not place this word; its timing is estimated from its neighbours.
    bool interpolated = false;
};

struct TranscriptSpeaker
{
    QString id;
    QString label;
};

// Identifies the file a transcript was made from, so a replaced or re-encoded source reads stale.
struct SourceFingerprint
{
    QString path;
    qint64 size = 0;
    qint64 mtimeMs = 0;

    static SourceFingerprint of(const QString &path);
    bool matches(const QString &path) const;
    bool isEmpty() const { return path.isEmpty(); }
};

struct Transcript
{
    QString engine;
    QString language;
    QDateTime createdAt;
    SourceFingerprint source;
    bool diarized = false;
    // False when word timings are interpolated inside Whisper segments rather than measured.
    bool wordTimingsAligned = false;
    QList<TranscriptSpeaker> speakers;
    QList<TranscriptWord> words; // sorted by startUs

    // Indexes of tokens overlapping [inUs, outUs).
    QList<int> wordsInRange(TimeUs inUs, TimeUs outUs) const;
    int speechTokenCount() const;

    QJsonObject toJson() const;
    static std::shared_ptr<const Transcript> fromJson(const QJsonObject &object);
};

using TranscriptPtr = std::shared_ptr<const Transcript>;

bool isSpeechToken(const TranscriptWord &word);
QString transcriptTokenTypeToString(TranscriptTokenType type);

// True when `text` (case-folded, punctuation stripped) is a hesitation like "um" or "uh".
bool isFillerWord(const QString &text);

struct TranscriptPhrase
{
    TimeUs startUs = 0;
    TimeUs endUs = 0;
    int speaker = -1;
    QString text;
    int firstWord = -1;
    int lastWord = -1;
};

// Groups speech tokens in [inUs, outUs) into phrases, breaking on a gap >= breakOnGapUs, a
// speaker change, or maxWords (0 = no cap). Audio events become their own phrase when
// includeEvents; fillers are dropped from the text unless includeFillers.
QList<TranscriptPhrase> packTranscriptPhrases(const Transcript &transcript, TimeUs inUs, TimeUs outUs,
                                              TimeUs breakOnGapUs, int maxWords, bool includeFillers,
                                              bool includeEvents);

// Display cues for the speech in [inUs, outUs), timed in source µs, packed like packSubtitleCues
// but from measured word timings.
QList<SubtitleCue> cuesFromTranscript(const Transcript &transcript, TimeUs inUs, TimeUs outUs,
                                      int maxLineWidth, int maxLineCount, int maxWordsPerCue);

} // namespace drift
