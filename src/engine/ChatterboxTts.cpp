#include "ChatterboxTts.h"

#include "ChatterboxTokenizer.h"
#include "GpuPackageParse.h"
#include "OrtSupport.h"

#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <cmath>
#include <random>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace drift {

namespace {

using drift::ort::cstrs;
using drift::ort::elementCount;
using drift::ort::ortPath;
using drift::ort::sessionNames;

const char *const kRequiredFiles[] = {
    "speech_encoder.onnx",     "speech_encoder.onnx_data", "embed_tokens.onnx",
    "embed_tokens.onnx_data",  "language_model_q4.onnx",   "language_model_q4.onnx_data",
    "s3gen_prepare.onnx",      "s3gen_estimator.onnx",     "s3gen_vocoder.onnx",
    "tokenizer.json",          "Cangjie5_TC.json",         "default_voice.wav",
    "constants.json",
};

constexpr int kMaxVoiceSamples = 20 * ChatterboxTts::kSampleRate;
constexpr int kMaxNewTokens = 1000;
constexpr int kChunkGapSamples = ChatterboxTts::kSampleRate * 120 / 1000;
constexpr int kSpeechVocab = 6561; // ids from here up are control tokens, not speech
constexpr quint32 kVoiceMagic = 0x56425444; // "DTBV"
constexpr quint32 kVoiceVersion = 1;
constexpr qint64 kMaxVoiceTensorElements = 64 * 1024 * 1024;

const char *const kLanguages[] = {"ar", "da", "de", "el", "en", "es", "fi", "fr", "hi", "it", "ko",
                                  "ms", "nl", "no", "pl", "pt", "ru", "sv", "sw", "tr", "zh"};

template <typename T>
void writeTensor(QDataStream &out, const std::vector<T> &data, const std::vector<int64_t> &shape)
{
    out << quint32(shape.size());
    for (const int64_t d : shape)
        out << qint64(d);
    out.writeRawData(reinterpret_cast<const char *>(data.data()), int(data.size() * sizeof(T)));
}

template <typename T>
bool readTensor(QDataStream &in, std::vector<T> &data, std::vector<int64_t> &shape)
{
    quint32 rank = 0;
    in >> rank;
    if (in.status() != QDataStream::Ok || rank == 0 || rank > 4)
        return false;
    shape.resize(rank);
    qint64 count = 1;
    for (quint32 i = 0; i < rank; ++i) {
        qint64 d = 0;
        in >> d;
        if (in.status() != QDataStream::Ok || d <= 0 || d > kMaxVoiceTensorElements)
            return false;
        shape[i] = d;
        count *= d;
        if (count > kMaxVoiceTensorElements)
            return false;
    }
    data.resize(size_t(count));
    const int bytes = int(count * sizeof(T));
    return in.readRawData(reinterpret_cast<char *>(data.data()), bytes) == bytes;
}

} // namespace

QString resolveChatterboxModelDir()
{
    const QStringList roots = GpuPackageParse::defaultSearchPaths(
        QStringLiteral("DRIFT_CHATTERBOX_MODEL_DIR"), QStringLiteral("models/chatterbox-multilingual"),
        QStringLiteral("tts-model"));
    for (const QString &root : roots) {
        bool complete = true;
        for (const char *f : kRequiredFiles)
            complete = complete && QFile::exists(QDir(root).filePath(QLatin1String(f)));
        if (complete)
            return root;
    }
    return {};
}

QByteArray VoiceConditioning::serialize() const
{
    QByteArray out;
    QDataStream s(&out, QIODevice::WriteOnly);
    s.setByteOrder(QDataStream::LittleEndian);
    s << kVoiceMagic << kVoiceVersion;
    writeTensor(s, condEmb, condEmbShape);
    writeTensor(s, promptToken, promptTokenShape);
    writeTensor(s, refXVector, refXVectorShape);
    writeTensor(s, promptFeat, promptFeatShape);
    return out;
}

