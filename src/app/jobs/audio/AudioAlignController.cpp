#include "app/jobs/audio/AudioAlignController.h"

#include "app/editor/AppController.h"
#include "core/edit/CutOps.h"
#include "core/edit/TimelineOps.h"
#include "engine/audio/AudioMixer.h"
#include "engine/audio/analysis/AudioAlign.h"

#include <QMetaObject>
#include <QSet>
#include <QtConcurrent>

#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

namespace {

constexpr int kAnalysisRate = 8000;
constexpr double kMaxSyncSeconds = 120.0;
constexpr double kMaxSyncLagSeconds = 60.0;

bool carriesAudio(const drift::Clip &clip)
{
    return clip.type == drift::ClipType::Audio
           || (clip.type == drift::ClipType::Video && !clip.suppressEmbeddedAudio);
}

// The clip whose sound `clip` plays: itself, or for a video whose audio was separated, its audio
// partner.
const drift::Clip *audibleClip(const drift::Project &project, const drift::Clip &clip)
{
    if (carriesAudio(clip))
        return &clip;
    for (const drift::ClipRef &ref : drift::linkedPartners(project, clip)) {
        if (project.tracks().at(ref.trackIndex).type != drift::TrackType::Audio)
            continue;
        const drift::Clip &partner = project.tracks().at(ref.trackIndex).clips.at(ref.clipIndex);
        if (carriesAudio(partner))
            return &partner;
    }
    return nullptr;
}

bool findClip(const drift::Project &project, const QString &id, int *trackOut, int *clipOut)
{
    for (int t = 0; t < project.tracks().size(); ++t) {
        const QList<drift::Clip> &clips = project.tracks().at(t).clips;
        for (int c = 0; c < clips.size(); ++c) {
            if (clips.at(c).id == id) {
                *trackOut = t;
                *clipOut = c;
                return true;
            }
        }
    }
    return false;
}

// Mono mix of `snap` from startUs for `frames` frames at the analysis rate. `progress` takes
// 0-1 and returns false to stop, which leaves the result short.
std::vector<float> mixMono(const drift::Project &snap, drift::TimeUs startUs, qint64 frames,
                           const std::function<bool(double)> &progress)
{
    std::vector<float> mono;
    if (frames <= 0)
        return mono;
    mono.resize(size_t(frames));
    AudioMixer mixer;
    mixer.setProject(&snap);
    constexpr int kBlock = 4096;
    std::vector<float> stereo(kBlock * 2);
    for (qint64 off = 0; off < frames; off += kBlock) {
        if (progress && !progress(double(off) / double(frames))) {
            mono.clear();
            return mono;
        }
        const int n = int(std::min<qint64>(kBlock, frames - off));
        std::fill(stereo.begin(), stereo.begin() + n * 2, 0.0f);
        mixer.mix(startUs + off * drift::kUsPerSecond / kAnalysisRate, n, kAnalysisRate,
                  stereo.data());
        for (int i = 0; i < n; ++i)
            mono[size_t(off + i)] = 0.5f * (stereo[i * 2] + stereo[i * 2 + 1]);
    }
    return mono;
}

bool retimed(const drift::Clip &clip)
{
    return std::abs(clip.effectiveSpeed() - 1.0) > 1e-6 || clip.reverse || clip.hasSpeedCurve();
}

} // namespace

AudioAlignController::AudioAlignController(AppController *app, QObject *parent)
    : QObject(parent)
    , m_app(app)
{
}

void AudioAlignController::cancel()
{
    if (m_running)
        m_cancel.storeRelaxed(1);
}

bool AudioAlignController::begin(const QString &message)
{
    if (m_running) {
        m_app->setLastMessage(tr("Audio analysis already in progress"), QStringLiteral("warning"));
        return false;
    }
    // Playback would drive the decode pool from a second thread while the mixer walks it.
    m_app->setPlaying(false);
    m_cancel.storeRelaxed(0);
    m_progress = 0.0;
    emit progressChanged();
    m_status = message;
    emit statusChanged();
    m_running = true;
    emit runningChanged();
    m_app->setLastMessage(message);
    return true;
}

void AudioAlignController::end()
{
    m_running = false;
    emit runningChanged();
    m_progress = 1.0;
    emit progressChanged();
}

