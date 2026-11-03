#include "app/bin/AssetPreviewController.h"

#include "app/editor/AppController.h"
#include "app/bin/AssetLibrary.h"
#include "core/project/clip/Clip.h"
#include "core/project/media/MediaAsset.h"
#include "core/base/Time.h"
#include "playback/preview/FrameGrid.h"

#include <algorithm>

AssetPreviewController::AssetPreviewController(AppController *app, QObject *parent)
    : QObject(parent)
    , m_app(app)
{
    connect(&m_player, &ClipPreviewPlayer::frameChanged, this, [this] {
        ++m_revision;
        emit frameChanged();
    });
    connect(&m_player, &ClipPreviewPlayer::frameSizeChanged, this,
            &AssetPreviewController::frameChanged);
    connect(&m_player, &ClipPreviewPlayer::positionChanged, this,
            &AssetPreviewController::positionChanged);
    connect(&m_player, &ClipPreviewPlayer::playingChanged, this, [this] {
        // Fast-forward ran off the end; a reverse shuttle is paused by design.
        if (!m_player.isPlaying() && m_shuttleRate > 0)
            clearShuttle();
        emit playingChanged();
    });
    m_reverseTimer.setTimerType(Qt::PreciseTimer);
    m_reverseTimer.setInterval(33);
    connect(&m_reverseTimer, &QTimer::timeout, this, &AssetPreviewController::onReverseTick);
    connect(&m_player, &ClipPreviewPlayer::durationChanged, this,
            &AssetPreviewController::sessionChanged);
}

void AssetPreviewController::setAudioDeviceId(const QByteArray &id)
{
    m_player.setAudioDeviceId(id);
}

void AssetPreviewController::setWindowOpen(bool open)
{
    if (m_windowOpen == open)
        return;
    m_windowOpen = open;
    emit windowOpenChanged();
}

void AssetPreviewController::begin(int index)
{
    if (!m_app->m_assetLibrary)
        return;

    const QString assetId = m_app->m_assetLibrary->assetIdAt(index);
    const drift::MediaAsset *asset = assetId.isEmpty() ? nullptr : m_app->m_project.asset(assetId);
    if (!asset || asset->path.isEmpty())
        return;

    // Images have nothing to play; the page shows the file itself through the image provider.
    if (asset->kind != drift::MediaKind::Video && asset->kind != drift::MediaKind::Audio) {
        m_index = index;
        m_active = true;
        emit sessionChanged();
        return;
    }

    // Same rule the speed-curve session follows: ClipReaderPool's workers are shared, so the
    // timeline must not be walking them while this player does.
    m_app->setPlaying(false);

    drift::Clip clip;
    clip.type = asset->kind == drift::MediaKind::Audio ? drift::ClipType::Audio
                                                       : drift::ClipType::Video;
    clip.assetId = asset->id;
    clip.name = asset->name;
    clip.path = asset->path;
    clip.thumbnailPath = asset->thumbnailPath;
    clip.filmstripPath = asset->filmstripPath;
    clip.srcIn = 0;
    clip.srcOut = asset->durationUs;
    clip.timelineStart = 0;
    clip.timelineDuration = asset->durationUs;
    clip.rotationCorrection = drift::rotationCorrectionOf(*asset);

    m_index = index;
    m_active = true;
    m_fps = drift::frameGridFps(asset->fps);
    clearShuttle();
    m_player.setClip(clip, m_app->m_project.sampleRate(), m_app->m_project.fps());

    emit sessionChanged();
}

void AssetPreviewController::beginClip(int track, int index)
{
    const auto &tracks = m_app->m_project.tracks();
    if (track < 0 || track >= tracks.size() || index < 0 || index >= tracks.at(track).clips.size())
        return;
    const drift::Clip &placed = tracks.at(track).clips.at(index);
    if (placed.path.isEmpty())
        return;

    m_index = -1;
    m_active = true;
    if (placed.type != drift::ClipType::Video) {
        m_player.clear();
        emit sessionChanged();
        return;
    }

    m_app->setPlaying(false);

    // The whole source, so positions are source seconds like the window's in/out points.
    const drift::MediaAsset *asset = m_app->m_project.asset(placed.assetId);
    drift::Clip clip;
    clip.type = drift::ClipType::Video;
    clip.assetId = placed.assetId;
    clip.name = placed.name;
    clip.path = placed.path;
    clip.thumbnailPath = placed.thumbnailPath;
    clip.filmstripPath = placed.filmstripPath;
    clip.srcIn = 0;
    clip.srcOut = asset && asset->durationUs > 0 ? asset->durationUs : placed.srcOut;
    clip.timelineStart = 0;
    clip.timelineDuration = clip.srcOut;
    clip.rotationCorrection = placed.rotationCorrection;
    m_fps = drift::frameGridFps(asset ? asset->fps : 0.0);
    clearShuttle();
    m_player.setClip(clip, m_app->m_project.sampleRate(), m_app->m_project.fps());

    emit sessionChanged();
}

