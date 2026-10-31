#include "engine/ml/vision/segmentation/Sam2Segmenter.h"

#include "engine/addons/PackageSearchPaths.h"
#include "engine/ml/runtime/OrtSupport.h"

#include <QByteArray>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>

#include <onnxruntime_cxx_api.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <deque>

namespace drift {
namespace {

// Fixed by the export; constants.json restates them and is checked on load.
constexpr int kImageSize = 1024;
constexpr int kFeatSize = 64; // 64x64 feature grid
constexpr int kHiddenDim = 256;
constexpr int kMemDim = 64;
constexpr int kNumMaskMem = 7; // conditioning frame + 6 recent
constexpr int kMaxObjectPointers = 16;
constexpr int kImageTokens = kFeatSize * kFeatSize;                        // 4096
constexpr int kPointerTokens = kMaxObjectPointers * (kHiddenDim / kMemDim); // 16 * 4 = 64
constexpr int kMemoryTokens = kNumMaskMem * kImageTokens + kPointerTokens;  // 28736

const char *const kFiles[] = {"vision_encoder.onnx", "mask_decoder.onnx", "memory_encoder.onnx",
                              "memory_attention.onnx", "pointer_tpos.onnx"};

using drift::ort::cstrs;
using drift::ort::elementCount;
using drift::ort::ortPath;
using drift::ort::sessionNames;

QDir graphDir(const QString &root)
{
    // The upstream repo nests the graphs under onnx/; accept a flat layout too.
    const QDir nested(QDir(root).filePath(QStringLiteral("onnx")));
    if (QFile::exists(nested.filePath(QLatin1String(kFiles[0]))))
        return nested;
    return QDir(root);
}

// The export is a set of named graphs plus constants.json, so a directory only counts as a model
// when every piece is present — a half-downloaded folder must not look installed.
QString resolveSam2ModelDir()
{
    const QStringList roots = PackageSearchPaths::defaultSearchPaths(
        QStringLiteral("DRIFT_SAM2_MODEL_DIR"), QStringLiteral("models/sam2"),
        QStringLiteral("sam2-model"));

    for (const QString &root : roots) {
        if (!QFile::exists(QDir(root).filePath(QStringLiteral("constants.json"))))
            continue;
        const QDir dir = graphDir(root);
        bool complete = true;
        for (const char *f : kFiles)
            complete = complete && QFile::exists(dir.filePath(QLatin1String(f)));
        if (complete)
            return root;
    }
    return {};
}

QString variantFromConstants(const QString &root)
{
    QFile f(QDir(root).filePath(QStringLiteral("constants.json")));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
    return obj.value(QStringLiteral("model")).toString().section(QLatin1Char('-'), -1);
}

} // namespace

struct Sam2Segmenter::Impl
{
    bool loaded = false;
    bool loadAttempted = false;
    QString error;
    QString modelDir;
    QString variant;

    std::unique_ptr<Ort::Session> vision, decoder, memEncoder, memAttention, pointerTpos;

    std::vector<std::string> visionIn, visionOut;
    std::vector<std::string> decIn, decOut;
    std::vector<std::string> memEncIn, memEncOut;
    std::vector<std::string> memAttnIn, memAttnOut;
    std::vector<std::string> ptrIn, ptrOut;

    std::vector<float> temporalPe; // kNumMaskMem x kMemDim
    float imageMean[3] = {0.485f, 0.456f, 0.406f};
    float imageStd[3] = {0.229f, 0.224f, 0.225f};

    bool ensureLoaded();
    bool loadConstants(const QString &dir);

