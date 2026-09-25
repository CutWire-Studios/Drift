#include "CtcAligner.h"

#include "GpuPackageParse.h"
#include "OrtSupport.h"
#include "SpeechAudio.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>

#include <onnxruntime_cxx_api.h>

#include <array>
#include <cmath>
#include <limits>

namespace drift {

namespace {

constexpr const char *kConfigFile = "aligner.json";

struct ModelInfo
{
    QString dir;
    QString language;
    QString modelFile = QStringLiteral("model.onnx");
    QHash<QChar, int> vocab;
    int blank = 0;
    int wordSep = -1;
    bool upper = true;
    bool normalize = true;
    int stride = 320;
};

bool readModelInfo(const QString &dir, ModelInfo *out)
{
    QFile cfg(QDir(dir).filePath(QLatin1String(kConfigFile)));
    QFile vocabFile(QDir(dir).filePath(QStringLiteral("vocab.json")));
    if (!cfg.open(QIODevice::ReadOnly) || !vocabFile.open(QIODevice::ReadOnly))
        return false;
    const QJsonObject c = QJsonDocument::fromJson(cfg.readAll()).object();
    ModelInfo info;
    info.dir = dir;
    info.language = c.value(QStringLiteral("language")).toString();
    info.modelFile = c.value(QStringLiteral("model")).toString(info.modelFile);
    info.upper = c.value(QStringLiteral("upper")).toBool(true);
    info.normalize = c.value(QStringLiteral("normalize")).toBool(true);
    info.stride = c.value(QStringLiteral("stride")).toInt(320);
    info.blank = c.value(QStringLiteral("blank")).toInt(0);
    const QString sep = c.value(QStringLiteral("word_sep")).toString();
    const QJsonObject v = QJsonDocument::fromJson(vocabFile.readAll()).object();
    for (auto it = v.begin(); it != v.end(); ++it) {
        if (it.key().size() == 1)
            info.vocab.insert(it.key().at(0), it.value().toInt());
    }
    if (!sep.isEmpty() && v.contains(sep))
        info.wordSep = v.value(sep).toInt();
    if (info.language.isEmpty() || info.vocab.isEmpty()
        || !QFile::exists(QDir(dir).filePath(info.modelFile)))
        return false;
    *out = info;
    return true;
}

// Each root is either a model directory itself, or a directory of per-language model dirs.
QList<ModelInfo> discoverModels()
{
    QList<ModelInfo> found;
    const QStringList roots = GpuPackageParse::defaultSearchPaths(
        QStringLiteral("DRIFT_ALIGN_MODEL_DIR"), QStringLiteral("models/align"), QStringLiteral("align-model"));
    for (const QString &root : roots) {
        ModelInfo info;
        if (readModelInfo(root, &info)) {
            found.append(info);
            continue;
        }
        for (const QString &sub : QDir(root).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            if (readModelInfo(QDir(root).filePath(sub), &info))
                found.append(info);
        }
    }
    return found;
}

const QStringList kOnes{QString(), QStringLiteral("ONE"), QStringLiteral("TWO"), QStringLiteral("THREE"),
                        QStringLiteral("FOUR"), QStringLiteral("FIVE"), QStringLiteral("SIX"),
                        QStringLiteral("SEVEN"), QStringLiteral("EIGHT"), QStringLiteral("NINE"),
                        QStringLiteral("TEN"), QStringLiteral("ELEVEN"), QStringLiteral("TWELVE"),
                        QStringLiteral("THIRTEEN"), QStringLiteral("FOURTEEN"), QStringLiteral("FIFTEEN"),
                        QStringLiteral("SIXTEEN"), QStringLiteral("SEVENTEEN"), QStringLiteral("EIGHTEEN"),
                        QStringLiteral("NINETEEN")};
const QStringList kTens{QString(), QString(), QStringLiteral("TWENTY"), QStringLiteral("THIRTY"),
                        QStringLiteral("FORTY"), QStringLiteral("FIFTY"), QStringLiteral("SIXTY"),
                        QStringLiteral("SEVENTY"), QStringLiteral("EIGHTY"), QStringLiteral("NINETY")};

QString underThousand(int n)
{
    QString out;
    if (n >= 100) {
        out = kOnes.at(n / 100) + QStringLiteral(" HUNDRED");
        n %= 100;
    }
    if (n >= 20) {
        out += (out.isEmpty() ? QString() : QStringLiteral(" ")) + kTens.at(n / 10);
        n %= 10;
    }
    if (n > 0)
        out += (out.isEmpty() ? QString() : QStringLiteral(" ")) + kOnes.at(n);
    return out;
}

// How an English speaker says the number: years as two pairs, the rest as a cardinal.
QString spellEnglishNumber(qint64 n)
{
    if (n == 0)
        return QStringLiteral("ZERO");
    if (n >= 1100 && n <= 2099 && n % 100 != 0 && n / 100 != 20)
        return underThousand(static_cast<int>(n / 100)) + QLatin1Char(' ') + underThousand(static_cast<int>(n % 100));
    QStringList parts;
    const std::array<std::pair<qint64, const char *>, 3> scales{
        {{1'000'000'000, "BILLION"}, {1'000'000, "MILLION"}, {1'000, "THOUSAND"}}};
    for (const auto &[size, name] : scales) {
        if (n >= size) {
            parts.append(underThousand(static_cast<int>(n / size)) + QLatin1Char(' ') + QLatin1String(name));
            n %= size;
        }
    }
    if (n > 0)
        parts.append(underThousand(static_cast<int>(n)));
    return parts.join(QLatin1Char(' '));
}

} // namespace

QString normalizeForAligner(const QString &word, const QHash<QChar, int> &vocab, bool upper,
                            const QString &language)
{
    QString text = word;
    if (language == QLatin1String("en")) {
        static const QRegularExpression digits(QStringLiteral("\\d+"));
        QString spelled;
        qsizetype last = 0;
        auto it = digits.globalMatch(text);
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            spelled += text.mid(last, m.capturedStart() - last);
            bool ok = false;
            const qint64 n = m.captured().toLongLong(&ok);
            spelled += QLatin1Char(' ')
                       + (ok && m.captured().size() <= 12 ? spellEnglishNumber(n) : m.captured())
                       + QLatin1Char(' ');
            last = m.capturedEnd();
        }
        spelled += text.mid(last);
        text = spelled;
        text.replace(QLatin1Char('%'), QStringLiteral(" PERCENT "));
        text.replace(QLatin1Char('&'), QStringLiteral(" AND "));
        text.replace(QLatin1Char('+'), QStringLiteral(" PLUS "));
        text.replace(QLatin1Char('@'), QStringLiteral(" AT "));
    }
    // Strip accents: é -> e, so Latin-script loanwords still align in an English model.
    text = text.normalized(QString::NormalizationForm_KD);
    QString out;
    for (QChar c : std::as_const(text)) {
        if (c.category() == QChar::Mark_NonSpacing)
            continue;
        if (c == QChar(0x2019))
            c = QLatin1Char('\'');
        if (upper)
            c = c.toUpper();
        if (vocab.contains(c))
            out.append(c);
        else if (c.isSpace() && !out.isEmpty() && !out.endsWith(QLatin1Char(' ')))
            out.append(QLatin1Char(' '));
    }
    return out.trimmed();
}