std::optional<VoiceConditioning> VoiceConditioning::deserialize(const QByteArray &data)
{
    QDataStream s(data);
    s.setByteOrder(QDataStream::LittleEndian);
    quint32 magic = 0, version = 0;
    s >> magic >> version;
    if (s.status() != QDataStream::Ok || magic != kVoiceMagic || version != kVoiceVersion)
        return std::nullopt;
    VoiceConditioning v;
    if (!readTensor(s, v.condEmb, v.condEmbShape) || !readTensor(s, v.promptToken, v.promptTokenShape)
        || !readTensor(s, v.refXVector, v.refXVectorShape)
        || !readTensor(s, v.promptFeat, v.promptFeatShape))
        return std::nullopt;
    return v;
}

struct ChatterboxTts::Impl
{
    bool loaded = false;
    QString modelDir;
    ChatterboxTokenizer tokenizer;

    int startToken = 6561;
    int stopToken = 6562;
    int numLayers = 30;
    int numKvHeads = 16;
    int headDim = 64;
    float repetitionPenalty = 1.2f;
    int cfmSteps = 10;
    float cfmCfgRate = 0.7f;
    bool cfmCosine = true;
    float cfmTemperature = 1.0f;

    std::unique_ptr<Ort::Session> speechEncoder, embedTokens, languageModel;
    std::unique_ptr<Ort::Session> s3Prepare, s3Estimator, s3Vocoder;
    std::vector<std::string> lmIn, lmOut;
    std::vector<int> presentToPast; // output index -> input index of its cache slot

    bool loadConstants(QString *err);
    bool ensureLoaded(QString *err);
    std::vector<float> decodeSpeech(std::vector<int64_t> &speech, const VoiceConditioning &voice,
                                    const std::function<bool(double)> &report, QString *err);
    std::vector<float> generateChunk(const QString &text, const QString &lang,
                                     const VoiceConditioning &voice, float exaggeration,
                                     const std::function<bool(double)> &progress, double base,
                                     double span, bool *cancelled, QString *err);
};

bool ChatterboxTts::Impl::loadConstants(QString *err)
{
    QFile f(QDir(modelDir).filePath(QStringLiteral("constants.json")));
    if (!f.open(QIODevice::ReadOnly)) {
        if (err)
            *err = QStringLiteral("Cannot read Chatterbox constants.json");
        return false;
    }
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    if (o.value(QStringLiteral("sampleRate")).toInt(kSampleRate) != kSampleRate) {
        if (err)
            *err = QStringLiteral("Unexpected Chatterbox sample rate");
        return false;
    }
    startToken = o.value(QStringLiteral("startSpeechToken")).toInt(startToken);
    stopToken = o.value(QStringLiteral("stopSpeechToken")).toInt(stopToken);
    numLayers = o.value(QStringLiteral("numLayers")).toInt(numLayers);
    numKvHeads = o.value(QStringLiteral("numKvHeads")).toInt(numKvHeads);
    headDim = o.value(QStringLiteral("headDim")).toInt(headDim);
    repetitionPenalty = float(o.value(QStringLiteral("repetitionPenalty")).toDouble(repetitionPenalty));
    cfmSteps = std::clamp(o.value(QStringLiteral("cfmSteps")).toInt(cfmSteps), 1, 100);
    cfmCfgRate = float(o.value(QStringLiteral("cfmCfgRate")).toDouble(cfmCfgRate));
    cfmCosine = o.value(QStringLiteral("cfmSchedule")).toString(QStringLiteral("cosine"))
        == QLatin1String("cosine");
    cfmTemperature = float(o.value(QStringLiteral("cfmTemperature")).toDouble(cfmTemperature));
    return true;
}

