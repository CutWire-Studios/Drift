#pragma once

#include <QObject>

#include <functional>
#include <memory>

class QQuickItem;
class QWindow;

// Relative mouse motion for 3D fly mode: the pointer is held still and hidden while the mouse's
// raw deltas arrive through moved(). Wayland uses zwp_pointer_constraints_v1 and Android pointer
// capture, Windows raw input, macOS decoupled cursor; elsewhere (X11) the pointer is hidden and
// warped back to the window centre after every move. lock() returns false where none of these
// work (a Wayland compositor without the protocol), and the caller falls back to steering.
namespace drift {

class PointerLockBackend
{
public:
    virtual ~PointerLockBackend() = default;
    virtual bool lock(QWindow *window) = 0;
    virtual void unlock() = 0;

    // Set by PointerLock. onActive reports the compositor/OS granting or revoking the lock.
    std::function<void(double dx, double dy)> onMotion;
    std::function<void(bool active)> onActive;
};

std::unique_ptr<PointerLockBackend> makeWaylandPointerLock();

class PointerLock : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool locked READ locked NOTIFY lockedChanged FINAL)

public:
    explicit PointerLock(QObject *parent = nullptr);
    ~PointerLock() override;

    bool locked() const { return m_locked; }

    // Asks for the lock on the item's window. True when a request is in flight or granted;
    // locked() turns true once it takes effect.
    Q_INVOKABLE bool lock(QQuickItem *item);
    Q_INVOKABLE void unlock();

signals:
    void moved(double dx, double dy);
    void lockedChanged();

private:
    void setLocked(bool locked);

    std::unique_ptr<PointerLockBackend> m_backend;
    QWindow *m_window = nullptr;
    bool m_locked = false;
};

} // namespace drift
