#include "app/platform/PointerLock.h"

#include <QGuiApplication>
#include <QWindow>

#ifdef DRIFT_HAVE_WAYLAND_LOCK

#include <QtGui/qpa/qplatformwindow_p.h>

#include <wayland-client.h>

#include "pointer-constraints-unstable-v1-client-protocol.h"
#include "relative-pointer-unstable-v1-client-protocol.h"

#include <cstring>

namespace drift {

namespace {

class WaylandLock : public PointerLockBackend
{
public:
    WaylandLock(wl_display *display, wl_pointer *pointer)
        : m_pointer(pointer)
    {
        wl_registry *registry = wl_display_get_registry(display);
        static const wl_registry_listener listener{
            [](void *data, wl_registry *reg, uint32_t name, const char *interface, uint32_t) {
                auto *self = static_cast<WaylandLock *>(data);
                if (!std::strcmp(interface, zwp_pointer_constraints_v1_interface.name))
                    self->m_constraints = static_cast<zwp_pointer_constraints_v1 *>(
                        wl_registry_bind(reg, name, &zwp_pointer_constraints_v1_interface, 1));
                else if (!std::strcmp(interface, zwp_relative_pointer_manager_v1_interface.name))
                    self->m_relative = static_cast<zwp_relative_pointer_manager_v1 *>(
                        wl_registry_bind(reg, name, &zwp_relative_pointer_manager_v1_interface, 1));
            },
            [](void *, wl_registry *, uint32_t) {},
        };
        wl_registry_add_listener(registry, &listener, this);
        wl_display_roundtrip(display);
        wl_registry_destroy(registry);
        m_display = display;
    }

    ~WaylandLock() override
    {
        unlock();
        if (m_constraints)
            zwp_pointer_constraints_v1_destroy(m_constraints);
        if (m_relative)
            zwp_relative_pointer_manager_v1_destroy(m_relative);
    }

    bool available() const { return m_constraints && m_relative; }

    bool lock(QWindow *window) override
    {
        auto *native = window->nativeInterface<QNativeInterface::Private::QWaylandWindow>();
        wl_surface *surface = native ? native->surface() : nullptr;
        if (!surface)
            return false;
        unlock();
        m_locked = zwp_pointer_constraints_v1_lock_pointer(m_constraints, surface, m_pointer, nullptr,
                                                           ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_ONESHOT);
        static const zwp_locked_pointer_v1_listener lockedListener{
            [](void *data, zwp_locked_pointer_v1 *) {
                static_cast<WaylandLock *>(data)->onActive(true);
            },
            [](void *data, zwp_locked_pointer_v1 *) {
                // A oneshot lock is dead once the compositor has unlocked it.
                auto *self = static_cast<WaylandLock *>(data);
                self->release();
                self->onActive(false);
            },
        };
        zwp_locked_pointer_v1_add_listener(m_locked, &lockedListener, this);
        m_motion = zwp_relative_pointer_manager_v1_get_relative_pointer(m_relative, m_pointer);
        static const zwp_relative_pointer_v1_listener motionListener{
            [](void *data, zwp_relative_pointer_v1 *, uint32_t, uint32_t, wl_fixed_t, wl_fixed_t,
               wl_fixed_t dxUnaccel, wl_fixed_t dyUnaccel) {
                static_cast<WaylandLock *>(data)->onMotion(wl_fixed_to_double(dxUnaccel),
                                                          wl_fixed_to_double(dyUnaccel));
            },
        };
        zwp_relative_pointer_v1_add_listener(m_motion, &motionListener, this);
        wl_display_flush(m_display);
        return true;
    }

    void unlock() override
    {
        if (!m_locked && !m_motion)
            return;
        release();
        wl_display_flush(m_display);
        onActive(false);
    }

private:
    void release()
    {
        if (m_locked)
            zwp_locked_pointer_v1_destroy(m_locked);
        if (m_motion)
            zwp_relative_pointer_v1_destroy(m_motion);
        m_locked = nullptr;
        m_motion = nullptr;
    }

    wl_display *m_display = nullptr;
    wl_pointer *m_pointer = nullptr;
    zwp_pointer_constraints_v1 *m_constraints = nullptr;
    zwp_relative_pointer_manager_v1 *m_relative = nullptr;
    zwp_locked_pointer_v1 *m_locked = nullptr;
    zwp_relative_pointer_v1 *m_motion = nullptr;
};

} // namespace

std::unique_ptr<PointerLockBackend> makeWaylandPointerLock()
{
    auto *wayland = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
    if (!wayland || !wayland->display() || !wayland->pointer())
        return nullptr;
    auto lock = std::make_unique<WaylandLock>(wayland->display(), wayland->pointer());
    if (!lock->available())
        return nullptr;
    return lock;
}

} // namespace drift

#endif
