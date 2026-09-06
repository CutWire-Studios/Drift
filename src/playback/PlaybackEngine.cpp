#include "PlaybackEngine.h"

#include "engine/AndroidUri.h"
#include "engine/ClipReaderPool.h"
#include "engine/GpuCompositor.h"
#include "engine/HwAccel.h"

#include <QSettings>
#include <QVariantMap>

#ifdef Q_OS_ANDROID
#include <QJniEnvironment>
#include <QJniObject>
#include <QtCore/qcoreapplication_platform.h>
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>

namespace {

#ifdef Q_OS_ANDROID

constexpr const char *kAudioFocusClass = "org/cutwire/drift/AudioFocus";

// The engine that owns preview audio. Focus loss and the headphone-unplug broadcast are dispatched
// on the Android UI thread, so the pause cannot be run there: it is posted to the engine's own
// thread instead. Playback is single-instance, so the last engine constructed is the right one.
std::atomic<PlaybackEngine *> g_audioFocusEngine{nullptr};

void nativePausePlayback(JNIEnv *, jclass)
{
    if (PlaybackEngine *engine = g_audioFocusEngine.load(std::memory_order_acquire))
        QMetaObject::invokeMethod(engine, &PlaybackEngine::pause, Qt::QueuedConnection);
}

// Both calls hop to the Android UI thread: AudioFocus keeps its request and its receiver in static
// fields that only that thread touches, which is also the thread the callbacks arrive on.
void callAudioFocus(const char *method)
{
    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([method] {
        QJniObject context = QNativeInterface::QAndroidApplication::context();
        if (!context.isValid())
            return;
        QJniObject::callStaticMethod<void>(kAudioFocusClass, method,
                                           "(Landroid/content/Context;)V", context.object());
        // Focus is advisory: the preview still plays if the request could not be made, it just
        // plays over whatever else is running.
        QJniEnvironment().checkAndClearExceptions();
    });
}

void requestAudioFocus()
{
    callAudioFocus("request");
}

void abandonAudioFocus()
{
    callAudioFocus("abandon");
}

#else
inline void requestAudioFocus() {}
inline void abandonAudioFocus() {}
#endif

constexpr int kPlayheadUpdateMs = 16; // ~60 Hz UI updates, independent of video decode

// GPU compositor readiness polling. The first ask is deferred past the window's
// own bring-up so it neither delays launch nor reports a failure that has not
// happened yet; the cap keeps a machine that will never have a share context
// from probing for the rest of the session.
constexpr int kGpuProbeDelayMs = 300;
constexpr int kGpuProbeIntervalMs = 250;
constexpr int kGpuProbeMaxAttempts = 20;

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

// "full", "half" and "quarter" are fractions of the preview panel (device pixels),
// never of the project: Full matches the panel, Half/Quarter are 1/2 and 1/4 of
// that, all capped at project resolution. "auto" is Full plus a compositor ratchet
// that trades remaining resolution for cadence when playback cannot keep up.
bool isKnownPreviewQuality(const QString &quality)
{
    return quality == QStringLiteral("full") || quality == QStringLiteral("half")
        || quality == QStringLiteral("quarter") || quality == QStringLiteral("auto");
}

constexpr QLatin1String kHwPrefix("hw:");

// Which backend a "hw:<id>" mode names, or None for every other mode.
drift::hwaccel::Backend decodeBackendFromString(const QString &mode)
{
    if (!mode.startsWith(kHwPrefix))
        return drift::hwaccel::Backend::None;
    return drift::hwaccel::backendFromId(mode.mid(kHwPrefix.size()));
}

// Canonical form of a decode mode, or empty when it names nothing this build knows.
// A backend that is not on this machine resolves to Auto rather than to a mode that
// would silently never engage — settings outlive the GPU they were written on.
QString normalizeDecodeMode(const QString &mode)
{
    const QString lowered = mode.toLower();
    if (lowered == QStringLiteral("auto") || lowered == QStringLiteral("software"))
        return lowered;

    const QList<drift::hwaccel::Backend> available = drift::hwaccel::availableDecodeBackends();
    // Legacy "hardware" (and anything that means "any GPU") pins the backend the probe
    // would have chosen, so the picker can show what is actually in use.
    if (lowered == QStringLiteral("hardware")) {
        return available.isEmpty()
            ? QStringLiteral("auto")
            : kHwPrefix + drift::hwaccel::id(available.first());
    }
    if (lowered.startsWith(kHwPrefix)) {
        const drift::hwaccel::Backend backend = decodeBackendFromString(lowered);
        if (backend != drift::hwaccel::Backend::None && available.contains(backend))
            return lowered;
        return QStringLiteral("auto");
    }
    return {};
}

ClipReader::HardwareDecodeMode decodeModeFromString(const QString &mode)
{
    if (mode == QStringLiteral("software"))
        return ClipReader::HardwareDecodeMode::Software;
    if (mode == QStringLiteral("hardware") || mode.startsWith(kHwPrefix))
        return ClipReader::HardwareDecodeMode::Hardware;
    return ClipReader::HardwareDecodeMode::Auto;
}

QString loadSavedDecodeMode()
{
    const QString saved = QSettings().value(QStringLiteral("preview/decodeMode")).toString();
    if (const QString normalized = normalizeDecodeMode(saved); !normalized.isEmpty())
        return normalized;
    // Previous two-state toggle wrote a bool. Keep an explicit Hardware or
    // Software choice; missing key (never touched) becomes Auto.
    if (QSettings().contains(QStringLiteral("preview/hardwareDecode"))) {
        return QSettings().value(QStringLiteral("preview/hardwareDecode")).toBool()
            ? normalizeDecodeMode(QStringLiteral("hardware"))
            : QStringLiteral("software");
    }
    return QStringLiteral("auto");
}

} // namespace

