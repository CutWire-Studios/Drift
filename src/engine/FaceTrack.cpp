#include "engine/FaceTrack.h"

#include "engine/Face111.h"
#include "engine/GpuEffectDefinition.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QRandomGenerator>
#include <QStandardPaths>
#include <QVector3D>
#include <QtEndian>

#include <algorithm>
#include <cmath>

namespace drift {
namespace {

// v1 wrote each face as a bare array of 24 numbers. v2 writes an object so the contour loops and
// the head pose can ride along, and readFace dispatches on the JSON shape rather than the file
// version — a v1 file is simply one where every face is still an array.
//
// The mesh blob `"m"` is an optional v2 field, not a version bump: tracks baked before the 3D
// face-mesh effect existed omit it, and a missing key is hasMesh = false rather than a hard error.
constexpr int kFormatVersion = 2;
constexpr int kFieldsPerFace = 24;
constexpr int kPoseFields = 8;

// Contour points are quantized to uint16 rather than written as JSON numbers. 128 points is 512
// bytes packed (683 base64) against roughly 2.2 KB spelled out, and decoding is one fromBase64
// plus a memcpy loop instead of 256 QJsonValue constructions per face per frame — which matters
// because loadFaceTrackCached parses on the compositor thread.
//
// The range has to cover width-normalized y, which reaches the frame aspect: 1.78 for 9:16
// portrait. It also has to go negative, because a face at the edge has contour points outside the
// frame and clipping them would kink the mask exactly where it is most visible. 4.0 across 65536
// steps is 0.23 px at 4K width.
constexpr double kContourMin = -1.0;
constexpr double kContourMax = 3.0;
constexpr int kContourBytes = contour::kTotalPoints * 2 * 2;

quint16 quantizeContour(double v)
{
    const double t = (v - kContourMin) / (kContourMax - kContourMin);
    return quint16(std::clamp(std::round(t * 65535.0), 0.0, 65535.0));
}

double dequantizeContour(quint16 q)
{
    return kContourMin + (double(q) / 65535.0) * (kContourMax - kContourMin);
}

QString encodeContour(const QList<QPointF> &contour)
{
    QByteArray raw(kContourBytes, Qt::Uninitialized);
    auto *words = reinterpret_cast<uchar *>(raw.data());
    for (int i = 0; i < contour::kTotalPoints; ++i) {
        qToLittleEndian<quint16>(quantizeContour(contour.at(i).x()), words + i * 4);
        qToLittleEndian<quint16>(quantizeContour(contour.at(i).y()), words + i * 4 + 2);
    }
    return QString::fromLatin1(raw.toBase64());
}

bool decodeContour(const QString &base64, QList<QPointF> *out)
{
    const QByteArray raw = QByteArray::fromBase64(base64.toLatin1());
    // A short blob would decode into a valid-looking but truncated polygon, which renders as
    // garbage makeup rather than as an error. Refuse it.
    if (raw.size() != kContourBytes)
        return false;

    const auto *words = reinterpret_cast<const uchar *>(raw.constData());
    out->clear();
    out->reserve(contour::kTotalPoints);
    for (int i = 0; i < contour::kTotalPoints; ++i) {
        out->append(QPointF(dequantizeContour(qFromLittleEndian<quint16>(words + i * 4)),
                            dequantizeContour(qFromLittleEndian<quint16>(words + i * 4 + 2))));
    }
    return true;
}

// Same uint16 range as contours: the mesh lives in the same width-normalized space as pose origin,
// including negative z and points that sit just outside the frame. 468 * 3 * 2 bytes packed, not
// JSON numbers — spelling out 1404 floats would dwarf the rest of the sidecar.
constexpr int kMeshBytes = kFaceMeshPoints * 3 * 2;

QString encodeMesh(const QList<QVector3D> &mesh)
{
    QByteArray raw(kMeshBytes, Qt::Uninitialized);
    auto *words = reinterpret_cast<uchar *>(raw.data());
    for (int i = 0; i < kFaceMeshPoints; ++i) {
        qToLittleEndian<quint16>(quantizeContour(double(mesh.at(i).x())), words + i * 6);
        qToLittleEndian<quint16>(quantizeContour(double(mesh.at(i).y())), words + i * 6 + 2);
        qToLittleEndian<quint16>(quantizeContour(double(mesh.at(i).z())), words + i * 6 + 4);
    }
    return QString::fromLatin1(raw.toBase64());
}

bool decodeMesh(const QString &base64, QList<QVector3D> *out)
{
    const QByteArray raw = QByteArray::fromBase64(base64.toLatin1());
    // A short blob would decode into a valid-looking but truncated mesh, which the warp would
    // treat as a face. Refuse it, same as contours.
    if (raw.size() != kMeshBytes)
        return false;

    const auto *words = reinterpret_cast<const uchar *>(raw.constData());
    out->clear();
    out->reserve(kFaceMeshPoints);
    for (int i = 0; i < kFaceMeshPoints; ++i) {
        out->append(QVector3D(float(dequantizeContour(qFromLittleEndian<quint16>(words + i * 6))),
                              float(dequantizeContour(qFromLittleEndian<quint16>(words + i * 6 + 2))),
                              float(dequantizeContour(qFromLittleEndian<quint16>(words + i * 6 + 4)))));
    }
    return true;
}

// The 24 v1 fields, rounded to five decimals: that is a twentieth of a pixel across a 4K frame,
// far below anything a warp can show, and it keeps the sidecar roughly a third of the size full
// doubles would need.
QJsonArray appendCoreFields(const FaceAnchors &a)
{
    auto round5 = [](double v) { return std::round(v * 100000.0) / 100000.0; };
    QJsonArray f;
    f.append(a.valid ? 1 : 0);
    for (const QPointF &p : {a.leftEye, a.rightEye, a.noseTip, a.mouthCenter, a.mouthLeft,
                             a.mouthRight, a.chin, a.forehead, a.faceCenter}) {
        f.append(round5(p.x()));
        f.append(round5(p.y()));
    }
    f.append(round5(a.faceRx));
    f.append(round5(a.faceRy));
    f.append(round5(a.angle));
    f.append(round5(a.eyeRadius));
    f.append(round5(a.score));
    return f;
}

void appendFace(QJsonArray *out, const FaceAnchors &a)
{
    // Same five decimals the core fields use. Left raw, a quaternion component serializes with up
    // to seventeen significant digits, which would cost more per frame than the packed contour
    // blob does — for precision far below what any of this can express.
    auto round5 = [](double v) { return std::round(v * 100000.0) / 100000.0; };

    QJsonObject face;
    face[QStringLiteral("f")] = appendCoreFields(a);
    if (a.hasContours && a.contour.size() == contour::kTotalPoints) {
        face[QStringLiteral("c")] = encodeContour(a.contour);
        // Cheeks are centroids rather than contour vertices, so they travel as plain numbers
        // instead of joining the packed loop.
        face[QStringLiteral("k")] = QJsonArray{round5(a.cheekLeft.x()), round5(a.cheekLeft.y()),
                                               round5(a.cheekRight.x()), round5(a.cheekRight.y())};
    }
    if (a.hasPose) {
        // Six decimals on the quaternion: it is a direction, and five would show up as about a
        // hundredth of a degree of jitter on a prop mounted to it.
        auto round6 = [](double v) { return std::round(v * 1000000.0) / 1000000.0; };
        face[QStringLiteral("p")] = QJsonArray{round6(a.poseQx),    round6(a.poseQy),
                                               round6(a.poseQz),    round6(a.poseQw),
                                               round5(a.poseScale), round5(a.poseOx),
                                               round5(a.poseOy),    round5(a.poseOz)};
    }
    if (a.hasMesh && a.mesh.size() == kFaceMeshPoints)
        face[QStringLiteral("m")] = encodeMesh(a.mesh);
    out->append(face);
}

bool readCoreFields(const QJsonArray &f, FaceAnchors *a)
{
    if (f.size() != kFieldsPerFace)
        return false;
    int i = 0;
    a->valid = f.at(i++).toInt() != 0;
    QPointF *points[] = {&a->leftEye, &a->rightEye,  &a->noseTip, &a->mouthCenter, &a->mouthLeft,
                         &a->mouthRight, &a->chin, &a->forehead, &a->faceCenter};
    for (QPointF *p : points) {
        const double x = f.at(i++).toDouble();
        const double y = f.at(i++).toDouble();
        *p = QPointF(x, y);
    }
    a->faceRx = f.at(i++).toDouble();
    a->faceRy = f.at(i++).toDouble();
    a->angle = f.at(i++).toDouble();
    a->eyeRadius = f.at(i++).toDouble();
    a->score = f.at(i++).toDouble();
    return true;
}

// Dispatches on shape, not on the file's version field: an array is a v1 face, an object is a v2
// one. That keeps a mixed or hand-edited file readable and means the version check stays a single
// range test.
bool readFace(const QJsonValue &value, FaceAnchors *a)
{
    if (value.isArray())
        return readCoreFields(value.toArray(), a);
    if (!value.isObject())
        return false;

    const QJsonObject face = value.toObject();
    if (!readCoreFields(face.value(QStringLiteral("f")).toArray(), a))
        return false;

    if (face.contains(QStringLiteral("c"))) {
        if (!decodeContour(face.value(QStringLiteral("c")).toString(), &a->contour))
            return false;
        a->hasContours = true;
        const QJsonArray cheeks = face.value(QStringLiteral("k")).toArray();
        if (cheeks.size() == 4) {
            a->cheekLeft = QPointF(cheeks.at(0).toDouble(), cheeks.at(1).toDouble());
            a->cheekRight = QPointF(cheeks.at(2).toDouble(), cheeks.at(3).toDouble());
        }
    }

    const QJsonArray pose = face.value(QStringLiteral("p")).toArray();
    if (pose.size() == kPoseFields) {
        a->poseQx = pose.at(0).toDouble();
        a->poseQy = pose.at(1).toDouble();
        a->poseQz = pose.at(2).toDouble();
        a->poseQw = pose.at(3).toDouble();
        a->poseScale = pose.at(4).toDouble();
        a->poseOx = pose.at(5).toDouble();
        a->poseOy = pose.at(6).toDouble();
        a->poseOz = pose.at(7).toDouble();
        a->hasPose = true;
    }

    // Absent `"m"` is the pre-mesh v2 shape, not an error — those tracks still drive warps and
    // makeup. A present but wrong-sized blob is refused, same as contours.
    if (face.contains(QStringLiteral("m"))) {
        if (!decodeMesh(face.value(QStringLiteral("m")).toString(), &a->mesh))
            return false;
        a->hasMesh = true;
    }
    return true;
}

QPointF lerp(const QPointF &a, const QPointF &b, double t)
{
    return a + (b - a) * t;
}

QVector3D lerp(const QVector3D &a, const QVector3D &b, double t)
{
    return a + (b - a) * float(t);
}

// Angles live on a circle: a face crossing the +/-pi seam would otherwise spin most of the way
// round between two adjacent frames.
// Normalized lerp with sign alignment. Over the sub-frame gaps this interpolates, the angular
// spread is small enough that nlerp and slerp are indistinguishable, and nlerp cannot divide by a
// near-zero sine. Aligning the signs first is what stops a quaternion and its negation — the same
// rotation — from interpolating the long way round.
void nlerpPose(const FaceAnchors &a, const FaceAnchors &b, double t, FaceAnchors *out)
{
    double bx = b.poseQx, by = b.poseQy, bz = b.poseQz, bw = b.poseQw;
    if (a.poseQx * bx + a.poseQy * by + a.poseQz * bz + a.poseQw * bw < 0.0) {
        bx = -bx;
        by = -by;
        bz = -bz;
        bw = -bw;
    }
    double qx = a.poseQx + (bx - a.poseQx) * t;
    double qy = a.poseQy + (by - a.poseQy) * t;
    double qz = a.poseQz + (bz - a.poseQz) * t;
    double qw = a.poseQw + (bw - a.poseQw) * t;
    const double n = std::sqrt(qx * qx + qy * qy + qz * qz + qw * qw);
    if (n < 1e-9) {
        out->hasPose = false;
        return;
    }
    out->poseQx = qx / n;
    out->poseQy = qy / n;
    out->poseQz = qz / n;
    out->poseQw = qw / n;
    out->poseScale = a.poseScale + (b.poseScale - a.poseScale) * t;
    out->poseOx = a.poseOx + (b.poseOx - a.poseOx) * t;
    out->poseOy = a.poseOy + (b.poseOy - a.poseOy) * t;
    out->poseOz = a.poseOz + (b.poseOz - a.poseOz) * t;
    out->hasPose = true;
}

double lerpAngle(double a, double b, double t)
{
    double delta = std::fmod(b - a, 2.0 * M_PI);
    if (delta > M_PI)
        delta -= 2.0 * M_PI;
    else if (delta < -M_PI)
        delta += 2.0 * M_PI;
    return a + delta * t;
}

struct CacheEntry
{
    QDateTime modified;
    qint64 size = 0;
    std::shared_ptr<const FaceTrack> track;
};

QMutex g_cacheMutex;
QHash<QString, CacheEntry> g_cache;

} // namespace

FaceAnchors FaceTrack::sample(TimeUs relativeUs, int faceIndex) const
{
    if (frames.isEmpty() || fps <= 0 || faceIndex < 0)
        return {};

    const double frameUs = double(kUsPerSecond) / fps;
    const double exact = double(relativeUs) / frameUs;
    if (exact <= 0.0) {
        const FaceTrackFrame &f = frames.first();
        return faceIndex < f.faces.size() ? f.faces.at(faceIndex) : FaceAnchors{};
    }
    if (exact >= frames.size() - 1) {
        const FaceTrackFrame &f = frames.last();
        return faceIndex < f.faces.size() ? f.faces.at(faceIndex) : FaceAnchors{};
    }

    const int i0 = int(exact);
    const int i1 = i0 + 1;
    const double t = exact - i0;

    const FaceTrackFrame &f0 = frames.at(i0);
    const FaceTrackFrame &f1 = frames.at(i1);
    if (faceIndex >= f0.faces.size() || faceIndex >= f1.faces.size())
        return {};

    const FaceAnchors &a = f0.faces.at(faceIndex);
    const FaceAnchors &b = f1.faces.at(faceIndex);
    if (!a.valid || !b.valid)
        return {};

    FaceAnchors out;
    out.valid = true;
    out.leftEye = lerp(a.leftEye, b.leftEye, t);
    out.rightEye = lerp(a.rightEye, b.rightEye, t);
    out.noseTip = lerp(a.noseTip, b.noseTip, t);
    out.mouthCenter = lerp(a.mouthCenter, b.mouthCenter, t);
    out.mouthLeft = lerp(a.mouthLeft, b.mouthLeft, t);
    out.mouthRight = lerp(a.mouthRight, b.mouthRight, t);
    out.chin = lerp(a.chin, b.chin, t);
    out.forehead = lerp(a.forehead, b.forehead, t);
    out.faceCenter = lerp(a.faceCenter, b.faceCenter, t);
    out.faceRx = a.faceRx + (b.faceRx - a.faceRx) * t;
    out.faceRy = a.faceRy + (b.faceRy - a.faceRy) * t;
    out.angle = lerpAngle(a.angle, b.angle, t);
    out.eyeRadius = a.eyeRadius + (b.eyeRadius - a.eyeRadius) * t;
    out.score = a.score + (b.score - a.score) * t;

    // Contours, pose, and mesh gate on both neighbours having them, the same rule `valid`
    // follows: a clip re-scanned only partway, or one bracketing a v1-era frame, must not hand
    // a shader half a mask.
    if (a.hasContours && b.hasContours && a.contour.size() == b.contour.size()) {
        out.contour.reserve(a.contour.size());
        for (int i = 0; i < a.contour.size(); ++i)
            out.contour.append(lerp(a.contour.at(i), b.contour.at(i), t));
        out.cheekLeft = lerp(a.cheekLeft, b.cheekLeft, t);
        out.cheekRight = lerp(a.cheekRight, b.cheekRight, t);
        out.hasContours = true;
    }
    if (a.hasPose && b.hasPose)
        nlerpPose(a, b, t, &out);
    if (a.hasMesh && b.hasMesh && a.mesh.size() == kFaceMeshPoints
        && b.mesh.size() == kFaceMeshPoints) {
        out.mesh.reserve(kFaceMeshPoints);
        for (int i = 0; i < kFaceMeshPoints; ++i)
            out.mesh.append(lerp(a.mesh.at(i), b.mesh.at(i), t));
        out.hasMesh = true;
    }

    return out;
}

QList<FaceAnchors> FaceTrack::sampleAll(TimeUs relativeUs) const
{
    int slotCount = 0;
    for (const FaceTrackFrame &f : frames)
        slotCount = qMax(slotCount, int(f.faces.size()));

    QList<FaceAnchors> out;
    out.reserve(slotCount);
    for (int i = 0; i < slotCount; ++i)
        out.append(sample(relativeUs, i));
    return out;
}

namespace {

// One window's running totals. The old parallel-array form did not survive 128 contour points,
// and the four groups need separate counters anyway: a frame can be valid without contours or mesh.
struct SmoothAccumulator
{
    QPointF leftEye, rightEye, noseTip, mouthCenter, mouthLeft, mouthRight, chin, forehead,
        faceCenter;
    double faceRx = 0.0, faceRy = 0.0, eyeRadius = 0.0;
    double angleX = 0.0, angleY = 0.0; // averaged as a vector, so the +/-pi seam does not cancel
    int n = 0;