    // Runs the mask decoder and turns its logits into a Sam2Result. The out-params feed the
    // caller's memory bookkeeping and are optional for one-shot use.
    Sam2Result runDecoder(const Sam2Embedding &embedding, const std::vector<float> &condFeats,
                          const std::vector<float> &points, const std::vector<int32_t> &labels,
                          std::vector<float> *highResMaskOut, std::vector<float> *pointerOut,
                          float *scoreOut);
};

bool Sam2Segmenter::Impl::loadConstants(const QString &dir)
{
    QFile f(QDir(dir).filePath(QStringLiteral("constants.json")));
    if (!f.open(QIODevice::ReadOnly)) {
        error = QStringLiteral("SAM2 constants.json missing");
        return false;
    }
    const QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();

    // These drive fixed tensor shapes below. Without this check a mismatched export fails deep
    // inside ORT with a shape error that names no file.
    if (obj.value(QStringLiteral("image_size")).toInt(kImageSize) != kImageSize
        || obj.value(QStringLiteral("feat_size")).toInt(kFeatSize) != kFeatSize
        || obj.value(QStringLiteral("hidden_dim")).toInt(kHiddenDim) != kHiddenDim
        || obj.value(QStringLiteral("mem_dim")).toInt(kMemDim) != kMemDim
        || obj.value(QStringLiteral("num_maskmem")).toInt(kNumMaskMem) != kNumMaskMem
        || obj.value(QStringLiteral("max_object_pointers")).toInt(kMaxObjectPointers)
               != kMaxObjectPointers) {
        error = QStringLiteral("SAM2 model has unexpected shape constants");
        return false;
    }

    const QJsonArray pe = obj.value(QStringLiteral("memory_temporal_positional_encoding")).toArray();
    if (pe.size() != kNumMaskMem) {
        error = QStringLiteral("SAM2 temporal positional encoding has %1 rows, expected %2")
                    .arg(pe.size())
                    .arg(kNumMaskMem);
        return false;
    }
    temporalPe.assign(size_t(kNumMaskMem) * kMemDim, 0.0f);
    for (int r = 0; r < kNumMaskMem; ++r) {
        const QJsonArray row = pe.at(r).toArray();
        if (row.size() != kMemDim) {
            error = QStringLiteral("SAM2 temporal positional encoding row %1 is malformed").arg(r);
            return false;
        }
        for (int c = 0; c < kMemDim; ++c)
            temporalPe[size_t(r) * kMemDim + c] = float(row.at(c).toDouble());
    }

    const QJsonArray mean = obj.value(QStringLiteral("image_mean")).toArray();
    const QJsonArray sd = obj.value(QStringLiteral("image_std")).toArray();
    for (int i = 0; i < 3 && i < mean.size(); ++i)
        imageMean[i] = float(mean.at(i).toDouble(imageMean[i]));
    for (int i = 0; i < 3 && i < sd.size(); ++i)
        imageStd[i] = float(sd.at(i).toDouble(imageStd[i]));

    variant = obj.value(QStringLiteral("model")).toString().section(QLatin1Char('-'), -1);
    return true;
}

bool Sam2Segmenter::Impl::ensureLoaded()
{
    if (loadAttempted)
        return loaded;

    modelDir = resolveSam2ModelDir();
    if (modelDir.isEmpty()) {
        // Deliberately not latched: the model arrives as an addon the user can install while the
        // app is running, and latching here would make it need a restart. A model that is present
        // but fails to load is latched below, since retrying that just repeats the failure.
        error = QStringLiteral("SAM2 model not found. Place the sam2.1 video export in models/sam2 "
                               "or set DRIFT_SAM2_MODEL_DIR.");
        return false;
    }
    // The runtime is an addon too. Unlike the model it cannot be picked up mid-session — the
    // library is loaded once per process — which is why the Addon Manager asks for a restart.
    if (!drift::ort::ensureLoaded(&error))
        return false;
    loadAttempted = true;

    if (!loadConstants(modelDir))
        return false;

    // Shared arena: memory_attention needs one contiguous 449 MiB score matrix (28736 memory
    // tokens x 4096 image tokens, fp32). Left alone, each of the five sessions builds its own
    // BFC arena and between them they reserve ~3.4 GB of a 4 GB card, so that allocation fails
    // even though the card looks nearly empty.
    Ort::Env &ortEnv = drift::ort::env();
    const QDir dir = graphDir(modelDir);
    QString loadError;
    const bool built = drift::ort::buildSessions(ortEnv, "sam2", true, &loadError,
                                                 [&](Ort::SessionOptions &opts) {
        auto open = [&](const char *name) {
            return std::make_unique<Ort::Session>(
                ortEnv, ortPath(dir.filePath(QLatin1String(name))).c_str(), opts);
        };
        vision = open("vision_encoder.onnx");
        decoder = open("mask_decoder.onnx");
        memEncoder = open("memory_encoder.onnx");
        memAttention = open("memory_attention.onnx");
        pointerTpos = open("pointer_tpos.onnx");
    });
    if (!built) {
        error = QStringLiteral("Failed to load SAM2 model: ") + loadError;
        vision.reset();
        decoder.reset();
        memEncoder.reset();
        memAttention.reset();
        pointerTpos.reset();
        return false;
    }

    try {
        visionIn = sessionNames(*vision, true);
        visionOut = sessionNames(*vision, false);
        decIn = sessionNames(*decoder, true);
        decOut = sessionNames(*decoder, false);
        memEncIn = sessionNames(*memEncoder, true);
        memEncOut = sessionNames(*memEncoder, false);
        memAttnIn = sessionNames(*memAttention, true);
        memAttnOut = sessionNames(*memAttention, false);
        ptrIn = sessionNames(*pointerTpos, true);
        ptrOut = sessionNames(*pointerTpos, false);
    } catch (const Ort::Exception &e) {
        error = QStringLiteral("Failed to load SAM2 model: ") + QString::fromUtf8(e.what());
        vision.reset();
        decoder.reset();
        memEncoder.reset();
        memAttention.reset();
        pointerTpos.reset();
        return false;
    }

    loaded = true;
    return true;
}

Sam2Segmenter::Sam2Segmenter()
    : d(std::make_unique<Impl>())
{
}

Sam2Segmenter::~Sam2Segmenter() = default;

Sam2Segmenter &Sam2Segmenter::instance()
{
    // Deliberately leaked. A function-local static is destroyed during exit, by which point the
    // CUDA driver has already run its own teardown, and freeing the sessions then aborts the
    // process with "CUDA failure 4: driver shutting down". The OS reclaims this at exit anyway.
    static Sam2Segmenter *s = new Sam2Segmenter;
    return *s;
}

bool Sam2Segmenter::modelPresent()
{
    return !resolveSam2ModelDir().isEmpty();
}

QString Sam2Segmenter::installedVariant()
{
    const QString root = resolveSam2ModelDir();
    return root.isEmpty() ? QString() : variantFromConstants(root);
}

bool Sam2Segmenter::available()
{
    return d->ensureLoaded();
}

QString Sam2Segmenter::lastError() const
{
    return d->error;
}

QString Sam2Segmenter::modelVariant()
{
    d->ensureLoaded();
    return d->variant;
}

Sam2Embedding Sam2Segmenter::encode(const QImage &frame)
{
    Sam2Embedding out;
    if (!d->ensureLoaded() || frame.isNull())
        return out;

    const QImage scaled =
        frame.scaled(kImageSize, kImageSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
            .convertToFormat(QImage::Format_RGB888);

    std::vector<float> input(size_t(3) * kImageSize * kImageSize);
    const size_t plane = size_t(kImageSize) * kImageSize;
    for (int y = 0; y < kImageSize; ++y) {
        const uchar *row = scaled.constScanLine(y);
        for (int x = 0; x < kImageSize; ++x) {
            const size_t idx = size_t(y) * kImageSize + x;
            for (int c = 0; c < 3; ++c)
                input[c * plane + idx] = (row[x * 3 + c] / 255.0f - d->imageMean[c]) / d->imageStd[c];
        }
    }

    try {
        const std::array<int64_t, 4> shape{1, 3, kImageSize, kImageSize};
        Ort::Value tensor = Ort::Value::CreateTensor<float>(ort::cpuMemory(), input.data(), input.size(),
                                                            shape.data(), shape.size());
        const auto inNames = cstrs(d->visionIn);
        const auto outNames = cstrs(d->visionOut);
        auto results = d->vision->Run(Ort::RunOptions{nullptr}, inNames.data(), &tensor, 1,
                                      outNames.data(), outNames.size());

        for (size_t i = 0; i < results.size(); ++i) {
            const std::string &name = d->visionOut[i];
            const int64_t n = elementCount(results[i].GetTensorTypeAndShapeInfo().GetShape());
            const float *src = results[i].GetTensorData<float>();
            std::vector<float> *dst = nullptr;
            if (name == "feats0")
                dst = &out.feats0;
            else if (name == "feats1")
                dst = &out.feats1;
            else if (name == "feats2")
                dst = &out.feats2;
            else if (name == "feats2_no_mem")
                dst = &out.feats2NoMem;
            else if (name == "vision_pos_embed")
                dst = &out.visionPosEmbed;
            if (dst)
                dst->assign(src, src + n);
        }
    } catch (const Ort::Exception &e) {
        d->error = QStringLiteral("SAM2 encode failed: ") + QString::fromUtf8(e.what());
        return out;
    }

    if (out.feats0.empty() || out.feats1.empty() || out.feats2.empty() || out.feats2NoMem.empty()
        || out.visionPosEmbed.empty()) {
        d->error = QStringLiteral("SAM2 vision encoder returned unexpected outputs");
        return out;
    }

    out.frameSize = frame.size();
    out.valid = true;
    return out;
}

Sam2Result Sam2Segmenter::Impl::runDecoder(const Sam2Embedding &embedding,
                                           const std::vector<float> &condFeats,
                                           const std::vector<float> &points,
                                           const std::vector<int32_t> &labels,
                                           std::vector<float> *highResMaskOut,
                                           std::vector<float> *pointerOut, float *scoreOut)
{
    Sam2Result result;
    const int pointCount = int(labels.size());

    std::vector<float> maskLogits;
    int maskW = 0;
    int maskH = 0;

    try {
        // ORT treats input tensors as read-only; const_cast avoids copying tens of MB per frame.
        auto &emb = const_cast<Sam2Embedding &>(embedding);
        auto &cond = const_cast<std::vector<float> &>(condFeats);
        auto &pts = const_cast<std::vector<float> &>(points);
        auto &lbl = const_cast<std::vector<int32_t> &>(labels);

        const std::array<int64_t, 4> f0Shape{1, 32, 256, 256};
        const std::array<int64_t, 4> f1Shape{1, 64, 128, 128};
        const std::array<int64_t, 4> f2Shape{1, kHiddenDim, kFeatSize, kFeatSize};
        const std::array<int64_t, 4> ptShape{1, 1, pointCount, 2};
        const std::array<int64_t, 3> lbShape{1, 1, pointCount};

        std::vector<Ort::Value> inputs;
        inputs.push_back(Ort::Value::CreateTensor<float>(ort::cpuMemory(), emb.feats0.data(), emb.feats0.size(),
                                                         f0Shape.data(), f0Shape.size()));
        inputs.push_back(Ort::Value::CreateTensor<float>(ort::cpuMemory(), emb.feats1.data(), emb.feats1.size(),
                                                         f1Shape.data(), f1Shape.size()));
        inputs.push_back(Ort::Value::CreateTensor<float>(ort::cpuMemory(), cond.data(), cond.size(),
                                                         f2Shape.data(), f2Shape.size()));
        inputs.push_back(Ort::Value::CreateTensor<float>(ort::cpuMemory(), pts.data(), pts.size(), ptShape.data(),
                                                         ptShape.size()));
        inputs.push_back(Ort::Value::CreateTensor<int32_t>(ort::cpuMemory(), lbl.data(), lbl.size(),
                                                           lbShape.data(), lbShape.size()));

        const auto inNames = cstrs(decIn);
        const auto outNames = cstrs(decOut);
        auto results = decoder->Run(Ort::RunOptions{nullptr}, inNames.data(), inputs.data(),
                                    inputs.size(), outNames.data(), outNames.size());

        for (size_t i = 0; i < results.size(); ++i) {
            const std::string &name = decOut[i];
            const auto shape = results[i].GetTensorTypeAndShapeInfo().GetShape();
            const float *src = results[i].GetTensorData<float>();
            const int64_t n = elementCount(shape);
            if (name == "high_res_mask") {
                maskH = int(shape[shape.size() - 2]);
                maskW = int(shape[shape.size() - 1]);
                maskLogits.assign(src, src + n);
                if (highResMaskOut)
                    highResMaskOut->assign(src, src + n);
            } else if (name == "iou") {
                result.iou = src[0];
            } else if (name == "object_score_logits") {
                if (scoreOut)
                    *scoreOut = src[0];
                result.occluded = src[0] <= 0.0f;
            } else if (name == "object_pointer") {
                if (pointerOut)
                    pointerOut->assign(src, src + n);
            }
        }
    } catch (const Ort::Exception &e) {
        result.error = QStringLiteral("SAM2 decode failed: ") + QString::fromUtf8(e.what());
        return result;
    }

    if (maskLogits.empty() || maskW <= 0 || maskH <= 0) {
        result.error = QStringLiteral("SAM2 decoder returned no mask");
        return result;
    }

    // Resize logits, then threshold. Thresholding at 1024 and scaling the binary result afterwards
    // aliases the mask edge.
    QImage logitImage(maskW, maskH, QImage::Format_Grayscale8);
    for (int y = 0; y < maskH; ++y) {
        uchar *row = logitImage.scanLine(y);
        for (int x = 0; x < maskW; ++x) {
            const float v = maskLogits[size_t(y) * maskW + x];
            const float t = 1.0f / (1.0f + std::exp(-v)); // sigmoid: 0.5 == logit 0
            row[x] = uchar(std::clamp(int(t * 255.0f + 0.5f), 0, 255));
        }
    }

    const QImage resized =
        logitImage.scaled(embedding.frameSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    QImage mask(embedding.frameSize, QImage::Format_Grayscale8);
    qint64 covered = 0;
    // The graph already suppresses an occluded object's mask; this keeps the QImage consistent
    // with the flag rather than trusting near-threshold noise.
    const bool suppress = result.occluded;
    for (int y = 0; y < mask.height(); ++y) {
        const uchar *src = resized.constScanLine(y);
        uchar *dst = mask.scanLine(y);
        for (int x = 0; x < mask.width(); ++x) {
            const bool on = !suppress && src[x] >= 128;
            dst[x] = on ? 255 : 0;
            covered += on ? 1 : 0;
        }
    }

    result.mask = mask;
    result.maskFraction = double(covered) / double(qMax(1, mask.width() * mask.height()));
    result.ok = true;
    return result;
}

Sam2Result Sam2Segmenter::segmentSeed(const Sam2Embedding &embedding, const Sam2Prompt &prompt)
{
    Sam2Result result;
    if (!d->ensureLoaded()) {
        result.error = d->error;
        return result;
    }
    if (!embedding.valid || embedding.frameSize.isEmpty()) {
        result.error = QStringLiteral("SAM2 called without a valid embedding");
        return result;
    }
    if (prompt.points.size() != prompt.labels.size() || prompt.isEmpty()) {
        result.error = QStringLiteral("SAM2 needs at least one prompt point");
        return result;
    }

    const double sx = double(kImageSize) / embedding.frameSize.width();
    const double sy = double(kImageSize) / embedding.frameSize.height();
    std::vector<float> points;
    std::vector<int32_t> labels;
    for (int i = 0; i < prompt.points.size(); ++i) {
        points.push_back(float(prompt.points.at(i).x() * sx));
        points.push_back(float(prompt.points.at(i).y() * sy));
        labels.push_back(prompt.labels.at(i));
    }

    // Decoded against feats2_no_mem: no memory has been built for this frame.
    return d->runDecoder(embedding, embedding.feats2NoMem, points, labels, nullptr, nullptr, nullptr);
}

// --- Track -----------------------------------------------------------------------------------

struct Sam2Segmenter::Track::State
{
    Impl *d = nullptr;
    bool seeded = false;
    int frameIndex = 0;

    // Conditioning (seed) memory, kept for the whole pass; recent memories, newest last.
    std::vector<float> condTokens, condPos;
    std::deque<std::vector<float>> recentTokens, recentPos;
    // Object pointers and the frame each came from, newest last.
    std::deque<std::vector<float>> pointers;
    std::deque<int> pointerFrames;

    bool encodeMemory(const Sam2Embedding &embedding, const std::vector<float> &highResMask,
                      float objectScore, bool binarize, std::vector<float> *tokensOut,
                      std::vector<float> *posOut, QString *errorOut);
    bool buildMemory(std::vector<float> *memory, std::vector<float> *memoryPos, QString *errorOut);
    bool attend(const Sam2Embedding &embedding, const std::vector<float> &memory,
                const std::vector<float> &memoryPos, std::vector<float> *condOut, QString *errorOut);
    void pushMemory(std::vector<float> tokens, std::vector<float> pos, std::vector<float> pointer);
};

bool Sam2Segmenter::Track::State::encodeMemory(const Sam2Embedding &embedding,
                                               const std::vector<float> &highResMask,
                                               float objectScore, bool binarize,
                                               std::vector<float> *tokensOut,
                                               std::vector<float> *posOut, QString *errorOut)
{
    try {
        auto &emb = const_cast<Sam2Embedding &>(embedding);
        auto &mask = const_cast<std::vector<float> &>(highResMask);
        std::vector<float> score{objectScore};
        float binarizeValue = binarize ? 1.0f : 0.0f;

        const std::array<int64_t, 4> f2Shape{1, kHiddenDim, kFeatSize, kFeatSize};
        const std::array<int64_t, 4> maskShape{1, 1, kImageSize, kImageSize};
        const std::array<int64_t, 2> scoreShape{1, 1};

        std::vector<Ort::Value> inputs;
        inputs.push_back(Ort::Value::CreateTensor<float>(ort::cpuMemory(), emb.feats2.data(),
                                                         emb.feats2.size(), f2Shape.data(),
                                                         f2Shape.size()));
        inputs.push_back(Ort::Value::CreateTensor<float>(ort::cpuMemory(), mask.data(), mask.size(),
                                                         maskShape.data(), maskShape.size()));
        inputs.push_back(Ort::Value::CreateTensor<float>(ort::cpuMemory(), score.data(), score.size(),
                                                         scoreShape.data(), scoreShape.size()));
        // `binarize` is a rank-0 scalar.
        inputs.push_back(Ort::Value::CreateTensor<float>(ort::cpuMemory(), &binarizeValue, 1, nullptr, 0));

        const auto inNames = cstrs(d->memEncIn);
        const auto outNames = cstrs(d->memEncOut);
        auto results = d->memEncoder->Run(Ort::RunOptions{nullptr}, inNames.data(), inputs.data(),
                                          inputs.size(), outNames.data(), outNames.size());

        for (size_t i = 0; i < results.size(); ++i) {
            const int64_t n = elementCount(results[i].GetTensorTypeAndShapeInfo().GetShape());
            const float *src = results[i].GetTensorData<float>();
            if (d->memEncOut[i] == "memory_tokens")
                tokensOut->assign(src, src + n);
            else if (d->memEncOut[i] == "memory_pos")
                posOut->assign(src, src + n);
        }
    } catch (const Ort::Exception &e) {
        if (errorOut)
            *errorOut = QStringLiteral("SAM2 memory encode failed: ") + QString::fromUtf8(e.what());
        return false;
    }
    return tokensOut->size() == size_t(kImageTokens) * kMemDim;
}

// Lays out the fixed-size memory the attention graph expects:
//   [0 .. 7*4096)     seven mask memories, 4096 tokens of kMemDim each
//   [7*4096 .. +64)   sixteen object pointers, each 256 dims split into four kMemDim tokens
// Slot 0 is the conditioning frame and uses the last temporal-PE row; slots 1..6 are the most
// recent frames, newest first, using row (slot-1). Short histories pad by duplicating the newest
// entry, which is what the reference implementation does for early frames.
bool Sam2Segmenter::Track::State::buildMemory(std::vector<float> *memory,
                                              std::vector<float> *memoryPos, QString *errorOut)
{
    if (condTokens.empty()) {
        if (errorOut)
            *errorOut = QStringLiteral("SAM2 track has no conditioning memory");
        return false;
    }

    memory->assign(size_t(kMemoryTokens) * kMemDim, 0.0f);
    memoryPos->assign(size_t(kMemoryTokens) * kMemDim, 0.0f);

    auto writeBlock = [&](int slot, const std::vector<float> &tokens, const std::vector<float> &pos,
                          int peRow) {
        const size_t base = size_t(slot) * kImageTokens * kMemDim;
        std::copy(tokens.begin(), tokens.end(), memory->begin() + base);
        for (int t = 0; t < kImageTokens; ++t) {
            for (int c = 0; c < kMemDim; ++c) {
                (*memoryPos)[base + size_t(t) * kMemDim + c] =
                    pos[size_t(t) * kMemDim + c] + d->temporalPe[size_t(peRow) * kMemDim + c];
            }
        }
    };

    writeBlock(0, condTokens, condPos, kNumMaskMem - 1);

    for (int slot = 1; slot < kNumMaskMem; ++slot) {
        const int back = std::min<int>(slot - 1, int(recentTokens.size()) - 1);
        if (back < 0) {
            // Nothing propagated yet: repeat the conditioning frame.
            writeBlock(slot, condTokens, condPos, kNumMaskMem - 1);
            continue;
        }
        const int idx = int(recentTokens.size()) - 1 - back;
        writeBlock(slot, recentTokens[idx], recentPos[idx], slot - 1);
    }

    const size_t pointerBase = size_t(kNumMaskMem) * kImageTokens * kMemDim;
    const int available = int(pointers.size());
    if (available <= 0)
        return true;

    std::vector<float> diffs;
    diffs.reserve(kMaxObjectPointers);
    const float denom = float(std::max(1, std::min(available, kMaxObjectPointers) - 1));
    for (int i = 0; i < kMaxObjectPointers; ++i) {
        const int back = std::min(i, available - 1); // pad by duplicating the newest
        const int idx = available - 1 - back;
        diffs.push_back(float(frameIndex - pointerFrames[idx]) / denom);
    }

    std::vector<float> pointerPos;
    try {
        const std::array<int64_t, 1> diffShape{int64_t(diffs.size())};
        Ort::Value in = Ort::Value::CreateTensor<float>(ort::cpuMemory(), diffs.data(), diffs.size(),
                                                        diffShape.data(), diffShape.size());
        const auto inNames = cstrs(d->ptrIn);
        const auto outNames = cstrs(d->ptrOut);
        auto results = d->pointerTpos->Run(Ort::RunOptions{nullptr}, inNames.data(), &in, 1,
                                           outNames.data(), outNames.size());
        const int64_t n = elementCount(results[0].GetTensorTypeAndShapeInfo().GetShape());
        const float *src = results[0].GetTensorData<float>();
        pointerPos.assign(src, src + n);
    } catch (const Ort::Exception &e) {
        if (errorOut)
            *errorOut = QStringLiteral("SAM2 pointer encoding failed: ") + QString::fromUtf8(e.what());
        return false;
    }

    constexpr int kPartsPerPointer = kHiddenDim / kMemDim;
    for (int i = 0; i < kMaxObjectPointers; ++i) {
        const int back = std::min(i, available - 1);
        const int idx = available - 1 - back;
        const std::vector<float> &ptr = pointers[idx];
        if (ptr.size() < size_t(kHiddenDim))
            continue;
        for (int part = 0; part < kPartsPerPointer; ++part) {
            const size_t token = pointerBase + (size_t(i) * kPartsPerPointer + part) * kMemDim;
            for (int c = 0; c < kMemDim; ++c) {
                (*memory)[token + c] = ptr[size_t(part) * kMemDim + c];
                (*memoryPos)[token + c] = pointerPos[size_t(i) * kMemDim + c];
            }
        }
    }
    return true;
}

bool Sam2Segmenter::Track::State::attend(const Sam2Embedding &embedding,
                                         const std::vector<float> &memory,
                                         const std::vector<float> &memoryPos,
                                         std::vector<float> *condOut, QString *errorOut)
{
    // The encoder emits NCHW; attention wants tokens-first [4096, 1, 256].
    auto toTokens = [](const std::vector<float> &nchw) {
        std::vector<float> out(size_t(kImageTokens) * kHiddenDim);
        for (int c = 0; c < kHiddenDim; ++c) {
            for (int t = 0; t < kImageTokens; ++t)
                out[size_t(t) * kHiddenDim + c] = nchw[size_t(c) * kImageTokens + t];
        }
        return out;
    };

    std::vector<float> feats = toTokens(embedding.feats2);
    std::vector<float> pos = toTokens(embedding.visionPosEmbed);
    auto &mem = const_cast<std::vector<float> &>(memory);
    auto &memPos = const_cast<std::vector<float> &>(memoryPos);

    try {
        const std::array<int64_t, 3> featShape{kImageTokens, 1, kHiddenDim};
        const std::array<int64_t, 3> memShape{kMemoryTokens, 1, kMemDim};

        std::vector<Ort::Value> inputs;
        inputs.push_back(Ort::Value::CreateTensor<float>(ort::cpuMemory(), feats.data(), feats.size(),
                                                         featShape.data(), featShape.size()));
        inputs.push_back(Ort::Value::CreateTensor<float>(ort::cpuMemory(), pos.data(), pos.size(),
                                                         featShape.data(), featShape.size()));
        inputs.push_back(Ort::Value::CreateTensor<float>(ort::cpuMemory(), mem.data(), mem.size(),
                                                         memShape.data(), memShape.size()));
        inputs.push_back(Ort::Value::CreateTensor<float>(ort::cpuMemory(), memPos.data(), memPos.size(),
                                                         memShape.data(), memShape.size()));

        const auto inNames = cstrs(d->memAttnIn);
        const auto outNames = cstrs(d->memAttnOut);
        auto results = d->memAttention->Run(Ort::RunOptions{nullptr}, inNames.data(), inputs.data(),
                                            inputs.size(), outNames.data(), outNames.size());
        const int64_t n = elementCount(results[0].GetTensorTypeAndShapeInfo().GetShape());
        const float *src = results[0].GetTensorData<float>();
        condOut->assign(src, src + n);
    } catch (const Ort::Exception &e) {
        if (errorOut)
            *errorOut = QStringLiteral("SAM2 memory attention failed: ") + QString::fromUtf8(e.what());
        return false;
    }
    return true;
}

void Sam2Segmenter::Track::State::pushMemory(std::vector<float> tokens, std::vector<float> pos,
                                             std::vector<float> pointer)
{
    recentTokens.push_back(std::move(tokens));
    recentPos.push_back(std::move(pos));
    while (int(recentTokens.size()) > kNumMaskMem - 1) {
        recentTokens.pop_front();
        recentPos.pop_front();
    }

    pointers.push_back(std::move(pointer));
    pointerFrames.push_back(frameIndex);
    while (int(pointers.size()) > kMaxObjectPointers) {
        pointers.pop_front();
        pointerFrames.pop_front();
    }
}

Sam2Segmenter::Track::Track(Impl *impl)
    : s(std::make_unique<State>())
{
    s->d = impl;
}

Sam2Segmenter::Track::~Track() = default;

bool Sam2Segmenter::Track::isSeeded() const
{
    return s->seeded;
}

Sam2Result Sam2Segmenter::Track::seed(const Sam2Embedding &embedding, const Sam2Prompt &prompt)
{
    Sam2Result result;
    if (!embedding.valid || prompt.isEmpty() || prompt.points.size() != prompt.labels.size()) {
        result.error = QStringLiteral("SAM2 track seed needs a valid frame and prompt");
        return result;
    }

    const double sx = double(kImageSize) / embedding.frameSize.width();
    const double sy = double(kImageSize) / embedding.frameSize.height();
    std::vector<float> points;
    std::vector<int32_t> labels;
    for (int i = 0; i < prompt.points.size(); ++i) {
        points.push_back(float(prompt.points.at(i).x() * sx));
        points.push_back(float(prompt.points.at(i).y() * sy));
        labels.push_back(prompt.labels.at(i));
    }

    std::vector<float> highResMask, pointer;
    float score = 0.0f;
    result = s->d->runDecoder(embedding, embedding.feats2NoMem, points, labels, &highResMask,
                              &pointer, &score);
    if (!result.ok)
        return result;

    QString error;
    // binarize=1: a point-prompted mask is thresholded before it becomes memory.
    if (!s->encodeMemory(embedding, highResMask, score, true, &s->condTokens, &s->condPos, &error)) {
        result.ok = false;
        result.error = error;
        return result;
    }

    s->pointers.clear();
    s->pointerFrames.clear();
    s->recentTokens.clear();
    s->recentPos.clear();
    s->pointers.push_back(std::move(pointer));
    s->pointerFrames.push_back(0);
    s->frameIndex = 0;
    s->seeded = true;
    return result;
}

Sam2Result Sam2Segmenter::Track::step(const Sam2Embedding &embedding)
{
    Sam2Result result;
    if (!s->seeded) {
        result.error = QStringLiteral("SAM2 track stepped before it was seeded");
        return result;
    }
    if (!embedding.valid) {
        result.error = QStringLiteral("SAM2 track stepped with an invalid frame");
        return result;
    }

    ++s->frameIndex;

    QString error;
    std::vector<float> memory, memoryPos;
    if (!s->buildMemory(&memory, &memoryPos, &error)) {
        result.error = error;
        return result;
    }

    std::vector<float> conditioned;
    if (!s->attend(embedding, memory, memoryPos, &conditioned, &error)) {
        result.error = error;
        return result;
    }

    // A single padding point with label -1. On propagated frames the object's identity comes from
    // memory, not prompts — which is why the user's clicks only ever apply to the seed frame.
    const std::vector<float> points{0.0f, 0.0f};
    const std::vector<int32_t> labels{-1};

    std::vector<float> highResMask, pointer;
    float score = 0.0f;
    result = s->d->runDecoder(embedding, conditioned, points, labels, &highResMask, &pointer, &score);
    if (!result.ok)
        return result;

    std::vector<float> tokens, pos;
    // binarize=0: propagated masks enter memory as soft logits.
    if (!s->encodeMemory(embedding, highResMask, score, false, &tokens, &pos, &error)) {
        result.ok = false;
        result.error = error;
        return result;
    }
    s->pushMemory(std::move(tokens), std::move(pos), std::move(pointer));
    return result;
}

std::unique_ptr<Sam2Segmenter::Track> Sam2Segmenter::newTrack()
{
    if (!d->ensureLoaded())
        return nullptr;
    return std::unique_ptr<Track>(new Track(d.get()));
}

} // namespace drift
