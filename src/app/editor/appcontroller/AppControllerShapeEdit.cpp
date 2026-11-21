#include "app/editor/appcontroller/AppControllerInternal.h"
#include "app/editor/ProjectFileController.h"
#include "core/shapes/ShapePath.h"
#include "engine/render2d/shapes/SvgShapeImport.h"
#include "engine/render2d/shapes/UserShapeStore.h"

#include <QFileInfo>
#include <QSet>
#include <QTransform>
#include <QUrl>

using namespace drift::appdetail;
using namespace drift::appcontroller_detail;

namespace {

// A shape's box never gets thinner than this, so a straight line still has a box to select.
constexpr double kMinBoxPx = 2.0;

// The extent of the curves themselves: a tangent handle sticking out does not enlarge the box.
bool contoursBounds(const QVector<drift::ShapeContour> &contours, QRectF *out)
{
    drift::ShapeStyle probe;
    probe.kind = drift::ShapeKind::Path;
    probe.contours = contours;
    const QPainterPath path = drift::shapePath(probe, QRectF(0.0, 0.0, 1.0, 1.0));
    if (path.isEmpty())
        return false;
    *out = path.boundingRect();
    return true;
}

// Widens a flat axis to `minW` x `minH`, around its centre.
QRectF withMinimumExtent(QRectF bounds, double minW, double minH)
{
    if (bounds.width() < minW)
        bounds.adjust(-(minW - bounds.width()) / 2.0, 0.0, (minW - bounds.width()) / 2.0, 0.0);
    if (bounds.height() < minH)
        bounds.adjust(0.0, -(minH - bounds.height()) / 2.0, 0.0, (minH - bounds.height()) / 2.0);
    return bounds;
}

// p' = (p - origin) / size; tangents are offsets, so they only scale.
void renormalize(QVector<drift::ShapeContour> &contours, const QRectF &box)
{
    for (drift::ShapeContour &contour : contours) {
        for (drift::ShapeVertex &v : contour.vertices) {
            v.p = QPointF((v.p.x() - box.left()) / box.width(), (v.p.y() - box.top()) / box.height());
            v.in = QPointF(v.in.x() / box.width(), v.in.y() / box.height());
            v.out = QPointF(v.out.x() / box.width(), v.out.y() / box.height());
        }
    }
}

bool isUnitBox(const QRectF &r)
{
    return qAbs(r.left()) < 1e-9 && qAbs(r.top()) < 1e-9 && qAbs(r.width() - 1.0) < 1e-9 && qAbs(r.height() - 1.0) < 1e-9;
}

drift::TimeUs relativeTime(const drift::Clip &clip, drift::TimeUs playheadUs)
{
    return qBound<drift::TimeUs>(0, playheadUs - clip.timelineStart, clip.timelineDuration);
}

// A key within half a frame of the time is the same key, so it is replaced rather than duplicated.
void writePathKey(drift::ShapeStyle &style, drift::TimeUs at, drift::TimeUs halfFrameUs,
                  const QVector<drift::ShapeContour> &contours)
{
    const auto candidate = style.pathKeys.lowerBound(at - halfFrameUs);
    if (candidate != style.pathKeys.end() && qAbs(candidate.key() - at) <= halfFrameUs)
        candidate.value() = contours;
    else
        style.pathKeys.insert(at, contours);
}

bool isKnobKey(const QString &key)
{
    static const QStringList knobs{QStringLiteral("points"),    QStringLiteral("innerRatio"), QStringLiteral("headSize"),
                                   QStringLiteral("thickness"), QStringLiteral("tailX"),      QStringLiteral("tailSize"),
                                   QStringLiteral("sweep")};
    return knobs.contains(key);
}

// The rect family rounds its own corners, so a baked outline keeps that radius and must not get
// it a second time as the Path's corner effect.
bool roundsItsOwnCorners(drift::ShapeKind kind)
{
    return kind == drift::ShapeKind::RoundedRectangle || kind == drift::ShapeKind::SpeechBubbleRect
           || kind == drift::ShapeKind::Callout || kind == drift::ShapeKind::Ellipse;
}

QString localPath(const QString &pathOrUrl)
{
    return pathOrUrl.startsWith(QLatin1String("file:")) ? QUrl(pathOrUrl).toLocalFile() : pathOrUrl;
}

} // namespace

