#include "WhisperTranscriber.h"

#include "WhisperTokenizer.h"
#include "GpuPackageParse.h"
#include "OrtRuntime.h"
#include "core/Time.h"

#include <QByteArray>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QRegularExpression>
#include <QThread>
#include <QVariantMap>

#include <onnxruntime_cxx_api.h>

extern "C" {
#include <libavutil/mem.h>
#include <libavutil/tx.h>
}

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

namespace drift {

namespace {

// Whisper-small feature-extractor constants.
constexpr int kSampleRate = 16000;
constexpr int kNFft = 400;
constexpr int kHop = 160;
constexpr int kNMel = 80;
constexpr int kNBins = kNFft / 2 + 1; // 201
constexpr int kChunkSamples = 30 * kSampleRate; // 480000
constexpr int kNFrames = 3000;

// Token ids (from generation_config.json).
constexpr int kVocab = 51865;
constexpr int kSotToken = 50258;        // <|startoftranscript|>
constexpr int kEosToken = 50257;        // <|endoftext|>
constexpr int kTranscribeToken = 50359; // <|transcribe|>
constexpr int kNoTimestampsToken = 50363;
constexpr int kTimestampBegin = 50364; // <|0.00|>
constexpr int kMaxDecodeTokens = 224;  // per 30s window
constexpr double kCompressionRatioThreshold = 2.4; // openai-whisper default
constexpr int kMaxConsecutiveRepeat = 8;

std::basic_string<ORTCHAR_T> ortPath(const QString &path)
{
#ifdef _WIN32
    return path.toStdWString();
#else
    return path.toStdString();
#endif
}

double hertzToMel(double freq)
{
    constexpr double minLogHertz = 1000.0;
    constexpr double minLogMel = 15.0;
    const double logstep = 27.0 / std::log(6.4);
    if (freq >= minLogHertz)
        return minLogMel + std::log(freq / minLogHertz) * logstep;
    return 3.0 * freq / 200.0;
}

double melToHertz(double mel)
{
    constexpr double minLogHertz = 1000.0;
    constexpr double minLogMel = 15.0;
    const double logstep = std::log(6.4) / 27.0;
    if (mel >= minLogMel)
        return minLogHertz * std::exp(logstep * (mel - minLogMel));
    return 200.0 * mel / 3.0;
}

// Slaney-normalized mel filterbank matching transformers.WhisperFeatureExtractor.
// Returns filters[kNMel][kNBins].
std::vector<std::vector<float>> buildMelFilters()
{
    std::vector<double> filterFreqs(kNMel + 2);
    const double melMin = hertzToMel(0.0);
    const double melMax = hertzToMel(8000.0);
    for (int i = 0; i < kNMel + 2; ++i) {
        const double mel = melMin + (melMax - melMin) * i / (kNMel + 1);
        filterFreqs[i] = melToHertz(mel);
    }

    std::vector<double> fftFreqs(kNBins);
    for (int k = 0; k < kNBins; ++k)
        fftFreqs[k] = static_cast<double>(k) * (kSampleRate / 2.0) / (kNBins - 1);

    std::vector<std::vector<float>> filters(kNMel, std::vector<float>(kNBins, 0.0f));
    for (int m = 0; m < kNMel; ++m) {
        const double left = filterFreqs[m];
        const double center = filterFreqs[m + 1];
        const double right = filterFreqs[m + 2];
        const double leftDiff = center - left;
        const double rightDiff = right - center;
        const double enorm = 2.0 / (right - left); // slaney norm
        for (int k = 0; k < kNBins; ++k) {
            const double down = (fftFreqs[k] - left) / leftDiff;
            const double up = (right - fftFreqs[k]) / rightDiff;
            const double v = std::min(down, up);
            filters[m][k] = static_cast<float>(std::max(0.0, v) * enorm);
        }
    }
    return filters;
}

QString languageDisplayName(const QString &code)
{
    // Whisper uses a few non-ISO aliases.
    if (code == QLatin1String("jw"))
        return QStringLiteral("Javanese");
    if (code == QLatin1String("haw"))
        return QStringLiteral("Hawaiian");

    const QLocale::Language lang = QLocale::codeToLanguage(code);
    if (lang != QLocale::AnyLanguage)
        return QLocale::languageToString(lang);
    return code;
}

QString resolveWhisperModelDir()
{
    const QStringList roots =
        GpuPackageParse::defaultSearchPaths(QStringLiteral("DRIFT_WHISPER_MODEL_DIR"),
                                            QStringLiteral("models/whisper-small"),
                                            QStringLiteral("whisper-model"));
    for (const QString &root : roots) {
        if (QFile::exists(QDir(root).filePath(QStringLiteral("encoder_model_fp16.onnx"))))
            return root;
    }
    return {};
}

// Parse "<|en|>" -> "en" from generation_config lang_to_id keys.
QString languageCodeFromTokenName(const QString &tokenName)
{
    QString code = tokenName;
    if (code.startsWith(QLatin1String("<|")) && code.endsWith(QLatin1String("|>")))
        code = code.mid(2, code.size() - 4);
    return code;
}

// Same metric openai-whisper uses to detect "aaaa…" / "අපි අපි අපි…" collapse.
double compressionRatio(const QString &text)
{
    const QByteArray utf8 = text.toUtf8();
    if (utf8.isEmpty())
        return 0.0;
    const QByteArray zipped = qCompress(utf8, 9);
    // qCompress prepends a 4-byte big-endian uncompressed length.
    const int compressed = std::max(1, static_cast<int>(zipped.size()) - 4);
    return static_cast<double>(utf8.size()) / static_cast<double>(compressed);
}

bool isDegenerateTranscript(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty())
        return true;
    if (compressionRatio(trimmed) > kCompressionRatioThreshold)
        return true;

