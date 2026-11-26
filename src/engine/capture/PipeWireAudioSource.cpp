#include "engine/capture/PipeWireAudioSource.h"

#include <spa/param/audio/format-utils.h>
#include <spa/pod/builder.h>

#include <pipewire/pipewire.h>

namespace drift {

struct PipeWireAudioSource::Impl
{
    PipeWireAudioSource *owner = nullptr;

    pw_thread_loop *loop = nullptr;
    pw_stream *stream = nullptr;
    spa_hook listener{};
    bool listenerAdded = false;

    int channels = 2;
    bool running = false;

    void teardown();
};

namespace {

void onProcess(void *userData)
{
    auto *impl = static_cast<PipeWireAudioSource::Impl *>(userData);
    pw_buffer *buffer = pw_stream_dequeue_buffer(impl->stream);
    if (!buffer)
        return;

    spa_buffer *spaBuffer = buffer->buffer;
    if (spaBuffer->n_datas > 0 && spaBuffer->datas[0].data) {
        const spa_data &data = spaBuffer->datas[0];
        const uint32_t bytes = data.chunk ? data.chunk->size : 0;
        const int count = int(bytes / sizeof(float));
        if (count > 0) {
            // Copied out before the buffer is requeued: PipeWire reuses it immediately, so a
            // view into it would be overwritten before the consumer ran.
            const float *source = static_cast<const float *>(data.data);
            QVector<float> interleaved(count);
            std::copy(source, source + count, interleaved.begin());
            QMetaObject::invokeMethod(impl->owner, "deliver", Qt::QueuedConnection,
                                      Q_ARG(QVector<float>, interleaved));
        }
    }

    pw_stream_queue_buffer(impl->stream, buffer);
}

void onStateChanged(void *userData, enum pw_stream_state old, enum pw_stream_state state,
                    const char *message)
{
    Q_UNUSED(old);
    auto *impl = static_cast<PipeWireAudioSource::Impl *>(userData);
    if (state != PW_STREAM_STATE_ERROR)
        return;
    const QString text = message ? QString::fromUtf8(message)
                                 : QStringLiteral("The system audio stream failed");
    QMetaObject::invokeMethod(impl->owner, "deliverFailure", Qt::QueuedConnection,
                              Q_ARG(QString, text));
}

const pw_stream_events kEvents = {
    .version = PW_VERSION_STREAM_EVENTS,
    .state_changed = onStateChanged,
    .process = onProcess,
};

} // namespace

void PipeWireAudioSource::Impl::teardown()
{
    if (loop)
        pw_thread_loop_stop(loop);
    if (stream) {
        if (listenerAdded) {
            spa_hook_remove(&listener);
            listenerAdded = false;
        }
        pw_stream_destroy(stream);
        stream = nullptr;
    }
    if (loop) {
        pw_thread_loop_destroy(loop);
        loop = nullptr;
    }
    running = false;
}

PipeWireAudioSource::PipeWireAudioSource(QObject *parent)
    : SystemAudioSource(parent)
    , d(std::make_unique<Impl>())
{
    d->owner = this;
}

PipeWireAudioSource::~PipeWireAudioSource()
{
    d->teardown();
}

bool PipeWireAudioSource::available()
{
    pw_init(nullptr, nullptr);
    // Connecting is the only honest test: the library being present says nothing about a server
    // running, which is the case in a container or a bare login shell.
    pw_thread_loop *loop = pw_thread_loop_new("drift-pw-probe", nullptr);
    if (!loop)
        return false;
    bool ok = false;
    pw_thread_loop_lock(loop);
    if (pw_context *context = pw_context_new(pw_thread_loop_get_loop(loop), nullptr, 0)) {
        if (pw_core *core = pw_context_connect(context, nullptr, 0)) {
            ok = true;
            pw_core_disconnect(core);
        }
        pw_context_destroy(context);
    }
    pw_thread_loop_unlock(loop);
    pw_thread_loop_destroy(loop);
    return ok;
}

bool PipeWireAudioSource::active() const
{
    return d->running;
}

bool PipeWireAudioSource::start(int sampleRate, int channels, QString *errorOut)
{
    auto fail = [&](const QString &message) {
        if (errorOut)
            *errorOut = message;
        d->teardown();
        return false;
    };

    if (d->running)
        return fail(QStringLiteral("System audio capture is already running"));
    if (sampleRate < 8000 || channels < 1 || channels > 8)
        return fail(QStringLiteral("Unsupported system audio format"));

    pw_init(nullptr, nullptr);
    d->channels = channels;

    d->loop = pw_thread_loop_new("drift-system-audio", nullptr);
    if (!d->loop)
        return fail(QStringLiteral("Could not create the system audio thread"));

    // stream.capture.sink is the whole trick: it turns a capture stream from "record a
    // microphone" into "record what the default sink is playing", which is the monitor Qt will
    // not show. Without it this would open the default input and record the room.
    pw_properties *props =
        pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, "Capture",
                          PW_KEY_MEDIA_ROLE, "Production", PW_KEY_STREAM_CAPTURE_SINK, "true",
                          nullptr);

    pw_thread_loop_lock(d->loop);
    d->stream = pw_stream_new_simple(pw_thread_loop_get_loop(d->loop), "drift-system-audio",
                                     props, &kEvents, d.get());
    if (!d->stream) {
        pw_thread_loop_unlock(d->loop);
        return fail(QStringLiteral("Could not create the system audio stream"));
    }
    d->listenerAdded = false; // pw_stream_new_simple installed the listener itself

    uint8_t buffer[1024];
    spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    spa_audio_info_raw info = {};
    // Interleaved float32, exactly what CaptureWriter::writeAudio takes, so nothing has to be
    // converted between here and the encoder.
    info.format = SPA_AUDIO_FORMAT_F32;
    info.rate = uint32_t(sampleRate);
    info.channels = uint32_t(channels);
    const spa_pod *params[1];
    params[0] = spa_format_audio_raw_build(&builder, SPA_PARAM_EnumFormat, &info);

    const int rc = pw_stream_connect(
        d->stream, PW_DIRECTION_INPUT, PW_ID_ANY,
        static_cast<pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS
                                     | PW_STREAM_FLAG_RT_PROCESS),
        params, 1);
    pw_thread_loop_unlock(d->loop);
    if (rc < 0)
        return fail(QStringLiteral("Could not connect to the system audio output"));

    if (pw_thread_loop_start(d->loop) < 0)
        return fail(QStringLiteral("Could not start the system audio thread"));

    d->running = true;
    return true;
}

void PipeWireAudioSource::stop()
{
    d->teardown();
}

void PipeWireAudioSource::deliver(const QVector<float> &interleaved)
{
    if (!d->running)
        return;
    emit samples(interleaved);
}

void PipeWireAudioSource::deliverFailure(const QString &message)
{
    d->teardown();
    emit failed(message);
}

} // namespace drift
