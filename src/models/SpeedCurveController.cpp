#include "SpeedCurveController.h"

#include "AppController.h"
#include "AppControllerDetail.h"
#include "core/Time.h"
#include "core/TimelineOps.h"

#include <QSet>
#include <QUuid>

SpeedCurveController::SpeedCurveController(AppController *app, QObject *parent)
    : QObject(parent)
    , m_app(app)
{
    // Independent of the timeline; it only ever reports on the one clip being retimed.
    connect(&m_player, &ClipPreviewPlayer::frameChanged, this, [this] {
        ++m_revision;
        emit frameChanged();
    });
    connect(&m_player, &ClipPreviewPlayer::frameSizeChanged, this,
            &SpeedCurveController::frameChanged);
    connect(&m_player, &ClipPreviewPlayer::positionChanged, this,
            &SpeedCurveController::positionChanged);
    connect(&m_player, &ClipPreviewPlayer::playingChanged, this,
            &SpeedCurveController::playingChanged);
    connect(&m_player, &ClipPreviewPlayer::durationChanged, this,
            &SpeedCurveController::curveChanged);
}

void SpeedCurveController::setAudioDeviceId(const QByteArray &id)
{
    m_player.setAudioDeviceId(id);
}

void SpeedCurveController::begin(int trackIndex, int clipIndex, bool allowNested)
{
    if (trackIndex < 0 || trackIndex >= m_app->m_project.tracks().size())
        return;
    const drift::Track &track = m_app->m_project.tracks().at(trackIndex);
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    const drift::Clip &clip = track.clips.at(clipIndex);
    // A composite reads its nested timeline, which the preview player cannot decode, so only
    // callers that never show the preview (MCP) may open a session on one.
    const bool nested = !clip.sequenceId.isEmpty();
    if ((clip.type != drift::ClipType::Video && clip.type != drift::ClipType::Audio
         && clip.type != drift::ClipType::Composite)
        || (nested && !allowNested)) {
        m_app->setLastMessage(tr("Custom speed works on video and audio clips"), QStringLiteral("warning"));
        return;
    }
    if ((clip.path.isEmpty() && !nested) || clip.srcOut <= clip.srcIn) {
        m_app->setLastMessage(tr("This clip has no media to speed up or slow down"), QStringLiteral("warning"));
        return;
    }

    // The preview drives ClipReaderPool from its own threads; leaving timeline playback running
    // would have both walking the same decode workers.
    m_app->setPlaying(false);

    m_track = trackIndex;
    m_clipIndex = clipIndex;
    m_clip = clip;
    // An existing ramp is what the editor should open on; otherwise start flat at the clip's
    // current constant speed so the graph begins where the clip already plays.
    m_curve = clip.hasSpeedCurve() ? clip.speedCurve : drift::SpeedCurve::flat(clip.effectiveSpeed());
    m_clip.speedCurve = m_curve;
    m_active = true;

    m_player.setClip(m_clip, m_app->m_project.sampleRate(), m_app->m_project.fps());

    emit sessionChanged();
    emit curveChanged();
}

void SpeedCurveController::end()
{
    if (!m_active)
        return;

    m_player.clear();
    m_active = false;
    m_track = -1;
    m_clipIndex = -1;
    m_clip = drift::Clip{};
    m_curve.clear();
    emit sessionChanged();
    emit curveChanged();
}

QVariantList SpeedCurveController::points() const
{
    QVariantList out;
    for (const drift::SpeedPoint &point : m_curve.points()) {
        out.append(QVariantMap{
            {QStringLiteral("pos"), point.pos},
            {QStringLiteral("speed"), point.speed},
            {QStringLiteral("inDx"), point.inDx},
            {QStringLiteral("inDy"), point.inDy},
            {QStringLiteral("outDx"), point.outDx},
            {QStringLiteral("outDy"), point.outDy},
            {QStringLiteral("corner"), point.corner},
        });
    }
    return out;
}

void SpeedCurveController::setPoints(const QVariantList &points)
{
    if (!m_active)
        return;

    QList<drift::SpeedPoint> parsed;
    parsed.reserve(points.size());
    for (const QVariant &entry : points) {
        const QVariantMap map = entry.toMap();
        drift::SpeedPoint point;
        point.pos = map.value(QStringLiteral("pos")).toDouble();
        point.speed = map.value(QStringLiteral("speed"), 1.0).toDouble();
        point.inDx = map.value(QStringLiteral("inDx")).toDouble();
        point.inDy = map.value(QStringLiteral("inDy")).toDouble();
        point.outDx = map.value(QStringLiteral("outDx")).toDouble();
        point.outDy = map.value(QStringLiteral("outDy")).toDouble();
        point.corner = map.value(QStringLiteral("corner")).toBool();
        parsed.append(point);
    }

    m_curve.setPoints(parsed);
    m_clip.speedCurve = m_curve;
    m_player.setSpeedCurve(m_curve);
    emit curveChanged();
}

