#include "engine/FaceLandmarker.h"

#include "engine/GpuPackageParse.h"
#include "engine/OrtSupport.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace drift {
namespace {

using drift::ort::cstrs;
using drift::ort::ortPath;
using drift::ort::sessionNames;

constexpr int kMaxFaces = 4;
constexpr int kLandmarkPoints = 468; // before the iris rings the attention head adds
static_assert(kLandmarkPoints == kFaceMeshPoints);
constexpr int kIrisPoints = 5;       // centre + 4 ring points, per eye

const char *const kDetectorFile = "face_detector.onnx";
const char *const kLandmarkFile = "face_landmark.onnx";

// MediaPipe face-mesh landmark indices. Verified against a rendered debug overlay — see
// tools/facedetect.cpp, which is the only practical way to keep these honest.
constexpr int kIdxNoseTip = 1;
constexpr int kIdxChin = 152;
constexpr int kIdxForehead = 10;
constexpr int kIdxUpperLip = 13;
constexpr int kIdxLowerLip = 14;
constexpr int kIdxMouthLeft = 61;
constexpr int kIdxMouthRight = 291;

// The contour loops live in FaceLandmarker.h — the face-swap alpha ramp and Face111 read the same
// tables, and one definition is the only way those stay in step.
//
// HANDEDNESS: everything there is named in *image* space, matching FaceAnchors — "left" means the
// low-x side of the frame, not the subject's own left. MediaPipe names its sets from the subject's
// point of view, so Drift's kEyeLeft is MediaPipe's FACEMESH_RIGHT_EYE and vice versa. The
// existing kIdxMouthLeft = 61 already follows this convention. Swapping a pair produces mirrored
// liner and shadow, which a symmetric test face will not reveal — check the overlay.
using mpidx::kBrowLeftRing;
using mpidx::kBrowRightRing;
using mpidx::kCheekLeft;
using mpidx::kCheekRight;
using mpidx::kEyeLeftRing;
using mpidx::kEyeRightRing;
using mpidx::kFaceOval;
using mpidx::kLipInner;
using mpidx::kLipOuter;

// Eye-centre pairs (inner and outer corner) used for the pose basis. The iris centres would be the
// obvious choice but they move with gaze; the corners are rigid to the skull.
constexpr int kIdxEyeLeftInner = 133;
constexpr int kIdxEyeLeftOuter = 33;
constexpr int kIdxEyeRightInner = 362;
constexpr int kIdxEyeRightOuter = 263;

struct Detection
{
    QPointF center; // pixels in the source frame
    double size = 0.0;
    double angle = 0.0;
    double score = 0.0;
};

QString resolveFaceModelDir()
{
    const QStringList roots = GpuPackageParse::defaultSearchPaths(
        QStringLiteral("DRIFT_FACE_MODEL_DIR"), QStringLiteral("models/face"),
        QStringLiteral("face-model"));

    // A directory only counts as a model when every piece is there — a half-downloaded folder
    // must not look installed.
    for (const QString &root : roots) {
        const QDir dir(root);
        if (QFile::exists(dir.filePath(QStringLiteral("constants.json")))
            && QFile::exists(dir.filePath(QLatin1String(kDetectorFile)))
            && QFile::exists(dir.filePath(QLatin1String(kLandmarkFile)))) {
            return root;
        }
    }
    return {};
}

float sigmoid(float x)
{
    return 1.0f / (1.0f + std::exp(-x));
}

// Bilinear sample of an RGB888 image, clamped at the edges. Out-of-frame ROI pixels clamp rather
// than go black: a black border inside the crop shifts the landmarks it produces.
void sampleRgb(const QImage &img, double x, double y, float *out)
{
    const int w = img.width();
    const int h = img.height();
    const double cx = std::clamp(x, 0.0, double(w - 1));
    const double cy = std::clamp(y, 0.0, double(h - 1));
    const int x0 = int(cx);
    const int y0 = int(cy);
    const int x1 = std::min(x0 + 1, w - 1);
    const int y1 = std::min(y0 + 1, h - 1);
    const double fx = cx - x0;
    const double fy = cy - y0;

    const uchar *r0 = img.constScanLine(y0);
    const uchar *r1 = img.constScanLine(y1);
    for (int c = 0; c < 3; ++c) {
        const double top = r0[x0 * 3 + c] * (1.0 - fx) + r0[x1 * 3 + c] * fx;
        const double bot = r1[x0 * 3 + c] * (1.0 - fx) + r1[x1 * 3 + c] * fx;
        out[c] = float(top * (1.0 - fy) + bot * fy);
    }
}

struct Vec3
{
    double x = 0.0, y = 0.0, z = 0.0;
};

Vec3 operator-(const Vec3 &a, const Vec3 &b)
{
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vec3 operator+(const Vec3 &a, const Vec3 &b)
{
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vec3 operator*(const Vec3 &a, double s)
{
    return {a.x * s, a.y * s, a.z * s};
}

double length(const Vec3 &v)
{
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

Vec3 normalized(const Vec3 &v)
{
    const double n = length(v);
    return n > 1e-12 ? v * (1.0 / n) : Vec3{};
}

Vec3 cross(const Vec3 &a, const Vec3 &b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

// Shepperd's method: pick the branch whose denominator is largest, so a basis representing a
// half turn does not divide by something near zero.
void basisToQuaternion(const Vec3 &r, const Vec3 &u, const Vec3 &f, double *qx, double *qy,
                       double *qz, double *qw)
{
    const double m00 = r.x, m01 = u.x, m02 = f.x;
    const double m10 = r.y, m11 = u.y, m12 = f.y;
    const double m20 = r.z, m21 = u.z, m22 = f.z;
    const double trace = m00 + m11 + m22;

    if (trace > 0.0) {
        const double s = 0.5 / std::sqrt(trace + 1.0);
        *qw = 0.25 / s;
        *qx = (m21 - m12) * s;
        *qy = (m02 - m20) * s;
        *qz = (m10 - m01) * s;
    } else if (m00 > m11 && m00 > m22) {
        const double s = 2.0 * std::sqrt(1.0 + m00 - m11 - m22);
        *qw = (m21 - m12) / s;
        *qx = 0.25 * s;
        *qy = (m01 + m10) / s;
        *qz = (m02 + m20) / s;
    } else if (m11 > m22) {
        const double s = 2.0 * std::sqrt(1.0 + m11 - m00 - m22);
        *qw = (m02 - m20) / s;
        *qx = (m01 + m10) / s;
        *qy = 0.25 * s;
        *qz = (m12 + m21) / s;
    } else {
        const double s = 2.0 * std::sqrt(1.0 + m22 - m00 - m11);
        *qw = (m10 - m01) / s;
        *qx = (m02 + m20) / s;
        *qy = (m12 + m21) / s;
        *qz = 0.25 * s;
    }

    const double n = std::sqrt(*qx * *qx + *qy * *qy + *qz * *qz + *qw * *qw);
    if (n > 1e-12) {
        *qx /= n;
        *qy /= n;
        *qz /= n;
        *qw /= n;
    }
}

double iou(const QRectF &a, const QRectF &b)
{
    const QRectF i = a.intersected(b);
    if (i.isEmpty())
        return 0.0;
    const double inter = i.width() * i.height();
    return inter / (a.width() * a.height() + b.width() * b.height() - inter);
}

} // namespace

struct FaceLandmarker::Impl
{
    bool loaded = false;
    bool loadAttempted = false;
    QString error;
    QString modelDir;

    std::unique_ptr<Ort::Session> detector, landmark;
    std::vector<std::string> detIn, detOut, lmIn, lmOut;

    int detectorSize = 640;
    int landmarkSize = 192;
    std::vector<int> strides{8, 16, 32};
    // Deliberately loose. YuNet is not rotation invariant and a tilted head scores around 0.58,
    // but the landmark model's own face flag is a second and much stronger gate, so letting weak
    // detections through costs a little time and buys recall on tilted faces.
    double scoreThreshold = 0.4;
    double nmsThreshold = 0.3;
    double roiScale = 1.5;
    double presenceThreshold = 0.5;

    bool ensureLoaded();
    bool loadConstants(const QString &dir);

    QList<Detection> runDetector(const QImage &frame);
    FaceAnchors runLandmark(const QImage &frame, const Detection &roi);
};

bool FaceLandmarker::Impl::loadConstants(const QString &dir)
{
    QFile f(QDir(dir).filePath(QStringLiteral("constants.json")));
    if (!f.open(QIODevice::ReadOnly)) {
        error = QStringLiteral("Face model constants.json missing");
        return false;
    }
    const QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();

    detectorSize = obj.value(QStringLiteral("detector_input_size")).toInt(detectorSize);
    landmarkSize = obj.value(QStringLiteral("landmark_input_size")).toInt(landmarkSize);
    scoreThreshold = obj.value(QStringLiteral("detector_score_threshold")).toDouble(scoreThreshold);
    nmsThreshold = obj.value(QStringLiteral("detector_nms_threshold")).toDouble(nmsThreshold);
    roiScale = obj.value(QStringLiteral("roi_scale")).toDouble(roiScale);
    presenceThreshold =
        obj.value(QStringLiteral("face_presence_threshold")).toDouble(presenceThreshold);

    const QJsonArray s = obj.value(QStringLiteral("detector_strides")).toArray();
    if (!s.isEmpty()) {
        strides.clear();
        for (const QJsonValue &v : s)
            strides.push_back(v.toInt());
    }

    if (detectorSize <= 0 || landmarkSize <= 0 || strides.empty()) {
        error = QStringLiteral("Face model constants.json has unusable values");
        return false;
    }
    return true;
}

bool FaceLandmarker::Impl::ensureLoaded()
{
    if (loadAttempted)
        return loaded;

    modelDir = resolveFaceModelDir();
    if (modelDir.isEmpty()) {
        // Deliberately not latched: the model arrives as an addon the user can install while the
        // app is running, and latching here would make it need a restart. A model that is present
        // but fails to load is latched below, since retrying that just repeats the failure.
        error = QStringLiteral("Face model not found. Install the face model addon, place it in "
                               "models/face, or set DRIFT_FACE_MODEL_DIR.");
        return false;
    }
    // The runtime is an addon too. Unlike the model it cannot be picked up mid-session — the
    // library is loaded once per process — which is why the Addon Manager asks for a restart.
    if (!drift::ort::ensureLoaded(&error))
        return false;
    loadAttempted = true;

    if (!loadConstants(modelDir))
        return false;

    Ort::Env &ortEnv = drift::ort::env();
    const QDir dir(modelDir);
    QString buildError;
    const bool built = drift::ort::buildSessions(
        ortEnv, "face", false, &buildError, [&](Ort::SessionOptions &opts) {
            detector = std::make_unique<Ort::Session>(
                ortEnv, ortPath(dir.filePath(QLatin1String(kDetectorFile))).c_str(), opts);
            landmark = std::make_unique<Ort::Session>(
                ortEnv, ortPath(dir.filePath(QLatin1String(kLandmarkFile))).c_str(), opts);
        });
    if (!built) {
        error = QStringLiteral("Failed to load face model: ") + buildError;
        detector.reset();
        landmark.reset();
        return false;
    }

    detIn = sessionNames(*detector, true);
    detOut = sessionNames(*detector, false);
    lmIn = sessionNames(*landmark, true);
    lmOut = sessionNames(*landmark, false);

    loaded = true;
    return true;
}

// YuNet v2: anchor-free, three strides, each contributing cls/obj/bbox/kps. Scores are already
// through a sigmoid in the graph, so they are clamped rather than activated here.
QList<Detection> FaceLandmarker::Impl::runDetector(const QImage &frame)
{
    QList<Detection> out;
    const int n = detectorSize;

    // Letterbox rather than stretch: a squashed face costs the detector recall, and undoing a
    // uniform scale afterwards is exact.
    const double scale = std::min(double(n) / frame.width(), double(n) / frame.height());
    const int fitW = std::max(1, int(std::lround(frame.width() * scale)));
    const int fitH = std::max(1, int(std::lround(frame.height() * scale)));
    const int padX = (n - fitW) / 2;
    const int padY = (n - fitH) / 2;

    const QImage fitted =
        frame.scaled(fitW, fitH, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

    // BGR, 0..255, no normalization — what YuNet was exported to expect.
    std::vector<float> input(size_t(3) * n * n, 0.0f);
    const size_t plane = size_t(n) * n;
    for (int y = 0; y < fitH; ++y) {
        const uchar *row = fitted.constScanLine(y);
        for (int x = 0; x < fitW; ++x) {
            const size_t idx = size_t(y + padY) * n + (x + padX);
            input[0 * plane + idx] = row[x * 3 + 2];
            input[1 * plane + idx] = row[x * 3 + 1];
            input[2 * plane + idx] = row[x * 3 + 0];
        }
    }

    std::vector<Ort::Value> results;
    try {
        const std::array<int64_t, 4> shape{1, 3, n, n};
        Ort::Value tensor = Ort::Value::CreateTensor<float>(drift::ort::cpuMemory(), input.data(), input.size(),
                                                            shape.data(), shape.size());
        const auto inNames = cstrs(detIn);
        const auto outNames = cstrs(detOut);
        results = detector->Run(Ort::RunOptions{nullptr}, inNames.data(), &tensor, 1,
                                outNames.data(), outNames.size());
    } catch (const Ort::Exception &e) {
        error = QStringLiteral("Face detection failed: ") + QString::fromUtf8(e.what());
        return out;
    }

    auto tensorFor = [&](const QString &name) -> const float * {
        for (size_t i = 0; i < detOut.size(); ++i) {
            if (QString::fromStdString(detOut[i]) == name)
                return results[i].GetTensorData<float>();
        }
        return nullptr;
    };

    struct Candidate
    {
        QRectF box;
        QPointF rightEye, leftEye;
        double score;
    };
    QList<Candidate> candidates;

    for (const int stride : strides) {
        const QString suffix = QString::number(stride);
        const float *cls = tensorFor(QStringLiteral("cls_") + suffix);
        const float *obj = tensorFor(QStringLiteral("obj_") + suffix);
        const float *bbox = tensorFor(QStringLiteral("bbox_") + suffix);
        const float *kps = tensorFor(QStringLiteral("kps_") + suffix);
        if (!cls || !obj || !bbox || !kps) {
            error = QStringLiteral("Face detector is missing the stride-%1 outputs").arg(stride);
            return out;
        }

        const int cols = n / stride;
        const int rows = n / stride;
        for (int i = 0; i < rows * cols; ++i) {
            const double score = std::sqrt(double(std::clamp(cls[i], 0.0f, 1.0f))
                                           * double(std::clamp(obj[i], 0.0f, 1.0f)));
            if (score < scoreThreshold)
                continue;

            const int col = i % cols;
            const int row = i / cols;
            const double cx = (col + bbox[i * 4 + 0]) * stride;
            const double cy = (row + bbox[i * 4 + 1]) * stride;
            const double w = std::exp(bbox[i * 4 + 2]) * stride;
            const double h = std::exp(bbox[i * 4 + 3]) * stride;

            Candidate c;
            c.box = QRectF(cx - w / 2.0, cy - h / 2.0, w, h);
            c.rightEye = QPointF((col + kps[i * 10 + 0]) * stride, (row + kps[i * 10 + 1]) * stride);
            c.leftEye = QPointF((col + kps[i * 10 + 2]) * stride, (row + kps[i * 10 + 3]) * stride);
            c.score = score;
            candidates.append(c);
        }
    }

    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate &a, const Candidate &b) { return a.score > b.score; });

    QList<Candidate> kept;
    for (const Candidate &c : candidates) {
        bool suppressed = false;
        for (const Candidate &k : kept)
            suppressed = suppressed || iou(c.box, k.box) > nmsThreshold;
        if (!suppressed)
            kept.append(c);
        if (kept.size() >= kMaxFaces)
            break;
    }

    // Back out of the letterbox, then build the ROI the landmark model was trained on: a square
    // around the detection, expanded by roiScale, rotated so the eye line is horizontal.
    for (const Candidate &c : kept) {
        const QPointF center((c.box.center().x() - padX) / scale,
                             (c.box.center().y() - padY) / scale);
        const QPointF rightEye((c.rightEye.x() - padX) / scale, (c.rightEye.y() - padY) / scale);
        const QPointF leftEye((c.leftEye.x() - padX) / scale, (c.leftEye.y() - padY) / scale);

        Detection d;
        d.center = center;
        d.size = std::max(c.box.width(), c.box.height()) / scale * roiScale;
        d.angle = std::atan2(leftEye.y() - rightEye.y(), leftEye.x() - rightEye.x());
        d.score = c.score;
        out.append(d);
    }

    std::sort(out.begin(), out.end(),
              [](const Detection &a, const Detection &b) { return a.size > b.size; });
    return out;
}

FaceAnchors FaceLandmarker::Impl::runLandmark(const QImage &frame, const Detection &roi)
{
    FaceAnchors anchors;
    const int n = landmarkSize;
    const double cosA = std::cos(roi.angle);
    const double sinA = std::sin(roi.angle);

    // Crop space -> frame pixels. Used to build the input and, inverted implicitly, to place the
    // landmarks the model returns.
    auto toFrame = [&](double u, double v) {
        const double nx = (u / n - 0.5) * roi.size;
        const double ny = (v / n - 0.5) * roi.size;
        return QPointF(roi.center.x() + nx * cosA - ny * sinA,
                       roi.center.y() + nx * sinA + ny * cosA);
    };

    std::vector<float> input(size_t(3) * n * n);
    const size_t plane = size_t(n) * n;
    for (int y = 0; y < n; ++y) {
        for (int x = 0; x < n; ++x) {
            const QPointF p = toFrame(x + 0.5, y + 0.5);
            float rgb[3];
            sampleRgb(frame, p.x(), p.y(), rgb);
            const size_t idx = size_t(y) * n + x;
            for (int c = 0; c < 3; ++c)
                input[c * plane + idx] = rgb[c] / 255.0f;
        }
    }

    std::vector<Ort::Value> results;
    try {
        const std::array<int64_t, 4> shape{1, 3, n, n};
        Ort::Value tensor = Ort::Value::CreateTensor<float>(drift::ort::cpuMemory(), input.data(), input.size(),
                                                            shape.data(), shape.size());
        const auto inNames = cstrs(lmIn);
        const auto outNames = cstrs(lmOut);
        results = landmark->Run(Ort::RunOptions{nullptr}, inNames.data(), &tensor, 1,
                                outNames.data(), outNames.size());
    } catch (const Ort::Exception &e) {
        error = QStringLiteral("Face landmarking failed: ") + QString::fromUtf8(e.what());
        return anchors;
    }

    const float *mesh = nullptr;
    const float *leftIris = nullptr;
    const float *rightIris = nullptr;
    const float *faceflag = nullptr;
    for (size_t i = 0; i < lmOut.size(); ++i) {
        const std::string &name = lmOut[i];
        if (name == "output_mesh_identity")
            mesh = results[i].GetTensorData<float>();
        else if (name == "output_left_iris")
            leftIris = results[i].GetTensorData<float>();
        else if (name == "output_right_iris")
            rightIris = results[i].GetTensorData<float>();
        else if (name == "conv_faceflag")
            faceflag = results[i].GetTensorData<float>();
    }
    if (!mesh || !leftIris || !rightIris || !faceflag) {
        error = QStringLiteral("Face landmark model returned unexpected outputs");
        return anchors;
    }

    anchors.score = sigmoid(faceflag[0]);
    if (anchors.score < presenceThreshold)
        return anchors; // the ROI does not actually hold a face

    const double fw = frame.width();
    const double fh = frame.height();
    auto meshPoint = [&](int index) {
        const QPointF p = toFrame(mesh[index * 3 + 0], mesh[index * 3 + 1]);
        return QPointF(p.x() / fw, p.y() / fh);
    };
    auto irisPoint = [&](const float *iris, int index) {
        const QPointF p = toFrame(iris[index * 2 + 0], iris[index * 2 + 1]);
        return QPointF(p.x() / fw, p.y() / fh);
    };

    anchors.noseTip = meshPoint(kIdxNoseTip);
    anchors.chin = meshPoint(kIdxChin);
    anchors.forehead = meshPoint(kIdxForehead);
    anchors.mouthLeft = meshPoint(kIdxMouthLeft);
    anchors.mouthRight = meshPoint(kIdxMouthRight);
    anchors.mouthCenter = (meshPoint(kIdxUpperLip) + meshPoint(kIdxLowerLip)) / 2.0;
    anchors.leftEye = irisPoint(leftIris, 0);
    anchors.rightEye = irisPoint(rightIris, 0);

    // Anchor geometry is measured in width-normalized space: uv with y scaled by the frame's
    // aspect, so that a radius means the same thing along both axes. Shaders rebuild that space
    // from u_resolution.
    const double aspect = fh / fw;
    auto toLocal = [aspect](const QPointF &uv) { return QPointF(uv.x(), uv.y() * aspect); };

    double irisSpan = 0.0;
    for (int i = 1; i < kIrisPoints; ++i) {
        const QPointF ring = toLocal(irisPoint(leftIris, i)) - toLocal(anchors.leftEye);
        irisSpan += std::hypot(ring.x(), ring.y());
    }
    anchors.eyeRadius = irisSpan / (kIrisPoints - 1);

    // Left-to-right, so an upright face reads as 0 rather than a half turn. The ROI fed to the
    // model is already eye-aligned, so the model's own left/right outputs are consistent in crop
    // space and this stays stable however the head is tilted.
    const QPointF eyeL = toLocal(anchors.leftEye);
    const QPointF eyeR = toLocal(anchors.rightEye);
    anchors.angle = std::atan2(eyeR.y() - eyeL.y(), eyeR.x() - eyeL.x());

    // Face oval half-axes, measured in the face's own rotated frame so a tilted head still gets
    // an ellipse that hugs it.
    QPointF centroid(0.0, 0.0);
    QList<QPointF> oval;
    oval.reserve(int(kFaceOval.size()));
    for (const int index : kFaceOval) {
        const QPointF p = toLocal(meshPoint(index));
        oval.append(p);
        centroid += p;
    }
    centroid /= double(oval.size());

    const double c = std::cos(-anchors.angle);
    const double s = std::sin(-anchors.angle);
    double maxX = 0.0;
    double maxY = 0.0;
    for (const QPointF &p : oval) {
        const QPointF d = p - centroid;
        maxX = std::max(maxX, std::abs(d.x() * c - d.y() * s));
        maxY = std::max(maxY, std::abs(d.x() * s + d.y() * c));
    }
    anchors.faceRx = maxX;
    anchors.faceRy = maxY;
    anchors.faceCenter = QPointF(centroid.x(), centroid.y() / aspect);

    // Contours, stored in width-normalized space. The oval is re-walked rather than reusing the
    // list above so that every span is filled by the same loop and the order cannot drift apart
    // from contour::Span.
    anchors.contour.reserve(contour::kTotalPoints);
    auto appendLoop = [&](const auto &indices) {
        for (const int index : indices)
            anchors.contour.append(toLocal(meshPoint(index)));
    };
    appendLoop(kFaceOval);
    appendLoop(kLipOuter);
    appendLoop(kLipInner);
    appendLoop(kEyeLeftRing);
    appendLoop(kEyeRightRing);
    appendLoop(kBrowLeftRing);
    appendLoop(kBrowRightRing);
    Q_ASSERT(anchors.contour.size() == contour::kTotalPoints);
    anchors.hasContours = anchors.contour.size() == contour::kTotalPoints;

    auto centroidOf = [&](const auto &indices) {
        QPointF sum(0.0, 0.0);
        for (const int index : indices)
            sum += meshPoint(index);
        return sum / double(indices.size());
    };
    anchors.cheekLeft = centroidOf(kCheekLeft);
    anchors.cheekRight = centroidOf(kCheekRight);

    // Head pose. The mesh's third component is a depth in crop pixels, and the crop-to-frame
    // transform is a rotation about z plus a uniform scale, so z carries over by the scale alone.
    // That makes all three components share one unit and the basis genuinely orthonormal — no PnP
    // solve, and no camera intrinsics we do not have.
    //
    // SIGN CONVENTION, measured rather than assumed: MediaPipe emits smaller z for points nearer
    // the camera, so the nose tip comes back most negative. Checked on five faces via the overlay
    // in tools/facedetect, where (noseTip - eyeMidpoint) . forward was +0.020..+0.029 against
    // +0.005..+0.006 for the chin. So `forward` points out of the face toward the viewer, which is
    // what a 3D prop mounted on this basis needs. Flip it and glasses render facing backwards.
    const double meshScale = roi.size / double(n) / fw;
    auto meshPoint3 = [&](int index) {
        const QPointF p = meshPoint(index);
        return Vec3{p.x(), p.y() * aspect, double(mesh[index * 3 + 2]) * meshScale};
    };
    const Vec3 eyeL3 = (meshPoint3(kIdxEyeLeftInner) + meshPoint3(kIdxEyeLeftOuter)) * 0.5;
    const Vec3 eyeR3 = (meshPoint3(kIdxEyeRightInner) + meshPoint3(kIdxEyeRightOuter)) * 0.5;
    const Vec3 rightAxis0 = eyeR3 - eyeL3;
    anchors.poseScale = length(rightAxis0);
    if (anchors.poseScale > 1e-9) {
        const Vec3 rightAxis = normalized(rightAxis0);
        // Image y grows downward, so forehead-minus-chin already points the way "up" reads on
        // screen. Gram-Schmidt against right keeps the basis square when the head is pitched.
        const Vec3 up0 = meshPoint3(kIdxForehead) - meshPoint3(kIdxChin);
        const Vec3 forwardAxis = normalized(cross(rightAxis, up0));
        const Vec3 upAxis = cross(forwardAxis, rightAxis);
        if (length(forwardAxis) > 0.5) {
            basisToQuaternion(rightAxis, upAxis, forwardAxis, &anchors.poseQx, &anchors.poseQy,
                              &anchors.poseQz, &anchors.poseQw);
            const Vec3 origin = (eyeL3 + eyeR3) * 0.5;
            anchors.poseOx = origin.x;
            anchors.poseOy = origin.y;
            anchors.poseOz = origin.z;
            anchors.hasPose = true;
        }
    }

    // Pose failing means the 3D basis is unusable, so the mesh is omitted rather than stored as a
    // 2D-only cloud the warp effect cannot place.
    if (anchors.hasPose) {
        anchors.mesh.reserve(kLandmarkPoints);
        for (int i = 0; i < kLandmarkPoints; ++i) {
            const Vec3 p = meshPoint3(i);
            anchors.mesh.append(QVector3D(float(p.x), float(p.y), float(p.z)));
        }
        anchors.hasMesh = true;
    }

    anchors.valid = true;
    return anchors;
}

FaceLandmarker::FaceLandmarker()
    : d(std::make_unique<Impl>())
{
}

FaceLandmarker::~FaceLandmarker() = default;

FaceLandmarker &FaceLandmarker::instance()
{
    // Deliberately leaked, for the same reason as Sam2Segmenter: a function-local static is
    // destroyed during exit, after the CUDA driver has begun its own teardown, and freeing the
    // sessions then aborts the process. The OS reclaims this at exit anyway.
    static FaceLandmarker *s = new FaceLandmarker;
    return *s;
}

bool FaceLandmarker::available()
{
    return d->ensureLoaded();
}

QString FaceLandmarker::lastError() const
{
    return d->error;
}

bool FaceLandmarker::modelPresent()
{
    return !resolveFaceModelDir().isEmpty();
}

int FaceLandmarker::maxFaces()
{
    return kMaxFaces;
}

QList<FaceAnchors> FaceLandmarker::detect(const QImage &frame, const QList<FaceAnchors> *hint)
{
    QList<FaceAnchors> out;
    if (!d->ensureLoaded() || frame.isNull())
        return out;

    const QImage rgb = frame.convertToFormat(QImage::Format_RGB888);
    const double aspect = double(rgb.height()) / rgb.width();

    // Re-use the previous frame's geometry as this frame's ROI where we can. Detection is the
    // expensive half and the jittery one, so running it every frame would cost time and stability
    // both.
    // Slot order is preserved across the whole function: index i is the same person from frame to
    // frame, so a clip's face 0 never swaps mid-warp.
    QList<FaceAnchors> tracked;
    QList<QPointF> previousCenter;
    if (hint) {
        for (const FaceAnchors &prev : *hint) {
            previousCenter.append(prev.faceCenter);
            if (!prev.valid) {
                tracked.append(FaceAnchors{});
                continue;
            }
            Detection roi;
            roi.center =
                QPointF(prev.faceCenter.x() * rgb.width(), prev.faceCenter.y() * rgb.height());
            roi.size = 2.0 * std::max(prev.faceRx, prev.faceRy) * d->roiScale * rgb.width();
            roi.angle = prev.angle;
            roi.score = prev.score;
            tracked.append(d->runLandmark(rgb, roi));
        }
    }

    // Any slot the hint could not carry forward — a face that moved too fast, turned away, or
    // left — falls back to a full detection pass. So does the first frame.
    const bool needDetect =
        tracked.isEmpty() || std::any_of(tracked.begin(), tracked.end(),
                                         [](const FaceAnchors &a) { return !a.valid; });
    if (needDetect) {
        const QList<Detection> detections = d->runDetector(rgb);
        QList<FaceAnchors> fresh;
        for (const Detection &roi : detections) {
            const FaceAnchors a = d->runLandmark(rgb, roi);
            if (a.valid)
                fresh.append(a);
        }

        // Fill the gaps from the fresh pass, matching each empty slot to the nearest new face by
        // where that slot's face was last seen. Only genuinely new faces take free slots.
        for (int i = 0; i < tracked.size() && !fresh.isEmpty(); ++i) {
            if (tracked[i].valid)
                continue;
            const QPointF was = previousCenter.value(i);
            int best = -1;
            double bestDist = std::numeric_limits<double>::max();
            for (int j = 0; j < fresh.size(); ++j) {
                const double dx = fresh[j].faceCenter.x() - was.x();
                const double dy = (fresh[j].faceCenter.y() - was.y()) * aspect;
                const double dist = std::hypot(dx, dy);
                if (dist < bestDist) {
                    bestDist = dist;
                    best = j;
                }
            }
            if (best >= 0)
                tracked[i] = fresh.takeAt(best);
        }
        for (const FaceAnchors &a : fresh) {
            if (tracked.size() >= kMaxFaces)
                break;
            tracked.append(a);
        }
    }

    while (tracked.size() > kMaxFaces)
        tracked.removeLast();
    return tracked;
}

} // namespace drift