bool ChatterboxTts::Impl::ensureLoaded(QString *err)
{
    if (loaded)
        return true;

    modelDir = resolveChatterboxModelDir();
    if (modelDir.isEmpty()) {
        if (err)
            *err = QStringLiteral("Text-to-speech model not found. Install the Text to Speech "
                                  "addon, or set DRIFT_CHATTERBOX_MODEL_DIR.");
        return false;
    }
    if (!drift::ort::ensureLoaded(err))
        return false;
    if (!loadConstants(err) || !tokenizer.load(modelDir, err))
        return false;

    Ort::Env &ortEnv = drift::ort::env();
    const QDir dir(modelDir);
    const bool ok = drift::ort::buildSessions(ortEnv, "tts", false, err, [&](Ort::SessionOptions &opts) {
        auto open = [&](const char *name) {
            return std::make_unique<Ort::Session>(
                ortEnv, ortPath(dir.filePath(QLatin1String(name))).c_str(), opts);
        };
        speechEncoder = open("speech_encoder.onnx");
        embedTokens = open("embed_tokens.onnx");
        languageModel = open("language_model_q4.onnx");
        s3Prepare = open("s3gen_prepare.onnx");
        s3Estimator = open("s3gen_estimator.onnx");
        s3Vocoder = open("s3gen_vocoder.onnx");
    });
    if (!ok) {
        if (err)
            *err = QStringLiteral("Failed to load text-to-speech model: ") + *err;
        speechEncoder.reset();
        embedTokens.reset();
        languageModel.reset();
        s3Prepare.reset();
        s3Estimator.reset();
        s3Vocoder.reset();
        return false;
    }
    lmIn = sessionNames(*languageModel, true);
    lmOut = sessionNames(*languageModel, false);
    presentToPast.assign(lmOut.size(), -1);
    for (size_t o = 1; o < lmOut.size(); ++o) {
        const std::string want = "past_key_values." + lmOut[o].substr(lmOut[o].find('.') + 1);
        const auto it = std::find(lmIn.begin(), lmIn.end(), want);
        if (it == lmIn.end()) {
            if (err)
                *err = QStringLiteral("Unexpected language model output ")
                    + QString::fromStdString(lmOut[o]);
            return false;
        }
        presentToPast[o] = int(it - lmIn.begin());
    }
    loaded = true;
    return true;
}