PlaybackEngine::PlaybackEngine(QObject *parent)
    : QObject(parent)
    , m_audio(QStringLiteral("PlaybackAudio"), this)
{
    const QString saved = QSettings().value(QStringLiteral("preview/quality")).toString().toLower();
    if (isKnownPreviewQuality(saved))
        m_previewQuality = saved;

    m_decodeMode = loadSavedDecodeMode();
    ClipReaderPool::instance().setHardwareDecodeMode(decodeModeFromString(m_decodeMode),
                                                     decodeBackendFromString(m_decodeMode));
    m_hwFallbackCount = ClipReader::hardwareFallbackCount();

    m_compositor.setDropLateFrames(!isQualityMode());
    m_compositor.setAdaptiveQuality(isAutoQuality());
    m_compositor.setStats(&m_stats);

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

    // The GPU compositor needs Qt Quick's global share context, which does not
    // exist until the first QQuickWindow initialises — after this constructor. So
    // do not ask now: ask shortly after, and keep asking while the answer is still
    // "not yet", otherwise a project opened from the command line composites once,
    // fails, and leaves the preview black for the session.
    m_gpuProbeTimer.setInterval(kGpuProbeIntervalMs);
    connect(&m_gpuProbeTimer, &QTimer::timeout, this, &PlaybackEngine::probeGpuCompositor);
    QTimer::singleShot(kGpuProbeDelayMs, this, [this] {
        probeGpuCompositor();
        if (m_gpuStatusId == QStringLiteral("unknown") || !gpuCompositorReady())
            m_gpuProbeTimer.start();
    });

#ifdef Q_OS_ANDROID
    g_audioFocusEngine.store(this, std::memory_order_release);
    QJniEnvironment().registerNativeMethods(
        kAudioFocusClass,
        {{"nativePausePlayback", "()V", reinterpret_cast<void *>(nativePausePlayback)}});
#endif
}