namespace drift::appcontroller_detail {

// Fits the box to the tight bounds of every contour and key, renormalizing them, and moves the
// frame, anchor and position so each point stays where it was on the canvas. A point is at
// position + R(rotation) * S * (framePoint - anchor), so the frame's top-left moving by n0*F
// shifts the anchor by the same amount (anchors that are keyed), or, for an anchor that follows
// the frame centre, moves the position by R * S * (the centre's shift).
void refitShapeBox(drift::Clip &clip, const QSizeF &canvas, drift::TimeUs relative)
{
    drift::ShapeStyle &style = clip.shapeStyle;
    QRectF bounds;
    bool any = false;
    const auto grow = [&](const QVector<drift::ShapeContour> &contours) {
        QRectF r;
        if (!contoursBounds(contours, &r))
            return;
        bounds = any ? bounds.united(r) : r;
        any = true;
    };
    grow(style.contours);
    for (const auto &key : std::as_const(style.pathKeys))
        grow(key);
    if (!any)
        return;

    const drift::ClipLayout layout = drift::clipLayoutAt(clip, relative, canvas);
    const double onScreenW = layout.frameW * qMax(1e-6, qAbs(layout.scaleX));
    const double onScreenH = layout.frameH * qMax(1e-6, qAbs(layout.scaleY));
    bounds = withMinimumExtent(bounds, kMinBoxPx / onScreenW, kMinBoxPx / onScreenH);
    if (isUnitBox(bounds))
        return;

    renormalize(style.contours, bounds);
    for (auto it = style.pathKeys.begin(); it != style.pathKeys.end(); ++it)
        renormalize(it.value(), bounds);

    const QSizeF frame(layout.frameW, layout.frameH);
    const bool followX = clip.anchorX.isEmpty();
    const bool followY = clip.anchorY.isEmpty();
    const QPointF shift(followX ? (bounds.center().x() - 0.5) * frame.width() : 0.0,
                        followY ? (bounds.center().y() - 0.5) * frame.height() : 0.0);

    struct Move
    {
        drift::TimeUs at;
        QPointF position;
    };
    QList<Move> moves;
    if (!shift.isNull()) {
        QSet<drift::TimeUs> times;
        for (auto it = clip.positionX.keyframes().cbegin(); it != clip.positionX.keyframes().cend(); ++it)
            times.insert(it.key());
        for (auto it = clip.positionY.keyframes().cbegin(); it != clip.positionY.keyframes().cend(); ++it)
            times.insert(it.key());
        if (times.isEmpty())
            times.insert(0);
        for (const drift::TimeUs at : std::as_const(times)) {
            const drift::ClipLayout there = drift::clipLayoutAt(clip, at, canvas);
            QTransform turn;
            turn.rotate(there.rotation);
            const QPointF d = turn.map(QPointF(shift.x() * there.scaleX, shift.y() * there.scaleY));
            moves.append({at, QPointF(there.posX + d.x(), there.posY + d.y())});
        }
    }
    const auto shiftAnchor = [](drift::KeyframeTrack<double> &track, double by) {
        const QList<drift::TimeUs> times = track.keyframes().keys();
        for (const drift::TimeUs at : times)
            track.keyframeRef(at)->value -= by;
    };
    if (!followX)
        shiftAnchor(clip.anchorX, bounds.left() * frame.width());
    if (!followY)
        shiftAnchor(clip.anchorY, bounds.top() * frame.height());
    for (const Move &move : moves) {
        clip.positionX.setKeyframe(move.at, move.position.x());
        clip.positionY.setKeyframe(move.at, move.position.y());
    }
    clip.frameW = frame.width() * bounds.width();
    clip.frameH = frame.height() * bounds.height();
}

} // namespace drift::appcontroller_detail

