#include "engine/capture/ScreenRecorder.h"

#include <QAudioDevice>
#include <QAudioInput>
#include <QCamera>
#include <QCameraDevice>
#include <QCapturableWindow>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QMediaCaptureSession>
#include <QMediaDevices>
#include <QMediaFormat>
#include <QMediaRecorder>
#include <QScreen>
#include <QScreenCapture>
#include <QUrl>
#include <QWindowCapture>

namespace drift {

namespace {

// PulseAudio and PipeWire publish a device that hears the machine's own output, named for the sink
// it mirrors. Qt passes the name through, so this is how "record the computer's sound" finds its
// device without a platform #ifdef. Windows and macOS publish nothing of the sort, which is why
// the caller treats an empty result as "system audio needs a loopback device installed".
bool looksLikeLoopback(const QAudioDevice &device)
{
    const QString description = device.description().toLower();
    const QString id = QString::fromUtf8(device.id()).toLower();
    for (const QLatin1String token : {QLatin1String("monitor"), QLatin1String("loopback")}) {
        if (description.contains(token) || id.contains(token))
            return true;
    }
    return false;
}

QScreen *screenNamed(const QString &name)
{
    if (name.isEmpty())
        return QGuiApplication::primaryScreen();
    const QList<QScreen *> screens = QGuiApplication::screens();
    for (QScreen *screen : screens) {
        if (screen->name() == name)
            return screen;
    }
    return QGuiApplication::primaryScreen();
}

QCameraDevice cameraWithId(const QString &id)
{
    const QList<QCameraDevice> devices = QMediaDevices::videoInputs();
    if (id.isEmpty())
        return QMediaDevices::defaultVideoInput();
    for (const QCameraDevice &device : devices) {
        if (QString::fromUtf8(device.id()) == id)
            return device;
    }
    return QMediaDevices::defaultVideoInput();
}

QAudioDevice audioWithId(const QString &id)
{
    if (id.isEmpty())
        return {};
    const QList<QAudioDevice> devices = QMediaDevices::audioInputs();
    for (const QAudioDevice &device : devices) {
        if (QString::fromUtf8(device.id()) == id)
            return device;
    }
    return {};
}

} // namespace

struct ScreenRecorder::Impl
{
    QMediaCaptureSession session;
    std::unique_ptr<QMediaRecorder> recorder;
    std::unique_ptr<QScreenCapture> screenCapture;
    std::unique_ptr<QWindowCapture> windowCapture;
    std::unique_ptr<QCamera> camera;
    std::unique_ptr<QAudioInput> audioInput;

    QString requestedPath;
    double seconds = 0.0;
    bool paused = false;
    // Set by cancel() so the file is removed as soon as the recorder reports it is closed —
    // deleting it before then races the muxer still writing its trailer.
    bool discardOnStop = false;

