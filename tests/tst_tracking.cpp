#include "engine/tracking/MotionTracking.h"

#include <QElapsedTimer>
#include <QtTest>

#include <cmath>
#include <random>

using namespace drift::tracking;

namespace {

constexpr int kW = 640;
constexpr int kH = 360;

GrayImage blur(const GrayImage &src, int passes)
{
    GrayImage a = src, b = src;
    for (int p = 0; p < passes; ++p) {
        for (int y = 0; y < a.height; ++y)
            for (int x = 0; x < a.width; ++x)
                b.data[size_t(y) * a.width + x] =
                    (a.at(x - 1, y) + 2 * a.at(x, y) + a.at(x + 1, y)) * 0.25f;
        for (int y = 0; y < a.height; ++y)
            for (int x = 0; x < a.width; ++x)
                a.data[size_t(y) * a.width + x] =
                    (b.at(x, y - 1) + 2 * b.at(x, y) + b.at(x, y + 1)) * 0.25f;
    }
    return a;
}

// Contrast is restored after blurring so the texture keeps a usable gradient.
GrayImage makeTexture(unsigned seed)
{
    GrayImage img;
    img.width = kW;
    img.height = kH;
    img.data.resize(size_t(kW) * kH);
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> dist(0.0f, 255.0f);
    for (float &v : img.data)
        v = dist(rng);
    img = blur(img, 2);
    float lo = 1e9f, hi = -1e9f;
    for (float v : img.data) {
        lo = std::min(lo, v);
        hi = std::max(hi, v);
    }
    for (float &v : img.data)
        v = (v - lo) / (hi - lo) * 255.0f;
    return img;
}

// out(x, y) = src(map(x, y)) with bilinear sampling
template<typename F>
GrayImage warp(const GrayImage &src, F map)
{
    GrayImage out = src;
    for (int y = 0; y < src.height; ++y) {
        for (int x = 0; x < src.width; ++x) {
            const QPointF p = map(QPointF(x, y));
            out.data[size_t(y) * src.width + x] = src.sample(float(p.x()), float(p.y()));
        }
    }
    return out;
}

double dist(QPointF a, QPointF b)
{
    return std::hypot(a.x() - b.x(), a.y() - b.y());
}

} // namespace

class TstTracking : public QObject
{
    Q_OBJECT
private slots:
    void lkRecoversTranslation();
    void pointStepFollowsAndLosesTrack();
    void similarityFit();
    void homographyRansac();
    void planarStepRecoversHomography();
    void goodFeaturesRespectRegion();
    void timing();
};

void TstTracking::lkRecoversTranslation()
{
    const GrayImage a = makeTexture(1);
    const double dx = 7.3, dy = -4.6;
    const GrayImage b = warp(a, [&](QPointF p) { return QPointF(p.x() - dx, p.y() - dy); });
    const Pyramid pa = buildPyramid(a), pb = buildPyramid(b);
    std::mt19937 rng(7);
    std::uniform_real_distribution<double> ux(40, kW - 40), uy(40, kH - 40);
    for (int i = 0; i < 20; ++i) {
        const QPointF p(ux(rng), uy(rng));
        const LkResult r = trackPointLk(pa, pb, p);
        QVERIFY(r.ok);
        QVERIFY2(dist(r.position, p + QPointF(dx, dy)) < 0.1, "LK missed the known translation");
        const LkResult back = trackPointLk(pb, pa, r.position);
        QVERIFY2(back.ok, qPrintable(QStringLiteral("%1,%2 -> %3,%4 -> %5,%6").arg(p.x()).arg(p.y()).arg(r.position.x()).arg(r.position.y()).arg(back.position.x()).arg(back.position.y())));
        QVERIFY(dist(back.position, p) < 0.2);
    }
}

