#include "AudioMixer.h"

#include "AudioEffectCatalog.h"
#include "ClipReaderPool.h"
#include "TransitionCatalog.h"
#include "core/Clip.h"
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

double transitionGainForClip(const drift::Track &track, const drift::Clip &clip, drift::TimeUs timelineUs)
{
    drift::TimeUs windowStart = 0;
    drift::TimeUs windowEnd = 0;
    const drift::Transition *transition = drift::activeTransitionAt(track, timelineUs, windowStart, windowEnd);
    if (!transition)
        return 1.0;

    const double p = drift::transitionProgress(timelineUs, windowStart, windowEnd);
    const TransitionPresetEntry *def = transitionDefForId(transition->kindId);
    const QString curve = def ? def->audioCurve : QStringLiteral("crossfade");
    const drift::TransitionAudioGains gains = drift::transitionAudioGains(curve, p);
    if (clip.id == transition->fromClipId)
        return gains.outgoing;
    if (clip.id == transition->toClipId)
        return gains.incoming;
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
QVector<float> AudioMixer::readClipAudio(const drift::Clip &clip, quint64 streamId,
                                         drift::TimeUs winStartUs, int outFrames, int sampleRate,
                                         drift::ClipAudioRetimer *retimer)
{
    QVector<float> out(outFrames * 2, 0.0f);
    if (outFrames <= 0)
        return out;

    const drift::TimeUs winDurUs = framesToUs(outFrames, sampleRate);
    const drift::TimeUs winEndUs = winStartUs + winDurUs;
    if (winEndUs <= clip.timelineStart || winStartUs >= clip.timelineEnd())
        return out; // window is entirely outside the clip — pure silence

    // Clamp the window to the clip; frames before the clip's start stay as the leading zeros above.
    const drift::TimeUs playStartUs = qMax(winStartUs, clip.timelineStart);
    const int leadFrames = static_cast<int>(((playStartUs - winStartUs) * sampleRate) / drift::kUsPerSecond);
    const int wantFrames = outFrames - leadFrames;
    if (wantFrames <= 0)
        return out;

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
            clip.reverse ? qMax<drift::TimeUs>(0, clip.timelineToSourceUs(playStartUs) - sourceSpanUs)
                         : clip.timelineToSourceUs(playStartUs);

        const int got = ClipReaderPool::instance().readAudioInterleaved(
            clip.path, streamId, sourceStartUs, wantFrames, sampleRate, out.data() + leadFrames * 2,
            clip.audioStreamIndex);
        if (got > 1 && clip.reverse) {
            for (int i = leadFrames, j = leadFrames + got - 1; i < j; ++i, --j) {
                std::swap(out[i * 2], out[j * 2]);
                std::swap(out[i * 2 + 1], out[j * 2 + 1]);
            }
        }
        return out;
    }

    if (!retimer)
        return out;

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
    block.sourceStartUs = clip.timelineToSourceUs(playStartUs);
    block.tempo = tempo;
    block.reverse = clip.reverse;

    const QString path = clip.path;
    const int audioStreamIndex = clip.audioStreamIndex;
    retimer->process(
        block,
        [&path, streamId, sampleRate, audioStreamIndex](drift::TimeUs sourceStartUs, int frames, float *dst) {
            return ClipReaderPool::instance().readAudioInterleaved(path, streamId, sourceStartUs, frames,
                                                                   sampleRate, dst, audioStreamIndex);
        },
        wantFrames, out.data() + leadFrames * 2);
    return out;
}