    // Tears every source down. The session keeps raw pointers, so they are detached before the
    // objects go: destroying a source the session still references crashes the backend.
    void releaseSources()
    {
        session.setScreenCapture(nullptr);
        session.setWindowCapture(nullptr);
        session.setCamera(nullptr);
        session.setAudioInput(nullptr);
        screenCapture.reset();
        windowCapture.reset();
        camera.reset();
        audioInput.reset();
    }
};

ScreenRecorder::ScreenRecorder(QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Impl>())
{
}

ScreenRecorder::~ScreenRecorder()
{
    // Stop first: dropping the recorder mid-write leaves a file with no moov atom, which no
    // decoder will open.
    if (d->recorder && d->recorder->recorderState() != QMediaRecorder::StoppedState)
        d->recorder->stop();
    d->releaseSources();
}

bool ScreenRecorder::screenCaptureAvailable()
{
    // Constructing one is the only honest test: whether a backend exists is a property of the
    // session (Wayland without the portal, a container with no compositor), not of the build.
    QScreenCapture probe;
    return probe.error() != QScreenCapture::CapturingNotSupported;
}

bool ScreenRecorder::cameraAvailable()
{
    return !QMediaDevices::videoInputs().isEmpty();
}

QVariantList ScreenRecorder::screens()
{
    QVariantList out;
    const QScreen *primary = QGuiApplication::primaryScreen();
    const QList<QScreen *> screens = QGuiApplication::screens();
    for (const QScreen *screen : screens) {
        const QSize size = screen->size();
        out.append(QVariantMap{{QStringLiteral("name"), screen->name()},
                               {QStringLiteral("description"), screen->manufacturer().isEmpty()
                                                                   ? screen->name()
                                                                   : screen->manufacturer()},
                               {QStringLiteral("primary"), screen == primary},
                               {QStringLiteral("width"), size.width()},
                               {QStringLiteral("height"), size.height()}});
    }
    return out;
}

QVariantList ScreenRecorder::windows()
{
    QVariantList out;
    const QList<QCapturableWindow> windows = QWindowCapture::capturableWindows();
    for (int i = 0; i < windows.size(); ++i) {
        const QCapturableWindow &window = windows.at(i);
        if (!window.isValid() || window.description().isEmpty())
            continue;
        // The index is into the unfiltered list, so it still addresses this window after the
        // nameless entries have been dropped from what the user sees.
        out.append(QVariantMap{{QStringLiteral("index"), i},
                               {QStringLiteral("title"), window.description()}});
    }
    return out;
}

QVariantList ScreenRecorder::cameras()
{
    QVariantList out;
    const QCameraDevice fallback = QMediaDevices::defaultVideoInput();
    const QList<QCameraDevice> devices = QMediaDevices::videoInputs();
    for (const QCameraDevice &device : devices) {
        out.append(QVariantMap{{QStringLiteral("id"), QString::fromUtf8(device.id())},
                               {QStringLiteral("description"), device.description()},
                               {QStringLiteral("isDefault"), device == fallback}});
    }
    return out;
}

QVariantList ScreenRecorder::audioInputs()
{
    QVariantList out;
    const QAudioDevice fallback = QMediaDevices::defaultAudioInput();
    const QList<QAudioDevice> devices = QMediaDevices::audioInputs();
    for (const QAudioDevice &device : devices) {
        out.append(QVariantMap{{QStringLiteral("id"), QString::fromUtf8(device.id())},
                               {QStringLiteral("description"), device.description()},
                               {QStringLiteral("isDefault"), device == fallback},
                               {QStringLiteral("loopback"), looksLikeLoopback(device)}});
    }
    return out;
}

bool ScreenRecorder::recording() const
{
    return d->recorder && d->recorder->recorderState() != QMediaRecorder::StoppedState;
}

bool ScreenRecorder::paused() const
{
    return d->paused;
}

double ScreenRecorder::recordedSeconds() const
{
    return d->seconds;
}

QString ScreenRecorder::outputPath() const
{
    return d->requestedPath;
}

bool ScreenRecorder::start(const CaptureRequest &request, QString *errorOut)
{
    auto fail = [errorOut](const QString &message) {
        if (errorOut)
            *errorOut = message;
        return false;
    };

    if (recording())
        return fail(tr("A recording is already running."));

    const QString problem = captureRequestProblem(request);
    if (!problem.isEmpty())
        return fail(problem);

    d->releaseSources();
    d->paused = false;
    d->seconds = 0.0;
    d->discardOnStop = false;

    switch (request.source) {
    case CaptureSource::Screen:
    case CaptureSource::Region: {
        // Region records the whole screen: no platform offers a rectangle grab, so the crop is
        // applied to the clip afterwards, where it stays adjustable instead of being burnt in.
        if (!screenCaptureAvailable())
            return fail(tr("This system does not allow screen recording."));
        d->screenCapture = std::make_unique<QScreenCapture>();
        d->screenCapture->setScreen(screenNamed(request.screenName));
        d->session.setScreenCapture(d->screenCapture.get());
        break;
    }
    case CaptureSource::Window: {
        // Re-read, because the list the user picked from is already stale: windows come and go.
        // The index is tried first and accepted only when the title still agrees; otherwise the
        // title is searched for, since a window that merely moved in the list is still the one
        // that was asked for, while the same index pointing at a new title is not.
        const QList<QCapturableWindow> windows = QWindowCapture::capturableWindows();
        QCapturableWindow target;
        if (request.windowIndex >= 0 && request.windowIndex < windows.size()) {
            const QCapturableWindow &candidate = windows.at(request.windowIndex);
            if (request.windowTitle.isEmpty() || candidate.description() == request.windowTitle)
                target = candidate;
        }
        if (!target.isValid() && !request.windowTitle.isEmpty()) {
            for (const QCapturableWindow &window : windows) {
                if (window.description() == request.windowTitle) {
                    target = window;
                    break;
                }
            }
        }
        if (!target.isValid())
            return fail(tr("That window has closed."));
        d->windowCapture = std::make_unique<QWindowCapture>();
        d->windowCapture->setWindow(target);
        d->session.setWindowCapture(d->windowCapture.get());
        break;
    }
    case CaptureSource::Camera: {
        const QCameraDevice device = cameraWithId(request.cameraId);
        if (device.isNull())
            return fail(tr("No camera is connected."));
        d->camera = std::make_unique<QCamera>(device);
        d->session.setCamera(d->camera.get());
        break;
    }
    }

    const QAudioDevice audio = audioWithId(request.audioInputId);
    if (!audio.isNull()) {
        d->audioInput = std::make_unique<QAudioInput>(audio);
        d->session.setAudioInput(d->audioInput.get());
    }

    d->recorder = std::make_unique<QMediaRecorder>();
    d->recorder->setVideoFrameRate(clampCaptureFps(request.fps));
    d->recorder->setQuality(QMediaRecorder::HighQuality);
    d->recorder->setOutputLocation(QUrl::fromLocalFile(request.outputPath));
    d->session.setRecorder(d->recorder.get());
    d->requestedPath = request.outputPath;
    emit outputPathChanged();

    connect(d->recorder.get(), &QMediaRecorder::durationChanged, this, [this](qint64 ms) {
        d->seconds = double(ms) / 1000.0;
        emit recordedSecondsChanged();
    });
    connect(d->recorder.get(), &QMediaRecorder::errorOccurred, this,
            [this](QMediaRecorder::Error, const QString &message) {
                emit error(message.isEmpty() ? tr("The recording failed.") : message);
            });
    connect(d->recorder.get(), &QMediaRecorder::recorderStateChanged, this,
            [this](QMediaRecorder::RecorderState state) {
                d->paused = state == QMediaRecorder::PausedState;
                emit pausedChanged();
                emit recordingChanged();
                if (state != QMediaRecorder::StoppedState)
                    return;

                // Qt reports where it really wrote, which is not always what was asked for: a
                // backend that cannot mux the requested container swaps the suffix.
                const QUrl actual = d->recorder->actualLocation();
                const QString path =
                    actual.isLocalFile() ? actual.toLocalFile() : d->requestedPath;
                d->releaseSources();
                if (d->discardOnStop) {
                    d->discardOnStop = false;
                    if (!path.isEmpty())
                        QFile::remove(path);
                    return;
                }
                emit finished(path);
            });

    // Sources only produce frames once they are switched on, and the recorder only accepts them
    // once it is recording, so the order here matters: arm the source, then start the muxer.
    if (d->screenCapture)
        d->screenCapture->setActive(true);
    if (d->windowCapture)
        d->windowCapture->setActive(true);
    if (d->camera)
        d->camera->start();

    d->recorder->record();
    emit recordingChanged();
    return true;
}

void ScreenRecorder::pause()
{
    if (recording() && !d->paused)
        d->recorder->pause();
}

void ScreenRecorder::resume()
{
    if (recording() && d->paused)
        d->recorder->record();
}

void ScreenRecorder::stop()
{
    if (recording())
        d->recorder->stop();
}

void ScreenRecorder::cancel()
{
    if (!recording())
        return;
    d->discardOnStop = true;
    d->recorder->stop();
}

} // namespace drift
