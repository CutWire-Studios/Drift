#include "engine/VdaDepth.h"

#include "engine/GpuPackageParse.h"
#include "engine/OrtSupport.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <onnxruntime_cxx_api.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace drift {

// --- stitcher -----------------------------------------------------------------------------------

namespace {

// Least-squares a, b minimising |a * prediction + b - target|^2, as upstream's
// compute_scale_and_shift with an all-true mask.
void fitScaleShift(const std::vector<const float *> &prediction,
                   const std::vector<const float *> &target, size_t pixels, double *scale,
                   double *shift)
{
    double a00 = 0.0, a01 = 0.0, a11 = 0.0, b0 = 0.0, b1 = 0.0;
    for (size_t f = 0; f < prediction.size(); ++f) {
        const float *p = prediction[f];
        const float *t = target[f];
        for (size_t i = 0; i < pixels; ++i) {
            a00 += double(p[i]) * p[i];
            a01 += p[i];
            b0 += double(p[i]) * t[i];
            b1 += t[i];
        }
        a11 += double(pixels);
    }
    *scale = 1.0;
    *shift = 0.0;
    const double det = a00 * a11 - a01 * a01;
    if (det != 0.0) {
        *scale = (a11 * b0 - a01 * b1) / det;
        *shift = (-a01 * b0 + a00 * b1) / det;
    }
}

std::vector<float> aligned(const float *raw, size_t pixels, double scale, double shift)
{
    std::vector<float> out(pixels);
    for (size_t i = 0; i < pixels; ++i)
        out[i] = std::max(0.0f, float(raw[i] * scale + shift));
    return out;
}

} // namespace

VdaStitcher::VdaStitcher(size_t pixelsPerFrame)
    : m_pixels(pixelsPerFrame)
{
}

void VdaStitcher::addWindow(const float *raw, const Emit &out)
{
    const auto frame = [&](int i) { return raw + size_t(i) * m_pixels; };

    if (m_first) {
        m_first = false;
        for (int i = 0; i < kVdaWindow; ++i)
            m_tail.emplace_back(frame(i), frame(i) + m_pixels);
        m_refFirst.assign(frame(0), frame(0) + m_pixels);
        m_refMiddle.assign(frame(12), frame(12) + m_pixels);
        emitSettled(kVdaInterp, out);
        return;
    }

    // Positions 0 and 1 of this window re-estimated the two long-range keyframes; fitting them onto
    // what the previous windows said about the same frames puts this window on the same scale.
    fitScaleShift({frame(0), frame(1)}, {m_refFirst.data(), m_refMiddle.data()}, m_pixels,
                  &m_scale, &m_shift);

    // Positions 2..9 are the last eight frames of the previous window, which are still pending.
    for (int i = 0; i < kVdaInterp; ++i) {
        const std::vector<float> post = aligned(frame(kVdaOverlap - kVdaInterp + i), m_pixels,
                                                m_scale, m_shift);
        const float w = float(i) / float(kVdaInterp - 1);
        std::vector<float> &pre = m_tail[m_tail.size() - kVdaInterp + i];
        for (size_t p = 0; p < m_pixels; ++p)
            pre[p] = pre[p] * (1.0f - w) + post[p] * w;
    }
    for (int i = kVdaOverlap; i < kVdaWindow; ++i)
        m_tail.push_back(aligned(frame(i), m_pixels, m_scale, m_shift));
    m_refMiddle = aligned(frame(12), m_pixels, m_scale, m_shift);
    emitSettled(kVdaInterp, out);
}

void VdaStitcher::finish(const Emit &out)
{
    emitSettled(0, out);
}

void VdaStitcher::emitSettled(size_t keep, const Emit &out)
{
    while (m_tail.size() > keep) {
        out(m_emitted++, m_tail.front().data());
        m_tail.pop_front();
    }
}

// --- model --------------------------------------------------------------------------------------

