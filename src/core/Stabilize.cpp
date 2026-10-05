#include "Stabilize.h"

#include <QFile>
#include <QRegularExpression>
#include <QtMath>

#include <algorithm>
#include <cmath>

namespace drift {
namespace {

double medianInPlace(QVector<double> &values)
{
    if (values.isEmpty())
        return 0.0;
    const int mid = values.size() / 2;
    std::nth_element(values.begin(), values.begin() + mid, values.end());
    const double upper = values.at(mid);
    if (values.size() % 2)
        return upper;
    std::nth_element(values.begin(), values.begin() + mid - 1, values.begin() + mid);
    return 0.5 * (values.at(mid - 1) + upper);
}

QPointF medianTranslation(const QVector<double> &vx, const QVector<double> &vy)
{
    QVector<double> x = vx;
    QVector<double> y = vy;
    return QPointF(medianInPlace(x), medianInPlace(y));
}

bool readI16(QFile &file, qint16 *value)
{
    return file.read(reinterpret_cast<char *>(value), sizeof(*value)) == sizeof(*value);
}

bool readI32(QFile &file, qint32 *value)
{
    return file.read(reinterpret_cast<char *>(value), sizeof(*value)) == sizeof(*value);
}

bool readF64(QFile &file, double *value)
{
    return file.read(reinterpret_cast<char *>(value), sizeof(*value)) == sizeof(*value);
}

QVector<QPointF> readBinaryTrf(QFile &file)
{
    // TRF1 + accuracy/shakiness/stepSize (int32) + contrastThreshold (double)
    if (!file.seek(4))
        return {};
    qint32 accuracy = 0;
    qint32 shakiness = 0;
    qint32 stepSize = 0;
    double contrast = 0.0;
    if (!readI32(file, &accuracy) || !readI32(file, &shakiness) || !readI32(file, &stepSize)
        || !readF64(file, &contrast)) {
        return {};
    }

    QVector<QPointF> frames;
    while (!file.atEnd()) {
        qint32 frameNum = 0;
        qint32 count = 0;
        if (!readI32(file, &frameNum) || !readI32(file, &count))
            break;
        if (count < 0 || count > (1 << 20))
            break;
        if (frameNum < 0 || frameNum > (1 << 22))
            break;

        QVector<double> vx;
        QVector<double> vy;
        vx.reserve(count);
        vy.reserve(count);
        bool ok = true;
        for (qint32 i = 0; i < count; ++i) {
            qint16 vxi = 0, vyi = 0, fx = 0, fy = 0, size = 0;
            double fieldContrast = 0.0;
            double match = 0.0;
            if (!readI16(file, &vxi) || !readI16(file, &vyi) || !readI16(file, &fx)
                || !readI16(file, &fy) || !readI16(file, &size) || !readF64(file, &fieldContrast)
                || !readF64(file, &match)) {
                ok = false;
                break;
            }
            vx.append(vxi);
            vy.append(vyi);
        }
        if (!ok)
            break;

        if (frameNum >= frames.size())
            frames.resize(frameNum + 1);
        frames[frameNum] = medianTranslation(vx, vy);
    }
    return frames;
}

QVector<QPointF> readAsciiTrf(QFile &file)
{
    file.seek(0);
    QVector<QPointF> frames;
    const QRegularExpression lmRe(
        QStringLiteral(R"(\(LM\s+([-+0-9.]+)\s+([-+0-9.]+))"));
    const QRegularExpression frameRe(
        QStringLiteral(R"(^Frame\s+(\d+))"));

    while (!file.atEnd()) {
        const QByteArray line = file.readLine();
        const QString text = QString::fromLatin1(line);
        const QRegularExpressionMatch frameMatch = frameRe.match(text);
        if (!frameMatch.hasMatch())
            continue;

        const int frameNum = frameMatch.captured(1).toInt();
        QVector<double> vx;
        QVector<double> vy;
        QRegularExpressionMatchIterator it = lmRe.globalMatch(text);
        while (it.hasNext()) {
            const QRegularExpressionMatch lm = it.next();
            vx.append(lm.captured(1).toDouble());
            vy.append(lm.captured(2).toDouble());
        }

        if (frameNum >= frames.size())
            frames.resize(frameNum + 1);
        frames[frameNum] = medianTranslation(vx, vy);
    }
    return frames;
}

QVector<QPointF> accumulatePath(const QVector<QPointF> &translations)
{
    QVector<QPointF> path(translations.size());
    QPointF acc;
    for (int i = 0; i < translations.size(); ++i) {
        acc += translations.at(i);
        path[i] = acc;
    }
    return path;
}

QVector<QPointF> smoothPath(const QVector<QPointF> &path, int radius)
{
    if (radius <= 0 || path.size() < 2)
        return path;

    QVector<QPointF> out(path.size());
    for (int i = 0; i < path.size(); ++i) {
        const int a = qMax(0, i - radius);
        const int b = qMin(path.size() - 1, i + radius);
        QPointF sum;
        for (int k = a; k <= b; ++k)
            sum += path.at(k);
        const double n = static_cast<double>(b - a + 1);
        out[i] = QPointF(sum.x() / n, sum.y() / n);
    }
    return out;
}

QVector<QPointF> compensationPath(const QVector<QPointF> &camera, int smoothing, bool tripod)
{
    if (camera.isEmpty())
        return {};

    QVector<QPointF> out(camera.size());
    // vid.stab's camera-path step stores `C - S` ("high frequency must be
    // transformed away"). Applying `S - C` instead doubles the shake.
    if (tripod) {
        const QPointF origin = camera.first();
        for (int i = 0; i < camera.size(); ++i)
            out[i] = camera.at(i) - origin;
        return out;
    }

    const QVector<QPointF> smoothed = smoothPath(camera, qMax(0, smoothing));
    for (int i = 0; i < camera.size(); ++i)
        out[i] = camera.at(i) - smoothed.at(i);
    return out;
}

bool residualWithinEpsilon(const QVector<QPointF> &points, int start, int end, double epsilon)
{
    const int count = end - start + 1;
    if (count <= 2)
        return true;

    double sumT = 0.0;
    double sumTT = 0.0;
    double sumX = 0.0;
    double sumY = 0.0;
    double sumTX = 0.0;
    double sumTY = 0.0;
    for (int k = start; k <= end; ++k) {
        const double t = static_cast<double>(k - start);
        sumT += t;
        sumTT += t * t;
        sumX += points.at(k).x();
        sumY += points.at(k).y();
        sumTX += t * points.at(k).x();
        sumTY += t * points.at(k).y();
    }

    const double det = static_cast<double>(count) * sumTT - sumT * sumT;
    double ax = points.at(start).x();
    double bx = 0.0;
    double ay = points.at(start).y();
    double by = 0.0;
    if (std::abs(det) > 1e-9) {
        bx = (static_cast<double>(count) * sumTX - sumT * sumX) / det;
        ax = (sumX - bx * sumT) / static_cast<double>(count);
        by = (static_cast<double>(count) * sumTY - sumT * sumY) / det;
        ay = (sumY - by * sumT) / static_cast<double>(count);
    }

    const double limit = epsilon * epsilon;
    for (int k = start; k <= end; ++k) {
        const double t = static_cast<double>(k - start);
        const double dx = points.at(k).x() - (ax + bx * t);
        const double dy = points.at(k).y() - (ay + by * t);
        if (dx * dx + dy * dy > limit)
            return false;
    }
    return true;
}

} // namespace

QVector<QPointF> readTrfFrameTranslations(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};