QList<QPair<int, int>> ctcForcedAlign(const float *lp, int frames, int vocab, const std::vector<int> &tokens,
                                      int blank)
{
    const int L = static_cast<int>(tokens.size());
    if (L == 0 || frames <= 0)
        return {};
    // Repeated labels need a blank between them, so the path is at least this long.
    int needed = L;
    for (int k = 1; k < L; ++k)
        needed += tokens[k] == tokens[k - 1] ? 1 : 0;
    if (frames < needed)
        return {};

    const int S = 2 * L + 1;
    auto label = [&](int s) { return (s % 2 == 0) ? blank : tokens[s / 2]; };
    constexpr float kNeg = -std::numeric_limits<float>::infinity();
    std::vector<float> prev(S, kNeg), cur(S, kNeg);
    // 0 = stay, 1 = from s-1, 2 = from s-2
    std::vector<quint8> back(static_cast<size_t>(frames) * S, 0);

    prev[0] = lp[blank];
    prev[1] = lp[label(1)];
    for (int t = 1; t < frames; ++t) {
        const float *row = lp + static_cast<size_t>(t) * vocab;
        // States that can't be reached yet (too few frames so far) or can no longer finish stay -inf.
        const int sMin = std::max(0, S - 2 - 2 * (frames - 1 - t));
        const int sMax = std::min(S - 1, 2 * t + 1);
        std::fill(cur.begin(), cur.end(), kNeg);
        for (int s = sMin; s <= sMax; ++s) {
            float best = prev[s];
            quint8 from = 0;
            if (s >= 1 && prev[s - 1] > best) {
                best = prev[s - 1];
                from = 1;
            }
            if (s >= 2 && label(s) != blank && label(s) != label(s - 2) && prev[s - 2] > best) {
                best = prev[s - 2];
                from = 2;
            }
            if (best == kNeg)
                continue;
            cur[s] = best + row[label(s)];
            back[static_cast<size_t>(t) * S + s] = from;
        }
        std::swap(prev, cur);
    }

    int s = prev[S - 1] >= prev[S - 2] ? S - 1 : S - 2;
    if (prev[s] == kNeg)
        return {};
    QList<QPair<int, int>> spans(L, {-1, -1});
    for (int t = frames - 1; t >= 0; --t) {
        if (s % 2 == 1) {
            QPair<int, int> &span = spans[s / 2];
            span.first = t;
            if (span.second < 0)
                span.second = t;
        }
        if (t > 0)
            s -= back[static_cast<size_t>(t) * S + s];
    }
    for (const auto &span : std::as_const(spans)) {
        if (span.first < 0)
            return {};
    }
    return spans;
}

