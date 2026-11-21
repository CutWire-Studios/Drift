#include "app/commandbar/CommandRetriever.h"

#include <QSet>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace drift::commandbar {

namespace {

constexpr double kK1 = 1.5;
constexpr double kB = 0.75;
constexpr double kEpsilon = 0.25;

const QStringList &stopWords()
{
    static const QStringList k = QStringLiteral("a an the to of and or it its this that these those me my in on at for "
                                                "with is are be do does i you please can could would just now then "
                                                "from by as into up so some any")
                                     .split(QLatin1Char(' '));
    return k;
}

struct Syn
{
    const char *phrase;
    const char *terms;
};

// Insertion order of the Python dict (the duplicate 'tempo' key keeps its first position).
const Syn kSyn[] = {
    {"cut", "split"}, {"chop", "split"}, {"slice", "split"}, {"razor", "split"},
    {"louder", "volume"}, {"quieter", "volume"}, {"turn it up", "volume"}, {"mute", "volume"},
    {"loudness", "volume lufs"}, {"slow motion", "speed"}, {"faster", "speed"}, {"speed up", "speed"},
    {"2x", "speed"}, {"half speed", "speed"}, {"backwards", "reverse"}, {"get rid", "delete remove"},
    {"nuke", "delete remove"}, {"shorten", "trim duration"}, {"title", "text"}, {"caption", "text subtitle"},
    {"bigger", "scale transform"}, {"smaller", "scale transform"}, {"zoom", "scale transform"},
    {"opacity", "transform"}, {"rotate", "rotation transform"}, {"tilt", "rotation transform"},
    {"center", "position transform"}, {"corner", "position transform"}, {"mirror", "flip"},
    {"sideways", "orientation"}, {"black and white", "grayscale effect"}, {"blur", "effect"},
    {"glitch", "effect"}, {"vignette", "effect"}, {"grain", "effect"}, {"warmer", "temperature effect"},
    {"saturation", "effect"}, {"contrast", "effect"}, {"brighten", "brightness effect"}, {"grade", "effect"},
    {"dissolve", "transition crossfade"}, {"wipe", "transition"}, {"marker", "bookmark"}, {"jump", "seek"},
    {"go to", "seek playhead"}, {"noise", "denoise"}, {"shaky", "stabilize"}, {"undo", "history"},
    {"render", "export"}, {"gif", "export"}, {"in point", "work area"}, {"subtitles", "generate transcribe"},
    {"scene", "detect shots"}, {"ease", "easing keyframe"}, {"copy", "duplicate"}, {"tempo", "beats"},
    {"silent", "silence"}, {"voiceover", "tts voice"}, {"picture in picture", "transform scale position"},
};

bool isWordChar(QChar c)
{
    const ushort u = c.unicode();
    return (u >= 'a' && u <= 'z') || (u >= '0' && u <= '9');
}

QString stem(const QString &w)
{
    static const char *const suffixes[] = {"ing", "ed", "es", "s"};
    for (const char *suffix : suffixes) {
        const QLatin1String s(suffix);
        if (w.size() > 4 && w.endsWith(s))
            return w.left(w.size() - s.size());
    }
    return w;
}

} // namespace

QStringList CommandRetriever::tokens(const QString &text)
{
    const QString lower = text.toLower();
    QStringList out;
    int i = 0;
    while (i < lower.size()) {
        if (!isWordChar(lower.at(i))) {
            ++i;
            continue;
        }
        int j = i;
        while (j < lower.size() && isWordChar(lower.at(j)))
            ++j;
        const QString word = lower.mid(i, j - i);
        if (!stopWords().contains(word))
            out.append(stem(word));
        i = j;
    }
    return out;
}

QString CommandRetriever::expand(const QString &query)
{
    const QString lower = query.toLower();
    QStringList extra;
    for (const Syn &syn : kSyn) {
        const QString phrase = QString::fromLatin1(syn.phrase);
        for (int at = lower.indexOf(phrase); at >= 0; at = lower.indexOf(phrase, at + 1)) {
            if (at == 0 || !isWordChar(lower.at(at - 1))) {
                extra.append(QString::fromLatin1(syn.terms));
                break;
            }
        }
    }
    return query + QLatin1Char(' ') + extra.join(QLatin1Char(' '));
}

CommandRetriever::CommandRetriever(const QList<CommandOp> &ops)
{
    QHash<QString, int> docFreq;
    QStringList firstSeen;
    long long totalLength = 0;
    for (const CommandOp &op : ops) {
        m_names.append(op.name);
        QString nm = op.name;
        nm.replace(QLatin1Char('_'), QLatin1Char(' '));
        const QStringList doc = tokens(nm + QLatin1Char(' ') + nm + QLatin1Char(' ') + op.toolbox + QLatin1Char(' ')
                                       + op.description);
        m_lengths.append(int(doc.size()));
        totalLength += doc.size();
        QHash<QString, int> freqs;
        for (const QString &w : doc)
            ++freqs[w];
        QSet<QString> inDoc;
        for (const QString &w : doc) {
            if (inDoc.contains(w))
                continue;
            inDoc.insert(w);
            if (!docFreq.contains(w))
                firstSeen.append(w);
            ++docFreq[w];
        }
        m_freqs.append(freqs);
    }
    const double n = double(ops.size());
    m_avgdl = n > 0 ? double(totalLength) / n : 0;

    double idfSum = 0;
    QStringList negative;
    for (const QString &w : firstSeen) {
        const double f = docFreq.value(w);
        const double idf = std::log(n - f + 0.5) - std::log(f + 0.5);
        m_idf.insert(w, idf);
        idfSum += idf;
        if (idf < 0)
            negative.append(w);
    }
    const double eps = kEpsilon * (idfSum / double(m_idf.size()));
    for (const QString &w : negative)
        m_idf[w] = eps;
}

QStringList CommandRetriever::rank(const QString &query) const
{
    const int n = int(m_names.size());
    std::vector<double> score(n, 0.0);
    for (const QString &q : tokens(expand(query))) {
        const double idf = m_idf.value(q, 0.0);
        for (int i = 0; i < n; ++i) {
            const double f = double(m_freqs.at(i).value(q, 0));
            const double norm = kK1 * ((1 - kB) + kB * double(m_lengths.at(i)) / m_avgdl);
            score[i] += idf * (f * (kK1 + 1) / (f + norm));
        }
    }
    std::vector<int> order(n);
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        if (score[a] != score[b])
            return score[a] > score[b];
        return m_names.at(a) < m_names.at(b);
    });
    QStringList out;
    out.reserve(n);
    for (int i : order)
        out.append(m_names.at(i));
    return out;
}

QStringList CommandRetriever::top(const QString &query, int k) const
{
    return rank(query).mid(0, k);
}

} // namespace drift::commandbar
