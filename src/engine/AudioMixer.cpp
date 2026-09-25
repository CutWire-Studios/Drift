#include "AudioMixer.h"

#include "AudioEffectCatalog.h"
#include "ClipReaderPool.h"
#include "TransitionCatalog.h"
#include "core/Clip.h"
#include "core/TimelineOps.h"
#include "core/Transition.h"

#include <QtMath>
#include <cmath>
#include <cstring>
#include <memory>
#include <utility>

namespace {

// Summing several clips, each with up to 2.0 of gain, regularly overshoots. Clamping squared off
// the peaks; this rounds them instead, asymptotically approaching full scale so the result can
// never exceed 1.0 however hard the mix is driven. Stateless — no attack, no release, no pumping
// across the timeline, nothing to reset on seek.
//
// The knee sits just under full scale on purpose. Anything lower colours audio that was never
// going to clip: normal material crosses -3 dBFS on every peak, so a knee down there is an
// always-on waveshaper rather than a safety net.
constexpr float kSoftClipKnee = 0.95f; // -0.45 dBFS

float softClip(float sample)
{
    const float magnitude = std::fabs(sample);
    if (magnitude <= kSoftClipKnee)
        return sample;

    const float over = (magnitude - kSoftClipKnee) / (1.0f - kSoftClipKnee);
    const float shaped = kSoftClipKnee + (1.0f - kSoftClipKnee) * std::tanh(over);
    return std::copysign(shaped, sample);
}

double volumeForClip(const drift::Clip &clip, drift::TimeUs timelineUs)
{
    if (clip.volume.isEmpty())
        return 1.0;
    const drift::TimeUs relative = qMax<drift::TimeUs>(0, timelineUs - clip.timelineStart);
    return qBound(0.0, clip.volume.evaluateAt(relative), 2.0);
}

// Balance law: attenuate one side, leave the other alone. Unity at centre, unlike a
// constant-power sin/cos pan, which would drop every existing clip's level by 3 dB the moment
// this shipped.
void panGainsForClip(const drift::Clip &clip, float *leftOut, float *rightOut)
{
    const double pan = qBound(-1.0, clip.pan, 1.0);
    *leftOut = static_cast<float>(qMin(1.0, 1.0 - pan));
    *rightOut = static_cast<float>(qMin(1.0, 1.0 + pan));
}

void panGainsForTrack(const drift::Track &track, float *leftOut, float *rightOut)
{
    const double pan = qBound(-1.0, track.pan, 1.0);
    *leftOut = static_cast<float>(qMin(1.0, 1.0 - pan));
    *rightOut = static_cast<float>(qMin(1.0, 1.0 + pan));
}

double transitionGainForClip(const drift::Track &track, const drift::Clip &clip, drift::TimeUs timelineUs,
                             const drift::AudioTransitionEdge &edge)
{
    drift::TimeUs windowStart = 0;
    drift::TimeUs windowEnd = 0;
    const drift::Transition *transition = drift::activeTransitionAt(track, timelineUs, windowStart, windowEnd);
    if (!transition)
        return 1.0;

    const double p = drift::transitionProgress(*transition, timelineUs, windowStart, windowEnd);
    const TransitionPresetEntry *def = transitionDefForId(transition->kindId);
    const QString curve = def ? def->audioCurve : QStringLiteral("crossfade");
    // Clips that only touch: each side crossfades when it has media past the cut, and dips when
    // it doesn't. Overlapping clips have real audio across the window already.
    if (clip.id == transition->fromClipId) {
        const QString side = edge.ownsOut ? drift::effectiveAudioCurve(curve, edge.outHasHandle) : curve;
        return drift::transitionAudioGains(side, p).outgoing;
    }
    if (clip.id == transition->toClipId) {
        const QString side = edge.ownsIn ? drift::effectiveAudioCurve(curve, edge.inHasHandle) : curve;
        return drift::transitionAudioGains(side, p).incoming;
    }
    return 1.0;
}

constexpr drift::TimeUs kTimelineGapToleranceUs = 2'000; // ~2 ms: allow frame rounding between blocks

drift::TimeUs framesToUs(int frames, int sampleRate)
{
    return static_cast<drift::TimeUs>(static_cast<int64_t>(frames) * drift::kUsPerSecond / sampleRate);
}

// Everything about a clip that decides where a timeline position lands in its source. When one of
// them moves, a running retimer's cursor is describing a mapping that no longer exists, so the
// stream has to restart — that is what makes dragging the speed slider during playback safe.
// The curve is sampled through speedAt(), which is const and allocation-free by design, rather
// than by walking its point list while the GUI thread may be rewriting it.
quint64 clipAudioIdentity(const drift::Clip &clip)
{
    return qHashMulti(0, clip.path, clip.srcIn, clip.srcOut, clip.timelineStart, clip.timelineDuration,
                      clip.reverse, clip.speed, clip.speedCurve.isEmpty(), clip.speedCurve.speedAt(0.0),
                      clip.speedCurve.speedAt(0.25), clip.speedCurve.speedAt(0.5),
                      clip.speedCurve.speedAt(0.75), clip.speedCurve.speedAt(1.0));
}

} // namespace

