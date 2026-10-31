#include "engine/tracking/MotionTracking.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace drift::tracking {
namespace {

constexpr int kMaxLkRadius = 16;
constexpr int kMaxPatternRadius = 24;
constexpr float kMinEigenPerPixel = 0.5f;
constexpr int kPlanarWindowRadius = 7;
constexpr double kPlanarFbThreshold = 1.0;
constexpr double kPlanarRansacThreshold = 2.0;

// Every tap of a window shares one fractional offset, so the four bilinear weights are computed once.
// The fast path needs the whole window (plus the +1 neighbour) inside the image.
void sampleWindow(const GrayImage &img, float cx, float cy, int r, float *out)
{
    const int n = 2 * r + 1;
    cx = std::clamp(cx, -1.0e4f, 1.0e4f);
    cy = std::clamp(cy, -1.0e4f, 1.0e4f);
    const float fx0 = cx - float(r);
    const float fy0 = cy - float(r);
    const int ix = int(std::floor(fx0));
    const int iy = int(std::floor(fy0));
    const float ax = fx0 - float(ix);
    const float ay = fy0 - float(iy);
    if (ix >= 0 && iy >= 0 && ix + n < img.width && iy + n < img.height) {
        const float w00 = (1 - ax) * (1 - ay);
        const float w10 = ax * (1 - ay);
        const float w01 = (1 - ax) * ay;
        const float w11 = ax * ay;
        for (int y = 0; y < n; ++y) {
            const float *p0 = img.data.data() + size_t(iy + y) * img.width + ix;
            const float *p1 = p0 + img.width;
            float *o = out + y * n;
            for (int x = 0; x < n; ++x)
                o[x] = w00 * p0[x] + w10 * p0[x + 1] + w01 * p1[x] + w11 * p1[x + 1];
        }
        return;
    }
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x)
            out[y * n + x] = img.sample(fx0 + float(x), fy0 + float(y));
}

float correlate(const float *a, const float *b, int count)
{
    double ma = 0, mb = 0;
    for (int i = 0; i < count; ++i) {
        ma += a[i];
        mb += b[i];
    }
    ma /= count;
    mb /= count;
    double saa = 0, sbb = 0, sab = 0;
    for (int i = 0; i < count; ++i) {
        const double da = a[i] - ma;
        const double db = b[i] - mb;
        saa += da * da;
        sbb += db * db;
        sab += da * db;
    }
    // Flat patches have no pattern to match; 1e-3 per sample is below 8-bit quantisation noise.
    if (saa < 1e-3 * count || sbb < 1e-3 * count)
        return 0.0f;
    return float(sab / std::sqrt(saa * sbb));
}

GrayImage downsample(const GrayImage &src)
{
    GrayImage dst;
    dst.width = std::max(1, src.width / 2);
    dst.height = std::max(1, src.height / 2);
    dst.data.resize(size_t(dst.width) * dst.height);
    static const float k[5] = {1 / 16.f, 4 / 16.f, 6 / 16.f, 4 / 16.f, 1 / 16.f};
    // Horizontal pass only for the columns that survive, vertical pass for the rows that survive.
    std::vector<float> tmp(size_t(dst.width) * src.height);
    for (int y = 0; y < src.height; ++y) {
        const float *row = src.data.data() + size_t(y) * src.width;
        for (int x = 0; x < dst.width; ++x) {
            float s = 0;
            for (int t = -2; t <= 2; ++t)
                s += k[t + 2] * row[std::clamp(2 * x + t, 0, src.width - 1)];
            tmp[size_t(y) * dst.width + x] = s;
        }
    }
    for (int y = 0; y < dst.height; ++y) {
        for (int x = 0; x < dst.width; ++x) {
            float s = 0;
            for (int t = -2; t <= 2; ++t)
                s += k[t + 2] * tmp[size_t(std::clamp(2 * y + t, 0, src.height - 1)) * dst.width + x];
            dst.data[size_t(y) * dst.width + x] = s;
        }
    }
    return dst;
}