    QList<QPointF> contour;
    QPointF cheekLeft, cheekRight;
    int nContour = 0;

    double qx = 0.0, qy = 0.0, qz = 0.0, qw = 0.0;
    double poseScale = 0.0, poseOx = 0.0, poseOy = 0.0, poseOz = 0.0;
    int nPose = 0;

    QList<QVector3D> mesh;
    int nMesh = 0;

    void add(const FaceAnchors &a, const FaceAnchors &centre)
    {
        leftEye += a.leftEye;
        rightEye += a.rightEye;
        noseTip += a.noseTip;
        mouthCenter += a.mouthCenter;
        mouthLeft += a.mouthLeft;
        mouthRight += a.mouthRight;
        chin += a.chin;
        forehead += a.forehead;
        faceCenter += a.faceCenter;
        faceRx += a.faceRx;
        faceRy += a.faceRy;
        eyeRadius += a.eyeRadius;
        angleX += std::cos(a.angle);
        angleY += std::sin(a.angle);
        ++n;

        if (a.hasContours && a.contour.size() == contour::kTotalPoints) {
            if (contour.isEmpty())
                contour.resize(contour::kTotalPoints);
            for (int i = 0; i < contour::kTotalPoints; ++i)
                contour[i] += a.contour.at(i);
            cheekLeft += a.cheekLeft;
            cheekRight += a.cheekRight;
            ++nContour;
        }

        if (a.hasPose) {
            // Align each sample against the window's centre frame before summing, or a quaternion
            // and its negation — the same rotation — cancel instead of reinforcing.
            const double dot = a.poseQx * centre.poseQx + a.poseQy * centre.poseQy
                + a.poseQz * centre.poseQz + a.poseQw * centre.poseQw;
            const double s = dot < 0.0 ? -1.0 : 1.0;
            qx += a.poseQx * s;
            qy += a.poseQy * s;
            qz += a.poseQz * s;
            qw += a.poseQw * s;
            poseScale += a.poseScale;
            poseOx += a.poseOx;
            poseOy += a.poseOy;
            poseOz += a.poseOz;
            ++nPose;
        }

        if (a.hasMesh && a.mesh.size() == kFaceMeshPoints) {
            if (mesh.isEmpty())
                mesh.resize(kFaceMeshPoints);
            for (int i = 0; i < kFaceMeshPoints; ++i)
                mesh[i] += a.mesh.at(i);
            ++nMesh;
        }
    }

