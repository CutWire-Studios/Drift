#pragma once

#include "GpuDevice.h"

#include <QList>
#include <QString>

// Which GPU the process runs on, for hybrid laptops. Windows only: on Linux that choice belongs to
// whoever launches Drift (prime-run, DRI_PRIME), not to Drift itself, so everything here is a no-op
// there and hardwareAdapters() is empty.
//
// GPU *identity* — what each adapter is, and which one OpenGL draws on — lives in GpuDevice.h.
namespace drift::gpu {

// Hardware adapters in DXGI order, software rasterizers skipped. Cached for the process: the
// set of GPUs does not change under a running app in any way the decoders could follow anyway.
QList<Adapter> hardwareAdapters();

// DXGI index of the first hardware adapter from the vendor a GL_VENDOR string names, or -1 when
// the string is empty, names no vendor this recognises, or matches no adapter here.
int adapterIndexForGlVendor(const QString &glVendor);

enum class Preference {
    Auto,            // whatever Windows and the driver decide
    PowerSaving,     // the integrated GPU
    HighPerformance, // the discrete GPU
};

// Stable ids for settings and QML: "auto", "integrated", "discrete". Unknown ids are Auto.
QString preferenceId(Preference preference);
Preference preferenceFromId(const QString &id);

Preference storedPreference();

// True where choosing changes anything: Windows with at least two hardware adapters.
bool preferenceSupported();

// Persists the choice and writes it where Windows keeps per-application GPU preferences. The GPU
// is picked when the graphics driver loads, so this takes effect on the next launch.
void storePreference(Preference preference);

// For main(), before any graphics context exists. Re-asserts a stored non-Auto choice for the
// executable's current path — the registry is keyed on it, and an update that installs somewhere
// else would otherwise drop back to the Windows default — and returns the choice.
Preference applyStoredPreference();

} // namespace drift::gpu