void scharr(const GrayImage &src, GrayImage *gx, GrayImage *gy)
{
    const int w = src.width, h = src.height;
    gx->width = gy->width = w;
    gx->height = gy->height = h;
    gx->data.assign(size_t(w) * h, 0.0f);
    gy->data.assign(size_t(w) * h, 0.0f);
    for (int y = 0; y < h; ++y) {
        const float *r0 = src.data.data() + size_t(std::max(y - 1, 0)) * w;
        const float *r1 = src.data.data() + size_t(y) * w;
        const float *r2 = src.data.data() + size_t(std::min(y + 1, h - 1)) * w;
        for (int x = 0; x < w; ++x) {
            const int xl = std::max(x - 1, 0), xr = std::min(x + 1, w - 1);
            const float dx = 3 * (r0[xr] - r0[xl]) + 10 * (r1[xr] - r1[xl]) + 3 * (r2[xr] - r2[xl]);
            const float dy = 3 * (r2[xl] - r0[xl]) + 10 * (r2[x] - r0[x]) + 3 * (r2[xr] - r0[xr]);
            // Border taps are clamped, so the span is 1 px there instead of 2.
            const float sx = (xr - xl) == 2 ? 1 / 32.f : (xr - xl) == 1 ? 1 / 16.f : 0.f;
            const int yu = std::max(y - 1, 0), yd = std::min(y + 1, h - 1);
            const float sy = (yd - yu) == 2 ? 1 / 32.f : (yd - yu) == 1 ? 1 / 16.f : 0.f;
            gx->data[size_t(y) * w + x] = dx * sx;
            gy->data[size_t(y) * w + x] = dy * sy;
        }
    }
}

using Mat3 = std::array<double, 9>;

Mat3 mul3(const Mat3 &a, const Mat3 &b)
{
    Mat3 r{};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            for (int k = 0; k < 3; ++k)
                r[i * 3 + j] += a[i * 3 + k] * b[k * 3 + j];
    return r;
}

bool inv3(const Mat3 &m, Mat3 *out)
{
    const double c00 = m[4] * m[8] - m[5] * m[7];
    const double c01 = m[5] * m[6] - m[3] * m[8];
    const double c02 = m[3] * m[7] - m[4] * m[6];
    const double det = m[0] * c00 + m[1] * c01 + m[2] * c02;
    if (!std::isfinite(det) || std::abs(det) < 1e-18)
        return false;
    const double id = 1.0 / det;
    *out = {c00 * id, (m[2] * m[7] - m[1] * m[8]) * id, (m[1] * m[5] - m[2] * m[4]) * id,
            c01 * id, (m[0] * m[8] - m[2] * m[6]) * id, (m[2] * m[3] - m[0] * m[5]) * id,
            c02 * id, (m[1] * m[6] - m[0] * m[7]) * id, (m[0] * m[4] - m[1] * m[3]) * id};
    return true;
}

// Cyclic Jacobi on a symmetric NxN matrix. Eigenvalues ascending in `values`, eigenvectors as columns of `vectors`.
template<int N>
void jacobiEigen(std::array<double, N * N> a, std::array<double, N> *values, std::array<double, N * N> *vectors)
{
    std::array<double, N * N> v{};
    for (int i = 0; i < N; ++i)
        v[i * N + i] = 1.0;
    for (int sweep = 0; sweep < 60; ++sweep) {
        double off = 0;
        for (int i = 0; i < N; ++i)
            for (int j = i + 1; j < N; ++j)
                off += a[i * N + j] * a[i * N + j];
        if (off < 1e-26)
            break;
        for (int p = 0; p < N; ++p) {
            for (int q = p + 1; q < N; ++q) {
                const double apq = a[p * N + q];
                if (std::abs(apq) < 1e-300)
                    continue;
                const double theta = (a[q * N + q] - a[p * N + p]) / (2 * apq);
                const double t = (theta >= 0 ? 1.0 : -1.0) / (std::abs(theta) + std::sqrt(theta * theta + 1));
                const double c = 1 / std::sqrt(t * t + 1);
                const double s = t * c;
                for (int k = 0; k < N; ++k) {
                    const double akp = a[k * N + p], akq = a[k * N + q];
                    a[k * N + p] = c * akp - s * akq;
                    a[k * N + q] = s * akp + c * akq;
                }
                for (int k = 0; k < N; ++k) {
                    const double apk = a[p * N + k], aqk = a[q * N + k];
                    a[p * N + k] = c * apk - s * aqk;
                    a[q * N + k] = s * apk + c * aqk;
                }
                for (int k = 0; k < N; ++k) {
                    const double vkp = v[k * N + p], vkq = v[k * N + q];
                    v[k * N + p] = c * vkp - s * vkq;
                    v[k * N + q] = s * vkp + c * vkq;
                }
            }
        }
    }
    std::array<int, N> order;
    for (int i = 0; i < N; ++i)
        order[i] = i;
    std::sort(order.begin(), order.end(), [&](int x, int y) { return a[x * N + x] < a[y * N + y]; });
    for (int i = 0; i < N; ++i) {
        (*values)[i] = a[order[i] * N + order[i]];
        for (int k = 0; k < N; ++k)
            (*vectors)[k * N + i] = v[k * N + order[i]];
    }
}