namespace {

using drift::ort::ortPath;
using drift::ort::sessionNames;

constexpr int kPatch = 14;
// The export's dynamic range for H/14 and W/14.
constexpr int kMinPatches = 8;
constexpr int kMaxPatches = 160;
constexpr int kFeatures = 4;
const char *const kEncoderInputs[] = {"image"};
const char *const kEncoderOutputs[] = {"feat1", "feat2", "feat3", "feat4"};
const char *const kHeadInputs[] = {"feat1", "feat2", "feat3", "feat4"};
const char *const kHeadOutputs[] = {"disparity"};

struct ModelFiles
{
    QString encoder;
    QString head;
};

struct ModelRoot
{
    QString dir;
    QString variant;
    ModelFiles fp32;
    ModelFiles fp16;
    std::array<float, 3> mean{};
    std::array<float, 3> std{};
};

// The windowing constants are compiled in (VdaStitcher), so a model exported with different ones
// must be refused rather than stitched wrongly.
bool windowingMatches(const QJsonObject &obj)
{
    if (obj.value(QStringLiteral("window")).toInt() != kVdaWindow
        || obj.value(QStringLiteral("overlap")).toInt() != kVdaOverlap
        || obj.value(QStringLiteral("interpLen")).toInt() != kVdaInterp) {
        return false;
    }
    const QJsonArray keyframes = obj.value(QStringLiteral("keyframes")).toArray();
    if (keyframes.size() != kVdaOverlap)
        return false;
    for (int i = 0; i < kVdaOverlap; ++i) {
        if (keyframes.at(i).toInt() != kVdaKeyframes[i])
            return false;
    }
    return true;
}

bool discoverRoot(ModelRoot *out, QString *why)
{
    const QStringList roots = GpuPackageParse::defaultSearchPaths(
        QStringLiteral("DRIFT_DEPTH_MODEL_DIR"), QStringLiteral("models/depth"),
        QStringLiteral("depth-model"));
    for (const QString &root : roots) {
        QFile f(QDir(root).filePath(QStringLiteral("constants.json")));
        if (!f.open(QIODevice::ReadOnly))
            continue;
        const QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
        if (!windowingMatches(obj)) {
            if (why)
                *why = QStringLiteral("The depth model in %1 was exported with windowing this "
                                      "version of Drift does not support.")
                           .arg(root);
            continue;
        }

        ModelRoot m;
        m.dir = root;
        m.variant = obj.value(QStringLiteral("variant")).toString();
        // A precision only counts when both of its graphs are on disk: a half-synced addon must
        // not look installed.
        const QJsonObject files = obj.value(QStringLiteral("files")).toObject();
        const auto pick = [&](const char *precision) {
            const QJsonObject o = files.value(QLatin1String(precision)).toObject();
            ModelFiles f{o.value(QStringLiteral("encoder")).toString(),
                         o.value(QStringLiteral("head")).toString()};
            const auto present = [&](const QString &name) {
                return !name.isEmpty() && QFile::exists(QDir(root).filePath(name));
            };
            return present(f.encoder) && present(f.head) ? f : ModelFiles{};
        };
        m.fp32 = pick("fp32");
        m.fp16 = pick("fp16");
        if (m.variant.isEmpty() || (m.fp32.encoder.isEmpty() && m.fp16.encoder.isEmpty()))
            continue;

        const QJsonArray mean = obj.value(QStringLiteral("mean")).toArray();
        const QJsonArray std = obj.value(QStringLiteral("std")).toArray();
        if (mean.size() != 3 || std.size() != 3)
            continue;
        for (int c = 0; c < 3; ++c) {
            m.mean[size_t(c)] = float(mean.at(c).toDouble());
            m.std[size_t(c)] = float(std.at(c).toDouble());
        }
        *out = m;
        return true;
    }
    return false;
}

} // namespace

