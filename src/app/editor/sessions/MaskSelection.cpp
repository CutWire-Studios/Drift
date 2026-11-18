#include "app/editor/sessions/MaskSelection.h"

#include <QPainter>
#include <QPolygonF>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <queue>

namespace drift::masking {

void fillPolygon(QImage &image, const QVector<QPointF> &pixelPolygon, bool erase, double opacity)
{
    if (image.isNull() || pixelPolygon.size() < 3)
        return;
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setOpacity(std::clamp(opacity, 0.0, 1.0));
    painter.setBrush(erase ? QColor(0, 0, 0) : QColor(255, 255, 255));
    painter.drawPolygon(QPolygonF(pixelPolygon));
}

void applyCover(QImage &image, const QImage &cover, bool erase)
{
    if (image.isNull() || cover.isNull())
        return;
    QImage c = cover.format() == QImage::Format_Grayscale8 ? cover : cover.convertToFormat(QImage::Format_Grayscale8);
    if (c.size() != image.size())
        c = c.scaled(image.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    const int target = erase ? 0 : 255;
    for (int y = 0; y < image.height(); ++y) {
        uchar *dst = image.scanLine(y);
        const uchar *src = c.constScanLine(y);
        for (int x = 0; x < image.width(); ++x) {
            if (src[x] == 0)
                continue;
            dst[x] = uchar(dst[x] + (target - int(dst[x])) * int(src[x]) / 255);
        }
    }
}

namespace {

double polygonArea(const QVector<QPointF> &poly)
{
    double sum = 0.0;
    for (int i = 0; i < poly.size(); ++i) {
        const QPointF &a = poly.at(i);
        const QPointF &b = poly.at((i + 1) % poly.size());
        sum += a.x() * b.y() - b.x() * a.y();
    }
    return std::abs(sum) / 2.0;
}

bool segmentsCross(const QPointF &a, const QPointF &b, const QPointF &c, const QPointF &d, QPointF *at)
{
    const QPointF r = b - a;
    const QPointF s = d - c;
    const double denom = r.x() * s.y() - r.y() * s.x();
    if (std::abs(denom) < 1e-12)
        return false;
    const QPointF ca = c - a;
    const double t = (ca.x() * s.y() - ca.y() * s.x()) / denom;
    const double u = (ca.x() * r.y() - ca.y() * r.x()) / denom;
    if (t < 0.0 || t > 1.0 || u < 0.0 || u > 1.0)
        return false;
    *at = a + r * t;
    return true;
}

} // namespace

QVector<QVector<QPointF>> findStrokeLoops(const QVector<QPointF> &pixelPoints, double closeTolerance, double minArea)
{
    QVector<QVector<QPointF>> loops;
    QVector<QPointF> path;
    for (const QPointF &p : pixelPoints) {
        if (!path.isEmpty()) {
            const QPointF a = path.constLast();
            for (int j = 0; j + 2 < path.size(); ++j) {
                QPointF at;
                if (!segmentsCross(path.at(j), path.at(j + 1), a, p, &at))
                    continue;
                QVector<QPointF> loop;
                loop.append(at);
                for (int k = j + 1; k < path.size(); ++k)
                    loop.append(path.at(k));
                if (polygonArea(loop) >= minArea)
                    loops.append(loop);
                path.resize(j + 1);
                path.append(at);
                break;
            }
        }
        path.append(p);
    }
    if (path.size() >= 3) {
        const QPointF d = path.constLast() - path.constFirst();
        if (std::hypot(d.x(), d.y()) <= closeTolerance && polygonArea(path) >= minArea)
            loops.append(path);
    }
    return loops;
}

namespace {

constexpr float kWeightZero = 0.43f;
constexpr float kWeightGradient = 0.14f;
constexpr float kWeightDirection = 0.43f;
constexpr double kPi = 3.14159265358979323846;

} // namespace

EdgeMap::EdgeMap(const QImage &source)
{
    if (source.isNull())
        return;
    const QImage gray = source.convertToFormat(QImage::Format_Grayscale8);
    const int w = gray.width();
    const int h = gray.height();
    if (w < 3 || h < 3)
        return;
    m_size = gray.size();
    const int n = w * h;
    auto at = [&gray](int x, int y) -> float {
        return gray.constScanLine(y)[x];
    };
    auto sample = [&](int x, int y) { return at(std::clamp(x, 0, w - 1), std::clamp(y, 0, h - 1)); };

    QVector<float> gx(n), gy(n), lap(n);
    m_grad.resize(n);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int i = y * w + x;
            gx[i] = (sample(x + 1, y - 1) + 2 * sample(x + 1, y) + sample(x + 1, y + 1)
                     - sample(x - 1, y - 1) - 2 * sample(x - 1, y) - sample(x - 1, y + 1)) / 8.0f;
            gy[i] = (sample(x - 1, y + 1) + 2 * sample(x, y + 1) + sample(x + 1, y + 1)
                     - sample(x - 1, y - 1) - 2 * sample(x, y - 1) - sample(x + 1, y - 1)) / 8.0f;
            m_grad[i] = std::hypot(gx[i], gy[i]);
            lap[i] = sample(x - 1, y) + sample(x + 1, y) + sample(x, y - 1) + sample(x, y + 1) - 4 * sample(x, y);
        }
    }

