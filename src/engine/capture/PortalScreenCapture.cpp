#include "engine/capture/PortalScreenCapture.h"

#include <QCoreApplication>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusUnixFileDescriptor>
#include <QRandomGenerator>
#include <QVariantMap>

#include <spa/param/video/format-utils.h>
#include <spa/pod/builder.h>
#include <spa/utils/result.h>

#include <pipewire/pipewire.h>

#include <unistd.h>

// One entry of the portal's `streams` array, a(ua{sv}): the PipeWire node to read from, plus its
// properties.
//
// Registered with Qt's D-Bus type system rather than walked by hand. A QDBusArgument taken
// straight out of the results map is still positioned on the variant that wraps it, so stepping
// into it manually reads a level too high and aborts inside libdbus ("type struct 114 not a basic
// type"). qdbus_cast unwraps it correctly.
//
// The frame size is deliberately not taken from here: PipeWire reports the negotiated size in
// param_changed, which is the size frames actually arrive at, and the portal's figure would be a
// second source of truth free to disagree with it.
struct DriftPortalStream
{
    uint nodeId = 0;
    QVariantMap properties;
};

inline const QDBusArgument &operator>>(const QDBusArgument &argument, DriftPortalStream &stream)
{
    argument.beginStructure();
    argument >> stream.nodeId >> stream.properties;
    argument.endStructure();
    return argument;
}

// Never sent, only received — but QDBusMetaType will not register a type without both directions.
inline QDBusArgument &operator<<(QDBusArgument &argument, const DriftPortalStream &stream)
{
    argument.beginStructure();
    argument << stream.nodeId << stream.properties;
    argument.endStructure();
    return argument;
}

Q_DECLARE_METATYPE(DriftPortalStream)
Q_DECLARE_METATYPE(QList<DriftPortalStream>)

namespace drift {

namespace {

constexpr const char *kPortalService = "org.freedesktop.portal.Desktop";
constexpr const char *kPortalPath = "/org/freedesktop/portal/desktop";
constexpr const char *kScreenCastIface = "org.freedesktop.portal.ScreenCast";
constexpr const char *kRequestIface = "org.freedesktop.portal.Request";
constexpr const char *kSessionIface = "org.freedesktop.portal.Session";

constexpr uint kSourceMonitor = 1;
constexpr uint kSourceWindow = 2;
constexpr uint kCursorHidden = 1;
constexpr uint kCursorEmbedded = 2;

// Unique per call, so the Request object path can be worked out and subscribed to *before* the
// method is sent. The portal is explicit that the Response signal may arrive before the method
// reply, and a subscription made afterwards would miss it and hang forever.
QString freshToken()
{
    return QStringLiteral("drift_%1_%2")
        .arg(QCoreApplication::applicationPid())
        .arg(QRandomGenerator::global()->generate(), 0, 16);
}

QString requestPathFor(const QDBusConnection &bus, const QString &token)
{
    QString unique = bus.baseService();
    if (unique.startsWith(QLatin1Char(':')))
        unique.remove(0, 1);
    unique.replace(QLatin1Char('.'), QLatin1Char('_'));
    return QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2").arg(unique, token);
}

QList<DriftPortalStream> parseStreams(const QVariant &value)
{
    return qdbus_cast<QList<DriftPortalStream>>(value);
}

// SPA video formats carrying four bytes a pixel, which is all this reads. Asking for exactly the
// set QImage can wrap keeps the conversion to a channel swap at worst.
spa_video_format kAcceptedFormats[] = {SPA_VIDEO_FORMAT_BGRx, SPA_VIDEO_FORMAT_RGBx,
                                       SPA_VIDEO_FORMAT_BGRA, SPA_VIDEO_FORMAT_RGBA};

} // namespace


// One portal request, listening on exactly its own object path and reporting once.
//
// This exists because Qt cannot connect a D-Bus signal to a lambda: without it the whole
// four-step conversation has to funnel through one slot and a step variable, and then a Response
// delivered twice — which happens, since each step subscribes afresh — advances the sequence a
// step early and hands one call's results to the next call's parser.
class PortalRequest : public QObject
{
    Q_OBJECT

public:
    PortalRequest(const QDBusConnection &bus, const QString &path, QObject *parent)
        : QObject(parent)
        , m_bus(bus)
        , m_path(path)
    {
        m_bus.connect(QLatin1String(kPortalService), m_path, QLatin1String(kRequestIface),
                      QStringLiteral("Response"), this,
                      SLOT(onResponse(uint, QVariantMap)));
    }