// Solves a 3x3 system by Gauss-Jordan with partial pivoting.
bool solve3(Mat3 a, std::array<double, 3> b, std::array<double, 3> *x)
{
    for (int c = 0; c < 3; ++c) {
        int piv = c;
        for (int r = c + 1; r < 3; ++r)
            if (std::abs(a[r * 3 + c]) > std::abs(a[piv * 3 + c]))
                piv = r;
        if (std::abs(a[piv * 3 + c]) < 1e-12)
            return false;
        if (piv != c) {
            for (int k = 0; k < 3; ++k)
                std::swap(a[c * 3 + k], a[piv * 3 + k]);
            std::swap(b[c], b[piv]);
        }
        for (int r = 0; r < 3; ++r) {
            if (r == c)
                continue;
            const double f = a[r * 3 + c] / a[c * 3 + c];
            for (int k = 0; k < 3; ++k)
                a[r * 3 + k] -= f * a[c * 3 + k];
            b[r] -= f * b[c];
        }
    }
    for (int i = 0; i < 3; ++i)
        (*x)[i] = b[i] / a[i * 3 + i];
    return true;
}

Mat3 normalizer(const QVector<QPointF> &pts)
{
    double cx = 0, cy = 0;
    for (const QPointF &p : pts) {
        cx += p.x();
        cy += p.y();
    }
    cx /= pts.size();
    cy /= pts.size();
    double d = 0;
    for (const QPointF &p : pts)
        d += std::hypot(p.x() - cx, p.y() - cy);
    d /= pts.size();
    const double s = d > 1e-12 ? std::sqrt(2.0) / d : 1.0;
    return {s, 0, -s * cx, 0, s, -s * cy, 0, 0, 1};
}

bool sameSize(const QVector<QPointF> &a, const QVector<QPointF> &b, int minimum)
{
    return a.size() == b.size() && a.size() >= minimum;
}

} // namespace

float GrayImage::at(int x, int y) const
{
    x = std::clamp(x, 0, width - 1);
    y = std::clamp(y, 0, height - 1);
    return data[size_t(y) * width + x];
}

float GrayImage::sample(float x, float y) const
{
    if (isEmpty())
        return 0.0f;
    x = std::clamp(x, 0.0f, float(width - 1));
    y = std::clamp(y, 0.0f, float(height - 1));
    const int x0 = int(x), y0 = int(y);
    const int x1 = std::min(x0 + 1, width - 1), y1 = std::min(y0 + 1, height - 1);
    const float ax = x - float(x0), ay = y - float(y0);
    const float *d = data.data();
    const float top = d[size_t(y0) * width + x0] * (1 - ax) + d[size_t(y0) * width + x1] * ax;
    const float bot = d[size_t(y1) * width + x0] * (1 - ax) + d[size_t(y1) * width + x1] * ax;
    return top * (1 - ay) + bot * ay;
}

GrayImage toGray(const QImage &image, int maxWidth, double *scale)
{
    GrayImage out;
    if (scale)
        *scale = 1.0;
    if (image.isNull())
        return out;
    QImage src = image;
    if (maxWidth > 0 && src.width() > maxWidth) {
        src = src.scaledToWidth(maxWidth, Qt::SmoothTransformation);
        if (scale)
            *scale = double(src.width()) / image.width();
    }
    const QImage gray = src.convertToFormat(QImage::Format_Grayscale8);
    out.width = gray.width();
    out.height = gray.height();
    out.data.resize(size_t(out.width) * out.height);
    for (int y = 0; y < out.height; ++y) {
        const uchar *line = gray.constScanLine(y);
        float *o = out.data.data() + size_t(y) * out.width;
        for (int x = 0; x < out.width; ++x)
            o[x] = float(line[x]);
    }
    return out;
}