void AudioAlignController::reportProgress(double fraction, const QString &status)
{
    QMetaObject::invokeMethod(
        this,
        [this, fraction, status]() {
            if (!m_running)
                return;
            m_progress = fraction;
            emit progressChanged();
            if (!status.isEmpty() && status != m_status) {
                m_status = status;
                emit statusChanged();
            }
        },
        Qt::QueuedConnection);
}

bool AudioAlignController::canRemoveSilence(int trackIndex, int clipIndex) const
{
    if (!m_app->isValidClipIndex(trackIndex, clipIndex))
        return false;
    return audibleClip(m_app->m_project, m_app->m_project.tracks().at(trackIndex).clips.at(clipIndex))
           != nullptr;
}

void AudioAlignController::removeSilence(int trackIndex, int clipIndex)
{
    if (!m_app->isValidClipIndex(trackIndex, clipIndex))
        return;
    const drift::Clip &target = m_app->m_project.tracks().at(trackIndex).clips.at(clipIndex);
    const drift::Clip *audible = audibleClip(m_app->m_project, target);
    if (!audible) {
        m_app->setLastMessage(tr("Select a clip with audio"), QStringLiteral("warning"));
        return;
    }
    if (audible->timelineDuration <= 0)
        return;
    const QString targetId = target.id;
    const drift::TimeUs startUs = audible->timelineStart;
    const drift::TimeUs durationUs = audible->timelineDuration;
    drift::Project snap = m_app->m_project.detachedCopy();
    drift::muteAllButClip(snap, audible->id);

    if (!begin(tr("Finding silence…")))
        return;

    (void)QtConcurrent::run([this, snap, targetId, startUs, durationUs]() {
        const qint64 frames = durationUs * kAnalysisRate / drift::kUsPerSecond;
        const std::vector<float> mono = mixMono(snap, startUs, frames, [this](double f) {
            reportProgress(0.95 * f, {});
            return m_cancel.loadRelaxed() == 0;
        });
        const bool cancelled = m_cancel.loadRelaxed() != 0 || mono.empty();
        QList<QPair<double, double>> silences;
        if (!cancelled) {
            for (const drift::SilenceSpan &s : drift::findSilence(mono.data(), qint64(mono.size()),
                                                                  kAnalysisRate))
                silences.append({s.start, s.end});
        }
        QMetaObject::invokeMethod(
            this,
            [this, cancelled, targetId, startUs, durationUs, silences]() {
                end();
                if (cancelled) {
                    m_app->setLastMessage(tr("Silence removal cancelled"));
                    return;
                }
                applySilence(targetId, startUs, durationUs, silences);
            },
            Qt::QueuedConnection);
    });
}

void AudioAlignController::applySilence(const QString &clipId, qint64 startUs, qint64 durationUs,
                                        const QList<QPair<double, double>> &silences)
{
    int track = -1, index = -1;
    if (!findClip(m_app->m_project, clipId, &track, &index)) {
        m_app->setLastMessage(tr("The clip changed while it was analysed"), QStringLiteral("warning"));
        return;
    }
    const drift::Clip clip = m_app->m_project.tracks().at(track).clips.at(index);
    if (clip.timelineStart != startUs || clip.timelineDuration != durationUs) {
        m_app->setLastMessage(tr("The clip changed while it was analysed"), QStringLiteral("warning"));
        return;
    }

    QList<drift::TimeRangeUs> removed;
    for (const QPair<double, double> &s : silences)
        removed.append({startUs + drift::secondsToUs(s.first), startUs + drift::secondsToUs(s.second)});
    constexpr drift::TimeUs kDeclickUs = 30'000;
    const QList<drift::TimeRangeUs> kept = drift::keptTimelineIntervals(
        clip, removed, std::max<drift::TimeUs>(drift::kCutMinEdgeUs, 2 * kDeclickUs));
    if (kept.size() == 1 && kept.first().startUs == clip.timelineStart
        && kept.first().endUs == clip.timelineEnd()) {
        m_app->setLastMessage(tr("No silence found"));
        return;
    }
    drift::TimeUs keptTotal = 0;
    for (const drift::TimeRangeUs &k : kept)
        keptTotal += k.length();

    const drift::Project before = m_app->m_project;
    QString error;
    if (!m_app->replaceClipGroupWithSegments(
            track, index,
            [&kept](const drift::Clip &member) {
                return drift::packedSegments(member, kept, kDeclickUs);
            },
            true, nullptr, &error)) {
        m_app->m_project = before;
        m_app->setLastMessage(tr("Could not remove the silence"), QStringLiteral("error"));
        return;
    }
    m_app->pushProjectEdit(before, tr("Remove silence"));
    m_app->finishEdit(tr("Remove silence"));
    m_app->setLastMessage(
        tr("Removed %1 s of silence")
            .arg(drift::usToSeconds(clip.timelineDuration - keptTotal), 0, 'f', 1));
}