    void writeTo(FaceAnchors *out) const
    {
        const double inv = 1.0 / double(n);
        out->leftEye = leftEye * inv;
        out->rightEye = rightEye * inv;
        out->noseTip = noseTip * inv;
        out->mouthCenter = mouthCenter * inv;
        out->mouthLeft = mouthLeft * inv;
        out->mouthRight = mouthRight * inv;
        out->chin = chin * inv;
        out->forehead = forehead * inv;
        out->faceCenter = faceCenter * inv;
        out->faceRx = faceRx * inv;
        out->faceRy = faceRy * inv;
        out->eyeRadius = eyeRadius * inv;
        out->angle = std::atan2(angleY, angleX);

        if (nContour >= 2 && out->hasContours
            && out->contour.size() == contour::kTotalPoints) {
            const double invC = 1.0 / double(nContour);
            for (int i = 0; i < contour::kTotalPoints; ++i)
                out->contour[i] = contour.at(i) * invC;
            out->cheekLeft = cheekLeft * invC;
            out->cheekRight = cheekRight * invC;
        }

        if (nPose >= 2 && out->hasPose) {
            const double norm = std::sqrt(qx * qx + qy * qy + qz * qz + qw * qw);
            if (norm > 1e-9) {
                out->poseQx = qx / norm;
                out->poseQy = qy / norm;
                out->poseQz = qz / norm;
                out->poseQw = qw / norm;
            }
            const double invP = 1.0 / double(nPose);
            out->poseScale = poseScale * invP;
            out->poseOx = poseOx * invP;
            out->poseOy = poseOy * invP;
            out->poseOz = poseOz * invP;
        }

        if (nMesh >= 2 && out->hasMesh && out->mesh.size() == kFaceMeshPoints) {
            const float invM = 1.0f / float(nMesh);
            for (int i = 0; i < kFaceMeshPoints; ++i)
                out->mesh[i] = mesh.at(i) * invM;
        }
    }
};

} // namespace

void smoothFaceTrack(FaceTrack *track, int radius)
{
    const int frameCount = track->frames.size();
    if (frameCount < 3 || radius < 1)
        return;

    int slotCount = 0;
    for (const FaceTrackFrame &f : track->frames)
        slotCount = qMax(slotCount, int(f.faces.size()));

    // A copy-on-write snapshot: reads come from the unsmoothed source so the window does not
    // consume its own output. Because `contour` is one QList rather than several, a frame detaches
    // by copying three pointers per face, not two kilobytes.
    const FaceTrack source = *track;
    for (int slot = 0; slot < slotCount; ++slot) {
        for (int i = 0; i < frameCount; ++i) {
            if (slot >= track->frames[i].faces.size() || !track->frames[i].faces[slot].valid)
                continue;

            const FaceAnchors &centre = source.frames[i].faces[slot];
            SmoothAccumulator acc;
            for (int d = -radius; d <= radius; ++d) {
                const int j = i + d;
                if (j < 0 || j >= frameCount || slot >= source.frames[j].faces.size())
                    continue;
                const FaceAnchors &a = source.frames[j].faces[slot];
                if (!a.valid)
                    continue;
                acc.add(a, centre);
            }
            if (acc.n < 2)
                continue;
            acc.writeTo(&track->frames[i].faces[slot]);
        }
    }
}

void applyFaceUniforms(QMap<QString, QVariant> *parameters, const QList<FaceAnchors> &faceSlots)
{
    // Consumed rather than forwarded: "faceIndex" selects the slot and is not a shader uniform.
    const int faceIndex = parameters->take(QStringLiteral("faceIndex")).toInt();
    FaceAnchors face;
    if (faceIndex >= 0 && faceIndex < faceSlots.size())
        face = faceSlots.at(faceIndex);

    parameters->insert(QStringLiteral("u_faceValid"), face.valid ? 1.0 : 0.0);
    if (!face.valid)
        return;

    auto point = [&](const char *name, const QPointF &p) {
        parameters->insert(QLatin1String(name) + QLatin1String("X"), p.x());
        parameters->insert(QLatin1String(name) + QLatin1String("Y"), p.y());
    };
    point("u_faceLeftEye", face.leftEye);
    point("u_faceRightEye", face.rightEye);
    point("u_faceNose", face.noseTip);
    point("u_faceMouth", face.mouthCenter);
    point("u_faceMouthLeft", face.mouthLeft);
    point("u_faceMouthRight", face.mouthRight);
    point("u_faceChin", face.chin);
    point("u_faceForehead", face.forehead);
    point("u_faceCenter", face.faceCenter);
    parameters->insert(QStringLiteral("u_faceRx"), face.faceRx);
    parameters->insert(QStringLiteral("u_faceRy"), face.faceRy);
    parameters->insert(QStringLiteral("u_faceAngle"), face.angle);
    parameters->insert(QStringLiteral("u_faceEyeRadius"), face.eyeRadius);

    // Everything above this line is what the pre-contour warp effects bind, unchanged. Everything
    // below is additive, and a shader that does not declare it pays nothing: uniformLocation
    // returns -1 for a name the program never mentions.
    parameters->insert(QStringLiteral("u_faceHasContours"), face.hasContours ? 1.0 : 0.0);
    if (face.hasContours && face.contour.size() == contour::kTotalPoints) {
        auto loop = [&](const char *name, contour::Span span) {
            GpuFloatArray array;
            array.tupleSize = 2;
            array.values.reserve(span.count * 2);
            for (int i = 0; i < span.count; ++i) {
                const QPointF &q = face.contour.at(span.offset + i);
                array.values.append(float(q.x()));
                array.values.append(float(q.y()));
            }
            parameters->insert(QLatin1String(name), QVariant::fromValue(array));
        };
        loop("u_faceOval", contour::kOval);
        loop("u_faceLipOuter", contour::kLipOuter);
        loop("u_faceLipInner", contour::kLipInner);
        loop("u_faceEyeLeft", contour::kEyeLeft);
        loop("u_faceEyeRight", contour::kEyeRight);
        loop("u_faceBrowLeft", contour::kBrowLeft);
        loop("u_faceBrowRight", contour::kBrowRight);
        point("u_faceCheekLeft", face.cheekLeft);
        point("u_faceCheekRight", face.cheekRight);
    }

    parameters->insert(QStringLiteral("u_facePoseValid"), face.hasPose ? 1.0 : 0.0);
    if (face.hasPose) {
        // Handed to shaders as the basis rather than the quaternion: a shader wants axes, and
        // rebuilding them from four floats in GLSL would be the same maths in a worse place.
        const double x = face.poseQx, y = face.poseQy, z = face.poseQz, w = face.poseQw;
        const double right[3] = {1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w)};
        const double up[3] = {2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w)};
        const double fwd[3] = {2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y)};
        auto axis = [&](const char *name, const double v[3]) {
            parameters->insert(QLatin1String(name) + QLatin1String("X"), v[0]);
            parameters->insert(QLatin1String(name) + QLatin1String("Y"), v[1]);
            parameters->insert(QLatin1String(name) + QLatin1String("Z"), v[2]);
        };
        axis("u_facePoseRight", right);
        axis("u_facePoseUp", up);
        axis("u_facePoseFwd", fwd);
        parameters->insert(QStringLiteral("u_facePoseOriginX"), face.poseOx);
        parameters->insert(QStringLiteral("u_facePoseOriginY"), face.poseOy);
        parameters->insert(QStringLiteral("u_facePoseOriginZ"), face.poseOz);
        parameters->insert(QStringLiteral("u_facePoseScale"), face.poseScale);
        parameters->insert(QStringLiteral("u_faceRoll"), std::atan2(right[1], right[0]));
        parameters->insert(QStringLiteral("u_faceYaw"), std::asin(std::clamp(-fwd[0], -1.0, 1.0)));
        parameters->insert(QStringLiteral("u_facePitch"), std::asin(std::clamp(up[2], -1.0, 1.0)));
    }

    // Flag only for the full mesh: 468 vertices are far too large for uniforms. The model3d path
    // reads FaceAnchors directly from the sampled track. The 111-point reduction does fit, and is
    // what "geometry": "face111" passes draw and what reshape shaders read.
    parameters->insert(QStringLiteral("u_faceHasMesh"), face.hasMesh ? 1.0 : 0.0);
    if (face.hasMesh && face.mesh.size() == kFaceMeshPoints) {
        GpuFloatArray array;
        array.tupleSize = 2;
        face111::fromMediaPipe(face.mesh, &array.values);
        parameters->insert(QStringLiteral("u_faceLandmarks111"), QVariant::fromValue(array));
    }
}