Pyramid buildPyramid(const GrayImage &level0, int levels)
{
    Pyramid p;
    if (level0.isEmpty())
        return p;
    p.levels.push_back(level0);
    for (int i = 1; i < levels; ++i) {
        const GrayImage &top = p.levels.back();
        if (top.width < 16 || top.height < 16)
            break;
        p.levels.push_back(downsample(top));
    }
    p.gradX.resize(p.levels.size());
    p.gradY.resize(p.levels.size());
    for (size_t i = 0; i < p.levels.size(); ++i)
        scharr(p.levels[i], &p.gradX[i], &p.gradY[i]);
    return p;
}

LkResult trackPointLk(const Pyramid &prev, const Pyramid &next, QPointF point, int windowRadius,
                      int maxIterations, float epsilon)
{
    LkResult res;
    res.position = point;
    const int levelCount = int(std::min(prev.levels.size(), next.levels.size()));
    if (levelCount == 0 || !std::isfinite(point.x()) || !std::isfinite(point.y()))
        return res;
    const int r = std::clamp(windowRadius, 2, kMaxLkRadius);
    const int n = 2 * r + 1;
    constexpr int kMax = (2 * kMaxLkRadius + 1) * (2 * kMaxLkRadius + 1);
    float tpl[kMax], tx[kMax], ty[kMax], cur[kMax];

    float gx = 0, gy = 0;
    float residual = 0;
    for (int level = levelCount - 1; level >= 0; --level) {
        const float scale = 1.0f / float(1 << level);
        const float px = float(point.x()) * scale;
        const float py = float(point.y()) * scale;
        sampleWindow(prev.levels[level], px, py, r, tpl);
        sampleWindow(prev.gradX[level], px, py, r, tx);
        sampleWindow(prev.gradY[level], px, py, r, ty);
        // Window taps off the image read clamped border pixels that do not move with the scene; on
        // coarse levels they bias the step enough to send every finer level the wrong way.
        // Zero gradients drop them from the solve.
        const GrayImage &lv = prev.levels[level];
        int inside = 0;
        for (int y = 0; y < n; ++y) {
            const float sy = py - float(r) + float(y);
            for (int x = 0; x < n; ++x) {
                const float sx = px - float(r) + float(x);
                if (sx >= 1 && sy >= 1 && sx <= float(lv.width - 2) && sy <= float(lv.height - 2)) {
                    ++inside;
                } else {
                    tx[y * n + x] = 0;
                    ty[y * n + x] = 0;
                }
            }
        }
        double sxx = 0, sxy = 0, syy = 0;
        for (int i = 0; i < n * n; ++i) {
            sxx += tx[i] * tx[i];
            sxy += tx[i] * ty[i];
            syy += ty[i] * ty[i];
        }
        const double det = sxx * syy - sxy * sxy;
        const double minEig = 0.5 * (sxx + syy - std::sqrt((sxx - syy) * (sxx - syy) + 4 * sxy * sxy)) / std::max(1, inside);
        if (minEig < kMinEigenPerPixel || det <= 0) {
            if (level == 0)
                return res;
            gx *= 2;
            gy *= 2;
            continue;
        }
        float vx = 0, vy = 0;
        for (int it = 0; it < std::max(1, maxIterations); ++it) {
            sampleWindow(next.levels[level], px + gx + vx, py + gy + vy, r, cur);
            double bx = 0, by = 0;
            for (int i = 0; i < n * n; ++i) {
                const float e = tpl[i] - cur[i];
                bx += tx[i] * e;
                by += ty[i] * e;
            }
            const float dx = float((syy * bx - sxy * by) / det);
            const float dy = float((sxx * by - sxy * bx) / det);
            if (!std::isfinite(dx) || !std::isfinite(dy))
                return res;
            vx += dx;
            vy += dy;
            if (dx * dx + dy * dy < epsilon * epsilon)
                break;
        }
        // A step beyond the window means the linearisation was lost (coarse levels of fine noise);
        // keeping the inherited guess is safer than following it.
        if (vx * vx + vy * vy > float(r * r)) {
            if (level == 0)
                return res;
            vx = vy = 0;
        }
        gx += vx;
        gy += vy;
        if (level == 0) {
            sampleWindow(next.levels[0], px + gx, py + gy, r, cur);
            for (int i = 0; i < n * n; ++i)
                residual += std::abs(tpl[i] - cur[i]);
            residual /= float(n * n);
        } else {
            gx *= 2;
            gy *= 2;
        }
    }
    res.position = QPointF(point.x() + gx, point.y() + gy);
    const GrayImage &l0 = next.levels[0];
    res.ok = res.position.x() >= 0 && res.position.y() >= 0 && res.position.x() <= l0.width - 1
        && res.position.y() <= l0.height - 1;
    res.residual = residual;
    return res;
}

