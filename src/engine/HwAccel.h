#pragma once

#include <QList>
#include <QString>

extern "C" {
#include <libavcodec/codec_id.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixfmt.h>
struct AVCodec;
}

// Hardware video device selection, shared by the preview decoder, the exporter and the
// debug report so all three agree on what this machine can do and probe it only once.
namespace drift::hwaccel {

// Decode-side device backends. Exporter's HwBackend is a different axis — it names an
// encoder vendor family, and maps AMF onto a D3D11VA device.
enum class Backend { None, Cuda, D3d11va, Vaapi, VideoToolbox };

// Backends to try for decode on this platform, best first. CUDA leads where it exists
// because it is the only one of the three with both a fast readback and a scaler.
QList<Backend> decodeBackendOrder();

// The GPU Qt renders on, as its GL_VENDOR string. Seeded at startup from the same context
// probe that checks the driver's OpenGL version, and refreshed when the compositor comes up.
// decodeBackendOrder() uses it to keep decode on the device that will draw the frames; without
// it the order would depend on whether GL happened to be up when the first clip opened, and a
// reader latches its backend for good.
void setRenderVendor(const QString &vendor);

// Those of decodeBackendOrder() whose device actually opens here, same order. This is
// what the preview's decode picker offers, so a listed choice is one that works.
QList<Backend> availableDecodeBackends();

AVHWDeviceType deviceType(Backend backend);

// Display name, in decode terms — the user is picking a decoder, not a device, so CUDA
// shows as NVDEC. Not translated: these are product names.
const char *name(Backend backend);

// Stable id for settings and QML ("nvdec", "vaapi", ...). backendFromId() resolves an
// unknown or absent id to None, which is what a config copied off another machine hits.
QString id(Backend backend);
Backend backendFromId(const QString &id);

// libavfilter scaler that runs on this backend's surfaces, or nullptr when there is none
// (D3D11VA, as of FFmpeg 7.1) — callers then read back at full size.
const char *scaleFilter(Backend backend);

// Cached: creating a device context is expensive, and the answer cannot change while the
// process runs.
bool deviceAvailable(AVHWDeviceType type);

// DRIFT_NO_HWACCEL is the escape hatch for a driver that decodes garbage or crashes.
bool disabledByEnv();

// The first decoder for codecId that can drive `type`. avcodec_find_decoder() returns the
// preferred software decoder, which for AV1 is libdav1d — it has no hardware config at
// all, so looking at that codec alone would skip hardware the native `av1` decoder drives.
const AVCodec *findDecoder(AVCodecID codecId, AVHWDeviceType type, AVPixelFormat *pixFmt);

} // namespace drift::hwaccel
