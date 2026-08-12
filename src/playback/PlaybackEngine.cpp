#include "PlaybackEngine.h"

#include <QSettings>

#include <algorithm>
#include <array>
#include <cstring>

namespace {

constexpr int kPlayheadUpdateMs = 16; // ~60 Hz UI updates, independent of video decode

// How much decoded source each clip's reader keeps buffered ahead of the
// playhead during fast playback. This absorbs frames that decode slower than
// realtime (long GOPs, a heavy transition) by spending the slack on either side
// of them. It is deliberately seconds and not minutes: the frames are held in
// RAM per clip — 2 s of 720p NV12 is ~83 MB — and every edit or seek discards
// the part of the buffer past the change.
constexpr drift::TimeUs kReadAheadUs = 2 * drift::kUsPerSecond;

// The rates the preview transport offers. All sit inside the stretcher's own clamp
// (kMinCurveSpeed..kMaxCurveSpeed), and nothing outside this list is accepted.
constexpr std::array<double, 6> kPlaybackRates{0.25, 0.5, 1.0, 1.5, 2.0, 4.0};

// "full", "half" and "quarter" are exact: the preview renders at the fraction
// asked for and nothing changes it underneath. "auto" starts from what "full"
// would give and lets the compositor trade resolution for cadence.
bool isKnownPreviewQuality(const QString &quality)
{
    return quality == QStringLiteral("full") || quality == QStringLiteral("half")
        || quality == QStringLiteral("quarter") || quality == QStringLiteral("auto");
}

} // namespace

PlaybackEngine::PlaybackEngine(QObject *parent)
    : QObject(parent)
    , m_audio(QStringLiteral("PlaybackAudio"), this)
{
    const QString saved = QSettings().value(QStringLiteral("preview/quality")).toString().toLower();
    if (isKnownPreviewQuality(saved))
        m_previewQuality = saved;

    const QString savedMode =
        QSettings().value(QStringLiteral("preview/playbackMode")).toString().toLower();
    if (savedMode == QStringLiteral("fast") || savedMode == QStringLiteral("quality"))
        m_playbackMode = savedMode;
    m_compositor.setDropLateFrames(!isQualityMode());
    m_compositor.setAdaptiveQuality(isAutoQuality());

    m_audio.setFillCallback([this](float *stereo, int frames) { return fillAudio(stereo, frames); });
    connect(&m_audio, &AudioOutputChannel::sampleRateChanged, this,
            &PlaybackEngine::onAudioSampleRateChanged);
    connect(&m_audio, &AudioOutputChannel::errorOccurred, this, &PlaybackEngine::audioError);
    m_sampleRate = m_audio.sampleRate();

    m_playheadTimer.setTimerType(Qt::PreciseTimer);
    m_compositeTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_playheadTimer, &QTimer::timeout, this, &PlaybackEngine::onPlayheadTick);
    connect(&m_compositeTimer, &QTimer::timeout, this, &PlaybackEngine::onCompositeTick);
    connect(&m_compositor, &CompositorService::frameReady, this, &PlaybackEngine::onFrameReady);
    connect(&m_compositor, &CompositorService::compositeFinished, this,
            &PlaybackEngine::onCompositeFinished);
}

PlaybackEngine::~PlaybackEngine()
{
    m_playing = false;
    m_playheadTimer.stop();
    m_compositeTimer.stop();
    m_clock.stop();

    // Blocking, so the audio thread cannot be inside fillAudio() while the members it reads are
    // torn down under it. The channel closes the sink and joins its thread from its own destructor.
    m_audio.stop();
}

void PlaybackEngine::ensureAudioSink()
{
    if (m_project)
        m_audio.setPreferredSampleRate(m_project->sampleRate());
    m_sampleRate = m_audio.sampleRate();
}

