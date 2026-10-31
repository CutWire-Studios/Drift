#include "engine/ml/audio/ClapTagger.h"

#include "engine/addons/PackageSearchPaths.h"
#include "engine/audio/analysis/RealFft.h"
#include "engine/ml/runtime/OrtSupport.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>

#include <onnxruntime_cxx_api.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

namespace drift {

namespace {

constexpr const char *kModelFile = "clap_audio.onnx";
constexpr const char *kVocabFile = "vocab.json";
constexpr const char *kEmbeddingsFile = "text_embeddings.bin";
constexpr int kDim = 512;
constexpr int kBatch = 4;
constexpr qint64 kMinSamples = 480;

const char *const kCategoryNames[kClapSectionCategories] = {"music_genre", "music_mood", "instruments", "ambience", "voice"};

QString resolveModelDir()
{
    const QStringList roots = PackageSearchPaths::defaultSearchPaths(
        QStringLiteral("DRIFT_AUDIO_CLAP_MODEL_DIR"), QStringLiteral("models/audio-clap"),
        QStringLiteral("audio-tag-model"));
    for (const QString &root : roots) {
        const QDir dir(root);
        if (QFile::exists(dir.filePath(QLatin1String(kModelFile)))
            && QFile::exists(dir.filePath(QLatin1String(kVocabFile)))
            && QFile::exists(dir.filePath(QLatin1String(kEmbeddingsFile)))) {
            return root;
        }
    }
    return {};
}

float dot(const float *a, const float *b)
{
    double acc = 0.0;
    for (int i = 0; i < kDim; ++i)
        acc += double(a[i]) * double(b[i]);
    return static_cast<float>(acc);
}

ClapEmbedding normalised(const std::vector<double> &sum)
{
    double n = 0.0;
    for (double v : sum)
        n += v * v;
    n = std::sqrt(n) + 1e-12;
    ClapEmbedding out{};
    for (int i = 0; i < kDim; ++i)
        out[static_cast<size_t>(i)] = static_cast<float>(sum[static_cast<size_t>(i)] / n);
    return out;
}

// Population z-scores in place; `d` is replaced by (d - mean) / (std + 1e-9).
void zScore(std::vector<float> &d)
{
    double mean = 0.0;
    for (float v : d)
        mean += v;
    mean /= double(d.size());
    double var = 0.0;
    for (float v : d)
        var += (v - mean) * (v - mean);
    const double sd = std::sqrt(var / double(d.size())) + 1e-9;
    for (float &v : d)
        v = static_cast<float>((v - mean) / sd);
}

QString normalisedTerm(QString text)
{
    text = text.toLower().simplified();
    for (const QLatin1String article : {QLatin1String("a "), QLatin1String("an "), QLatin1String("the ")}) {
        if (text.startsWith(article)) {
            text.remove(0, article.size());
            break;
        }
    }
    return text;
}

int editDistance(const QString &a, const QString &b)
{
    std::vector<int> prev(static_cast<size_t>(b.size()) + 1), cur(prev.size());
    for (size_t j = 0; j < prev.size(); ++j)
        prev[j] = static_cast<int>(j);
    for (int i = 1; i <= a.size(); ++i) {
        cur[0] = i;
        for (int j = 1; j <= b.size(); ++j) {
            cur[static_cast<size_t>(j)] = std::min({prev[static_cast<size_t>(j)] + 1, cur[static_cast<size_t>(j) - 1] + 1,
                                                    prev[static_cast<size_t>(j) - 1] + (a[i - 1] == b[j - 1] ? 0 : 1)});
        }
        std::swap(prev, cur);
    }
    return prev[static_cast<size_t>(b.size())];
}

QStringList stems(const QString &text)
{
    QStringList out;
    for (QString w : text.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        if (w.size() > 3 && w.endsWith(QLatin1Char('s')))
            w.chop(1);
        out.append(w);
    }
    return out;
}

// ---- section boundaries: log-mel novelty, as earshot's timeline does

constexpr int kNoveltyFft = 2048;
constexpr int kNoveltyHop = 2400; // 50 ms at 48 kHz
constexpr int kNoveltyBands = 40;

struct LogMel
{
    int bands = 0;
    int frames = 0;
    std::vector<float> db; // [band * frames + frame]
    float at(int band, int frame) const { return db[static_cast<size_t>(band) * static_cast<size_t>(frames) + static_cast<size_t>(frame)]; }
};

struct MelFilters
{
    struct Row
    {
        int first = 0;
        std::vector<float> weights;
    };
    std::vector<Row> rows;
};

const MelFilters &noveltyFilters()
{
    static const MelFilters filters = [] {
        auto mel = [](double f) { return 2595.0 * std::log10(1.0 + f / 700.0); };
        const double lo = mel(30.0), hi = mel(16000.0);
        std::vector<double> edges(kNoveltyBands + 2);
        for (int i = 0; i < kNoveltyBands + 2; ++i)
            edges[static_cast<size_t>(i)] = 700.0 * (std::pow(10.0, (lo + (hi - lo) * i / (kNoveltyBands + 1)) / 2595.0) - 1.0);
        MelFilters out;
        const int bins = kNoveltyFft / 2 + 1;
        for (int b = 0; b < kNoveltyBands; ++b) {
            const double l = edges[static_cast<size_t>(b)], c = edges[static_cast<size_t>(b) + 1], h = edges[static_cast<size_t>(b) + 2];
            MelFilters::Row row;
            int last = -1;
            for (int k = 0; k < bins; ++k) {
                const double f = double(k) * ClapTagger::kSampleRate / kNoveltyFft;
                const double w = std::max(0.0, std::min((f - l) / (c - l), (h - f) / (h - c)));
                if (w > 0.0) {
                    if (last < 0)
                        row.first = k;
                    row.weights.resize(static_cast<size_t>(k - row.first) + 1, 0.0f);
                    row.weights.back() = static_cast<float>(w);
                    last = k;
                }
            }
            out.rows.push_back(std::move(row));
        }
        return out;
    }();
    return filters;
}

LogMel logMel(const float *m, qint64 n)
{
    LogMel out;
    if (n <= 0)
        return out;
    const int pad = kNoveltyFft / 2;
    std::vector<float> y(static_cast<size_t>(n) + 2 * pad, 0.0f);
    std::copy(m, m + n, y.begin() + pad);
    if (n > kNoveltyFft) { // numpy "reflect"
        for (int i = 1; i <= pad; ++i) {
            y[static_cast<size_t>(pad - i)] = m[i];
            y[static_cast<size_t>(pad + n - 1 + i)] = m[n - 1 - i];
        }
    }
    const qint64 frames = 1 + (static_cast<qint64>(y.size()) - kNoveltyFft) / kNoveltyHop;
    out.bands = kNoveltyBands;
    out.frames = static_cast<int>(frames);
    out.db.assign(static_cast<size_t>(out.bands) * static_cast<size_t>(frames), 0.0f);
    static const std::vector<float> window = [] {
        std::vector<float> w(kNoveltyFft);
        for (int k = 0; k < kNoveltyFft; ++k)
            w[static_cast<size_t>(k)] = static_cast<float>(0.5 - 0.5 * std::cos(2.0 * M_PI * k / (kNoveltyFft - 1)));
        return w;
    }();
    RealFft fft(kNoveltyFft);
    if (!fft.ok())
        return {};
    const MelFilters &filters = noveltyFilters();
    std::vector<float> power(kNoveltyFft / 2 + 1);
    for (qint64 t = 0; t < frames; ++t) {
        const float *src = y.data() + t * kNoveltyHop;
        for (int k = 0; k < kNoveltyFft; ++k)
            fft.in()[k] = src[k] * window[static_cast<size_t>(k)];
        fft.run();
        const float *spec = fft.out();
        for (int k = 0; k <= kNoveltyFft / 2; ++k)
            power[static_cast<size_t>(k)] = spec[2 * k] * spec[2 * k] + spec[2 * k + 1] * spec[2 * k + 1];
        for (int b = 0; b < kNoveltyBands; ++b) {
            const MelFilters::Row &row = filters.rows[static_cast<size_t>(b)];
            double acc = 0.0;
            for (size_t k = 0; k < row.weights.size(); ++k)
                acc += double(row.weights[k]) * power[static_cast<size_t>(row.first) + k];
            out.db[static_cast<size_t>(b) * static_cast<size_t>(frames) + static_cast<size_t>(t)] =
                static_cast<float>(10.0 * std::log10(acc + 1e-12));
        }
    }
    return out;
}

// scipy.ndimage.median_filter(v, size=v.size(), mode="nearest")
std::vector<double> wholeMedianFilter(const std::vector<double> &v)
{
    const int n = static_cast<int>(v.size());
    std::vector<double> out(v.size()), window(v.size());
    for (int i = 0; i < n; ++i) {
        const int first = i - n / 2;
        for (int j = 0; j < n; ++j)
            window[static_cast<size_t>(j)] = v[static_cast<size_t>(std::clamp(first + j, 0, n - 1))];
        std::nth_element(window.begin(), window.begin() + n / 2, window.end());
        out[static_cast<size_t>(i)] = window[static_cast<size_t>(n / 2)];
    }
    return out;
}

std::vector<double> robustZ(const std::vector<double> &v)
{
    const std::vector<double> med = wholeMedianFilter(v);
    std::vector<double> dev(v.size());
    for (size_t i = 0; i < v.size(); ++i)
        dev[i] = std::abs(v[i] - med[i]);
    std::vector<double> mad = wholeMedianFilter(dev);
    std::vector<double> positive;
    for (double x : v) {
        if (x > 0.0)
            positive.push_back(x);
    }
    double floor = 1e-6;
    if (!positive.empty()) {
        std::sort(positive.begin(), positive.end());
        const double pos = 0.99 * double(positive.size() - 1);
        const size_t lo = static_cast<size_t>(pos);
        const double frac = pos - double(lo);
        const double p99 = lo + 1 < positive.size() ? positive[lo] * (1.0 - frac) + positive[lo + 1] * frac : positive[lo];
        floor = 0.02 * p99;
    }
    std::vector<double> z(v.size());
    for (size_t i = 0; i < v.size(); ++i)
        z[i] = (v[i] - med[i]) / (mad[i] * 1.4826 + floor + 1e-9);
    return z;
}

// Foote novelty (checkerboard kernel, +-4 s) on 0.5 s log-mel frames: peaks at least 4 s apart
// with a robust z >= 3.
QList<double> noveltyBounds(const float *m, qint64 n)
{
    constexpr double kernelSeconds = 4.0;
    constexpr double thresholdZ = 3.0;
    const LogMel L = logMel(m, n);
    const int w = static_cast<int>(kernelSeconds / 0.5);
    const int cols = L.frames / 10;
    if (cols < 4 * w)
        return {};
    std::vector<std::vector<double>> F(static_cast<size_t>(cols), std::vector<double>(kNoveltyBands, 0.0));
    for (int b = 0; b < kNoveltyBands; ++b) {
        for (int c = 0; c < cols; ++c) {
            double acc = 0.0;
            for (int k = 0; k < 10; ++k)
                acc += L.at(b, c * 10 + k);
            F[static_cast<size_t>(c)][static_cast<size_t>(b)] = acc / 10.0;
        }
        double mean = 0.0;
        for (int c = 0; c < cols; ++c)
            mean += F[static_cast<size_t>(c)][static_cast<size_t>(b)];
        mean /= cols;
        double var = 0.0;
        for (int c = 0; c < cols; ++c)
            var += std::pow(F[static_cast<size_t>(c)][static_cast<size_t>(b)] - mean, 2.0);
        const double sd = std::sqrt(var / cols) + 1e-6;
        for (int c = 0; c < cols; ++c)
            F[static_cast<size_t>(c)][static_cast<size_t>(b)] = (F[static_cast<size_t>(c)][static_cast<size_t>(b)] - mean) / sd;
    }
    for (auto &col : F) {
        double norm = 0.0;
        for (double v : col)
            norm += v * v;
        norm = std::sqrt(norm) + 1e-9;
        for (double &v : col)
            v /= norm;
    }
    auto similarity = [&](int a, int b) {
        double acc = 0.0;
        for (int k = 0; k < kNoveltyBands; ++k)
            acc += F[static_cast<size_t>(a)][static_cast<size_t>(k)] * F[static_cast<size_t>(b)][static_cast<size_t>(k)];
        return acc;
    };
    std::vector<double> g(static_cast<size_t>(2 * w)), r(static_cast<size_t>(2 * w));
    for (int i = 0; i < 2 * w; ++i) {
        const double x = (i - w) + 0.5;
        g[static_cast<size_t>(i)] = std::exp(-0.5 * x * x / std::pow(0.5 * w, 2.0));
        r[static_cast<size_t>(i)] = i < w ? -1.0 : 1.0;
    }
    std::vector<double> novelty(static_cast<size_t>(cols), 0.0);
    for (int i = w; i < cols - w; ++i) {
        double acc = 0.0;
        for (int a = 0; a < 2 * w; ++a) {
            for (int b = 0; b < 2 * w; ++b)
                acc += r[static_cast<size_t>(a)] * r[static_cast<size_t>(b)] * g[static_cast<size_t>(a)]
                       * g[static_cast<size_t>(b)] * similarity(i - w + a, i - w + b);
        }
        novelty[static_cast<size_t>(i)] = acc;
    }
    const std::vector<double> z = robustZ(novelty);
    QList<double> out;
    for (int i = w; i < cols - w; ++i) {
        double peak = novelty[static_cast<size_t>(i)];
        for (int k = std::max(0, i - w); k <= std::min(cols - 1, i + w); ++k)
            peak = std::max(peak, novelty[static_cast<size_t>(k)]);
        if (novelty[static_cast<size_t>(i)] == peak && z[static_cast<size_t>(i)] >= thresholdZ)
            out.append(std::round(i * 0.5 * 100.0) / 100.0);
    }
    return out;
}

// Moves a coarse boundary to the strongest spectral change within +-radius (1 s halves).
double refineBoundary(const float *m, qint64 n, double t, double radius)
{
    const double rate = ClapTagger::kSampleRate;
    const double a = std::max(0.0, t - radius - 1.0);
    const double b = std::min(double(n) / rate, t + radius + 1.0);
    const qint64 from = static_cast<qint64>(a * rate), to = static_cast<qint64>(b * rate);
    if (to - from < 2 * ClapTagger::kSampleRate)
        return t;
    const LogMel L = logMel(m + from, to - from);
    double best = -1.0, at = t;
    for (int k = 10; k < L.frames - 10; ++k) {
        const double tt = a + k * 0.05;
        if (std::abs(tt - t) > radius)
            continue;
        double d2 = 0.0;
        for (int band = 0; band < L.bands; ++band) {
            double before = 0.0, after = 0.0;
            for (int j = 0; j < 10; ++j) {
                before += L.at(band, k - 10 + j);
                after += L.at(band, k + j);
            }
            d2 += std::pow((before - after) / 10.0, 2.0);
        }
        const double d = std::sqrt(d2);
        if (d > best) {
            best = d;
            at = tt;
        }
    }
    return std::round(at * 100.0) / 100.0;
}

} // namespace

QString clapVerdict(float z)
{
    if (z >= 3.0f)
        return QStringLiteral("strong");
    if (z >= 2.5f)
        return QStringLiteral("plausible");
    if (z >= 2.0f)
        return QStringLiteral("weak");
    return QStringLiteral("no match");
}

struct ClapTagger::Impl
{
    QMutex mutex;
    bool loaded = false;
    bool loadAttempted = false;
    QString error;
    std::unique_ptr<Ort::Session> session;
    std::string inName;
    std::string outName;