    QVector<float> sorted = m_grad;
    const int top = std::min(n - 1, int(n * 0.995));
    std::nth_element(sorted.begin(), sorted.begin() + top, sorted.end());
    const float gmax = std::max(sorted.at(top), 1.0f);

    m_zero.resize(n);
    m_dirX.resize(n);
    m_dirY.resize(n);
    constexpr float kMinStep = 4.0f;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int i = y * w + x;
            bool crossing = false;
            const int nbX[4] = {x + 1, x - 1, x, x};
            const int nbY[4] = {y, y, y + 1, y - 1};
            for (int k = 0; k < 4 && !crossing; ++k) {
                if (nbX[k] < 0 || nbX[k] >= w || nbY[k] < 0 || nbY[k] >= h)
                    continue;
                const float q = lap[nbY[k] * w + nbX[k]];
                crossing = ((lap[i] < 0) != (q < 0) || lap[i] == 0) && std::abs(lap[i] - q) > kMinStep
                           && std::abs(lap[i]) <= std::abs(q);
            }
            m_zero[i] = crossing ? 0.0f : 1.0f;
            const float g = m_grad[i];
            // The edge runs perpendicular to the gradient.
            m_dirX[i] = g > 1e-3f ? gy[i] / g : 0.0f;
            m_dirY[i] = g > 1e-3f ? -gx[i] / g : 0.0f;
            m_grad[i] = 1.0f - std::min(1.0f, g / gmax);
        }
    }
}

QPoint EdgeMap::clamped(QPoint p) const
{
    return QPoint(std::clamp(p.x(), 0, m_size.width() - 1), std::clamp(p.y(), 0, m_size.height() - 1));
}

QPoint EdgeMap::snap(QPoint p, int radius) const
{
    if (isNull())
        return p;
    p = clamped(p);
    QPoint best = p;
    float bestCost = std::numeric_limits<float>::max();
    for (int y = std::max(0, p.y() - radius); y <= std::min(m_size.height() - 1, p.y() + radius); ++y) {
        for (int x = std::max(0, p.x() - radius); x <= std::min(m_size.width() - 1, p.x() + radius); ++x) {
            const double d = std::hypot(x - p.x(), y - p.y());
            if (d > radius)
                continue;
            const int i = y * m_size.width() + x;
            const float cost = kWeightZero * m_zero.at(i) + kWeightGradient * m_grad.at(i) + float(d) * 1e-4f;
            if (cost < bestCost) {
                bestCost = cost;
                best = QPoint(x, y);
            }
        }
    }
    return best;
}

