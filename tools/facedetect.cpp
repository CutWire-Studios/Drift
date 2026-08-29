// Headless face-landmark check: runs FaceLandmarker over one frame and writes an annotated PNG
// plus the anchors as JSON. The overlay is the only practical way to confirm the ROI affine and
// the MediaPipe landmark indices are right, so keep it working.
//
// With --effect it also bakes a one-frame track and pushes the frame through the real effect
// chain, which is how the face shaders get checked without launching the app.
//
//   facedetect <image-or-video> [--time <seconds>] [--out overlay.png]
//              [--effect <id>] [--param k=v] [--effect-out warped.png]

#include "engine/ClipReaderPool.h"
#include "engine/EffectCatalog.h"
#include "engine/EffectProcessor.h"
#include "engine/FaceLandmarker.h"
#include "engine/FaceTrack.h"
#include "core/Effect.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QPolygonF>
#include <QQuaternion>
#include <QTextStream>
#include <QVector3D>

namespace {

void mark(QPainter &p, const QPointF &uv, const QSize &size, const QColor &color,
          const QString &label)
{
    const QPointF px(uv.x() * size.width(), uv.y() * size.height());
    p.setPen(QPen(color, 2.0));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(px, 4.0, 4.0);
    p.drawText(px + QPointF(7, -7), label);
}

// Contours arrive width-normalized, so y divides back out by the aspect before it is a pixel.
QPointF contourPx(const QPointF &wn, const QSize &size)
{
    const double aspect = double(size.height()) / double(size.width());
    return QPointF(wn.x() * size.width(), (wn.y() / aspect) * size.height());
}

// Draws one loop and numbers its first few vertices, which is what makes a wrong index set or a
// swapped left/right pair visible rather than merely plausible.
void drawLoop(QPainter &p, const QList<QPointF> &contour, drift::contour::Span span,
              const QSize &size, const QColor &color, const QString &label)
{
    QPolygonF poly;
    poly.reserve(span.count);
    for (int i = 0; i < span.count; ++i)
        poly.append(contourPx(contour.at(span.offset + i), size));

    p.setPen(QPen(color, 2.0));
    p.setBrush(Qt::NoBrush);
    p.drawPolygon(poly);

    // Vertex 0 filled, then every fourth numbered: enough to read the winding direction off the
    // image without burying the face in text.
    p.setBrush(color);
    p.drawEllipse(poly.first(), 4.0, 4.0);
    p.setBrush(Qt::NoBrush);
    p.drawText(poly.first() + QPointF(6, -6), label + QStringLiteral(" 0"));
    for (int i = 4; i < span.count; i += 4)
        p.drawText(poly.at(i) + QPointF(4, -4), QString::number(i));
}

QJsonArray loopToJson(const QList<QPointF> &contour, drift::contour::Span span)
{
    QJsonArray out;
    for (int i = 0; i < span.count; ++i) {
        const QPointF &q = contour.at(span.offset + i);
        out.append(QJsonArray{q.x(), q.y()});
    }
    return out;
}

// Rotates a vector by the pose quaternion. Only used to turn the stored quaternion back into the
// basis it came from, which is exactly the round trip that catches a sign error.
QVector3D rotateByPose(const drift::FaceAnchors &a, const QVector3D &v)
{
    const QQuaternion q(float(a.poseQw), float(a.poseQx), float(a.poseQy), float(a.poseQz));
    return q.rotatedVector(v);
}

} // namespace

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QTextStream out(stdout);
    QTextStream err(stderr);

    const QStringList args = QCoreApplication::arguments();
    if (args.size() < 2) {
        err << "usage: facedetect <image-or-video> [--time <seconds>] [--out overlay.png]\n";
        return 2;
    }

    const QString input = args.at(1);
    double seconds = 0.0;
    QString overlayPath = QStringLiteral("facedetect-overlay.png");
    QString effectId;
    QString effectOut = QStringLiteral("facedetect-effect.png");
    QMap<QString, QVariant> overrides;
    for (int i = 2; i + 1 < args.size(); i += 2) {
        const QString key = args.at(i);
        const QString value = args.at(i + 1);
        if (key == QLatin1String("--time"))
            seconds = value.toDouble();
        else if (key == QLatin1String("--out"))
            overlayPath = value;
        else if (key == QLatin1String("--effect"))
            effectId = value;
        else if (key == QLatin1String("--effect-out"))
            effectOut = value;
        else if (key == QLatin1String("--param") && value.contains(QLatin1Char('=')))
            overrides.insert(value.section(QLatin1Char('='), 0, 0),
                             value.section(QLatin1Char('='), 1));
    }

    QImage frame(input);
    if (frame.isNull()) {
        frame = ClipReaderPool::instance().readVideoFrame(
            input, 1, drift::TimeUs(seconds * drift::kUsPerSecond), 1920, 1080);
    }
    if (frame.isNull()) {
        err << "could not read a frame from " << input << "\n";
        return 1;
    }

    drift::FaceLandmarker &fl = drift::FaceLandmarker::instance();
    if (!fl.available()) {
        err << fl.lastError() << "\n";
        return 1;
    }

    const QList<drift::FaceAnchors> faces = fl.detect(frame);
    out << "frame " << frame.width() << "x" << frame.height() << ", faces: " << faces.size()
        << "\n";

    QImage overlay = frame.convertToFormat(QImage::Format_RGBA8888);
    QPainter p(&overlay);
    p.setRenderHint(QPainter::Antialiasing, true);
    QFont font = p.font();
    font.setPixelSize(qMax(10, overlay.height() / 60));
    p.setFont(font);

    QJsonArray json;
    for (int i = 0; i < faces.size(); ++i) {
        const drift::FaceAnchors &a = faces.at(i);
        out << "  face " << i << " valid=" << a.valid << " score=" << a.score
            << " angle=" << (a.angle * 180.0 / M_PI) << "deg rx=" << a.faceRx << " ry=" << a.faceRy
            << " eyeR=" << a.eyeRadius << " contours=" << a.hasContours << " pose=" << a.hasPose
            << "\n";
        if (!a.valid)
            continue;

        mark(p, a.leftEye, overlay.size(), Qt::cyan, QStringLiteral("Leye"));
        mark(p, a.rightEye, overlay.size(), Qt::cyan, QStringLiteral("Reye"));
        mark(p, a.noseTip, overlay.size(), Qt::yellow, QStringLiteral("nose"));
        mark(p, a.mouthCenter, overlay.size(), Qt::magenta, QStringLiteral("mouth"));
        mark(p, a.mouthLeft, overlay.size(), Qt::magenta, QStringLiteral("mL"));
        mark(p, a.mouthRight, overlay.size(), Qt::magenta, QStringLiteral("mR"));
        mark(p, a.chin, overlay.size(), Qt::green, QStringLiteral("chin"));
        mark(p, a.forehead, overlay.size(), Qt::green, QStringLiteral("brow"));
        mark(p, a.faceCenter, overlay.size(), Qt::red, QStringLiteral("c"));

        // The oval, drawn in the same width-normalized space the shaders will use.
        p.save();
        p.translate(a.faceCenter.x() * overlay.width(), a.faceCenter.y() * overlay.height());
        p.rotate(a.angle * 180.0 / M_PI);
        p.setPen(QPen(Qt::red, 2.0));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QPointF(0, 0), a.faceRx * overlay.width(), a.faceRy * overlay.width());
        p.restore();

        QJsonObject o;
        o[QStringLiteral("score")] = a.score;
        o[QStringLiteral("angleDeg")] = a.angle * 180.0 / M_PI;
        o[QStringLiteral("faceRx")] = a.faceRx;
        o[QStringLiteral("faceRy")] = a.faceRy;
        o[QStringLiteral("eyeRadius")] = a.eyeRadius;

        if (a.hasContours) {
            using namespace drift::contour;
            drawLoop(p, a.contour, kLipOuter, overlay.size(), QColor(255, 80, 120),
                     QStringLiteral("lipOut"));
            drawLoop(p, a.contour, kLipInner, overlay.size(), QColor(255, 180, 60),
                     QStringLiteral("lipIn"));
            drawLoop(p, a.contour, kEyeLeft, overlay.size(), QColor(80, 220, 255),
                     QStringLiteral("eyeL"));
            drawLoop(p, a.contour, kEyeRight, overlay.size(), QColor(80, 140, 255),
                     QStringLiteral("eyeR"));
            drawLoop(p, a.contour, kBrowLeft, overlay.size(), QColor(160, 255, 120),
                     QStringLiteral("browL"));
            drawLoop(p, a.contour, kBrowRight, overlay.size(), QColor(90, 200, 90),
                     QStringLiteral("browR"));
            mark(p, a.cheekLeft, overlay.size(), QColor(255, 140, 200), QStringLiteral("chkL"));
            mark(p, a.cheekRight, overlay.size(), QColor(255, 140, 200), QStringLiteral("chkR"));

            QJsonObject loops;
            loops[QStringLiteral("oval")] = loopToJson(a.contour, kOval);
            loops[QStringLiteral("lipOuter")] = loopToJson(a.contour, kLipOuter);
            loops[QStringLiteral("lipInner")] = loopToJson(a.contour, kLipInner);
            loops[QStringLiteral("eyeLeft")] = loopToJson(a.contour, kEyeLeft);
            loops[QStringLiteral("eyeRight")] = loopToJson(a.contour, kEyeRight);
            loops[QStringLiteral("browLeft")] = loopToJson(a.contour, kBrowLeft);
            loops[QStringLiteral("browRight")] = loopToJson(a.contour, kBrowRight);
            o[QStringLiteral("contours")] = loops;
        }

        if (a.hasPose) {
            const QVector3D right = rotateByPose(a, QVector3D(1, 0, 0));
            const QVector3D up = rotateByPose(a, QVector3D(0, 1, 0));
            const QVector3D forward = rotateByPose(a, QVector3D(0, 0, 1));

            // Axes drawn from the eye midpoint, one interocular distance long. On a frontal face
            // right must run toward increasing x, up must point at the forehead (so *upward* on
            // screen, i.e. decreasing y), and forward must be close to +/-z. Which sign forward
            // takes is the thing to read off here — it decides whether a 3D prop faces the camera.
            const QPointF originUv = contourPx(QPointF(a.poseOx, a.poseOy), overlay.size());
            const double len = a.poseScale * overlay.width();
            const struct { QVector3D axis; QColor color; const char *name; } axes[] = {
                {right, QColor(255, 60, 60), "R"},
                {up, QColor(60, 255, 60), "U"},
                {forward, QColor(80, 120, 255), "F"},
            };
            const double aspect = double(overlay.height()) / double(overlay.width());
            for (const auto &ax : axes) {
                const QPointF tip = originUv
                    + QPointF(ax.axis.x() * len, ax.axis.y() * len / aspect);
                p.setPen(QPen(ax.color, 3.0));
                p.drawLine(originUv, tip);
                p.drawText(tip + QPointF(5, -5), QLatin1String(ax.name));
            }

            // Roll from the quaternion against the independently derived eye-line angle. They come
            // from different vertices (eye corners vs iris centres) so they will not match exactly,
            // but a large gap means a sign is wrong somewhere.
            const double roll = std::atan2(right.y(), right.x());
            out << "    pose fwd=(" << forward.x() << ", " << forward.y() << ", " << forward.z()
                << ") roll=" << (roll * 180.0 / M_PI) << "deg vs angle="
                << (a.angle * 180.0 / M_PI) << "deg scale=" << a.poseScale << "\n";

            QJsonObject pose;
            pose[QStringLiteral("right")] = QJsonArray{right.x(), right.y(), right.z()};
            pose[QStringLiteral("up")] = QJsonArray{up.x(), up.y(), up.z()};
            pose[QStringLiteral("forward")] = QJsonArray{forward.x(), forward.y(), forward.z()};
            pose[QStringLiteral("origin")] = QJsonArray{a.poseOx, a.poseOy, a.poseOz};
            pose[QStringLiteral("scale")] = a.poseScale;
            pose[QStringLiteral("rollDeg")] = roll * 180.0 / M_PI;
            o[QStringLiteral("pose")] = pose;
        }

        json.append(o);
    }
    p.end();

    if (!overlay.save(overlayPath)) {
        err << "could not write " << overlayPath << "\n";
        return 1;
    }
    out << "wrote " << overlayPath << "\n"
        << QJsonDocument(json).toJson(QJsonDocument::Compact) << "\n";

    if (effectId.isEmpty())
        return 0;

    reloadEffectCatalog();
    const EffectPresetEntry *def = effectDefForId(effectId);
    if (!def) {
        err << "unknown effect '" << effectId << "'\n";
        return 1;
    }
    if (!def->needsFace)
        err << "note: " << effectId << " does not declare requires:face\n";

    // Bake the same anchors into two frames and sample between them, so this also exercises
    // FaceTrack::sample's interpolation rather than only the exact-frame path.
    drift::FaceTrack track;
    track.fps = 30;
    drift::FaceTrackFrame baked;
    baked.faces = faces;
    track.frames = {baked, baked};

    drift::Effect effect;
    effect.catalogId = effectId;
    for (auto it = overrides.cbegin(); it != overrides.cend(); ++it)
        effect.parameters.insert(it.key(), it.value());

    const QImage warped = EffectProcessor::applyEffects(
        frame.convertToFormat(QImage::Format_RGBA8888), {effect}, 0,
        track.sampleAll(drift::kUsPerSecond / 60));
    if (warped.isNull() || !warped.save(effectOut)) {
        err << "could not render " << effectId << "\n";
        return 1;
    }
    out << "wrote " << effectOut << " (" << effectId << ")\n";
    return 0;
}
