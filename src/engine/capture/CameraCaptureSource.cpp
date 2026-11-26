#include "engine/capture/CameraCaptureSource.h"

#include <QCamera>
#include <QCameraDevice>
#include <QElapsedTimer>
#include <QMediaCaptureSession>
#include <QMediaDevices>
#include <QVideoFrame>
#include <QVideoSink>

namespace drift {

namespace {

QCameraDevice cameraWithId(const QString &id)
{
    if (id.isEmpty())
        return QMediaDevices::defaultVideoInput();
    const QList<QCameraDevice> devices = QMediaDevices::videoInputs();
    for (const QCameraDevice &device : devices) {
        if (QString::fromUtf8(device.id()) == id)
            return device;
    }
    return QMediaDevices::defaultVideoInput();
}

} // namespace

struct CameraCaptureSource::Impl
{
    QMediaCaptureSession session;
    std::unique_ptr<QCamera> camera;
    std::unique_ptr<QVideoSink> sink;
    bool running = false;
    bool announced = false;
    // A camera's own frame times are not comparable with the screen's, so the first frame defines
    // zero here exactly as it does for the portal.
    QElapsedTimer clock;
};

CameraCaptureSource::CameraCaptureSource(QObject *parent)
    : ScreenCaptureSource(parent)
    , d(std::make_unique<Impl>())
{
}

CameraCaptureSource::~CameraCaptureSource()
{
    // The session holds raw pointers; detach before the objects go or the backend reads freed
    // memory on teardown.
    d->session.setCamera(nullptr);
    d->session.setVideoSink(nullptr);
}

bool CameraCaptureSource::available()
{
    return !QMediaDevices::videoInputs().isEmpty();
}

ScreenCaptureSource::Capabilities CameraCaptureSource::capabilities() const
{
    Capabilities caps;
    // A camera is neither a screen nor a window, and there is no picker to defer to: the caller
    // chooses the device from CaptureDevices::cameras().
    caps.screens = false;
    caps.windows = false;
    caps.platformChoosesSource = false;
    caps.cursorOptional = false;
    return caps;
}

bool CameraCaptureSource::active() const
{
    return d->running;
}

void CameraCaptureSource::start(const CaptureRequest &request)
{
    if (d->running) {
        emit failed(tr("A camera recording is already running."));
        return;
    }

    const QCameraDevice device = cameraWithId(request.cameraId);
    if (device.isNull()) {
        emit failed(tr("No camera is connected."));
        return;
    }

    d->camera = std::make_unique<QCamera>(device);
    d->sink = std::make_unique<QVideoSink>();
    d->session.setCamera(d->camera.get());
    d->session.setVideoSink(d->sink.get());

    connect(d->camera.get(), &QCamera::errorOccurred, this,
            [this](QCamera::Error, const QString &message) {
                emit failed(message.isEmpty() ? tr("The camera stopped working.") : message);
            });

    connect(d->sink.get(), &QVideoSink::videoFrameChanged, this, [this](const QVideoFrame &frame) {
        if (!d->running || !frame.isValid())
            return;
        QImage image = frame.toImage();
        if (image.isNull())
            return;

        // The first frame is what tells the caller the real size: a camera's configured resolution
        // is a request, and the device is free to answer with another.
        if (!d->announced) {
            d->announced = true;
            d->clock.start();
            emit started(image.size());
        }
        emit this->frame(image, TimeUs(d->clock.nsecsElapsed() / 1000));
    });

    d->running = true;
    d->announced = false;
    d->camera->start();
}

void CameraCaptureSource::stop()
{
    if (!d->running)
        return;
    d->running = false;
    if (d->camera)
        d->camera->stop();
    d->session.setCamera(nullptr);
    d->session.setVideoSink(nullptr);
    d->camera.reset();
    d->sink.reset();
    emit stopped();
}

} // namespace drift
