#include "ChatterboxTokenizer.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include <algorithm>
#include <climits>

namespace drift {

namespace {

constexpr int kMaxChunkChars = 300;

// The Whisper-style pre-tokenizer in tokenizer.json is the Rust regex \w+|[^\w\s]+. Rust's \w
// counts combining marks, which matters for Devanagari and Arabic vowel signs; PCRE's does not.
bool isWordChar(char32_t c)
{
    switch (QChar::category(c)) {
    case QChar::Letter_Uppercase:
    case QChar::Letter_Lowercase:
    case QChar::Letter_Titlecase:
    case QChar::Letter_Modifier:
    case QChar::Letter_Other:
    case QChar::Mark_NonSpacing:
    case QChar::Mark_SpacingCombining:
    case QChar::Mark_Enclosing:
    case QChar::Number_DecimalDigit:
    case QChar::Number_Letter:
    case QChar::Punctuation_Connector:
        return true;
    default:
        return c == 0x200C || c == 0x200D;
    }
}

QStringList codePoints(const QString &s)
{
    QStringList out;
    const QList<uint> ucs4 = s.toUcs4();
    out.reserve(ucs4.size());
    for (const uint c : ucs4)
        out.push_back(QString::fromUcs4(reinterpret_cast<const char32_t *>(&c), 1));
    return out;
}

bool isSentenceEnd(QChar c)
{
    const ushort u = c.unicode();
    return u == '.' || u == '!' || u == '?' || u == 0x3002 || u == 0xFF01 || u == 0xFF1F
        || u == 0x061F || u == 0x2026 || u == 0xFF0E;
}

bool isCjkEnd(QChar c)
{
    const ushort u = c.unicode();
    return u == 0x3002 || u == 0xFF01 || u == 0xFF1F;
}

bool isCloser(QChar c)
{
    const ushort u = c.unicode();
    return u == '"' || u == '\'' || u == ')' || u == ']' || u == 0x201D || u == 0x2019
        || u == 0x300D || u == 0x300F || u == 0xFF09 || u == 0xBB;
}

} // namespace

bool ChatterboxTokenizer::load(const QString &dir, QString *err)
{
    QFile f(QDir(dir).filePath(QStringLiteral("tokenizer.json")));
    if (!f.open(QIODevice::ReadOnly)) {
        if (err)
            *err = QStringLiteral("Cannot read tokenizer.json");
        return false;
    }
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    const QJsonObject model = root.value(QStringLiteral("model")).toObject();
    const QJsonObject vocabObj = model.value(QStringLiteral("vocab")).toObject();
    if (vocabObj.isEmpty()) {
        if (err)
            *err = QStringLiteral("tokenizer.json has no BPE vocabulary");
        return false;
    }

    vocab.clear();
    vocab.reserve(vocabObj.size());
    for (auto it = vocabObj.begin(); it != vocabObj.end(); ++it)
        vocab.insert(it.key(), it.value().toInt());

    mergeRank.clear();
    const QJsonArray merges = model.value(QStringLiteral("merges")).toArray();
    for (int i = 0; i < merges.size(); ++i)
        mergeRank.insert(merges.at(i).toString(), i);

    const QString unk = model.value(QStringLiteral("unk_token")).toString();
    unkId = vocab.value(unk, 1);

    addedByFirst.clear();
    for (const QJsonValue &v : root.value(QStringLiteral("added_tokens")).toArray()) {
        const QJsonObject o = v.toObject();
        const QString content = o.value(QStringLiteral("content")).toString();
        if (content.isEmpty())
            continue;
        addedByFirst[content.at(0).unicode()].push_back({content, o.value(QStringLiteral("id")).toInt()});
    }
    for (auto &list : addedByFirst) {
        std::sort(list.begin(), list.end(), [](const AddedToken &a, const AddedToken &b) {
            return a.content.size() > b.content.size();
        });
    }

    const QJsonObject post = root.value(QStringLiteral("post_processor")).toObject();
    const QJsonObject specials = post.value(QStringLiteral("special_tokens")).toObject();
    prefix.clear();
    suffix.clear();
    bool seenSequence = false;
    for (const QJsonValue &v : post.value(QStringLiteral("single")).toArray()) {
        const QJsonObject o = v.toObject();
        if (o.contains(QStringLiteral("Sequence"))) {
            seenSequence = true;
            continue;
        }
        const QString name = o.value(QStringLiteral("SpecialToken")).toObject()
                                 .value(QStringLiteral("id")).toString();
        for (const QJsonValue &id : specials.value(name).toObject().value(QStringLiteral("ids")).toArray())
            (seenSequence ? suffix : prefix).push_back(id.toInt());
    }
    if (prefix.empty() || suffix.empty()) {
        if (err)
            *err = QStringLiteral("tokenizer.json has no usable post-processor template");
        return false;
    }

    cangjiePath = QDir(dir).filePath(QStringLiteral("Cangjie5_TC.json"));
    cangjieLoaded = false;
    wordToCj.clear();
    cjToWords.clear();
    return true;
}

bool ChatterboxTokenizer::loadCangjie()
{
    if (cangjieLoaded)
        return !wordToCj.isEmpty();
    cangjieLoaded = true;
    QFile f(cangjiePath);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    for (const QJsonValue &v : QJsonDocument::fromJson(f.readAll()).array()) {
        const QStringList parts = v.toString().split(QLatin1Char('\t'));
        if (parts.size() < 2)
            continue;
        wordToCj.insert(parts.at(0), parts.at(1));
        cjToWords[parts.at(1)].push_back(parts.at(0));
    }
    return !wordToCj.isEmpty();
}

QString ChatterboxTokenizer::cangjie(const QString &text)
{
    if (!loadCangjie())
        return text;
    QString out;
    for (const QString &ch : codePoints(text)) {
        if (QChar::category(ch.toUcs4().first()) != QChar::Letter_Other) {
            out += ch;
            continue;
        }
        const auto it = wordToCj.constFind(ch);
        if (it == wordToCj.cend()) {
            out += ch;
            continue;
        }
        QString code = it.value();
        const int index = cjToWords.value(code).indexOf(ch);
        if (index > 0)
            code += QString::number(index);
        for (const QChar c : code)
            out += QStringLiteral("[cj_") + c + QLatin1Char(']');
        out += QStringLiteral("[cj_.]");
    }
    return out;
}

QString ChatterboxTokenizer::koreanNormalize(const QString &text)
{
    QString out;
    out.reserve(text.size() * 2);
    for (const QChar c : text) {
        const int u = c.unicode();
        if (u < 0xAC00 || u > 0xD7AF) {
            out += c;
            continue;
        }
        const int base = u - 0xAC00;
        out += QChar(0x1100 + base / (21 * 28));
        out += QChar(0x1161 + (base % (21 * 28)) / 28);
        if (base % 28 > 0)
            out += QChar(0x11A7 + base % 28);
    }
    return out.trimmed();
}

QString ChatterboxTokenizer::puncNorm(const QString &input)
{
    if (input.isEmpty())
        return QStringLiteral("You need to add some text for me to talk.");

    QString text = input;
    if (text.at(0).isLower())
        text[0] = text.at(0).toUpper();

    text = text.simplified();

    static const std::pair<const char16_t *, const char16_t *> replacements[] = {
        {u"...", u", "}, {u"…", u", "}, {u":", u","}, {u" - ", u", "}, {u";", u", "},
        {u"—", u"-"}, {u"–", u"-"}, {u" ,", u","}, {u"“", u"\""},
        {u"”", u"\""}, {u"‘", u"'"}, {u"’", u"'"},
    };
    for (const auto &[from, to] : replacements)
        text.replace(QString::fromUtf16(from), QString::fromUtf16(to));

    while (text.endsWith(QLatin1Char(' ')))
        text.chop(1);

    static const QString enders = QString::fromUtf16(u".!?-,、，。？！");
    if (text.isEmpty() || !enders.contains(text.back()))
        text += QLatin1Char('.');
    return text;
}

QString ChatterboxTokenizer::preprocess(const QString &text, const QString &lang)
{
    QString txt = text.toLower().normalized(QString::NormalizationForm_KD);
    if (lang == QLatin1String("zh"))
        txt = cangjie(txt);
    else if (lang == QLatin1String("ko"))
        txt = koreanNormalize(txt);
    if (!lang.isEmpty())
        txt = QLatin1Char('[') + lang.toLower() + QLatin1Char(']') + txt;
    return txt;
}

void ChatterboxTokenizer::encodeWord(const QString &word, std::vector<int64_t> &out) const
{
    struct Sym
    {
        QString text;
        bool unk;
    };
    std::vector<Sym> syms;
    for (const QString &ch : codePoints(word))
        syms.push_back({ch, !vocab.contains(ch)});

    for (;;) {
        int best = -1;
        int bestRank = INT_MAX;
        for (size_t i = 0; i + 1 < syms.size(); ++i) {
            if (syms[i].unk || syms[i + 1].unk)
                continue;
            const auto it = mergeRank.constFind(syms[i].text + QLatin1Char(' ') + syms[i + 1].text);
            if (it != mergeRank.cend() && it.value() < bestRank) {
                bestRank = it.value();
                best = static_cast<int>(i);
            }
        }
        if (best < 0)
            break;
        syms[best].text += syms[best + 1].text;
        syms.erase(syms.begin() + best + 1);
    }

    for (const Sym &s : syms)
        out.push_back(s.unk ? unkId : vocab.value(s.text, unkId));
}

std::vector<int64_t> ChatterboxTokenizer::encode(const QString &text, const QString &lang)
{
    QString txt = preprocess(puncNorm(text), lang);
    txt.replace(QLatin1Char(' '), QStringLiteral("[SPACE]"));

    std::vector<int64_t> ids = prefix;
    QString pending;
    auto flush = [&] {
        qsizetype i = 0;
        const QList<uint> ucs4 = pending.toUcs4();
        const qsizetype n = ucs4.size();
        auto wordChar = [&](qsizetype k) { return isWordChar(ucs4.at(k)); };
        auto space = [&](qsizetype k) { return QChar::isSpace(ucs4.at(k)); };
        while (i < n) {
            if (space(i)) {
                ++i;
                continue;
            }
            const bool word = wordChar(i);
            qsizetype j = i;
            while (j < n && !space(j) && wordChar(j) == word)
                ++j;
            encodeWord(QString::fromUcs4(reinterpret_cast<const char32_t *>(ucs4.constData() + i), j - i), ids);
            i = j;
        }
        pending.clear();
    };

    for (qsizetype i = 0; i < txt.size();) {
        const AddedToken *hit = nullptr;
        const auto it = addedByFirst.constFind(txt.at(i).unicode());
        if (it != addedByFirst.cend()) {
            for (const AddedToken &t : it.value()) {
                if (QStringView(txt).mid(i, t.content.size()) == t.content) {
                    hit = &t;
                    break;
                }
            }
        }
        if (hit) {
            flush();
            ids.push_back(hit->id);
            i += hit->content.size();
        } else {
            pending += txt.at(i++);
        }
    }
    flush();

    ids.insert(ids.end(), suffix.begin(), suffix.end());
    return ids;
}

QStringList ChatterboxTokenizer::splitForSynthesis(const QString &text)
{
    QStringList sentences;
    QString cur;
    const qsizetype n = text.size();
    for (qsizetype i = 0; i < n; ++i) {
        const QChar c = text.at(i);
        if (c == QLatin1Char('\n')) {
            sentences << cur;
            cur.clear();
            continue;
        }
        cur += c;
        if (!isSentenceEnd(c))
            continue;
        // "3.5" and "e.g." stay whole: a Latin ender only counts before whitespace or the end.
        while (i + 1 < n && (isSentenceEnd(text.at(i + 1)) || isCloser(text.at(i + 1))))
            cur += text.at(++i);
        const bool atEnd = i + 1 >= n;
        if (atEnd || text.at(i + 1).isSpace() || isCjkEnd(c)) {
            sentences << cur;
            cur.clear();
        }
    }
    sentences << cur;

    // A single run longer than the budget is cut at its last comma or space within it.
    QStringList pieces;
    for (QString s : std::as_const(sentences)) {
        s = s.trimmed();
        while (s.size() > kMaxChunkChars) {
            qsizetype cut = s.lastIndexOf(QRegularExpression(QStringLiteral("[,;、， ]")),
                                          kMaxChunkChars);
            if (cut < kMaxChunkChars / 3)
                cut = kMaxChunkChars;
            else
                ++cut;
            pieces << s.left(cut).trimmed();
            s = s.mid(cut).trimmed();
        }
        if (!s.isEmpty())
            pieces << s;
    }

    QStringList out;
    QString cur2;
    for (const QString &p : std::as_const(pieces)) {
        if (!cur2.isEmpty() && cur2.size() + 1 + p.size() > kMaxChunkChars) {
            out << cur2;
            cur2.clear();
        }
        if (!cur2.isEmpty())
            cur2 += QLatin1Char(' ');
        cur2 += p;
    }
    if (!cur2.isEmpty())
        out << cur2;
    return out;
}

} // namespace drift
