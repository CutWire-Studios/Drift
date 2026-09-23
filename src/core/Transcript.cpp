#include "Transcript.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

namespace drift {

namespace {

QChar typeCode(TranscriptTokenType type)
{
    switch (type) {
    case TranscriptTokenType::Word: return QLatin1Char('w');
    case TranscriptTokenType::Spacing: return QLatin1Char('s');
    case TranscriptTokenType::AudioEvent: return QLatin1Char('e');
    case TranscriptTokenType::Filler: return QLatin1Char('f');
    }
    return QLatin1Char('w');
}

TranscriptTokenType typeFromCode(QChar c)
{
    switch (c.toLatin1()) {
    case 's': return TranscriptTokenType::Spacing;
    case 'e': return TranscriptTokenType::AudioEvent;
    case 'f': return TranscriptTokenType::Filler;
    default: return TranscriptTokenType::Word;
    }
}

QString stripForMatch(const QString &text)
{
    QString out;
    out.reserve(text.size());
    for (const QChar c : text) {
        if (c.isLetterOrNumber())
            out.append(c.toLower());
    }
    return out;
}

} // namespace

SourceFingerprint SourceFingerprint::of(const QString &path)
{
    SourceFingerprint fp;
    fp.path = path;
    const QFileInfo info(path);
    if (info.exists()) {
        fp.size = info.size();
        fp.mtimeMs = info.lastModified().toMSecsSinceEpoch();
    }
    return fp;
}

bool SourceFingerprint::matches(const QString &otherPath) const
{
    const QFileInfo info(otherPath);
    if (!info.exists())
        return otherPath == path;
    // Size is the strong signal; mtime alone changes on copy/sync, so only a size match with a
    // differing path and mtime still counts as the same media.
    if (info.size() != size)
        return false;
    return otherPath == path || info.lastModified().toMSecsSinceEpoch() == mtimeMs;
}

bool isSpeechToken(const TranscriptWord &word)
{
    return word.type == TranscriptTokenType::Word || word.type == TranscriptTokenType::Filler;
}

QString transcriptTokenTypeToString(TranscriptTokenType type)
{
    switch (type) {
    case TranscriptTokenType::Word: return QStringLiteral("word");
    case TranscriptTokenType::Spacing: return QStringLiteral("spacing");
    case TranscriptTokenType::AudioEvent: return QStringLiteral("event");
    case TranscriptTokenType::Filler: return QStringLiteral("filler");
    }
    return QStringLiteral("word");
}

bool isFillerWord(const QString &text)
{
    static const QSet<QString> fillers{
        QStringLiteral("um"),  QStringLiteral("umm"), QStringLiteral("uh"),  QStringLiteral("uhh"),
        QStringLiteral("erm"), QStringLiteral("er"),  QStringLiteral("ah"),  QStringLiteral("hmm"),
        QStringLiteral("hm"),  QStringLiteral("mm"),  QStringLiteral("mhm"), QStringLiteral("uhm"),
    };
    return fillers.contains(stripForMatch(text));
}

QList<int> Transcript::wordsInRange(TimeUs inUs, TimeUs outUs) const
{
    QList<int> out;
    auto it = std::lower_bound(words.cbegin(), words.cend(), inUs,
                               [](const TranscriptWord &w, TimeUs t) { return w.endUs <= t; });
    // Tokens are sorted by start, not end; a long token can end after a later one starts, so step
    // back over any that still reach into the range.
    while (it != words.cbegin() && std::prev(it)->endUs > inUs)
        --it;
    for (; it != words.cend() && it->startUs < outUs; ++it) {
        if (it->endUs > inUs || (it->endUs == it->startUs && it->startUs >= inUs))
            out.append(static_cast<int>(it - words.cbegin()));
    }
    return out;
}

int Transcript::speechTokenCount() const
{
    return static_cast<int>(std::count_if(words.cbegin(), words.cend(), isSpeechToken));
}

QJsonObject Transcript::toJson() const
{
    // Columns rather than an object per token: an hour of speech is ~10k tokens, and this keeps
    // it to a few hundred KB inside the project file.
    QJsonArray starts, durations, texts, speakerCol, confidences;
    QString kinds;
    kinds.reserve(words.size());
    bool anySpeaker = false;
    bool anyConfidence = false;
    QJsonArray interpolated;
    for (int i = 0; i < words.size(); ++i) {
        const TranscriptWord &w = words.at(i);
        starts.append(static_cast<double>(w.startUs / kUsPerMs));
        durations.append(static_cast<double>((w.endUs - w.startUs) / kUsPerMs));
        texts.append(w.text);
        speakerCol.append(w.speaker);
        anySpeaker |= w.speaker >= 0;
        confidences.append(std::isnan(w.confidence) ? -1.0 : std::round(w.confidence * 1000.0) / 1000.0);
        anyConfidence |= !std::isnan(w.confidence);
        kinds.append(typeCode(w.type));
        if (w.interpolated)
            interpolated.append(i);
    }
    QJsonObject obj{
        {QStringLiteral("engine"), engine},
        {QStringLiteral("language"), language},
        {QStringLiteral("createdAt"), createdAt.toString(Qt::ISODate)},
        {QStringLiteral("sourcePath"), source.path},
        {QStringLiteral("sourceSize"), static_cast<double>(source.size)},
        {QStringLiteral("sourceMtime"), static_cast<double>(source.mtimeMs)},
        {QStringLiteral("aligned"), wordTimingsAligned},
        {QStringLiteral("diarized"), diarized},
        {QStringLiteral("t"), starts},
        {QStringLiteral("d"), durations},
        {QStringLiteral("x"), texts},
        {QStringLiteral("k"), kinds},
    };
    if (anySpeaker)
        obj.insert(QStringLiteral("s"), speakerCol);
    if (anyConfidence)
        obj.insert(QStringLiteral("c"), confidences);
    if (!interpolated.isEmpty())
        obj.insert(QStringLiteral("i"), interpolated);
    if (!speakers.isEmpty()) {
        QJsonArray arr;
        for (const TranscriptSpeaker &s : speakers)
            arr.append(QJsonObject{{QStringLiteral("id"), s.id}, {QStringLiteral("label"), s.label}});
        obj.insert(QStringLiteral("speakers"), arr);
    }
    return obj;
}

TranscriptPtr Transcript::fromJson(const QJsonObject &obj)
{
    const QJsonArray starts = obj.value(QStringLiteral("t")).toArray();
    const QJsonArray durations = obj.value(QStringLiteral("d")).toArray();
    const QJsonArray texts = obj.value(QStringLiteral("x")).toArray();
    const QString kinds = obj.value(QStringLiteral("k")).toString();
    if (starts.size() != durations.size() || starts.size() != texts.size())
        return nullptr;
    const QJsonArray speakerCol = obj.value(QStringLiteral("s")).toArray();
    const QJsonArray confidences = obj.value(QStringLiteral("c")).toArray();

    auto t = std::make_shared<Transcript>();
    t->engine = obj.value(QStringLiteral("engine")).toString();
    t->language = obj.value(QStringLiteral("language")).toString();
    t->createdAt = QDateTime::fromString(obj.value(QStringLiteral("createdAt")).toString(), Qt::ISODate);
    t->source.path = obj.value(QStringLiteral("sourcePath")).toString();
    t->source.size = static_cast<qint64>(obj.value(QStringLiteral("sourceSize")).toDouble());
    t->source.mtimeMs = static_cast<qint64>(obj.value(QStringLiteral("sourceMtime")).toDouble());
    t->wordTimingsAligned = obj.value(QStringLiteral("aligned")).toBool();
    t->diarized = obj.value(QStringLiteral("diarized")).toBool();
    for (const QJsonValue &v : obj.value(QStringLiteral("speakers")).toArray()) {
        const QJsonObject s = v.toObject();
        t->speakers.append({s.value(QStringLiteral("id")).toString(), s.value(QStringLiteral("label")).toString()});
    }
    t->words.reserve(starts.size());
    for (int i = 0; i < starts.size(); ++i) {
        TranscriptWord w;
        w.startUs = static_cast<TimeUs>(starts.at(i).toDouble()) * kUsPerMs;
        w.endUs = w.startUs + static_cast<TimeUs>(durations.at(i).toDouble()) * kUsPerMs;
        w.text = texts.at(i).toString();
        if (i < speakerCol.size())
            w.speaker = static_cast<qint16>(speakerCol.at(i).toInt(-1));
        if (i < confidences.size() && confidences.at(i).toDouble(-1) >= 0)
            w.confidence = static_cast<float>(confidences.at(i).toDouble());
        if (i < kinds.size())
            w.type = typeFromCode(kinds.at(i));
        t->words.append(w);
    }
    for (const QJsonValue &v : obj.value(QStringLiteral("i")).toArray()) {
        const int idx = v.toInt(-1);
        if (idx >= 0 && idx < t->words.size())
            t->words[idx].interpolated = true;
    }
    return t;
}

QList<TranscriptPhrase> packTranscriptPhrases(const Transcript &transcript, TimeUs inUs, TimeUs outUs,
                                              TimeUs breakOnGapUs, int maxWords, bool includeFillers,
                                              bool includeEvents)
{
    QList<TranscriptPhrase> phrases;
    TranscriptPhrase cur;
    int count = 0;
    auto flush = [&] {
        if (cur.firstWord >= 0) {
            cur.text = cur.text.trimmed();
            phrases.append(cur);
        }
        cur = TranscriptPhrase{};
        count = 0;
    };

    for (const int i : transcript.wordsInRange(inUs, outUs)) {
        const TranscriptWord &w = transcript.words.at(i);
        if (w.type == TranscriptTokenType::Spacing)
            continue;
        if (w.type == TranscriptTokenType::AudioEvent) {
            if (!includeEvents)
                continue;
            flush();
            cur = {w.startUs, w.endUs, w.speaker, w.text, i, i};
            flush();
            continue;
        }
        if (w.type == TranscriptTokenType::Filler && !includeFillers)
            continue;
        if (cur.firstWord >= 0
            && (w.startUs - cur.endUs >= breakOnGapUs || w.speaker != cur.speaker
                || (maxWords > 0 && count >= maxWords)))
            flush();
        if (cur.firstWord < 0) {
            cur.startUs = w.startUs;
            cur.speaker = w.speaker;
            cur.firstWord = i;
        }
        cur.endUs = std::max(cur.endUs, w.endUs);
        cur.lastWord = i;
        const QString text = w.text.trimmed();
        if (!cur.text.isEmpty() && !text.isEmpty() && !text.front().isPunct())
            cur.text += QLatin1Char(' ');
        cur.text += text;
        ++count;
    }
    flush();
    return phrases;
}

QList<SubtitleCue> cuesFromTranscript(const Transcript &transcript, TimeUs inUs, TimeUs outUs,
                                      int maxLineWidth, int maxLineCount, int maxWordsPerCue)
{
    // One cue per word, then the existing packer: it re-splits each cue into words, and a
    // single-word cue keeps its measured timing exactly.
    QList<SubtitleCue> perWord;
    for (const int i : transcript.wordsInRange(inUs, outUs)) {
        const TranscriptWord &w = transcript.words.at(i);
        if (w.type != TranscriptTokenType::Word && w.type != TranscriptTokenType::Filler)
            continue;
        const QString text = w.text.trimmed();
        if (text.isEmpty())
            continue;
        SubtitleCue cue;
        cue.startUs = std::max(w.startUs, inUs);
        cue.endUs = std::min(std::max(w.endUs, w.startUs + 1), outUs);
        if (cue.endUs <= cue.startUs)
            continue;
        cue.text = perWord.isEmpty() ? text : QLatin1Char(' ') + text;
        perWord.append(cue);
    }
    return packSubtitleCues(perWord, maxLineWidth, maxLineCount, maxWordsPerCue);
}

} // namespace drift