// Silence outside the clip means this is safe to call for a preroll window that runs off the
// clip's front edge.
namespace {

// A block that runs past the clip's end still reads a full block of source (the decoder and the
// retimer both need the continuity), so whatever lies beyond srcOut would otherwise be heard: up
// to a block of the audio a cut was meant to remove, and a click where it stops.
void silencePastEnd(QVector<float> &out, drift::TimeUs winStartUs, drift::TimeUs audibleEndUs, int sampleRate)
{
    const int frames = out.size() / 2;
    const int64_t tail = ((audibleEndUs - winStartUs) * sampleRate + drift::kUsPerSecond - 1) / drift::kUsPerSecond;
    for (int i = static_cast<int>(std::clamp<int64_t>(tail, 0, frames)); i < frames; ++i) {
        out[i * 2] = 0.0f;
        out[i * 2 + 1] = 0.0f;
    }
}

} // namespace

QVector<float> AudioMixer::readClipAudio(const drift::Clip &clip, quint64 streamId,
                                         drift::TimeUs winStartUs, int outFrames, int sampleRate,
                                         drift::ClipAudioRetimer *retimer, const SourceReader &source,
                                         drift::TimeUs audibleStartUs, drift::TimeUs audibleEndUs)
{
    QVector<float> out;
    readClipAudio(out, clip, streamId, winStartUs, outFrames, sampleRate, retimer, source,
                  audibleStartUs, audibleEndUs);
    return out;
}

