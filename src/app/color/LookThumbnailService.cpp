#include "app/color/LookThumbnailService.h"

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

// Only the base frame is rendered here; ColorImageProvider applies each look to it when a card
// asks for its image, so a pack of hundreds of looks costs nothing until they scroll into view.
void renderLookBase(std::shared_ptr<const drift::Project> project, qint64 timeUs)
{
    FrameCompositor::RenderOptions options;
    options.previewScale = std::min(1.0, double(kThumbnailWidth) / qMax(1, project->width()));
    FrameCompositor compositor;
    compositor.setProject(project.get());
    const QImage base = compositor.compositeAt(timeUs, options).convertToFormat(QImage::Format_ARGB32);
    if (base.isNull())
        return;
    publishColorImage(QStringLiteral("look/none"), base);
}

} // namespace

LookThumbnailService::LookThumbnailService(std::function<drift::Project()> projectSnapshot, QObject *parent)
    : QObject(parent)
    , m_projectSnapshot(std::move(projectSnapshot))
    , m_debounce(new QTimer(this))
{
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(kDebounceMs);
    connect(m_debounce, &QTimer::timeout, this, &LookThumbnailService::fire);
}

LookThumbnailService::~LookThumbnailService() = default;

void LookThumbnailService::request(qint64 playheadUs, const QString &sourceClipId,
                                   const QString &gradeHostClipId, int gradeEffectIndex)
{
    if (!m_active)
        return;
    m_playheadUs = playheadUs;
    m_sourceClipId = sourceClipId;
    m_gradeHostClipId = gradeHostClipId;
    m_gradeEffectIndex = gradeEffectIndex;
    m_debounce->start();
}

void LookThumbnailService::setActive(bool active)
{
    m_active = active;
    if (!active)
        m_debounce->stop();
}

void LookThumbnailService::fire()
{
    if (!m_active)
        return;
    if (m_rendering) {
        m_queued = true;
        return;
    }
    startRender();
}

void LookThumbnailService::startRender()
{
    m_rendering = true;
    const int serial = ++m_serial;

    drift::Project project = m_projectSnapshot();
    qint64 timeUs = m_playheadUs;
    for (drift::Track &track : project.tracks()) {
        for (drift::Clip &clip : track.clips) {
            if (clip.id == m_sourceClipId) {
                if (timeUs < clip.timelineStart || timeUs >= clip.timelineEnd())
                    timeUs = clip.timelineStart + clip.timelineDuration / 2;
            }
            if (clip.id == m_gradeHostClipId && m_gradeEffectIndex >= 0
                && m_gradeEffectIndex < clip.effects.size())
                clip.effects[m_gradeEffectIndex].parameters.insert(QStringLiteral("look"), QString());
        }
    }
    if (!m_sourceClipId.isEmpty()) {
        for (drift::Track &track : project.tracks()) {
            track.clips.erase(std::remove_if(track.clips.begin(), track.clips.end(),
                                             [&](const drift::Clip &clip) {
                                                 return clip.id != m_sourceClipId
                                                     && clip.id != m_gradeHostClipId;
                                             }),
                              track.clips.end());
            track.transitions.clear();
        }
    }
    const auto snapshot = std::make_shared<const drift::Project>(std::move(project));

    auto *watcher = new QFutureWatcher<void>(this);
    connect(watcher, &QFutureWatcher<void>::finished, this, [this, watcher, serial]() {
        watcher->deleteLater();
        onRenderFinished(serial);
    });
    watcher->setFuture(QtConcurrent::run(renderLookBase, snapshot, timeUs));
}

void LookThumbnailService::onRenderFinished(int serial)
{
    m_rendering = false;
    emit thumbnailsReady(serial);
    if (m_queued) {
        m_queued = false;
        if (m_active)
            startRender();
    }
}

#include "moc_LookThumbnailService.cpp"