// The device would not take the project's rate, or the sink moved to a device that runs at a
// different one. Everything downstream of the sink counts in output samples, so re-anchor it.
void PlaybackEngine::onAudioSampleRateChanged()
{
    m_sampleRate = m_audio.sampleRate();
    m_mixer.resetClipAudioState();
    m_audioStreamGeneration.fetch_add(1, std::memory_order_release);
    if (isQualityMode())
        return;

    m_clock.reset(m_playheadUs, m_sampleRate);
    if (m_playing)
        m_clock.start();
}

void PlaybackEngine::setProject(drift::Project *project)
{
    m_project = project;
    m_mixer.setProject(project);
    m_compositor.setProject(project);
    refreshFrame();
}

void PlaybackEngine::setPlayheadUs(drift::TimeUs us)
{
    m_playheadUs = qMax<drift::TimeUs>(0, us);
    // Only real seeks reach here — the playhead tick emits its position directly rather than
    // routing back through this setter. That matters: the mixer's per-clip DSP is streaming, and a
    // reset on every tick would leave a retimed clip permanently re-priming instead of playing.
    m_mixer.resetClipAudioState();
    m_audioStreamGeneration.fetch_add(1, std::memory_order_release);
    m_clock.reset(m_playheadUs, m_sampleRate);
    // reset() clears the running flag; resume the clock if we are still in play
    // so edits/seeks during playback don't freeze audio at one timeline spot.
    // Quality mode has no clock — its loop picks the new playhead up on the next
    // completed frame.
    if (!m_playing)
        refreshFrame();
    else if (!isQualityMode())
        m_clock.start();
}

int PlaybackEngine::previewTextureId() const
{
    QMutexLocker lock(&m_frameMutex);
    return static_cast<int>(m_currentFrame.textureId);
}

QSize PlaybackEngine::previewTextureSize() const
{
    QMutexLocker lock(&m_frameMutex);
    return m_currentFrame.size;
}

QString PlaybackEngine::previewQuality() const
{
    return m_previewQuality;
}

void PlaybackEngine::setPreviewQuality(const QString &quality)
{
    const QString normalized = quality.toLower();
    // An unrecognized value used to silently become "half", which quietly changed
    // what the user was looking at. Ignore it instead.
    if (!isKnownPreviewQuality(normalized)) {
        qWarning("PlaybackEngine: ignoring unknown preview quality '%s'", qPrintable(quality));
        return;
    }
    if (m_previewQuality == normalized)
        return;

    m_previewQuality = normalized;
    QSettings().setValue(QStringLiteral("preview/quality"), m_previewQuality);
    m_compositor.setAdaptiveQuality(isAutoQuality());
    emit previewQualityChanged();
    refreshFrame();
}

QString PlaybackEngine::playbackMode() const
{
    return m_playbackMode;
}

void PlaybackEngine::setPlaybackMode(const QString &mode)
{
    const QString normalized = mode.toLower();
    if (normalized != QStringLiteral("fast") && normalized != QStringLiteral("quality")) {
        qWarning("PlaybackEngine: ignoring unknown playback mode '%s'", qPrintable(mode));
        return;
    }
    if (m_playbackMode == normalized)
        return;

    m_playbackMode = normalized;
    QSettings().setValue(QStringLiteral("preview/playbackMode"), m_playbackMode);
    m_compositor.setDropLateFrames(!isQualityMode());
    emit playbackModeChanged();

    // The two modes drive the playhead from different sources and differ on
    // whether the sink runs, so switching mid-playback restarts the transport
    // from where it currently sits.
    if (m_playing) {
        pause();
        play();
    }
}

void PlaybackEngine::setPlaybackRate(double rate)
{
    const auto match = std::find_if(kPlaybackRates.begin(), kPlaybackRates.end(),
                                    [rate](double candidate) { return qFuzzyCompare(candidate, rate); });
    if (match == kPlaybackRates.end()) {
        qWarning("PlaybackEngine: ignoring unsupported playback rate %f", rate);
        return;
    }
    if (qFuzzyCompare(m_playbackRate, *match))
        return;

    // Every position the clock reports is derived from its total rendered sample count, so a rate
    // applied mid-flight would rescale audio that has already been played. Restart the transport
    // from where it sits instead, exactly as a mode change does. Pausing first is also what makes
    // the assignment safe: fillAudio reads the rate on the audio thread, and pause() does not
    // return until the sink has stopped.
    const bool wasPlaying = m_playing;
    if (wasPlaying)
        pause();

    m_playbackRate = *match;
    emit playbackRateChanged();

    if (wasPlaying)
        play();
}

