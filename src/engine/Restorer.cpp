#include "engine/Restorer.h"

#include "engine/GpuPackageParse.h"
#include "engine/OrtSupport.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>

#include <onnxruntime_cxx_api.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace drift {

namespace {

using drift::ort::ortPath;
using drift::ort::sessionNames;

// Tile edge in source pixels for graphs with dynamic height and width, and the context added on
// every side of it. Without the overlap each tile's border is restored from half a neighbourhood
// and the seams show.
constexpr int kTile = 512;
constexpr int kPad = 16;
// Transformer-style upscalers (DAT, SwinIR, HAT) only accept sizes divisible by their attention
// window; padding every tile to a multiple of 16 covers the common ones and costs SPAN nothing.
constexpr int kAlign = 16;

QMutex g_mutex;
QHash<QString, QPair<qint64, int>> g_probedScale;      // path -> (mtime, scale; 0 = unusable)
QHash<QString, std::weak_ptr<Restorer>> g_loaded;      // path -> live session

int scaleFromName(const QString &baseName)
{
    static const QRegularExpression prefix(QStringLiteral("^([1-8])x"),
                                           QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression token(
        QStringLiteral("(?:^|[_\\-. ])(?:([1-8])x|x([1-8]))(?:[_\\-. ]|$)"),
        QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch m = prefix.match(baseName);
    if (m.hasMatch())
        return m.captured(1).toInt();
    m = token.match(baseName);
    if (m.hasMatch())
        return (m.captured(1).isEmpty() ? m.captured(2) : m.captured(1)).toInt();
    return 0;
}

// Half-precision exports are common on OpenModelDB. Their tensors are converted at the boundary; the
// CPU provider runs the graph itself, casting around any op it has no fp16 kernel for.
bool isSupportedType(ONNXTensorElementDataType type)
{
    return type == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT
           || type == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16;
}

// Wraps `data` as the graph's input, through `half` when the graph takes fp16. Both buffers must
// outlive the returned tensor.
Ort::Value inputTensor(ONNXTensorElementDataType type, const std::vector<float> &data,
                       std::vector<Ort::Float16_t> &half, const std::array<int64_t, 4> &shape)
{
    if (type == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16) {
        half.resize(data.size());
        for (size_t i = 0; i < data.size(); ++i)
            half[i] = Ort::Float16_t(data[i]);
        return Ort::Value::CreateTensor<Ort::Float16_t>(drift::ort::cpuMemory(), half.data(),
                                                        half.size(), shape.data(), shape.size());
    }
    return Ort::Value::CreateTensor<float>(drift::ort::cpuMemory(),
                                           const_cast<float *>(data.data()), data.size(),
                                           shape.data(), shape.size());
}

// One 32x32 (or the graph's fixed size) inference on CPU, to learn the scale of a model whose file
// name does not say it.
int probeScale(const QString &path)
{
    QString error;
    if (!drift::ort::ensureLoaded(&error))
        return 0;
    try {
        Ort::SessionOptions opts;
        opts.SetIntraOpNumThreads(std::max(1, QThread::idealThreadCount()));
        Ort::Session session(drift::ort::env(), ortPath(path).c_str(), opts);
        if (session.GetInputCount() != 1 || session.GetOutputCount() < 1)
            return 0;
        // The shape info is a view into the TypeInfo, which has to outlive it.
        const Ort::TypeInfo typeInfo = session.GetInputTypeInfo(0);
        const auto info = typeInfo.GetTensorTypeAndShapeInfo();
        const std::vector<int64_t> shape = info.GetShape();
        const ONNXTensorElementDataType type = info.GetElementType();
        if (!isSupportedType(type) || shape.size() != 4)
            return 0;
        const int64_t h = shape[2] > 0 ? shape[2] : 32;
        const int64_t w = shape[3] > 0 ? shape[3] : 32;
        const std::array<int64_t, 4> in{1, 3, h, w};
        std::vector<float> data(size_t(3 * h * w), 0.5f);
        std::vector<Ort::Float16_t> half;
        Ort::Value tensor = inputTensor(type, data, half, in);
        const std::vector<std::string> ins = sessionNames(session, true);
        const std::vector<std::string> outs = sessionNames(session, false);
        const char *inName = ins.front().c_str();
        const char *outName = outs.front().c_str();
        std::vector<Ort::Value> result =
            session.Run(Ort::RunOptions{nullptr}, &inName, &tensor, 1, &outName, 1);
        const std::vector<int64_t> out = result.front().GetTensorTypeAndShapeInfo().GetShape();
        if (out.size() != 4 || out[1] != 3 || out[3] % w != 0 || out[2] != out[3] / w * h)
            return 0;
        return int(out[3] / w);
    } catch (const Ort::Exception &e) {
        qWarning("[restore] %s is not a usable restore model: %s", qUtf8Printable(path), e.what());
        return 0;
    }
}

int customScale(const QFileInfo &file)
{
    if (const int named = scaleFromName(file.completeBaseName()))
        return named;
    const QString path = file.absoluteFilePath();
    const qint64 mtime = file.lastModified().toMSecsSinceEpoch();
    {
        QMutexLocker lock(&g_mutex);
        const auto it = g_probedScale.constFind(path);
        if (it != g_probedScale.cend() && it->first == mtime)
            return it->second;
    }
    const int scale = probeScale(path);
    QMutexLocker lock(&g_mutex);
    g_probedScale.insert(path, {mtime, scale});
    return scale;
}

} // namespace

struct Restorer::Impl
{
    std::unique_ptr<Ort::Session> session;
    std::string inName;
    std::string outName;
    int scale = 1;
    int fixedW = 0; // the graph's static input size, 0 when dynamic
    int fixedH = 0;
    ONNXTensorElementDataType inputType = ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT;
    std::vector<float> input;
    std::vector<Ort::Float16_t> inputHalf;
    std::vector<float> outputFloat;
};

Restorer::Restorer()
    : d(std::make_unique<Impl>())
{
}

Restorer::~Restorer() = default;

QString Restorer::customModelsDir()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
        .filePath(QStringLiteral("models/restore"));
}

QList<Restorer::Model> Restorer::models()
{
    QList<Model> addons;
    QList<Model> custom;
    QSet<QString> seen;
    const QStringList roots = GpuPackageParse::defaultSearchPaths(
        QStringLiteral("DRIFT_RESTORE_MODEL_DIR"), QStringLiteral("models/restore"),
        QStringLiteral("restore-model"));
    for (const QString &root : roots) {
        const QDir dir(root);
        QFile constants(dir.filePath(QStringLiteral("constants.json")));
        if (constants.open(QIODevice::ReadOnly)) {
            const QJsonObject obj = QJsonDocument::fromJson(constants.readAll()).object();
            QJsonArray entries = obj.value(QStringLiteral("models")).toArray();
            if (entries.isEmpty())
                entries.append(obj);
            for (const QJsonValue &entry : entries) {
                const QJsonObject e = entry.toObject();
                Model m;
                m.id = e.value(QStringLiteral("model")).toString();
                m.name = e.value(QStringLiteral("name")).toString(m.id);
                m.task = e.value(QStringLiteral("task")).toString();
                m.scale = e.value(QStringLiteral("scale")).toInt();
                const QString file = e.value(QStringLiteral("files")).toObject()
                                         .value(QStringLiteral("fp32")).toString();
                m.path = file.isEmpty() ? QString() : dir.filePath(file);
                for (const QJsonValue &c : e.value(QStringLiteral("content")).toArray())
                    m.content.append(c.toString());
                m.summary = e.value(QStringLiteral("summary")).toString();
                const QString thumbnail = e.value(QStringLiteral("thumbnail")).toString();
                if (!thumbnail.isEmpty() && dir.exists(thumbnail))
                    m.thumbnail = dir.filePath(thumbnail);
                m.secondsPerMegapixel = e.value(QStringLiteral("secondsPerMegapixel")).toDouble();
                if (m.id.isEmpty() || m.scale < 1 || m.path.isEmpty() || !QFile::exists(m.path)
                    || seen.contains(m.id)) {
                    continue;
                }
                seen.insert(m.id);
                addons.append(m);
            }
            continue;
        }

        const QFileInfoList files =
            dir.entryInfoList({QStringLiteral("*.onnx")}, QDir::Files, QDir::Name | QDir::IgnoreCase);
        for (const QFileInfo &file : files) {
            Model m;
            m.id = QStringLiteral("custom:") + file.fileName();
            if (seen.contains(m.id))
                continue;
            m.scale = customScale(file);
            if (m.scale < 1)
                continue;
            m.name = file.completeBaseName();
            m.task = m.scale == 1 ? QStringLiteral("decompress") : QStringLiteral("upscale");
            m.custom = true;
            m.path = file.absoluteFilePath();
            seen.insert(m.id);
            custom.append(m);
        }
    }
    return addons + custom;
}

std::shared_ptr<Restorer> Restorer::load(const QString &id, QString *error)
{
    Model model;
    for (const Model &m : models()) {
        if (m.id == id) {
            model = m;
            break;
        }
    }
    if (model.path.isEmpty()) {
        *error = QStringLiteral("The restore model \"%1\" is not installed.").arg(id);
        return nullptr;
    }

    {
        QMutexLocker lock(&g_mutex);
        if (std::shared_ptr<Restorer> live = g_loaded.value(model.path).lock())
            return live;
    }

    if (!drift::ort::ensureLoaded(error))
        return nullptr;

    std::shared_ptr<Restorer> r(new Restorer);
    Ort::Env &env = drift::ort::env();
    QString loadError;
    const auto build = [&](Ort::SessionOptions &opts) {
        r->d->session = std::make_unique<Ort::Session>(env, ortPath(model.path).c_str(), opts);
    };
    // Plain CPU unless a provider was asked for by name, as for RvmMatter. "auto" prefers the
    // WebGPU plugin EP, whose NHWC layout pass breaks these graphs: their exports name the output's
    // dynamic dims after the input's, so a 2x model fails the moment it runs ("Shape mismatch
    // attempting to re-use buffer {1,528,528,3} != {1,1056,1056,3}"). The same graphs run fine on
    // CPU.
    bool built = true;
    if (drift::ort::variantExplicit()) {
        built = drift::ort::buildSessions(env, "restore", false, &loadError, build);
    } else {
        try {
            Ort::SessionOptions opts;
            opts.SetIntraOpNumThreads(std::max(1, QThread::idealThreadCount()));
            build(opts);
        } catch (const Ort::Exception &e) {
            loadError = QString::fromUtf8(e.what());
            built = false;
        }
    }
    if (!built) {
        *error = QStringLiteral("Failed to load %1: %2").arg(model.name, loadError);
        return nullptr;
    }

    Ort::Session &s = *r->d->session;
    const Ort::TypeInfo typeInfo = s.GetInputTypeInfo(0);
    const auto info = typeInfo.GetTensorTypeAndShapeInfo();
    const std::vector<int64_t> shape = info.GetShape();
    if (s.GetInputCount() != 1 || s.GetOutputCount() < 1 || shape.size() != 4
        || (shape[1] > 0 && shape[1] != 3)) {
        *error = QStringLiteral("%1 is not an image restoration model: it must take one "
                                "1x3xHxW RGB image.")
                     .arg(model.name);
        return nullptr;
    }
    if (!isSupportedType(info.GetElementType())) {
        *error = QStringLiteral("%1 takes neither float32 nor float16 input.").arg(model.name);
        return nullptr;
    }
    r->d->inputType = info.GetElementType();
    r->d->inName = sessionNames(s, true).front();
    r->d->outName = sessionNames(s, false).front();
    r->d->scale = model.scale;
    r->d->fixedH = shape[2] > 0 ? int(shape[2]) : 0;
    r->d->fixedW = shape[3] > 0 ? int(shape[3]) : 0;
    if ((r->d->fixedW && r->d->fixedW <= 2 * kPad) || (r->d->fixedH && r->d->fixedH <= 2 * kPad)) {
        *error = QStringLiteral("%1 takes a fixed input too small to tile.").arg(model.name);
        return nullptr;
    }

    qInfo("[restore] loaded %s (%dx)", qUtf8Printable(model.name), model.scale);
    QMutexLocker lock(&g_mutex);
    g_loaded.insert(model.path, r);
    return r;
}

int Restorer::scale() const
{
    return d->scale;
}

bool Restorer::process(QImage &frame, const std::function<bool(double)> &progress, QString *error)
{
    const QImage src = frame.convertToFormat(QImage::Format_RGB888);
    const int W = src.width();
    const int H = src.height();
    const int s = d->scale;
    QImage dst(W * s, H * s, QImage::Format_RGB888);
    if (dst.isNull()) {
        *error = QStringLiteral("Not enough memory for a %1x%2 frame.").arg(W * s).arg(H * s);
        return false;
    }

    const int coreW = d->fixedW ? d->fixedW - 2 * kPad : kTile;
    const int coreH = d->fixedH ? d->fixedH - 2 * kPad : kTile;
    const char *inName = d->inName.c_str();
    const char *outName = d->outName.c_str();

    const int tiles = ((W + coreW - 1) / coreW) * ((H + coreH - 1) / coreH);
    int done = 0;
    for (int y0 = 0; y0 < H; y0 += coreH) {
        for (int x0 = 0; x0 < W; x0 += coreW) {
            if (progress && !progress(double(done++) / tiles))
                return false;
            const int cw = std::min(coreW, W - x0);
            const int ch = std::min(coreH, H - y0);
            // The tile's context, clamped to the frame.
            const int rx = std::max(0, x0 - kPad);
            const int ry = std::max(0, y0 - kPad);
            const int rw = std::min(W, x0 + cw + kPad) - rx;
            const int rh = std::min(H, y0 + ch + kPad) - ry;
            // What the graph is fed: its fixed size, or the context rounded up. The extra rows and
            // columns repeat the edge, and their output is discarded.
            const int iw = d->fixedW ? d->fixedW : (rw + kAlign - 1) / kAlign * kAlign;
            const int ih = d->fixedH ? d->fixedH : (rh + kAlign - 1) / kAlign * kAlign;

            const size_t plane = size_t(iw) * size_t(ih);
            d->input.resize(plane * 3);
            float *r = d->input.data();
            float *g = r + plane;
            float *b = g + plane;
            for (int y = 0; y < ih; ++y) {
                const uchar *line = src.constScanLine(ry + std::min(y, rh - 1));
                for (int x = 0; x < iw; ++x) {
                    const uchar *px = line + 3 * (rx + std::min(x, rw - 1));
                    const size_t i = size_t(y) * size_t(iw) + size_t(x);
                    r[i] = px[0] / 255.0f;
                    g[i] = px[1] / 255.0f;
                    b[i] = px[2] / 255.0f;
                }
            }

            const std::array<int64_t, 4> shape{1, 3, ih, iw};
            std::vector<Ort::Value> result;
            try {
                Ort::Value tensor = inputTensor(d->inputType, d->input, d->inputHalf, shape);
                result = d->session->Run(Ort::RunOptions{nullptr}, &inName, &tensor, 1, &outName,
                                         1);
            } catch (const Ort::Exception &e) {
                *error = QStringLiteral("Restore model failed: %1").arg(QString::fromUtf8(e.what()));
                return false;
            }

            const auto outInfo = result.front().GetTensorTypeAndShapeInfo();
            const std::vector<int64_t> out = outInfo.GetShape();
            if (out.size() != 4 || out[1] != 3 || out[2] != int64_t(ih) * s
                || out[3] != int64_t(iw) * s) {
                *error = QStringLiteral("The restore model returned an unexpected shape; its scale "
                                        "is not %1x.")
                             .arg(s);
                return false;
            }
            // The output can differ from the input: some exports take float and compute in half.
            const float *o = nullptr;
            if (outInfo.GetElementType() == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16) {
                const Ort::Float16_t *h = result.front().GetTensorData<Ort::Float16_t>();
                d->outputFloat.resize(outInfo.GetElementCount());
                for (size_t i = 0; i < d->outputFloat.size(); ++i)
                    d->outputFloat[i] = h[i].ToFloat();
                o = d->outputFloat.data();
            } else if (outInfo.GetElementType() == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
                o = result.front().GetTensorData<float>();
            } else {
                *error = QStringLiteral("The restore model returned neither float32 nor float16.");
                return false;
            }
            const size_t outW = size_t(iw) * size_t(s);
            const size_t outPlane = outW * size_t(ih) * size_t(s);
            // Copy back only the core; the context around it belongs to the neighbouring tiles.
            const int ox = (x0 - rx) * s;
            const int oy = (y0 - ry) * s;
            for (int y = 0; y < ch * s; ++y) {
                uchar *line = dst.scanLine(y0 * s + y) + 3 * size_t(x0) * size_t(s);
                const size_t row = size_t(oy + y) * outW + size_t(ox);
                for (int x = 0; x < cw * s; ++x) {
                    for (int c = 0; c < 3; ++c) {
                        const float v = o[size_t(c) * outPlane + row + size_t(x)];
                        line[3 * x + c] = uchar(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
                    }
                }
            }
        }
    }

    frame = dst;
    return true;
}

} // namespace drift