    // Catch short loops that don't always trip gzip (e.g. a few repeated words).
    const QStringList words = trimmed.split(QRegularExpression(QStringLiteral("\\s+")),
                                            Qt::SkipEmptyParts);
    if (words.size() >= 8) {
        QHash<QString, int> counts;
        int best = 0;
        for (const QString &w : words) {
            const int c = ++counts[w];
            best = std::max(best, c);
        }
        if (best * 2 >= words.size() && best >= 6)
            return true;
    }
    return false;
}

// Count <|start|> text <|end|> pairs that would become real cues (matches the segmenter).
int countClosedSegments(const std::vector<int> &generated, const WhisperTokenizer &tokenizer)
{
    int closed = 0;
    bool open = false;
    std::vector<int> textTokens;
    for (const int tok : generated) {
        if (tok >= kTimestampBegin) {
            if (open && !textTokens.empty()) {
                const QString text = tokenizer.decode(textTokens).trimmed();
                if (!text.isEmpty() && !isDegenerateTranscript(text))
                    ++closed;
            }
            textTokens.clear();
            open = true; // end of one segment is start of the next
        } else if (tok != kEosToken && tok < WhisperTokenizer::kTextTokenLimit) {
            textTokens.push_back(tok);
        }
    }
    return closed;
}

QString formatClock(double seconds)
{
    const int total = std::max(0, static_cast<int>(std::llround(seconds)));
    return QStringLiteral("%1:%2")
        .arg(total / 60)
        .arg(total % 60, 2, 10, QLatin1Char('0'));
}

Ort::Value floatView(const Ort::Value &src, const Ort::MemoryInfo &mem)
{
    auto info = src.GetTensorTypeAndShapeInfo();
    auto shape = info.GetShape();
    return Ort::Value::CreateTensor<float>(mem, const_cast<float *>(src.GetTensorData<float>()),
                                           info.GetElementCount(), shape.data(), shape.size());
}

std::string presentToPast(const std::string &name)
{
    // "present.3.decoder.key" -> "past_key_values.3.decoder.key"
    const std::string prefix = "present";
    if (name.rfind(prefix, 0) == 0)
        return "past_key_values" + name.substr(prefix.size());
    return name;
}

} // namespace

struct WhisperTranscriber::Impl
{
    bool loaded = false;
    bool loadAttempted = false;
    QString error;
    QString modelDir;

    std::unique_ptr<Ort::Session> encoder;
    std::unique_ptr<Ort::Session> decoder;
    std::unique_ptr<Ort::Session> decoderPast;

    std::vector<std::string> encInNames, encOutNames;
    std::vector<std::string> decInNames, decOutNames;
    std::vector<std::string> decpInNames, decpOutNames;

    WhisperTokenizer tokenizer;
    std::vector<std::vector<float>> melFilters;
    std::vector<float> hann;

    std::vector<char> suppressMask;      // vocab-sized, from suppress_tokens (+ no_timestamps)
    std::vector<int> beginSuppress;      // begin_suppress_tokens
    QHash<QString, int> languageByCode;  // "en" -> token id
    std::vector<int> languageTokenIds;   // values for auto-detect argmax
    std::mt19937 rng{std::random_device{}()};

    // Scratch FFT buffers (av_tx).
    AVTXContext *tx = nullptr;
    av_tx_fn txFn = nullptr;
    float *fftIn = nullptr;
    void *fftOut = nullptr; // AVComplexFloat[kNBins]