drift::TimeUs PlaybackEngine::frameStepUs() const
{
    return drift::frameDurationUs(m_project ? qMax(1, m_project->fps()) : 30);
}

void PlaybackEngine::setPreviewRenderSize(int width, int height)
{
    width = qMax(0, width);
    height = qMax(0, height);
    if (m_previewRenderWidth == width && m_previewRenderHeight == height)
        return;

    m_previewRenderWidth = width;
    m_previewRenderHeight = height;
    // Only Auto derives its scale from this size; re-compositing at a fixed
    // fraction would render the identical frame again on every pixel of a drag.
    if (!isAutoQuality())
        return;
    if (m_playing)
        onCompositeTick();
    else
        refreshFrame();
}

bool PlaybackEngine::hasFrame() const
{
    QMutexLocker lock(&m_frameMutex);
    return m_currentFrame.isValid();
}

void PlaybackEngine::play()
{
    if (m_playing)
        return;

    m_mixer.resetClipAudioState();
    m_audioStreamGeneration.fetch_add(1, std::memory_order_release);
    m_clock.setRate(m_playbackRate);
    m_clock.reset(m_playheadUs, m_sampleRate);
    m_playing = true;
    m_compositor.setPlaybackActive(true);

    if (isQualityMode()) {
        // Quality mode is not realtime: the playhead steps one frame per
        // completed composite, so there is no clock for audio to follow and the
        // sink stays stopped. The loop re-arms itself from onCompositeFinished.
        emit playingChanged();
        m_qualityRequestUs = m_playheadUs;
        m_compositor.requestComposite(m_playheadUs, playbackRenderOptions());
        return;
    }

    ensureAudioSink();
    m_clock.start();

    // Opening the device may settle on a rate the project did not ask for, which comes back as
    // sampleRateChanged and re-anchors the clock — so this has to follow m_playing being set.
    m_audio.start();

    emit playingChanged();

    m_playheadTimer.start(kPlayheadUpdateMs);

    const int fps = m_project ? qMax(1, m_project->fps()) : 30;
    // This is a display cadence, not a timeline one: a wall second should show about fps frames
    // whatever the rate, and above 1x that simply means covering more timeline per frame. Below 1x
    // the same interval would re-request the frame already on screen several times over, so the
    // tick stretches with the rate instead.
    const double frameMs = drift::usToSeconds(drift::frameDurationUs(fps)) * 1000.0;
    const int tickMs = qMax(1, static_cast<int>(frameMs * qMax(1.0, 1.0 / m_playbackRate)));
    m_compositeTimer.start(tickMs);
    // Auto quality reacts to composites that overrun two display ticks. Deriving
    // the budget from the tick keeps it tied to the real cadence at this fps and
    // rate, instead of a fixed figure that only ever suited 30 fps at 1x.
    m_compositor.setLateFrameBudgetMs(tickMs * 2);

    onPlayheadTick();
    onCompositeTick();
}

void PlaybackEngine::pause()
{
    if (!m_playing)
        return;

    m_playing = false;
    m_compositor.setPlaybackActive(false);
    m_playheadTimer.stop();
    m_compositeTimer.stop();
    m_clock.pause();
    // In quality mode the frame loop owns the playhead; the clock never ran.
    if (!isQualityMode())
        m_playheadUs = m_clock.pausedAt();
    m_qualityRequestUs = -1;
    m_mixer.resetClipAudioState();
    m_audioStreamGeneration.fetch_add(1, std::memory_order_release);
    m_audio.stop();
    emit playingChanged();
    emit playheadUsChanged(static_cast<quint64>(m_playheadUs));
    refreshFrame();
}