float ncc(const GrayImage &imgA, QPointF a, const GrayImage &imgB, QPointF b, int radius)
{
    const int r = std::clamp(radius, 1, kMaxPatternRadius);
    const int n = 2 * r + 1;
    constexpr int kMax = (2 * kMaxPatternRadius + 1) * (2 * kMaxPatternRadius + 1);
    float pa[kMax], pb[kMax];
    sampleWindow(imgA, float(a.x()), float(a.y()), r, pa);
    sampleWindow(imgB, float(b.x()), float(b.y()), r, pb);
    return correlate(pa, pb, n * n);
}

PointTrackStep trackPointStep(const Pyramid &prev, const Pyramid &next, QPointF prevPos,
                              const GrayImage &referenceFrame, QPointF referencePos, int patternRadius)
{
    PointTrackStep step;
    step.position = prevPos;
    step.lost = true;
    if (next.levels.empty())
        return step;
    const LkResult fwd = trackPointLk(prev, next, prevPos);
    if (!fwd.ok)
        return step;
    const LkResult bwd = trackPointLk(next, prev, fwd.position);
    const double fb = bwd.ok ? std::hypot(bwd.position.x() - prevPos.x(), bwd.position.y() - prevPos.y()) : 4.0;

    const int r = std::clamp(patternRadius, 3, kMaxPatternRadius);
    const int n = 2 * r + 1;
    constexpr int kMax = (2 * kMaxPatternRadius + 1) * (2 * kMaxPatternRadius + 1);
    float ref[kMax], cur[kMax];
    sampleWindow(referenceFrame, float(referencePos.x()), float(referencePos.y()), r, ref);
    const GrayImage &img = next.levels[0];
    auto score = [&](QPointF p) {
        sampleWindow(img, float(p.x()), float(p.y()), r, cur);
        return correlate(ref, cur, n * n);
    };

    QPointF pos = fwd.position;
    float best = score(pos);
    if (best < 0.9f) {
        const QPointF centre = pos;
        for (int pass = 0; pass < 2; ++pass) {
            const QPointF c = pass == 0 ? centre : pos;
            const int span = pass == 0 ? 2 : 1;
            const double stepPx = pass == 0 ? 1.0 : 0.5;
            for (int dy = -span; dy <= span; ++dy) {
                for (int dx = -span; dx <= span; ++dx) {
                    if (!dx && !dy)
                        continue;
                    const QPointF cand(c.x() + dx * stepPx, c.y() + dy * stepPx);
                    const float s = score(cand);
                    if (s > best) {
                        best = s;
                        pos = cand;
                    }
                }
            }
        }
    }

    const float nccTerm = std::clamp((best - 0.5f) / 0.5f, 0.0f, 1.0f);
    const float fbTerm = std::clamp(1.0f - float(fb) / 2.0f, 0.0f, 1.0f);
    step.position = pos;
    step.confidence = nccTerm * fbTerm;
    const bool inside = pos.x() >= 0 && pos.y() >= 0 && pos.x() <= img.width - 1 && pos.y() <= img.height - 1;
    step.lost = !inside || step.confidence < 0.2f;
    return step;
}

bool fitTranslation(const QVector<QPointF> &src, const QVector<QPointF> &dst, QTransform *out)
{
    if (!sameSize(src, dst, 1))
        return false;
    double dx = 0, dy = 0;
    for (int i = 0; i < src.size(); ++i) {
        dx += dst[i].x() - src[i].x();
        dy += dst[i].y() - src[i].y();
    }
    *out = QTransform::fromTranslate(dx / src.size(), dy / src.size());
    return true;
}