void AudioMixer::readClipAudio(QVector<float> &out, const drift::Clip &clip, quint64 streamId,
                               drift::TimeUs winStartUs, int outFrames, int sampleRate,
                               drift::ClipAudioRetimer *retimer, const SourceReader &source,
                               drift::TimeUs audibleStartUs, drift::TimeUs audibleEndUs)
{
    out.resize(qMax(0, outFrames) * 2);
    std::fill(out.begin(), out.end(), 0.0f);
    if (outFrames <= 0)
        return;

    const drift::TimeUs startUs = audibleStartUs >= 0 ? qMin(audibleStartUs, clip.timelineStart) : clip.timelineStart;
    const drift::TimeUs endUs = audibleEndUs >= 0 ? qMax(audibleEndUs, clip.timelineEnd()) : clip.timelineEnd();
    // Inside the clip this is exactly timelineToSourceUs; only a handle needs the unclamped form.
    const bool extended = startUs < clip.timelineStart || endUs > clip.timelineEnd();
    const auto toSource = [&clip, extended](drift::TimeUs t) {
        return extended ? clip.timelineToSourceUsUnclamped(t) : clip.timelineToSourceUs(t);
    };

    const drift::TimeUs winDurUs = framesToUs(outFrames, sampleRate);
    const drift::TimeUs winEndUs = winStartUs + winDurUs;
    if (winEndUs <= startUs || winStartUs >= endUs)
        return; // window is entirely outside the clip — pure silence

    // Clamp the window to the clip; frames before the clip's start stay as the leading zeros above.
    const drift::TimeUs playStartUs = qMax(winStartUs, startUs);
    const int leadFrames = static_cast<int>(((playStartUs - winStartUs) * sampleRate) / drift::kUsPerSecond);
    const int wantFrames = outFrames - leadFrames;
    if (wantFrames <= 0)
        return;

    // Whether a clip is retimed is a property of the clip, not of the moment. A ramp that happens
    // to pass through 1.0 in this block still goes through the stretcher: bypassing it for one
    // block would skip the source read, leave the retimer's cursor where it was, and re-enter the
    // stream with a hole in it.
    if (!clip.hasSpeedCurve() && qFuzzyCompare(clip.effectiveSpeed(), 1.0)) {
        // Frame counts are derived in the sample domain, never by going through microseconds. A
        // block whose duration is not a whole number of microseconds — 1024 frames at 48 kHz is
        // 21333.33 — used to truncate twice, once into µs and once back out, and ask the decoder
        // for 1023 frames to fill 1024. The frame left behind stayed at the buffer's initial zero,
        // putting a single-sample dropout on every block boundary: a periodic impulse, which is a
        // harmonic comb all the way to Nyquist.
        const drift::TimeUs sourceSpanUs = qMax<drift::TimeUs>(1, framesToUs(wantFrames, sampleRate));
        // Reverse reads the block ahead of the mapped position and flips it below.
        const drift::TimeUs sourceStartUs =
            clip.reverse ? qMax<drift::TimeUs>(0, toSource(playStartUs) - sourceSpanUs)
                         : toSource(playStartUs);

        const int got =
            source ? source(sourceStartUs, wantFrames, out.data() + leadFrames * 2)
                   : ClipReaderPool::instance().readAudioInterleaved(
                         clip.path, streamId, sourceStartUs, wantFrames, sampleRate,
                         out.data() + leadFrames * 2, clip.audioStreamIndex);
        if (got > 1 && clip.reverse) {
            for (int i = leadFrames, j = leadFrames + got - 1; i < j; ++i, --j) {
                std::swap(out[i * 2], out[j * 2]);
                std::swap(out[i * 2 + 1], out[j * 2 + 1]);
            }
        }
        silencePastEnd(out, winStartUs, endUs, sampleRate);
        return;
    }

    if (!retimer)
        return;

    // Take the ramp's average over the block rather than its value at the left edge: differencing
    // the mapping is the curve's integral, so the audio cannot slowly slide against the picture
    // over a long ramp. The exception is the final, partly-overhanging block — timelineToSourceUs
    // clamps at the clip's end, so differencing there would under-report the rate.
    const drift::TimeUs blockEndUs = playStartUs + framesToUs(wantFrames, sampleRate);
    double tempo = clip.effectiveSpeed();
    if (clip.hasSpeedCurve()) {
        tempo = blockEndUs <= clip.timelineEnd()
                    ? static_cast<double>(clip.timelineToSourceUs(blockEndUs)
                                          - clip.timelineToSourceUs(playStartUs))
                          / static_cast<double>(blockEndUs - playStartUs)
                    : clip.speedCurve.speedAtTimelineOffset(playStartUs - clip.timelineStart,
                                                            clip.srcOut - clip.srcIn);
    }

    drift::ClipAudioBlock block;
    block.identity = clipAudioIdentity(clip);
    block.sampleRate = sampleRate;
    block.timelineStartUs = playStartUs;
    // For a reversed clip this is already the source position of the first sample in playback
    // order, which is exactly what the retimer's cursor means; it walks backwards from there.
    block.sourceStartUs = toSource(playStartUs);
    block.tempo = tempo;
    block.reverse = clip.reverse;

    const QString path = clip.path;
    const int audioStreamIndex = clip.audioStreamIndex;
    retimer->process(
        block,
        [&path, &source, streamId, sampleRate, audioStreamIndex](drift::TimeUs sourceStartUs, int frames,
                                                                 float *dst) {
            if (source)
                return source(sourceStartUs, frames, dst);
            return ClipReaderPool::instance().readAudioInterleaved(path, streamId, sourceStartUs, frames,
                                                                   sampleRate, dst, audioStreamIndex);
        },
        wantFrames, out.data() + leadFrames * 2);
    silencePastEnd(out, winStartUs, endUs, sampleRate);
}