    const QByteArray head = file.peek(4);
    if (head.startsWith("TRF"))
        return readBinaryTrf(file);
    return readAsciiTrf(file);
}

QVector<int> piecewiseLinearBreakpoints(const QVector<QPointF> &points, double epsilon)
{
    const int n = points.size();
    QVector<int> keys;
    if (n <= 0)
        return keys;
    if (n == 1) {
        keys.append(0);
        return keys;
    }

    const double eps = qMax(0.01, epsilon);
    keys.append(0);
    int start = 0;
    while (start < n - 1) {
        int end = start + 1;
        while (end < n - 1 && residualWithinEpsilon(points, start, end + 1, eps))
            ++end;
        keys.append(end);
        start = end;
    }
    return keys;
}

StabilizePlan planStabilizeKeyframes(const QString &trfPath, TimeUs trfStartUs, const Clip &clip,
                                     double fps, double scaleX, double scaleY, int smoothing,
                                     bool tripod, double epsilonCanvasPx)
{
    StabilizePlan plan;
    const QVector<QPointF> translations = readTrfFrameTranslations(trfPath);
    if (translations.isEmpty() || fps <= 0.0)
        return plan;

    const QVector<QPointF> camera = accumulatePath(translations);
    const QVector<QPointF> compensation = compensationPath(camera, smoothing, tripod);

    QVector<QPointF> canvas;
    QVector<TimeUs> times;
    canvas.reserve(compensation.size());
    times.reserve(compensation.size());

    const double sx = scaleX == 0.0 ? 1.0 : scaleX;
    const double sy = scaleY == 0.0 ? 1.0 : scaleY;
    TimeUs lastTime = -1;
    for (int i = 0; i < compensation.size(); ++i) {
        const TimeUs sourceUs =
            trfStartUs + static_cast<TimeUs>(llround(static_cast<double>(i) * 1'000'000.0 / fps));
        if (sourceUs < clip.srcIn || sourceUs > clip.srcOut)
            continue;
        const TimeUs local = qBound(TimeUs{0}, clip.sourceUsToClipLocalUs(sourceUs),
                                    qMax(TimeUs{0}, clip.timelineDuration));
        if (local == lastTime)
            continue;
        lastTime = local;
        canvas.append(QPointF(compensation.at(i).x() * sx, compensation.at(i).y() * sy));
        times.append(local);
    }
    if (canvas.isEmpty())
        return plan;

    // The smoother is one-sided at the start of the clip, so C-S is usually a
    // large DC offset on frame 0. Subtract that so the first key sits on the
    // rest pose (clip center) instead of jumping into a corner.
    const QPointF start = canvas.first();
    for (QPointF &p : canvas)
        p -= start;

    double epsilon = qMax(0.25, epsilonCanvasPx);
    QVector<int> breaks = piecewiseLinearBreakpoints(canvas, epsilon);
    while (breaks.size() > 400 && epsilon < 32.0) {
        epsilon *= 1.5;
        breaks = piecewiseLinearBreakpoints(canvas, epsilon);
    }

    plan.keys.reserve(breaks.size());
    TimeUs prev = -1;
    for (int index : breaks) {
        if (index < 0 || index >= times.size())
            continue;
        const TimeUs t = times.at(index);
        if (t == prev)
            continue;
        prev = t;
        plan.keys.append(StabilizeKeyframe{t, canvas.at(index).x(), canvas.at(index).y()});
    }
    if (plan.keys.isEmpty())
        plan.keys.append(StabilizeKeyframe{0, canvas.first().x(), canvas.first().y()});
    if (plan.keys.first().timeUs != 0) {
        plan.keys.prepend(StabilizeKeyframe{0, plan.keys.first().dx, plan.keys.first().dy});
    }
    return plan;
}