bool fitSimilarity(const QVector<QPointF> &src, const QVector<QPointF> &dst, bool allowScale,
                   bool allowRotation, QTransform *out)
{
    if (!allowScale && !allowRotation)
        return fitTranslation(src, dst, out);
    if (!sameSize(src, dst, 2))
        return false;
    const int n = src.size();
    QPointF cs, cd;
    for (int i = 0; i < n; ++i) {
        cs += src[i];
        cd += dst[i];
    }
    cs /= n;
    cd /= n;
    double dot = 0, cross = 0, var = 0;
    for (int i = 0; i < n; ++i) {
        const QPointF s = src[i] - cs, d = dst[i] - cd;
        dot += s.x() * d.x() + s.y() * d.y();
        cross += s.x() * d.y() - s.y() * d.x();
        var += s.x() * s.x() + s.y() * s.y();
    }
    if (var < 1e-12)
        return false;
    double c = 1, s = 0, scale = 1;
    if (allowRotation) {
        const double mag = std::hypot(dot, cross);
        if (mag < 1e-12)
            return false;
        c = dot / mag;
        s = cross / mag;
        if (allowScale)
            scale = mag / var;
    } else {
        scale = dot / var;
        if (scale <= 1e-9)
            return false;
    }
    const double a = scale * c, b = scale * s;
    const double tx = cd.x() - (a * cs.x() - b * cs.y());
    const double ty = cd.y() - (b * cs.x() + a * cs.y());
    *out = QTransform(a, b, -b, a, tx, ty);
    return true;
}

bool fitAffine(const QVector<QPointF> &src, const QVector<QPointF> &dst, QTransform *out)
{
    if (!sameSize(src, dst, 3))
        return false;
    const Mat3 ts = normalizer(src);
    const Mat3 td = normalizer(dst);
    Mat3 ata{};
    std::array<double, 3> bx{}, by{};
    for (int i = 0; i < src.size(); ++i) {
        const double row[3] = {ts[0] * src[i].x() + ts[2], ts[4] * src[i].y() + ts[5], 1.0};
        const double u = td[0] * dst[i].x() + td[2];
        const double v = td[4] * dst[i].y() + td[5];
        for (int a = 0; a < 3; ++a) {
            for (int b = 0; b < 3; ++b)
                ata[a * 3 + b] += row[a] * row[b];
            bx[a] += row[a] * u;
            by[a] += row[a] * v;
        }
    }
    std::array<double, 3> px, py;
    if (!solve3(ata, bx, &px) || !solve3(ata, by, &py))
        return false;
    // normalised affine A_n as a 3x3, then undo both normalisations
    Mat3 an = {px[0], px[1], px[2], py[0], py[1], py[2], 0, 0, 1};
    Mat3 tdInv;
    if (!inv3(td, &tdInv))
        return false;
    const Mat3 m = mul3(tdInv, mul3(an, ts));
    *out = QTransform(m[0], m[3], m[1], m[4], m[2], m[5]);
    return true;
}

Homography identityHomography()
{
    return {1, 0, 0, 0, 1, 0, 0, 0, 1};
}

bool fitHomography(const QVector<QPointF> &src, const QVector<QPointF> &dst, Homography *h)
{
    if (!sameSize(src, dst, 4))
        return false;
    const Mat3 ts = normalizer(src);
    const Mat3 td = normalizer(dst);
    std::array<double, 81> ata{};
    for (int i = 0; i < src.size(); ++i) {
        const double x = ts[0] * src[i].x() + ts[2], y = ts[4] * src[i].y() + ts[5];
        const double u = td[0] * dst[i].x() + td[2], v = td[4] * dst[i].y() + td[5];
        const double r1[9] = {-x, -y, -1, 0, 0, 0, u * x, u * y, u};
        const double r2[9] = {0, 0, 0, -x, -y, -1, v * x, v * y, v};
        for (int a = 0; a < 9; ++a)
            for (int b = 0; b < 9; ++b)
                ata[a * 9 + b] += r1[a] * r1[b] + r2[a] * r2[b];
    }
    std::array<double, 9> values;
    std::array<double, 81> vectors;
    jacobiEigen<9>(ata, &values, &vectors);
    // A second near-zero eigenvalue means the points do not constrain the model (collinear, repeated).
    if (!(values[1] > 1e-9 * std::max(values[8], 1e-300)))
        return false;
    Mat3 hn;
    for (int i = 0; i < 9; ++i)
        hn[i] = vectors[i * 9];
    Mat3 tdInv;
    if (!inv3(td, &tdInv))
        return false;
    Mat3 m = mul3(tdInv, mul3(hn, ts));
    if (std::abs(m[8]) > 1e-12) {
        for (double &e : m)
            e /= m[8];
    }
    for (double e : m)
        if (!std::isfinite(e))
            return false;
    *h = m;
    return true;
}