    ~Impl()
    {
        if (tx)
            av_tx_uninit(&tx);
        av_free(fftIn);
        av_free(fftOut);
    }

    bool ensureLanguageMap();
    bool ensureLoaded();
    std::vector<std::string> names(Ort::Session &s, bool inputs);
    std::vector<float> logMel(const float *pcm, int count); // returns [kNMel*kNFrames]
    Ort::Value runEncoder(const std::vector<float> &mel);

    // Apply openai-whisper ApplyTimestampRules + suppress masks into a mutable logits copy,
    // then greedy- or temperature-sample the next token.
    int sampleToken(const float *logits, const std::vector<int> &generated, int minTimestamp,
                    bool firstStep, float temperature);

    // Decode one 30s encoder window; retries with rising temperature when the transcript
    // looks collapsed (high compression ratio / repeated words).
    std::vector<int> decodeWindow(Ort::Value &encHidden, const std::vector<int64_t> &prompt,
                                  const std::function<void(const QString &)> &status);
};

int WhisperTranscriber::Impl::sampleToken(const float *logits, const std::vector<int> &generated,
                                          int minTimestamp, bool firstStep, float temperature)
{
    std::vector<float> scores(static_cast<size_t>(kVocab));
    std::memcpy(scores.data(), logits, sizeof(float) * static_cast<size_t>(kVocab));

    constexpr float kNegInf = -std::numeric_limits<float>::infinity();

    for (int v = 0; v < kVocab; ++v) {
        if (suppressMask[v])
            scores[static_cast<size_t>(v)] = kNegInf;
    }

    // Timestamps must not go backward (and segments must have nonzero length).
    for (int v = kTimestampBegin; v < minTimestamp && v < kVocab; ++v)
        scores[static_cast<size_t>(v)] = kNegInf;

    // openai-whisper ApplyTimestampRules: first sampled token must be a timestamp.
    if (firstStep) {
        for (int v = 0; v < kTimestampBegin; ++v)
            scores[static_cast<size_t>(v)] = kNegInf;
        for (const int t : beginSuppress) {
            if (t >= 0 && t < kVocab)
                scores[static_cast<size_t>(t)] = kNegInf;
        }
    }

    // Timestamps appear in pairs except before EOS.
    // After a lone timestamp (segment start): next must be text.
    // After text then timestamp (segment end): next must be timestamp/EOS (not text).
    if (!generated.empty()) {
        const int last = generated.back();
        const bool lastWasTs = last >= kTimestampBegin;
        const bool penultimateWasTs =
            generated.size() < 2 || generated[generated.size() - 2] >= kTimestampBegin;
        if (lastWasTs) {
            if (penultimateWasTs) {
                for (int v = kTimestampBegin; v < kVocab; ++v)
                    scores[static_cast<size_t>(v)] = kNegInf;
            } else {
                for (int v = 0; v < kEosToken; ++v)
                    scores[static_cast<size_t>(v)] = kNegInf;
            }
        }
    }

    // If timestamp probability mass beats any single text token, force a timestamp.
    // Uses log-sum-exp over timestamp logits vs max text logit (same as Whisper).
    {
        float maxAll = kNegInf;
        for (int v = 0; v < kVocab; ++v)
            maxAll = std::max(maxAll, scores[static_cast<size_t>(v)]);
        if (std::isfinite(maxAll)) {
            double tsSum = 0.0;
            float maxText = kNegInf;
            for (int v = 0; v < kTimestampBegin; ++v) {
                const float s = scores[static_cast<size_t>(v)];
                if (s > maxText)
                    maxText = s;
            }
            for (int v = kTimestampBegin; v < kVocab; ++v) {
                const float s = scores[static_cast<size_t>(v)];
                if (std::isfinite(s))
                    tsSum += std::exp(static_cast<double>(s - maxAll));
            }
            const float tsLogprob = maxAll + static_cast<float>(std::log(std::max(tsSum, 1e-30)));
            if (tsLogprob > maxText) {
                for (int v = 0; v < kTimestampBegin; ++v)
                    scores[static_cast<size_t>(v)] = kNegInf;
            }
        }
    }

    if (temperature <= 0.0f) {
        int best = kEosToken;
        float bestVal = kNegInf;
        for (int v = 0; v < kVocab; ++v) {
            const float s = scores[static_cast<size_t>(v)];
            if (s > bestVal) {
                bestVal = s;
                best = v;
            }
        }
        return best;
    }

    // Temperature sample over finite logits.
    float maxScore = kNegInf;
    for (int v = 0; v < kVocab; ++v)
        maxScore = std::max(maxScore, scores[static_cast<size_t>(v)]);
    if (!std::isfinite(maxScore))
        return kEosToken;

    std::vector<double> probs(static_cast<size_t>(kVocab), 0.0);
    double total = 0.0;
    for (int v = 0; v < kVocab; ++v) {
        const float s = scores[static_cast<size_t>(v)];
        if (!std::isfinite(s))
            continue;
        const double p = std::exp(static_cast<double>((s - maxScore) / temperature));
        probs[static_cast<size_t>(v)] = p;
        total += p;
    }
    if (total <= 0.0)
        return kEosToken;

    std::uniform_real_distribution<double> dist(0.0, total);
    double draw = dist(rng);
    for (int v = 0; v < kVocab; ++v) {
        draw -= probs[static_cast<size_t>(v)];
        if (draw <= 0.0)
            return v;
    }
    return kEosToken;
}