    ~PortalRequest() override
    {
        m_bus.disconnect(QLatin1String(kPortalService), m_path, QLatin1String(kRequestIface),
                         QStringLiteral("Response"), this,
                         SLOT(onResponse(uint, QVariantMap)));
    }

signals:
    void done(uint response, const QVariantMap &results);

private slots:
    void onResponse(uint response, const QVariantMap &results)
    {
        // The portal closes a request after answering it, but a duplicate delivery must not reach
        // the caller twice: the second one would carry the same results into the next step.
        if (m_answered)
            return;
        m_answered = true;
        emit done(response, results);
        deleteLater();
    }

private:
    QDBusConnection m_bus;
    QString m_path;
    bool m_answered = false;
};

struct PortalScreenCapture::Impl
{
    PortalScreenCapture *owner = nullptr;
    QDBusConnection bus = QDBusConnection::sessionBus();

    CaptureRequest request;
    bool starting = false;
    QString sessionHandle;
    bool running = false;

    pw_thread_loop *loop = nullptr;
    pw_context *context = nullptr;
    pw_core *core = nullptr;
    pw_stream *stream = nullptr;
    spa_hook streamListener{};
    bool listenerAdded = false;
    spa_video_info_raw videoInfo{};

    QSize frameSize;
    qint64 firstFrameUs = -1;

    // Sends a portal method and reports its single Response. The request object is created
    // before the call goes out, because the portal is explicit that Response may arrive before
    // the method reply — subscribing afterwards would miss it and hang.
    void call(const QString &method, const QVariantList &arguments, const QString &token,
              std::function<void(const QVariantMap &)> onOk);
    void fail(const QString &message);
    // The Start step's results: pick a stream and open the PipeWire remote for it.
    void onStarted(const QVariantMap &results);