void PlaybackEngine::refreshFrame()
{
    // Edits / seeks take a fresh project snapshot; the play loop reuses one.
    m_compositor.invalidateSnapshot();
    // Use the same RenderOptions as the play loop so paused/scrubbed frames
    // match what playback shows (preview scale + temporal-effect history).
    m_compositor.requestComposite(m_playheadUs, playbackRenderOptions());
}

void PlaybackEngine::checkEndOfTimeline(drift::TimeUs timeUs)
{
    if (!m_project)
        return;

    drift::TimeUs loopIn = 0;
    drift::TimeUs loopOut = 0;
    if (shouldLoopWorkArea(&loopIn, &loopOut)) {
        if (timeUs >= loopOut) {
            setPlayheadUs(loopIn);
            return;
        }
        return;
    }

    const drift::TimeUs durationUs = m_project->durationUs();
    if (timeUs >= durationUs) {
        m_playheadUs = durationUs;
        emit playheadUsChanged(static_cast<quint64>(m_playheadUs));
        QMetaObject::invokeMethod(this, &PlaybackEngine::pause, Qt::QueuedConnection);
    }
}

bool PlaybackEngine::shouldLoopWorkArea(drift::TimeUs *loopInOut, drift::TimeUs *loopOutOut) const
{
    if (!m_loopWorkArea || !m_project || !m_project->hasWorkArea())
        return false;

    const drift::TimeUs loopIn = m_project->workAreaInUs();
    const drift::TimeUs loopOut = m_project->workAreaOutUs();
    if (loopOut <= loopIn)
        return false;

    if (loopInOut)
        *loopInOut = loopIn;
    if (loopOutOut)
        *loopOutOut = loopOut;
    return true;
}

void PlaybackEngine::onPlayheadTick()
{
    if (!m_playing || !m_project)
        return;

    const drift::TimeUs timeUs = m_clock.currentTimeUs();
    if (timeUs == m_playheadUs)
        return;

    m_playheadUs = timeUs;
    emit playheadUsChanged(static_cast<quint64>(timeUs));
    checkEndOfTimeline(timeUs);
}

void PlaybackEngine::onCompositeTick()
{
    if (!m_playing || !m_project)
        return;

    m_compositor.requestComposite(m_clock.currentTimeUs(), playbackRenderOptions());
}

void PlaybackEngine::onCompositeFinished()
{
    if (!m_playing || !m_project || !isQualityMode())
        return;

    // Step forward only if the playhead is still where this frame was requested;
    // a seek that arrived while it rendered is honoured instead of skipped past.
    if (m_qualityRequestUs == m_playheadUs) {
        m_playheadUs += frameStepUs();
        emit playheadUsChanged(static_cast<quint64>(m_playheadUs));
        checkEndOfTimeline(m_playheadUs);
        if (m_playheadUs >= m_project->durationUs())
            return;
    }

    m_qualityRequestUs = m_playheadUs;
    m_compositor.requestComposite(m_playheadUs, playbackRenderOptions());
}

void PlaybackEngine::onFrameReady(const GpuFrameTexture &frame)
{
    if (!frame.isValid())
        return;

    {
        QMutexLocker lock(&m_frameMutex);
        m_currentFrame = frame;
    }
    emit currentFrameChanged();
}

