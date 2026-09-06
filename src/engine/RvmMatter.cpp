#include "engine/RvmMatter.h"

#include "engine/GpuPackageParse.h"
#include "engine/OrtSupport.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>

#include <onnxruntime_cxx_api.h>

#include <algorithm>
#include <iterator>
#include <map>

namespace drift {
namespace {

using drift::ort::ortPath;
using drift::ort::sessionNames;

// The recurrent decoder halves the feature map four times, so the network only accepts sizes that
// are a multiple of 16 — and the project canvas routinely is not (1080 is not). Frames are padded
// up rather than scaled: a resize would slide the alpha off the subject by a fraction of a pixel,
// every frame, which is exactly the error a matte cannot absorb.
constexpr int kSizeMultiple = 16;

// Fixed by the export. Order matters: these are the arrays handed to Session::Run.
const char *const kInputNames[] = {"src", "r1i", "r2i", "r3i", "r4i", "downsample_ratio"};
const char *const kOutputNames[] = {"fgr", "pha", "r1o", "r2o", "r3o", "r4o"};
constexpr int kRecurrentCount = 4;

// Best default first. An empty variant request takes the first of these that is installed:
// MobileNetV3 is several times faster and good enough for almost everything.
const char *const kVariantOrder[] = {"mobilenetv3", "resnet50"};

struct ModelRoot
{
    QString dir;
    QString variant;
    QString fp32; // file name within dir, empty when absent
    QString fp16;
};

int variantRank(const QString &variant)
{
    for (int i = 0; i < int(std::size(kVariantOrder)); ++i) {
        if (variant == QLatin1String(kVariantOrder[i]))
            return i;
    }
    return int(std::size(kVariantOrder));
}

// Every installed model root, best variant first. Unlike the other model classes this cannot stop
// at the first hit: the two RVM addons install side by side and the user chooses between them.
QList<ModelRoot> discoverRoots()
{
    QStringList roots = GpuPackageParse::defaultSearchPaths(QStringLiteral("DRIFT_RVM_MODEL_DIR"),
                                                            QStringLiteral("models/rvm"),
                                                            QStringLiteral("rvm-model"));
    // The addon roots above already cover both packages; this is only the bundled / hand-placed
    // convention for the second one, which cannot share a subdirectory name with the first.
    roots += GpuPackageParse::defaultSearchPaths({}, QStringLiteral("models/rvm-resnet50"));
    roots.removeDuplicates();

    QList<ModelRoot> out;
    for (const QString &root : roots) {
        QFile f(QDir(root).filePath(QStringLiteral("constants.json")));
        if (!f.open(QIODevice::ReadOnly))
            continue;
        const QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();

        ModelRoot m;
        m.dir = root;
        m.variant = obj.value(QStringLiteral("variant")).toString();
        if (m.variant.isEmpty())
            continue;

        // A precision only counts when its file is actually on disk: a half-synced addon must not
        // look installed, and the fp16 weights are optional.
        const QJsonObject files = obj.value(QStringLiteral("files")).toObject();
        const auto pick = [&](const char *key) {
            const QString name = files.value(QLatin1String(key)).toString();
            return !name.isEmpty() && QFile::exists(QDir(root).filePath(name)) ? name : QString();
        };
        m.fp32 = pick("fp32");
        m.fp16 = pick("fp16");
        if (m.fp32.isEmpty() && m.fp16.isEmpty())
            continue;

        // First root wins per variant, the same rule every other catalog resolves duplicates by.
        const bool seen = std::any_of(out.cbegin(), out.cend(), [&](const ModelRoot &r) {
            return r.variant == m.variant;
        });
        if (!seen)
            out.append(m);
    }

    std::sort(out.begin(), out.end(), [](const ModelRoot &a, const ModelRoot &b) {
        return variantRank(a.variant) < variantRank(b.variant);
    });
    return out;
}

inline float toFloat(float v)
{
    return v;
}
inline float toFloat(Ort::Float16_t v)
{
    return v.ToFloat();
}

// Packs the frame into the planar CHW float layout the network wants, padded out to padW x padH by
// replicating the edge pixels. RVM takes plain RGB in 0..1 — no ImageNet mean/std, unlike SAM2.
//
// Templated because the fp16 export wants Ort::Float16_t here, and converting a second time over
// six million elements per frame is not free.
template <typename T>
void packSource(const QImage &rgb, int padW, int padH, T *dst)
{
    const int w = rgb.width();
    const int h = rgb.height();
    for (int c = 0; c < 3; ++c) {
        for (int y = 0; y < padH; ++y) {
            const uchar *line = rgb.constScanLine(std::min(y, h - 1));
            T *row = dst + (qsizetype(c) * padH + y) * padW;
            for (int x = 0; x < padW; ++x)
                row[x] = T(float(line[std::min(x, w - 1) * 3 + c]) / 255.0f);
        }
    }
}

// Crops the padding back off. Alpha is written out soft: thresholding here would throw away the
// only thing RVM has over SAM2.
template <typename T>
QImage unpackAlpha(const T *src, int padW, const QSize &size)
{
    QImage out(size, QImage::Format_Grayscale8);
    for (int y = 0; y < size.height(); ++y) {
        const T *row = src + qsizetype(y) * padW;
        uchar *dst = out.scanLine(y);
        for (int x = 0; x < size.width(); ++x)
            dst[x] = uchar(qRound(qBound(0.0f, toFloat(row[x]), 1.0f) * 255.0f));
    }
    return out;
}

template <typename T>
QImage unpackForeground(const T *src, int padW, int padH, const QSize &size)
{
    QImage out(size, QImage::Format_RGB888);
    for (int c = 0; c < 3; ++c) {
        const T *plane = src + qsizetype(c) * padH * padW;
        for (int y = 0; y < size.height(); ++y) {
            const T *row = plane + qsizetype(y) * padW;
            uchar *dst = out.scanLine(y) + c;
            for (int x = 0; x < size.width(); ++x)
                dst[x * 3] = uchar(qRound(qBound(0.0f, toFloat(row[x]), 1.0f) * 255.0f));
        }
    }
    return out;
}

} // namespace

struct RvmMatter::Impl
{
    struct Loaded
    {
        std::unique_ptr<Ort::Session> session;
        bool fp16 = false;
        QString variant;
    };