std::vector<float> ChatterboxTts::Impl::generateChunk(
    const QString &text, const QString &lang, const VoiceConditioning &voice, float exaggeration,
    const std::function<bool(double)> &progress, double base, double span, bool *cancelled,
    QString *err)
{
    std::vector<int64_t> ids = tokenizer.encode(text, lang);
    const int64_t textLen = int64_t(ids.size());
    std::vector<int64_t> positions(ids.size());
    for (size_t i = 0; i < ids.size(); ++i)
        positions[i] = ids[i] >= startToken ? 0 : int64_t(i) - 1;
    float exagg = exaggeration;

    const Ort::MemoryInfo &mem = drift::ort::cpuMemory();
    const char *embIn[] = {"input_ids", "position_ids", "exaggeration"};
    const char *embOut[] = {"inputs_embeds"};
    const int64_t exaggShape[] = {1};

    auto embed = [&](std::vector<int64_t> &idv, std::vector<int64_t> &posv) {
        const int64_t shape[] = {1, int64_t(idv.size())};
        Ort::Value in[3] = {
            Ort::Value::CreateTensor<int64_t>(mem, idv.data(), idv.size(), shape, 2),
            Ort::Value::CreateTensor<int64_t>(mem, posv.data(), posv.size(), shape, 2),
            Ort::Value::CreateTensor<float>(mem, &exagg, 1, exaggShape, 1),
        };
        return embedTokens->Run(Ort::RunOptions{nullptr}, embIn, in, 3, embOut, 1);
    };

    const double expectedTokens = std::max<double>(60.0, text.size() * 2.0);
    auto report = [&](double frac) {
        if (progress && !progress(base + span * std::clamp(frac, 0.0, 1.0))) {
            *cancelled = true;
            return false;
        }
        return true;
    };

    std::vector<int64_t> generated{startToken};
    try {
        auto textEmb = embed(ids, positions);
        const float *textData = textEmb[0].GetTensorData<float>();
        const auto textShape = textEmb[0].GetTensorTypeAndShapeInfo().GetShape();
        const int64_t hidden = textShape.back();

        std::vector<float> embeds;
        embeds.reserve(voice.condEmb.size() + size_t(textLen * hidden));
        embeds.insert(embeds.end(), voice.condEmb.begin(), voice.condEmb.end());
        embeds.insert(embeds.end(), textData, textData + textLen * hidden);
        int64_t seqLen = voice.condEmbShape.at(1) + textLen;

        std::vector<Ort::Value> cache;
        cache.reserve(lmIn.size());
        const std::vector<float> noData(1);
        const int64_t pastShape[] = {1, numKvHeads, 0, headDim};
        for (const std::string &name : lmIn) {
            if (name.rfind("past_key_values.", 0) == 0)
                cache.push_back(Ort::Value::CreateTensor<float>(
                    mem, const_cast<float *>(noData.data()), 0, pastShape, 4));
            else
                cache.emplace_back(nullptr);
        }

        std::vector<int64_t> mask(size_t(seqLen), 1);
        std::vector<char> penalised;
        std::vector<int64_t> history{startToken};
        const std::vector<const char *> inN = cstrs(lmIn);
        const std::vector<const char *> outN = cstrs(lmOut);

        for (int step = 0; step < kMaxNewTokens; ++step) {
            const int64_t embShape[] = {1, seqLen, hidden};
            const int64_t maskShape[] = {1, int64_t(mask.size())};
            std::vector<Ort::Value> ins;
            ins.reserve(lmIn.size());
            for (size_t k = 0; k < lmIn.size(); ++k) {
                if (lmIn[k] == "inputs_embeds")
                    ins.push_back(Ort::Value::CreateTensor<float>(mem, embeds.data(), embeds.size(),
                                                                  embShape, 3));
                else if (lmIn[k] == "attention_mask")
                    ins.push_back(Ort::Value::CreateTensor<int64_t>(mem, mask.data(), mask.size(),
                                                                    maskShape, 2));
                else
                    ins.push_back(std::move(cache[k]));
            }
            auto outs = languageModel->Run(Ort::RunOptions{nullptr}, inN.data(), ins.data(),
                                           ins.size(), outN.data(), outN.size());

            const auto lshape = outs[0].GetTensorTypeAndShapeInfo().GetShape();
            const int64_t vocabSize = lshape.back();
            const int64_t rows = lshape[lshape.size() - 2];
            const float *last = outs[0].GetTensorData<float>() + (rows - 1) * vocabSize;
            std::vector<float> scores(last, last + vocabSize);
            if (penalised.empty()) {
                penalised.assign(size_t(vocabSize), 0);
                penalised[size_t(startToken)] = 1;
            }
            for (const int64_t t : history) {
                if (t < 0 || t >= vocabSize)
                    continue;
                float &sc = scores[size_t(t)];
                sc = sc < 0 ? sc * repetitionPenalty : sc / repetitionPenalty;
            }
            const int64_t next = std::max_element(scores.begin(), scores.end()) - scores.begin();
            generated.push_back(next);
            if (next == stopToken)
                break;
            if (next >= 0 && next < vocabSize && !penalised[size_t(next)]) {
                penalised[size_t(next)] = 1;
                history.push_back(next);
            }
            if (!report(double(step + 1) / expectedTokens * 0.95))
                return {};

            for (size_t i = 1; i < outs.size(); ++i)
                cache[size_t(presentToPast[i])] = std::move(outs[i]);

            std::vector<int64_t> idv{next}, posv{int64_t(step) + 1};
            auto e = embed(idv, posv);
            const auto eshape = e[0].GetTensorTypeAndShapeInfo().GetShape();
            embeds.assign(e[0].GetTensorData<float>(),
                          e[0].GetTensorData<float>() + elementCount(eshape));
            seqLen = 1;
            mask.push_back(1);
        }
    } catch (const Ort::Exception &e) {
        if (err)
            *err = QStringLiteral("Speech generation failed: ") + QString::fromUtf8(e.what());
        return {};
    }

    std::vector<int64_t> speech = voice.promptToken;
    for (size_t i = 1; i < generated.size(); ++i) {
        if (generated[i] < kSpeechVocab)
            speech.push_back(generated[i]);
    }

    return decodeSpeech(speech, voice, report, err);
}

