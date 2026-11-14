#include "app/editor/sessions/CurveEditorController.h"

#include "app/editor/AppController.h"
#include "app/editor/AppControllerDetail.h"
#include "engine/packages/transitions/TransitionCatalog.h"

CurveEditorController::CurveEditorController(AppController *app, QObject *parent)
    : QObject(parent)
    , m_app(app)
{
}

namespace {

drift::ClipAnimation &edgeAnim(drift::Clip &clip, bool in) { return in ? clip.animIn : clip.animOut; }
// anim selects the edge's animation slot instead of its fade; both carry a curve and a shape.
drift::FadeCurve &edgeCurve(drift::Clip &clip, bool in, bool anim)
{
    if (anim)
        return edgeAnim(clip, in).curve;
    return in ? clip.fadeInCurve : clip.fadeOutCurve;
}
drift::FadeShape &edgeShape(drift::Clip &clip, bool in, bool anim)
{
    if (anim)
        return edgeAnim(clip, in).shape;
    return in ? clip.fadeInShape : clip.fadeOutShape;
}

} // namespace

void CurveEditorController::beginFade(int trackIndex, int clipIndex, const QString &edge, const QString &target)
{
    if (trackIndex < 0 || trackIndex >= m_app->m_project.tracks().size())
        return;
    const drift::Track &track = m_app->m_project.tracks().at(trackIndex);
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    const bool anim = target == QLatin1String("animation");
    if (anim && (track.clips.at(clipIndex).type == drift::ClipType::Audio
                 || track.clips.at(clipIndex).type == drift::ClipType::Subtitle))
        return;

    if (m_fadeActive)
        endFade();

    drift::Clip &clip = m_app->m_project.tracks()[trackIndex].clips[clipIndex];
    m_fadeIn = edge != QLatin1String("out");
    m_fadeAnim = anim;
    const drift::FadeCurve current = edgeCurve(clip, m_fadeIn, m_fadeAnim);
    const drift::FadeShape currentShape = edgeShape(clip, m_fadeIn, m_fadeAnim);
    m_fadeTrack = trackIndex;
    m_fadeClipIndex = clipIndex;
    m_fadeClipId = clip.id;
    m_fadeClipName = clip.name;
    m_fadeBefore = current;
    m_fadeShapeBefore = currentShape;
    m_fadeApplied = false;

    m_fadeMode = current == drift::FadeCurve::Bezier ? drift::FadeCurve::Bezier
                                                     : drift::FadeCurve::Custom;
    if (current == drift::FadeCurve::Bezier)
        m_fadeShape = currentShape.hasHandles() ? currentShape
                                                : drift::FadeShape::bezierPreset(QString());
    else if (current == drift::FadeCurve::Custom && !currentShape.isEmpty())
        m_fadeShape = currentShape;
    else if (current == drift::FadeCurve::Linear)
        m_fadeShape = drift::FadeShape::linearPreset();
    else if (current == drift::FadeCurve::EqualPower)
        m_fadeShape = drift::FadeShape::equalPowerPreset();
    else
        m_fadeShape = drift::FadeShape::smoothPreset();

    edgeCurve(clip, m_fadeIn, m_fadeAnim) = m_fadeMode;
    edgeShape(clip, m_fadeIn, m_fadeAnim) = m_fadeShape;
    drift::appdetail::syncLinkedPartnersFrom(m_app->m_project, clip);
    m_fadeActive = true;
    emit fadeSessionChanged();
    emit fadeChanged();
    m_app->emitPreviewFrame();
}

void CurveEditorController::endFade()
{
    if (!m_fadeActive)
        return;

    if (!m_fadeApplied
        && m_fadeTrack >= 0 && m_fadeTrack < m_app->m_project.tracks().size()) {
        drift::Track &track = m_app->m_project.tracks()[m_fadeTrack];
        if (m_fadeClipIndex >= 0 && m_fadeClipIndex < track.clips.size()
            && track.clips.at(m_fadeClipIndex).id == m_fadeClipId) {
            drift::Clip &clip = track.clips[m_fadeClipIndex];
            edgeCurve(clip, m_fadeIn, m_fadeAnim) = m_fadeBefore;
            edgeShape(clip, m_fadeIn, m_fadeAnim) = m_fadeShapeBefore;
            drift::appdetail::syncLinkedPartnersFrom(m_app->m_project, clip);
            m_app->emitPreviewFrame();
        }
    }

    m_fadeActive = false;
    m_fadeAnim = false;
    m_fadeTrack = -1;
    m_fadeClipIndex = -1;
    m_fadeClipId.clear();
    m_fadeClipName.clear();
    m_fadeShape.clear();
    m_fadeShapeBefore.clear();
    m_fadeApplied = false;
    emit fadeSessionChanged();
    emit fadeChanged();
}