void AssetPreviewController::end()
{
    if (!m_active)
        return;

    clearShuttle();
    m_player.setPreviewRate(1.0);
    m_player.setGainDb(0.0);
    m_player.clear();
    m_active = false;
    m_index = -1;
    emit sessionChanged();
}

double AssetPreviewController::duration() const
{
    return drift::usToSeconds(m_player.durationUs());
}

double AssetPreviewController::position() const
{
    return drift::usToSeconds(m_player.positionUs());
}

void AssetPreviewController::play()
{
    if (!m_active)
        return;
    m_app->setPlaying(false);
    clearShuttle();
    m_player.play();
}

void AssetPreviewController::pause()
{
    clearShuttle();
    m_player.pause();
}

void AssetPreviewController::seek(double seconds)
{
    if (!m_active)
        return;
    m_player.seek(drift::secondsToUs(std::max(0.0, seconds)));
}

int AssetPreviewController::positionFrame() const
{
    return drift::frameIndexAt(m_player.positionUs(), m_fps);
}

double AssetPreviewController::snapToFrame(double seconds) const
{
    return drift::usToSeconds(
        drift::snapToFrameUs(drift::secondsToUs(std::max(0.0, seconds)), m_fps));
}

void AssetPreviewController::seekFrame(int frame)
{
    if (!m_active)
        return;
    pause();
    const int last = drift::lastFrameIndex(m_player.durationUs(), m_fps);
    m_player.seek(drift::frameTimeUs(std::clamp(frame, 0, last), m_fps));
}

void AssetPreviewController::stepFrames(int delta)
{
    if (!m_active)
        return;
    pause();
    seekFrame(positionFrame() + delta);
}

void AssetPreviewController::setShuttleRate(int rate)
{
    if (m_shuttleRate == rate)
        return;
    m_shuttleRate = rate;
    emit shuttleRateChanged();
}

void AssetPreviewController::clearShuttle()
{
    m_reverseTimer.stop();
    m_player.setShuttleRate(1.0);
    setShuttleRate(0);
}

void AssetPreviewController::shuttle(int rate)
{
    if (!m_active || !drift::isShuttleRate(rate))
        return;
    if (rate == 0) {
        pause();
        return;
    }

    m_app->setPlaying(false);
    if (rate > 0) {
        m_reverseTimer.stop();
        m_player.setShuttleRate(rate);
        m_player.play();
        if (!m_player.isPlaying()) {
            clearShuttle();
            return;
        }
        setShuttleRate(rate);
        return;
    }

    m_player.pause();
    m_player.setShuttleRate(1.0);
    setShuttleRate(rate);
    m_reverseOriginUs = m_player.positionUs();
    m_reverseElapsed.start();
    m_reverseTimer.start();
}

void AssetPreviewController::onReverseTick()
{
    // Position from elapsed real time rather than a step per tick, so a slow backward decode
    // drops frames instead of slowing the shuttle down.
    const double travelled = static_cast<double>(m_reverseElapsed.nsecsElapsed()) / 1000.0
                             * -m_shuttleRate;
    const drift::TimeUs target = m_reverseOriginUs - static_cast<drift::TimeUs>(travelled);
    if (target <= 0) {
        m_player.seek(0);
        clearShuttle();
        return;
    }
    const int last = drift::lastFrameIndex(m_player.durationUs(), m_fps);
    m_player.seek(drift::frameTimeUs(std::clamp(drift::frameIndexAt(target, m_fps), 0, last), m_fps));
}

void AssetPreviewController::shuttleForward()
{
    // Space-bar playback counts as 1x so L from there goes to 2x.
    const int current = m_shuttleRate == 0 && m_player.isPlaying() ? 1 : m_shuttleRate;
    shuttle(drift::shuttleNext(current, true));
}

void AssetPreviewController::shuttleReverse()
{
    const int current = m_shuttleRate == 0 && m_player.isPlaying() ? 1 : m_shuttleRate;
    shuttle(drift::shuttleNext(current, false));
}

void AssetPreviewController::shuttleStop()
{
    shuttle(0);
}

void AssetPreviewController::setPreviewRate(double speed)
{
    m_player.setPreviewRate(speed);
}

void AssetPreviewController::setPreviewGain(double db)
{
    m_player.setGainDb(db);
}

#include "moc_AssetPreviewController.cpp"
