#include "core/Project.h"
#include "engine/FontCatalog.h"
#include "engine/FrameCompositor.h"

#include <QGuiApplication>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

int main(int argc, char *argv[])
{
    // QGuiApplication, not QCoreApplication: the compositor needs a GL surface,
    // which cannot be created without a GUI application instance.
    QGuiApplication app(argc, argv);
    reloadFontCatalog();
    QTextStream err(stderr);

    const QStringList args = app.arguments();
    if (args.size() != 4) {
        err << "usage: renderframe <project.dcut.json> <time_us> <output.png>\n";
        return 1;
    }

    QFile file(args.at(1));
    if (!file.open(QIODevice::ReadOnly)) {
        err << "failed to open project: " << args.at(1) << "\n";
        return 1;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) {
        err << "invalid project json\n";
        return 1;
    }

    QString error;
    drift::Project project = drift::Project::fromJson(doc.object(), &error);
    if (!error.isEmpty()) {
        err << "project load failed: " << error << "\n";
        return 1;
    }

    bool ok = false;
    const drift::TimeUs timeUs = args.at(2).toLongLong(&ok);
    if (!ok || timeUs < 0) {
        err << "invalid time_us: " << args.at(2) << "\n";
        return 1;
    }

    FrameCompositor compositor;
    compositor.setProject(&project);
    const QImage frame = compositor.compositeAt(timeUs);
    if (frame.isNull()) {
        err << "compositor returned empty frame\n";
        return 1;
    }

    if (!frame.save(args.at(3))) {
        err << "failed to write: " << args.at(3) << "\n";
        return 1;
    }

    return 0;
}
