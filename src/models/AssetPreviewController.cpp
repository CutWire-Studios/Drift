#include "AssetPreviewController.h"

#include "AppController.h"
#include "AssetLibrary.h"
#include "core/Clip.h"
#include "core/MediaAsset.h"
#include "core/Time.h"

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
    connect(&m_player, &ClipPreviewPlayer::playingChanged, this,
            &AssetPreviewController::playingChanged);
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
    m_player.setClip(clip, m_app->m_project.sampleRate(), m_app->m_project.fps());

    emit sessionChanged();
}

void AssetPreviewController::end()
{
    if (!m_active)
        return;

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
    m_player.play();
}

void AssetPreviewController::pause()
{
    m_player.pause();
}

void AssetPreviewController::seek(double seconds)
{
    if (!m_active)
        return;
    m_player.seek(drift::secondsToUs(std::max(0.0, seconds)));
}