// A selected item for syncing: every selected clip sharing a link (a video and its separated
// audio) is one item, represented by its first audio-carrying member.
struct AudioAlignController::SyncUnit
{
    QString key;
    QString repId;
    drift::TimeUs startUs = 0;
    drift::TimeUs durationUs = 0;
};

bool AudioAlignController::canSyncByAudio() const
{
    QSet<QString> keys;
    for (const QPair<int, int> &pair : std::as_const(m_app->m_selection)) {
        if (!m_app->isValidClipIndex(pair.first, pair.second))
            continue;
        const drift::Clip &clip = m_app->m_project.tracks().at(pair.first).clips.at(pair.second);
        if (carriesAudio(clip))
            keys.insert(clip.linkId.isEmpty() ? clip.id : clip.linkId);
    }
    return keys.size() == 2;
}

void AudioAlignController::syncByAudio()
{
    QList<SyncUnit> units;
    for (const QPair<int, int> &pair : std::as_const(m_app->m_selection)) {
        if (!m_app->isValidClipIndex(pair.first, pair.second))
            continue;
        const drift::Clip &clip = m_app->m_project.tracks().at(pair.first).clips.at(pair.second);
        if (!carriesAudio(clip))
            continue;
        const QString key = clip.linkId.isEmpty() ? clip.id : clip.linkId;
        const bool known = std::any_of(units.cbegin(), units.cend(),
                                       [&key](const SyncUnit &u) { return u.key == key; });
        if (!known)
            units.append({key, clip.id, clip.timelineStart, clip.timelineDuration});
    }
    if (units.size() != 2) {
        m_app->setLastMessage(tr("Select exactly two clips with audio to sync"),
                              QStringLiteral("warning"));
        return;
    }

    // Both items and everything linked to them: speed or direction changes break the
    // fixed-offset assumption.
    for (const SyncUnit &unit : std::as_const(units)) {
        int t = -1, c = -1;
        if (!findClip(m_app->m_project, unit.repId, &t, &c))
            return;
        const drift::Clip &rep = m_app->m_project.tracks().at(t).clips.at(c);
        bool bad = retimed(rep);
        for (const drift::ClipRef &ref : drift::linkedPartners(m_app->m_project, rep))
            bad = bad || retimed(m_app->m_project.tracks().at(ref.trackIndex).clips.at(ref.clipIndex));
        if (bad) {
            m_app->setLastMessage(
                tr("Clips with changed speed or reversed playback can't be synced. Reset their "
                   "speed and direction first."),
                QStringLiteral("warning"));
            return;
        }
    }

    const SyncUnit anchor = units.at(0);
    const SyncUnit other = units.at(1);
    drift::Project anchorSnap = m_app->m_project.detachedCopy();
    drift::muteAllButClip(anchorSnap, anchor.repId);
    drift::Project otherSnap = m_app->m_project.detachedCopy();
    drift::muteAllButClip(otherSnap, other.repId);

    if (!begin(tr("Matching audio…")))
        return;

    (void)QtConcurrent::run([this, anchor, other, anchorSnap, otherSnap]() {
        const auto framesFor = [](drift::TimeUs durationUs) {
            return qint64(std::min(drift::usToSeconds(durationUs), kMaxSyncSeconds) * kAnalysisRate);
        };
        const auto alive = [this]() { return m_cancel.loadRelaxed() == 0; };
        const std::vector<float> a = mixMono(anchorSnap, anchor.startUs, framesFor(anchor.durationUs),
                                             [this, &alive](double f) {
                                                 reportProgress(0.4 * f, {});
                                                 return alive();
                                             });
        const std::vector<float> b = alive() ? mixMono(otherSnap, other.startUs,
                                                         framesFor(other.durationUs),
                                                         [this, &alive](double f) {
                                                             reportProgress(0.4 + 0.4 * f, {});
                                                             return alive();
                                                         })
                                               : std::vector<float>{};
        drift::AudioOffsetResult result;
        const bool cancelled = !alive() || a.empty() || b.empty();
        if (!cancelled) {
            reportProgress(0.85, tr("Correlating…"));
            drift::AudioOffsetParams params;
            params.maxLagSeconds = kMaxSyncLagSeconds;
            result = drift::findAudioOffset(a, b, kAnalysisRate, params, alive);
        }
        QMetaObject::invokeMethod(
            this,
            [this, anchor, other, result, cancelled]() {
                end();
                if (m_cancel.loadRelaxed() != 0 || cancelled) {
                    m_app->setLastMessage(tr("Audio sync cancelled"));
                    return;
                }
                using Status = drift::AudioOffsetResult::Status;
                switch (result.status) {
                case Status::Silent:
                    m_app->setLastMessage(tr("One of the clips is silent, so there is nothing to match"),
                                          QStringLiteral("warning"));
                    return;
                case Status::TooShort:
                    m_app->setLastMessage(tr("The clips are too short to match by audio"),
                                          QStringLiteral("warning"));
                    return;
                case Status::Ambiguous:
                    m_app->setLastMessage(
                        tr("No clear match between the clips' audio. Nothing was moved."),
                        QStringLiteral("warning"));
                    return;
                case Status::Ok:
                    break;
                }
                applySync(anchor.repId, other.repId, anchor.startUs, other.startUs, result.lagSeconds);
            },
            Qt::QueuedConnection);
    });
}