std::vector<int> WhisperTranscriber::Impl::decodeWindow(
    Ort::Value &encHidden, const std::vector<int64_t> &prompt,
    const std::function<void(const QString &)> &status)
{
    static const float kTemperatures[] = {0.0f, 0.2f, 0.4f, 0.6f, 0.8f, 1.0f};

    std::vector<int> bestGenerated;
    int bestClosed = -1;
    for (const float temperature : kTemperatures) {
        if (temperature > 0.0f && status) {
            status(QStringLiteral("Difficult audio — trying another pass…"));
        }
        std::unordered_map<std::string, Ort::Value> pastKV;
        std::vector<int> generated;
        int minTimestamp = kTimestampBegin;
        bool firstStep = true;
        int nextToken = kEosToken;
        int repeatCount = 0;
        int lastTextToken = -1;

        {
            const int64_t idShape[2] = {1, static_cast<int64_t>(prompt.size())};
            Ort::Value idTensor = Ort::Value::CreateTensor<int64_t>(
                ort::cpuMemory(), const_cast<int64_t *>(prompt.data()), prompt.size(), idShape, 2);
            Ort::Value encView = floatView(encHidden, ort::cpuMemory());
            Ort::Value ins[2] = {std::move(idTensor), std::move(encView)};
            const char *inN[2] = {decInNames[0].c_str(), decInNames[1].c_str()};
            std::vector<const char *> outN;
            for (const auto &n : decOutNames)
                outN.push_back(n.c_str());
            auto outs =
                decoder->Run(Ort::RunOptions{nullptr}, inN, ins, 2, outN.data(), outN.size());

            const float *logits = outs[0].GetTensorMutableData<float>();
            const int last = static_cast<int>(prompt.size()) - 1;
            nextToken = sampleToken(logits + static_cast<size_t>(last) * kVocab, generated,
                                    minTimestamp, firstStep, temperature);
            firstStep = false;

            for (size_t i = 1; i < decOutNames.size(); ++i)
                pastKV.emplace(presentToPast(decOutNames[i]), std::move(outs[i]));
        }

        int cachePos = static_cast<int>(prompt.size());

        for (int step = 0; step < kMaxDecodeTokens; ++step) {
            if (nextToken == kEosToken)
                break;

            if (nextToken >= kTimestampBegin) {
                // After a segment-end timestamp (text then ts), next start must be strictly later.
                const bool closing =
                    !generated.empty() && generated.back() < kTimestampBegin;
                minTimestamp = closing ? nextToken + 1 : nextToken;
                repeatCount = 0;
                lastTextToken = -1;
            } else if (nextToken < kTimestampBegin) {
                if (nextToken == lastTextToken) {
                    ++repeatCount;
                    if (repeatCount >= kMaxConsecutiveRepeat)
                        break;
                } else {
                    lastTextToken = nextToken;
                    repeatCount = 1;
                }
            }

            generated.push_back(nextToken);

            std::vector<int64_t> idData{nextToken};
            const int64_t idShape[2] = {1, 1};
            Ort::Value idTensor =
                Ort::Value::CreateTensor<int64_t>(ort::cpuMemory(), idData.data(), 1, idShape, 2);
            std::vector<int64_t> cacheData{cachePos};
            const int64_t cacheShape[1] = {1};
            Ort::Value cacheTensor =
                Ort::Value::CreateTensor<int64_t>(ort::cpuMemory(), cacheData.data(), 1, cacheShape, 1);

            std::vector<Ort::Value> ins;
            std::vector<const char *> inN;
            ins.reserve(decpInNames.size());
            for (const std::string &name : decpInNames) {
                inN.push_back(name.c_str());
                if (name == "input_ids")
                    ins.push_back(std::move(idTensor));
                else if (name == "cache_position")
                    ins.push_back(std::move(cacheTensor));
                else
                    ins.push_back(floatView(pastKV.at(name), ort::cpuMemory()));
            }
            std::vector<const char *> outN;
            for (const auto &n : decpOutNames)
                outN.push_back(n.c_str());

            auto outs = decoderPast->Run(Ort::RunOptions{nullptr}, inN.data(), ins.data(),
                                         ins.size(), outN.data(), outN.size());

            const float *logits = outs[0].GetTensorMutableData<float>();
            nextToken = sampleToken(logits, generated, minTimestamp, false, temperature);

            for (size_t i = 1; i < decpOutNames.size(); ++i)
                pastKV.insert_or_assign(presentToPast(decpOutNames[i]), std::move(outs[i]));
            ++cachePos;
        }

        std::vector<int> textOnly;
        textOnly.reserve(generated.size());
        for (const int tok : generated) {
            if (tok >= 0 && tok < WhisperTokenizer::kTextTokenLimit)
                textOnly.push_back(tok);
        }
        const QString text = tokenizer.decode(textOnly).trimmed();
        const int closed = countClosedSegments(generated, tokenizer);
        if (closed > bestClosed) {
            bestClosed = closed;
            bestGenerated = generated;
        }

        // Empty = silence (OK). Otherwise require at least one closed timestamp pair —
        // otherwise we "accept" free-running text that the segmenter then drops (0 cues).
        const bool ok = text.isEmpty()
                        || (closed > 0 && !isDegenerateTranscript(text));
        if (ok) {
            if (temperature > 0.0f)
                qWarning() << "[whisper] accepted decode at temperature" << temperature
                           << "chars" << text.size() << "closedSegments" << closed;
            return generated;
        }

        qWarning() << "[whisper] reject decode temperature" << temperature
                   << "compression" << compressionRatio(text) << "chars" << text.size()
                   << "closedSegments" << closed << "tokens" << generated.size();
    }

    // All temperatures failed the quality bar; keep the attempt with the most closed segments.
    return bestGenerated;
}

