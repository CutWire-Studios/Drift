#include "engine/Face111.h"

#include "engine/FaceLandmarker.h"

#include <QPointF>

#include <algorithm>
#include <cmath>
#include <initializer_list>

namespace drift::face111 {

// Both tables: GPUPixel, src/filter/face_makeup_filter.cc. Copyright (c) 2021 PixPark.
// Apache License 2.0.
const std::array<uint32_t, 528> kTriangles{
    33, 34, 64, 64, 34, 65, 65, 34, 107, 107, 34, 35, 35, 36, 107,
    107, 36, 66, 66, 107, 65, 66, 36, 67, 67, 36, 37, 37, 67, 43,
    43, 38, 68, 68, 38, 39, 39, 68, 69, 39, 40, 108, 39, 108, 69,
    69, 108, 70, 70, 108, 41, 41, 108, 40, 41, 70, 71, 71, 41, 42,
    0, 33, 52, 33, 52, 64, 52, 64, 53, 64, 53, 65, 65, 53, 72,
    65, 72, 66, 66, 72, 54, 66, 54, 67, 54, 67, 55, 67, 55, 78,
    67, 78, 43, 52, 53, 57, 53, 72, 74, 53, 74, 57, 74, 57, 73,
    72, 54, 104, 72, 104, 74, 74, 104, 73, 73, 104, 56, 104, 56, 54,
    54, 56, 55, 68, 43, 79, 68, 79, 58, 68, 58, 59, 68, 59, 69,
    69, 59, 75, 69, 75, 70, 70, 75, 60, 70, 60, 71, 71, 60, 61,
    71, 61, 42, 42, 61, 32, 61, 60, 62, 60, 75, 77, 60, 77, 62,
    77, 62, 76, 75, 77, 105, 77, 105, 76, 105, 76, 63, 105, 63, 59,
    105, 59, 75, 59, 63, 58, 0, 52, 1, 1, 52, 2, 2, 52, 57,
    2, 57, 3, 3, 57, 4, 4, 57, 109, 57, 109, 74, 74, 109, 56,
    56, 109, 80, 80, 109, 82, 82, 109, 7, 7, 109, 6, 6, 109, 5,
    5, 109, 4, 56, 80, 55, 55, 80, 78, 32, 61, 31, 31, 61, 30,
    30, 61, 62, 30, 62, 29, 29, 62, 28, 28, 62, 110, 62, 110, 76,
    76, 110, 63, 63, 110, 81, 81, 110, 83, 83, 110, 25, 25, 110, 26,
    26, 110, 27, 27, 110, 28, 63, 81, 58, 58, 81, 79, 78, 43, 44,
    43, 44, 79, 78, 44, 80, 79, 81, 44, 80, 44, 45, 44, 81, 45,
    80, 45, 46, 45, 81, 46, 80, 46, 82, 81, 46, 83, 82, 46, 47,
    47, 46, 48, 48, 46, 49, 49, 46, 50, 50, 46, 51, 51, 46, 83,
    7, 82, 84, 82, 84, 47, 84, 47, 85, 85, 47, 48, 48, 85, 86,
    86, 48, 49, 49, 86, 87, 49, 87, 88, 88, 49, 50, 88, 50, 89,
    89, 50, 51, 89, 51, 90, 51, 90, 83, 83, 90, 25, 84, 85, 96,
    96, 85, 97, 97, 85, 86, 86, 97, 98, 86, 98, 87, 87, 98, 88,
    88, 98, 99, 88, 99, 89, 89, 99, 100, 89, 100, 90, 90, 100, 91,
    100, 91, 101, 101, 91, 92, 101, 92, 102, 102, 92, 93, 102, 93, 94,
    102, 94, 103, 103, 94, 95, 103, 95, 96, 96, 95, 84, 96, 97, 103,
    97, 103, 106, 97, 106, 98, 106, 103, 102, 106, 102, 101, 106, 101, 99,
    106, 98, 99, 99, 101, 100, 7, 84, 8, 8, 84, 9, 9, 84, 10,
    10, 84, 95, 10, 95, 11, 11, 95, 12, 12, 95, 94, 12, 94, 13,
    13, 94, 14, 14, 94, 93, 14, 93, 15, 15, 93, 16, 16, 93, 17,
    17, 93, 18, 18, 93, 92, 18, 92, 19, 19, 92, 20, 20, 92, 91,
    20, 91, 21, 21, 91, 22, 22, 91, 90, 22, 90, 23, 23, 90, 24,
    24, 90, 25,};

const std::array<float, kPoints * 2> kReferenceUv{
    0.302451f, 0.384169f, 0.302986f, 0.409377f, 0.304336f, 0.434977f,
    0.306984f, 0.460683f, 0.311010f, 0.486447f, 0.316537f, 0.511947f,
    0.323069f, 0.536942f, 0.331312f, 0.561627f, 0.342011f, 0.585088f,
    0.355477f, 0.607217f, 0.371142f, 0.627774f, 0.388459f, 0.646991f,
    0.407041f, 0.665229f, 0.426325f, 0.682694f, 0.447468f, 0.697492f,
    0.471782f, 0.707060f, 0.500000f, 0.709867f, 0.528218f, 0.707060f,
    0.552532f, 0.697492f, 0.573675f, 0.682694f, 0.592959f, 0.665229f,
    0.611541f, 0.646991f, 0.628858f, 0.627774f, 0.644523f, 0.607217f,
    0.657989f, 0.585088f, 0.668688f, 0.561627f, 0.676931f, 0.536942f,
    0.683463f, 0.511947f, 0.688990f, 0.486447f, 0.693016f, 0.460683f,
    0.695664f, 0.434977f, 0.697014f, 0.409377f, 0.697549f, 0.384169f,
    0.331655f, 0.354725f, 0.354609f, 0.331785f, 0.387080f, 0.325436f,
    0.420446f, 0.330125f, 0.452685f, 0.339996f, 0.547315f, 0.339996f,
    0.579554f, 0.330125f, 0.612920f, 0.325436f, 0.645391f, 0.331785f,
    0.668345f, 0.354725f, 0.500000f, 0.405156f, 0.500000f, 0.442322f,
    0.500000f, 0.480116f, 0.500000f, 0.517378f, 0.457729f, 0.542442f,
    0.476911f, 0.546376f, 0.500000f, 0.550557f, 0.523089f, 0.546376f,
    0.542271f, 0.542442f, 0.366597f, 0.404028f, 0.385132f, 0.392425f,
    0.428177f, 0.397495f, 0.442446f, 0.414082f, 0.422818f, 0.419177f,
    0.382917f, 0.415929f, 0.557554f, 0.414082f, 0.571823f, 0.397495f,
    0.614868f, 0.392425f, 0.633403f, 0.404028f, 0.617083f, 0.415929f,
    0.577182f, 0.419177f, 0.360880f, 0.349748f, 0.391440f, 0.348304f,
    0.421788f, 0.352051f, 0.451601f, 0.358026f, 0.548399f, 0.358026f,
    0.578212f, 0.352051f, 0.608560f, 0.348304f, 0.639120f, 0.349748f,
    0.407165f, 0.390906f, 0.402591f, 0.420584f, 0.406113f, 0.405280f,
    0.592835f, 0.390906f, 0.597409f, 0.420584f, 0.593887f, 0.405280f,
    0.471223f, 0.409619f, 0.528777f, 0.409619f, 0.455607f, 0.495169f,
    0.544393f, 0.495169f, 0.441855f, 0.523363f, 0.558145f, 0.523363f,
    0.426186f, 0.593516f, 0.453348f, 0.586128f, 0.481258f, 0.582594f,
    0.500000f, 0.584476f, 0.518742f, 0.582594f, 0.546652f, 0.586128f,
    0.573814f, 0.593516f, 0.556544f, 0.620391f, 0.531320f, 0.639672f,
    0.500000f, 0.644911f, 0.468680f, 0.639672f, 0.443456f, 0.620391f,
    0.433718f, 0.595595f, 0.466898f, 0.597025f, 0.500000f, 0.599883f,
    0.533102f, 0.597025f, 0.566282f, 0.595595f, 0.534634f, 0.610720f,
    0.500000f, 0.616173f, 0.465366f, 0.610720f, 0.406113f, 0.405280f,
    0.593887f, 0.405280f, 0.500000f, 0.608028f, 0.389259f, 0.336870f,
    0.610740f, 0.336870f, 0.386071f, 0.503558f, 0.613928f, 0.503558f,};

namespace {

using V = QPointF;

// Points `count` evenly by arc length along an open polyline, both ends included. The two jaw
// halves are resampled rather than picked vertex by vertex: Face++ spaces its jaw evenly, while
// the MediaPipe oval bunches up around the chin.
template <size_t N>
void resample(const std::array<V, N> &path, int count, V *out)
{
    std::array<double, N> along{};
    for (size_t i = 1; i < N; ++i) {
        const V d = path[i] - path[i - 1];
        along[i] = along[i - 1] + std::hypot(d.x(), d.y());
    }
    size_t seg = 0;
    for (int k = 0; k < count; ++k) {
        const double t = along[N - 1] * k / (count - 1);
        while (seg + 2 < N && along[seg + 1] < t)
            ++seg;
        const double len = along[seg + 1] - along[seg];
        const double f = len > 0.0 ? std::clamp((t - along[seg]) / len, 0.0, 1.0) : 0.0;
        out[k] = path[seg] * (1.0 - f) + path[seg + 1] * f;
    }
}

} // namespace

void fromMediaPipe(const QList<QVector3D> &mesh468, QVector<float> *out)
{
    out->clear();
    if (mesh468.size() != kFaceMeshPoints)
        return;

    auto at = [&](int i) { return V(mesh468.at(i).x(), mesh468.at(i).y()); };
    auto mean = [&](std::initializer_list<int> ids) {
        V sum;
        for (int i : ids)
            sum += at(i);
        return sum / double(ids.size());
    };
    auto ringCentroid = [&](const auto &ring) {
        V sum;
        for (int i : ring)
            sum += at(i);
        return sum / double(ring.size());
    };

    std::array<V, kPoints> p{};

    // Jaw: kFaceOval runs clockwise from the forehead (10). Index 18 is the chin (152); the
    // image-right half starts at 389 (index 6), the image-left half at 162 (index 30). Starting
    // above the ear rather than at it is what lines point 0 up with the outer eye corner.
    const auto &oval = mpidx::kFaceOval;
    std::array<V, 13> left{}, right{};
    for (int i = 0; i < 13; ++i) {
        left[size_t(i)] = at(oval[size_t(30 - i)]);
        right[size_t(i)] = at(oval[size_t(6 + i)]);
    }
    std::array<V, 17> jaw{};
    resample(left, 17, jaw.data());
    for (int i = 0; i < 17; ++i)
        p[size_t(i)] = jaw[size_t(i)];
    resample(right, 17, jaw.data());
    for (int i = 0; i < 16; ++i)
        p[size_t(32 - i)] = jaw[size_t(i)];

    // Brows. Each ring runs along the upper edge from the inner end, then back along the lower
    // edge; Face++ orders both brows from image-left to image-right.
    const auto &bl = mpidx::kBrowLeftRing;
    const auto &br = mpidx::kBrowRightRing;
    for (int k = 0; k < 5; ++k) {
        p[size_t(33 + k)] = at(bl[size_t(4 - k)]);
        p[size_t(38 + k)] = at(br[size_t(k)]);
    }
    for (int k = 0; k < 4; ++k) {
        p[size_t(64 + k)] = at(bl[size_t(5 + k)]);
        p[size_t(68 + k)] = at(br[size_t(8 - k)]);
    }
    p[107] = mean({bl[1], bl[2], bl[6], bl[7]});
    p[108] = mean({br[1], br[2], br[6], br[7]});

    // Nose.
    p[43] = at(168);
    p[44] = at(197);
    p[45] = at(5);
    p[46] = at(1);
    p[47] = at(98);
    p[48] = at(97);
    p[49] = at(2);
    p[50] = at(326);
    p[51] = at(327);
    p[78] = at(193);
    p[79] = at(417);
    p[80] = at(115);
    p[81] = at(344);
    p[82] = at(129);
    p[83] = at(358);

    // Eyes. Both rings start at a corner and run along the upper lid to the other corner (index
    // 8), then back along the lower lid, so the same ring slots serve both eyes. The left ring
    // starts at its outer corner and the right at its inner, which is exactly Face++'s order.
    auto eye = [&](const std::array<int, 16> &ring, int corner, int upper1, int top, int upper2,
                   int corner2, int lower2, int bottom, int lower1) {
        p[size_t(corner)] = at(ring[0]);
        p[size_t(upper1)] = at(ring[3]);
        p[size_t(top)] = at(ring[4]);
        p[size_t(upper2)] = at(ring[5]);
        p[size_t(corner2)] = at(ring[8]);
        p[size_t(lower2)] = at(ring[11]);
        p[size_t(bottom)] = at(ring[12]);
        p[size_t(lower1)] = at(ring[13]);
    };
    eye(mpidx::kEyeLeftRing, 52, 53, 72, 54, 55, 56, 73, 57);
    eye(mpidx::kEyeRightRing, 58, 59, 75, 60, 61, 62, 76, 63);
    p[74] = p[104] = ringCentroid(mpidx::kEyeLeftRing);
    p[77] = p[105] = ringCentroid(mpidx::kEyeRightRing);

    // Lips. kLipOuter starts at the image-left corner (61) and runs over the upper lip; kLipInner
    // likewise from 78.
    const auto &lo = mpidx::kLipOuter;
    const auto &li = mpidx::kLipInner;
    const int upperOuter[7] = {0, 2, 4, 5, 6, 8, 10};
    for (int k = 0; k < 7; ++k)
        p[size_t(84 + k)] = at(lo[size_t(upperOuter[k])]);
    const int lowerOuter[5] = {12, 14, 15, 16, 18};
    for (int k = 0; k < 5; ++k)
        p[size_t(91 + k)] = at(lo[size_t(lowerOuter[k])]);
    const int upperInner[5] = {0, 3, 5, 7, 10};
    for (int k = 0; k < 5; ++k)
        p[size_t(96 + k)] = at(li[size_t(upperInner[k])]);
    const int lowerInner[3] = {13, 15, 17};
    for (int k = 0; k < 3; ++k)
        p[size_t(101 + k)] = at(li[size_t(lowerInner[k])]);
    p[106] = mean({13, 14});

    p[109] = ringCentroid(mpidx::kCheekLeft);
    p[110] = ringCentroid(mpidx::kCheekRight);

    out->resize(kPoints * 2);
    for (int i = 0; i < kPoints; ++i) {
        (*out)[2 * i] = float(p[size_t(i)].x());
        (*out)[2 * i + 1] = float(p[size_t(i)].y());
    }
}

} // namespace drift::face111
