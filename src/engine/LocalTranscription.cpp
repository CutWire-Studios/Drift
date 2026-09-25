#include "LocalTranscription.h"

#include "CtcAligner.h"
#include "SileroVad.h"
#include "SpeakerDiarizer.h"
#include "SpeechAudio.h"
#include "WhisperTranscriber.h"

#include <QDebug>
#include <QFile>
#include <QRegularExpression>

#include <algorithm>

namespace drift {

namespace {

QStringList splitWords(const QString &text)
{
    return text.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
}

// Whisper and the aligner together hold ~1 GB; on a small machine let Whisper go first.
bool lowMemory()
{
    QFile meminfo(QStringLiteral("/proc/meminfo"));
    if (!meminfo.open(QIODevice::ReadOnly))
        return false;
    const QByteArray line = meminfo.readLine();
    const QList<QByteArray> parts = line.simplified().split(' ');
    return parts.size() >= 2 && parts.at(1).toLongLong() < 8LL * 1024 * 1024;
}

struct Segment
{
    TimeUs startUs = 0;
    TimeUs endUs = 0;
    QStringList words;
};

} // namespace

QList<TranscriptWord> interpolatedWords(const QString &text, TimeUs startUs, TimeUs endUs)
{
    const QStringList words = splitWords(text);
    QList<TranscriptWord> out;
    if (words.isEmpty())
        return out;
    int total = 0;
    for (const QString &w : words)
        total += std::max(1, static_cast<int>(w.size()));
    const TimeUs span = std::max<TimeUs>(1, endUs - startUs);
    TimeUs cursor = startUs;
    for (int i = 0; i < words.size(); ++i) {
        TranscriptWord w;
        w.text = words.at(i);
        w.startUs = cursor;
        w.endUs = i + 1 == words.size()
                      ? endUs
                      : cursor + static_cast<TimeUs>(static_cast<double>(std::max(1, static_cast<int>(words.at(i).size())))
                                                     / total * span);
        w.interpolated = true;
        cursor = w.endUs;
        out.append(w);
    }
    return out;
}

LocalTranscribeResult transcribeLocal(const std::vector<float> &pcmIn, TimeUs offsetUs,
                                      const LocalTranscribeOptions &options,
                                      const std::function<bool(double, const QString &)> &progress)
{
    LocalTranscribeResult result;
    auto report = [&](double f, const QString &status) { return !progress || progress(f, status); };
    if (pcmIn.empty()) {
        result.error = QStringLiteral("No audio");
        return result;
    }

    // 1. Speech map. Long stretches without speech are silenced before Whisper sees them: that is
    // where it invents text ("Thank you for watching").
    std::vector<float> pcm = pcmIn;
    QList<VadRange> speech;
    const bool haveVad = options.vad && SileroVad::modelPresent() && SileroVad::instance().available();
    if (haveVad) {
        if (!report(0.0, QStringLiteral("Finding speech…"))) {
            result.cancelled = true;
            return result;
        }
        const std::vector<float> probs = SileroVad::instance().probabilities(pcm);
        speech = vadSpeechRanges(probs, pcm.size(), VadParams{});
        constexpr TimeUs kKeep = 300'000;
        constexpr TimeUs kMinGap = 2'000'000;
        TimeUs cursor = 0;
        auto silence = [&](TimeUs a, TimeUs b) {
            if (b - a < kMinGap)
                return;
            const size_t from = speechUsToSamples(a + kKeep);
            const size_t to = std::min(pcm.size(), speechUsToSamples(b - kKeep));
            std::fill(pcm.begin() + static_cast<std::ptrdiff_t>(std::min(from, to)),
                      pcm.begin() + static_cast<std::ptrdiff_t>(to), 0.0f);
        };
        for (const VadRange &r : std::as_const(speech)) {
            silence(cursor, r.startUs);
            cursor = r.endUs;
        }
        silence(cursor, speechSamplesToUs(pcm.size()));
    }

    // 2. Words.
    WhisperTranscriber &whisper = WhisperTranscriber::instance();
    if (!whisper.available()) {
        result.error = whisper.lastError();
        return result;
    }
    const WhisperResult heard = whisper.transcribeSegments(
        pcm, [&](double f, const QString &s) { return report(0.05 + 0.6 * f, s); }, options.language);
    if (heard.cancelled) {
        result.cancelled = true;
        return result;
    }
    if (!heard.ok) {
        result.error = heard.error;
        return result;
    }
    const QString language = heard.language.isEmpty() ? options.language : heard.language;

    QList<Segment> segments;
    for (const SubtitleCue &cue : heard.cues) {
        const QStringList words = splitWords(cue.text);
        if (!words.isEmpty())
            segments.append({cue.startUs, cue.endUs, words});
    }

    auto transcript = std::make_shared<Transcript>();
    transcript->language = language;
    transcript->engine = QStringLiteral("whisper-small");
    transcript->createdAt = QDateTime::currentDateTimeUtc();

    // 3. When each word was said. Segments are batched into chunks of up to ~28 s so the aligner
    // sees context on both sides without its quadratic attention blowing up.
    const bool canAlign = options.align && CtcAligner::modelPresent(language);
    if (canAlign && lowMemory())
        whisper.unload();
    CtcAligner &aligner = CtcAligner::instance();
    const bool alignerReady = canAlign && aligner.available(language);
    constexpr TimeUs kChunkMax = 28'000'000;
    constexpr TimeUs kWiden = 300'000;
    int aligned = 0;
    QList<int> wordSegment; // which Whisper segment each word came from
    for (int i = 0; i < segments.size();) {
        int j = i;
        while (j + 1 < segments.size() && segments.at(j + 1).endUs - segments.at(i).startUs <= kChunkMax)
            ++j;
        const TimeUs chunkStart = std::max<TimeUs>(0, segments.at(i).startUs - kWiden);
        const TimeUs chunkEnd = std::min(speechSamplesToUs(pcmIn.size()), segments.at(j).endUs + kWiden);
        QStringList chunkWords;
        for (int k = i; k <= j; ++k)
            chunkWords += segments.at(k).words;

        QList<AlignedWord> placed;
        if (alignerReady && chunkEnd - chunkStart <= kChunkMax + 2 * kWiden) {
            const size_t a = speechUsToSamples(chunkStart);
            const size_t b = std::min(pcmIn.size(), speechUsToSamples(chunkEnd));
            placed = aligner.align(pcmIn.data() + a, b - a, chunkWords, language);
        }

        int w = 0;
        for (int k = i; k <= j; ++k) {
            const Segment &seg = segments.at(k);
            QList<TranscriptWord> fallback = interpolatedWords(seg.words.join(QLatin1Char(' ')), seg.startUs, seg.endUs);
            for (int n = 0; n < seg.words.size(); ++n, ++w) {
                TranscriptWord word = fallback.at(n);
                if (w < placed.size() && placed.at(w).aligned) {
                    word.startUs = chunkStart + placed.at(w).startUs;
                    word.endUs = chunkStart + placed.at(w).endUs;
                    word.confidence = placed.at(w).score;
                    word.interpolated = false;
                    ++aligned;
                }
                word.type = isFillerWord(word.text) ? TranscriptTokenType::Filler : TranscriptTokenType::Word;
                transcript->words.append(word);
                wordSegment.append(k);
            }
        }
        if (!report(0.65 + (options.diarize ? 0.3 : 0.35) * (static_cast<double>(j + 1) / segments.size()),
                    QStringLiteral("Timing words…"))) {
            result.cancelled = true;
            return result;
        }
        i = j + 1;
    }

    // An unplaceable word sits between its placed neighbours rather than wherever Whisper's
    // segment interpolation put it, but never outside its own segment.
    for (int i = 0; i < transcript->words.size(); ++i) {
        TranscriptWord &w = transcript->words[i];
        if (!w.interpolated || aligned == 0)
            continue;
        const Segment &seg = segments.at(wordSegment.at(i));
        const TimeUs lo = i > 0 && wordSegment.at(i - 1) == wordSegment.at(i)
                              ? transcript->words.at(i - 1).endUs
                              : seg.startUs;
        int next = i + 1;
        while (next < transcript->words.size() && transcript->words.at(next).interpolated
               && wordSegment.at(next) == wordSegment.at(i))
            ++next;
        const TimeUs hi = next < transcript->words.size() && wordSegment.at(next) == wordSegment.at(i)
                              ? transcript->words.at(next).startUs
                              : seg.endUs;
        if (hi > lo) {
            const TimeUs share = (hi - lo) / (next - i);
            w.startUs = lo;
            w.endUs = lo + share;
        }
    }

    // Words don't run on into silence: Whisper and CTC both like to stretch a final word.
    if (!speech.isEmpty()) {
        for (TranscriptWord &w : transcript->words) {
            for (const VadRange &r : std::as_const(speech)) {
                if (w.startUs >= r.startUs - 100'000 && w.startUs < r.endUs) {
                    w.endUs = std::min(w.endUs, r.endUs + 100'000);
                    break;
                }
            }
            w.endUs = std::max(w.endUs, w.startUs + 1);
        }
    }

    std::stable_sort(transcript->words.begin(), transcript->words.end(),
                     [](const TranscriptWord &a, const TranscriptWord &b) { return a.startUs < b.startUs; });

    // 4. Who said it.
    if (options.diarize) {
        if (!SpeakerDiarizer::modelPresent()) {
            result.error = QStringLiteral("Speaker labels need the diarize-model addon");
            return result;
        }
        if (lowMemory()) {
            whisper.unload();
            aligner.unload();
        }
        SpeakerDiarizer &diarizer = SpeakerDiarizer::instance();
        if (!diarizer.available()) {
            result.error = diarizer.lastError();
            return result;
        }
        DiarizeParams params;
        params.numSpeakers = options.numSpeakers;
        bool cancelled = false;
        const QList<DiarizeSegment> turns = diarizer.diarize(
            pcmIn, params, [&](double f) { return report(0.95 + 0.05 * f, QStringLiteral("Telling speakers apart…")); },
            &cancelled);
        if (cancelled) {
            result.cancelled = true;
            return result;
        }
        assignSpeakers(*transcript, turns);
    }

    for (TranscriptWord &w : transcript->words) {
        w.startUs += offsetUs;
        w.endUs += offsetUs;
    }
    transcript->wordTimingsAligned = aligned > 0;
    if (transcript->wordTimingsAligned)
        transcript->engine += QStringLiteral("+ctc-") + language;
    if (haveVad)
        transcript->engine += QStringLiteral("+vad");
    result.transcript = transcript;
    report(1.0, QStringLiteral("Done"));
    return result;
}

} // namespace drift
