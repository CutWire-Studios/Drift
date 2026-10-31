#pragma once

#include <QImage>
#include <QPointF>
#include <QPolygonF>
#include <QTransform>
#include <QVector>

#include <array>
#include <vector>

// Pure CPU motion-tracking primitives. Everything works in level-0 pixel coordinates of the
// analysis frame; pixel (i, j) is sampled at the integer coordinate (i, j), not at i + 0.5.
// No threads in here: the caller runs these on a worker.
namespace drift::tracking {

struct GrayImage
{
    int width = 0;
    int height = 0;
    std::vector<float> data;

    bool isEmpty() const { return width <= 0 || height <= 0; }
    float at(int x, int y) const;
    float sample(float x, float y) const;
};

// Luma in 0..255. Frames wider than maxWidth are scaled down; *scale receives outWidth / inWidth.
GrayImage toGray(const QImage &image, int maxWidth = 960, double *scale = nullptr);

struct Pyramid
{
    std::vector<GrayImage> levels;
    std::vector<GrayImage> gradX; // Scharr, normalised to intensity change per pixel
    std::vector<GrayImage> gradY;
};

Pyramid buildPyramid(const GrayImage &level0, int levels = 4);

struct LkResult
{
    QPointF position;
    bool ok = false;
    float residual = 0; // mean absolute intensity error over the window at level 0
};

// windowRadius is clamped to 2..16.
LkResult trackPointLk(const Pyramid &prev, const Pyramid &next, QPointF point, int windowRadius = 10,
                      int maxIterations = 30, float epsilon = 0.01f);

float ncc(const GrayImage &imgA, QPointF a, const GrayImage &imgB, QPointF b, int radius);

struct PointTrackStep
{
    QPointF position;
    float confidence = 0;
    bool lost = false;
};

PointTrackStep trackPointStep(const Pyramid &prev, const Pyramid &next, QPointF prevPos,
                              const GrayImage &referenceFrame, QPointF referencePos, int patternRadius);

bool fitTranslation(const QVector<QPointF> &src, const QVector<QPointF> &dst, QTransform *out);
bool fitSimilarity(const QVector<QPointF> &src, const QVector<QPointF> &dst, bool allowScale,
                   bool allowRotation, QTransform *out);
bool fitAffine(const QVector<QPointF> &src, const QVector<QPointF> &dst, QTransform *out);

// Row-major 3x3, h[8] normalised to 1 where possible.
using Homography = std::array<double, 9>;
Homography identityHomography();
bool fitHomography(const QVector<QPointF> &src, const QVector<QPointF> &dst, Homography *h);
bool ransacHomography(const QVector<QPointF> &src, const QVector<QPointF> &dst, double thresholdPx,
                      int iterations, Homography *h, QVector<bool> *inliers);
QPointF applyHomography(const Homography &h, QPointF p);
QPolygonF mapPolygon(const Homography &h, const QPolygonF &polygon);
Homography composeHomography(const Homography &second, const Homography &first); // second * first
bool invertHomography(const Homography &h, Homography *out);

// region empty = whole image. Strongest first.
QVector<QPointF> goodFeatures(const Pyramid &pyramid, const QPolygonF &region, int maxCorners = 200,
                              double qualityLevel = 0.01, double minDistance = 8.0);

struct PlanarStep
{
    Homography referenceToNext = identityHomography();
    QVector<QPointF> features; // inlier positions in `next`
    double inlierRatio = 0;    // inliers / features that survived the forward-backward check
    bool ok = false;
};

PlanarStep trackPlanarStep(const Pyramid &prev, const Pyramid &next, const QVector<QPointF> &features,
                           const Homography &referenceToPrev);

} // namespace drift::tracking