    bool startPipeWire(uint nodeId, int fd, QString *errorOut);
    void teardownPipeWire();
    void closeSession();
};

void PortalScreenCapture::Impl::call(const QString &method, const QVariantList &arguments,
                                     const QString &token,
                                     std::function<void(const QVariantMap &)> onOk)
{
    auto *request = new PortalRequest(bus, requestPathFor(bus, token), owner);
    QObject::connect(request, &PortalRequest::done, owner,
                     [this, onOk](uint response, const QVariantMap &results) {
                         // 1 is the user closing the picker: an ordinary outcome, not a fault,
                         // and not something to show them an error about.
                         if (response == 1) {
                             starting = false;
                             closeSession();
                             emit owner->stopped();
                             return;
                         }
                         if (response != 0) {
                             fail(QCoreApplication::translate(
                                 "PortalScreenCapture",
                                 "The desktop refused to share the screen."));
                             return;
                         }
                         onOk(results);
                     });

    QDBusMessage message = QDBusMessage::createMethodCall(
        QLatin1String(kPortalService), QLatin1String(kPortalPath),
        QLatin1String(kScreenCastIface), method);
    message.setArguments(arguments);
    bus.asyncCall(message);
}

void PortalScreenCapture::Impl::fail(const QString &message)
{
    starting = false;
    teardownPipeWire();
    closeSession();
    const bool wasRunning = running;
    running = false;
    emit owner->failed(message);
    if (wasRunning)
        emit owner->stopped();
}

void PortalScreenCapture::Impl::closeSession()
{
    if (sessionHandle.isEmpty())
        return;
    QDBusMessage close = QDBusMessage::createMethodCall(
        QLatin1String(kPortalService), sessionHandle, QLatin1String(kSessionIface),
        QStringLiteral("Close"));
    bus.asyncCall(close);
    sessionHandle.clear();
}

// ---------------------------------------------------------------------------------------------
// PipeWire
// ---------------------------------------------------------------------------------------------

namespace {

void onStreamParamChanged(void *userData, uint32_t id, const struct spa_pod *param)
{
    auto *impl = static_cast<PortalScreenCapture::Impl *>(userData);
    if (!param || id != SPA_PARAM_Format)
        return;

    uint32_t mediaType = 0;
    uint32_t mediaSubtype = 0;
    if (spa_format_parse(param, &mediaType, &mediaSubtype) < 0)
        return;
    if (mediaType != SPA_MEDIA_TYPE_video || mediaSubtype != SPA_MEDIA_SUBTYPE_raw)
        return;
    if (spa_format_video_raw_parse(param, &impl->videoInfo) < 0)
        return;

    impl->frameSize = QSize(int(impl->videoInfo.size.width), int(impl->videoInfo.size.height));

    // Ask for plain mapped memory rather than a DMA-BUF. A dmabuf would avoid the copy, but it
    // has to be imported through EGL on the render thread to be read at all, and this encodes on
    // the CPU anyway — so the import would buy nothing and couple capture to the GL context.
    uint8_t buffer[1024];
    spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    const spa_pod *params[1];
    params[0] = static_cast<const spa_pod *>(spa_pod_builder_add_object(
        &builder, SPA_TYPE_OBJECT_ParamBuffers, SPA_PARAM_Buffers,
        SPA_PARAM_BUFFERS_buffers, SPA_POD_CHOICE_RANGE_Int(4, 2, 8),
        SPA_PARAM_BUFFERS_dataType,
        SPA_POD_CHOICE_FLAGS_Int(1 << SPA_DATA_MemPtr | 1 << SPA_DATA_MemFd)));
    pw_stream_update_params(impl->stream, params, 1);

    const QSize size = impl->frameSize;
    QMetaObject::invokeMethod(
        impl->owner, [owner = impl->owner, size]() { emit owner->started(size); },
        Qt::QueuedConnection);
}

void onStreamProcess(void *userData)
{
    auto *impl = static_cast<PortalScreenCapture::Impl *>(userData);
    pw_buffer *buffer = pw_stream_dequeue_buffer(impl->stream);
    if (!buffer)
        return;

    spa_buffer *spaBuffer = buffer->buffer;
    if (spaBuffer->n_datas > 0 && spaBuffer->datas[0].data && impl->frameSize.isValid()) {
        const spa_data &data = spaBuffer->datas[0];
        const int width = impl->frameSize.width();
        const int height = impl->frameSize.height();
        const int stride = data.chunk->stride > 0 ? int(data.chunk->stride) : width * 4;

        // Wrapped, then copied: the buffer has to go back to PipeWire immediately, and a QImage
        // over its memory would dangle the moment it is requeued.
        const QImage::Format format =
            (impl->videoInfo.format == SPA_VIDEO_FORMAT_BGRA
             || impl->videoInfo.format == SPA_VIDEO_FORMAT_RGBA)
                ? QImage::Format_ARGB32
                : QImage::Format_RGB32;
        QImage wrapped(static_cast<const uchar *>(data.data), width, height, stride, format);
        QImage frame = wrapped.copy();
        if (impl->videoInfo.format == SPA_VIDEO_FORMAT_RGBx
            || impl->videoInfo.format == SPA_VIDEO_FORMAT_RGBA) {
            // QImage's 32-bit formats are BGRA in memory on little-endian machines, so an
            // RGB-ordered stream has to have its red and blue swapped or everything is teal.
            frame = frame.rgbSwapped();
        }

        // PipeWire timestamps on the graph clock. The first frame defines zero, so a recording
        // always starts at the beginning however long the portal dialog was open for.
        qint64 nowUs = 0;
        if (buffer->time > 0)
            nowUs = qint64(buffer->time) / 1000;
        if (impl->firstFrameUs < 0)
            impl->firstFrameUs = nowUs;
        const qint64 stamp = nowUs - impl->firstFrameUs;

        QMetaObject::invokeMethod(impl->owner, "deliverFrame", Qt::QueuedConnection,
                                  Q_ARG(QImage, frame), Q_ARG(qint64, stamp));
    }

    pw_stream_queue_buffer(impl->stream, buffer);
}

void onStreamStateChanged(void *userData, enum pw_stream_state old, enum pw_stream_state state,
                          const char *message)
{
    Q_UNUSED(old);
    auto *impl = static_cast<PortalScreenCapture::Impl *>(userData);
    if (state != PW_STREAM_STATE_ERROR)
        return;
    const QString text = message ? QString::fromUtf8(message)
                                 : QStringLiteral("The screen capture stream failed");
    QMetaObject::invokeMethod(impl->owner, "deliverFailure", Qt::QueuedConnection,
                              Q_ARG(QString, text));
}

const pw_stream_events kStreamEvents = {
    .version = PW_VERSION_STREAM_EVENTS,
    .state_changed = onStreamStateChanged,
    .param_changed = onStreamParamChanged,
    .process = onStreamProcess,
};

} // namespace

bool PortalScreenCapture::Impl::startPipeWire(uint nodeId, int fd, QString *errorOut)
{
    auto fail = [&](const QString &message) {
        if (errorOut)
            *errorOut = message;
        teardownPipeWire();
        return false;
    };

    // Idempotent and refcounted inside PipeWire, so calling it per session is fine.
    pw_init(nullptr, nullptr);

    loop = pw_thread_loop_new("drift-capture", nullptr);
    if (!loop)
        return fail(QStringLiteral("Could not create the capture thread"));

    pw_thread_loop_lock(loop);
    context = pw_context_new(pw_thread_loop_get_loop(loop), nullptr, 0);
    if (!context) {
        pw_thread_loop_unlock(loop);
        return fail(QStringLiteral("Could not create the PipeWire context"));
    }

    // The descriptor the portal handed over is already an authenticated connection to the
    // server; connecting by name instead would be a second, unauthorised one.
    core = pw_context_connect_fd(context, fd, nullptr, 0);
    if (!core) {
        pw_thread_loop_unlock(loop);
        return fail(QStringLiteral("Could not connect to PipeWire"));
    }

    stream = pw_stream_new(core, "drift-screen-capture",
                           pw_properties_new(PW_KEY_MEDIA_TYPE, "Video", PW_KEY_MEDIA_CATEGORY,
                                             "Capture", PW_KEY_MEDIA_ROLE, "Screen", nullptr));
    if (!stream) {
        pw_thread_loop_unlock(loop);
        return fail(QStringLiteral("Could not create the capture stream"));
    }
    pw_stream_add_listener(stream, &streamListener, &kStreamEvents, this);
    listenerAdded = true;

    uint8_t buffer[2048];
    spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    const spa_pod *params[1];
    // Size and rate are left as wide ranges: the portal decides what the user shared, and a
    // narrow request here is how negotiation fails on a display this code never anticipated.
    spa_rectangle minSize = SPA_RECTANGLE(1, 1);
    spa_rectangle maxSize = SPA_RECTANGLE(8192, 8192);
    spa_rectangle defSize = SPA_RECTANGLE(1920, 1080);
    spa_fraction minRate = SPA_FRACTION(0, 1);
    spa_fraction maxRate = SPA_FRACTION(240, 1);
    spa_fraction defRate = SPA_FRACTION(30, 1);
    params[0] = static_cast<const spa_pod *>(spa_pod_builder_add_object(
        &builder, SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat,
        SPA_FORMAT_mediaType, SPA_POD_Id(SPA_MEDIA_TYPE_video),
        SPA_FORMAT_mediaSubtype, SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw),
        SPA_FORMAT_VIDEO_format,
        SPA_POD_CHOICE_ENUM_Id(5, kAcceptedFormats[0], kAcceptedFormats[0], kAcceptedFormats[1],
                               kAcceptedFormats[2], kAcceptedFormats[3]),
        SPA_FORMAT_VIDEO_size, SPA_POD_CHOICE_RANGE_Rectangle(&defSize, &minSize, &maxSize),
        SPA_FORMAT_VIDEO_framerate,
        SPA_POD_CHOICE_RANGE_Fraction(&defRate, &minRate, &maxRate)));

