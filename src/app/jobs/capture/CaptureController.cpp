#include "app/jobs/capture/CaptureController.h"

#include "app/bin/AssetLibrary.h"
#include "app/editor/AppController.h"
#include "app/editor/ProjectFileController.h"
#include "engine/capture/CaptureDevices.h"
#include "engine/capture/CaptureWriter.h"
#include "engine/capture/RecordingLocation.h"
#include "engine/capture/ScreenCaptureSource.h"

#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSource>
#include <QDateTime>
#include <QFile>
#include <QMediaDevices>
#include <QSettings>
#include <QUrl>

#include <cstring>
#include <vector>

namespace {

// 48 kHz stereo: what every capture device offers, and what the AAC encoder wants anyway.
constexpr int kAudioRate = 48000;
constexpr int kAudioChannels = 2;

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

struct CaptureController::Impl
{
    AppController *app = nullptr;
    CaptureController *owner = nullptr;

    std::unique_ptr<drift::ScreenCaptureSource> source;
    std::unique_ptr<drift::CaptureWriter> writer;
    std::unique_ptr<QAudioSource> audio;
    QIODevice *audioDevice = nullptr;
    QAudioFormat audioFormat;

    QString path;
    QString statusText;
    double seconds = 0.0;
    bool recording = false;
    bool writerOpen = false;
    bool discard = false;
    int fps = 30;

    // Scratch for the pull from the audio device, reused so a 10 ms callback does not allocate.
    std::vector<char> audioScratch;
    std::vector<float> audioFloats;

    void setStatus(const QString &text)
    {
        if (statusText == text)
            return;
        statusText = text;
        emit owner->statusChanged();
    }

    // Where this recording should be written, honouring the Settings choice and reporting when it
    // could not be met.
    QString resolveOutputPath(const QString &kind);

