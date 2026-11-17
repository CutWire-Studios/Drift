#include "core/project/media/SourceFrameFit.h"

#include "core/project/media/MediaAsset.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace drift {

namespace {

constexpr double kPi = 3.14159265358979323846;

// One constraint is  t*k + s*m <= L  (t scales the centre offset, s the box size). All four are
// evaluated in display pixels, the way cropSourceFrame turns the source.
struct Limit
{
    double k;
    double m;
    double L;
};

struct Fit
{
    Limit limit[4];
    double dx, dy; // centre offset from the source centre
    double hw, hh; // half sizes of the base box
};

Fit makeFit(const QRectF &frame, double angleDeg, double W, double H)
{
    const double th = angleDeg * kPi / 180.0;
    const double cs = std::cos(th), sn = std::sin(th);
    const double c = std::abs(cs), n = std::abs(sn);
    Fit f;
    f.dx = (frame.x() + frame.width() / 2.0) * W - W / 2.0;
    f.dy = (frame.y() + frame.height() / 2.0) * H - H / 2.0;
    f.hw = frame.width() * W / 2.0;
    f.hh = frame.height() * H / 2.0;
    const double u0 = f.dx * cs + f.dy * sn;
    const double v0 = -f.dx * sn + f.dy * cs;
    f.limit[0] = {std::abs(u0), f.hw * c + f.hh * n, W / 2.0};
    f.limit[1] = {std::abs(v0), f.hw * n + f.hh * c, H / 2.0};
    f.limit[2] = {std::abs(f.dx), f.hw, W / 2.0};
    f.limit[3] = {std::abs(f.dy), f.hh, H / 2.0};
    return f;
}

double sMax(const Fit &f, double t)
{
    double s = std::numeric_limits<double>::infinity();
    for (const Limit &l : f.limit)
        if (l.m > 0)
            s = std::min(s, (l.L - t * l.k) / l.m);
    return s;
}

} // namespace

bool sourceFrameCovered(const QRectF &frame, double angleDeg, double srcW, double srcH, double epsilon)
{
    if (!(srcW > 0) || !(srcH > 0))
        return true;
    const Fit f = makeFit(frame, angleDeg, srcW, srcH);
    const double slack = epsilon * std::max(srcW, srcH);
    for (const Limit &l : f.limit)
        if (l.k + l.m > l.L + slack)
            return false;
    return true;
}

QRectF fitCropInsideRotation(const QRectF &frame, double angleDeg, double srcW, double srcH,
                             double minFraction)
{
    if (!(srcW > 0) || !(srcH > 0) || !std::isfinite(angleDeg) || angleDeg == 0.0
        || !(frame.width() > 0) || !(frame.height() > 0))
        return frame;
    const Fit f = makeFit(frame, angleDeg, srcW, srcH);

    double s = std::min(1.0, sMax(f, 1.0));
    double t = 1.0;
    if (s >= 1.0 - 1e-12)
        return frame;

    const double sFloor = std::min(1.0, std::max(minFraction * srcW / (2.0 * f.hw),
                                                 minFraction * srcH / (2.0 * f.hh)));
    if (s < sFloor) {
        t = 1.0;
        for (const Limit &l : f.limit)
            if (l.k > 0)
                t = std::min(t, (l.L - sFloor * l.m) / l.k);
        t = std::clamp(t, 0.0, 1.0);
        s = std::min(1.0, sMax(f, t));
    }
    s = std::max(s, 0.0);

    // Half a pixel on the shorter side, but never more than half the box so thin boxes survive.
    const double inset = std::min(0.5 / std::min(f.hw, f.hh), s / 2.0);
    s = std::max(s - inset, 1e-6);

    const double cx = srcW / 2.0 + t * f.dx;
    const double cy = srcH / 2.0 + t * f.dy;
    return normalizedSourceFrame((cx - s * f.hw) / srcW, (cy - s * f.hh) / srcH,
                                 s * f.hw * 2.0 / srcW, s * f.hh * 2.0 / srcH);
}

} // namespace drift
