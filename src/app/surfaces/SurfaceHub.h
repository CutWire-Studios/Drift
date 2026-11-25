#pragma once

#include <QObject>

namespace drift {

class ControlSurfaceManager;

// The transports that feed ControlSurfaceManager, for the settings page. A transport that is not
// built for this platform (no MIDI on wasm, say) is null and its flag false. They are typed as
// QObject so this header pulls none of them in; QML reaches their members through the meta object.
class SurfaceHub : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool hasMidi READ hasMidi CONSTANT)
    Q_PROPERTY(QObject *midi READ midi CONSTANT)

public:
    explicit SurfaceHub(ControlSurfaceManager *manager, QObject *parent = nullptr);

    bool hasMidi() const { return m_midi != nullptr; }
    QObject *midi() const { return m_midi; }

private:
    QObject *m_midi = nullptr;
};

} // namespace drift
