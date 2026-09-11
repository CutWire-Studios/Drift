#include "ClipPreviewPlayer.h"

#include "ClipPreviewImageStore.h"
#include "engine/AudioMixer.h"
#include "engine/ClipReaderPool.h"
#include "engine/ReverseProxyCache.h"

#include <cstring>

namespace {

constexpr int kPositionUpdateMs = 16; // ~60 Hz, independent of decode

// The window is a working surface, not a mastering monitor: decoding at preview size keeps the
// pump ahead of the clock even where the ramp asks for 8×.
constexpr int kFrameMaxWidth = 960;
constexpr int kFrameMaxHeight = 540;

// The auditioner scrubs its own range while the timeline may be playing the same clip. Salting the
// timeline's stream id keeps the two off each other's decode cursors.
constexpr quint64 kPreviewStreamSalt = 0x9E3779B97F4A7C15ull;

} // namespace

void ClipPreviewFrameWorker::decode(const QString &path, quint64 streamId, qint64 sourceUs,
                                    int maxWidth, int maxHeight, quint64 token, int rotationOverride)
{
    if (token < latestToken.load(std::memory_order_acquire))
        return; // a newer position arrived while this sat in the queue

    const QImage image = ClipReaderPool::instance().readVideoFrame(
        path, streamId, static_cast<drift::TimeUs>(sourceUs), maxWidth, maxHeight,
        QString(), 15, false, rotationOverride);
    if (!image.isNull())
        emit decoded(image, token);
}

ClipPreviewPlayer::ClipPreviewPlayer(QObject *parent)
    : QObject(parent)
    , m_audio(QStringLiteral("ClipPreviewAudio"), this)
    , m_frameWorker(new ClipPreviewFrameWorker)
{
    m_audio.setFillCallback([this](float *stereo, int frames) { return fillAudio(stereo, frames); });
    connect(&m_audio, &AudioOutputChannel::sampleRateChanged, this,
            &ClipPreviewPlayer::onAudioSampleRateChanged);
    m_sampleRate = m_audio.sampleRate();

    m_frameWorker->moveToThread(&m_frameThread);
    m_frameThread.setObjectName(QStringLiteral("ClipPreviewDecode"));
    connect(&m_frameThread, &QThread::finished, m_frameWorker, &QObject::deleteLater);
    connect(m_frameWorker, &ClipPreviewFrameWorker::decoded, this, &ClipPreviewPlayer::onDecoded);
    m_frameThread.start();

    m_positionTimer.setTimerType(Qt::PreciseTimer);
    m_frameTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_positionTimer, &QTimer::timeout, this, &ClipPreviewPlayer::onPositionTick);
    connect(&m_frameTimer, &QTimer::timeout, this, [this] { requestFrame(positionUs()); });
}

ClipPreviewPlayer::~ClipPreviewPlayer()
{
    m_playing = false;
    m_positionTimer.stop();
    m_frameTimer.stop();
    m_clock.stop();

    // Blocking, so no refill can be inside fillAudio() while this object comes apart.
    m_audio.stop();

    m_frameThread.quit();
    m_frameThread.wait();
}

void ClipPreviewPlayer::ensureAudioSink()
{
    m_sampleRate = m_audio.sampleRate();
}

// The device settled on a rate other than the project's; the retimer and clock both count in
// output samples, so start them over on the new one.
void ClipPreviewPlayer::onAudioSampleRateChanged()
{
    m_sampleRate = m_audio.sampleRate();
    m_clock.reset(m_positionUs, m_sampleRate);
    if (m_playing)
        m_clock.start();
}

void ClipPreviewPlayer::setClip(const drift::Clip &clip, int sampleRate, int fps)
{
    pause();

    m_audio.setPreferredSampleRate(qMax(8000, sampleRate));
    m_sampleRate = m_audio.sampleRate();
    m_fps = qMax(1, fps);

    {
        QMutexLocker lock(&m_clipMutex);
        m_clip = clip;
        m_clip.timelineStart = 0;
        m_clip.syncDurationFromSpeedCurve();
    }

    m_positionUs = 0;
    m_frameSize = QSize();
    ClipPreviewImageStore::clear();

    emit durationChanged();
    emit positionChanged();
    requestFrame(0);
}

void ClipPreviewPlayer::setSpeedCurve(const drift::SpeedCurve &curve)
{
    drift::TimeUs duration = 0;
    {
        QMutexLocker lock(&m_clipMutex);
        m_clip.speedCurve = curve;
        m_clip.syncDurationFromSpeedCurve();
        duration = m_clip.timelineDuration;
    }

    // The ramp moved under the playhead; keep it inside the clip it now describes.
    if (m_positionUs > duration)
        seek(duration);

    emit durationChanged();
    if (!m_playing)
        requestFrame(m_positionUs);
}

void ClipPreviewPlayer::clear()
{
    pause();
    {
        QMutexLocker lock(&m_clipMutex);
        m_clip = drift::Clip{};
    }
    m_positionUs = 0;
    m_frameSize = QSize();
    ClipPreviewImageStore::clear();
    emit frameChanged();
    emit durationChanged();
    emit positionChanged();
}

drift::TimeUs ClipPreviewPlayer::durationUs() const
{
    QMutexLocker lock(&m_clipMutex);
    return m_clip.timelineDuration;
}