QPointF applyHomography(const Homography &h, QPointF p)
{
    const double w = h[6] * p.x() + h[7] * p.y() + h[8];
    if (std::abs(w) < 1e-12)
        return p;
    return QPointF((h[0] * p.x() + h[1] * p.y() + h[2]) / w, (h[3] * p.x() + h[4] * p.y() + h[5]) / w);
}

QPolygonF mapPolygon(const Homography &h, const QPolygonF &polygon)
{
    QPolygonF out;
    out.reserve(polygon.size());
    for (const QPointF &p : polygon)
        out.append(applyHomography(h, p));
    return out;
}

Homography composeHomography(const Homography &second, const Homography &first)
{
    return mul3(second, first);
}

bool invertHomography(const Homography &h, Homography *out)
{
    Mat3 inv;
    if (!inv3(h, &inv))
        return false;
    if (std::abs(inv[8]) > 1e-12) {
        for (double &e : inv)
            e /= inv[8];
    }
    *out = inv;
    return true;
}

bool ransacHomography(const QVector<QPointF> &src, const QVector<QPointF> &dst, double thresholdPx,
                      int iterations, Homography *h, QVector<bool> *inliers)
{
    const int n = src.size();
    if (!sameSize(src, dst, 4))
        return false;
    const double thr2 = thresholdPx * thresholdPx;
    auto countInliers = [&](const Homography &m, QVector<bool> *mask) {
        int count = 0;
        for (int i = 0; i < n; ++i) {
            const QPointF q = applyHomography(m, src[i]);
            const double dx = q.x() - dst[i].x(), dy = q.y() - dst[i].y();
            const bool in = std::isfinite(dx) && std::isfinite(dy) && dx * dx + dy * dy < thr2;
            if (mask)
                (*mask)[i] = in;
            count += in;
        }
        return count;
    };

    std::mt19937 rng(0x5eed1234u);
    std::uniform_int_distribution<int> pick(0, n - 1);
    Homography best{};
    int bestCount = 0;
    int limit = std::max(1, iterations);
    QVector<QPointF> s(4), d(4);
    for (int it = 0; it < limit; ++it) {
        int idx[4];
        for (int k = 0; k < 4;) {
            const int c = pick(rng);
            bool dup = false;
            for (int j = 0; j < k; ++j)
                dup |= idx[j] == c;
            if (!dup)
                idx[k++] = c;
        }
        for (int k = 0; k < 4; ++k) {
            s[k] = src[idx[k]];
            d[k] = dst[idx[k]];
        }
        Homography m;
        if (!fitHomography(s, d, &m))
            continue;
        const int count = countInliers(m, nullptr);
        if (count > bestCount) {
            bestCount = count;
            best = m;
            const double w = double(count) / n;
            const double denom = std::log(std::max(1.0 - w * w * w * w, 1e-12));
            if (w > 0 && denom < 0)
                limit = std::min(limit, std::max(it + 1, int(std::ceil(std::log(0.001) / denom))));
        }
    }
    if (bestCount < 4)
        return false;

    QVector<bool> mask(n);
    countInliers(best, &mask);
    for (int pass = 0; pass < 3; ++pass) {
        QVector<QPointF> is, id;
        for (int i = 0; i < n; ++i) {
            if (mask[i]) {
                is.append(src[i]);
                id.append(dst[i]);
            }
        }
        Homography refit;
        if (!fitHomography(is, id, &refit))
            break;
        QVector<bool> next(n);
        const int count = countInliers(refit, &next);
        if (count < bestCount && pass > 0)
            break;
        best = refit;
        bestCount = count;
        mask = next;
    }
    if (bestCount < 4)
        return false;
    *h = best;
    if (inliers)
        *inliers = mask;
    return true;
}