struct VdaDepth::Impl
{
    std::unique_ptr<Ort::Session> encoder;
    std::unique_ptr<Ort::Session> head;
    ModelRoot root;
    QString error;
    // A model that is present but broken fails the same way every time; a missing one may arrive
    // as an addon mid-session, so only this half is latched.
    QString failed;

    bool ensureLoaded();
};

bool VdaDepth::Impl::ensureLoaded()
{
    if (encoder && head) {
        error.clear();
        return true;
    }
    if (!failed.isEmpty()) {
        error = failed;
        return false;
    }

    QString why;
    if (!discoverRoot(&root, &why)) {
        error = why.isEmpty() ? QStringLiteral("Depth model not found. Install the Depth addon, or "
                                               "set DRIFT_DEPTH_MODEL_DIR.")
                              : why;
        return false;
    }
    if (!drift::ort::ensureLoaded(&error))
        return false;

    // fp16 only pays on a GPU provider; on CPU ONNX Runtime runs a half graph through inserted
    // casts and it measured about 30% slower than fp32.
    const bool wantHalf = !drift::ort::providerToTry().isEmpty() && !root.fp16.encoder.isEmpty();
    const ModelFiles files = wantHalf || root.fp32.encoder.isEmpty() ? root.fp16 : root.fp32;

    Ort::Env &env = drift::ort::env();
    QString loadError;
    const bool built = drift::ort::buildSessions(
        env, "depth", false, &loadError, [&](Ort::SessionOptions &opts) {
            encoder = std::make_unique<Ort::Session>(
                env, ortPath(QDir(root.dir).filePath(files.encoder)).c_str(), opts);
            head = std::make_unique<Ort::Session>(
                env, ortPath(QDir(root.dir).filePath(files.head)).c_str(), opts);
        });
    if (!built) {
        encoder.reset();
        head.reset();
        failed = error = QStringLiteral("Failed to load the depth model: ") + loadError;
        return false;
    }

    // Check the graphs are the export we think they are. Without this a mismatched model fails
    // deep inside ONNX Runtime with a message that names no file.
    const auto check = [&](Ort::Session &s, const QString &file, const auto &ins, const auto &outs) {
        const std::vector<std::string> haveIn = sessionNames(s, true);
        const std::vector<std::string> haveOut = sessionNames(s, false);
        for (const char *name : ins) {
            if (std::find(haveIn.cbegin(), haveIn.cend(), name) == haveIn.cend())
                return QStringLiteral("%1 is not a Video Depth Anything export: input \"%2\" is "
                                      "missing.")
                    .arg(file, QLatin1String(name));
        }
        for (const char *name : outs) {
            if (std::find(haveOut.cbegin(), haveOut.cend(), name) == haveOut.cend())
                return QStringLiteral("%1 is not a Video Depth Anything export: output \"%2\" is "
                                      "missing.")
                    .arg(file, QLatin1String(name));
        }
        return QString();
    };
    QString mismatch = check(*encoder, files.encoder, kEncoderInputs, kEncoderOutputs);
    if (mismatch.isEmpty())
        mismatch = check(*head, files.head, kHeadInputs, kHeadOutputs);
    if (!mismatch.isEmpty()) {
        encoder.reset();
        head.reset();
        failed = error = mismatch;
        return false;
    }

    error.clear();
    qInfo("[depth] loaded %s (%s)", qUtf8Printable(root.variant),
          files.encoder == root.fp16.encoder ? "fp16" : "fp32");
    return true;
}

// --- pass ---------------------------------------------------------------------------------------

struct VdaDepth::Pass::State
{
    Ort::Session *encoder = nullptr;
    Ort::Session *head = nullptr;
    std::array<float, 3> mean{};
    std::array<float, 3> std{};
    int shortSide = 0;
    Emit onFrame;

