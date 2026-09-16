#pragma once

// Which physical GPUs this machine has, and which one OpenGL renders on.
//
// Separate from GpuPreference, which is about *choosing* the GPU the process runs on and is
// Windows-only. This is identity: the ids the decode side needs in order to tell whether a
// hardware decoder would hand its frames to the GPU that is going to draw them, or to a
// different one — which costs a bus crossing per frame and rules out zero-copy entirely.

#include <QList>
#include <QPair>
#include <QString>
#include <QtGlobal>

namespace drift::gpu {

// PCI vendor:device, the only GPU identity both platforms can produce — DXGI reports it in
// DXGI_ADAPTER_DESC1, Linux exposes it under /sys/class/drm. An invalid id means "not known",
// never "no GPU": callers must treat it as an open question rather than as a mismatch.
struct PciId
{
    quint16 vendor = 0;
    quint16 device = 0;

    bool isValid() const { return vendor != 0; }
};

inline bool operator==(PciId a, PciId b)
{
    return a.vendor == b.vendor && a.device == b.device;
}
inline bool operator!=(PciId a, PciId b)
{
    return !(a == b);
}

// One GPU. Fields that do not apply to a platform stay empty or -1.
struct Adapter
{
    // DXGI enumeration index. FFmpeg's d3d11va device string is exactly this number. -1 off
    // Windows.
    int index = -1;
    QString slot;   // "0000:01:00.0" on Linux, "dxgi:1" on Windows
    QString name;   // "NVIDIA GeForce RTX 3070"
    QString vendor; // "NVIDIA"
    QString driver; // kernel module, Linux only: "nvidia", "i915", "amdgpu"
    quint16 vendorId = 0;
    quint16 deviceId = 0;

    PciId pci() const { return {vendorId, deviceId}; }
};

// Every GPU this machine has: the DRM class and the PCI display class on Linux, DXGI on
// Windows. Cached for the process — the set of GPUs does not change under a running app in
// any way the decoders could follow anyway.
QList<Adapter> enumerateAdapters();

// "NVIDIA", "Intel", "AMD"... for a PCI vendor id. Empty when the id is not one this knows.
QString pciVendorName(quint16 vendorId);

// The PCI vendor id a GL_VENDOR string names, 0 when it names none this recognises.
quint16 vendorIdForGlVendor(const QString &glVendor);

// Records the DRM device node the current OpenGL context renders through, so the question can
// be answered later from any thread. Call with the context current; a no-op off Linux.
//
// Only EGL can answer it. Qt on GLX has no device to ask about, which leaves the node empty
// and makes renderPciId() fall back to matching GL_VENDOR against the adapter list.
void probeRenderDrmNode();
QString renderDrmNode();

// PCI id behind a DRM node, card or render ("/dev/dri/renderD129"). Linux only; a bare node
// name works too. Invalid when the node does not exist.
PciId pciIdForDrmNode(const QString &node);

// Render nodes and the PCI id each one drives, in /sys/class/drm order. Linux only.
QList<QPair<QString, PciId>> drmRenderNodes();

// The GPU OpenGL renders on. Exact when probeRenderDrmNode() found a node; otherwise the sole
// adapter whose vendor matches GL_VENDOR, which is the answer on every hybrid laptop since
// the two GPUs there are from different vendors. Invalid when neither narrows it to one.
//
// `glVendor` empty asks hwaccel::renderVendor() for the string; pass one to ask about a
// renderer other than the live one.
PciId renderPciId(const QString &glVendor = {});

// Display name for a PCI id, from enumerateAdapters(). Empty when nothing matches.
QString adapterName(PciId id);

// Points the sysfs walk at a fixture tree and drops the adapter cache. Tests only; an empty
// root restores "/sys".
void setSysfsRootForTesting(const QString &root);

} // namespace drift::gpu