void TstTracking::pointStepFollowsAndLosesTrack()
{
    const GrayImage base = makeTexture(2);
    auto frame = [&](int i) { return warp(base, [&](QPointF p) { return QPointF(p.x() - 3.0 * i, p.y()); }); };
    const QPointF start(150, 180);
    GrayImage ref = frame(0);
    Pyramid prev = buildPyramid(ref);
    QPointF pos = start;
    for (int i = 1; i <= 30; ++i) {
        const GrayImage f = frame(i);
        const Pyramid next = buildPyramid(f);
        const PointTrackStep s = trackPointStep(prev, next, pos, ref, start, 8);
        QVERIFY(!s.lost);
        QVERIFY2(s.confidence > 0.8f, qPrintable(QStringLiteral("frame %1 conf %2").arg(i).arg(s.confidence)));
        pos = s.position;
        prev = next;
    }
    QVERIFY2(dist(pos, start + QPointF(90, 0)) < 1.0, "drift too large");

    const Pyramid noise = buildPyramid(makeTexture(99));
    const PointTrackStep s = trackPointStep(prev, noise, pos, ref, start, 8);
    QVERIFY(s.lost || s.confidence < 0.3f);
}

void TstTracking::similarityFit()
{
    const double ang = 10.0 * M_PI / 180.0, scale = 1.2;
    QVector<QPointF> src, dst;
    std::mt19937 rng(3);
    std::uniform_real_distribution<double> u(-100, 100);
    for (int i = 0; i < 12; ++i) {
        const QPointF p(u(rng), u(rng));
        src.append(p);
        dst.append(QPointF(scale * (std::cos(ang) * p.x() - std::sin(ang) * p.y()) + 15,
                           scale * (std::sin(ang) * p.x() + std::cos(ang) * p.y()) - 8));
    }
    QTransform t;
    QVERIFY(fitSimilarity(src, dst, true, true, &t));
    QCOMPARE_LT(std::abs(t.m11() - scale * std::cos(ang)), 1e-9);
    QCOMPARE_LT(std::abs(t.m12() - scale * std::sin(ang)), 1e-9);
    QCOMPARE_LT(std::abs(t.dx() - 15), 1e-9);
    QCOMPARE_LT(std::abs(t.dy() + 8), 1e-9);

    QVERIFY(fitSimilarity(src, dst, false, true, &t));
    QCOMPARE_LT(std::abs(std::hypot(t.m11(), t.m12()) - 1.0), 1e-9);
    QCOMPARE_LT(std::abs(std::atan2(t.m12(), t.m11()) - ang), 1e-9);

    QVERIFY(fitSimilarity(src, dst, true, false, &t));
    QCOMPARE_LT(std::abs(t.m12()), 1e-12);

    QTransform aff;
    QVERIFY(fitAffine(src, dst, &aff));
    for (int i = 0; i < src.size(); ++i)
        QCOMPARE_LT(dist(aff.map(src[i]), dst[i]), 1e-8);

    QVERIFY(fitTranslation(src, src, &t));
    QCOMPARE_LT(std::abs(t.dx()) + std::abs(t.dy()), 1e-12);
    QVERIFY(!fitSimilarity({QPointF(1, 1)}, {QPointF(2, 2)}, true, true, &t));
}

void TstTracking::homographyRansac()
{
    const Homography truth = {1.05, 0.08, 20.0, -0.06, 0.97, -12.0, 0.0004, -0.0002, 1.0};
    std::mt19937 rng(11);
    std::uniform_real_distribution<double> ux(0, kW), uy(0, kH), off(-80, 80);
    QVector<QPointF> src, dst;
    QVector<bool> isOutlier;
    for (int i = 0; i < 100; ++i) {
        const QPointF p(ux(rng), uy(rng));
        src.append(p);
        const bool outlier = i % 10 < 3;
        isOutlier.append(outlier);
        QPointF q = applyHomography(truth, p);
        if (outlier)
            q += QPointF(off(rng) + (off(rng) > 0 ? 30 : -30), off(rng) + 30);
        dst.append(q);
    }
    QVector<QPointF> cs, cd;
    for (int i = 0; i < 100; ++i)
        if (!isOutlier[i]) {
            cs.append(src[i]);
            cd.append(dst[i]);
        }
    Homography direct;
    QVERIFY(fitHomography(cs, cd, &direct));
    for (int i = 0; i < cs.size(); ++i)
        QCOMPARE_LT(dist(applyHomography(direct, cs[i]), cd[i]), 1e-6);

    Homography h;
    QVector<bool> inl;
    QVERIFY(ransacHomography(src, dst, 1.0, 500, &h, &inl));
    for (int i = 0; i < 100; ++i) {
        if (isOutlier[i]) {
            QVERIFY(!inl[i]);
        } else {
            QVERIFY(inl[i]);
            QCOMPARE_LT(dist(applyHomography(h, src[i]), dst[i]), 0.5);
        }
    }
    Homography inv;
    QVERIFY(invertHomography(h, &inv));
    QCOMPARE_LT(dist(applyHomography(inv, applyHomography(h, QPointF(100, 100))), QPointF(100, 100)), 1e-6);
}