    QString error;
    // One session per variant, kept for the life of the process. A Track is created per pass, and
    // reloading the graph for every pass would dominate a short clip.
    std::map<QString, Loaded> loaded;
    // Latched failures, keyed by variant so the reason survives a later successful load of the
    // other one. A model that is present but broken fails the same way every time, whereas a
    // missing one may arrive as an addon mid-session — so only this half is remembered.
    std::map<QString, QString> failed;

    Loaded *ensureLoaded(const QString &requested);
};

RvmMatter::Impl::Loaded *RvmMatter::Impl::ensureLoaded(const QString &requested)
{
    const QList<ModelRoot> roots = discoverRoots();
    if (roots.isEmpty()) {
        error = QStringLiteral("Video matting model not found. Install the People Cutout addon, "
                               "or set DRIFT_RVM_MODEL_DIR.");
        return nullptr;
    }

    ModelRoot root = roots.first();
    if (!requested.isEmpty()) {
        const auto it = std::find_if(roots.cbegin(), roots.cend(), [&](const ModelRoot &r) {
            return r.variant == requested;
        });
        if (it == roots.cend()) {
            error = QStringLiteral("Video matting model \"%1\" is not installed.").arg(requested);
            return nullptr;
        }
        root = *it;
    }

    if (const auto it = loaded.find(root.variant); it != loaded.end()) {
        error.clear();
        return &it->second;
    }
    if (const auto it = failed.find(root.variant); it != failed.end()) {
        error = it->second;
        return nullptr;
    }

    if (!drift::ort::ensureLoaded(&error))
        return nullptr;

    // RVM stays on the plain CPU provider unless an execution provider was asked for by name.
    //
    // This is not a performance trade-off, it is a correctness one. The WebGPU plugin EP runs this
    // graph without any error and returns a plausible-looking foreground, but `pha` comes back
    // near-zero: the matte is silently empty. It is also around five times slower than CPU on this
    // model, so there is nothing on the other side of the scale. Every other model here is happy to
    // take whatever "auto" picks; this one must not.
    //
    // An explicit acceleration choice is still honoured — that setting exists to investigate
    // exactly this — but it says so first, because the failure mode is a blank cutout rather than
    // an error.
    const bool useEp = drift::ort::variantExplicit();
    if (useEp) {
        qWarning("[rvm] using the %s execution provider because it was asked for by name. Video "
                 "matting is only known-good on CPU; WebGPU in particular returns an empty matte.",
                 qUtf8Printable(drift::ort::preferredVariant()));
    }

    // fp16 is only worth loading for a GPU provider. On CPU ONNX Runtime services a half graph
    // through inserted Cast nodes and it comes out slower than fp32.
    const bool wantHalf = useEp && !root.fp16.isEmpty();
    const QString file = wantHalf ? root.fp16 : (root.fp32.isEmpty() ? root.fp16 : root.fp32);
    const bool half = (file == root.fp16);

    Loaded entry;
    entry.fp16 = half;
    entry.variant = root.variant;

    Ort::Env &ortEnv = drift::ort::env();
    try {
        Ort::SessionOptions opts;
        opts.SetIntraOpNumThreads(std::max(1, QThread::idealThreadCount()));
        // sharedArena stays false: RVM is one small session, and the env-wide CUDA allocator exists
        // for SAM2's 449 MiB attention matrix spread across five sessions.
        if (useEp)
            drift::ort::appendRequestedProvider(opts, ortEnv, "rvm");
        entry.session = std::make_unique<Ort::Session>(
            ortEnv, ortPath(QDir(root.dir).filePath(file)).c_str(), opts);
    } catch (const Ort::Exception &e) {
        error = QStringLiteral("Failed to load the video matting model: ")
                + QString::fromUtf8(e.what());
        failed.insert({root.variant, error});
        return nullptr;
    }

    // Check the graph is the export we think it is. Without this a mismatched model fails deep
    // inside ONNX Runtime with a message that names no file.
    const std::vector<std::string> ins = sessionNames(*entry.session, true);
    const std::vector<std::string> outs = sessionNames(*entry.session, false);
    const auto missing = [](const std::vector<std::string> &have, const char *name) {
        return std::find(have.cbegin(), have.cend(), name) == have.cend();
    };
    for (const char *name : kInputNames) {
        if (missing(ins, name)) {
            error = QStringLiteral("%1 is not a Robust Video Matting export: input \"%2\" is "
                                   "missing.")
                        .arg(file, QLatin1String(name));
            failed.insert({root.variant, error});
            return nullptr;
        }
    }
    for (const char *name : kOutputNames) {
        if (missing(outs, name)) {
            error = QStringLiteral("%1 is not a Robust Video Matting export: output \"%2\" is "
                                   "missing.")
                        .arg(file, QLatin1String(name));
            failed.insert({root.variant, error});
            return nullptr;
        }
    }

    error.clear();
    qInfo("[rvm] loaded %s (%s)", qUtf8Printable(file), half ? "fp16" : "fp32");
    return &(loaded[root.variant] = std::move(entry));
}

struct RvmMatter::Track::State
{
    Ort::Session *session = nullptr;
    bool fp16 = false;
    QString variant;