QVariantList CurveEditorController::fadePoints() const
{
    QVariantList out;
    for (const QPointF &pt : m_fadeShape.points()) {
        out.append(QVariantMap{
            {QStringLiteral("t"), pt.x()},
            {QStringLiteral("g"), pt.y()},
        });
    }
    return out;
}

void CurveEditorController::setFadePoints(const QVariantList &points)
{
    if (!m_fadeActive)
        return;
    if (m_fadeTrack < 0 || m_fadeTrack >= m_app->m_project.tracks().size())
        return;
    drift::Track &track = m_app->m_project.tracks()[m_fadeTrack];
    if (m_fadeClipIndex < 0 || m_fadeClipIndex >= track.clips.size())
        return;
    drift::Clip &clip = track.clips[m_fadeClipIndex];
    if (clip.id != m_fadeClipId)
        return;

    QList<QPointF> parsed;
    parsed.reserve(points.size());
    for (const QVariant &entry : points) {
        const QVariantMap map = entry.toMap();
        parsed.append(QPointF(map.value(QStringLiteral("t")).toDouble(),
                              map.value(QStringLiteral("g")).toDouble()));
    }
    m_fadeShape.setPoints(parsed);
    m_fadeMode = drift::FadeCurve::Custom;
    edgeCurve(clip, m_fadeIn, m_fadeAnim) = drift::FadeCurve::Custom;
    edgeShape(clip, m_fadeIn, m_fadeAnim) = m_fadeShape;
    drift::appdetail::syncLinkedPartnersFrom(m_app->m_project, clip);
    emit fadeChanged();
    m_app->emitPreviewFrame();
}

// --- transition progress curve -------------------------------------------------------------
//
// Mirrors the clip fade-curve session: the candidate shape is auditioned on the live transition
// so the preview updates as the curve is dragged, and either applyTransition() commits it
// or endTransition() puts the previous curve back.

void CurveEditorController::beginTransition(int trackIndex, const QString &transitionId)
{
    if (trackIndex < 0 || trackIndex >= m_app->m_project.tracks().size())
        return;
    drift::Transition *transition = drift::appdetail::findTransition(m_app->m_project.tracks()[trackIndex], transitionId);
    if (!transition)
        return;

    if (m_transitionActive)
        endTransition();

    m_transitionTrack = trackIndex;
    m_transitionId = transitionId;
    const TransitionPresetEntry *def = transitionDefForId(transition->kindId);
    m_transitionName = def ? def->meta.displayName : transition->kindId;
    m_transitionBefore = transition->easingCurve;
    m_transitionShapeBefore = transition->easingShape;
    m_transitionApplied = false;

    // Seed the editor from whatever the transition is using now, so opening Custom on a Smooth
    // transition starts from that shape rather than snapping to a straight line.
    m_transitionMode = transition->easingCurve == drift::FadeCurve::Bezier
        ? drift::FadeCurve::Bezier
        : drift::FadeCurve::Custom;
    if (transition->easingCurve == drift::FadeCurve::Bezier)
        m_transitionShape = transition->easingShape.hasHandles()
            ? transition->easingShape
            : drift::FadeShape::bezierPreset(QString());
    else if (transition->easingCurve == drift::FadeCurve::Custom && !transition->easingShape.isEmpty())
        m_transitionShape = transition->easingShape;
    else if (transition->easingCurve == drift::FadeCurve::Smooth)
        m_transitionShape = drift::FadeShape::smoothPreset();
    else if (transition->easingCurve == drift::FadeCurve::EqualPower)
        m_transitionShape = drift::FadeShape::equalPowerPreset();
    else
        m_transitionShape = drift::FadeShape::linearPreset();

    transition->easingCurve = m_transitionMode;
    transition->easingShape = m_transitionShape;
    m_transitionActive = true;
    emit transitionSessionChanged();
    emit transitionChanged();
    m_app->emitPreviewFrame();
}

void CurveEditorController::endTransition()
{
    if (!m_transitionActive)
        return;

    if (!m_transitionApplied && m_transitionTrack >= 0
        && m_transitionTrack < m_app->m_project.tracks().size()) {
        drift::Transition *transition =
            drift::appdetail::findTransition(m_app->m_project.tracks()[m_transitionTrack], m_transitionId);
        if (transition) {
            transition->easingCurve = m_transitionBefore;
            transition->easingShape = m_transitionShapeBefore;
            m_app->emitPreviewFrame();
        }
    }

    m_transitionActive = false;
    m_transitionTrack = -1;
    m_transitionId.clear();
    m_transitionName.clear();
    m_transitionShape.clear();
    m_transitionShapeBefore.clear();
    m_transitionApplied = false;
    emit transitionSessionChanged();
    emit transitionChanged();
}