    bool vocabLoaded = false;
    bool vocabAttempted = false;
    struct Phrase
    {
        QString text, kind;
        float background = 0.0f;
    };
    struct Term
    {
        QString text, category;
        float bias = 0.0f;
    };
    std::vector<Phrase> phrases;
    std::vector<Term> terms;
    std::vector<float> phraseRows; // phrases x 512
    std::vector<float> termRows;   // terms x 512
    float tagCenter = 0.5f;

    bool ensureLoaded();
    bool ensureVocabulary();
};

bool ClapTagger::Impl::ensureVocabulary()
{
    if (vocabLoaded)
        return true;
    if (vocabAttempted)
        return false;
    vocabAttempted = true;
    const QString dir = resolveModelDir();
    if (dir.isEmpty()) {
        error = QStringLiteral("Audio tagging model not found. Install it from the Addon Manager "
                               "(install_addon({id:\"audio.clap\"})), place it in models/audio-clap, or "
                               "set DRIFT_AUDIO_CLAP_MODEL_DIR.");
        return false;
    }
    const QDir root(dir);
    QFile vocabFile(root.filePath(QLatin1String(kVocabFile)));
    if (!vocabFile.open(QIODevice::ReadOnly)) {
        error = QStringLiteral("Cannot read %1").arg(QLatin1String(kVocabFile));
        return false;
    }
    const QJsonObject vocab = QJsonDocument::fromJson(vocabFile.readAll()).object();
    for (const QJsonValue &v : vocab.value(QStringLiteral("event_phrases")).toArray()) {
        const QJsonObject o = v.toObject();
        phrases.push_back({o.value(QStringLiteral("text")).toString(), o.value(QStringLiteral("kind")).toString(),
                           static_cast<float>(o.value(QStringLiteral("background_mean")).toDouble())});
    }
    for (const QJsonValue &v : vocab.value(QStringLiteral("vocab_terms")).toArray()) {
        const QJsonObject o = v.toObject();
        terms.push_back({o.value(QStringLiteral("text")).toString(), o.value(QStringLiteral("category")).toString(),
                         static_cast<float>(o.value(QStringLiteral("bias")).toDouble())});
    }
    tagCenter = static_cast<float>(vocab.value(QStringLiteral("constants")).toObject()
                                       .value(QStringLiteral("event_tag_center")).toDouble(0.5));
    const QJsonObject rows = vocab.value(QStringLiteral("embeddings")).toObject().value(QStringLiteral("rows")).toObject();
    const QJsonObject phraseRange = rows.value(QStringLiteral("event_phrases")).toObject();
    const QJsonObject termRange = rows.value(QStringLiteral("vocab_terms")).toObject();
    QFile bin(root.filePath(QLatin1String(kEmbeddingsFile)));
    if (!bin.open(QIODevice::ReadOnly)) {
        error = QStringLiteral("Cannot read %1").arg(QLatin1String(kEmbeddingsFile));
        return false;
    }
    const QByteArray raw = bin.readAll();
    const qsizetype rowBytes = kDim * qsizetype(sizeof(float));
    auto slice = [&](const QJsonObject &range, std::vector<float> &out, size_t expected) {
        const qsizetype first = range.value(QStringLiteral("first")).toInt();
        const qsizetype count = range.value(QStringLiteral("count")).toInt();
        if (static_cast<size_t>(count) != expected || (first + count) * rowBytes > raw.size())
            return false;
        out.resize(static_cast<size_t>(count) * kDim);
        memcpy(out.data(), raw.constData() + first * rowBytes, static_cast<size_t>(count) * static_cast<size_t>(rowBytes));
        return true;
    };
    if (phrases.empty() || terms.empty() || !slice(phraseRange, phraseRows, phrases.size())
        || !slice(termRange, termRows, terms.size())) {
        error = QStringLiteral("%1 does not match %2").arg(QLatin1String(kEmbeddingsFile), QLatin1String(kVocabFile));
        phrases.clear();
        terms.clear();
        return false;
    }
    vocabLoaded = true;
    return true;
}

bool ClapTagger::Impl::ensureLoaded()
{
    if (loaded)
        return true;
    if (loadAttempted)
        return false;
    loadAttempted = true;
    if (!ort::ensureLoaded(&error))
        return false;
    if (!ensureVocabulary())
        return false;
    const QString dir = resolveModelDir();
    Ort::Env &env = ort::env();
    QString buildError;
    const bool built = ort::buildSessions(env, "audio-clap", false, &buildError, [&](Ort::SessionOptions &opts) {
        session = std::make_unique<Ort::Session>(
            env, ort::ortPath(QDir(dir).filePath(QLatin1String(kModelFile))).c_str(), opts);
    });
    if (!built) {
        error = QStringLiteral("Failed to load the audio tagging model: ") + buildError;
        session.reset();
        return false;
    }
    const std::vector<std::string> inputs = ort::sessionNames(*session, true);
    const std::vector<std::string> outputs = ort::sessionNames(*session, false);
    if (inputs.size() != 1 || outputs.size() != 1) {
        error = QStringLiteral("%1 is not the CLAP export Drift expects").arg(QLatin1String(kModelFile));
        session.reset();
        return false;
    }
    inName = inputs.front();
    outName = outputs.front();
    loaded = true;
    return true;
}

ClapTagger::ClapTagger() : d(std::make_unique<Impl>()) {}
ClapTagger::~ClapTagger() = default;

ClapTagger &ClapTagger::instance()
{
    static ClapTagger inst;
    return inst;
}

bool ClapTagger::modelPresent()
{
    return !resolveModelDir().isEmpty();
}

bool ClapTagger::available()
{
    QMutexLocker lock(&d->mutex);
    return d->ensureLoaded();
}

QString ClapTagger::lastError() const
{
    return d->error;
}

bool ClapTagger::loadVocabulary()
{
    QMutexLocker lock(&d->mutex);
    return d->ensureVocabulary();
}

QStringList ClapTagger::vocabularyTerms() const
{
    QMutexLocker lock(&d->mutex);
    QStringList out;
    if (d->ensureVocabulary()) {
        for (const Impl::Term &t : d->terms)
            out.append(t.text);
    }
    return out;
}

QString ClapTagger::termCategory(int term) const
{
    QMutexLocker lock(&d->mutex);
    return d->ensureVocabulary() && term >= 0 && term < static_cast<int>(d->terms.size())
               ? d->terms[static_cast<size_t>(term)].category : QString();
}

int ClapTagger::termIndex(const QString &text) const
{
    QMutexLocker lock(&d->mutex);
    if (!d->ensureVocabulary())
        return -1;
    const QString want = normalisedTerm(text);
    for (size_t i = 0; i < d->terms.size(); ++i) {
        if (normalisedTerm(d->terms[i].text) == want)
            return static_cast<int>(i);
    }
    const QStringList wantStems = stems(want);
    if (wantStems.isEmpty())
        return -1;
    int best = -1;
    for (size_t i = 0; i < d->terms.size(); ++i) {
        const QString term = normalisedTerm(d->terms[i].text);
        const QStringList termStems = stems(term);
        const bool all = std::all_of(wantStems.cbegin(), wantStems.cend(),
                                     [&](const QString &w) { return termStems.contains(w); });
        if (all && (best < 0 || term.size() < normalisedTerm(d->terms[static_cast<size_t>(best)].text).size()))
            best = static_cast<int>(i);
    }
    return best;
}

QStringList ClapTagger::closestTerms(const QString &text, int count) const
{
    QMutexLocker lock(&d->mutex);
    if (!d->ensureVocabulary())
        return {};
    const QString want = normalisedTerm(text);
    const QStringList wantStems = stems(want);
    std::vector<std::pair<double, int>> scored;
    for (size_t i = 0; i < d->terms.size(); ++i) {
        const QString term = normalisedTerm(d->terms[i].text);
        const QStringList termStems = stems(term);
        int shared = 0;
        for (const QString &w : wantStems)
            shared += termStems.contains(w) ? 1 : 0;
        const double jaccard = double(shared) / double(std::max<qsizetype>(1, wantStems.size() + termStems.size() - shared));
        const double edit = 1.0 - double(editDistance(want, term)) / double(std::max<qsizetype>(1, std::max(want.size(), term.size())));
        const double contained = !want.isEmpty() && (term.contains(want) || want.contains(term)) ? 0.3 : 0.0;
        scored.emplace_back(0.5 * jaccard + 0.5 * edit + contained, static_cast<int>(i));
    }
    std::stable_sort(scored.begin(), scored.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
    QStringList out;
    for (size_t k = 0; k < scored.size() && out.size() < count; ++k)
        out.append(d->terms[static_cast<size_t>(scored[k].second)].text);
    return out;
}

std::vector<ClapEmbedding> ClapTagger::embed(const float *pcm, qint64 frames, const std::vector<ClapSpan> &spans,
                                             const std::function<bool(double)> &progress)
{
    QMutexLocker lock(&d->mutex);
    if (!d->ensureLoaded() || spans.empty())
        return {};

    // Items in a batch share one length, so group spans by it.
    std::map<qint64, std::vector<size_t>> byLength;
    for (size_t i = 0; i < spans.size(); ++i) {
        const qint64 begin = std::clamp<qint64>(spans[i].begin, 0, frames);
        const qint64 end = std::clamp<qint64>(spans[i].end, begin, std::min(frames, begin + kWindowSamples));
        byLength[std::max(kMinSamples, end - begin)].push_back(i);
    }
    std::vector<ClapEmbedding> out(spans.size());
    std::vector<float> batch;
    const char *inNames[] = {d->inName.c_str()};
    const char *outNames[] = {d->outName.c_str()};
    size_t done = 0;
    try {
        for (const auto &[length, items] : byLength) {
            for (size_t first = 0; first < items.size(); first += kBatch) {
                const size_t n = std::min<size_t>(kBatch, items.size() - first);
                batch.assign(n * static_cast<size_t>(length), 0.0f);
                for (size_t k = 0; k < n; ++k) {
                    const ClapSpan &span = spans[items[first + k]];
                    const qint64 begin = std::clamp<qint64>(span.begin, 0, frames);
                    const qint64 end = std::clamp<qint64>(span.end, begin, std::min(frames, begin + length));
                    std::copy(pcm + begin, pcm + end, batch.begin() + static_cast<qint64>(k) * length);
                }
                const std::array<int64_t, 2> shape{static_cast<int64_t>(n), static_cast<int64_t>(length)};
                Ort::Value input = Ort::Value::CreateTensor<float>(ort::cpuMemory(), batch.data(), batch.size(),
                                                                   shape.data(), shape.size());
                std::vector<Ort::Value> result = d->session->Run(Ort::RunOptions{nullptr}, inNames, &input, 1, outNames, 1);
                const float *e = result.front().GetTensorData<float>();
                for (size_t k = 0; k < n; ++k)
                    std::copy(e + k * kDim, e + (k + 1) * kDim, out[items[first + k]].begin());
                done += n;
                if (progress && !progress(static_cast<double>(done) / static_cast<double>(spans.size())))
                    return {};
            }
        }
    } catch (const Ort::Exception &e) {
        d->error = QString::fromUtf8(e.what());
        return {};
    }
    return out;
}

ClapEmbedding ClapTagger::meanOf(const std::vector<ClapEmbedding> &embeddings)
{
    std::vector<double> sum(kDim, 0.0);
    for (const ClapEmbedding &e : embeddings) {
        for (int i = 0; i < kDim; ++i)
            sum[static_cast<size_t>(i)] += e[static_cast<size_t>(i)];
    }
    return normalised(sum);
}

std::vector<ClapSpan> ClapTagger::clipSpans(qint64 frames, int maxWindows)
{
    std::vector<ClapSpan> spans;
    if (frames <= kWindowSamples) {
        spans.push_back({0, frames});
        return spans;
    }
    constexpr qint64 hop = kWindowSamples / 2;
    std::vector<qint64> starts;
    for (qint64 s = 0; s + kWindowSamples <= frames; s += hop)
        starts.push_back(s);
    if ((frames - kWindowSamples) % hop)
        starts.push_back(frames - kWindowSamples);
    if (maxWindows <= 1) {
        starts = {(frames - kWindowSamples) / 2};
    } else if (static_cast<int>(starts.size()) > maxWindows) {
        std::vector<qint64> picked;
        for (int i = 0; i < maxWindows; ++i)
            picked.push_back(starts[static_cast<size_t>(std::llround(double(i) * double(starts.size() - 1) / double(maxWindows - 1)))]);
        starts = picked;
    }
    for (qint64 s : starts)
        spans.push_back({s, s + kWindowSamples});
    return spans;
}

bool ClapTagger::embedClip(const float *pcm, qint64 frames, ClapEmbedding *out, int maxWindows)
{
    const std::vector<ClapEmbedding> parts = embed(pcm, frames, clipSpans(frames, maxWindows));
    if (parts.empty())
        return false;
    *out = meanOf(parts);
    return true;
}

QList<ClapTag> ClapTagger::eventTags(const ClapEmbedding &embedding, int count) const
{
    QMutexLocker lock(&d->mutex);
    if (!d->ensureVocabulary())
        return {};
    const size_t n = d->phrases.size();
    std::vector<float> cosine(n), z(n);
    for (size_t i = 0; i < n; ++i) {
        cosine[i] = dot(embedding.data(), d->phraseRows.data() + i * kDim);
        z[i] = cosine[i] - d->tagCenter * d->phrases[i].background;
    }
    zScore(z);
    std::vector<size_t> order(n);
    for (size_t i = 0; i < n; ++i)
        order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return z[a] > z[b]; });
    QList<ClapTag> out;
    for (size_t k = 0; k < std::min<size_t>(static_cast<size_t>(count), n); ++k) {
        const size_t i = order[k];
        out.append({d->phrases[i].text, d->phrases[i].kind, QString(), z[i], cosine[i]});
    }
    return out;
}

