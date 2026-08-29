// Reports whether Qt's multimedia backend plugin loads, and what audio outputs it finds.
//
// Every QAudioSink in Drift gets its devices from that plugin. It is also the one part of the
// Windows package that the FFmpeg DLLs we copy over windeployqt's can break: the app links FFmpeg
// directly for decoding, so video keeps working while audio goes silent with nothing logged. Run
// this from inside the staged tree, pointed at the plugin, to catch that before the installer is
// built.
//
// Usage: audiobackendcheck [path/to/mediaplugin]
// Exit code 1 means the plugin was named and would not load. An empty device list is reported but
// not fatal: CI runners have no audio endpoint.

#include <QAudioDevice>
#include <QCoreApplication>
#include <QFileInfo>
#include <QMediaDevices>
#include <QPluginLoader>
#include <QTextStream>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);

    const QString backend = qEnvironmentVariable("QT_MEDIA_BACKEND");
    out << "backend: " << (backend.isEmpty() ? QStringLiteral("(default)") : backend) << "\n";

    int status = 0;
    if (argc > 1) {
        // QPluginLoader treats a relative name as a search under libraryPaths(), not
        // as a path from the working directory. Resolve first so `multimedia\foo.dll`
        // from inside the staged tree (and CI's dist\bin\multimedia\...) actually load.
        const QString path = QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath();
        QPluginLoader loader(path);
        if (loader.load()) {
            out << "plugin: loaded " << path << "\n";
            loader.unload();
        } else {
            out << "plugin: FAILED " << path << " — " << loader.errorString() << "\n";
            status = 1;
        }
    }

    const QList<QAudioDevice> outputs = QMediaDevices::audioOutputs();
    out << "audioOutputs: " << outputs.size() << "\n";
    for (const QAudioDevice &device : outputs) {
        out << "  " << device.description() << (device.isDefault() ? " (default)" : "")
            << " " << device.preferredFormat().sampleRate() << " Hz"
            << " " << device.preferredFormat().channelCount() << " ch\n";
    }

    out.flush();
    return status;
}