    const int rc = pw_stream_connect(stream, PW_DIRECTION_INPUT, nodeId,
                                     static_cast<pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT
                                                                  | PW_STREAM_FLAG_MAP_BUFFERS),
                                     params, 1);
    pw_thread_loop_unlock(loop);
    if (rc < 0)
        return fail(QStringLiteral("Could not connect to the shared screen"));

    if (pw_thread_loop_start(loop) < 0)
        return fail(QStringLiteral("Could not start the capture thread"));

    return true;
}

void PortalScreenCapture::Impl::teardownPipeWire()
{
    if (loop)
        pw_thread_loop_stop(loop);
    if (stream) {
        if (listenerAdded) {
            spa_hook_remove(&streamListener);
            listenerAdded = false;
        }
        pw_stream_destroy(stream);
        stream = nullptr;
    }
    if (core) {
        pw_core_disconnect(core);
        core = nullptr;
    }
    if (context) {
        pw_context_destroy(context);
        context = nullptr;
    }
    if (loop) {
        pw_thread_loop_destroy(loop);
        loop = nullptr;
    }
    firstFrameUs = -1;
    frameSize = QSize();
}

// ---------------------------------------------------------------------------------------------
// Public surface
// ---------------------------------------------------------------------------------------------

PortalScreenCapture::PortalScreenCapture(QObject *parent)
    : ScreenCaptureSource(parent)
    , d(std::make_unique<Impl>())
{
    // Idempotent, and it has to happen before any reply is demarshalled: without the registration
    // qdbus_cast cannot see the stream type and quietly returns an empty list.
    static const bool registered = [] {
        qDBusRegisterMetaType<DriftPortalStream>();
        qDBusRegisterMetaType<QList<DriftPortalStream>>();
        return true;
    }();
    Q_UNUSED(registered);
    d->owner = this;
}