QString faceTrackCacheDir()
{
    const QString root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (root.isEmpty())
        return {};
    const QString dir = QDir(root).filePath(QStringLiteral("facetracks"));
    if (!QDir().mkpath(dir))
        return {};
    return dir;
}

QString newFaceTrackPath()
{
    const QString dir = faceTrackCacheDir();
    if (dir.isEmpty())
        return {};
    const QString name = QStringLiteral("face-%1-%2.json")
                             .arg(QDateTime::currentMSecsSinceEpoch())
                             .arg(QRandomGenerator::global()->bounded(100000), 5, 10,
                                  QLatin1Char('0'));
    return QDir(dir).filePath(name);
}

QString faceSwapSourcePath(const QString &photoPath)
{
    if (photoPath.isEmpty())
        return {};
    const QString root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (root.isEmpty())
        return {};
    const QString dir = QDir(root).filePath(QStringLiteral("faceswapsources"));
    if (!QDir().mkpath(dir))
        return {};

    const QFileInfo info(photoPath);
    const QString key = QStringLiteral("%1|%2|%3")
                            .arg(info.absoluteFilePath())
                            .arg(info.lastModified().toMSecsSinceEpoch())
                            .arg(info.size());
    const QByteArray digest =
        QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha1).toHex();
    return QDir(dir).filePath(QStringLiteral("photo-%1.json").arg(QString::fromLatin1(digest)));
}