QVariantList AppController::setShapeContours(int trackIndex, int clipIndex, const QVariantList &list, bool commit)
{
    if (!isValidClipIndex(trackIndex, clipIndex))
        return {};
    const drift::Clip &probe = m_project.tracks().at(trackIndex).clips.at(clipIndex);
    if (probe.type != drift::ClipType::Shape || probe.shapeStyle.kind != drift::ShapeKind::Path)
        return {};
    const QVector<drift::ShapeContour> contours = shapeContoursFromVariant(list);
    if (contours.isEmpty())
        return shapeContoursAt(trackIndex, clipIndex);

    if (!commit && !m_previewDragActive)
        beginPreviewDrag(tr("Edit shape path"));
    const bool dragging = m_previewDragActive;
    // Snapshot before taking any reference into m_project (see setShapeStyle). Not worth a
    // copy per pointer move when the drag already holds one.
    const drift::Project before = dragging ? drift::Project() : m_project;

    drift::Clip &clip = m_project.tracks()[trackIndex].clips[clipIndex];
    const drift::TimeUs relative = relativeTime(clip, m_playheadUs);
    if (clip.shapeStyle.pathKeys.isEmpty())
        clip.shapeStyle.contours = contours;
    else
        writePathKey(clip.shapeStyle, relative, drift::frameDurationUs(m_projectFile->projectFps()) / 2, contours);
    refitShapeBox(clip, QSizeF(m_project.width(), m_project.height()), relative);
    const QVariantList stored = shapeContoursToVariant(clip.shapeStyle.resolvedAt(relative).contours);

    if (dragging) {
        emitPreviewEdit(trackIndex, clipIndex, {QStringLiteral("shape.contours")});
        if (commit)
            commitPreviewDrag();
        return stored;
    }
    pushProjectEdit(before, tr("Edit shape path"));
    finishEdit(tr("Shape path changed"));
    return stored;
}

QVariantList AppController::shapeContoursAt(int trackIndex, int clipIndex) const
{
    if (!isValidClipIndex(trackIndex, clipIndex))
        return {};
    const drift::Clip &clip = m_project.tracks().at(trackIndex).clips.at(clipIndex);
    if (clip.type != drift::ClipType::Shape || clip.shapeStyle.kind != drift::ShapeKind::Path)
        return {};
    return shapeContoursToVariant(clip.shapeStyle.resolvedAt(relativeTime(clip, m_playheadUs)).contours);
}

void AppController::setShapePathKeyed(int trackIndex, int clipIndex, bool keyed)
{
    if (!isValidClipIndex(trackIndex, clipIndex))
        return;
    const drift::Clip &probe = m_project.tracks().at(trackIndex).clips.at(clipIndex);
    if (probe.type != drift::ClipType::Shape || probe.shapeStyle.kind != drift::ShapeKind::Path
        || keyed == !probe.shapeStyle.pathKeys.isEmpty())
        return;

    const drift::Project before = m_project;
    drift::Clip &clip = m_project.tracks()[trackIndex].clips[clipIndex];
    const drift::TimeUs relative = relativeTime(clip, m_playheadUs);
    if (keyed) {
        clip.shapeStyle.pathKeys.insert(relative, clip.shapeStyle.contours);
    } else {
        clip.shapeStyle.contours = clip.shapeStyle.resolvedAt(relative).contours;
        clip.shapeStyle.pathKeys.clear();
    }
    pushProjectEdit(before, keyed ? tr("Animate shape path") : tr("Stop animating shape path"));
    finishEdit(keyed ? tr("Shape path animated") : tr("Shape path static"));
}