namespace {

constexpr int kMaxCompositeDepth = 4;

void accumulateClipAudio(const drift::Project &project, const drift::Clip &clip,
                         const drift::Track &track, drift::TimeUs timelineStartUs,
                         int sampleCount, int sampleRate, float *mixBuffer,
                         QMutex &stateMutex,
                         QHash<QString, std::shared_ptr<ClipAudioState>> &clipAudio,
                         const QList<const drift::Clip *> &laneClips, size_t laneKey,
                         quint64 streamSalt, int depth,
                         quint64 snapshotSerial, float *outPeakL = nullptr,
                         float *outPeakR = nullptr)
{
    if (clip.path.isEmpty() && clip.sequenceId.isEmpty())
        return;
    if (!clip.sequenceId.isEmpty()
        && (depth >= kMaxCompositeDepth || !project.hasSequence(clip.sequenceId)))
        return;

    const drift::TimeUs bufferEndUs = timelineStartUs + static_cast<drift::TimeUs>(
                                                            (static_cast<int64_t>(sampleCount) * drift::kUsPerSecond)
                                                            / sampleRate);

    // A transition across a butt cut can play this clip into the media past its edges.
    drift::AudioTransitionEdge edge;
    if (!track.transitions.isEmpty())
        edge = drift::audioTransitionEdgeFor(track, clip, drift::sourceDurationForClip(project, clip));
    const drift::TimeUs audibleStartUs = clip.timelineStart - edge.extendBeforeUs;
    const drift::TimeUs audibleEndUs = clip.timelineEnd() + edge.extendAfterUs;

    if (bufferEndUs <= audibleStartUs || timelineStartUs >= audibleEndUs)
        return;

    const drift::TimeUs blockDurUs = static_cast<drift::TimeUs>(
        (static_cast<int64_t>(sampleCount) * drift::kUsPerSecond) / sampleRate);

    // Hold a strong reference rather than pointing into the hash. mix() runs on the audio thread
    // while resetClipAudioState() clears this hash from the GUI thread on every seek, play and
    // pause: an unguarded operator[] can rehash underneath that clear(), and a reference into the
    // hash dangles the moment clear() drops the last owner — the state's buffers are then freed
    // while this thread is still processing into them.
    //
    // Looked up unconditionally, above the effects branch: a retimed clip needs its stretcher
    // whether or not it also has an effect chain.
    std::shared_ptr<ClipAudioState> statePtr;
    {
        QMutexLocker locker(&stateMutex);
        statePtr = clipAudio.value(clip.id);
        if (!statePtr) {
            statePtr = std::make_shared<ClipAudioState>();
            clipAudio.insert(clip.id, statePtr);
        }
    }
    // Safe even if the hash is cleared right now: this copy keeps the state alive until the block
    // finishes, and the next block simply builds a fresh one.
    ClipAudioState &state = *statePtr;

    const quint64 streamId = ClipReaderPool::streamIdForClip(clip.id) ^ streamSalt;

    // A composite's source is its nested timeline's mix, read at the clip's source time.
    AudioMixer::SourceReader source;
    if (!clip.sequenceId.isEmpty()) {
        if (!state.nestedMixer) {
            state.nestedView = std::make_shared<drift::Project>();
            state.nestedMixer = std::make_shared<AudioMixer>();
            state.nestedMixer->setMasterClipEnabled(false);
            state.nestedMixer->setNesting(qHashMulti(streamSalt, clip.id), depth + 1);
            state.nestedMixer->setProject(state.nestedView.get());
        }
        // Refreshed whenever the project changed so edits inside the composite are heard; the
        // address stays put, so the nested mixer keeps its per-clip state. Keyed on the snapshot
        // serial, not its address: a new snapshot can be allocated where a freed one was.
        if (snapshotSerial == 0 || state.nestedSerial != snapshotSerial) {
            *state.nestedView = project.sequenceView(clip.sequenceId);
            state.nestedSerial = snapshotSerial;
        }
        AudioMixer *nested = state.nestedMixer.get();
        source = [nested, sampleRate](drift::TimeUs sourceStartUs, int frames, float *dst) {
            nested->mix(sourceStartUs, frames, sampleRate, dst);
            return frames;
        };
    }

    QVector<float> &chunk = state.chunk;
    // The clip's own stack plus whatever the nested audio lanes on its track contribute right
    // now. A lane can begin and end part-way through a clip, so this list changes shape mid-clip
    // — which is exactly the case the rebuild check below exists to cover.
    if (!clip.audioEffects.isEmpty() || !laneClips.isEmpty()) {
        drift::AudioEffectRack &rack = state.rack;
        if (snapshotSerial == 0 || state.effectSpecsSerial != snapshotSerial
            || state.effectSpecsKey != laneKey) {
            QList<drift::Effect> effectChain = clip.audioEffects;
            for (const drift::Clip *adjustment : laneClips)
                effectChain.append(adjustment->audioEffects);
            state.effectSpecs = audioEffectSpecsFor(effectChain);
            state.effectSpecsSerial = snapshotSerial;
            state.effectSpecsKey = laneKey;
        }

        const drift::TimeUs lastEndUs = rack.lastTimelineEndUs();
        const bool continuous = lastEndUs >= 0
                                && qAbs(timelineStartUs - lastEndUs) <= kTimelineGapToleranceUs;

        // A rebuild means the stages themselves are new and hold no history, so it is as much a
        // discontinuity as a seek. Without this, a lane starting mid-clip opens its tail cold.
        bool rebuilt = false;
        const bool active =
            rack.configure(state.effectSpecs, sampleRate, &rebuilt);
        if (active && (!continuous || rebuilt)) {
            rack.reset();
            // Warm the stages on the audio immediately before this block. That is what makes an
            // echo tail already present after a seek instead of fading in from silence, and what
            // lines up a latent stage instead of leaving it permanently late.
            const int primeFrames = rack.primeFrames();
            if (primeFrames > 0) {
                const drift::TimeUs primeStartUs =
                    timelineStartUs
                    - static_cast<drift::TimeUs>((static_cast<int64_t>(primeFrames) * drift::kUsPerSecond)
                                                 / sampleRate);
                // The preroll window ends exactly where this block starts, so the retimer sees one
                // continuous stream across the two reads and does not restart between them.
                AudioMixer::readClipAudio(state.preroll, clip, streamId, primeStartUs, primeFrames,
                                          sampleRate, &state.retimer, source, audibleStartUs,
                                          audibleEndUs);
                rack.warmUp(state.preroll.constData(), primeFrames);
            }
        }

        AudioMixer::readClipAudio(chunk, clip, streamId, timelineStartUs, sampleCount, sampleRate,
                                  &state.retimer, source, audibleStartUs, audibleEndUs);
        if (active)
            rack.process(chunk.data(), sampleCount);
        rack.setLastTimelineEndUs(timelineStartUs + blockDurUs);
    } else {
        AudioMixer::readClipAudio(chunk, clip, streamId, timelineStartUs, sampleCount, sampleRate,
                                  &state.retimer, source, audibleStartUs, audibleEndUs);
    }

    const int frames = qMin(sampleCount, chunk.size() / 2);
    // Pan is a plain scalar, so it is constant across the block — hoisted out of the loop.
    float panL = 1.0f;
    float panR = 1.0f;
    panGainsForClip(clip, &panL, &panR);
    float trackPanL = 1.0f;
    float trackPanR = 1.0f;
    panGainsForTrack(track, &trackPanL, &trackPanR);
    const float trackVol = static_cast<float>(qMax(0.0, track.volume));

    // The clip gain (volume keys, transition, fades, edge ramps) is evaluated at control points
    // every kGainStep frames and interpolated between them. Each evaluation walks keyframes and
    // the track's transitions; doing that per sample was most of the mixer's time, and at 32
    // frames (under a millisecond) the steps are far finer than any of those curves change.
    constexpr int kGainStep = 32;
    const auto gainAt = [&](int frame) {
        const drift::TimeUs t =
            timelineStartUs + static_cast<drift::TimeUs>((static_cast<int64_t>(frame) * drift::kUsPerSecond) / sampleRate);
        // An edge a butt-cut transition covers is shaped by the transition alone.
        return static_cast<float>(volumeForClip(clip, t) * transitionGainForClip(track, clip, t, edge)
                                  * clip.fadeMultiplier(t, edge.ownsIn, edge.ownsOut)
                                  * clip.audioEdgeMultiplier(t, edge.ownsIn, edge.ownsOut));
    };
    const float scaleL = panL * trackVol * trackPanL;
    const float scaleR = panR * trackVol * trackPanR;

    float peakL = 0.0f;
    float peakR = 0.0f;
    float gainA = frames > 0 ? gainAt(0) : 0.0f;
    for (int start = 0; start < frames; start += kGainStep) {
        const int end = qMin(frames, start + kGainStep);
        // The span's far end: the next control point, or the block's last frame.
        const float gainB = end < frames ? gainAt(end) : gainAt(frames - 1);
        const int span = end < frames ? kGainStep : qMax(1, frames - 1 - start);
        const float slope = (gainB - gainA) / float(span);
        for (int i = start; i < end; ++i) {
            const float gain = gainA + slope * float(i - start);
            const float sampleL = chunk[i * 2] * gain * scaleL;
            const float sampleR = chunk[i * 2 + 1] * gain * scaleR;
            mixBuffer[i * 2] += sampleL;
            mixBuffer[i * 2 + 1] += sampleR;
            peakL = qMax(peakL, qAbs(sampleL));
            peakR = qMax(peakR, qAbs(sampleR));
        }
        gainA = gainB;
    }
    if (outPeakL)
        *outPeakL = qMax(*outPeakL, peakL);
    if (outPeakR)
        *outPeakR = qMax(*outPeakR, peakR);
}

} // namespace