    bool startAudio(const QString &audioInputId);
    void pullAudio();
    void teardown();
    void finishRecording();
};

QString CaptureController::Impl::resolveOutputPath(const QString &kind)
{
    QSettings settings;
    const QString modeText =
        settings.value(QStringLiteral("capture/location")).toString().trimmed();
    const drift::RecordingLocation mode =
        drift::recordingLocationFromString(modeText).value_or(drift::RecordingLocation::Movies);
    const QString folder = settings.value(QStringLiteral("capture/folder")).toString();

    const QString fileName = drift::recordingFileName(kind, QDateTime::currentDateTime(),
                                                      QStringLiteral("mp4"));
    const drift::RecordingTarget target =
        drift::chooseRecordingTarget(mode, folder,
                                     app->projectFile() ? app->projectFile()->currentProjectPath()
                                                        : QString(),
                                     fileName);

    // Said out loud, because the recording is about to go somewhere other than where the user
    // asked, and finding it later is their problem otherwise.
    if (target.folderUnavailable) {
        app->setLastMessage(
            tr("The folder for recordings isn't available, so this one goes to your Movies "
               "folder."),
            QStringLiteral("warning"));
    } else if (target.projectUnavailable) {
        app->setLastMessage(
            tr("Save the project to keep recordings beside it. This one goes to your Movies "
               "folder."),
            QStringLiteral("warning"));
    }
    return target.path;
}

bool CaptureController::Impl::startAudio(const QString &audioInputId)
{
    const QAudioDevice device = audioWithId(audioInputId);
    if (device.isNull())
        return false;

    QAudioFormat format;
    format.setSampleRate(kAudioRate);
    format.setChannelCount(kAudioChannels);
    format.setSampleFormat(QAudioFormat::Float);
    if (!device.isFormatSupported(format)) {
        // Int16 is the format every device supports when float is refused; it is converted on the
        // way through rather than asking the encoder to deal with two layouts.
        format.setSampleFormat(QAudioFormat::Int16);
        if (!device.isFormatSupported(format))
            format = device.preferredFormat();
    }
    if (format.channelCount() < 1 || format.sampleRate() < 8000)
        return false;

    audioFormat = format;
    audio = std::make_unique<QAudioSource>(device, format);
    audioDevice = audio->start();
    if (!audioDevice) {
        audio.reset();
        return false;
    }
    QObject::connect(audioDevice, &QIODevice::readyRead, owner, [this] { pullAudio(); });
    return true;
}

void CaptureController::Impl::pullAudio()
{
    if (!audioDevice || !writerOpen || !writer)
        return;

    const qint64 available = audioDevice->bytesAvailable();
    if (available <= 0)
        return;
    audioScratch.resize(size_t(available));
    const qint64 read = audioDevice->read(audioScratch.data(), available);
    if (read <= 0)
        return;

    const int channels = audioFormat.channelCount();
    const int bytesPerSample = audioFormat.bytesPerSample();
    if (channels <= 0 || bytesPerSample <= 0)
        return;
    const int frames = int(read / (bytesPerSample * channels));
    if (frames <= 0)
        return;

    audioFloats.resize(size_t(frames) * size_t(kAudioChannels));
    for (int frame = 0; frame < frames; ++frame) {
        for (int channel = 0; channel < kAudioChannels; ++channel) {
            // A mono device feeds both sides rather than leaving one silent, which is what a
            // headset microphone would otherwise sound like after the recording is placed.
            const int sourceChannel = channel < channels ? channel : 0;
            const char *sample =
                audioScratch.data() + (size_t(frame) * size_t(channels) + size_t(sourceChannel))
                                          * size_t(bytesPerSample);
            float value = 0.0f;
            if (audioFormat.sampleFormat() == QAudioFormat::Float) {
                std::memcpy(&value, sample, sizeof(float));
            } else if (audioFormat.sampleFormat() == QAudioFormat::Int16) {
                qint16 raw = 0;
                std::memcpy(&raw, sample, sizeof(qint16));
                value = float(raw) / 32768.0f;
            } else if (audioFormat.sampleFormat() == QAudioFormat::Int32) {
                qint32 raw = 0;
                std::memcpy(&raw, sample, sizeof(qint32));
                value = float(double(raw) / 2147483648.0);
            } else if (audioFormat.sampleFormat() == QAudioFormat::UInt8) {
                value = (float(quint8(*sample)) - 128.0f) / 128.0f;
            }
            audioFloats[size_t(frame) * size_t(kAudioChannels) + size_t(channel)] = value;
        }
    }

    QString error;
    writer->writeAudio(audioFloats.data(), frames, &error);
}

void CaptureController::Impl::teardown()
{
    if (audio) {
        audio->stop();
        audio.reset();
    }
    audioDevice = nullptr;
    if (source) {
        source->stop();
        source.reset();
    }
    writer.reset();
    writerOpen = false;
}

void CaptureController::Impl::finishRecording()
{
    const QString file = path;
    const bool keep = !discard;

    if (writer && writerOpen) {
        QString error;
        if (keep) {
            if (!writer->finish(&error)) {
                teardown();
                recording = false;
                emit owner->recordingChanged();
                setStatus(error);
                app->setLastMessage(error, QStringLiteral("error"));
                emit owner->failed(error);
                return;
            }
        } else {
            writer->abort();
        }
    }
    teardown();

    recording = false;
    emit owner->recordingChanged();

    if (!keep) {
        if (!file.isEmpty())
            QFile::remove(file);
        setStatus(QString());
        return;
    }

    if (file.isEmpty() || !QFile::exists(file)) {
        const QString message = tr("The recording did not produce a file.");
        setStatus(message);
        app->setLastMessage(message, QStringLiteral("error"));
        emit owner->failed(message);
        return;
    }

    setStatus(QString());
    if (AssetLibrary *library = app->assetLibrary())
        library->importUrls({QUrl::fromLocalFile(file)});
    app->setLastMessage(tr("Recording saved and added to the bin"), QStringLiteral("success"));
    emit owner->finished(file);
}

CaptureController::CaptureController(AppController *app, QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Impl>())
{
    d->app = app;
    d->owner = this;
}

CaptureController::~CaptureController()
{
    // Never leave a half-written file behind on shutdown: abort() removes the ".part".
    if (d->writer && d->writerOpen)
        d->writer->abort();
    d->teardown();
}

bool CaptureController::recording() const
{
    return d->recording;
}

double CaptureController::recordedSeconds() const
{
    return d->seconds;
}

QString CaptureController::status() const
{
    return d->statusText;
}

bool CaptureController::screenAvailable() const
{
    // Built and thrown away: whether a backend exists is a property of the running session, not
    // of the build, and this is the only honest way to ask.
    return drift::makeScreenCaptureSource() != nullptr;
}

bool CaptureController::cameraAvailable() const
{
    return drift::makeCameraCaptureSource() != nullptr;
}

bool CaptureController::platformChoosesScreen() const
{
    const auto probe = drift::makeScreenCaptureSource();
    return probe && probe->capabilities().platformChoosesSource;
}

QVariantList CaptureController::cameras() const
{
    return drift::CaptureDevices::cameras();
}

QVariantList CaptureController::audioInputs() const
{
    return drift::CaptureDevices::audioInputs();
}

QString CaptureController::systemAudioInput() const
{
    return drift::CaptureDevices::firstLoopbackInput();
}

bool CaptureController::recordScreen(const QString &audioInputId, int fps)
{
    if (d->recording) {
        d->app->setLastMessage(tr("A recording is already running."), QStringLiteral("warning"));
        return false;
    }
    auto source = drift::makeScreenCaptureSource(this);
    if (!source) {
        const QString message =
            tr("Drift cannot record the screen on this system. On Linux this needs "
               "xdg-desktop-portal and PipeWire.");
        d->app->setLastMessage(message, QStringLiteral("error"));
        return false;
    }
    d->source = std::move(source);

    drift::CaptureRequest request;
    request.source = drift::CaptureSource::Screen;
    request.fps = drift::clampCaptureFps(fps);
    d->fps = request.fps;

    d->path = d->resolveOutputPath(tr("Screen"));
    if (d->path.isEmpty()) {
        d->source.reset();
        d->app->setLastMessage(tr("There is nowhere to save the recording."),
                               QStringLiteral("error"));
        return false;
    }
    request.outputPath = d->path;

    // Connected before start, because the portal can answer — or be cancelled — immediately.
    connect(d->source.get(), &drift::ScreenCaptureSource::started, this,
            [this, audioInputId](const QSize &size) {
                drift::CaptureWriter::Settings settings;
                // Rounded down to even: a monitor can report an odd height, and H.264 refuses it.
                settings.size = QSize(size.width() - size.width() % 2,
                                      size.height() - size.height() % 2);
                settings.fps = d->fps;
                settings.audio = !audioInputId.isEmpty();
                settings.audioSampleRate = kAudioRate;
                settings.audioChannels = kAudioChannels;

                d->writer = std::make_unique<drift::CaptureWriter>();
                QString error;
                if (!d->writer->open(d->path, settings, &error)) {
                    d->writer.reset();
                    d->source->stop();
                    d->recording = false;
                    emit recordingChanged();
                    d->app->setLastMessage(error, QStringLiteral("error"));
                    emit failed(error);
                    return;
                }
                d->writerOpen = true;
                if (settings.audio && !d->startAudio(audioInputId)) {
                    // Not fatal: a picture with no sound beats refusing the take outright.
                    d->app->setLastMessage(tr("That microphone could not be opened, so this "
                                              "recording has no sound."),
                                           QStringLiteral("warning"));
                }
                d->setStatus(tr("Recording"));
            });
    connect(d->source.get(), &drift::ScreenCaptureSource::frame, this,
            [this](const QImage &image, drift::TimeUs timestampUs) {
                if (!d->writerOpen || !d->writer)
                    return;
                QString error;
                d->writer->writeVideoFrame(image, timestampUs, &error);
                const double seconds = double(timestampUs) / double(drift::kUsPerSecond);
                if (seconds - d->seconds >= 0.1) {
                    d->seconds = seconds;
                    emit recordedSecondsChanged();
                }
            });
    connect(d->source.get(), &drift::ScreenCaptureSource::failed, this,
            [this](const QString &message) {
                d->discard = true;
                d->finishRecording();
                d->app->setLastMessage(message, QStringLiteral("error"));
                emit failed(message);
            });
    connect(d->source.get(), &drift::ScreenCaptureSource::stopped, this, [this] {
        // The portal reports this when the user cancels the picker or revokes the share, so it
        // has to finalize whatever was written rather than assume stop() did.
        if (d->recording)
            d->finishRecording();
    });

    d->discard = false;
    d->seconds = 0.0;
    d->recording = true;
    emit recordingChanged();
    emit recordedSecondsChanged();
    d->setStatus(platformChoosesScreen() ? tr("Waiting for you to choose what to share…")
                                         : tr("Starting…"));
    d->source->start(request);
    return true;
}

bool CaptureController::recordCamera(const QString &cameraId, const QString &audioInputId, int fps)
{
    if (d->recording) {
        d->app->setLastMessage(tr("A recording is already running."), QStringLiteral("warning"));
        return false;
    }
    auto source = drift::makeCameraCaptureSource(this);
    if (!source) {
        d->app->setLastMessage(tr("No camera is connected."), QStringLiteral("error"));
        return false;
    }

    // The camera shares the screen path's wiring entirely; only the request differs.
    d->source = std::move(source);
    drift::CaptureRequest request;
    request.source = drift::CaptureSource::Camera;
    request.cameraId = cameraId;
    request.fps = drift::clampCaptureFps(fps);
    d->fps = request.fps;

    d->path = d->resolveOutputPath(tr("Camera"));
    if (d->path.isEmpty()) {
        d->source.reset();
        d->app->setLastMessage(tr("There is nowhere to save the recording."),
                               QStringLiteral("error"));
        return false;
    }
    request.outputPath = d->path;

    connect(d->source.get(), &drift::ScreenCaptureSource::started, this,
            [this, audioInputId](const QSize &size) {
                drift::CaptureWriter::Settings settings;
                settings.size = QSize(size.width() - size.width() % 2,
                                      size.height() - size.height() % 2);
                settings.fps = d->fps;
                settings.audio = !audioInputId.isEmpty();
                settings.audioSampleRate = kAudioRate;
                settings.audioChannels = kAudioChannels;

                d->writer = std::make_unique<drift::CaptureWriter>();
                QString error;
                if (!d->writer->open(d->path, settings, &error)) {
                    d->writer.reset();
                    d->source->stop();
                    d->recording = false;
                    emit recordingChanged();
                    d->app->setLastMessage(error, QStringLiteral("error"));
                    emit failed(error);
                    return;
                }
                d->writerOpen = true;
                if (settings.audio && !d->startAudio(audioInputId)) {
                    d->app->setLastMessage(tr("That microphone could not be opened, so this "
                                              "recording has no sound."),
                                           QStringLiteral("warning"));
                }
                d->setStatus(tr("Recording"));
            });
    connect(d->source.get(), &drift::ScreenCaptureSource::frame, this,
            [this](const QImage &image, drift::TimeUs timestampUs) {
                if (!d->writerOpen || !d->writer)
                    return;
                QString error;
                d->writer->writeVideoFrame(image, timestampUs, &error);
                const double seconds = double(timestampUs) / double(drift::kUsPerSecond);
                if (seconds - d->seconds >= 0.1) {
                    d->seconds = seconds;
                    emit recordedSecondsChanged();
                }
            });
    connect(d->source.get(), &drift::ScreenCaptureSource::failed, this,
            [this](const QString &message) {
                d->discard = true;
                d->finishRecording();
                d->app->setLastMessage(message, QStringLiteral("error"));
                emit failed(message);
            });

    d->discard = false;
    d->seconds = 0.0;
    d->recording = true;
    emit recordingChanged();
    emit recordedSecondsChanged();
    d->setStatus(tr("Starting…"));
    d->source->start(request);
    return true;
}

void CaptureController::stop()
{
    if (!d->recording)
        return;
    d->discard = false;
    d->finishRecording();
}

void CaptureController::cancel()
{
    if (!d->recording)
        return;
    d->discard = true;
    d->finishRecording();
}