QVector<QPointF> goodFeatures(const Pyramid &pyramid, const QPolygonF &region, int maxCorners,
                              double qualityLevel, double minDistance)
{
    QVector<QPointF> result;
    if (pyramid.levels.empty() || maxCorners <= 0)
        return result;
    const GrayImage &gx = pyramid.gradX[0];
    const GrayImage &gy = pyramid.gradY[0];
    const int w = gx.width, h = gx.height;
    constexpr int kMargin = 2;
    QRect box(0, 0, w, h);
    if (!region.isEmpty())
        box &= region.boundingRect().toAlignedRect();
    box &= QRect(kMargin, kMargin, w - 2 * kMargin, h - 2 * kMargin);
    if (box.isEmpty())
        return result;

    // The 3x3 window sums are taken over a one-pixel ring around the box so that local-maximum
    // tests at the box edge have neighbours.
    const int x0 = std::max(box.left() - 1, 1), y0 = std::max(box.top() - 1, 1);
    const int x1 = std::min(box.right() + 1, w - 2), y1 = std::min(box.bottom() + 1, h - 2);
    const int bw = x1 - x0 + 1, bh = y1 - y0 + 1;
    std::vector<float> eig(size_t(bw) * bh, 0.0f);
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            double sxx = 0, sxy = 0, syy = 0;
            for (int j = -1; j <= 1; ++j) {
                const float *px = gx.data.data() + size_t(y + j) * w + x - 1;
                const float *py = gy.data.data() + size_t(y + j) * w + x - 1;
                for (int i = 0; i < 3; ++i) {
                    sxx += px[i] * px[i];
                    sxy += px[i] * py[i];
                    syy += py[i] * py[i];
                }
            }
            eig[size_t(y - y0) * bw + (x - x0)] =
                float(0.5 * (sxx + syy - std::sqrt((sxx - syy) * (sxx - syy) + 4 * sxy * sxy)));
        }
    }

    struct Candidate { float score; int x, y; };
    std::vector<Candidate> cands;
    float strongest = 0;
    for (int y = box.top(); y <= box.bottom(); ++y) {
        for (int x = box.left(); x <= box.right(); ++x) {
            const float e = eig[size_t(y - y0) * bw + (x - x0)];
            if (e <= 0)
                continue;
            bool isMax = true;
            for (int j = -1; j <= 1 && isMax; ++j)
                for (int i = -1; i <= 1; ++i)
                    if ((i || j) && eig[size_t(y + j - y0) * bw + (x + i - x0)] > e) {
                        isMax = false;
                        break;
                    }
            if (!isMax)
                continue;
            if (!region.isEmpty() && !region.containsPoint(QPointF(x, y), Qt::OddEvenFill))
                continue;
            cands.push_back({e, x, y});
            strongest = std::max(strongest, e);
        }
    }
    const float cut = float(qualityLevel) * strongest;
    std::sort(cands.begin(), cands.end(), [](const Candidate &a, const Candidate &b) { return a.score > b.score; });
    const double md2 = minDistance * minDistance;
    for (const Candidate &c : cands) {
        if (c.score < cut || result.size() >= maxCorners)
            break;
        bool clear = true;
        for (const QPointF &q : result) {
            const double dx = q.x() - c.x, dy = q.y() - c.y;
            if (dx * dx + dy * dy < md2) {
                clear = false;
                break;
            }
        }
        if (clear)
            result.append(QPointF(c.x, c.y));
    }
    return result;
}

PlanarStep trackPlanarStep(const Pyramid &prev, const Pyramid &next, const QVector<QPointF> &features,
                           const Homography &referenceToPrev)
{
    PlanarStep step;
    QVector<QPointF> from, to;
    from.reserve(features.size());
    to.reserve(features.size());
    for (const QPointF &f : features) {
        const LkResult fwd = trackPointLk(prev, next, f, kPlanarWindowRadius);
        if (!fwd.ok)
            continue;
        const LkResult bwd = trackPointLk(next, prev, fwd.position, kPlanarWindowRadius);
        if (!bwd.ok || std::hypot(bwd.position.x() - f.x(), bwd.position.y() - f.y()) > kPlanarFbThreshold)
            continue;
        from.append(f);
        to.append(fwd.position);
    }
    if (from.size() < 8)
        return step;
    Homography frame;
    QVector<bool> mask;
    if (!ransacHomography(from, to, kPlanarRansacThreshold, 200, &frame, &mask))
        return step;
    for (int i = 0; i < to.size(); ++i)
        if (mask[i])
            step.features.append(to[i]);
    step.inlierRatio = double(step.features.size()) / from.size();
    step.referenceToNext = composeHomography(frame, referenceToPrev);
    if (std::abs(step.referenceToNext[8]) > 1e-12) {
        const double s = step.referenceToNext[8];
        for (double &e : step.referenceToNext)
            e /= s;
    }
    step.ok = step.features.size() >= 8;
    return step;
}

} // namespace drift::tracking