struct CtcAligner::Impl
{
    QMutex mutex;
    QString error;
    ModelInfo info;
    std::unique_ptr<Ort::Session> session;
    std::string inName;
    std::string outName;

    bool ensureLoaded(const QString &language);
};

bool CtcAligner::Impl::ensureLoaded(const QString &language)
{
    if (session && info.language == language)
        return true;
    session.reset();
    if (!ort::ensureLoaded(&error))
        return false;
    bool found = false;
    for (const ModelInfo &m : discoverModels()) {
        if (m.language == language) {
            info = m;
            found = true;
            break;
        }
    }
    if (!found) {
        error = QStringLiteral("No word alignment model for \"%1\". Install one from the Addon Manager.")
                    .arg(language);
        return false;
    }
    const auto path = ort::ortPath(QDir(info.dir).filePath(info.modelFile));
    if (!ort::buildSessions(ort::env(), "align", false, &error, [&](Ort::SessionOptions &opts) {
            session = std::make_unique<Ort::Session>(ort::env(), path.c_str(), opts);
        }))
        return false;
    const auto ins = ort::sessionNames(*session, true);
    const auto outs = ort::sessionNames(*session, false);
    if (ins.empty() || outs.empty()) {
        error = QStringLiteral("alignment model has no inputs/outputs");
        session.reset();
        return false;
    }
    inName = ins.front();
    outName = outs.front();
    return true;
}

CtcAligner::CtcAligner() : d(std::make_unique<Impl>()) {}
CtcAligner::~CtcAligner() = default;

CtcAligner &CtcAligner::instance()
{
    static CtcAligner inst;
    return inst;
}

QStringList CtcAligner::installedLanguages()
{
    QStringList langs;
    for (const ModelInfo &m : discoverModels()) {
        if (!langs.contains(m.language))
            langs.append(m.language);
    }
    return langs;
}

bool CtcAligner::modelPresent(const QString &language)
{
    return installedLanguages().contains(language);
}

bool CtcAligner::available(const QString &language)
{
    QMutexLocker lock(&d->mutex);
    return d->ensureLoaded(language);
}

QString CtcAligner::lastError() const
{
    return d->error;
}

void CtcAligner::unload()
{
    QMutexLocker lock(&d->mutex);
    d->session.reset();
}