std::vector<std::string> WhisperTranscriber::Impl::names(Ort::Session &s, bool inputs)
{
    Ort::AllocatorWithDefaultOptions alloc;
    std::vector<std::string> out;
    const size_t n = inputs ? s.GetInputCount() : s.GetOutputCount();
    for (size_t i = 0; i < n; ++i) {
        auto name = inputs ? s.GetInputNameAllocated(i, alloc) : s.GetOutputNameAllocated(i, alloc);
        out.emplace_back(name.get());
    }
    return out;
}

bool WhisperTranscriber::Impl::ensureLanguageMap()
{
    if (!languageByCode.isEmpty())
        return true;

    if (modelDir.isEmpty())
        modelDir = resolveWhisperModelDir();
    if (modelDir.isEmpty())
        return false;

    QFile gc(QDir(modelDir).filePath(QStringLiteral("generation_config.json")));
    if (!gc.open(QIODevice::ReadOnly))
        return false;

    const QJsonObject obj = QJsonDocument::fromJson(gc.readAll()).object();
    const QJsonObject langs = obj.value(QStringLiteral("lang_to_id")).toObject();
    languageByCode.clear();
    languageTokenIds.clear();
    languageByCode.reserve(langs.size());
    languageTokenIds.reserve(static_cast<size_t>(langs.size()));
    for (auto it = langs.constBegin(); it != langs.constEnd(); ++it) {
        const int id = it.value().toInt(-1);
        if (id < 0)
            continue;
        const QString code = languageCodeFromTokenName(it.key());
        if (code.isEmpty())
            continue;
        languageByCode.insert(code, id);
        languageTokenIds.push_back(id);
    }
    return !languageByCode.isEmpty();
}