void AudioAlignController::applySync(const QString &anchorId, const QString &otherId,
                                     qint64 anchorStartUs, qint64 otherStartUs, double lagSeconds)
{
    int at = -1, ac = -1, ot = -1, oc = -1;
    if (!findClip(m_app->m_project, anchorId, &at, &ac) || !findClip(m_app->m_project, otherId, &ot, &oc)
        || m_app->m_project.tracks().at(at).clips.at(ac).timelineStart != anchorStartUs
        || m_app->m_project.tracks().at(ot).clips.at(oc).timelineStart != otherStartUs) {
        m_app->setLastMessage(tr("The clips changed while they were analysed"),
                              QStringLiteral("warning"));
        return;
    }

    // other[n] ~ anchor[n + lag], so other's first sample belongs lag after the anchor's.
    const drift::TimeUs delta = anchorStartUs + drift::secondsToUs(lagSeconds) - otherStartUs;
    if (delta == 0) {
        m_app->setLastMessage(tr("The clips are already in sync"));
        return;
    }

    QList<QPair<int, int>> group{{ot, oc}};
    for (const drift::ClipRef &ref :
         drift::linkedPartners(m_app->m_project, m_app->m_project.tracks().at(ot).clips.at(oc)))
        group.append({ref.trackIndex, ref.clipIndex});
    QSet<QString> ids;
    for (const QPair<int, int> &p : std::as_const(group))
        ids.insert(m_app->m_project.tracks().at(p.first).clips.at(p.second).id);

    for (const QPair<int, int> &p : std::as_const(group)) {
        const drift::Track &track = m_app->m_project.tracks().at(p.first);
        const drift::Clip &clip = track.clips.at(p.second);
        const drift::TimeUs moved = clip.timelineStart + delta;
        if (moved < 0) {
            m_app->setLastMessage(tr("Syncing would move the clip before the start of the timeline"),
                                  QStringLiteral("warning"));
            return;
        }
        if (!m_app->allowClipOverlap()
            && drift::clampClipStartNoOverlap(track, ids, moved, clip.timelineDuration) != moved) {
            m_app->setLastMessage(tr("There is no room to move the clip there"),
                                  QStringLiteral("warning"));
            return;
        }
    }

    const drift::Project before = m_app->m_project;
    for (const QPair<int, int> &p : std::as_const(group))
        m_app->m_project.tracks()[p.first].clips[p.second].timelineStart += delta;
    m_app->pushProjectEdit(before, tr("Sync by audio"));
    m_app->finishEdit(tr("Sync by audio"));
    m_app->setLastMessage(
        tr("Moved the second clip %1 s to match the first")
            .arg(drift::usToSeconds(delta), 0, 'f', 3));
}

#include "moc_AudioAlignController.cpp"