QVariantList CurveEditorController::transitionPoints() const
{
    QVariantList out;
    for (const QPointF &pt : m_transitionShape.points()) {
        out.append(QVariantMap{
            {QStringLiteral("t"), pt.x()},
            {QStringLiteral("g"), pt.y()},
        });
    }
    return out;
}

void CurveEditorController::setTransitionPoints(const QVariantList &points)
{
    if (!m_transitionActive)
        return;
    if (m_transitionTrack < 0 || m_transitionTrack >= m_app->m_project.tracks().size())
        return;
    drift::Transition *transition =
        drift::appdetail::findTransition(m_app->m_project.tracks()[m_transitionTrack], m_transitionId);
    if (!transition)
        return;

    QList<QPointF> parsed;
    parsed.reserve(points.size());
    for (const QVariant &entry : points) {
        const QVariantMap map = entry.toMap();
        parsed.append(QPointF(map.value(QStringLiteral("t")).toDouble(),
                              map.value(QStringLiteral("g")).toDouble()));
    }
    m_transitionShape.setPoints(parsed);
    m_transitionMode = drift::FadeCurve::Custom;
    transition->easingCurve = drift::FadeCurve::Custom;
    transition->easingShape = m_transitionShape;
    emit transitionChanged();
    m_app->emitPreviewFrame();
}

QString CurveEditorController::transitionMode() const
{
    return m_transitionMode == drift::FadeCurve::Bezier ? QStringLiteral("bezier")
                                                             : QStringLiteral("points");
}

QVariantList CurveEditorController::transitionHandles() const
{
    return {m_transitionShape.handle1().x(), m_transitionShape.handle1().y(),
            m_transitionShape.handle2().x(), m_transitionShape.handle2().y()};
}

void CurveEditorController::setTransitionHandles(double c1x, double c1y, double c2x, double c2y)
{
    if (!m_transitionActive)
        return;
    if (m_transitionTrack < 0 || m_transitionTrack >= m_app->m_project.tracks().size())
        return;
    drift::Transition *transition =
        drift::appdetail::findTransition(m_app->m_project.tracks()[m_transitionTrack], m_transitionId);
    if (!transition)
        return;

    m_transitionShape.setHandles(QPointF(c1x, c1y), QPointF(c2x, c2y));
    m_transitionMode = drift::FadeCurve::Bezier;
    transition->easingCurve = drift::FadeCurve::Bezier;
    transition->easingShape = m_transitionShape;
    emit transitionChanged();
    m_app->emitPreviewFrame();
}

void CurveEditorController::resetTransitionPreset(const QString &preset)
{
    if (!m_transitionActive)
        return;
    if (preset.startsWith(QLatin1String("bezier:"))) {
        const drift::FadeShape seed = drift::FadeShape::bezierPreset(preset.mid(7));
        setTransitionHandles(seed.handle1().x(), seed.handle1().y(),
                                  seed.handle2().x(), seed.handle2().y());
        return;
    }
    if (preset == QLatin1String("linear"))
        m_transitionShape = drift::FadeShape::linearPreset();
    else if (preset == QLatin1String("equalPower") || preset == QLatin1String("natural"))
        m_transitionShape = drift::FadeShape::equalPowerPreset();
    else
        m_transitionShape = drift::FadeShape::smoothPreset();

    QVariantList points;
    for (const QPointF &pt : m_transitionShape.points()) {
        points.append(QVariantMap{
            {QStringLiteral("t"), pt.x()},
            {QStringLiteral("g"), pt.y()},
        });
    }
    setTransitionPoints(points);
}

void CurveEditorController::applyTransition()
{
    if (!m_transitionActive)
        return;
    if (m_transitionTrack < 0 || m_transitionTrack >= m_app->m_project.tracks().size())
        return;
    drift::Transition *transition =
        drift::appdetail::findTransition(m_app->m_project.tracks()[m_transitionTrack], m_transitionId);
    if (!transition) {
        m_app->setLastMessage(tr("That transition is gone — open the custom curve again"),
                       QStringLiteral("warning"));
        endTransition();
        return;
    }

    // Rebuild the "before" snapshot so undo restores the curve the session started from, not the
    // audition state the live project is currently holding.
    drift::Project before = m_app->m_project;
    if (m_transitionTrack < before.tracks().size()) {
        if (drift::Transition *beforeTransition =
                drift::appdetail::findTransition(before.tracks()[m_transitionTrack], m_transitionId)) {
            beforeTransition->easingCurve = m_transitionBefore;
            beforeTransition->easingShape = m_transitionShapeBefore;
        }
    }

    transition->easingCurve = m_transitionMode;
    transition->easingShape = m_transitionShape;
    m_app->pushProjectEdit(before, tr("Custom transition curve"));
    m_transitionApplied = true;
    m_app->finishEdit(tr("Custom transition curve applied"));
    emit m_app->selectedTransitionDataChanged();
    emit transitionApplied();
    endTransition();
}