bool WhisperTranscriber::Impl::ensureLoaded()
{
    if (loadAttempted)
        return loaded;
    loadAttempted = true;

    modelDir = resolveWhisperModelDir();
    if (modelDir.isEmpty()) {
        error = QStringLiteral("Whisper model not found. Place it in models/whisper-small "
                               "or set DRIFT_WHISPER_MODEL_DIR.");
        return false;
    }
    if (!ort::ensureLoaded(&error))
        return false;

    try {
        Ort::Env &ortEnv = ort::env();
        Ort::SessionOptions opts;
        opts.SetIntraOpNumThreads(std::max(1, QThread::idealThreadCount()));
        // fp16 graph fusions (SimplifiedLayerNormFusion) crash on load; disable them.
        opts.SetGraphOptimizationLevel(ORT_DISABLE_ALL);

        const QDir dir(modelDir);
        encoder = std::make_unique<Ort::Session>(
            ortEnv, ortPath(dir.filePath(QStringLiteral("encoder_model_fp16.onnx"))).c_str(), opts);
        decoder = std::make_unique<Ort::Session>(
            ortEnv, ortPath(dir.filePath(QStringLiteral("decoder_model_fp16.onnx"))).c_str(), opts);
        decoderPast = std::make_unique<Ort::Session>(
            ortEnv,
            ortPath(dir.filePath(QStringLiteral("decoder_with_past_model_fp16.onnx"))).c_str(),
            opts);

        encInNames = names(*encoder, true);
        encOutNames = names(*encoder, false);
        decInNames = names(*decoder, true);
        decOutNames = names(*decoder, false);
        decpInNames = names(*decoderPast, true);
        decpOutNames = names(*decoderPast, false);
    } catch (const Ort::Exception &e) {
        error = QStringLiteral("Failed to load Whisper model: ") + QString::fromUtf8(e.what());
        return false;
    }

    if (!tokenizer.load(QDir(modelDir).filePath(QStringLiteral("vocab.json")))) {
        error = QStringLiteral("Failed to load Whisper tokenizer (vocab.json).");
        return false;
    }

    // Parse generation_config.json for suppression + language tokens.
    suppressMask.assign(kVocab, 0);
    suppressMask[kNoTimestampsToken] = 1; // always emit timestamps
    QFile gc(QDir(modelDir).filePath(QStringLiteral("generation_config.json")));
    if (gc.open(QIODevice::ReadOnly)) {
        const QJsonObject obj = QJsonDocument::fromJson(gc.readAll()).object();
        for (const QJsonValue v : obj.value(QStringLiteral("suppress_tokens")).toArray()) {
            const int t = v.toInt(-1);
            if (t >= 0 && t < kVocab)
                suppressMask[t] = 1;
        }
        for (const QJsonValue v : obj.value(QStringLiteral("begin_suppress_tokens")).toArray()) {
            const int t = v.toInt(-1);
            if (t >= 0 && t < kVocab)
                beginSuppress.push_back(t);
        }
    }
    if (!ensureLanguageMap()) {
        error = QStringLiteral("Failed to load Whisper language map (generation_config.json).");
        return false;
    }

    melFilters = buildMelFilters();
    hann.resize(kNFft);
    for (int n = 0; n < kNFft; ++n)
        hann[n] = 0.5f * (1.0f - std::cos(2.0 * M_PI * n / kNFft)); // periodic Hann

    float scale = 1.0f;
    fftIn = static_cast<float *>(av_malloc(sizeof(float) * kNFft));
    fftOut = av_malloc(sizeof(float) * 2 * (kNBins + 1));
    if (av_tx_init(&tx, &txFn, AV_TX_FLOAT_RDFT, 0, kNFft, &scale, 0) < 0) {
        error = QStringLiteral("Failed to initialize FFT (av_tx).");
        return false;
    }

    loaded = true;
    return true;
}

std::vector<float> WhisperTranscriber::Impl::logMel(const float *pcm, int count)
{
    const int pad = kNFft / 2;
    std::vector<float> padded(kChunkSamples + kNFft, 0.0f);
    const int n = std::min(count, kChunkSamples);
    for (int i = 0; i < n; ++i)
        padded[pad + i] = pcm[i];
    // Reflect padding (numpy 'reflect', edge sample not repeated).
    for (int k = 1; k <= pad && k < n; ++k)
        padded[pad - k] = pcm[k];
    for (int k = 1; k <= pad && k < n; ++k)
        padded[pad + n - 1 + k] = pcm[n - 1 - k];

    auto *cout = static_cast<float *>(fftOut); // interleaved re,im
    std::vector<float> mel(kNMel * kNFrames, 0.0f);
    float logMax = -1e30f;

    for (int t = 0; t < kNFrames; ++t) {
        const int start = t * kHop;
        for (int i = 0; i < kNFft; ++i)
            fftIn[i] = padded[start + i] * hann[i];
        txFn(tx, cout, fftIn, sizeof(float));

        for (int m = 0; m < kNMel; ++m) {
            const std::vector<float> &fil = melFilters[m];
            float acc = 0.0f;
            for (int k = 0; k < kNBins; ++k) {
                const float re = cout[2 * k];
                const float im = cout[2 * k + 1];
                acc += fil[k] * (re * re + im * im);
            }
            float v = std::log10(std::max(acc, 1e-10f));
            mel[m * kNFrames + t] = v;
            logMax = std::max(logMax, v);
        }
    }

    const float floor = logMax - 8.0f;
    for (float &v : mel)
        v = (std::max(v, floor) + 4.0f) / 4.0f;
    return mel;
}