std::vector<float> ChatterboxTts::Impl::decodeSpeech(std::vector<int64_t> &speech,
                                                     const VoiceConditioning &voice,
                                                     const std::function<bool(double)> &report,
                                                     QString *err)
{
    try {
        const Ort::MemoryInfo &m = drift::ort::cpuMemory();
        const int64_t tokShape[] = {1, int64_t(speech.size())};
        Ort::Value prepIn[3] = {
            Ort::Value::CreateTensor<int64_t>(m, speech.data(), speech.size(), tokShape, 2),
            Ort::Value::CreateTensor<float>(m, const_cast<float *>(voice.refXVector.data()),
                                            voice.refXVector.size(), voice.refXVectorShape.data(),
                                            voice.refXVectorShape.size()),
            Ort::Value::CreateTensor<float>(m, const_cast<float *>(voice.promptFeat.data()),
                                            voice.promptFeat.size(), voice.promptFeatShape.data(),
                                            voice.promptFeatShape.size()),
        };
        const char *prepInNames[] = {"speech_tokens", "speaker_embeddings", "speaker_features"};
        const char *prepOutNames[] = {"mu", "spks", "cond"};
        auto prep = s3Prepare->Run(Ort::RunOptions{nullptr}, prepInNames, prepIn, 3, prepOutNames, 3);

        const auto muShape = prep[0].GetTensorTypeAndShapeInfo().GetShape();
        const auto spksShape = prep[1].GetTensorTypeAndShapeInfo().GetShape();
        const int64_t frames = muShape.at(2);
        const int64_t promptFrames = voice.promptFeatShape.at(1);
        if (frames <= promptFrames) {
            if (err)
                *err = QStringLiteral("Speech decoding failed: no frames beyond the voice prompt");
            return {};
        }

        // Starting noise is redrawn on every call, as in the reference implementation.
        const int64_t count = elementCount(muShape);
        std::vector<float> x(static_cast<size_t>(count));
        std::mt19937 rng{std::random_device{}()};
        std::normal_distribution<float> normal(0.0f, cfmTemperature);
        for (float &v : x)
            v = normal(rng);

        float t = 0.0f;
        const int64_t scalarShape[] = {1};
        Ort::Value stepIn[6] = {
            Ort::Value::CreateTensor<float>(m, x.data(), x.size(), muShape.data(), muShape.size()),
            Ort::Value::CreateTensor<float>(m, const_cast<float *>(prep[0].GetTensorData<float>()),
                                            size_t(count), muShape.data(), muShape.size()),
            Ort::Value::CreateTensor<float>(m, const_cast<float *>(prep[2].GetTensorData<float>()),
                                            size_t(count), muShape.data(), muShape.size()),
            Ort::Value::CreateTensor<float>(m, &t, 1, scalarShape, 1),
            Ort::Value::CreateTensor<float>(m, const_cast<float *>(prep[1].GetTensorData<float>()),
                                            size_t(elementCount(spksShape)), spksShape.data(),
                                            spksShape.size()),
            Ort::Value::CreateTensor<float>(m, &cfmCfgRate, 1, scalarShape, 1),
        };
        const char *stepInNames[] = {"x", "mu", "cond", "t", "spks", "cfg_rate"};
        const char *stepOutNames[] = {"dphi_dt"};

        auto schedule = [&](int i) {
            const double u = double(i) / cfmSteps;
            return float(cfmCosine ? 1.0 - std::cos(u * 0.5 * M_PI) : u);
        };
        for (int i = 0; i < cfmSteps; ++i) {
            t = schedule(i);
            const float dt = schedule(i + 1) - t;
            auto out = s3Estimator->Run(Ort::RunOptions{nullptr}, stepInNames, stepIn, 6,
                                        stepOutNames, 1);
            const float *dphi = out[0].GetTensorData<float>();
            for (int64_t k = 0; k < count; ++k)
                x[size_t(k)] += dt * dphi[k];
            if (!report(0.95 + 0.05 * double(i + 1) / (cfmSteps + 1)))
                return {};
        }

        const int64_t melFrames = frames - promptFrames;
        const int64_t bins = muShape.at(1);
        std::vector<float> mel(size_t(bins * melFrames));
        for (int64_t c = 0; c < bins; ++c)
            std::copy_n(x.begin() + c * frames + promptFrames, melFrames, mel.begin() + c * melFrames);
        const int64_t melShape[] = {1, bins, melFrames};
        Ort::Value vocIn = Ort::Value::CreateTensor<float>(m, mel.data(), mel.size(), melShape, 3);
        const char *vocInName = "mel";
        const char *vocOutName = "waveform";
        auto res = s3Vocoder->Run(Ort::RunOptions{nullptr}, &vocInName, &vocIn, 1, &vocOutName, 1);
        const int64_t n = elementCount(res[0].GetTensorTypeAndShapeInfo().GetShape());
        const float *data = res[0].GetTensorData<float>();
        std::vector<float> wav(data, data + n);
        report(1.0);
        return wav;
    } catch (const Ort::Exception &e) {
        if (err)
            *err = QStringLiteral("Speech decoding failed: ") + QString::fromUtf8(e.what());
        return {};
    }
}

