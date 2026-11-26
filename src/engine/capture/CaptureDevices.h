#pragma once

#include <QString>
#include <QVariantList>

namespace drift {

// What this machine can record from. Enumeration only — starting a capture is the job of a
// ScreenCaptureSource.
namespace CaptureDevices {

// [{id, description, isDefault}] for the cameras.
QVariantList cameras();

// [{id, description, isDefault, loopback}] for the audio inputs. `loopback` marks a device that
// hears what the computer is playing rather than a microphone.
//
// In practice no loopback device is listed on a stock Linux, Windows or macOS install: PipeWire
// and PulseAudio do publish a monitor source for every sink, but Qt filters monitors out of its
// input enumeration, and the other two platforms have nothing of the kind without a virtual cable
// the user installs. Recording the machine's own sound properly needs a client that talks to
// PipeWire, WASAPI loopback or ScreenCaptureKit directly.
QVariantList audioInputs();

// The id of the first loopback input, or empty when there is none. Empty is the normal answer —
// see audioInputs().
QString firstLoopbackInput();

} // namespace CaptureDevices

} // namespace drift
