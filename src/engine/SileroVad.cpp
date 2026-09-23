#include "SileroVad.h"

#include "GpuPackageParse.h"
#include "OrtSupport.h"
#include "SpeechAudio.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>

#include <onnxruntime_cxx_api.h>

#include <array>

namespace drift {

namespace {

constexpr const char *kModelFile = "silero_vad.onnx";
constexpr int kContextSamples = 64;

QString resolveModelDir()
{
    const QStringList roots = GpuPackageParse::defaultSearchPaths(
        QStringLiteral("DRIFT_VAD_MODEL_DIR"), QStringLiteral("models/silero-vad"),
        QStringLiteral("vad-model"));
    for (const QString &root : roots) {
        if (QFile::exists(QDir(root).filePath(QLatin1String(kModelFile))))
            return root;
    }
    return {};
}

} // namespace

struct SileroVad::Impl
{
    QMutex mutex;
    bool loaded = false;
    bool loadAttempted = false;
    QString error;
    std::unique_ptr<Ort::Session> session;
    std::vector<std::string> inNames;
    std::vector<std::string> outNames;

    bool ensureLoaded();
};

bool SileroVad::Impl::ensureLoaded()
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
        error = QStringLiteral("Voice activity model not found. Install it from the Addon Manager, "
                               "place it in models/silero-vad, or set DRIFT_VAD_MODEL_DIR.");
        return false;
    }
    try {
        Ort::SessionOptions opts;
        opts.SetIntraOpNumThreads(1);
        opts.SetInterOpNumThreads(1);
        session = std::make_unique<Ort::Session>(
            ort::env(), ort::ortPath(QDir(dir).filePath(QLatin1String(kModelFile))).c_str(), opts);
    } catch (const Ort::Exception &e) {
        error = QString::fromUtf8(e.what());
        return false;
    }
    inNames = ort::sessionNames(*session, true);
    outNames = ort::sessionNames(*session, false);
    if (inNames.size() != 3 || outNames.size() != 2) {
        error = QStringLiteral("%1 is not a Silero VAD v5 export").arg(QLatin1String(kModelFile));
        return false;
    }
    loaded = true;
    return true;
}

SileroVad::SileroVad() : d(std::make_unique<Impl>()) {}
SileroVad::~SileroVad() = default;

SileroVad &SileroVad::instance()
{
    static SileroVad inst;
    return inst;
}

bool SileroVad::modelPresent()
{
    return !resolveModelDir().isEmpty();
}

bool SileroVad::available()
{
    QMutexLocker lock(&d->mutex);
    return d->ensureLoaded();
}

QString SileroVad::lastError() const
{
    return d->error;
}

std::vector<float> SileroVad::probabilities(const std::vector<float> &pcm,
                                            const std::function<bool(double)> &progress)
{
    QMutexLocker lock(&d->mutex);
    if (!d->ensureLoaded())
        return {};

    // Input order in the export is input, state, sr; look them up by name rather than trust it.
    auto indexOf = [](const std::vector<std::string> &names, const char *name) {
        for (size_t i = 0; i < names.size(); ++i) {
            if (names[i] == name)
                return static_cast<int>(i);
        }
        return -1;
    };
    const int inX = indexOf(d->inNames, "input");
    const int inState = indexOf(d->inNames, "state");
    const int inSr = indexOf(d->inNames, "sr");
    if (inX < 0 || inState < 0 || inSr < 0) {
        d->error = QStringLiteral("unexpected Silero VAD inputs");
        return {};
    }

    const size_t windows = (pcm.size() + kWindowSamples - 1) / kWindowSamples;
    std::vector<float> probs;
    probs.reserve(windows);
    std::array<float, kContextSamples + kWindowSamples> frame{};
    std::array<float, 2 * 1 * 128> state{};
    int64_t sr = kSpeechSampleRate;
    const std::array<int64_t, 2> xShape{1, kContextSamples + kWindowSamples};
    const std::array<int64_t, 3> stateShape{2, 1, 128};
    const std::vector<const char *> inN = ort::cstrs(d->inNames);
    const std::vector<const char *> outN = ort::cstrs(d->outNames);

    for (size_t w = 0; w < windows; ++w) {
        // Context = the tail of the previous window, as the reference wrapper feeds it.
        std::copy(frame.end() - kContextSamples, frame.end(), frame.begin());
        const size_t base = w * kWindowSamples;
        for (int i = 0; i < kWindowSamples; ++i)
            frame[kContextSamples + i] = base + i < pcm.size() ? pcm[base + i] : 0.0f;

        std::array<Ort::Value, 3> inputs{Ort::Value{nullptr}, Ort::Value{nullptr}, Ort::Value{nullptr}};
        inputs[inX] = Ort::Value::CreateTensor<float>(ort::cpuMemory(), frame.data(), frame.size(),
                                                      xShape.data(), xShape.size());
        inputs[inState] = Ort::Value::CreateTensor<float>(ort::cpuMemory(), state.data(), state.size(),
                                                          stateShape.data(), stateShape.size());
        inputs[inSr] = Ort::Value::CreateTensor<int64_t>(ort::cpuMemory(), &sr, 1, nullptr, 0);
        try {
            std::vector<Ort::Value> out = d->session->Run(Ort::RunOptions{nullptr}, inN.data(), inputs.data(),
                                                          inputs.size(), outN.data(), outN.size());
            probs.push_back(out[0].GetTensorData<float>()[0]);
            const float *s = out[1].GetTensorData<float>();
            std::copy(s, s + state.size(), state.begin());
        } catch (const Ort::Exception &e) {
            d->error = QString::fromUtf8(e.what());
            qWarning() << "[vad] inference failed:" << d->error;
            return {};
        }
        if (progress && (w % 256) == 0 && !progress(static_cast<double>(w) / windows))
            return {};
    }
    return probs;
}

