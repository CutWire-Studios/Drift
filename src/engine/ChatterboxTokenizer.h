#pragma once

#include <QHash>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <vector>

namespace drift {

// Text front end for Chatterbox Multilingual: Resemble's punctuation clean-up, lowercase + NFKD,
// the per-language rewrites (Cangjie for zh, jamo for ko), then the BPE vocabulary from
// tokenizer.json wrapped in the model's <EXAGGERATION> <s> ... </s> <START_SPEECH> x2 template.
//
// Not ported: pkuseg word segmentation for zh (characters are converted one by one), and
// Russian stress marks.
class ChatterboxTokenizer
{
public:
    // dir holds tokenizer.json and Cangjie5_TC.json.
    bool load(const QString &dir, QString *err);

    // punc_norm -> preprocess -> BPE -> template. Empty input becomes the upstream placeholder
    // sentence rather than an empty prompt.
    std::vector<int64_t> encode(const QString &text, const QString &lang);

    // What reaches the BPE step: lowercased, NFKD, language-rewritten, "[lang]"-prefixed.
    QString preprocess(const QString &text, const QString &lang);

    static QString puncNorm(const QString &text);
    static QString koreanNormalize(const QString &text);

    // Sentence-sized pieces for synthesis. Splits after . ! ? 。！？ ؟ and newlines, then packs
    // neighbouring sentences together up to ~300 characters so short ones are not synthesised
    // (and prosodically reset) one by one.
    static QStringList splitForSynthesis(const QString &text);

private:
    struct Symbol;
    void encodeWord(const QString &word, std::vector<int64_t> &out) const;
    QString cangjie(const QString &text);
    bool loadCangjie();

    QHash<QString, int> vocab;
    QHash<QString, int> mergeRank; // "left right" -> priority, lower merges first
    struct AddedToken
    {
        QString content;
        int id;
    };
    QHash<char16_t, std::vector<AddedToken>> addedByFirst; // longest first
    int unkId = 1;
    std::vector<int64_t> prefix, suffix;

    QString cangjiePath;
    bool cangjieLoaded = false;
    QHash<QString, QString> wordToCj;
    QHash<QString, QStringList> cjToWords;
};

} // namespace drift