    // The four hidden tensors, carried straight from one Run's outputs into the next Run's inputs.
    // Kept as Ort::Values rather than copied out: on a GPU provider they never have to leave device
    // memory, and nothing on the host needs to look at them.
    std::vector<Ort::Value> recurrent;

    // Reused across frames so a clip does not reallocate megabytes per frame. Also has to outlive
    // each Run: CreateTensor over a host pointer does not take ownership.
    std::vector<float> srcF32;
    std::vector<Ort::Float16_t> srcF16;
    float downsampleRatio = 1.0f;

    QString error;

    bool resetRecurrent();
};

bool RvmMatter::Track::State::resetRecurrent()
{
    // The export declares the initial hidden state as a 1x1x1x1 zero tensor; the network grows it
    // to the real spatial size on the first frame.
    const int64_t shape[4] = {1, 1, 1, 1};
    Ort::AllocatorWithDefaultOptions alloc;
    recurrent.clear();
    recurrent.reserve(kRecurrentCount);
    try {
        for (int i = 0; i < kRecurrentCount; ++i) {
            if (fp16) {
                Ort::Value v = Ort::Value::CreateTensor<Ort::Float16_t>(alloc, shape, 4);
                *v.GetTensorMutableData<Ort::Float16_t>() = Ort::Float16_t(0.0f);
                recurrent.push_back(std::move(v));
            } else {
                Ort::Value v = Ort::Value::CreateTensor<float>(alloc, shape, 4);
                *v.GetTensorMutableData<float>() = 0.0f;
                recurrent.push_back(std::move(v));
            }
        }
    } catch (const Ort::Exception &e) {
        error = QString::fromUtf8(e.what());
        return false;
    }
    return true;
}

RvmMatter::Track::Track(std::unique_ptr<State> state)
    : s(std::move(state))
{
}

RvmMatter::Track::~Track() = default;

QString RvmMatter::Track::variant() const
{
    return s->variant;
}

RvmResult RvmMatter::Track::step(const QImage &frame)
{
    RvmResult result;
    if (frame.isNull()) {
        result.error = QStringLiteral("Empty frame");
        return result;
    }

    const QImage rgb = frame.format() == QImage::Format_RGB888
                           ? frame
                           : frame.convertToFormat(QImage::Format_RGB888);
    const QSize size = rgb.size();
    const int padW = ((size.width() + kSizeMultiple - 1) / kSizeMultiple) * kSizeMultiple;
    const int padH = ((size.height() + kSizeMultiple - 1) / kSizeMultiple) * kSizeMultiple;

    // Upstream's recommended ratio by resolution, as one expression: it lands on 1.0 up to 512px,
    // 0.4 at 720p, 0.27 at 1080p and 0.13 at 4K, matching their table for a portrait-framed subject.
    s->downsampleRatio = std::min(1.0f, 512.0f / float(std::max(size.width(), size.height())));

    const int64_t srcShape[4] = {1, 3, padH, padW};
    const int64_t ratioShape[1] = {1};
    const qsizetype srcCount = qsizetype(3) * padH * padW;

    try {
        const Ort::MemoryInfo &mem = drift::ort::cpuMemory();

        Ort::Value src{nullptr};
        if (s->fp16) {
            s->srcF16.resize(srcCount);
            packSource(rgb, padW, padH, s->srcF16.data());
            src = Ort::Value::CreateTensor<Ort::Float16_t>(mem, s->srcF16.data(), size_t(srcCount),
                                                           srcShape, 4);
        } else {
            s->srcF32.resize(srcCount);
            packSource(rgb, padW, padH, s->srcF32.data());
            src = Ort::Value::CreateTensor<float>(mem, s->srcF32.data(), size_t(srcCount), srcShape,
                                                  4);
        }

        // downsample_ratio is fp32 in both exports.
        Ort::Value ratio =
            Ort::Value::CreateTensor<float>(mem, &s->downsampleRatio, 1, ratioShape, 1);

        std::vector<Ort::Value> inputs;
        inputs.reserve(6);
        inputs.push_back(std::move(src));
        for (Ort::Value &r : s->recurrent)
            inputs.push_back(std::move(r)); // handed back below, from this Run's own outputs
        inputs.push_back(std::move(ratio));

        std::vector<Ort::Value> outputs =
            s->session->Run(Ort::RunOptions{nullptr}, kInputNames, inputs.data(), inputs.size(),
                            kOutputNames, std::size(kOutputNames));

        if (s->fp16) {
            result.foreground = unpackForeground(outputs[0].GetTensorData<Ort::Float16_t>(), padW,
                                                 padH, size);
            result.alpha = unpackAlpha(outputs[1].GetTensorData<Ort::Float16_t>(), padW, size);
        } else {
            result.foreground =
                unpackForeground(outputs[0].GetTensorData<float>(), padW, padH, size);
            result.alpha = unpackAlpha(outputs[1].GetTensorData<float>(), padW, size);
        }

        s->recurrent.clear();
        for (int i = 0; i < kRecurrentCount; ++i)
            s->recurrent.push_back(std::move(outputs[2 + i]));
    } catch (const Ort::Exception &e) {
        result.error = QStringLiteral("Video matting failed: ") + QString::fromUtf8(e.what());
        return result;
    }

    result.ok = true;
    return result;
}

RvmMatter::RvmMatter()
    : d(std::make_unique<Impl>())
{
}

RvmMatter::~RvmMatter() = default;

RvmMatter &RvmMatter::instance()
{
    // Deliberately leaked, for the same reason as Sam2Segmenter: a function-local static is
    // destroyed during exit, by which point the CUDA driver has already run its own teardown, and
    // freeing the session then aborts the process.
    static RvmMatter *s = new RvmMatter;
    return *s;
}

bool RvmMatter::modelPresent()
{
    return !discoverRoots().isEmpty();
}

QStringList RvmMatter::installedVariants()
{
    QStringList out;
    for (const ModelRoot &root : discoverRoots())
        out.append(root.variant);
    return out;
}

bool RvmMatter::available(const QString &variant)
{
    return d->ensureLoaded(variant) != nullptr;
}

QString RvmMatter::lastError() const
{
    return d->error;
}

std::unique_ptr<RvmMatter::Track> RvmMatter::newTrack(const QString &variant)
{
    Impl::Loaded *entry = d->ensureLoaded(variant);
    if (!entry)
        return nullptr;

    auto state = std::make_unique<Track::State>();
    state->session = entry->session.get();
    state->fp16 = entry->fp16;
    state->variant = entry->variant;
    if (!state->resetRecurrent()) {
        d->error = state->error;
        return nullptr;
    }
    return std::unique_ptr<Track>(new Track(std::move(state)));
}

} // namespace drift