std::vector<ClapTagger::TermScore> ClapTagger::termScores(const ClapEmbedding &embedding) const
{
    QMutexLocker lock(&d->mutex);
    if (!d->ensureVocabulary())
        return {};
    const size_t n = d->terms.size();
    std::vector<float> u(n);
    std::vector<TermScore> out(n);
    for (size_t i = 0; i < n; ++i) {
        out[i].cosine = dot(embedding.data(), d->termRows.data() + i * kDim);
        u[i] = out[i].cosine - d->terms[i].bias;
    }
    zScore(u);
    for (size_t i = 0; i < n; ++i)
        out[i].z = u[i];
    return out;
}

QList<ClapTag> ClapTagger::topTerms(const ClapEmbedding &embedding, int count, const QStringList &excludeCategories) const
{
    const std::vector<TermScore> scores = termScores(embedding);
    QMutexLocker lock(&d->mutex);
    std::vector<size_t> order;
    for (size_t i = 0; i < scores.size(); ++i) {
        if (!excludeCategories.contains(d->terms[i].category))
            order.push_back(i);
    }
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return scores[a].z > scores[b].z; });
    QList<ClapTag> out;
    for (size_t k = 0; k < std::min<size_t>(static_cast<size_t>(count), order.size()); ++k) {
        const size_t i = order[k];
        out.append({d->terms[i].text, QString(), d->terms[i].category, scores[i].z, scores[i].cosine});
    }
    return out;
}

