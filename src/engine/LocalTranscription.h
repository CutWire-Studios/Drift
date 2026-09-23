#pragma once

#include "core/Transcript.h"

#include <QString>

#include <functional>
#include <memory>
#include <vector>

namespace drift {

struct LocalTranscribeOptions
{
    QString language; // empty = auto-detect
    bool align = true;
    bool vad = true;
    bool diarize = false;
    int numSpeakers = -1;
};

struct LocalTranscribeResult
{
    std::shared_ptr<Transcript> transcript;
    QString error;
    bool cancelled = false;
};

// Whisper for the words, the CTC aligner for when each was said, Silero VAD (when installed) to
// keep Whisper off silence and to tighten word edges, and the diarizer for who said it. pcm is
// 16 kHz mono starting at source time offsetUs; the transcript comes back in source time.
// Synchronous and heavy: run it on a job thread.
LocalTranscribeResult transcribeLocal(const std::vector<float> &pcm, TimeUs offsetUs,
                                      const LocalTranscribeOptions &options,
                                      const std::function<bool(double, const QString &)> &progress);

// Words of a Whisper segment with times spread by character count, the fallback when no aligner
// can place them. Exposed for tests.
QList<TranscriptWord> interpolatedWords(const QString &text, TimeUs startUs, TimeUs endUs);

} // namespace drift