bool writeFaceTrack(const QString &path, const FaceTrack &track, QString *errorOut)
{
    QJsonObject root;
    root[QStringLiteral("version")] = kFormatVersion;
    root[QStringLiteral("fps")] = track.fps;
    root[QStringLiteral("startSrcUs")] = qint64(track.startSrcUs);

    QJsonArray frames;
    for (const FaceTrackFrame &frame : track.frames) {
        QJsonArray faces;
        for (const FaceAnchors &a : frame.faces)
            appendFace(&faces, a);
        frames.append(faces);
    }
    root[QStringLiteral("frames")] = frames;

    const QString partPath = path + QStringLiteral(".part");
    QFile f(partPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorOut)
            *errorOut = QStringLiteral("Could not write %1").arg(partPath);
        return false;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    f.close();

    QFile::remove(path);
    if (!QFile::rename(partPath, path)) {
        QFile::remove(partPath);
        if (errorOut)
            *errorOut = QStringLiteral("Could not finalize %1").arg(path);
        return false;
    }
    return true;
}

bool readFaceTrack(const QString &path, FaceTrack *out, QString *errorOut)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (errorOut)
            *errorOut = QStringLiteral("Could not read %1").arg(path);
        return false;
    }

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (errorOut)
            *errorOut = QStringLiteral("Face track %1 is not valid JSON").arg(path);
        return false;
    }

    // Reject the future, accept the past. A v1 sidecar has no contours, so makeup effects see
    // u_faceHasContours = 0 and pass through, while every warp effect keeps working untouched —
    // which is the whole point of not hard-breaking on the version bump.
    const QJsonObject root = doc.object();
    const int version = root.value(QStringLiteral("version")).toInt();
    if (version < 1 || version > kFormatVersion) {
        if (errorOut)
            *errorOut = QStringLiteral("Face track %1 has an unsupported format version").arg(path);
        return false;
    }

    out->fps = root.value(QStringLiteral("fps")).toInt();
    out->startSrcUs = TimeUs(root.value(QStringLiteral("startSrcUs")).toInteger(0));
    out->frames.clear();

    const QJsonArray frames = root.value(QStringLiteral("frames")).toArray();
    out->frames.reserve(frames.size());
    for (const QJsonValue &frameValue : frames) {
        FaceTrackFrame frame;
        const QJsonArray faces = frameValue.toArray();
        for (const QJsonValue &faceValue : faces) {
            FaceAnchors a;
            if (!readFace(faceValue, &a)) {
                if (errorOut)
                    *errorOut = QStringLiteral("Face track %1 has a malformed entry").arg(path);
                return false;
            }
            frame.faces.append(a);
        }
        out->frames.append(frame);
    }

    if (out->fps <= 0) {
        if (errorOut)
            *errorOut = QStringLiteral("Face track %1 has no frame rate").arg(path);
        return false;
    }
    return true;
}

std::shared_ptr<const FaceTrack> loadFaceTrackCached(const QString &path)
{
    if (path.isEmpty())
        return nullptr;

    const QFileInfo info(path);
    if (!info.exists())
        return nullptr;

    QMutexLocker lock(&g_cacheMutex);
    const auto it = g_cache.constFind(path);
    if (it != g_cache.cend() && it->modified == info.lastModified() && it->size == info.size())
        return it->track;

    auto track = std::make_shared<FaceTrack>();
    QString error;
    if (!readFaceTrack(path, track.get(), &error)) {
        // Cache the failure too: a broken sidecar must not mean a disk read on every composited
        // frame for the rest of the session.
        g_cache.insert(path, CacheEntry{info.lastModified(), info.size(), nullptr});
        return nullptr;
    }

    CacheEntry entry{info.lastModified(), info.size(), track};
    g_cache.insert(path, entry);
    return track;
}

} // namespace drift