double SpeedCurveController::sourceStart() const
{
    return drift::usToSeconds(m_clip.srcIn);
}

double SpeedCurveController::mediaDuration() const
{
    return drift::usToSeconds(m_app->sourceDurationForClip(m_clip));
}

double SpeedCurveController::sourceDuration() const
{
    return drift::usToSeconds(m_clip.srcOut - m_clip.srcIn);
}

double SpeedCurveController::retimedDuration() const
{
    return drift::usToSeconds(m_player.durationUs());
}

double SpeedCurveController::position() const
{
    return drift::usToSeconds(m_player.positionUs());
}

void SpeedCurveController::play()
{
    if (!m_active)
        return;
    m_app->setPlaying(false);
    m_player.play();
}

void SpeedCurveController::pause()
{
    m_player.pause();
}

void SpeedCurveController::seek(double seconds)
{
    if (!m_active)
        return;
    m_player.seek(drift::secondsToUs(seconds));
}

double SpeedCurveController::sourcePosition() const
{
    const drift::TimeUs span = m_clip.srcOut - m_clip.srcIn;
    if (span <= 0)
        return 0.0;
    const drift::TimeUs offset =
        m_curve.sourceOffsetForTimelineOffset(m_player.positionUs(), span);
    return static_cast<double>(offset) / span;
}

void SpeedCurveController::seekAtSource(double position)
{
    if (!m_active)
        return;
    const drift::TimeUs span = m_clip.srcOut - m_clip.srcIn;
    if (span <= 0)
        return;
    const drift::TimeUs offset = static_cast<drift::TimeUs>(qBound(0.0, position, 1.0) * span);
    m_player.seek(m_curve.timelineOffsetForSourceOffset(offset, span));
}

void SpeedCurveController::apply()
{
    if (!m_active)
        return;
    if (m_track < 0 || m_track >= m_app->m_project.tracks().size())
        return;
    const drift::Track &track = m_app->m_project.tracks().at(m_track);
    if (m_clipIndex < 0 || m_clipIndex >= track.clips.size())
        return;

    m_player.pause();

    const drift::Clip source = track.clips.at(m_clipIndex);
    // The timeline stays editable while the window is open, so the indices captured at the start
    // of the session can point at a different clip by now.
    if (source.id != m_clip.id) {
        m_app->setLastMessage(tr("That clip moved — open Custom speed again"), QStringLiteral("warning"));
        return;
    }

    const drift::Project before = m_app->m_project;
    drift::Clip retimed = source;
    retimed.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    retimed.speedCurve = m_curve;
    retimed.syncDurationFromSpeedCurve();
    // The copy stands on its own: it carries its own retimed audio rather than staying paired
    // with a companion clip that is still playing at the original rate.
    retimed.linkId.clear();
    retimed.suppressEmbeddedAudio = false;
    retimed.name = (source.name.isEmpty() ? QStringLiteral("Clip") : source.name)
                   + QStringLiteral(" (retimed)");
    drift::appdetail::remapKeyframesForRetime(retimed, source);

    // The retimed copy replaces the clip it was made from rather than joining it. Left in place the
    // original keeps playing underneath at the original rate: its audio sums into the mix, and it
    // shows through wherever the retimed duration differs. A detached audio companion goes with it
    // for the same reason — the copy carries its own audio.
    QSet<QString> replacedIds{source.id};
    for (const drift::ClipRef &ref : drift::linkedPartners(m_app->m_project, source))
        replacedIds.insert(m_app->m_project.tracks().at(ref.trackIndex).clips.at(ref.clipIndex).id);

    const int newTrack =
        drift::insertTrackAboveForClipType(m_app->m_project, m_track, source.type);
    m_app->m_project.tracks()[newTrack].clips.append(retimed);

    for (drift::Track &t : m_app->m_project.tracks()) {
        for (int i = t.clips.size() - 1; i >= 0; --i) {
            if (replacedIds.contains(t.clips.at(i).id))
                t.clips.removeAt(i);
        }
        for (int i = t.transitions.size() - 1; i >= 0; --i) {
            const drift::Transition &transition = t.transitions.at(i);
            if (replacedIds.contains(transition.fromClipId) || replacedIds.contains(transition.toClipId))
                t.transitions.removeAt(i);
        }
    }

    m_app->pushProjectEdit(before, tr("Custom speed applied"));
    m_app->finishEdit(tr("Custom speed applied"));
    m_app->selectClip(newTrack, m_app->m_project.tracks().at(newTrack).clips.size() - 1);
    emit applied();
}
