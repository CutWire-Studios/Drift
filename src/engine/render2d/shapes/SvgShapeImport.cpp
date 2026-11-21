#include "engine/render2d/shapes/SvgShapeImport.h"

#include "core/shapes/ShapePath.h"

#include <QFile>
#include <QPainterPath>
#include <QRegularExpression>
#include <QTransform>
#include <QXmlStreamReader>
#include <QtMath>

#ifdef DRIFT_WITH_SKIA
#include "include/core/SkPath.h"
#include "include/utils/SkParsePath.h"
#endif

namespace drift {

namespace {

#ifdef DRIFT_WITH_SKIA
QPointF toQt(const SkPoint &p)
{
    return QPointF(p.fX, p.fY);
}

bool pathFromData(const QString &d, QPainterPath *out)
{
    const QByteArray utf8 = d.toUtf8();
    const std::optional<SkPath> parsed = SkParsePath::FromSVGString(utf8.constData());
    if (!parsed)
        return false;

    SkPath::Iter iter(*parsed, false);
    SkPoint pts[4];
    QPainterPath path;
    for (SkPath::Verb verb = iter.next(pts); verb != SkPath::kDone_Verb; verb = iter.next(pts)) {
        switch (verb) {
        case SkPath::kMove_Verb:
            path.moveTo(toQt(pts[0]));
            break;
        case SkPath::kLine_Verb:
            path.lineTo(toQt(pts[1]));
            break;
        case SkPath::kQuad_Verb:
            path.quadTo(toQt(pts[1]), toQt(pts[2]));
            break;
        case SkPath::kConic_Verb: {
            // Arcs come out as conics, which Qt has no segment for.
            SkPoint quads[1 + 2 * 4];
            const int count = SkPath::ConvertConicToQuads(pts[0], pts[1], pts[2], iter.conicWeight(), quads, 2);
            for (int i = 0; i < count; ++i)
                path.quadTo(toQt(quads[1 + 2 * i]), toQt(quads[2 + 2 * i]));
            break;
        }
        case SkPath::kCubic_Verb:
            path.cubicTo(toQt(pts[1]), toQt(pts[2]), toQt(pts[3]));
            break;
        case SkPath::kClose_Verb:
            path.closeSubpath();
            break;
        default:
            break;
        }
    }
    *out = path;
    return true;
}
#else
bool pathFromData(const QString &, QPainterPath *)
{
    return false;
}
#endif

QVector<double> numbers(const QString &text)
{
    static const QRegularExpression re(QStringLiteral("[-+]?(?:\\d+\\.?\\d*|\\.\\d+)(?:[eE][-+]?\\d+)?"));
    QVector<double> out;
    for (auto it = re.globalMatch(text); it.hasNext();)
        out.append(it.next().captured().toDouble());
    return out;
}

double length(const QString &text, double fallback = 0.0)
{
    const QVector<double> n = numbers(text);
    return n.isEmpty() ? fallback : n.first();
}

QTransform parseTransform(const QString &text)
{
    static const QRegularExpression re(QStringLiteral("(matrix|translate|scale|rotate|skewX|skewY)\\s*\\(([^)]*)\\)"));
    QTransform total;
    for (auto it = re.globalMatch(text); it.hasNext();) {
        const QRegularExpressionMatch m = it.next();
        const QString name = m.captured(1);
        const QVector<double> a = numbers(m.captured(2));
        QTransform t;
        if (name == QLatin1String("matrix") && a.size() >= 6) {
            t = QTransform(a[0], a[1], a[2], a[3], a[4], a[5]);
        } else if (name == QLatin1String("translate") && !a.isEmpty()) {
            t.translate(a[0], a.size() > 1 ? a[1] : 0.0);
        } else if (name == QLatin1String("scale") && !a.isEmpty()) {
            t.scale(a[0], a.size() > 1 ? a[1] : a[0]);
        } else if (name == QLatin1String("rotate") && !a.isEmpty()) {
            if (a.size() >= 3)
                t.translate(a[1], a[2]);
            t.rotate(a[0]);
            if (a.size() >= 3)
                t.translate(-a[1], -a[2]);
        } else if (name == QLatin1String("skewX") && !a.isEmpty()) {
            t = QTransform(1, 0, qTan(qDegreesToRadians(a[0])), 1, 0, 0);
        } else if (name == QLatin1String("skewY") && !a.isEmpty()) {
            t = QTransform(1, qTan(qDegreesToRadians(a[0])), 0, 1, 0, 0);
        }
        // Listed left to right, the leftmost is applied last (outermost).
        total = t * total;
    }
    return total;
}

QString styleProperty(const QXmlStreamAttributes &attrs, const QString &name)
{
    const QString direct = attrs.value(name).toString();
    if (!direct.isEmpty())
        return direct;
    for (const QString &declaration : attrs.value(QStringLiteral("style")).toString().split(u';')) {
        const int colon = declaration.indexOf(u':');
        if (colon > 0 && declaration.left(colon).trimmed() == name)
            return declaration.mid(colon + 1).trimmed();
    }
    return {};
}

bool isSkippedContainer(const QStringView name)
{
    static const QStringList skipped{QStringLiteral("defs"),     QStringLiteral("clipPath"), QStringLiteral("mask"),
                                     QStringLiteral("symbol"),   QStringLiteral("pattern"),  QStringLiteral("marker"),
                                     QStringLiteral("metadata"), QStringLiteral("style"),    QStringLiteral("title"),
                                     QStringLiteral("desc"),     QStringLiteral("script"),   QStringLiteral("filter"),
                                     QStringLiteral("linearGradient"), QStringLiteral("radialGradient")};
    return skipped.contains(name);
}

bool elementPath(const QStringView name, const QXmlStreamAttributes &a, QPainterPath *out)
{
    const auto attr = [&](const char *key) { return length(a.value(QLatin1String(key)).toString()); };
    QPainterPath path;
    if (name == u"path") {
        return pathFromData(a.value(QStringLiteral("d")).toString(), out);
    } else if (name == u"rect") {
        const double w = attr("width");
        const double h = attr("height");
        if (w <= 0.0 || h <= 0.0)
            return false;
        double rx = attr("rx");
        double ry = attr("ry");
        if (!a.hasAttribute(QStringLiteral("rx")))
            rx = ry;
        if (!a.hasAttribute(QStringLiteral("ry")))
            ry = rx;
        rx = qMin(rx, w / 2.0);
        ry = qMin(ry, h / 2.0);
        const QRectF rect(attr("x"), attr("y"), w, h);
        if (rx > 0.0 && ry > 0.0)
            path.addRoundedRect(rect, rx, ry);
        else
            path.addRect(rect);
    } else if (name == u"circle") {
        const double r = attr("r");
        if (r <= 0.0)
            return false;
        path.addEllipse(QPointF(attr("cx"), attr("cy")), r, r);
    } else if (name == u"ellipse") {
        const double rx = attr("rx");
        const double ry = attr("ry");
        if (rx <= 0.0 || ry <= 0.0)
            return false;
        path.addEllipse(QPointF(attr("cx"), attr("cy")), rx, ry);
    } else if (name == u"line") {
        path.moveTo(attr("x1"), attr("y1"));
        path.lineTo(attr("x2"), attr("y2"));
    } else if (name == u"polyline" || name == u"polygon") {
        const QVector<double> n = numbers(a.value(QStringLiteral("points")).toString());
        if (n.size() < 4)
            return false;
        path.moveTo(n[0], n[1]);
        for (int i = 2; i + 1 < n.size(); i += 2)
            path.lineTo(n[i], n[i + 1]);
        if (name == u"polygon")
            path.closeSubpath();
    } else {
        return false;
    }
    *out = path;
    return true;
}

} // namespace

bool importSvgShapeData(const QByteArray &svg, QVector<ShapeContour> *out, Qt::FillRule *rule, double *aspect,
                        QString *error)
{
    const auto fail = [&](const QString &message) {
        if (error)
            *error = message;
        return false;
    };
#ifndef DRIFT_WITH_SKIA
    Q_UNUSED(svg)
    Q_UNUSED(out)
    Q_UNUSED(rule)
    Q_UNUSED(aspect)
    return fail(QStringLiteral("SVG import needs a build with Skia"));
#else
    QXmlStreamReader xml(svg);
    QPainterPath all;
    bool evenOdd = false;
    bool sawRoot = false;
    QList<QTransform> stack{QTransform()};
    QList<bool> hidden{false};

    while (!xml.atEnd()) {
        const QXmlStreamReader::TokenType token = xml.readNext();
        if (token == QXmlStreamReader::EndElement) {
            if (stack.size() > 1) {
                stack.removeLast();
                hidden.removeLast();
            }
            continue;
        }
        if (token != QXmlStreamReader::StartElement)
            continue;

        const QStringView name = xml.name();
        if (!sawRoot) {
            if (name != u"svg")
                return fail(QStringLiteral("not an SVG file"));
            sawRoot = true;
        }
        if (isSkippedContainer(name)) {
            xml.skipCurrentElement();
            continue;
        }

        const QXmlStreamAttributes attrs = xml.attributes();
        const QTransform local = parseTransform(attrs.value(QStringLiteral("transform")).toString());
        const QTransform world = local * stack.last();
        const bool off = hidden.last() || styleProperty(attrs, QStringLiteral("display")) == QLatin1String("none");
        stack.append(world);
        hidden.append(off);

        QPainterPath path;
        if (!off && elementPath(name, attrs, &path)) {
            all.addPath(world.map(path));
            if (styleProperty(attrs, QStringLiteral("fill-rule")) == QLatin1String("evenodd"))
                evenOdd = true;
        }
    }
    if (xml.hasError())
        return fail(xml.errorString());
    if (!sawRoot)
        return fail(QStringLiteral("not an SVG file"));
    if (all.isEmpty())
        return fail(QStringLiteral("the SVG has no shapes"));

    QRectF bounds = all.boundingRect();
    const double extent = qMax(bounds.width(), bounds.height());
    if (extent <= 0.0)
        return fail(QStringLiteral("the SVG has no shapes"));
    // A straight line has no extent across it; give it a thin box so the aspect stays finite.
    const double minExtent = extent * 0.01;
    if (bounds.width() < minExtent)
        bounds.adjust(-(minExtent - bounds.width()) / 2.0, 0.0, (minExtent - bounds.width()) / 2.0, 0.0);
    if (bounds.height() < minExtent)
        bounds.adjust(0.0, -(minExtent - bounds.height()) / 2.0, 0.0, (minExtent - bounds.height()) / 2.0);

    QVector<ShapeContour> contours = contoursFromPainterPath(all, bounds);
    if (contours.isEmpty())
        return fail(QStringLiteral("the SVG has no shapes"));
    *out = contours;
    *rule = evenOdd ? Qt::OddEvenFill : Qt::WindingFill;
    *aspect = bounds.width() / bounds.height();
    return true;
#endif
}

bool importSvgShape(const QString &path, QVector<ShapeContour> *out, Qt::FillRule *rule, double *aspect,
                    QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return importSvgShapeData(file.readAll(), out, rule, aspect, error);
}

bool contoursFromSvgPath(const QString &d, QVector<ShapeContour> *out, Qt::FillRule *rule, QString *error)
{
    QPainterPath path;
    if (!pathFromData(d, &path) || path.isEmpty()) {
        if (error)
            *error = QStringLiteral("not a valid SVG path");
        return false;
    }
    *out = contoursFromPainterPath(path, QRectF(0.0, 0.0, 1.0, 1.0));
    if (out->isEmpty()) {
        if (error)
            *error = QStringLiteral("the path has no segments");
        return false;
    }
    if (rule)
        *rule = Qt::WindingFill;
    return true;
}

} // namespace drift