PortalScreenCapture::~PortalScreenCapture()
{
    // Outstanding PortalRequests are children of this object, so their subscriptions go with them.
    d->teardownPipeWire();
    d->closeSession();
}

bool PortalScreenCapture::available()
{
    QDBusInterface iface(QLatin1String(kPortalService), QLatin1String(kPortalPath),
                         QLatin1String(kScreenCastIface), QDBusConnection::sessionBus());
    return iface.isValid() && iface.property("version").isValid();
}

ScreenCaptureSource::Capabilities PortalScreenCapture::capabilities() const
{
    Capabilities caps;
    QDBusInterface iface(QLatin1String(kPortalService), QLatin1String(kPortalPath),
                         QLatin1String(kScreenCastIface), QDBusConnection::sessionBus());
    const uint types = iface.property("AvailableSourceTypes").toUInt();
    caps.screens = (types & kSourceMonitor) != 0;
    caps.windows = (types & kSourceWindow) != 0;
    caps.cursorOptional = iface.property("AvailableCursorModes").toUInt() != 0;
    caps.platformChoosesSource = true;
    return caps;
}

bool PortalScreenCapture::active() const
{
    return d->running;
}

void PortalScreenCapture::start(const CaptureRequest &request)
{
    if (d->starting || d->running) {
        emit failed(tr("A screen recording is already starting."));
        return;
    }
    if (!available()) {
        emit failed(tr("This desktop has no screen-sharing portal, so Drift cannot record the "
                       "screen. Install xdg-desktop-portal and a backend for your desktop."));
        return;
    }

    d->request = request;
    d->starting = true;

    const QString token = freshToken();
    QVariantMap options{{QStringLiteral("handle_token"), token},
                        {QStringLiteral("session_handle_token"), freshToken()}};

    d->call(QStringLiteral("CreateSession"), {options}, token, [this](const QVariantMap &results) {
        d->sessionHandle = results.value(QStringLiteral("session_handle")).toString();
        if (d->sessionHandle.isEmpty()) {
            d->fail(tr("The screen-sharing portal returned no session."));
            return;
        }

        const QString selectToken = freshToken();
        QVariantMap selectOptions{
            {QStringLiteral("handle_token"), selectToken},
            // Both types regardless of what was asked for: the picker belongs to the user, and
            // letting them change their mind in it beats refusing what they chose.
            {QStringLiteral("types"), uint(kSourceMonitor | kSourceWindow)},
            {QStringLiteral("multiple"), false},
            {QStringLiteral("cursor_mode"), uint(kCursorEmbedded)}};

        d->call(QStringLiteral("SelectSources"),
                {QVariant::fromValue(QDBusObjectPath(d->sessionHandle)), selectOptions},
                selectToken, [this](const QVariantMap &) {
                    const QString startToken = freshToken();
                    QVariantMap startOptions{{QStringLiteral("handle_token"), startToken}};
                    // Empty parent window: Drift's window handle is not knowable here, and the
                    // portal reads that as "no parent", which only costs modality.
                    d->call(QStringLiteral("Start"),
                            {QVariant::fromValue(QDBusObjectPath(d->sessionHandle)), QString(),
                             startOptions},
                            startToken, [this](const QVariantMap &startResults) {
                                d->onStarted(startResults);
                            });
                });
    });
}

