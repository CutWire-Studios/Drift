// Renders an animated preview strip for every transition package: N frames sampled across
// p = 0..1, packed left-to-right into one PNG. The browser shows a single cell and scrubs
// through the rest on hover, which a still frame at p = 0.5 cannot convey (a crossfade and a
// dip look identical there).
//
// Frames go through the real GpuEffectExecutor path, so a wrong Y flip or a swapped from/to
// shows up immediately across the sheet.

#include "engine/GpuEffectExecutor.h"
#include "engine/TransitionCatalog.h"

#include <QColor>
#include <QDir>
#include <QGuiApplication>
#include <QImage>
#include <QLinearGradient>
#include <QPainter>
#include <QTextStream>

namespace {

// Two bases that stay distinguishable at any progress: warm vs cool, and different structure.
QImage makeBaseA(int size)
{
    QImage image(size, size, QImage::Format_RGBA8888);
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing, true);

    QLinearGradient bg(0, 0, size, size);
    bg.setColorAt(0.0, QColor(232, 108, 52));
    bg.setColorAt(1.0, QColor(122, 30, 62));
    p.fillRect(image.rect(), bg);

    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 214, 132));
    p.drawEllipse(QPoint(int(size * 0.34), int(size * 0.34)), int(size * 0.16), int(size * 0.16));

    p.setBrush(QColor(60, 16, 40, 190));
    for (int i = 0; i < 4; ++i) {
        const int y = int(size * (0.60 + i * 0.10));
        p.drawRect(0, y, size, int(size * 0.045));
    }
    p.end();
    return image;
}

QImage makeBaseB(int size)
{
    QImage image(size, size, QImage::Format_RGBA8888);
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing, true);

    QLinearGradient bg(0, size, size, 0);
    bg.setColorAt(0.0, QColor(24, 44, 96));
    bg.setColorAt(1.0, QColor(58, 186, 176));
    p.fillRect(image.rect(), bg);

    // A checker reads clearly through warps, wipes and pixelation.
    p.setBrush(QColor(232, 244, 255, 60));
    p.setPen(Qt::NoPen);
    const int cells = 8;
    const double step = double(size) / cells;
    for (int y = 0; y < cells; ++y) {
        for (int x = 0; x < cells; ++x) {
            if ((x + y) % 2 == 0)
                p.drawRect(QRectF(x * step, y * step, step, step));
        }
    }

    p.setBrush(QColor(255, 255, 255, 220));
    p.drawEllipse(QPoint(int(size * 0.66), int(size * 0.62)), int(size * 0.14), int(size * 0.14));
    p.end();
    return image;
}

} // namespace

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QTextStream err(stderr);
    QTextStream out(stdout);

    const QStringList args = app.arguments();
    QString root =
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("transitions"));
    QString baseAPath;
    QString baseBPath;
    QString onlyId;
    int size = 128;
    int frames = 12;

    for (int i = 1; i < args.size(); ++i) {
        const QString a = args.at(i);
        if (a == QLatin1String("--transitions") && i + 1 < args.size())
            root = args.at(++i);
        else if (a == QLatin1String("--base-a") && i + 1 < args.size())
            baseAPath = args.at(++i);
        else if (a == QLatin1String("--base-b") && i + 1 < args.size())
            baseBPath = args.at(++i);
        else if (a == QLatin1String("--only") && i + 1 < args.size())
            onlyId = args.at(++i);
        else if (a == QLatin1String("--size") && i + 1 < args.size())
            size = qBound(32, args.at(++i).toInt(), 512);
        else if (a == QLatin1String("--frames") && i + 1 < args.size())
            frames = qBound(2, args.at(++i).toInt(), 48);
        else if (a == QLatin1String("--help") || a == QLatin1String("-h")) {
            err << "usage: transitionthumbs [--transitions DIR] [--base-a img] [--base-b img] "
                   "[--only id] [--size N] [--frames N]\n";
            return 0;
        }
    }

    if (!QDir(root).exists()) {
        err << "transitions dir missing: " << root << "\n";
        return 1;
    }

    reloadTransitionCatalog({root});
    if (!GpuEffectExecutor::instance().isAvailable()) {
        err << "OpenGL offscreen context unavailable\n";
        return 1;
    }

    auto loadBase = [&](const QString &path, QImage (*fallback)(int)) {
        QImage image;
        if (!path.isEmpty())
            image = QImage(path).convertToFormat(QImage::Format_RGBA8888);
        if (image.isNull())
            image = fallback(size * 2);
        return image.scaled(size, size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation)
            .copy(0, 0, size, size);
    };
    const QImage baseA = loadBase(baseAPath, makeBaseA);
    const QImage baseB = loadBase(baseBPath, makeBaseB);

    int ok = 0;
    int failed = 0;
    for (const TransitionPresetEntry &def : transitionCatalog()) {
        if (!onlyId.isEmpty() && def.meta.id != onlyId)
            continue;
        if (!def.gpu.valid) {
            err << "skip invalid " << def.meta.id << "\n";
            ++failed;
            continue;
        }

        drift::Transition instance;
        instance.kindId = def.meta.id;
        const QMap<QString, QVariant> params = resolvedTransitionParameters(instance, def);

        QImage strip(size * frames, size, QImage::Format_RGBA8888);
        strip.fill(Qt::transparent);
        QPainter painter(&strip);

        bool allOk = true;
        for (int i = 0; i < frames; ++i) {
            const double p = double(i) / double(frames - 1);
            bool passOk = false;
            const QImage cell = GpuEffectExecutor::instance().apply(
                QLatin1String(kTransitionCacheKeyPrefix) + def.meta.id, def.gpu, {baseA, baseB},
                params, 0, p, &passOk);
            if (!passOk || cell.isNull()) {
                allOk = false;
                break;
            }
            painter.drawImage(QPoint(i * size, 0), cell);
        }
        painter.end();

        if (!allOk) {
            err << "FAIL render " << def.meta.id << "\n";
            ++failed;
            continue;
        }

        const QString outPath =
            QDir(def.gpu.packageDir).filePath(QStringLiteral("preview_strip.png"));
        if (!strip.save(outPath, "PNG")) {
            err << "FAIL write " << outPath << "\n";
            ++failed;
            continue;
        }
        out << "wrote " << outPath << " (" << frames << " frames)\n";
        ++ok;
    }

    out << "done: " << ok << " ok, " << failed << " failed\n";
    return failed == 0 ? 0 : 2;
}