drift::TimeUs ClipPreviewPlayer::positionUs() const
{
    return m_playing ? m_clock.currentTimeUs() : m_positionUs;
}

QSize ClipPreviewPlayer::frameSize() const
{
    return m_frameSize;
}

void ClipPreviewPlayer::play()
{
    if (m_playing || durationUs() <= 0)
        return;

    // Restart from the top rather than sitting stuck on the tail.
    if (m_positionUs >= durationUs())
        m_positionUs = 0;

    ensureAudioSink();
    m_clock.reset(m_positionUs, m_sampleRate);
    m_clock.start();
    m_playing = true;

    // May settle on a rate the clip was not prepared at, which comes back as sampleRateChanged and
    // re-anchors the clock — so this has to follow m_playing being set.
    m_audio.start();

    emit playingChanged();

    m_positionTimer.start(kPositionUpdateMs);
    m_frameTimer.start(qMax(1, 1000 / m_fps));
}

void ClipPreviewPlayer::pause()
{
    if (!m_playing)
        return;

    m_playing = false;
    m_positionTimer.stop();
    m_frameTimer.stop();
    m_clock.pause();
    m_positionUs = m_clock.pausedAt();

    m_audio.stop();

    emit playingChanged();
    emit positionChanged();
}

void ClipPreviewPlayer::seek(drift::TimeUs us)
{
    m_positionUs = qBound<drift::TimeUs>(0, us, durationUs());
    if (m_playing) {
        m_clock.reset(m_positionUs, m_sampleRate);
        m_clock.start();
    }
    emit positionChanged();
    requestFrame(m_positionUs);
}

void ClipPreviewPlayer::onPositionTick()
{
    if (!m_playing)
        return;

    const drift::TimeUs duration = durationUs();
    const drift::TimeUs now = m_clock.currentTimeUs();
    if (now >= duration) {
        m_positionUs = duration;
        pause();
        requestFrame(m_positionUs);
        return;
    }

    m_positionUs = now;
    emit positionChanged();
}

void ClipPreviewPlayer::requestFrame(drift::TimeUs position)
{
    QString path;
    drift::TimeUs sourceUs = 0;
    quint64 streamId = 0;
    int rotationOverride = -1;
    {
        QMutexLocker lock(&m_clipMutex);
        if (m_clip.type == drift::ClipType::Audio || m_clip.path.isEmpty())
            return;
        const drift::VideoRead read = drift::resolveVideoRead(m_clip, position);
        path = read.path;
        sourceUs = read.sourceUs;
        streamId = kPreviewStreamSalt ^ ClipReaderPool::streamIdForClip(m_clip.id);
        rotationOverride = m_clip.rotationOverride;
    }

    const quint64 token = ++m_frameToken;
    m_frameWorker->latestToken.store(token, std::memory_order_release);
    QMetaObject::invokeMethod(m_frameWorker, "decode", Qt::QueuedConnection, Q_ARG(QString, path),
                              Q_ARG(quint64, streamId), Q_ARG(qint64, static_cast<qint64>(sourceUs)),
                              Q_ARG(int, kFrameMaxWidth), Q_ARG(int, kFrameMaxHeight),
                              Q_ARG(quint64, token), Q_ARG(int, rotationOverride));
}

void ClipPreviewPlayer::onDecoded(const QImage &image, quint64 token)
{
    // Decodes can finish out of order across seeks; only ever move forward.
    if (token < m_shownToken)
        return;
    m_shownToken = token;

    ClipPreviewImageStore::setFrame(image);
    if (m_frameSize != image.size()) {
        m_frameSize = image.size();
        emit frameSizeChanged();
    }
    emit frameChanged();
}

int ClipPreviewPlayer::fillAudio(float *buffer, int sampleCount)
{
    if (!buffer || sampleCount <= 0)
        return 0;

    if (!m_playing) {
        std::memset(buffer, 0, static_cast<size_t>(sampleCount) * 2 * sizeof(float));
        return sampleCount;
    }

    drift::Clip clip;
    {
        QMutexLocker lock(&m_clipMutex);
        clip = m_clip;
    }

    const drift::TimeUs timeUs = m_clock.produceTimeUs();
    // Salted off the timeline's id for this clip: the auditioner scrubs its own range while the
    // timeline may be playing the same file, and one decode cursor cannot serve both.
    const quint64 streamId = kPreviewStreamSalt ^ ClipReaderPool::streamIdForClip(clip.id);
    const QVector<float> mixed =
        AudioMixer::readClipAudio(clip, streamId, timeUs, sampleCount, m_sampleRate, &m_retimer);
    const int frames = qMin(sampleCount, static_cast<int>(mixed.size() / 2));
    std::memcpy(buffer, mixed.constData(), static_cast<size_t>(frames) * 2 * sizeof(float));
    if (frames < sampleCount) {
        std::memset(buffer + static_cast<size_t>(frames) * 2, 0,
                    static_cast<size_t>(sampleCount - frames) * 2 * sizeof(float));
    }

    m_clock.onAudioSamplesRendered(sampleCount);
    m_clock.syncPlaybackUs(static_cast<drift::TimeUs>(m_audio.processedUSecs()));
    return sampleCount;
}
