#include "app/platform/PointerLock.h"

#include <QCursor>
#include <QEvent>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScreen>
#include <QWindow>

#if defined(Q_OS_WIN)
#include <QAbstractNativeEventFilter>
#include <windows.h>
#elif defined(Q_OS_MACOS)
#include <ApplicationServices/ApplicationServices.h>

#include <QTimer>
#elif defined(Q_OS_ANDROID)
#include <QJniEnvironment>
#include <QJniObject>
#include <QMetaObject>
#include <QtCore/qcoreapplication_platform.h>
#endif

namespace drift {

#ifndef DRIFT_HAVE_WAYLAND_LOCK
std::unique_ptr<PointerLockBackend> makeWaylandPointerLock()
{
    return nullptr;
}
#endif

namespace {

#if defined(Q_OS_WIN)

class RawInputLock : public PointerLockBackend, public QAbstractNativeEventFilter
{
public:
    ~RawInputLock() override { unlock(); }

    bool lock(QWindow *window) override
    {
        m_hwnd = reinterpret_cast<HWND>(window->winId());
        const RAWINPUTDEVICE device{0x01, 0x02, 0, m_hwnd};
        if (!RegisterRawInputDevices(&device, 1, sizeof(device)))
            return false;
        // One pixel at the window centre: the pointer cannot drift while raw input keeps counting.
        const QPoint centre = window->mapToGlobal(QPoint(window->width() / 2, window->height() / 2));
        const qreal ratio = window->devicePixelRatio();
        const int x = int(centre.x() * ratio);
        const int y = int(centre.y() * ratio);
        const RECT clip{x, y, x + 1, y + 1};
        ClipCursor(&clip);
        QCoreApplication::instance()->installNativeEventFilter(this);
        m_active = true;
        onActive(true);
        return true;
    }

    void unlock() override
    {
        if (!m_active)
            return;
        m_active = false;
        QCoreApplication::instance()->removeNativeEventFilter(this);
        ClipCursor(nullptr);
        const RAWINPUTDEVICE device{0x01, 0x02, RIDEV_REMOVE, nullptr};
        RegisterRawInputDevices(&device, 1, sizeof(device));
        onActive(false);
    }

    bool nativeEventFilter(const QByteArray &type, void *message, qintptr *) override
    {
        if (type != "windows_generic_MSG")
            return false;
        const MSG *msg = static_cast<const MSG *>(message);
        if (msg->message != WM_INPUT)
            return false;
        RAWINPUT input;
        UINT size = sizeof(input);
        if (GetRawInputData(reinterpret_cast<HRAWINPUT>(msg->lParam), RID_INPUT, &input, &size,
                            sizeof(RAWINPUTHEADER)) == UINT(-1))
            return false;
        if (input.header.dwType != RIM_TYPEMOUSE || (input.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE))
            return false;
        onMotion(input.data.mouse.lLastX, input.data.mouse.lLastY);
        return false;
    }

private:
    HWND m_hwnd = nullptr;
    bool m_active = false;
};

std::unique_ptr<PointerLockBackend> makePlatformLock()
{
    return std::make_unique<RawInputLock>();
}

#elif defined(Q_OS_MACOS)

class DecoupledCursorLock : public PointerLockBackend
{
public:
    DecoupledCursorLock()
    {
        m_poll.setInterval(4);
        QObject::connect(&m_poll, &QTimer::timeout, &m_poll, [this] {
            int32_t dx = 0;
            int32_t dy = 0;
            CGGetLastMouseDelta(&dx, &dy);
            if (dx != 0 || dy != 0)
                onMotion(dx, dy);
        });
    }
    ~DecoupledCursorLock() override { unlock(); }

    bool lock(QWindow *) override
    {
        if (CGAssociateMouseAndMouseCursorPosition(false) != kCGErrorSuccess)
            return false;
        int32_t dx = 0;
        int32_t dy = 0;
        CGGetLastMouseDelta(&dx, &dy);
        m_poll.start();
        m_active = true;
        onActive(true);
        return true;
    }

    void unlock() override
    {
        if (!m_active)
            return;
        m_active = false;
        m_poll.stop();
        CGAssociateMouseAndMouseCursorPosition(true);
        onActive(false);
    }

private:
    QTimer m_poll;
    bool m_active = false;
};

std::unique_ptr<PointerLockBackend> makePlatformLock()
{
    return std::make_unique<DecoupledCursorLock>();
}

#elif defined(Q_OS_ANDROID)

constexpr const char *kCaptureClass = "org/cutwire/drift/PointerCapture";

class AndroidCaptureLock;
AndroidCaptureLock *g_capture = nullptr;

class AndroidCaptureLock : public PointerLockBackend
{
public:
    AndroidCaptureLock() { g_capture = this; }
    ~AndroidCaptureLock() override
    {
        unlock();
        g_capture = nullptr;
    }

    bool lock(QWindow *) override
    {
        QJniObject context = QNativeInterface::QAndroidApplication::context();
        if (!context.isValid())
            return false;
        QJniObject::callStaticMethod<void>(kCaptureClass, "request", "(Landroid/content/Context;)V",
                                           context.object());
        return !QJniEnvironment().checkAndClearExceptions();
    }