    QSize size;           // inference size, fixed by the first frame
    size_t featurePlane = 0; // floats in one frame's one feature map
    // Encoder output per window slot. Slots 0..kVdaOverlap-1 of every window after the first are
    // the previous window's keyframes, carried over rather than encoded again.
    using Features = std::shared_ptr<const std::array<std::vector<float>, kFeatures>>;
    std::vector<Features> window;
    int newInWindow = 0;
    bool firstWindow = true;
    std::vector<TimeUs> pts; // every real frame pushed, by frame index
    std::unique_ptr<VdaStitcher> stitcher;
    bool stopped = false;

    std::vector<float> input;
    std::array<std::vector<float>, kFeatures> headInput;
    QString error;

    Features encode(const QImage &rgb);
    bool runWindow();
};

VdaDepth::Pass::State::Features VdaDepth::Pass::State::encode(const QImage &rgb)
{
    const int w = size.width();
    const int h = size.height();
    input.resize(size_t(3) * w * h);
    for (int c = 0; c < 3; ++c) {
        const float m = mean[size_t(c)];
        const float sd = std[size_t(c)];
        float *plane = input.data() + size_t(c) * w * h;
        for (int y = 0; y < h; ++y) {
            const uchar *line = rgb.constScanLine(y);
            for (int x = 0; x < w; ++x)
                plane[size_t(y) * w + x] = (float(line[x * 3 + c]) / 255.0f - m) / sd;
        }
    }

    const int64_t shape[4] = {1, 3, h, w};
    Ort::Value tensor = Ort::Value::CreateTensor<float>(drift::ort::cpuMemory(), input.data(),
                                                        input.size(), shape, 4);
    std::vector<Ort::Value> out = encoder->Run(Ort::RunOptions{nullptr}, kEncoderInputs, &tensor,
                                               1, kEncoderOutputs, kFeatures);
    if (featurePlane == 0)
        featurePlane = size_t(out[0].GetTensorTypeAndShapeInfo().GetElementCount());
    auto features = std::make_shared<std::array<std::vector<float>, kFeatures>>();
    for (int i = 0; i < kFeatures; ++i) {
        const float *data = out[size_t(i)].GetTensorData<float>();
        (*features)[size_t(i)].assign(data, data + featurePlane);
    }
    return features;
}

bool VdaDepth::Pass::State::runWindow()
{
    for (int i = 0; i < kFeatures; ++i) {
        std::vector<float> &dst = headInput[size_t(i)];
        dst.resize(featurePlane * kVdaWindow);
        for (int t = 0; t < kVdaWindow; ++t)
            std::memcpy(dst.data() + featurePlane * t, (*window[size_t(t)])[size_t(i)].data(),
                        featurePlane * sizeof(float));
    }

    const int64_t patchH = size.height() / kPatch;
    const int64_t patchW = size.width() / kPatch;
    const int64_t shape[4] = {kVdaWindow, int64_t(featurePlane / size_t(patchH * patchW)), patchH,
                              patchW};
    std::vector<Ort::Value> inputs;
    for (int i = 0; i < kFeatures; ++i) {
        inputs.push_back(Ort::Value::CreateTensor<float>(drift::ort::cpuMemory(),
                                                         headInput[size_t(i)].data(),
                                                         headInput[size_t(i)].size(), shape, 4));
    }
    std::vector<Ort::Value> out = head->Run(Ort::RunOptions{nullptr}, kHeadInputs, inputs.data(),
                                            inputs.size(), kHeadOutputs, 1);

    const auto forward = [this](int frameIndex, const float *disparity) {
        // Frames past the end are the padding that filled the last window.
        if (stopped || frameIndex >= int(pts.size()))
            return;
        if (!onFrame(pts[size_t(frameIndex)], disparity))
            stopped = true;
    };
    stitcher->addWindow(out[0].GetTensorData<float>(), forward);

    std::vector<Features> next;
    next.reserve(kVdaWindow);
    for (const int k : kVdaKeyframes)
        next.push_back(window[size_t(k)]);
    window = std::move(next);
    newInWindow = 0;
    firstWindow = false;
    return !stopped;
}