PlaybackEngine::~PlaybackEngine()
{
#ifdef Q_OS_ANDROID
    // A focus change already in flight on the Android UI thread must not find this engine.
    g_audioFocusEngine.store(nullptr, std::memory_order_release);
    abandonAudioFocus();
#endif
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
    if (m_playing) {
        m_sinkPlayedUsOffset = m_audio.processedUSecs();
        m_clock.start();
    }
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
    // The grid position is stale after a jump: without this the next display tick can
    // quantise to the frame that is already on screen and decline to redraw it.
    m_lastRequestedFrameUs = -1;
    // reset() clears the running flag; resume the clock if we are still in play
    // so edits/seeks during playback don't freeze audio at one timeline spot.
    // Quality mode has no clock — its loop picks the new playhead up on the next
    // completed frame.
    if (!m_playing)
        refreshFrame();
    else if (!isQualityMode()) {
        m_sinkPlayedUsOffset = m_audio.processedUSecs();
        m_clock.start();
    }
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

QImage PlaybackEngine::previewImage() const
{
    QMutexLocker lock(&m_frameMutex);
    return m_currentFrame.image;
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

QString PlaybackEngine::decodeMode() const
{
    return m_decodeMode;
}

QVariantList PlaybackEngine::decodeModes() const
{
    QVariantList modes;
    auto append = [&modes](const QString &id, const QString &label) {
        modes.append(QVariantMap{{QStringLiteral("id"), id}, {QStringLiteral("label"), label}});
    };
    append(QStringLiteral("auto"), tr("Auto"));
    append(QStringLiteral("software"), tr("Software"));
    // Only backends whose device opens here, so every listed choice is one that runs.
    for (const drift::hwaccel::Backend backend : drift::hwaccel::availableDecodeBackends()) {
        append(kHwPrefix + drift::hwaccel::id(backend),
               tr("Hardware (%1)").arg(QString::fromLatin1(drift::hwaccel::name(backend))));
    }
    return modes;
}

void PlaybackEngine::setDecodeMode(const QString &mode)
{
    const QString normalized = normalizeDecodeMode(mode);
    if (normalized.isEmpty()) {
        qWarning("PlaybackEngine: ignoring unknown decode mode '%s'", qPrintable(mode));
        return;
    }
    if (m_decodeMode == normalized)
        return;

    m_decodeMode = normalized;
    QSettings().setValue(QStringLiteral("preview/decodeMode"), m_decodeMode);
    ClipReaderPool::instance().setHardwareDecodeMode(decodeModeFromString(m_decodeMode),
                                                     decodeBackendFromString(m_decodeMode));
    // A new path gets a fresh benefit of the doubt: a fallback under the old one says
    // nothing about this one, and leaving the count behind would suppress the notice.
    m_hwFallbackCount = ClipReader::hardwareFallbackCount();
    emit decodeModeChanged();
    refreshFrame();
}

// Readers drop to software on their own when a driver fails mid-decode, which is
// otherwise invisible — the preview just gets slower. Called per composited frame;
// the check is one relaxed atomic load.
void PlaybackEngine::checkHardwareFallback()
{
    const quint64 count = ClipReader::hardwareFallbackCount();
    if (count == m_hwFallbackCount)
        return;
    m_hwFallbackCount = count;
    if (m_decodeMode == QStringLiteral("software"))
        return;

    const drift::hwaccel::Backend backend = decodeBackendFromString(m_decodeMode);
    emit hardwareDecodeFellBack(backend == drift::hwaccel::Backend::None
                                    ? QString()
                                    : QString::fromLatin1(drift::hwaccel::name(backend)));
}

// Ask the GPU compositor whether it is up, and republish the answer when it
// changes. Also the recovery path: the share context can appear after the
// preview's one and only composite request has already failed, so a compositor
// that comes good is re-asked for the frame nobody else will ask for again.
void PlaybackEngine::probeGpuCompositor()
{
    // Forces an attempt rather than reading the last one; status() alone would
    // stay "unknown" forever if nothing else ever composited.
    const bool ready = GpuCompositor::isAvailable();
    const drift::gl::GlStatusInfo info = GpuCompositor::status();

    // The compositor's context is the one that actually draws, so its vendor is the
    // authoritative answer to "which GPU should be decoding". Startup seeds this from a
    // throwaway probe; this corrects it if the two ever disagree.
    if (!info.vendor.isEmpty())
        drift::hwaccel::setRenderVendor(info.vendor);

    const QString id = QString::fromLatin1(drift::gl::statusId(info.status));
    const QString detail = drift::gl::describeGl(info);
    if (id != m_gpuStatusId || detail != m_gpuStatusDetail) {
        m_gpuStatusId = id;
        m_gpuStatusDetail = detail;
        emit gpuCompositorStatusChanged();
    }

    if (ready) {
        m_gpuProbeTimer.stop();
        refreshFrame();
        return;
    }

    // Keep retrying only while the answer could still change, and not forever:
    // a share context that has not appeared in a few seconds is not coming.
    ++m_gpuProbeAttempts;
    if (drift::gl::isTransient(info.status) && m_gpuProbeAttempts < kGpuProbeMaxAttempts)
        return;

    m_gpuProbeTimer.stop();
    if (!m_gpuUnavailableNotified) {
        m_gpuUnavailableNotified = true;
        emit gpuCompositorUnavailable(m_gpuStatusId, m_gpuStatusDetail);
    }
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
    // Every quality mode sizes the canvas from the panel, so a resize has to
    // rebuild the frame. Decode size is quantized, which keeps the reader cache
    // from dropping on every pixel of a drag.
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
    // Android dims and locks on its own idle timer, which would be wrong mid-playback even
    // without touch input; no-op on desktop.
    drift::android::acquireKeepScreenOn();

    if (isQualityMode()) {
        // Quality mode is not realtime: the playhead steps one frame per
        // completed composite, so there is no clock for audio to follow and the
        // sink stays stopped. The loop re-arms itself from onCompositeFinished.
        emit playingChanged();
        m_qualityRequestUs = m_playheadUs;
        m_compositor.requestComposite(m_playheadUs, playbackRenderOptions());
        return;
    }

    // Only the realtime path makes sound — quality mode leaves the sink stopped — and taking
    // focus for a silent render would interrupt whatever the user is listening to for nothing.
    requestAudioFocus();
    ensureAudioSink();
    m_sinkPlayedUsOffset = m_audio.processedUSecs();
    m_clock.start();

    // Opening the device may settle on a rate the project did not ask for, which comes back as
    // sampleRateChanged and re-anchors the clock — so this has to follow m_playing being set.
    m_audio.start();

    emit playingChanged();

    m_stats.reset();
    m_lastRequestedFrameUs = -1;
    m_playheadTimer.start(kPlayheadUpdateMs);
    syncDisplayCadence();

    onPlayheadTick();
    onCompositeTick();
}

void PlaybackEngine::syncDisplayCadence()
{
    if (!m_playing || isQualityMode())
        return;

    const int fps = m_project ? qMax(1, m_project->fps()) : 30;
    // This is a display cadence, not a timeline one: a wall second should show about fps frames
    // whatever the rate, and above 1x that simply means covering more timeline per frame. Below 1x
    // the same interval would re-request the frame already on screen several times over, so the
    // tick stretches with the rate instead.
    const double frameMs = drift::usToSeconds(drift::frameDurationUs(fps)) * 1000.0;
    // Rounded, not truncated. The cast this replaces turned 16.67 ms into 16, which asks a
    // 60 Hz display for 62.5 frames a second: the two drift through a full frame of phase
    // about every half second, and the picture hitches each time they cross. Whole
    // milliseconds still cannot express 16.67, which is why the display, not this timer,
    // drives playback whenever a window is telling us its refresh rate.
    const int tickMs =
        qMax(1, static_cast<int>(std::lround(frameMs * qMax(1.0, 1.0 / m_playbackRate))));
    // Watchdog interval when the display is driving: long enough that it only fires once the
    // swap stream has genuinely stopped (hidden window, no compositor throttling, headless).
    m_compositeTimer.start(m_refreshRate > 0.0 ? tickMs * 2 : tickMs);
    // Auto quality reacts to composites that overrun two display ticks. Deriving
    // the budget from the tick keeps it tied to the real cadence at this fps and
    // rate, instead of a fixed figure that only ever suited 30 fps at 1x.
    m_compositor.setLateFrameBudgetMs(tickMs * 2);
}

qint64 PlaybackEngine::refreshIntervalNs() const
{
    if (m_refreshRate <= 0.0)
        return 0;
    return static_cast<qint64>(std::llround(1'000'000'000.0 / m_refreshRate));
}

void PlaybackEngine::setDisplayRefreshRate(double hz)
{
    const double rate = hz > 0.0 ? hz : 0.0;
    if (qFuzzyCompare(m_refreshRate, rate))
        return;
    m_refreshRate = rate;
    m_stats.setRefreshRate(m_refreshRate);
    // Re-arm: the watchdog interval depends on whether a display is driving us, and dragging
    // the window to a panel with a different refresh rate changes the lead time too.
    syncDisplayCadence();
}

void PlaybackEngine::onFrameSwapped()
{
    // Queued from the render thread, so this only counts. Comparing this rate against the
    // delivered rate is what separates "we cannot composite fast enough" from "we composite
    // fine but the display never gets to show the frames evenly".
    m_stats.noteDisplayed();
}

void PlaybackEngine::onDisplayTick()
{
    m_lastDisplayTickNs = PlaybackClock::nowNs();
    if (!m_playing || !m_project || isQualityMode())
        return;
    requestFrameForPresentation();
}

void PlaybackEngine::requestFrameForPresentation()
{
    const drift::TimeUs step = frameStepUs();
    if (step <= 0)
        return;

    // The composite started now reaches the screen at the *next* swap, not this one. Choosing
    // the frame for that instant is what decouples motion from composite latency: aiming at
    // "now" instead means every millisecond of variation in how long a frame takes to build
    // moves the picture, which is what the eye reads as stutter even when the frame rate is
    // nominally fine.
    const drift::TimeUs targetUs = m_clock.currentTimeUs() + refreshIntervalNs() / 1000;

    // Quantise onto the project's frame grid so each source frame is requested exactly once
    // and then held for however many refreshes fall before the next one is due — a steady 2:2
    // for 30 fps on a 60 Hz panel, 2:3 for 24. Requesting on a cadence of our own instead is
    // what let 60 fps content beat against a 60 Hz display.
    const drift::TimeUs frameUs = (targetUs / step) * step;
    if (frameUs == m_lastRequestedFrameUs)
        return;
    m_lastRequestedFrameUs = frameUs;

    m_compositor.requestComposite(frameUs, playbackRenderOptions());
}

void PlaybackEngine::pause()
{
    if (!m_playing)
        return;

    m_playing = false;
    m_compositor.setPlaybackActive(false);
    drift::android::releaseKeepScreenOn();
    abandonAudioFocus();
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

    // Backstop only. While the display is producing frames it schedules the preview, and this
    // timer has to stay out of the way — two clocks asking for frames is the arrangement that
    // produced the beat in the first place.
    if (const qint64 interval = refreshIntervalNs();
        interval > 0 && m_lastDisplayTickNs != 0
        && PlaybackClock::nowNs() - m_lastDisplayTickNs < 2 * interval) {
        return;
    }

    requestFrameForPresentation();
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
    checkHardwareFallback();

    if (!frame.isValid())
        return;

    {
        QMutexLocker lock(&m_frameMutex);
        m_currentFrame = frame;
    }
    m_stats.setUploadPath(GpuCompositor::previewUploadPathId());
    emit currentFrameChanged();
}

FrameCompositor::RenderOptions PlaybackEngine::playbackRenderOptions() const
{
    FrameCompositor::RenderOptions options;
    double qualityFraction = 1.0;
    if (m_previewQuality == QStringLiteral("quarter"))
        qualityFraction = 0.25;
    else if (m_previewQuality == QStringLiteral("half"))
        qualityFraction = 0.5;

    // Fit the project into the panel (device pixels), never larger than 1:1 with
    // the export frame. Until the panel has reported a size, stay at project
    // resolution so the first composite is not a stub.
    double fit = 1.0;
    if (m_project && m_previewRenderWidth > 0 && m_previewRenderHeight > 0) {
        const double widthScale =
            static_cast<double>(m_previewRenderWidth) / qMax(1, m_project->width());
        const double heightScale =
            static_cast<double>(m_previewRenderHeight) / qMax(1, m_project->height());
        fit = qMin(1.0, qMin(widthScale, heightScale));
    }
    options.previewScale = qBound(kMinPreviewScale, fit * qualityFraction, 1.0);

    // During fast playback, cap temporal history so time_echo cannot multiply
    // decode work unboundedly. Paused, scrubbed and quality-mode frames keep the
    // full history: those are exactly the cases where fidelity is the point.
    options.maxTimeEchoHistoryFrames = m_playing && !isQualityMode() ? 12 : -1;

    // Buffer decoded frames ahead of the playhead only while realtime playback is
    // actually running: paused and quality-mode frames have no deadline to miss,
    // and read-ahead during editing is thrown away by the next edit.
    options.readAheadUs = m_playing && !isQualityMode() ? kReadAheadUs : 0;

    // Hide the text clip being edited in place so the QML inline editor stands in
    // for it. Never applies while playing (no inline edit during playback).
    if (!m_playing)
        options.skipClipId = m_editingClipId;

    return options;
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
    const qint64 playedUs = qMax(qint64(0), m_audio.processedUSecs() - m_sinkPlayedUsOffset);
    m_clock.syncPlaybackUs(static_cast<drift::TimeUs>(playedUs));
    return sampleCount;
}