float EdgeMap::linkCost(int p, int q, bool diagonal) const
{
    const float scale = diagonal ? 1.41421356f : 1.0f;
    float lx = float((q % m_size.width()) - (p % m_size.width()));
    float ly = float((q / m_size.width()) - (p / m_size.width()));
    const float len = std::hypot(lx, ly);
    lx /= len;
    ly /= len;
    float dp = m_dirX.at(p) * lx + m_dirY.at(p) * ly;
    if (dp < 0.0f) {
        dp = -dp;
        lx = -lx;
        ly = -ly;
    }
    const float dq = std::clamp(m_dirX.at(q) * lx + m_dirY.at(q) * ly, -1.0f, 1.0f);
    const float direction = float(2.0 / (3.0 * kPi)) * (std::acos(std::min(dp, 1.0f)) + std::acos(dq));
    return kWeightZero * m_zero.at(q) + kWeightGradient * scale * m_grad.at(q) + kWeightDirection * direction;
}

QVector<QPoint> EdgeMap::path(QPoint from, QPoint to) const
{
    if (isNull())
        return {from, to};
    from = clamped(from);
    to = clamped(to);
    if (from == to)
        return {from};

    const int margin = 40 + int(0.3 * std::max(std::abs(from.x() - to.x()), std::abs(from.y() - to.y())));
    const int x0 = std::max(0, std::min(from.x(), to.x()) - margin);
    const int y0 = std::max(0, std::min(from.y(), to.y()) - margin);
    const int x1 = std::min(m_size.width() - 1, std::max(from.x(), to.x()) + margin);
    const int y1 = std::min(m_size.height() - 1, std::max(from.y(), to.y()) + margin);
    const int ww = x1 - x0 + 1;
    const int wh = y1 - y0 + 1;
    const int stride = m_size.width();
    auto local = [&](QPoint p) { return (p.y() - y0) * ww + (p.x() - x0); };
    auto global = [&](int l) { return (l / ww + y0) * stride + (l % ww + x0); };

    QVector<float> dist(ww * wh, std::numeric_limits<float>::max());
    QVector<int> prev(ww * wh, -1);
    using Item = std::pair<float, int>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> queue;
    const int start = local(from);
    const int goal = local(to);
    dist[start] = 0.0f;
    queue.push({0.0f, start});
    bool found = false;
    while (!queue.empty()) {
        const auto [d, cur] = queue.top();
        queue.pop();
        if (d > dist.at(cur))
            continue;
        if (cur == goal) {
            found = true;
            break;
        }
        const int cx = cur % ww;
        const int cy = cur / ww;
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dy == 0)
                    continue;
                const int nx = cx + dx;
                const int ny = cy + dy;
                if (nx < 0 || ny < 0 || nx >= ww || ny >= wh)
                    continue;
                const int next = ny * ww + nx;
                const float nd = d + linkCost(global(cur), global(next), dx != 0 && dy != 0);
                if (nd < dist.at(next)) {
                    dist[next] = nd;
                    prev[next] = cur;
                    queue.push({nd, next});
                }
            }
        }
    }
    if (!found)
        return {from, to};

    QVector<QPoint> result;
    for (int cur = goal; cur >= 0; cur = prev.at(cur))
        result.append(QPoint(cur % ww + x0, cur / ww + y0));
    std::reverse(result.begin(), result.end());
    return result;
}