VdaDepth::Pass::Pass(std::unique_ptr<State> state)
    : s(std::move(state))
{
}

VdaDepth::Pass::~Pass() = default;

QSize VdaDepth::Pass::size() const
{
    return s->size;
}

QString VdaDepth::Pass::error() const
{
    return s->error;
}

bool VdaDepth::Pass::push(const QImage &frame, TimeUs ptsUs)
{
    if (s->stopped)
        return false;
    if (frame.isNull()) {
        s->error = QStringLiteral("Empty frame");
        return false;
    }

    if (s->size.isEmpty()) {
        s->size = VdaDepth::inferenceSize(frame.size(), s->shortSide);
        s->stitcher = std::make_unique<VdaStitcher>(size_t(s->size.width()) * s->size.height());
    }
    const QImage rgb = frame.scaled(s->size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                           .convertToFormat(QImage::Format_RGB888);

    try {
        s->window.push_back(s->encode(rgb));
        s->pts.push_back(ptsUs);
        ++s->newInWindow;
        if (int(s->window.size()) == kVdaWindow)
            return s->runWindow();
    } catch (const Ort::Exception &e) {
        s->error = QStringLiteral("Depth estimation failed: ") + QString::fromUtf8(e.what());
        return false;
    }
    return true;
}

bool VdaDepth::Pass::flush()
{
    if (s->stopped)
        return false;
    try {
        // Upstream pads the last window by repeating the final frame. Its features are already to
        // hand, so the padding costs no encoder runs.
        if (s->newInWindow > 0) {
            while (int(s->window.size()) < kVdaWindow)
                s->window.push_back(s->window.back());
            if (!s->runWindow())
                return false;
        }
    } catch (const Ort::Exception &e) {
        s->error = QStringLiteral("Depth estimation failed: ") + QString::fromUtf8(e.what());
        return false;
    }
    if (s->stitcher) {
        s->stitcher->finish([this](int frameIndex, const float *disparity) {
            if (!s->stopped && frameIndex < int(s->pts.size())
                && !s->onFrame(s->pts[size_t(frameIndex)], disparity)) {
                s->stopped = true;
            }
        });
    }
    return !s->stopped;
}

// --- VdaDepth -----------------------------------------------------------------------------------

VdaDepth::VdaDepth()
    : d(std::make_unique<Impl>())
{
}

VdaDepth::~VdaDepth() = default;

VdaDepth &VdaDepth::instance()
{
    // Deliberately leaked, as RvmMatter: a CUDA session freed during static destruction aborts.
    static VdaDepth *s = new VdaDepth;
    return *s;
}

bool VdaDepth::modelPresent()
{
    ModelRoot root;
    return discoverRoot(&root, nullptr);
}

bool VdaDepth::available()
{
    return d->ensureLoaded();
}

QString VdaDepth::lastError() const
{
    return d->error;
}

QString VdaDepth::variant() const
{
    return d->encoder ? d->root.variant : QString();
}

QSize VdaDepth::inferenceSize(const QSize &frame, int shortSide)
{
    const double scale = double(shortSide) / double(std::max(1, std::min(frame.width(), frame.height())));
    const auto side = [scale](int v) {
        const int patches = int(std::lround(v * scale / kPatch));
        return std::clamp(patches, kMinPatches, kMaxPatches) * kPatch;
    };
    return QSize(side(frame.width()), side(frame.height()));
}

std::unique_ptr<VdaDepth::Pass> VdaDepth::newPass(int shortSide, Pass::Emit onFrame)
{
    if (!d->ensureLoaded())
        return nullptr;
    auto state = std::make_unique<Pass::State>();
    state->encoder = d->encoder.get();
    state->head = d->head.get();
    state->mean = d->root.mean;
    state->std = d->root.std;
    state->shortSide = shortSide;
    state->onFrame = std::move(onFrame);
    return std::unique_ptr<Pass>(new Pass(std::move(state)));
}

} // namespace drift
