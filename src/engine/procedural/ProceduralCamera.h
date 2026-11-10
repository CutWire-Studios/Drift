#pragma once

#include <array>
#include <cmath>

namespace drift::proceduralcam {

// The same matrices mat.lookAt and mat.perspective build in Lua: column-major, view space looks
// down +Z with +X right and +Y up, clip z in [-1, 1].
inline std::array<float, 16> lookAt(const std::array<float, 3> &eye, const std::array<float, 3> &target,
                                    const std::array<float, 3> &up)
{
    const auto norm = [](std::array<float, 3> v) {
        const float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        if (l > 0.f)
            for (float &c : v)
                c /= l;
        return v;
    };
    const auto cross = [](const std::array<float, 3> &a, const std::array<float, 3> &b) {
        return std::array<float, 3>{a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2],
                                    a[0] * b[1] - a[1] * b[0]};
    };
    const std::array<float, 3> f = norm({target[0] - eye[0], target[1] - eye[1], target[2] - eye[2]});
    const std::array<float, 3> r = norm(cross(f, up));
    const std::array<float, 3> u = cross(r, f);
    std::array<float, 16> m{};
    const std::array<float, 3> *rows[3] = {&r, &u, &f};
    for (int row = 0; row < 3; ++row) {
        for (int c = 0; c < 3; ++c)
            m[std::size_t(c * 4 + row)] = (*rows[row])[std::size_t(c)];
        m[std::size_t(12 + row)] = -((*rows[row])[0] * eye[0] + (*rows[row])[1] * eye[1] + (*rows[row])[2] * eye[2]);
    }
    m[15] = 1.f;
    return m;
}

inline std::array<float, 16> perspective(float fovy, float aspect, float n, float f)
{
    const float t = 1.f / std::tan(fovy * 0.5f);
    std::array<float, 16> m{};
    m[0] = t / aspect;
    m[5] = t;
    m[10] = (f + n) / (f - n);
    m[11] = 1.f;
    m[14] = -2.f * f * n / (f - n);
    return m;
}

} // namespace drift::proceduralcam
