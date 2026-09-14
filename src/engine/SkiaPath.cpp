#include "SkiaPath.h"

#include <QGradient>

#include <vector>

#include "include/core/SkPathBuilder.h"
#include "include/core/SkPoint.h"
#include "include/core/SkTileMode.h"
#include "include/effects/SkDashPathEffect.h"
#include "include/effects/SkGradient.h"

namespace drift::skia {

SkPath toSkPath(const QPainterPath &path)
{
    SkPathBuilder b(path.fillRule() == Qt::WindingFill ? SkPathFillType::kWinding
                                                       : SkPathFillType::kEvenOdd);
    // QPainterPath has no close verb: closeSubpath() appends a LineTo back to the start. Qt's
    // stroker joins such an end instead of capping it, so emit a close for the same effect.
    QPointF start;
    const int n = path.elementCount();
    for (int i = 0; i < n; ++i) {
        const QPainterPath::Element &e = path.elementAt(i);
        switch (e.type) {
        case QPainterPath::MoveToElement:
            start = QPointF(e.x, e.y);
            b.moveTo(SkPoint::Make(float(e.x), float(e.y)));
            break;
        case QPainterPath::LineToElement: {
            const bool last = i + 1 == n || path.elementAt(i + 1).type == QPainterPath::MoveToElement;
            if (last && qFuzzyCompare(e.x, start.x()) && qFuzzyCompare(e.y, start.y()))
                b.close();
            else
                b.lineTo(SkPoint::Make(float(e.x), float(e.y)));
            break;
        }
        case QPainterPath::CurveToElement: {
            const QPainterPath::Element &c2 = path.elementAt(i + 1);
            const QPainterPath::Element &end = path.elementAt(i + 2);
            b.cubicTo(SkPoint::Make(float(e.x), float(e.y)), SkPoint::Make(float(c2.x), float(c2.y)),
                      SkPoint::Make(float(end.x), float(end.y)));
            i += 2;
            break;
        }
        case QPainterPath::CurveToDataElement:
            break;
        }
    }
    return b.detach();
}

SkMatrix toSkMatrix(const QTransform &t)
{
    return SkMatrix::MakeAll(float(t.m11()), float(t.m21()), float(t.m31()),
                             float(t.m12()), float(t.m22()), float(t.m32()),
                             float(t.m13()), float(t.m23()), float(t.m33()));
}

SkColor toSkColor(const QColor &c)
{
    return SkColorSetARGB(c.alpha(), c.red(), c.green(), c.blue());
}

namespace {

SkTileMode tileModeFor(QGradient::Spread spread)
{
    switch (spread) {
    case QGradient::PadSpread:
        return SkTileMode::kClamp;
    case QGradient::RepeatSpread:
        return SkTileMode::kRepeat;
    case QGradient::ReflectSpread:
        return SkTileMode::kMirror;
    }
    return SkTileMode::kClamp;
}

sk_sp<SkShader> gradientShader(const QBrush &brush)
{
    const QGradient *gradient = brush.gradient();
    if (!gradient)
        return nullptr;

    std::vector<SkColor4f> colors;
    std::vector<float> positions;
    for (const QGradientStop &stop : gradient->stops()) {
        colors.push_back(SkColor4f::FromColor(toSkColor(stop.second)));
        positions.push_back(float(stop.first));
    }
    if (colors.empty())
        return nullptr;

    const SkGradient::Colors stops{SkSpan<const SkColor4f>(colors), SkSpan<const float>(positions),
                                   tileModeFor(gradient->spread())};
    const SkGradient grad{stops, SkGradient::Interpolation{}};
    const SkMatrix local = toSkMatrix(brush.transform());

    switch (gradient->type()) {
    case QGradient::LinearGradient: {
        const auto *lin = static_cast<const QLinearGradient *>(gradient);
        const SkPoint pts[2] = {
            SkPoint::Make(float(lin->start().x()), float(lin->start().y())),
            SkPoint::Make(float(lin->finalStop().x()), float(lin->finalStop().y()))};
        return SkShaders::LinearGradient(pts, grad, &local);
    }
    case QGradient::RadialGradient: {
        // Qt's radial gradient runs from a focal circle to the outer circle; that is Skia's
        // two-point conical gradient, and it degenerates to a plain radial when they share a
        // centre.
        const auto *rad = static_cast<const QRadialGradient *>(gradient);
        return SkShaders::TwoPointConicalGradient(
            SkPoint::Make(float(rad->focalPoint().x()), float(rad->focalPoint().y())),
            float(rad->focalRadius()),
            SkPoint::Make(float(rad->center().x()), float(rad->center().y())),
            float(rad->radius()), grad, &local);
    }
    case QGradient::ConicalGradient:
    case QGradient::NoGradient:
        break;
    }
    return nullptr;
}

} // namespace

bool applyBrush(SkPaint &paint, const QBrush &brush)
{
    if (brush.style() == Qt::NoBrush)
        return false;
    paint.setStyle(SkPaint::kFill_Style);
    if (sk_sp<SkShader> shader = gradientShader(brush)) {
        paint.setShader(std::move(shader));
        paint.setColor(SK_ColorBLACK);
    } else {
        paint.setShader(nullptr);
        paint.setColor(toSkColor(brush.color()));
    }
    return true;
}

bool applyPen(SkPaint &paint, const QPen &pen)
{
    if (pen.style() == Qt::NoPen || pen.widthF() <= 0.0)
        return false;
    paint.setStyle(SkPaint::kStroke_Style);
    paint.setStrokeWidth(float(pen.widthF()));
    paint.setColor(toSkColor(pen.color()));
    paint.setShader(nullptr);

    switch (pen.capStyle()) {
    case Qt::FlatCap:
        paint.setStrokeCap(SkPaint::kButt_Cap);
        break;
    case Qt::SquareCap:
        paint.setStrokeCap(SkPaint::kSquare_Cap);
        break;
    case Qt::RoundCap:
        paint.setStrokeCap(SkPaint::kRound_Cap);
        break;
    default:
        break;
    }
    switch (pen.joinStyle()) {
    case Qt::MiterJoin:
    case Qt::SvgMiterJoin:
        paint.setStrokeJoin(SkPaint::kMiter_Join);
        paint.setStrokeMiter(float(pen.miterLimit()));
        break;
    case Qt::BevelJoin:
        paint.setStrokeJoin(SkPaint::kBevel_Join);
        break;
    case Qt::RoundJoin:
        paint.setStrokeJoin(SkPaint::kRound_Join);
        break;
    default:
        break;
    }

    // Qt dash lengths are multiples of the pen width; Skia wants pixels.
    const QList<qreal> pattern = pen.dashPattern();
    if (pattern.isEmpty() || pattern.size() % 2 != 0) {
        paint.setPathEffect(nullptr);
        return true;
    }
    std::vector<SkScalar> intervals;
    intervals.reserve(pattern.size());
    for (qreal len : pattern)
        intervals.push_back(float(qMax(0.0, len) * pen.widthF()));
    paint.setPathEffect(
        SkDashPathEffect::Make(SkSpan<const SkScalar>(intervals), float(pen.dashOffset() * pen.widthF())));
    return true;
}

} // namespace drift::skia
