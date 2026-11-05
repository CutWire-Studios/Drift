#pragma once

#include <QMatrix4x4>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <limits>

namespace drift {

// The scene camera's rotation is R = Ry(y) · Rx(x) · Rz(z) (QMatrix4x4::rotate, degrees): yaw
// first, so a level camera stays level for any pitch and yaw. This solves for (x, y, z) from the
// rotation part of `r`, choosing among the equivalent solutions the one nearest `nearest` and
// unwrapping each angle to within 180 degrees of it. At pitch +-90 degrees yaw and roll turn about
// the same axis; the roll is then held at `nearest[2]`.
inline void cameraEulerFromMatrix(const QMatrix4x4 &r, const double nearest[3], double out[3])
{
    constexpr double toDeg = 180.0 / M_PI;
    const auto wrapNear = [](double angle, double reference) {
        return angle - 360.0 * std::round((angle - reference) / 360.0);
    };
    const double sx = std::clamp(-double(r(1, 2)), -1.0, 1.0);
    double cand[2][3];
    if (std::abs(sx) > 0.99999) {
        const double z = nearest[2];
        const double phi = std::atan2(sx * std::sin(z / toDeg), std::cos(z / toDeg));
        const double y = (phi - std::atan2(double(r(2, 0)), double(r(0, 0)))) * toDeg;
        cand[0][0] = cand[1][0] = sx > 0 ? 90.0 : -90.0;
        cand[0][1] = cand[1][1] = y;
        cand[0][2] = cand[1][2] = z;
    } else {
        const double x = std::asin(sx) * toDeg;
        const double y = std::atan2(double(r(0, 2)), double(r(2, 2))) * toDeg;
        const double z = std::atan2(double(r(1, 0)), double(r(1, 1))) * toDeg;
        cand[0][0] = x;
        cand[0][1] = y;
        cand[0][2] = z;
        cand[1][0] = 180.0 - x;
        cand[1][1] = y + 180.0;
        cand[1][2] = z + 180.0;
    }
    double bestCost = std::numeric_limits<double>::infinity();
    for (auto &c : cand) {
        double cost = 0.0;
        for (int i = 0; i < 3; ++i) {
            c[i] = wrapNear(c[i], nearest[i]);
            cost += std::abs(c[i] - nearest[i]);
        }
        if (cost < bestCost) {
            bestCost = cost;
            std::copy(c, c + 3, out);
        }
    }
}

// The scene camera used to be stored in a camera frame (canvas-centred, y down, +z toward the
// viewer) as C = Rx(rot0) · Ry(rot1) · Rz(rot2) · translate(p) with the eye at C · (0, 0, d). This is
// the same camera as the world-space transform translate(eye) · R of the current model, for a canvas
// `width` x `height`: its translation is the eye and its rotation part is R.
inline QMatrix4x4 cameraWorldFromLegacy(const double p[3], const double rot[3], double d, double width,
                                        double height)
{
    QMatrix4x4 old;
    old.rotate(float(rot[0]), 1.f, 0.f, 0.f);
    old.rotate(float(rot[1]), 0.f, 1.f, 0.f);
    old.rotate(float(rot[2]), 0.f, 0.f, 1.f);
    old.translate(float(p[0]), float(p[1]), float(p[2]));
    QMatrix4x4 frameToWorld;
    frameToWorld.translate(float(width * 0.5), float(height * 0.5));
    frameToWorld.scale(1.f, 1.f, -1.f);
    QMatrix4x4 lens;
    lens.translate(0.f, 0.f, float(d));
    lens.scale(1.f, 1.f, -1.f);
    return frameToWorld * old * lens;
}

} // namespace drift
