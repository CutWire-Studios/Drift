#include "engine/capture/CaptureDevices.h"

#include <QAudioDevice>
#include <QCameraDevice>
#include <QMediaDevices>
#include <QVariantMap>

namespace drift {

namespace {

// A device that hears the machine's own output rather than a microphone, recognised by the name
// the sound server gives it. Matching on the name avoids a platform #ifdef, and is all that can be
// done: nothing in the device API says "this is a loopback".
bool looksLikeLoopback(const QAudioDevice &device)
{
    const QString description = device.description().toLower();
    const QString id = QString::fromUtf8(device.id()).toLower();
    for (const QLatin1String token : {QLatin1String("monitor"), QLatin1String("loopback")}) {
        if (description.contains(token) || id.contains(token))
            return true;
    }
    return false;
}

} // namespace

QVariantList CaptureDevices::cameras()
{
    QVariantList out;
    const QCameraDevice fallback = QMediaDevices::defaultVideoInput();
    const QList<QCameraDevice> devices = QMediaDevices::videoInputs();
    for (const QCameraDevice &device : devices) {
        out.append(QVariantMap{{QStringLiteral("id"), QString::fromUtf8(device.id())},
                               {QStringLiteral("description"), device.description()},
                               {QStringLiteral("isDefault"), device == fallback}});
    }
    return out;
}

QVariantList CaptureDevices::audioInputs()
{
    QVariantList out;
    const QAudioDevice fallback = QMediaDevices::defaultAudioInput();
    const QList<QAudioDevice> devices = QMediaDevices::audioInputs();
    for (const QAudioDevice &device : devices) {
        out.append(QVariantMap{{QStringLiteral("id"), QString::fromUtf8(device.id())},
                               {QStringLiteral("description"), device.description()},
                               {QStringLiteral("isDefault"), device == fallback},
                               {QStringLiteral("loopback"), looksLikeLoopback(device)}});
    }
    return out;
}

QString CaptureDevices::firstLoopbackInput()
{
    const QList<QAudioDevice> devices = QMediaDevices::audioInputs();
    for (const QAudioDevice &device : devices) {
        if (looksLikeLoopback(device))
            return QString::fromUtf8(device.id());
    }
    return {};
}

} // namespace drift