ChatterboxTts::ChatterboxTts()
    : d(std::make_unique<Impl>())
{
}

ChatterboxTts::~ChatterboxTts() = default;

bool ChatterboxTts::load(QString *err)
{
    return d->ensureLoaded(err);
}

QStringList ChatterboxTts::supportedLanguages()
{
    QStringList out;
    for (const char *l : kLanguages)
        out << QLatin1String(l);
    return out;
}

QString ChatterboxTts::modelDir() const
{
    return d->modelDir;
}

std::optional<VoiceConditioning> ChatterboxTts::encodeVoice(const std::vector<float> &pcm24k,
                                                            QString *err)
{
    if (!d->ensureLoaded(err))
        return std::nullopt;
    if (pcm24k.empty()) {
        if (err)
            *err = QStringLiteral("The reference voice has no audio.");
        return std::nullopt;
    }

    std::vector<float> audio(pcm24k.begin(),
                             pcm24k.begin() + std::min<size_t>(pcm24k.size(), kMaxVoiceSamples));
    try {
        const int64_t shape[] = {1, int64_t(audio.size())};
        Ort::Value in = Ort::Value::CreateTensor<float>(drift::ort::cpuMemory(), audio.data(),
                                                        audio.size(), shape, 2);
        const char *inName = "audio_values";
        const char *outNames[] = {"audio_features", "audio_tokens", "speaker_embeddings",
                                  "speaker_features"};
        auto outs = d->speechEncoder->Run(Ort::RunOptions{nullptr}, &inName, &in, 1, outNames, 4);

        VoiceConditioning v;
        auto grab = [&](size_t i, auto &data, std::vector<int64_t> &shapeOut) {
            using T = typename std::decay_t<decltype(data)>::value_type;
            shapeOut = outs[i].GetTensorTypeAndShapeInfo().GetShape();
            const T *p = outs[i].GetTensorData<T>();
            data.assign(p, p + elementCount(shapeOut));
        };
        grab(0, v.condEmb, v.condEmbShape);
        grab(1, v.promptToken, v.promptTokenShape);
        grab(2, v.refXVector, v.refXVectorShape);
        grab(3, v.promptFeat, v.promptFeatShape);
        return v;
    } catch (const Ort::Exception &e) {
        if (err)
            *err = QStringLiteral("Voice encoding failed: ") + QString::fromUtf8(e.what());
        return std::nullopt;
    }
}

std::vector<float> ChatterboxTts::synthesize(const QString &text, const QString &lang,
                                             const VoiceConditioning &voice, float exaggeration,
                                             const std::function<bool(double)> &progress,
                                             QString *err)
{
    if (!d->ensureLoaded(err))
        return {};
    if (!supportedLanguages().contains(lang)) {
        if (err)
            *err = QStringLiteral("Unsupported language: ") + lang;
        return {};
    }
    if (voice.condEmbShape.size() < 2) {
        if (err)
            *err = QStringLiteral("Invalid voice conditioning.");
        return {};
    }

    QStringList chunks = ChatterboxTokenizer::splitForSynthesis(text);
    if (chunks.isEmpty())
        chunks << QString();

    std::vector<float> out;
    bool cancelled = false;
    const double span = 1.0 / double(chunks.size());
    for (qsizetype i = 0; i < chunks.size(); ++i) {
        const std::vector<float> wav = d->generateChunk(chunks.at(i), lang, voice, exaggeration,
                                                        progress, double(i) * span, span, &cancelled,
                                                        err);
        if (cancelled)
            return {};
        if (wav.empty())
            return {};
        if (i > 0)
            out.insert(out.end(), size_t(kChunkGapSamples), 0.0f);
        out.insert(out.end(), wav.begin(), wav.end());
    }
    return out;
}

} // namespace drift
