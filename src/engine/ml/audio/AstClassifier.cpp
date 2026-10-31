#include "engine/ml/audio/AstClassifier.h"

#include "engine/addons/PackageSearchPaths.h"
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
#include <map>

namespace drift {

namespace {

constexpr const char *kModelFile = "ast_audioset.onnx";
constexpr const char *kLabelsFile = "labels.json";
constexpr const char *kConfigFile = "config.json";
constexpr qint64 kMinSamples = 1600;
constexpr int kBatch = 4;

constexpr double kPresenceWindowSeconds = 5.12;
constexpr double kPresenceHopSeconds = 2.56;
constexpr float kPresenceThreshold = 0.3f; // earshot's section_music / section_speech threshold
constexpr float kSilenceProbability = 0.5f;
constexpr double kSilenceDbfs = -60.0;

QString resolveModelDir()
{
    const QStringList roots = PackageSearchPaths::defaultSearchPaths(
        QStringLiteral("DRIFT_AUDIO_AST_MODEL_DIR"), QStringLiteral("models/audio-ast"),
        QStringLiteral("audio-class-model"));
    for (const QString &root : roots) {
        const QDir dir(root);
        if (QFile::exists(dir.filePath(QLatin1String(kModelFile)))
            && QFile::exists(dir.filePath(QLatin1String(kLabelsFile)))
            && QFile::exists(dir.filePath(QLatin1String(kConfigFile)))) {
            return root;
        }
    }
    return {};
}

QJsonObject readObject(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

} // namespace

struct AstClassifier::Impl
{
    QMutex mutex;
    bool loaded = false;
    bool loadAttempted = false;
    QString error;
    std::unique_ptr<Ort::Session> session;
    std::string inName;
    std::string probName;
    QStringList labels;
    int speech = 0;
    int music = 137;
    int silence = 500;