namespace {

struct Lab
{
    float l, a, b;
};

float srgbToLinear(int v)
{
    const float c = v / 255.0f;
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

Lab toLab(QRgb px)
{
    static float table[256];
    static bool ready = false;
    if (!ready) {
        for (int i = 0; i < 256; ++i)
            table[i] = srgbToLinear(i);
        ready = true;
    }
    const float r = table[qRed(px)];
    const float g = table[qGreen(px)];
    const float b = table[qBlue(px)];
    auto f = [](float t) { return t > 0.008856f ? std::cbrt(t) : 7.787f * t + 16.0f / 116.0f; };
    const float fx = f((0.4124564f * r + 0.3575761f * g + 0.1804375f * b) / 0.95047f);
    const float fy = f(0.2126729f * r + 0.7151522f * g + 0.0721750f * b);
    const float fz = f((0.0193339f * r + 0.1191920f * g + 0.9503041f * b) / 1.08883f);
    return {116.0f * fy - 16.0f, 500.0f * (fx - fy), 200.0f * (fy - fz)};
}

} // namespace

QImage magicWandCover(const QImage &source, QPoint seed, double tolerance, bool contiguous)
{
    if (source.isNull())
        return {};
    const QImage image = source.convertToFormat(QImage::Format_ARGB32);
    const int w = image.width();
    const int h = image.height();
    seed = QPoint(std::clamp(seed.x(), 0, w - 1), std::clamp(seed.y(), 0, h - 1));

    QVector<Lab> lab(w * h);
    for (int y = 0; y < h; ++y) {
        const QRgb *row = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < w; ++x)
            lab[y * w + x] = toLab(row[x]);
    }

    // The mean of the seed's 3x3 neighbourhood, so one noisy pixel does not decide the colour.
    Lab ref{0, 0, 0};
    int count = 0;
    for (int y = std::max(0, seed.y() - 1); y <= std::min(h - 1, seed.y() + 1); ++y) {
        for (int x = std::max(0, seed.x() - 1); x <= std::min(w - 1, seed.x() + 1); ++x) {
            const Lab &c = lab.at(y * w + x);
            ref.l += c.l;
            ref.a += c.a;
            ref.b += c.b;
            ++count;
        }
    }
    ref = {ref.l / count, ref.a / count, ref.b / count};

    const float limit = float(std::clamp(tolerance, 0.0, 100.0) * 1.2);
    auto similar = [&](int i) {
        const Lab &c = lab.at(i);
        return std::sqrt((c.l - ref.l) * (c.l - ref.l) + (c.a - ref.a) * (c.a - ref.a) + (c.b - ref.b) * (c.b - ref.b))
               <= limit;
    };

    QImage cover(w, h, QImage::Format_Grayscale8);
    cover.fill(0);
    if (contiguous) {
        QVector<int> stack{seed.y() * w + seed.x()};
        QVector<bool> seen(w * h, false);
        seen[stack.constFirst()] = true;
        while (!stack.isEmpty()) {
            const int i = stack.takeLast();
            if (!similar(i))
                continue;
            const int x = i % w;
            const int y = i / w;
            cover.scanLine(y)[x] = 255;
            const int nb[4] = {x > 0 ? i - 1 : -1, x + 1 < w ? i + 1 : -1, y > 0 ? i - w : -1, y + 1 < h ? i + w : -1};
            for (int j : nb) {
                if (j >= 0 && !seen.at(j)) {
                    seen[j] = true;
                    stack.append(j);
                }
            }
        }
    } else {
        for (int y = 0; y < h; ++y) {
            uchar *row = cover.scanLine(y);
            for (int x = 0; x < w; ++x)
                row[x] = similar(y * w + x) ? 255 : 0;
        }
    }

    QImage soft(w, h, QImage::Format_Grayscale8);
    for (int y = 0; y < h; ++y) {
        uchar *out = soft.scanLine(y);
        for (int x = 0; x < w; ++x) {
            int sum = 0;
            int n = 0;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const int px = x + dx;
                    const int py = y + dy;
                    if (px < 0 || py < 0 || px >= w || py >= h)
                        continue;
                    sum += cover.constScanLine(py)[px];
                    ++n;
                }
            }
            out[x] = uchar(sum / n);
        }
    }
    return soft;
}

} // namespace drift::masking
