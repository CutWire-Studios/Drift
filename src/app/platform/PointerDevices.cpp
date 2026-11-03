#include "app/platform/PointerDevices.h"

#ifdef Q_OS_ANDROID
#include <QJniEnvironment>
#include <QJniObject>
#include <QMetaObject>
#include <QPointer>
#include <QtCore/qcoreapplication_platform.h>
#endif

namespace {

#ifdef Q_OS_ANDROID

constexpr const char *kPointerDevicesClass = "org/cutwire/drift/PointerDevices";

QPointer<drift::PointerDevices> g_instance;

// Called on the Android main looper, not the Qt thread.
void JNICALL pointerDevicesChanged(JNIEnv *, jclass, jboolean present)
{
    if (!g_instance)
        return;
    QMetaObject::invokeMethod(g_instance.data(), "setMousePresent", Qt::QueuedConnection,
                              Q_ARG(bool, present));
}

#endif

} // namespace

namespace drift {

PointerDevices::PointerDevices(QObject *parent)
    : QObject(parent)
{
#ifdef Q_OS_ANDROID
    g_instance = this;
    QJniObject context = QNativeInterface::QAndroidApplication::context();
    if (!context.isValid())
        return;
    const JNINativeMethod methods[] = {
        {"nativeMouseChanged", "(Z)V", reinterpret_cast<void *>(pointerDevicesChanged)},
    };
    QJniEnvironment env;
    if (!env.registerNativeMethods(kPointerDevicesClass, methods, 1))
        return;
    m_mousePresent = QJniObject::callStaticMethod<jboolean>(
        kPointerDevicesClass, "hasMouse", "(Landroid/content/Context;)Z", context.object());
    // listen() builds its Handler on the main looper, so it is safe to call from this thread.
    QJniObject::callStaticMethod<void>(kPointerDevicesClass, "listen", "(Landroid/content/Context;)V",
                                       context.object());
    env.checkAndClearExceptions();
#endif
}

void PointerDevices::setMousePresent(bool present)
{
    if (m_mousePresent == present)
        return;
    m_mousePresent = present;
    emit mousePresentChanged();
}

} // namespace drift

#include "moc_PointerDevices.cpp"