Ort::Value WhisperTranscriber::Impl::runEncoder(const std::vector<float> &mel)
{
    const int64_t shape[3] = {1, kNMel, kNFrames};
    Ort::Value in = Ort::Value::CreateTensor<float>(ort::cpuMemory(), const_cast<float *>(mel.data()),
                                                    mel.size(), shape, 3);
    const char *inName = encInNames[0].c_str();
    const char *outName = encOutNames[0].c_str();
    auto outs = encoder->Run(Ort::RunOptions{nullptr}, &inName, &in, 1, &outName, 1);
    return std::move(outs[0]);
}

// --- WhisperTranscriber ----------------------------------------------------

WhisperTranscriber::WhisperTranscriber() : d(std::make_unique<Impl>()) {}
WhisperTranscriber::~WhisperTranscriber() = default;

WhisperTranscriber &WhisperTranscriber::instance()
{
    static WhisperTranscriber s;
    return s;
}

bool WhisperTranscriber::modelPresent()
{
    return !resolveWhisperModelDir().isEmpty();
}

bool WhisperTranscriber::available()
{
    return d->ensureLoaded();
}

QString WhisperTranscriber::lastError() const
{
    return d->error;
}

QVariantList WhisperTranscriber::supportedLanguages()
{
    QVariantList out;
    if (!d->ensureLanguageMap())
        return out;

    QList<QPair<QString, QString>> entries;
    entries.reserve(d->languageByCode.size());
    for (auto it = d->languageByCode.constBegin(); it != d->languageByCode.constEnd(); ++it)
        entries.append({it.key(), languageDisplayName(it.key())});

    std::sort(entries.begin(), entries.end(),
              [](const QPair<QString, QString> &a, const QPair<QString, QString> &b) {
                  return a.second.localeAwareCompare(b.second) < 0;
              });

    out.reserve(entries.size());
    for (const auto &entry : entries) {
        QVariantMap row;
        row.insert(QStringLiteral("code"), entry.first);
        row.insert(QStringLiteral("label"), entry.second);
        out.append(row);
    }
    return out;
}

WhisperResult WhisperTranscriber::transcribe(
    const std::vector<float> &pcm, const std::function<bool(double, const QString &)> &progress,
    const QString &languageCode, int maxWordsPerCue)
{
    WhisperResult result = transcribeSegments(pcm, progress, languageCode);
    if (result.ok) {
        // Pack into short display lines like openai-whisper's VTT writer
        // (word_timestamps + max_line_width=42, max_line_count=1), optionally capped shorter still.
        result.cues = packSubtitleCues(result.cues, 42, 1, maxWordsPerCue);
    }
    return result;
}

void WhisperTranscriber::unload()
{
    d->encoder.reset();
    d->decoder.reset();
    d->decoderPast.reset();
    // ensureLoaded() rebuilds these; leave nothing for it to leak or append to twice.
    if (d->tx)
        av_tx_uninit(&d->tx);
    av_freep(&d->fftIn);
    av_freep(&d->fftOut);
    d->beginSuppress.clear();
    d->loaded = false;
    d->loadAttempted = false;
}