void TstTracking::planarStepRecoversHomography()
{
    const GrayImage a = makeTexture(5);
    // next(x) = a(Hinv x), so the content moves by H
    const Homography H = {1.01, 0.012, 4.0, -0.01, 0.995, 2.5, 0.00001, -0.00001, 1.0};
    Homography Hinv;
    QVERIFY(invertHomography(H, &Hinv));
    const GrayImage b = warp(a, [&](QPointF p) { return applyHomography(Hinv, p); });
    const Pyramid pa = buildPyramid(a), pb = buildPyramid(b);

    const QPolygonF region({QPointF(150, 80), QPointF(500, 80), QPointF(500, 290), QPointF(150, 290)});
    const QVector<QPointF> feats = goodFeatures(pa, region, 200, 0.01, 8.0);
    QVERIFY(feats.size() > 50);
    const PlanarStep s = trackPlanarStep(pa, pb, feats, identityHomography());
    QVERIFY(s.ok);
    QVERIFY(s.inlierRatio > 0.8);
    for (const QPointF &c : region)
        QCOMPARE_LT(dist(applyHomography(s.referenceToNext, c), applyHomography(H, c)), 1.0);

    const Homography composed = composeHomography(H, H);
    const PlanarStep s2 = trackPlanarStep(pa, pb, feats, H);
    QVERIFY(s2.ok);
    for (const QPointF &c : region)
        QCOMPARE_LT(dist(applyHomography(s2.referenceToNext, c), applyHomography(composed, c)), 1.5);
}

void TstTracking::goodFeaturesRespectRegion()
{
    const Pyramid p = buildPyramid(makeTexture(8));
    const QPolygonF tri({QPointF(100, 60), QPointF(420, 120), QPointF(200, 300)});
    const QVector<QPointF> f = goodFeatures(p, tri, 100, 0.01, 12.0);
    QVERIFY(f.size() > 10);
    for (int i = 0; i < f.size(); ++i) {
        QVERIFY(tri.containsPoint(f[i], Qt::OddEvenFill));
        for (int j = i + 1; j < f.size(); ++j)
            QVERIFY(dist(f[i], f[j]) >= 12.0);
    }
    QVERIFY(f.size() <= 100);
    QCOMPARE(goodFeatures(p, QPolygonF(), 30, 0.01, 8.0).size(), 30);
}

void TstTracking::timing()
{
    const GrayImage a = makeTexture(5);
    const GrayImage b = warp(a, [](QPointF p) { return QPointF(p.x() - 2.0, p.y() - 1.0); });
    QElapsedTimer t;
    t.start();
    Pyramid pa;
    for (int i = 0; i < 10; ++i)
        pa = buildPyramid(a);
    qInfo() << "pyramid build ms:" << t.nsecsElapsed() / 1e7;
    const Pyramid pb = buildPyramid(b);
    t.restart();
    for (int i = 0; i < 100; ++i)
        trackPointStep(pa, pb, QPointF(300, 180), a, QPointF(298, 179), 8);
    qInfo() << "point step ms:" << t.nsecsElapsed() / 1e8;
    const QVector<QPointF> feats = goodFeatures(pa, QPolygonF(), 200);
    t.restart();
    for (int i = 0; i < 5; ++i)
        trackPlanarStep(pa, pb, feats, identityHomography());
    qInfo() << "planar step (" << feats.size() << "features) ms:" << t.nsecsElapsed() / 5e6;
}

QTEST_APPLESS_MAIN(TstTracking)
#include "tst_tracking.moc"