void PortalScreenCapture::stop()
{
    d->starting = false;
    d->teardownPipeWire();
    d->closeSession();
    if (d->running) {
        d->running = false;
        emit stopped();
    }
}

void PortalScreenCapture::deliverFrame(const QImage &image, qint64 timestampUs)
{
    if (!d->running)
        return;
    emit frame(image, TimeUs(timestampUs));
}

void PortalScreenCapture::deliverFailure(const QString &message)
{
    d->fail(message);
}

void PortalScreenCapture::Impl::onStarted(const QVariantMap &results)
{
    starting = false;
    const QList<DriftPortalStream> streams = parseStreams(results.value(QStringLiteral("streams")));
    if (streams.isEmpty()) {
        fail(QCoreApplication::translate("PortalScreenCapture",
                                         "The desktop shared nothing to record."));
        return;
    }

    const uint nodeId = streams.first().nodeId;
    QDBusMessage message = QDBusMessage::createMethodCall(
        QLatin1String(kPortalService), QLatin1String(kPortalPath),
        QLatin1String(kScreenCastIface), QStringLiteral("OpenPipeWireRemote"));
    message.setArguments({QVariant::fromValue(QDBusObjectPath(sessionHandle)),
                          QVariant::fromValue(QVariantMap{})});

    QDBusPendingReply<QDBusUnixFileDescriptor> reply = bus.asyncCall(message);
    auto *watcher = new QDBusPendingCallWatcher(reply, owner);
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, owner,
                     [this, nodeId](QDBusPendingCallWatcher *self) {
                         self->deleteLater();
                         QDBusPendingReply<QDBusUnixFileDescriptor> fdReply = *self;
                         if (fdReply.isError() || !fdReply.value().isValid()) {
                             fail(QCoreApplication::translate(
                                 "PortalScreenCapture",
                                 "The desktop would not open the screen-sharing stream."));
                             return;
                         }
                         // Duplicated: QDBusUnixFileDescriptor closes its own copy when it goes
                         // out of scope, and PipeWire takes ownership of what it is handed.
                         const int fd = ::dup(fdReply.value().fileDescriptor());
                         if (fd < 0) {
                             fail(QCoreApplication::translate(
                                 "PortalScreenCapture",
                                 "The desktop would not open the screen-sharing stream."));
                             return;
                         }
                         QString error;
                         if (!startPipeWire(nodeId, fd, &error)) {
                             fail(error);
                             return;
                         }
                         running = true;
                     });
}

} // namespace drift

#include "PortalScreenCapture.moc"