QString CurveEditorController::fadeTarget() const
{
    return m_fadeAnim ? QStringLiteral("animation") : QStringLiteral("fade");
}

QString CurveEditorController::fadeEdge() const
{
    return m_fadeIn ? QStringLiteral("in") : QStringLiteral("out");
}

QString CurveEditorController::fadeMode() const
{
    return m_fadeMode == drift::FadeCurve::Bezier ? QStringLiteral("bezier")
                                                       : QStringLiteral("points");
}

QVariantList CurveEditorController::fadeHandles() const
{
    return {m_fadeShape.handle1().x(), m_fadeShape.handle1().y(),
            m_fadeShape.handle2().x(), m_fadeShape.handle2().y()};
}

void CurveEditorController::setFadeHandles(double c1x, double c1y, double c2x, double c2y)
{
    if (!m_fadeActive)
        return;
    if (m_fadeTrack < 0 || m_fadeTrack >= m_app->m_project.tracks().size())
        return;
    drift::Track &track = m_app->m_project.tracks()[m_fadeTrack];
    if (m_fadeClipIndex < 0 || m_fadeClipIndex >= track.clips.size())
        return;
    drift::Clip &clip = track.clips[m_fadeClipIndex];
    if (clip.id != m_fadeClipId)
        return;

    m_fadeShape.setHandles(QPointF(c1x, c1y), QPointF(c2x, c2y));
    m_fadeMode = drift::FadeCurve::Bezier;
    edgeCurve(clip, m_fadeIn, m_fadeAnim) = drift::FadeCurve::Bezier;
    edgeShape(clip, m_fadeIn, m_fadeAnim) = m_fadeShape;
    drift::appdetail::syncLinkedPartnersFrom(m_app->m_project, clip);
    emit fadeChanged();
    m_app->emitPreviewFrame();
}

void CurveEditorController::resetFadePreset(const QString &preset)
{
    if (!m_fadeActive)
        return;
    if (preset.startsWith(QLatin1String("bezier:"))) {
        const drift::FadeShape seed = drift::FadeShape::bezierPreset(preset.mid(7));
        setFadeHandles(seed.handle1().x(), seed.handle1().y(),
                            seed.handle2().x(), seed.handle2().y());
        return;
    }
    if (preset == QLatin1String("linear"))
        m_fadeShape = drift::FadeShape::linearPreset();
    else if (preset == QLatin1String("equalPower") || preset == QLatin1String("natural"))
        m_fadeShape = drift::FadeShape::equalPowerPreset();
    else
        m_fadeShape = drift::FadeShape::smoothPreset();

    QVariantList points;
    for (const QPointF &pt : m_fadeShape.points()) {
        points.append(QVariantMap{
            {QStringLiteral("t"), pt.x()},
            {QStringLiteral("g"), pt.y()},
        });
    }
    setFadePoints(points);
}

void CurveEditorController::applyFade()
{
    if (!m_fadeActive)
        return;
    if (m_fadeTrack < 0 || m_fadeTrack >= m_app->m_project.tracks().size())
        return;
    drift::Track &track = m_app->m_project.tracks()[m_fadeTrack];
    if (m_fadeClipIndex < 0 || m_fadeClipIndex >= track.clips.size())
        return;
    drift::Clip &clip = track.clips[m_fadeClipIndex];
    if (clip.id != m_fadeClipId) {
        m_app->setLastMessage(tr("That clip moved — open Custom fade again"), QStringLiteral("warning"));
        return;
    }

    // Rebuild the "before" snapshot: restore prior fade fields on a copy of the current project.
    drift::Project before = m_app->m_project;
    if (m_fadeTrack < before.tracks().size()
        && m_fadeClipIndex < before.tracks().at(m_fadeTrack).clips.size()) {
        drift::Clip &beforeClip = before.tracks()[m_fadeTrack].clips[m_fadeClipIndex];
        edgeCurve(beforeClip, m_fadeIn, m_fadeAnim) = m_fadeBefore;
        edgeShape(beforeClip, m_fadeIn, m_fadeAnim) = m_fadeShapeBefore;
        drift::appdetail::syncLinkedPartnersFrom(before, beforeClip);
    }

    edgeCurve(clip, m_fadeIn, m_fadeAnim) = m_fadeMode;
    edgeShape(clip, m_fadeIn, m_fadeAnim) = m_fadeShape;
    drift::appdetail::syncLinkedPartnersFrom(m_app->m_project, clip);
    const QString label = m_fadeAnim ? tr("Custom animation curve applied") : tr("Custom fade applied");
    m_app->pushProjectEdit(before, label);
    m_fadeApplied = true;
    m_app->finishEdit(label);
    emit fadeApplied();
    endFade();
}


#include "moc_CurveEditorController.cpp"