    bool ensureLoaded();
    std::vector<std::vector<float>> run(const float *pcm, qint64 frames, const std::vector<AstSpan> &spans,
                                        const std::function<bool(double)> &progress);
};

bool AstClassifier::Impl::ensureLoaded()
{
    if (loaded)
        return true;
    if (loadAttempted)
        return false;
    loadAttempted = true;
    if (!ort::ensureLoaded(&error))
        return false;
    const QString dir = resolveModelDir();
    if (dir.isEmpty()) {
        error = QStringLiteral("Speech and music model not found. Install it from the Addon Manager "
                               "(install_addon({id:\"audio.ast\"})), place it in models/audio-ast, or "
                               "set DRIFT_AUDIO_AST_MODEL_DIR.");
        return false;
    }
    const QDir root(dir);
    const QJsonObject config = readObject(root.filePath(QLatin1String(kConfigFile)));
    const QJsonObject index = config.value(QStringLiteral("label_index")).toObject();
    speech = index.value(QStringLiteral("Speech")).toInt(speech);
    music = index.value(QStringLiteral("Music")).toInt(music);
    silence = index.value(QStringLiteral("Silence")).toInt(silence);
    QFile labelFile(root.filePath(QLatin1String(kLabelsFile)));
    if (labelFile.open(QIODevice::ReadOnly)) {
        for (const QJsonValue &v : QJsonDocument::fromJson(labelFile.readAll()).array())
            labels.append(v.toString());
    }
    if (labels.size() != kClasses) {
        error = QStringLiteral("%1 does not hold %2 labels").arg(QLatin1String(kLabelsFile)).arg(kClasses);
        return false;
    }
    Ort::Env &env = ort::env();
    QString buildError;
    const bool built = ort::buildSessions(env, "audio-ast", false, &buildError, [&](Ort::SessionOptions &opts) {
        session = std::make_unique<Ort::Session>(
            env, ort::ortPath(root.filePath(QLatin1String(kModelFile))).c_str(), opts);
    });
    if (!built) {
        error = QStringLiteral("Failed to load the speech and music model: ") + buildError;
        session.reset();
        return false;
    }
    const std::vector<std::string> inputs = ort::sessionNames(*session, true);
    const std::vector<std::string> outputs = ort::sessionNames(*session, false);
    if (inputs.size() != 1 || std::find(outputs.begin(), outputs.end(), "probabilities") == outputs.end()) {
        error = QStringLiteral("%1 is not the AST export Drift expects").arg(QLatin1String(kModelFile));
        session.reset();
        return false;
    }
    inName = inputs.front();
    probName = "probabilities";
    loaded = true;
    return true;
}

std::vector<std::vector<float>> AstClassifier::Impl::run(const float *pcm, qint64 frames,
                                                         const std::vector<AstSpan> &spans,
                                                         const std::function<bool(double)> &progress)
{
    std::vector<std::vector<float>> out(spans.size());
    // Items in a batch share one length, so group spans by it.
    std::map<qint64, std::vector<size_t>> byLength;
    std::vector<qint64> lengths(spans.size());
    for (size_t i = 0; i < spans.size(); ++i) {
        const qint64 begin = std::clamp<qint64>(spans[i].begin, 0, frames);
        const qint64 end = std::clamp<qint64>(spans[i].end, begin, std::min(frames, begin + kWindowSamples));
        lengths[i] = std::max(kMinSamples, end - begin);
        byLength[lengths[i]].push_back(i);
    }
    size_t done = 0;
    std::vector<float> batch;
    const char *inNames[] = {inName.c_str()};
    const char *outNames[] = {probName.c_str()};
    try {
        for (const auto &[length, items] : byLength) {
            for (size_t first = 0; first < items.size(); first += kBatch) {
                const size_t n = std::min<size_t>(kBatch, items.size() - first);
                batch.assign(n * static_cast<size_t>(length), 0.0f);
                for (size_t k = 0; k < n; ++k) {
                    const AstSpan &span = spans[items[first + k]];
                    const qint64 begin = std::clamp<qint64>(span.begin, 0, frames);
                    const qint64 end = std::clamp<qint64>(span.end, begin, std::min(frames, begin + length));
                    std::copy(pcm + begin, pcm + end, batch.begin() + static_cast<qint64>(k) * length);
                }
                const std::array<int64_t, 2> shape{static_cast<int64_t>(n), static_cast<int64_t>(length)};
                Ort::Value input = Ort::Value::CreateTensor<float>(ort::cpuMemory(), batch.data(), batch.size(),
                                                                   shape.data(), shape.size());
                std::vector<Ort::Value> result = session->Run(Ort::RunOptions{nullptr}, inNames, &input, 1, outNames, 1);
                const float *p = result.front().GetTensorData<float>();
                for (size_t k = 0; k < n; ++k)
                    out[items[first + k]].assign(p + k * kClasses, p + (k + 1) * kClasses);
                done += n;
                if (progress && !progress(static_cast<double>(done) / static_cast<double>(spans.size())))
                    return {};
            }
        }
    } catch (const Ort::Exception &e) {
        error = QString::fromUtf8(e.what());
        return {};
    }
    return out;
}

AstClassifier::AstClassifier() : d(std::make_unique<Impl>()) {}
AstClassifier::~AstClassifier() = default;

AstClassifier &AstClassifier::instance()
{
    static AstClassifier inst;
    return inst;
}

bool AstClassifier::modelPresent()
{
    return !resolveModelDir().isEmpty();
}

bool AstClassifier::available()
{
    QMutexLocker lock(&d->mutex);
    return d->ensureLoaded();
}

QString AstClassifier::lastError() const
{
    return d->error;
}

std::vector<std::vector<float>> AstClassifier::probabilities(const float *pcm, qint64 frames,
                                                             const std::vector<AstSpan> &spans,
                                                             const std::function<bool(double)> &progress)
{
    QMutexLocker lock(&d->mutex);
    if (!d->ensureLoaded() || spans.empty())
        return {};
    return d->run(pcm, frames, spans, progress);
}

std::vector<float> AstClassifier::probabilitiesOfClip(const float *pcm, qint64 frames)
{
    std::vector<AstSpan> spans;
    if (frames <= kWindowSamples) {
        spans.push_back({0, frames});
    } else {
        constexpr qint64 hop = kWindowSamples / 2;
        for (qint64 s = 0; s + kWindowSamples <= frames; s += hop)
            spans.push_back({s, s + kWindowSamples});
        if ((frames - kWindowSamples) % hop)
            spans.push_back({frames - kWindowSamples, frames});
    }
    const auto rows = probabilities(pcm, frames, spans);
    if (rows.empty())
        return {};
    std::vector<float> best(kClasses, 0.0f);
    for (const auto &row : rows) {
        for (int i = 0; i < kClasses; ++i)
            best[static_cast<size_t>(i)] = std::max(best[static_cast<size_t>(i)], row[static_cast<size_t>(i)]);
    }
    return best;
}

QString AstClassifier::labelName(int index) const
{
    return index >= 0 && index < d->labels.size() ? d->labels.at(index) : QString();
}

int AstClassifier::labelIndex(const QString &label) const
{
    return d->labels.indexOf(label);
}

AstSummary AstClassifier::summarise(const std::vector<float> &p, int topCount, float minProbability) const
{
    AstSummary s;
    if (p.size() != static_cast<size_t>(kClasses))
        return s;
    s.speech = p[static_cast<size_t>(d->speech)];
    s.music = p[static_cast<size_t>(d->music)];
    s.silence = p[static_cast<size_t>(d->silence)];
    std::vector<int> order(kClasses);
    for (int i = 0; i < kClasses; ++i)
        order[static_cast<size_t>(i)] = i;
    std::partial_sort(order.begin(), order.begin() + std::min(topCount, kClasses), order.end(),
                      [&](int a, int b) { return p[static_cast<size_t>(a)] > p[static_cast<size_t>(b)]; });
    for (int k = 0; k < std::min(topCount, kClasses); ++k) {
        const float v = p[static_cast<size_t>(order[static_cast<size_t>(k)])];
        if (v >= minProbability)
            s.top.append({labelName(order[static_cast<size_t>(k)]), v});
    }
    return s;
}

std::vector<float> AstClassifier::resample48kTo16k(const float *pcm, qint64 frames)
{
    constexpr int kTaps = 193; // odd, symmetric
    constexpr int kHalf = kTaps / 2;
    static const std::vector<float> kernel = [] {
        std::vector<float> k(kTaps);
        const double cutoff = 7600.0 / 48000.0; // cycles per input sample
        double sum = 0.0;
        for (int i = 0; i < kTaps; ++i) {
            const double x = i - kHalf;
            const double sinc = x == 0.0 ? 2.0 * cutoff : std::sin(2.0 * M_PI * cutoff * x) / (M_PI * x);
            const double w = 0.42 - 0.5 * std::cos(2.0 * M_PI * i / (kTaps - 1))
                             + 0.08 * std::cos(4.0 * M_PI * i / (kTaps - 1));
            k[static_cast<size_t>(i)] = static_cast<float>(sinc * w);
            sum += sinc * w;
        }
        for (float &v : k)
            v = static_cast<float>(v / sum);
        return k;
    }();

    std::vector<float> out(static_cast<size_t>(frames / 3));
    for (qint64 o = 0; o < static_cast<qint64>(out.size()); ++o) {
        const qint64 centre = o * 3;
        const qint64 lo = std::max<qint64>(0, centre - kHalf);
        const qint64 hi = std::min<qint64>(frames - 1, centre + kHalf);
        float acc = 0.0f;
        for (qint64 i = lo; i <= hi; ++i)
            acc += pcm[i] * kernel[static_cast<size_t>(i - centre + kHalf)];
        out[static_cast<size_t>(o)] = acc;
    }
    return out;
}

AstPresence astPresence(AstClassifier &ast, const float *pcm, qint64 frames, int maxWindows,
                        const std::function<bool(double)> &progress)
{
    AstPresence result;
    const int rate = AstClassifier::kSampleRate;
    const double total = static_cast<double>(frames) / rate;
    if (total < 0.1) {
        result.error = QStringLiteral("Nothing to analyse");
        return result;
    }
    const double win = std::min(kPresenceWindowSeconds, total);
    double hop = kPresenceHopSeconds;
    if (total > win && (total - win) / hop + 1.0 > maxWindows)
        hop = (total - win) / (maxWindows - 1);
    std::vector<double> starts;
    for (double s = 0.0; s + win <= total + 1e-6; s += hop)
        starts.push_back(s);
    if (starts.empty() || total - win - starts.back() > 1e-3)
        starts.push_back(std::max(0.0, total - win));

    std::vector<AstSpan> spans;
    for (double s : starts)
        spans.push_back({static_cast<qint64>(std::llround(s * rate)), static_cast<qint64>(std::llround((s + win) * rate))});
    const auto rows = ast.probabilities(pcm, frames, spans, progress);
    if (rows.empty()) {
        result.error = ast.lastError();
        return result;
    }

    auto kindOf = [&](size_t i) {
        const AstSummary s = ast.summarise(rows[i], 0, 1.0f);
        double sq = 0.0;
        for (qint64 k = spans[i].begin; k < spans[i].end; ++k)
            sq += double(pcm[k]) * pcm[k];
        const double rms = 10.0 * std::log10(sq / double(std::max<qint64>(1, spans[i].end - spans[i].begin)) + 1e-12);
        if (rms < kSilenceDbfs || s.silence >= kSilenceProbability)
            return QStringLiteral("silence");
        const bool sp = s.speech >= kPresenceThreshold;
        const bool mu = s.music >= kPresenceThreshold;
        return sp && mu ? QStringLiteral("speech+music")
                        : sp ? QStringLiteral("speech") : mu ? QStringLiteral("music") : QStringLiteral("other");
    };

    for (size_t i = 0; i < starts.size(); ++i) {
        const double centre = starts[i] + win / 2.0;
        const double a = i == 0 ? 0.0 : std::max(0.0, centre - hop / 2.0);
        const double b = i + 1 == starts.size() ? total : std::min(total, centre + hop / 2.0);
        if (b <= a)
            continue;
        const QString kind = kindOf(i);
        if (!result.spans.isEmpty() && result.spans.last().kind == kind && a <= result.spans.last().end + 1e-6) {
            result.spans.last().end = std::max(result.spans.last().end, b);
        } else {
            const double from = result.spans.isEmpty() ? a : std::max(a, result.spans.last().end);
            result.spans.append({from, b, kind});
        }
    }
    for (const AstPresenceSpan &s : std::as_const(result.spans)) {
        const double len = s.end - s.start;
        if (s.kind.startsWith(QLatin1String("speech")))
            result.speechSeconds += len;
        if (s.kind == QLatin1String("music") || s.kind == QLatin1String("speech+music"))
            result.musicSeconds += len;
        if (s.kind == QLatin1String("silence"))
            result.silenceSeconds += len;
    }
    result.windows = static_cast<int>(starts.size());
    result.ok = true;
    return result;
}

} // namespace drift