void AudioMixer::setProject(const drift::Project *project)
{
    if (m_project != project) {
        QMutexLocker locker(&m_clipAudioMutex);
        m_clipAudio.clear();
        // A snapshot of the previous project must not outlive the switch.
        QMutexLocker snapshotLocker(&m_snapshotMutex);
        m_snapshot.reset();
        m_snapshotSerial = 0;
    }
    m_project = project;
}

void AudioMixer::setSnapshot(std::shared_ptr<const drift::Project> snapshot, quint64 serial)
{
    QMutexLocker locker(&m_snapshotMutex);
    m_snapshot = std::move(snapshot);
    m_snapshotSerial = m_snapshot ? serial : 0;
}

void AudioMixer::resetClipAudioState()
{
    {
        QMutexLocker locker(&m_clipAudioMutex);
        m_clipAudio.clear();
    }
    {
        QMutexLocker lock(&m_levelsMutex);
        m_trackLevels.clear();
        m_masterLevels = {0.0f, 0.0f};
        m_trackMeterPeaks.clear();
        m_masterMeterPeak = {0.0f, 0.0f};
    }
    // The decoders behind those clips are just as discontinuous. Their sequential fast path cannot
    // see a playhead move on its own — a forward seek shorter than its threshold reads as ordinary
    // playback — so it has to be told.
    ClipReaderPool::instance().resetAudioStreams();
}