void AppController::removeShapePathKey(int trackIndex, int clipIndex, double atSeconds)
{
    if (!isValidClipIndex(trackIndex, clipIndex))
        return;
    const drift::Clip &clip = m_project.tracks().at(trackIndex).clips.at(clipIndex);
    if (clip.type != drift::ClipType::Shape)
        return;
    const drift::TimeUs halfFrame = drift::frameDurationUs(m_projectFile->projectFps()) / 2;
    const drift::TimeUs at = drift::secondsToUs(atSeconds) - clip.timelineStart;
    const auto found = clip.shapeStyle.pathKeys.lowerBound(at - halfFrame);
    if (found == clip.shapeStyle.pathKeys.constEnd() || qAbs(found.key() - at) > halfFrame)
        return;

    const drift::Project before = m_project;
    drift::ShapeStyle &style = m_project.tracks()[trackIndex].clips[clipIndex].shapeStyle;
    const auto doomed = style.pathKeys.lowerBound(at - halfFrame);
    // Deleting the last key would otherwise snap the shape back to the stale base contours.
    if (style.pathKeys.size() == 1)
        style.contours = doomed.value();
    style.pathKeys.erase(doomed);
    pushProjectEdit(before, tr("Delete shape key"));
    finishEdit(tr("Shape path key removed"));
}

void AppController::convertShapeToPath(int trackIndex, int clipIndex)
{
    if (!isValidClipIndex(trackIndex, clipIndex))
        return;
    const drift::Clip &probe = m_project.tracks().at(trackIndex).clips.at(clipIndex);
    if (probe.type != drift::ClipType::Shape || probe.shapeStyle.kind == drift::ShapeKind::Path)
        return;

    const drift::Project before = m_project;
    drift::Clip &clip = m_project.tracks()[trackIndex].clips[clipIndex];
    const drift::TimeUs relative = relativeTime(clip, m_playheadUs);
    const drift::ClipLayout layout = drift::clipLayoutAt(clip, relative, QSizeF(m_project.width(), m_project.height()));
    // The painter lays the outline into the scaled box, so the corner radius reads against that.
    const QRectF box(0.0, 0.0, layout.frameW * qMax(1e-6, qAbs(layout.scaleX)),
                     layout.frameH * qMax(1e-6, qAbs(layout.scaleY)));
    const drift::ShapeStyle resolved = clip.shapeStyle.resolvedAt(relative);
    const QPainterPath outline = drift::shapePath(resolved, box);
    const QVector<drift::ShapeContour> contours = drift::contoursFromPainterPath(outline, box);
    if (contours.isEmpty()) {
        setLastMessage(tr("This shape has no outline to edit"), QStringLiteral("error"));
        return;
    }

    drift::ShapeStyle &style = clip.shapeStyle;
    const bool ownCorners = roundsItsOwnCorners(style.kind);
    style.kind = drift::ShapeKind::Path;
    style.contours = contours;
    style.fillRule = outline.fillRule();
    style.pathKeys.clear();
    for (auto it = style.keyframes.begin(); it != style.keyframes.end();) {
        if (isKnobKey(it.key()) || (ownCorners && it.key() == QLatin1String("cornerRadius")))
            it = style.keyframes.erase(it);
        else
            ++it;
    }
    if (ownCorners)
        style.cornerRadius = 0.0;

    pushProjectEdit(before, tr("Make shape editable"));
    finishEdit(tr("Shape is now editable"));
}

QVariantMap AppController::addPathShapeClip(const QVariantList &canvasContours, int trackIndex, double atSeconds)
{
    QVector<drift::ShapeContour> contours = shapeContoursFromVariant(canvasContours);
    QRectF bounds;
    if (!contoursBounds(contours, &bounds))
        return {};
    bounds = withMinimumExtent(bounds, kMinBoxPx, kMinBoxPx);
    renormalize(contours, bounds);

    const drift::ClipRef ref = placeShapeClip(drift::pathShapeStyle(contours, Qt::WindingFill), tr("Path"), 1.0, bounds,
                                              trackIndex, atSeconds, tr("Shape added"));
    if (ref.trackIndex < 0)
        return {};
    return {{QStringLiteral("track"), ref.trackIndex}, {QStringLiteral("clip"), ref.clipIndex}};
}