void captureStabilizeRestPose(Clip &clip)
{
    if (clip.stabilizeHasRestPose)
        return;
    clip.stabilizeRestX = clip.transformX.isEmpty() ? 0.0 : clip.transformX.evaluateAt(0);
    clip.stabilizeRestY = clip.transformY.isEmpty() ? 0.0 : clip.transformY.evaluateAt(0);
    clip.stabilizeRestW = clip.transformW.isEmpty() ? 0.0 : clip.transformW.evaluateAt(0);
    clip.stabilizeRestH = clip.transformH.isEmpty() ? 0.0 : clip.transformH.evaluateAt(0);
    clip.stabilizeRestRot = clip.rotation.isEmpty() ? 0.0 : clip.rotation.evaluateAt(0);
    clip.stabilizeHasRestPose = true;
}

void restoreStabilizeRestPose(Clip &clip)
{
    if (!clip.stabilizeHasRestPose)
        return;
    clip.transformX = {};
    clip.transformY = {};
    clip.transformX.setKeyframe(0, clip.stabilizeRestX);
    clip.transformY.setKeyframe(0, clip.stabilizeRestY);
    if (clip.transformW.keyframes().size() <= 1)
        clip.transformW.setKeyframe(0, qMax(1.0, clip.stabilizeRestW));
    if (clip.transformH.keyframes().size() <= 1)
        clip.transformH.setKeyframe(0, qMax(1.0, clip.stabilizeRestH));
    if (clip.rotation.keyframes().size() <= 1)
        clip.rotation.setKeyframe(0, clip.stabilizeRestRot);
    clip.stabilizeHasRestPose = false;
}

void applyStabilizePlan(Clip &clip, const StabilizePlan &plan)
{
    captureStabilizeRestPose(clip);

    double maxAbs = 0.0;
    for (const StabilizeKeyframe &key : plan.keys)
        maxAbs = qMax(maxAbs, qMax(std::abs(key.dx), std::abs(key.dy)));

    const double restW = qMax(1.0, clip.stabilizeRestW);
    const double restH = qMax(1.0, clip.stabilizeRestH);
    double zoom = 1.0;
    if (clip.transformW.keyframes().size() <= 1 && clip.transformH.keyframes().size() <= 1
        && maxAbs > 0.5) {
        zoom = qBound(1.0, 1.0 + 2.0 * maxAbs / qMin(restW, restH), 1.25);
    }
    const double newW = restW * zoom;
    const double newH = restH * zoom;
    const double originX = clip.stabilizeRestX + restW * 0.5 - newW * 0.5;
    const double originY = clip.stabilizeRestY + restH * 0.5 - newH * 0.5;

    KeyframeTrack<double> x;
    KeyframeTrack<double> y;
    if (plan.keys.isEmpty()) {
        x.setKeyframe(0, originX);
        y.setKeyframe(0, originY);
    } else {
        for (const StabilizeKeyframe &key : plan.keys) {
            // path dx/dy is in unzoomed layout pixels; after we enlarge W/H
            // around the rest center, one source pixel is `zoom` times larger.
            x.setKeyframe(key.timeUs, originX + key.dx * zoom);
            y.setKeyframe(key.timeUs, originY + key.dy * zoom);
        }
    }
    clip.transformX = x;
    clip.transformY = y;
    if (clip.transformW.keyframes().size() <= 1)
        clip.transformW.setKeyframe(0, newW);
    if (clip.transformH.keyframes().size() <= 1)
        clip.transformH.setKeyframe(0, newH);
}

} // namespace drift