namespace {

// Distinct from any clip id, which is always a UUID, so the bus can share the per-clip state hash
// and be torn down by resetClipAudioState() on seek along with everything else.
const QString kMasterBusStateKey = QStringLiteral("__master_bus__");

// The audio-kind adjustments on each track's nested lanes append their effects to each of that
// track's clips, which is the audio mirror of how a video lane folds into the clip's own layer
// pass. Standalone ones are the master bus: an audio track has no z-order, so "everything below"
// simply means the whole mix, and every live one contributes to a single chain.
AudioMixer::AudioAdjustments collectAudioAdjustments(const drift::Project &project)
{
    AudioMixer::AudioAdjustments result;
    const QList<drift::Track> &tracks = project.tracks();
    for (int i = 0; i < tracks.size(); ++i) {
        const drift::Track &track = tracks.at(i);
        if (!track.isAdjustment() || track.muted || track.hidden)
            continue;
        QList<const drift::Clip *> *target = &result.masterBus;
        if (track.isAdjustmentLane()) {
            const int parent = drift::adjustmentLaneParentIndex(project, i);
            if (parent < 0)
                continue;
            target = &result.lanes[parent];
        }
        for (const drift::Clip &adjustment : track.clips) {
            if (adjustment.adjustmentKind == drift::AdjustmentKind::AudioEffects
                && !adjustment.audioEffects.isEmpty())
                target->append(&adjustment);
        }
    }
    return result;
}

// The adjustments among `candidates` live at `timelineUs`, into `live`. Returns a key for that
// set, which is all that can change an effect chain within one snapshot.
size_t liveAdjustments(const QList<const drift::Clip *> &candidates, drift::TimeUs timelineUs,
                       QList<const drift::Clip *> &live)
{
    live.clear();
    size_t key = 0;
    for (const drift::Clip *adjustment : candidates) {
        if (!adjustment->containsTime(timelineUs))
            continue;
        live.append(adjustment);
        key = qHashMulti(key, adjustment->id);
    }
    return key;
}

} // namespace