QString AppController::saveShapeToLibrary(int trackIndex, int clipIndex, const QString &name)
{
    if (!isValidClipIndex(trackIndex, clipIndex))
        return {};
    const drift::Clip &clip = m_project.tracks().at(trackIndex).clips.at(clipIndex);
    if (clip.type != drift::ClipType::Shape)
        return {};

    const drift::TimeUs relative = relativeTime(clip, m_playheadUs);
    const drift::ClipLayout layout = drift::clipLayoutAt(clip, relative, QSizeF(m_project.width(), m_project.height()));
    const QSizeF box(layout.frameW * qMax(1e-6, qAbs(layout.scaleX)), layout.frameH * qMax(1e-6, qAbs(layout.scaleY)));
    const drift::ShapeStyle resolved = clip.shapeStyle.resolvedAt(relative);
    QVector<drift::ShapeContour> contours = resolved.contours;
    Qt::FillRule rule = resolved.fillRule;
    if (resolved.kind != drift::ShapeKind::Path) {
        const QRectF full(0.0, 0.0, box.width(), box.height());
        const QPainterPath outline = drift::shapePath(resolved, full);
        contours = drift::contoursFromPainterPath(outline, full);
        rule = outline.fillRule();
    }

    // The library box is the outline's own, so a pie or a path that no longer touches its frame
    // does not keep empty margins.
    QRectF bounds;
    if (!contoursBounds(contours, &bounds)) {
        setLastMessage(tr("This shape has no outline to save"), QStringLiteral("error"));
        return {};
    }
    bounds = withMinimumExtent(bounds, kMinBoxPx / box.width(), kMinBoxPx / box.height());
    renormalize(contours, bounds);
    const double aspect = (box.width() * bounds.width()) / (box.height() * bounds.height());

    const QString label = name.trimmed().isEmpty() ? clip.name : name;
    const QString id = drift::UserShapeStore::instance().add(label, contours, rule, aspect);
    if (id.isEmpty()) {
        setLastMessage(tr("Could not save the shape"), QStringLiteral("error"));
        return {};
    }
    emit userShapesChanged();
    setLastMessage(tr("Saved to My shapes"), QStringLiteral("success"));
    return id;
}

bool AppController::renameUserShape(const QString &shapeId, const QString &name)
{
    if (!drift::UserShapeStore::instance().rename(shapeId, name))
        return false;
    emit userShapesChanged();
    return true;
}

bool AppController::deleteUserShape(const QString &shapeId)
{
    if (!drift::UserShapeStore::instance().remove(shapeId))
        return false;
    emit userShapesChanged();
    return true;
}

QString AppController::importSvgToLibrary(const QString &path)
{
    QString error;
    const QString id = drift::UserShapeStore::instance().addFromSvgFile(localPath(path), &error);
    if (id.isEmpty()) {
        setLastMessage(tr("Could not import the SVG: %1").arg(error), QStringLiteral("error"));
        return {};
    }
    emit userShapesChanged();
    setLastMessage(tr("Added to My shapes"), QStringLiteral("success"));
    return id;
}

QVariantMap AppController::importSvgAsShapeClip(const QString &path, int trackIndex, double atSeconds)
{
    const QString file = localPath(path);
    QVector<drift::ShapeContour> contours;
    Qt::FillRule rule = Qt::WindingFill;
    double aspect = 1.0;
    QString error;
    if (!drift::importSvgShape(file, &contours, &rule, &aspect, &error)) {
        setLastMessage(tr("Could not import the SVG: %1").arg(error), QStringLiteral("error"));
        return {};
    }
    const drift::ClipRef ref = placeShapeClip(drift::pathShapeStyle(contours, rule), QFileInfo(file).completeBaseName(), aspect,
                                              {}, trackIndex, atSeconds, tr("Shape added"));
    if (ref.trackIndex < 0)
        return {};
    return {{QStringLiteral("track"), ref.trackIndex}, {QStringLiteral("clip"), ref.clipIndex}};
}
