#include "SubtitleCue.h"

#include <QCoreApplication>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>

namespace drift {

QString subtitleClipName(const QList<SubtitleCue> &cues)
{
    if (cues.isEmpty())
        return QCoreApplication::translate("SubtitleCue", "Subtitles");
    return QCoreApplication::translate("SubtitleCue", "Subtitles (%1)").arg(cues.size());
}

namespace {

struct TimedWord
{
    TimeUs startUs = 0;
    TimeUs endUs = 0;
    QString word; // may include a leading space (Whisper-style)
};

// Split "Hello world. How are you?" into ["Hello", " world.", " How", " are", " you?"] so
// joining reproduces the original spacing/punctuation.
QStringList tokenizeWords(const QString &text)
{
    static const QRegularExpression re(QStringLiteral(R"((\s*\S+))"));
    QStringList tokens;
    auto it = re.globalMatch(text);
    while (it.hasNext())
        tokens.append(it.next().captured(1));
    return tokens;
}

QList<TimedWord> wordsFromCue(const SubtitleCue &cue)
{
    const QString text = cue.text;
    const QStringList tokens = tokenizeWords(text);
    QList<TimedWord> words;
    if (tokens.isEmpty())
        return words;

    int totalWeight = 0;
    QList<int> weights;
    weights.reserve(tokens.size());
    for (const QString &tok : tokens) {
        const int w = std::max(1, static_cast<int>(tok.trimmed().size()));
        weights.append(w);
        totalWeight += w;
    }

    const TimeUs span = std::max<TimeUs>(1, cue.endUs - cue.startUs);
    TimeUs cursor = cue.startUs;
    for (int i = 0; i < tokens.size(); ++i) {
        TimedWord tw;
        tw.word = tokens.at(i);
        tw.startUs = cursor;
        if (i + 1 == tokens.size()) {
            tw.endUs = cue.endUs;
        } else {
            const TimeUs dur =
                static_cast<TimeUs>((static_cast<double>(weights.at(i)) / totalWeight) * span);
            tw.endUs = std::min(cue.endUs, cursor + std::max<TimeUs>(1, dur));
        }
        if (tw.endUs <= tw.startUs)
            tw.endUs = tw.startUs + 1;
        cursor = tw.endUs;
        words.append(tw);
    }
    if (!words.isEmpty())
        words.last().endUs = cue.endUs;
    return words;
}

QList<TimedWord> flattenWords(const QList<SubtitleCue> &cues)
{
    QList<TimedWord> all;
    for (const SubtitleCue &cue : cues) {
        const QString trimmed = cue.text.trimmed();
        if (trimmed.isEmpty() || cue.endUs <= cue.startUs)
            continue;
        all += wordsFromCue(cue);
    }
    return all;
}

} // namespace

int activeWordIndexAt(const QString &text, TimeUs startUs, TimeUs endUs, TimeUs localUs)
{
    if (endUs <= startUs || localUs < startUs)
        return -1;

    SubtitleCue cue;
    cue.startUs = startUs;
    cue.endUs = endUs;
    cue.text = text;
    const QList<TimedWord> words = wordsFromCue(cue);
    for (int i = 0; i < words.size(); ++i) {
        if (localUs < words.at(i).endUs)
            return i;
    }
    // Past the last word's end (rounding, or the window overrunning the text): keep it lit.
    return words.isEmpty() ? -1 : words.size() - 1;
}

const SubtitleCue *activeSubtitleCueAt(const QList<SubtitleCue> &cues, TimeUs localUs)
{
    for (const SubtitleCue &cue : cues) {
        if (localUs >= cue.startUs && localUs < cue.endUs)
            return &cue;
    }
    return nullptr;
}

int subtitleCueIndexAt(const QList<SubtitleCue> &cues, TimeUs localUs)
{
    for (int i = 0; i < cues.size(); ++i) {
        const SubtitleCue &cue = cues.at(i);
        if (localUs >= cue.startUs && localUs < cue.endUs)
            return i;
    }
    return -1;
}

void sortSubtitleCues(QList<SubtitleCue> &cues)
{
    std::sort(cues.begin(), cues.end(), [](const SubtitleCue &a, const SubtitleCue &b) {
        if (a.startUs != b.startUs)
            return a.startUs < b.startUs;
        return a.endUs < b.endUs;
    });
}

QList<SubtitleCue> packSubtitleCues(const QList<SubtitleCue> &cues, int maxLineWidth,
                                    int maxLineCount)
{
    if (cues.isEmpty())
        return {};

    // Mirror openai-whisper SubtitlesWriter: packing only applies when both limits are set.
    if (maxLineWidth <= 0 || maxLineCount <= 0)
        return cues;

    const QList<TimedWord> words = flattenWords(cues);
    if (words.isEmpty())
        return {};

    constexpr TimeUs kLongPauseUs = 3 * kUsPerSecond;

    QList<SubtitleCue> packed;
    QList<TimedWord> subtitle;
    int lineLen = 0;
    int lineCount = 1;
    TimeUs lastStart = words.first().startUs;

    auto flush = [&]() {
        if (subtitle.isEmpty())
            return;
        SubtitleCue cue;
        cue.startUs = subtitle.first().startUs;
        cue.endUs = subtitle.last().endUs;
        QString text;
        for (const TimedWord &w : subtitle)
            text += w.word;
        cue.text = text.trimmed().replace(QLatin1Char('\n'), QLatin1Char(' '));
        if (!cue.text.isEmpty() && cue.endUs > cue.startUs)
            packed.append(cue);
        subtitle.clear();
        lineLen = 0;
        lineCount = 1;
    };

    for (TimedWord timing : words) {
        const bool longPause = timing.startUs - lastStart > kLongPauseUs;
        const bool hasRoom = lineLen + timing.word.size() <= maxLineWidth;

        if (lineLen > 0 && hasRoom && !longPause) {
            lineLen += timing.word.size();
            subtitle.append(timing);
        } else {
            timing.word = timing.word.trimmed();
            if (!subtitle.isEmpty() && (longPause || lineCount >= maxLineCount)) {
                flush();
            } else if (lineLen > 0) {
                ++lineCount;
                timing.word = QLatin1Char('\n') + timing.word;
            }
            lineLen = timing.word.trimmed().size();
            subtitle.append(timing);
        }
        lastStart = timing.startUs;
    }
    flush();

    sortSubtitleCues(packed);
    return packed;
}

} // namespace drift