QList<VadRange> vadSpeechRanges(const std::vector<float> &probs, size_t totalSamples, const VadParams &params)
{
    const int64_t window = SileroVad::kWindowSamples;
    const int64_t rate = kSpeechSampleRate;
    const int64_t minSpeech = rate * params.minSpeechMs / 1000;
    const int64_t minSilence = rate * params.minSilenceMs / 1000;
    const int64_t pad = rate * params.speechPadMs / 1000;
    const float neg = params.negThreshold >= 0 ? params.negThreshold
                                               : std::max(params.threshold - 0.15f, 0.01f);
    const int64_t audioLen = static_cast<int64_t>(totalSamples);

    struct Seg
    {
        int64_t start = 0;
        int64_t end = 0;
    };
    QList<Seg> speeches;
    bool triggered = false;
    Seg current;
    int64_t tempEnd = 0;
    for (size_t i = 0; i < probs.size(); ++i) {
        const float p = probs[i];
        const int64_t pos = window * static_cast<int64_t>(i);
        if (p >= params.threshold && tempEnd)
            tempEnd = 0;
        if (p >= params.threshold && !triggered) {
            triggered = true;
            current.start = pos;
            continue;
        }
        if (p < neg && triggered) {
            if (!tempEnd)
                tempEnd = pos;
            if (pos - tempEnd < minSilence)
                continue;
            current.end = tempEnd;
            if (current.end - current.start > minSpeech)
                speeches.append(current);
            current = {};
            tempEnd = 0;
            triggered = false;
        }
    }
    if (triggered && audioLen - current.start > minSpeech) {
        current.end = audioLen;
        speeches.append(current);
    }

    for (int i = 0; i < speeches.size(); ++i) {
        if (i == 0)
            speeches[i].start = std::max<int64_t>(0, speeches[i].start - pad);
        if (i + 1 < speeches.size()) {
            const int64_t silence = speeches[i + 1].start - speeches[i].end;
            if (silence < 2 * pad) {
                speeches[i].end += silence / 2;
                speeches[i + 1].start = std::max<int64_t>(0, speeches[i + 1].start - silence / 2);
            } else {
                speeches[i].end = std::min(audioLen, speeches[i].end + pad);
                speeches[i + 1].start = std::max<int64_t>(0, speeches[i + 1].start - pad);
            }
        } else {
            speeches[i].end = std::min(audioLen, speeches[i].end + pad);
        }
    }

    QList<VadRange> out;
    for (const Seg &s : std::as_const(speeches))
        out.append({speechSamplesToUs(static_cast<size_t>(s.start)), speechSamplesToUs(static_cast<size_t>(s.end))});
    return out;
}

} // namespace drift