    void unlock() override
    {
        QJniObject context = QNativeInterface::QAndroidApplication::context();
        if (!context.isValid())
            return;
        QJniObject::callStaticMethod<void>(kCaptureClass, "release", "(Landroid/content/Context;)V",
                                           context.object());
        QJniEnvironment().checkAndClearExceptions();
    }
};

// Both run on the Android UI thread.
void JNICALL captureMotion(JNIEnv *, jclass, jfloat dx, jfloat dy)
{
    QMetaObject::invokeMethod(qApp, [dx, dy] {
        if (g_capture)
            g_capture->onMotion(dx, dy);
    }, Qt::QueuedConnection);
}

void JNICALL captureChanged(JNIEnv *, jclass, jboolean active)
{
    QMetaObject::invokeMethod(qApp, [active] {
        if (g_capture)
            g_capture->onActive(active);
    }, Qt::QueuedConnection);
}

std::unique_ptr<PointerLockBackend> makePlatformLock()
{
    static const bool registered = [] {
        const JNINativeMethod methods[] = {
            {"nativeMotion", "(FF)V", reinterpret_cast<void *>(captureMotion)},
            {"nativeChanged", "(Z)V", reinterpret_cast<void *>(captureChanged)},
        };
        return QJniEnvironment().registerNativeMethods(kCaptureClass, methods, 2);
    }();
    if (!registered)
        return nullptr;
    return std::make_unique<AndroidCaptureLock>();
}

#elif defined(Q_OS_UNIX) && !defined(Q_OS_WASM)

// X11: hide the pointer, grab it and warp it back to the centre after each move; the distance it
// travelled is the delta. Compositors that ignore QCursor::setPos (Wayland) cannot use this.
class WarpLock : public PointerLockBackend, public QObject
{
public:
    ~WarpLock() override { unlock(); }

    bool lock(QWindow *window) override
    {
        if (QGuiApplication::platformName() != QLatin1String("xcb"))
            return false;
        m_window = window;
        window->installEventFilter(this);
        window->setMouseGrabEnabled(true);
        recentre();
        onActive(true);
        return true;
    }

    void unlock() override
    {
        if (!m_window)
            return;
        m_window->removeEventFilter(this);
        m_window->setMouseGrabEnabled(false);
        m_window = nullptr;
        onActive(false);
    }

protected:
    bool eventFilter(QObject *, QEvent *event) override
    {
        if (event->type() != QEvent::MouseMove || !m_window)
            return false;
        const QPointF delta = static_cast<QMouseEvent *>(event)->globalPosition() - m_centre;
        if (!delta.isNull()) {
            onMotion(delta.x(), delta.y());
            recentre();
        }
        return true;
    }

private:
    void recentre()
    {
        m_centre = QPointF(m_window->mapToGlobal(QPoint(m_window->width() / 2, m_window->height() / 2)));
        QCursor::setPos(m_window->screen(), m_centre.toPoint());
    }

    QPointer<QWindow> m_window;
    QPointF m_centre;
};

std::unique_ptr<PointerLockBackend> makePlatformLock()
{
    return std::make_unique<WarpLock>();
}

#else

std::unique_ptr<PointerLockBackend> makePlatformLock()
{
    return nullptr;
}

#endif

} // namespace

PointerLock::PointerLock(QObject *parent)
    : QObject(parent)
{
    connect(qGuiApp, &QGuiApplication::focusWindowChanged, this, [this](QWindow *focused) {
        if (m_window && focused != m_window)
            unlock();
    });
}

PointerLock::~PointerLock()
{
    unlock();
}

bool PointerLock::lock(QQuickItem *item)
{
    QWindow *window = item ? item->window() : nullptr;
    if (!window || !window->handle())
        return false;
    if (m_window == window)
        return true;
    unlock();
    m_backend = QGuiApplication::platformName() == QLatin1String("wayland") ? makeWaylandPointerLock()
                                                                               : makePlatformLock();
    if (!m_backend)
        return false;
    m_backend->onMotion = [this](double dx, double dy) { emit moved(dx, dy); };
    m_backend->onActive = [this](bool active) { setLocked(active); };
    m_window = window;
    if (!m_backend->lock(window)) {
        m_backend.reset();
        m_window = nullptr;
        return false;
    }
    return true;
}

void PointerLock::unlock()
{
    if (!m_backend)
        return;
    m_window = nullptr;
    m_backend->unlock();
    m_backend.reset();
    setLocked(false);
}

void PointerLock::setLocked(bool locked)
{
    if (locked == m_locked)
        return;
    m_locked = locked;
    if (locked) {
        QGuiApplication::setOverrideCursor(Qt::BlankCursor);
    } else {
        QGuiApplication::restoreOverrideCursor();
        // The compositor or OS took the lock back. The backend is still inside its callback, so
        // it is torn down once that has returned.
        QMetaObject::invokeMethod(this, &PointerLock::unlock, Qt::QueuedConnection);
    }
    emit lockedChanged();
}

} // namespace drift

#include "moc_PointerLock.cpp"