FrameCompositor::RenderOptions PlaybackEngine::playbackRenderOptions() const
{
    FrameCompositor::RenderOptions options;
    if (m_previewQuality == QStringLiteral("quarter")) {
        options.previewScale = 0.25;
    } else if (m_previewQuality == QStringLiteral("half")) {
        options.previewScale = 0.5;
    } else {
        options.previewScale = 1.0;
    }
    // During fast playback, cap temporal history so time_echo cannot multiply
    // decode work unboundedly. Paused, scrubbed and quality-mode frames keep the
    // full history: those are exactly the cases where fidelity is the point.
    options.maxTimeEchoHistoryFrames = m_playing && !isQualityMode() ? 12 : -1;

    // Buffer decoded frames ahead of the playhead only while realtime playback is
    // actually running: paused and quality-mode frames have no deadline to miss,
    // and read-ahead during editing is thrown away by the next edit.
    options.readAheadUs = m_playing && !isQualityMode() ? kReadAheadUs : 0;

    // Auto is the only mode that looks at how big the preview is on screen: it
    // renders no more pixels than are actually displayed, and the compositor may
    // take it further down while playback cannot keep up. Full, Half and Quarter
    // are fractions of the project resolution and nothing else — Full composites
    // the same frame the export would, whatever size the panel happens to be.
    // That matters beyond the video: renderScale also sizes text rasterisation,
    // shape strokes and every effect's pixel radii, so a canvas fitted to a small
    // panel renders coarse titles and graphics, not just a smaller picture.
    if (isAutoQuality() && m_project && m_previewRenderWidth > 0 && m_previewRenderHeight > 0) {
        const double widthScale = static_cast<double>(m_previewRenderWidth) / qMax(1, m_project->width());
        const double heightScale = static_cast<double>(m_previewRenderHeight) / qMax(1, m_project->height());
        options.previewScale = qBound(0.1, qMin(widthScale, heightScale), 1.0);
    }

    // Hide the text clip being edited in place so the QML inline editor stands in
    // for it. Never applies while playing (no inline edit during playback).
    if (!m_playing)
        options.skipClipId = m_editingClipId;

    options.maskViewClipId = m_maskViewClipId;

    return options;
}

void PlaybackEngine::setMaskViewClipId(const QString &id)
{
    if (m_maskViewClipId == id)
        return;
    m_maskViewClipId = id;
    refreshFrame();
}

void PlaybackEngine::setEditingClipId(const QString &id)
{
    if (m_editingClipId == id)
        return;
    m_editingClipId = id;
    if (!m_playing)
        refreshFrame();
}

int PlaybackEngine::fillAudio(float *buffer, int sampleCount)
{
    if (!buffer || sampleCount <= 0)
        return 0;

    if (!m_playing || !m_project) {
        std::memset(buffer, 0, static_cast<size_t>(sampleCount) * 2 * sizeof(float));
        return sampleCount;
    }

    // Mix at the produce position (audio we are generating into the buffer),
    // then anchor the visible playhead to what the sink has actually played so
    // video follows audio rather than leading it by the buffer depth.
    if (qFuzzyCompare(m_playbackRate, 1.0)) {
        m_mixer.mix(m_clock.produceTimeUs(), sampleCount, m_sampleRate, buffer);
    } else {
        // Off 1x, the whole mix is treated as one source and stretched, which keeps the pitch and
        // leaves AudioMixer alone — it maps sample counts to timeline microseconds 1:1 throughout,
        // and threading a global rate through it would touch every clip overlap test in there.
        // The retimer's "timeline" here is the sink's own output, and its "source" is the project
        // timeline; it owns the read cursor, so the mixer is pulled at whatever position it wants.
        drift::ClipAudioBlock block;
        block.identity = m_audioStreamGeneration.load(std::memory_order_acquire);
        block.sampleRate = m_sampleRate;
        block.timelineStartUs = m_clock.renderedFramesUs();
        block.sourceStartUs = m_clock.produceTimeUs();
        block.tempo = m_playbackRate;

        m_rateRetimer.process(
            block,
            [this](drift::TimeUs sourceStartUs, int frames, float *dst) {
                m_mixer.mix(sourceStartUs, frames, m_sampleRate, dst);
                // The mixer is silent rather than exhausted past the end of the timeline; playback
                // stops on the playhead reaching the duration, not on the source running out.
                return frames;
            },
            sampleCount, buffer);
    }
    m_clock.onAudioSamplesRendered(sampleCount);
    m_clock.syncPlaybackUs(static_cast<drift::TimeUs>(m_audio.processedUSecs()));
    return sampleCount;
}