namespace {

void accumulateClipAudio(const drift::Clip &clip, const drift::Track &track, drift::TimeUs timelineStartUs,
                         int sampleCount, int sampleRate, float *mixBuffer,
                         QMutex &stateMutex,
                         QHash<QString, std::shared_ptr<ClipAudioState>> &clipAudio)
{
    if (clip.path.isEmpty())
        return;

    const drift::TimeUs bufferEndUs = timelineStartUs + static_cast<drift::TimeUs>(
                                                            (static_cast<int64_t>(sampleCount) * drift::kUsPerSecond)
                                                            / sampleRate);

    const bool overlaps = clip.containsTime(timelineStartUs) || clip.containsTime(bufferEndUs - 1)
                          || (timelineStartUs < clip.timelineStart && bufferEndUs > clip.timelineEnd());
    if (!overlaps)
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

    const quint64 streamId = ClipReaderPool::streamIdForClip(clip.id);

    QVector<float> chunk;
    if (!clip.audioEffects.isEmpty()) {
        drift::AudioEffectRack &rack = state.rack;

        const drift::TimeUs lastEndUs = rack.lastTimelineEndUs();
        const bool continuous = lastEndUs >= 0
                                && qAbs(timelineStartUs - lastEndUs) <= kTimelineGapToleranceUs;

        const bool active = rack.configure(audioEffectSpecsFor(clip.audioEffects), sampleRate);
        if (active && !continuous) {
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
                const QVector<float> preroll = AudioMixer::readClipAudio(
                    clip, streamId, primeStartUs, primeFrames, sampleRate, &state.retimer);
                rack.warmUp(preroll.constData(), primeFrames);
            }
        }

        chunk = AudioMixer::readClipAudio(clip, streamId, timelineStartUs, sampleCount, sampleRate,
                                          &state.retimer);
        if (active)
            rack.process(chunk.data(), sampleCount);
        rack.setLastTimelineEndUs(timelineStartUs + blockDurUs);
    } else {
        chunk = AudioMixer::readClipAudio(clip, streamId, timelineStartUs, sampleCount, sampleRate,
                                          &state.retimer);
    }

    const int frames = qMin(sampleCount, chunk.size() / 2);
    for (int i = 0; i < frames; ++i) {
        const drift::TimeUs sampleTimeUs =
            timelineStartUs + static_cast<drift::TimeUs>((static_cast<int64_t>(i) * drift::kUsPerSecond) / sampleRate);
        const float gain = static_cast<float>(volumeForClip(clip, sampleTimeUs)
                                              * transitionGainForClip(track, clip, sampleTimeUs)
                                              * clip.fadeMultiplier(sampleTimeUs));
        mixBuffer[i * 2] += chunk[i * 2] * gain;
        mixBuffer[i * 2 + 1] += chunk[i * 2 + 1] * gain;
    }
}

} // namespace

void AudioMixer::setProject(const drift::Project *project)
{
    if (m_project != project) {
        QMutexLocker locker(&m_clipAudioMutex);
        m_clipAudio.clear();
    }
    m_project = project;
}

void AudioMixer::resetClipAudioState()
{
    {
        QMutexLocker locker(&m_clipAudioMutex);
        m_clipAudio.clear();
    }
    // The decoders behind those clips are just as discontinuous. Their sequential fast path cannot
    // see a playhead move on its own — a forward seek shorter than its threshold reads as ordinary
    // playback — so it has to be told.
    ClipReaderPool::instance().resetAudioStreams();
}

void AudioMixer::mix(drift::TimeUs timelineStartUs, int sampleCount, int sampleRate,
                     float *interleavedStereoOut) const
{
    if (!interleavedStereoOut || sampleCount <= 0 || !m_project)
        return;

    std::memset(interleavedStereoOut, 0, static_cast<size_t>(sampleCount) * 2 * sizeof(float));

    for (const drift::Track &track : m_project->tracks()) {
        if (track.muted || track.hidden)
            continue;

        if (track.type == drift::TrackType::Audio) {
            for (const drift::Clip &clip : track.clips)
                accumulateClipAudio(clip, track, timelineStartUs, sampleCount, sampleRate,
                                      interleavedStereoOut, m_clipAudioMutex, m_clipAudio);
        } else if (track.type == drift::TrackType::Video) {
            for (const drift::Clip &clip : track.clips) {
                if (clip.type == drift::ClipType::Video && !clip.suppressEmbeddedAudio)
                    accumulateClipAudio(clip, track, timelineStartUs, sampleCount, sampleRate,
                                          interleavedStereoOut, m_clipAudioMutex, m_clipAudio);
            }
        }
    }

    for (int i = 0; i < sampleCount * 2; ++i)
        interleavedStereoOut[i] = softClip(interleavedStereoOut[i]);
}