WhisperResult WhisperTranscriber::transcribeSegments(
    const std::vector<float> &pcm, const std::function<bool(double, const QString &)> &progress,
    const QString &languageCode)
{
    WhisperResult result;
    if (!d->ensureLoaded()) {
        result.error = d->error;
        return result;
    }

    const int total = static_cast<int>(pcm.size());
    if (total <= 0) {
        result.ok = true;
        return result;
    }
    const double totalSeconds = static_cast<double>(total) / kSampleRate;

    auto report = [&](double fraction, const QString &status) -> bool {
        if (!progress)
            return true;
        return progress(std::min(1.0, std::max(0.0, fraction)), status);
    };

    const QString forcedLang = languageCode.trimmed().toLower();
    const bool forceLanguage = !forcedLang.isEmpty();
    int languageToken = d->languageByCode.value(QStringLiteral("en"), 50259); // <|en|> fallback
    if (forceLanguage) {
        const auto it = d->languageByCode.constFind(forcedLang);
        if (it == d->languageByCode.constEnd()) {
            result.error = QStringLiteral("Unsupported Whisper language: %1").arg(forcedLang);
            return result;
        }
        languageToken = it.value();
    }

    int cursor = 0;

    while (cursor < total) {
        const double windowStartSec = static_cast<double>(cursor) / kSampleRate;
        const double windowEndSec = std::min(totalSeconds, windowStartSec + 30.0);
        const QString windowStatus =
            QStringLiteral("Transcribing %1–%2 of %3…")
                .arg(formatClock(windowStartSec), formatClock(windowEndSec),
                     formatClock(totalSeconds));
        if (!report(static_cast<double>(cursor) / total, windowStatus)) {
            result.cancelled = true;
            return result;
        }

        const std::vector<float> mel = d->logMel(pcm.data() + cursor, total - cursor);

        Ort::Value encHidden = d->runEncoder(mel);

        // Language detection on the first window only (skipped when a language is forced).
        if (!forceLanguage && cursor == 0) {
            if (!report(0.0, QStringLiteral("Detecting language…"))) {
                result.cancelled = true;
                return result;
            }
            std::vector<int64_t> ids{kSotToken};
            const int64_t idShape[2] = {1, 1};
            Ort::Value idTensor =
                Ort::Value::CreateTensor<int64_t>(ort::cpuMemory(), ids.data(), ids.size(), idShape, 2);
            Ort::Value encView = floatView(encHidden, ort::cpuMemory());
            std::vector<Ort::Value> ins;
            ins.push_back(std::move(idTensor));
            ins.push_back(std::move(encView));
            std::vector<const char *> inN{d->decInNames[0].c_str(), d->decInNames[1].c_str()};
            std::vector<const char *> outN;
            for (const auto &n : d->decOutNames)
                outN.push_back(n.c_str());
            auto outs = d->decoder->Run(Ort::RunOptions{nullptr}, inN.data(), ins.data(), ins.size(),
                                        outN.data(), outN.size());
            const float *logits = outs[0].GetTensorMutableData<float>();
            float best = -1e30f;
            for (const int lang : d->languageTokenIds) {
                if (lang >= 0 && lang < kVocab && logits[lang] > best) {
                    best = logits[lang];
                    languageToken = lang;
                }
            }
            if (!report(0.0, windowStatus)) {
                result.cancelled = true;
                return result;
            }
        }

        // Prompt: <|sot|> <lang> <|transcribe|>  (timestamps enabled).
        std::vector<int64_t> prompt{kSotToken, languageToken, kTranscribeToken};

        const double windowFrac = static_cast<double>(cursor) / total;
        const std::vector<int> generated = d->decodeWindow(
            encHidden, prompt, [&](const QString &msg) { report(windowFrac, msg); });

        // --- segment the generated tokens by timestamp pairs ---
        double lastSegmentEnd = -1.0;
        double segStart = -1.0;
        std::vector<int> textTokens;
        auto flush = [&](double end) {
            if (segStart >= 0.0 && !textTokens.empty()) {
                const QString text = d->tokenizer.decode(textTokens).trimmed();
                const double absStart = windowStartSec + segStart;
                const double absEnd = windowStartSec + end;
                if (!text.isEmpty() && !isDegenerateTranscript(text) && absStart < totalSeconds) {
                    SubtitleCue cue;
                    cue.startUs = secondsToUs(absStart);
                    cue.endUs = secondsToUs(std::min(absEnd, totalSeconds));
                    cue.text = text;
                    if (cue.endUs > cue.startUs)
                        result.cues.append(cue);
                }
            }
            textTokens.clear();
        };

        for (const int tok : generated) {
            if (tok >= kTimestampBegin) {
                const double t = (tok - kTimestampBegin) * 0.02;
                if (segStart < 0.0) {
                    segStart = t;
                } else {
                    flush(t);
                    lastSegmentEnd = t;
                    segStart = t;
                }
            } else if (tok != kEosToken) {
                textTokens.push_back(tok);
            }
        }
        // Do not flush trailing text without a closing timestamp. That path was promoting
        // greedy collapse loops (e.g. "අපි අපි අපි…") into cues; openai-whisper only keeps
        // properly closed <|start|> text <|end|> pairs.

        qWarning() << "[whisper] window" << windowStartSec << "tokens" << generated.size()
                   << "cues so far" << result.cues.size()
                   << "langToken" << languageToken;

        double advance = (lastSegmentEnd > 0.05) ? lastSegmentEnd : 30.0;
        advance = std::min(advance, 30.0);
        if (advance < 0.05)
            advance = 30.0;
        // If the window produced nothing usable, still advance so we don't stall.
        if (generated.empty() || lastSegmentEnd < 0.05)
            advance = std::min(30.0, std::max(1.0, totalSeconds - windowStartSec));
        cursor += static_cast<int>(std::llround(advance * kSampleRate));
    }

    if (progress)
        progress(1.0, QStringLiteral("Finishing up…"));
    sortSubtitleCues(result.cues);
    result.language = d->languageByCode.key(languageToken);
    result.ok = true;
    return result;
}

} // namespace drift
