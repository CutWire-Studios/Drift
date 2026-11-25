#include "app/surfaces/SurfaceHub.h"

#include "app/surfaces/ControlSurfaceManager.h"

#ifdef DRIFT_SURFACE_MIDI
#include "app/surfaces/midi/MidiSurfaceService.h"
#endif

namespace drift {

SurfaceHub::SurfaceHub(ControlSurfaceManager *manager, QObject *parent)
    : QObject(parent)
{
#ifdef DRIFT_SURFACE_MIDI
    if (MidiIo *io = createPlatformMidiIo(this))
        m_midi = new MidiSurfaceService(manager, io, this);
#endif
#if !defined(DRIFT_SURFACE_MIDI)
    Q_UNUSED(manager)
#endif
}

} // namespace drift
