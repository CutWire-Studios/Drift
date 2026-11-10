#include "app/editor/BlendPreviewController.h"

#include "app/editor/AppController.h"
#include "core/effects/BlendMode.h"
#include "core/project/Project.h"
#include "engine/compositor/FrameCompositor.h"
#include "ui/imageproviders/color/ColorImageProvider.h"

#include <QFutureWatcher>
#include <QTimer>
#include <QtConcurrent>

#include <algorithm>

namespace {

constexpr int kThumbnailWidth = 160;
constexpr int kDebounceMs = 300;

void renderBlendModes(std::shared_ptr<drift::Project> project, int trackIndex, int clipIndex, qint64 timeUs)
{
    FrameCompositor::RenderOptions options;
    options.previewScale = std::min(1.0, double(kThumbnailWidth) / qMax(1, project->width()));
    options.cacheVideoSources = true;
    FrameCompositor compositor;
    compositor.setProject(project.get());
    drift::Clip &clip = project->tracks()[trackIndex].clips[clipIndex];
    for (int i = int(drift::BlendMode::Normal); i <= int(drift::BlendMode::Luminosity); ++i) {
        const auto mode = static_cast<drift::BlendMode>(i);
        clip.blendMode = mode;
        compositor.clearNestedViewCache();
        const QImage image = compositor.compositeAt(timeUs, options).convertToFormat(QImage::Format_ARGB32);
        if (!image.isNull())
            publishColorImage(QStringLiteral("blend/") + drift::blendModeToString(mode), image);
    }
}

} // namespace

BlendPreviewController::BlendPreviewController(AppController *app, QObject *parent)
    : QObject(parent)
    , m_app(app)
    , m_debounce(new QTimer(this))
{
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(kDebounceMs);
    connect(m_debounce, &QTimer::timeout, this, &BlendPreviewController::fire);
    connect(app, &AppController::selectionChanged, this, &BlendPreviewController::request);
    connect(app, &AppController::selectedClipDataChanged, this, &BlendPreviewController::request);
    connect(app, &AppController::tracksChanged, this, &BlendPreviewController::request);
    connect(app, &AppController::undoStackChanged, this, &BlendPreviewController::request);
    connect(app, &AppController::playheadSecondsChanged, this, [this]() {
        if (m_active && !m_app->playing())
            request();
    });
}

void BlendPreviewController::setActive(bool active)
{
    if (m_active == active)
        return;
    m_active = active;
    if (!active)
        m_debounce->stop();
    emit activeChanged();
    request();
}

void BlendPreviewController::request()
{
    if (m_active)
        m_debounce->start();
}

void BlendPreviewController::fire()
{
    if (!m_active)
        return;
    if (m_rendering) {
        m_queued = true;
        return;
    }
    startRender();
}

void BlendPreviewController::startRender()
{
    const int t = m_app->selectedTrack();
    const int c = m_app->selectedClip();
    const auto &tracks = m_app->m_project.tracks();
    if (t < 0 || t >= tracks.size() || c < 0 || c >= tracks.at(t).clips.size())
        return;
    const drift::Clip &target = tracks.at(t).clips.at(c);
    switch (target.type) {
    case drift::ClipType::Audio:
    case drift::ClipType::Subtitle:
    case drift::ClipType::Adjustment:
        return;
    default:
        break;
    }

    qint64 timeUs = qint64(m_app->playheadSeconds() * 1000000.0);
    if (timeUs < target.timelineStart || timeUs >= target.timelineEnd())
        timeUs = target.timelineStart + target.timelineDuration / 2;

    auto snapshot = std::make_shared<drift::Project>(m_app->m_project.detachedCopy());
    for (int i = 0; i < t; ++i) {
        snapshot->tracks()[i].clips.clear();
        snapshot->tracks()[i].transitions.clear();
    }

    m_rendering = true;
    auto *watcher = new QFutureWatcher<void>(this);
    connect(watcher, &QFutureWatcher<void>::finished, this, [this, watcher]() {
        watcher->deleteLater();
        m_rendering = false;
        ++m_serial;
        emit serialChanged();
        if (m_queued) {
            m_queued = false;
            if (m_active)
                startRender();
        }
    });
    watcher->setFuture(QtConcurrent::run(renderBlendModes, snapshot, t, c, timeUs));
}

#include "moc_BlendPreviewController.cpp"
