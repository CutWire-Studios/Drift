#include "app/color/ScopesController.h"

#include "app/editor/AppController.h"
#include "engine/color/ScopeAnalysis.h"
#include "playback/PlaybackEngine.h"
#include "ui/imageproviders/color/ColorImageProvider.h"

#include <QFutureWatcher>
#include <QtConcurrent>

ScopesController::ScopesController(AppController *app, QObject *parent)
    : QObject(parent)
    , m_app(app)
{
}

void ScopesController::setParade(bool on)
{
    if (m_parade == on)
        return;
    m_parade = on;
    emit paradeChanged();
}

void ScopesController::acquire()
{
    if (m_count++ > 0)
        return;
    PlaybackEngine *playback = m_app->playback();
    m_frameConnection = connect(playback, &PlaybackEngine::scopeFrame, this, &ScopesController::onScopeFrame);
    playback->acquireScopes();
    emit activeChanged();
}

void ScopesController::release()
{
    if (m_count == 0)
        return;
    if (--m_count > 0)
        return;
    disconnect(m_frameConnection);
    m_pending = QImage();
    m_app->playback()->releaseScopes();
    emit activeChanged();
}

void ScopesController::onScopeFrame(const QImage &frame)
{
    if (m_jobRunning) {
        m_pending = frame;
        return;
    }
    startJob(frame);
}

void ScopesController::startJob(const QImage &frame)
{
    m_jobRunning = true;
    auto *watcher = new QFutureWatcher<void>(this);
    connect(watcher, &QFutureWatcher<void>::finished, this, [this, watcher]() {
        watcher->deleteLater();
        onJobFinished();
    });
    const bool parade = m_parade;
    watcher->setFuture(QtConcurrent::run([frame, parade]() {
        publishColorImage(QStringLiteral("scope/histogram"),
                          drift::renderHistogram(drift::computeHistogram(frame)));
        publishColorImage(QStringLiteral("scope/waveform"), drift::renderWaveform(frame, parade));
        publishColorImage(QStringLiteral("scope/vectorscope"), drift::renderVectorscope(frame));
    }));
}

void ScopesController::onJobFinished()
{
    m_jobRunning = false;
    ++m_serial;
    emit serialChanged();
    if (m_pending.isNull())
        return;
    QImage next = std::move(m_pending);
    m_pending = QImage();
    startJob(next);
}

#include "moc_ScopesController.cpp"