ClapSections ClapTagger::sections(const float *pcm, qint64 frames, double window, double hop, int maxWindows,
                                  const std::function<bool(double)> &progress)
{
    ClapSections result;
    const double rate = kSampleRate;
    const double dur = double(frames) / rate;
    if (dur < 1.0)
        return result;

    hop = std::max(hop, std::round((dur - window) / std::max(1, maxWindows - 1) * 100.0) / 100.0);
    std::vector<double> starts;
    for (double s = 0.0; s <= std::max(dur - window, 0.0) + 1e-6; s += hop)
        starts.push_back(s);
    if (starts.empty() || starts.back() + window < dur - 0.5)
        starts.push_back(std::max(0.0, dur - window));
    struct Span { double a, b; };
    std::vector<Span> spans;
    std::vector<ClapSpan> samples;
    for (double s : starts) {
        spans.push_back({std::round(s * 1000.0) / 1000.0, std::round(std::min(dur, s + window) * 1000.0) / 1000.0});
        samples.push_back({static_cast<qint64>(std::llround(spans.back().a * rate)),
                           static_cast<qint64>(std::llround(spans.back().b * rate))});
    }
    const std::vector<ClapEmbedding> A = embed(pcm, frames, samples, progress);
    if (A.empty())
        return result;
    result.windows = static_cast<int>(spans.size());
    result.novelty = noveltyBounds(pcm, frames);

    std::vector<std::vector<int>> segs;
    for (size_t i = 0; i < spans.size(); ++i)
        segs.push_back({static_cast<int>(i)});
    auto meanOfSeg = [&](const std::vector<int> &seg) {
        std::vector<double> sum(kDim, 0.0);
        for (int i : seg) {
            for (int k = 0; k < kDim; ++k)
                sum[static_cast<size_t>(k)] += A[static_cast<size_t>(i)][static_cast<size_t>(k)];
        }
        return normalised(sum);
    };
    auto length = [&](const std::vector<int> &seg) { return spans[static_cast<size_t>(seg.back())].b - spans[static_cast<size_t>(seg.front())].a; };
    auto blocked = [&](const std::vector<int> &a, const std::vector<int> &b) {
        const Span &last = spans[static_cast<size_t>(a.back())];
        const Span &first = spans[static_cast<size_t>(b.front())];
        const double j = first.a + (first.a >= last.b ? window : last.b - first.a) / 2.0;
        for (double t : result.novelty) {
            if (std::abs(t - j) <= hop)
                return true;
        }
        return false;
    };

    constexpr double mergeCosine = 0.84;
    constexpr double minLength = 4.0;
    const size_t maxCount = static_cast<size_t>(std::max(2.0, std::ceil(dur / 8.0)));
    while (segs.size() > 1) {
        std::vector<double> sims(segs.size() - 1);
        for (size_t j = 0; j + 1 < segs.size(); ++j)
            sims[j] = dot(meanOfSeg(segs[j]).data(), meanOfSeg(segs[j + 1]).data()) - (blocked(segs[j], segs[j + 1]) ? 1.0 : 0.0);
        std::vector<size_t> shortOnes;
        for (size_t j = 0; j < sims.size(); ++j) {
            if (length(segs[j]) < minLength || length(segs[j + 1]) < minLength)
                shortOnes.push_back(j);
        }
        size_t j = static_cast<size_t>(std::max_element(sims.begin(), sims.end()) - sims.begin());
        if (sims[j] < mergeCosine && segs.size() <= maxCount) {
            if (shortOnes.empty())
                break;
            j = *std::max_element(shortOnes.begin(), shortOnes.end(), [&](size_t a, size_t b) { return sims[a] < sims[b]; });
        }
        segs[j].insert(segs[j].end(), segs[j + 1].begin(), segs[j + 1].end());
        segs.erase(segs.begin() + static_cast<qint64>(j) + 1);
    }

    std::vector<double> bounds{0.0};
    for (size_t k = 0; k + 1 < segs.size(); ++k) {
        const Span &last = spans[static_cast<size_t>(segs[k].back())];
        const Span &first = spans[static_cast<size_t>(segs[k + 1].front())];
        const double coarse = first.a < last.b ? (last.a + first.a + window) / 2.0 : first.a;
        bounds.push_back(refineBoundary(pcm, frames, coarse, hop));
    }
    bounds.push_back(std::round(dur * 100.0) / 100.0);
    for (size_t k = 1; k + 1 < bounds.size();) {
        if (bounds[k] - bounds[k - 1] < minLength / 2.0 || bounds[k + 1] - bounds[k] < minLength / 2.0) {
            segs[k - 1].insert(segs[k - 1].end(), segs[k].begin(), segs[k].end());
            segs.erase(segs.begin() + static_cast<qint64>(k));
            bounds.erase(bounds.begin() + static_cast<qint64>(k));
        } else {
            ++k;
        }
    }

    for (size_t k = 0; k < segs.size(); ++k) {
        ClapSection s;
        s.start = bounds[k];
        s.end = bounds[k + 1];
        s.windows = static_cast<int>(segs[k].size());
        s.embedding = meanOfSeg(segs[k]);
        double coherence = 0.0;
        for (int i : segs[k])
            coherence += dot(A[static_cast<size_t>(i)].data(), s.embedding.data());
        s.coherence = static_cast<float>(coherence / double(segs[k].size()));

        const qint64 from = std::min(frames, static_cast<qint64>(std::llround(s.start * rate)));
        const qint64 to = std::min(frames, static_cast<qint64>(std::llround(s.end * rate)));
        double squares = 0.0;
        float peak = 0.0f;
        for (qint64 i = from; i < to; ++i) {
            squares += double(pcm[i]) * pcm[i];
            peak = std::max(peak, std::abs(pcm[i]));
        }
        s.digitalSilence = to > from && peak == 0.0f;
        if (to - from > static_cast<qint64>(0.4 * rate)) {
            const double level = 10.0 * std::log10(squares / double(to - from) + 1e-12) - 0.691;
            s.levelDb = level > -70.0 ? level : -120.0;
        }

        QMutexLocker lock(&d->mutex);
        if (!d->ensureVocabulary())
            return {};
        std::vector<float> cosine(d->terms.size()), z;
        for (size_t i = 0; i < d->terms.size(); ++i)
            cosine[i] = dot(s.embedding.data(), d->termRows.data() + i * kDim);
        z = cosine;
        zScore(z);
        for (int cat = 0; cat < kClapSectionCategories; ++cat) {
            std::vector<size_t> order;
            for (size_t i = 0; i < d->terms.size(); ++i) {
                if (d->terms[i].category == QLatin1String(kCategoryNames[cat]))
                    order.push_back(i);
            }
            std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return z[a] > z[b]; });
            for (size_t n = 0; n < std::min<size_t>(2, order.size()); ++n) {
                const size_t i = order[n];
                s.tags[cat].append({d->terms[i].text, QString(), d->terms[i].category, z[i], cosine[i]});
            }
        }
        result.sections.append(s);
    }
    return result;
}

} // namespace drift
