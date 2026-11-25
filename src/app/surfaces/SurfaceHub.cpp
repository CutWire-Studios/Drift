#include "app/surfaces/SurfaceHub.h"

#include "app/surfaces/ControlSurfaceManager.h"

#ifdef DRIFT_SURFACE_MIDI
#include "app/surfaces/midi/MidiSurfaceService.h"
#endif
#ifdef DRIFT_SURFACE_OSC
#include "app/surfaces/osc/OscSurfaceService.h"
#endif

namespace drift {

SurfaceHub::SurfaceHub(ControlSurfaceManager *manager, QObject *parent)
    : QObject(parent)
{
#ifdef DRIFT_SURFACE_MIDI
    if (MidiIo *io = createPlatformMidiIo(this))
        m_midi = new MidiSurfaceService(manager, io, this);
#endif
#ifdef DRIFT_SURFACE_OSC
    m_osc = new OscSurfaceService(manager, this);
#endif
#if !defined(DRIFT_SURFACE_MIDI) && !defined(DRIFT_SURFACE_OSC)
    Q_UNUSED(manager)
#endif
}

} // namespace drift
