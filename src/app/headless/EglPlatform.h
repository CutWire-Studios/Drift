#pragma once

#include <QList>
#include <QString>
#include <QStringList>

namespace drift::headless {

struct EglDevice
{
    int index = 0;
    QString renderer;
    QString drmNode;
    bool software = false;
};

// What the native EGL platform ended up on; empty before the platform plugin has been created.
QString activeEglDescription();

// Every EGL device the driver offers, probed by creating a 3.3 core context on each. Needs no
// Qt application, so `--list-gpus` can run before one exists.
QList<EglDevice> listEglDevices();

// Sets QT_QPA_PLATFORM so QApplication comes up on the native EGL platform (no display server),
// with the stock platforms behind it as fallbacks. Does nothing when the user already chose one
// through the environment or -platform. `gpu` is an index or a renderer/node substring.
void selectNativeEglPlatform(int argc, char *argv[], const QString &gpu);

} // namespace drift::headless