QList<AlignedWord> CtcAligner::align(const float *pcm, size_t samples, const QStringList &words,
                                     const QString &language)
{
    QList<AlignedWord> result(words.size());
    QMutexLocker lock(&d->mutex);
    if (words.isEmpty() || samples < 400 || !d->ensureLoaded(language))
        return result;
    const ModelInfo &info = d->info;

    // Target sequence: each word's characters, joined by the word separator when the vocab has one.
    std::vector<int> tokens;
    QList<QPair<int, int>> wordTokens; // [first, last] token index per word, or {-1,-1}
    for (const QString &word : words) {
        const QString norm = normalizeForAligner(word, info.vocab, info.upper, language);
        if (norm.isEmpty()) {
            wordTokens.append({-1, -1});
            continue;
        }
        if (!tokens.empty() && info.wordSep >= 0)
            tokens.push_back(info.wordSep);
        const int first = static_cast<int>(tokens.size());
        for (const QChar c : norm) {
            if (c == QLatin1Char(' ')) {
                if (info.wordSep >= 0)
                    tokens.push_back(info.wordSep);
            } else {
                tokens.push_back(info.vocab.value(c));
            }
        }
        wordTokens.append({first, static_cast<int>(tokens.size()) - 1});
    }
    if (tokens.empty())
        return result;

    std::vector<float> input(pcm, pcm + samples);
    if (info.normalize) {
        double mean = 0.0;
        for (float v : input)
            mean += v;
        mean /= static_cast<double>(input.size());
        double var = 0.0;
        for (float v : input)
            var += (v - mean) * (v - mean);
        const double scale = 1.0 / std::sqrt(var / static_cast<double>(input.size()) + 1e-7);
        for (float &v : input)
            v = static_cast<float>((v - mean) * scale);
    }

    std::vector<Ort::Value> outputs;
    try {
        const std::array<int64_t, 2> shape{1, static_cast<int64_t>(input.size())};
        Ort::Value in = Ort::Value::CreateTensor<float>(ort::cpuMemory(), input.data(), input.size(),
                                                        shape.data(), shape.size());
        const char *inN = d->inName.c_str();
        const char *outN = d->outName.c_str();
        outputs = d->session->Run(Ort::RunOptions{nullptr}, &inN, &in, 1, &outN, 1);
    } catch (const Ort::Exception &e) {
        d->error = QString::fromUtf8(e.what());
        qWarning() << "[align] inference failed:" << d->error;
        return result;
    }
    const auto shape = outputs[0].GetTensorTypeAndShapeInfo().GetShape();
    if (shape.size() != 3)
        return result;
    const int frames = static_cast<int>(shape[1]);
    const int vocab = static_cast<int>(shape[2]);
    std::vector<float> lp(outputs[0].GetTensorData<float>(),
                          outputs[0].GetTensorData<float>() + static_cast<size_t>(frames) * vocab);
    for (int t = 0; t < frames; ++t) {
        float *row = lp.data() + static_cast<size_t>(t) * vocab;
        const float mx = *std::max_element(row, row + vocab);
        double sum = 0.0;
        for (int v = 0; v < vocab; ++v)
            sum += std::exp(row[v] - mx);
        const float lse = mx + static_cast<float>(std::log(sum));
        for (int v = 0; v < vocab; ++v)
            row[v] -= lse;
    }

    const QList<QPair<int, int>> spans = ctcForcedAlign(lp.data(), frames, vocab, tokens, info.blank);
    if (spans.isEmpty())
        return result;

    // Frame index to time from the actual frame rate of this run, not a nominal stride.
    const double usPerFrame = static_cast<double>(speechSamplesToUs(samples)) / frames;
    for (int w = 0; w < words.size(); ++w) {
        const QPair<int, int> tk = wordTokens.at(w);
        if (tk.first < 0)
            continue;
        int f0 = spans.at(tk.first).first;
        int f1 = spans.at(tk.second).second;
        double score = 0.0;
        int n = 0;
        for (int k = tk.first; k <= tk.second; ++k) {
            if (tokens[k] == info.wordSep)
                continue;
            for (int t = spans.at(k).first; t <= spans.at(k).second; ++t) {
                score += std::exp(lp[static_cast<size_t>(t) * vocab + tokens[k]]);
                ++n;
            }
        }
        AlignedWord &out = result[w];
        out.startUs = static_cast<TimeUs>(f0 * usPerFrame);
        out.endUs = static_cast<TimeUs>((f1 + 1) * usPerFrame);
        out.score = n > 0 ? static_cast<float>(score / n) : 0.0f;
        out.aligned = true;
    }
    return result;
}

} // namespace drift
