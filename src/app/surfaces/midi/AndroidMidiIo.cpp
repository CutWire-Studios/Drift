#include "app/surfaces/midi/MidiIo.h"

#include <QJniEnvironment>
#include <QJniObject>
#include <QMetaObject>
#include <QPointer>
#include <QtCore/qcoreapplication_platform.h>

namespace drift {

namespace {

constexpr const char *kBridge = "org/cutwire/drift/MidiBridge";

class AndroidMidiIo;
QPointer<AndroidMidiIo> g_io;

QJniObject appContext()
{
    return QNativeInterface::QAndroidApplication::context();
}

class AndroidMidiIo final : public MidiIo {
public:
    explicit AndroidMidiIo(QObject *parent) : MidiIo(parent) { g_io = this; }

    QStringList inputPorts() override
    {
        const QJniObject names = QJniObject::callStaticObjectMethod(
            kBridge, "ports", "(Landroid/content/Context;)Ljava/lang/String;", appContext().object());
        const QString joined = names.isValid() ? names.toString() : QString();
        return joined.isEmpty() ? QStringList() : joined.split(QLatin1Char('\n'));
    }

    bool open(const QString &name) override
    {
        return QJniObject::callStaticMethod<jboolean>(kBridge, "open",
                                                      "(Landroid/content/Context;Ljava/lang/String;)Z",
                                                      appContext().object(),
                                                      QJniObject::fromString(name).object<jstring>());
    }

    void close(const QString &name) override
    {
        QJniObject::callStaticMethod<void>(kBridge, "close", "(Ljava/lang/String;)V",
                                           QJniObject::fromString(name).object<jstring>());
    }

    void send(const QString &name, const QByteArray &bytes) override
    {
        QJniEnvironment env;
        jbyteArray data = env->NewByteArray(static_cast<jsize>(bytes.size()));
        env->SetByteArrayRegion(data, 0, static_cast<jsize>(bytes.size()),
                                reinterpret_cast<const jbyte *>(bytes.constData()));
        QJniObject::callStaticMethod<void>(kBridge, "send", "(Ljava/lang/String;[B)V",
                                           QJniObject::fromString(name).object<jstring>(), data);
        env->DeleteLocalRef(data);
    }

    void deliver(const QString &name, const QByteArray &bytes) { emit received(name, bytes); }
};

// Called on a binder thread.
void JNICALL nativeReceived(JNIEnv *env, jclass, jstring name, jbyteArray data)
{
    if (!g_io)
        return;
    const QString port = QJniObject(name).toString();
    const jsize size = env->GetArrayLength(data);
    QByteArray bytes(size, Qt::Uninitialized);
    env->GetByteArrayRegion(data, 0, size, reinterpret_cast<jbyte *>(bytes.data()));
    QMetaObject::invokeMethod(g_io.data(), [port, bytes] {
        if (g_io)
            static_cast<AndroidMidiIo *>(g_io.data())->deliver(port, bytes);
    }, Qt::QueuedConnection);
}

} // namespace

MidiIo *createPlatformMidiIo(QObject *parent)
{
    const JNINativeMethod methods[] = {
        {"nativeReceived", "(Ljava/lang/String;[B)V", reinterpret_cast<void *>(nativeReceived)},
    };
    if (!QJniEnvironment().registerNativeMethods(kBridge, methods, 1))
        return nullptr;
    return new AndroidMidiIo(parent);
}

} // namespace drift