void AudioMixer::mix(drift::TimeUs timelineStartUs, int sampleCount, int sampleRate,
                     float *interleavedStereoOut) const
{
    // Held for the whole block. The GUI thread keeps a reference to the previous snapshot until
    // the next swap, so letting go of this one here never frees a project on the audio thread.
    std::shared_ptr<const drift::Project> hold;
    quint64 serial = 0;
    {
        QMutexLocker locker(&m_snapshotMutex);
        hold = m_snapshot;
        serial = m_snapshotSerial;
    }
    const drift::Project *project = hold ? hold.get() : m_project;
    if (!interleavedStereoOut || sampleCount <= 0 || !project)
        return;

    std::memset(interleavedStereoOut, 0, static_cast<size_t>(sampleCount) * 2 * sizeof(float));

    const QList<drift::Track> &tracks = project->tracks();
    bool anySolo = false;
    for (const drift::Track &t : tracks) {
        if (t.solo) {
            anySolo = true;
            break;
        }
    }

    // A snapshot's adjustments are fixed, so they are gathered once per serial; without one the
    // project can change between blocks and they are gathered every time.
    AudioAdjustments unsnapshotted;
    if (serial != 0 && m_adjustments.serial != serial) {
        m_adjustments = collectAudioAdjustments(*project);
        m_adjustments.serial = serial;
    } else if (serial == 0) {
        unsnapshotted = collectAudioAdjustments(*project);
    }
    const AudioAdjustments &adjustments = serial != 0 ? m_adjustments : unsnapshotted;
    QList<const drift::Clip *> live;

    QHash<int, QPair<float, float>> newTrackLevels;
    for (int ti = 0; ti < tracks.size(); ++ti) {
        const drift::Track &track = tracks.at(ti);
        if (track.muted || track.hidden || (anySolo && !track.solo))
            continue;

        // Adjustment tracks carry no audio of their own: a lane's effects reach the mix through
        // the clips it modifies, a standalone one through the master bus below.
        if (track.isAdjustment())
            continue;

        const size_t laneKey =
            liveAdjustments(adjustments.lanes.value(ti), timelineStartUs, live);

        float peakL = 0.0f;
        float peakR = 0.0f;
        if (track.type == drift::TrackType::Audio) {
            for (const drift::Clip &clip : track.clips)
                accumulateClipAudio(*project, clip, track, timelineStartUs, sampleCount, sampleRate,
                                    interleavedStereoOut, m_clipAudioMutex, m_clipAudio,
                                    live, laneKey, m_streamSalt, m_depth, serial, &peakL, &peakR);
        } else if (track.type == drift::TrackType::Video) {
            for (const drift::Clip &clip : track.clips) {
                if ((clip.type == drift::ClipType::Video || clip.type == drift::ClipType::Composite)
                    && !clip.suppressEmbeddedAudio)
                    accumulateClipAudio(*project, clip, track, timelineStartUs, sampleCount,
                                        sampleRate, interleavedStereoOut, m_clipAudioMutex,
                                        m_clipAudio, live, laneKey, m_streamSalt, m_depth, serial,
                                        &peakL, &peakR);
            }
        }
        newTrackLevels.insert(ti, {peakL, peakR});
    }

    // The master bus runs on the summed mix, before the limiter — an adjustment that raises level
    // must still be caught by the soft clip rather than sitting outside it.
    const size_t busKey = liveAdjustments(adjustments.masterBus, timelineStartUs, live);
    if (!live.isEmpty()) {
        std::shared_ptr<ClipAudioState> statePtr;
        {
            // Keyed like a clip so resetClipAudioState() tears the bus down on seek along with
            // everything else; the id cannot collide with a clip's UUID.
            QMutexLocker locker(&m_clipAudioMutex);
            statePtr = m_clipAudio.value(kMasterBusStateKey);
            if (!statePtr) {
                statePtr = std::make_shared<ClipAudioState>();
                m_clipAudio.insert(kMasterBusStateKey, statePtr);
            }
        }
        drift::AudioEffectRack &rack = statePtr->rack;

        const drift::TimeUs lastEndUs = rack.lastTimelineEndUs();
        const bool continuous = lastEndUs >= 0
                                && qAbs(timelineStartUs - lastEndUs) <= kTimelineGapToleranceUs;

        ClipAudioState &state = *statePtr;
        if (serial == 0 || state.effectSpecsSerial != serial || state.effectSpecsKey != busKey) {
            QList<drift::Effect> busEffects;
            for (const drift::Clip *adjustment : std::as_const(live))
                busEffects.append(adjustment->audioEffects);
            state.effectSpecs = audioEffectSpecsFor(busEffects);
            state.effectSpecsSerial = serial;
            state.effectSpecsKey = busKey;
        }
        bool rebuilt = false;
        const bool active = rack.configure(state.effectSpecs, sampleRate, &rebuilt);
        // No preroll here, unlike a clip: the bus's input is the mix itself, which cannot be
        // re-read for the window before this block without re-running every clip. A tail on the
        // master therefore opens cold after a seek.
        if (active && (!continuous || rebuilt))
            rack.reset();
        if (active)
            rack.process(interleavedStereoOut, sampleCount);
    }

    if (m_masterMuted) {
        std::memset(interleavedStereoOut, 0, static_cast<size_t>(sampleCount) * 2 * sizeof(float));
    } else {
        const float mGain = static_cast<float>(qMax(0.0, m_masterVolume));
        if (!qFuzzyCompare(mGain, 1.0f)) {
            for (int i = 0; i < sampleCount * 2; ++i)
                interleavedStereoOut[i] *= mGain;
        }
        if (m_masterClipEnabled) {
            for (int i = 0; i < sampleCount * 2; ++i)
                interleavedStereoOut[i] = softClip(interleavedStereoOut[i]);
        }
    }

    float mPeakL = 0.0f;
    float mPeakR = 0.0f;
    for (int i = 0; i < sampleCount; ++i) {
        mPeakL = qMax(mPeakL, qAbs(interleavedStereoOut[i * 2]));
        mPeakR = qMax(mPeakR, qAbs(interleavedStereoOut[i * 2 + 1]));
    }

    {
        QMutexLocker lock(&m_levelsMutex);
        for (auto it = newTrackLevels.cbegin(); it != newTrackLevels.cend(); ++it) {
            QPair<float, float> &peak = m_trackMeterPeaks[it.key()];
            peak.first = qMax(peak.first, it.value().first);
            peak.second = qMax(peak.second, it.value().second);
        }
        m_trackLevels = std::move(newTrackLevels);
        m_masterLevels = {mPeakL, mPeakR};
        m_masterMeterPeak.first = qMax(m_masterMeterPeak.first, mPeakL);
        m_masterMeterPeak.second = qMax(m_masterMeterPeak.second, mPeakR);
    }
}

QList<float> AudioMixer::takeMeterPeaks(const QList<int> &trackIndexes) const
{
    QList<float> out;
    out.reserve(2 + trackIndexes.size() * 2);
    QMutexLocker lock(&m_levelsMutex);
    out.append(m_masterMeterPeak.first);
    out.append(m_masterMeterPeak.second);
    for (int trackIndex : trackIndexes) {
        const QPair<float, float> peak = m_trackMeterPeaks.value(trackIndex, {0.0f, 0.0f});
        out.append(peak.first);
        out.append(peak.second);
    }
    m_trackMeterPeaks.clear();
    m_masterMeterPeak = {0.0f, 0.0f};
    return out;
}

QPair<float, float> AudioMixer::trackLevels(int trackIndex) const
{
    QMutexLocker lock(&m_levelsMutex);
    return m_trackLevels.value(trackIndex, {0.0f, 0.0f});
}

QPair<float, float> AudioMixer::masterLevels() const
{
    QMutexLocker lock(&m_levelsMutex);
    return m_masterLevels;
}
