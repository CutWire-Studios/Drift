// Renders an animated preview strip for every transition package: N frames sampled across
// p = 0..1, packed left-to-right into one PNG. The browser shows a single cell and scrubs
// through the rest on hover, which a still frame at p = 0.5 cannot convey (a crossfade and a
// dip look identical there).
//
// Frames go through the real GpuEffectExecutor path, so a wrong Y flip or a swapped from/to
// shows up immediately across the sheet.

#include "engine/gpu/effects/GpuEffectExecutor.h"
#include "engine/packages/PackageThumbnails.h"
#include "engine/packages/transitions/TransitionCatalog.h"

#include <QDir>
#include <QGuiApplication>
#include <QImage>
#include <QTextStream>

#include <algorithm>
#include <limits>
#include <vector>

namespace {

// Preview strips are the bulk of the transitions addon -- 149 packages of full-colour PNG came to
// 34 MB, against 1.2 MB for every shader in it. They are 128px cells viewed as a hover scrub, so a
// 128-entry palette is indistinguishable in use and roughly a third of the bytes.
//
// Qt has no median-cut quantiser, and its built-in Indexed8 conversion falls back to a fixed
// colour cube that bands badly on photographic content. This is a straight population-sorted
// palette over a 5-5-5 histogram plus nearest-entry mapping with Floyd-Steinberg error diffusion,
// which is enough for thumbnails and keeps the tool dependency-free.
// The strip is allocated RGBA8888, so hasAlphaChannel() is always true and says nothing about
// whether any pixel actually uses it. Both bases are opaque and fill the frame, so in practice
// nothing is transparent -- but a transition drawing outside both clips could be, and that must
// not be flattened into a palette.
bool isFullyOpaque(const QImage &image)
{
    const QImage argb = image.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < argb.height(); ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(argb.constScanLine(y));
        for (int x = 0; x < argb.width(); ++x) {
            if (qAlpha(line[x]) != 255)
                return false;
        }
    }
    return true;
}

QImage quantize(const QImage &source, int maxColors)
{
    const QImage rgb = source.convertToFormat(QImage::Format_RGB32);
    const int w = rgb.width();
    const int h = rgb.height();

    // 5 bits per channel: fine enough that distinct picture colours stay distinct, coarse enough
    // that the histogram is small and the popular entries dominate.
    std::vector<int> histogram(32 * 32 * 32, 0);
    for (int y = 0; y < h; ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(rgb.constScanLine(y));
        for (int x = 0; x < w; ++x) {
            const QRgb c = line[x];
            histogram[((qRed(c) >> 3) << 10) | ((qGreen(c) >> 3) << 5) | (qBlue(c) >> 3)] += 1;
        }
    }

    std::vector<int> cells;
    cells.reserve(histogram.size());
    for (size_t i = 0; i < histogram.size(); ++i) {
        if (histogram[i] > 0)
            cells.push_back(int(i));
    }
    std::sort(cells.begin(), cells.end(),
              [&histogram](int a, int b) { return histogram[a] > histogram[b]; });
    if (int(cells.size()) > maxColors)
        cells.resize(maxColors);

    QVector<QRgb> palette;
    palette.reserve(cells.size());
    for (int cell : cells) {
        // +4 recentres the sample in its 8-wide bucket rather than pinning it to the low edge.
        const int r = qMin(255, ((cell >> 10) & 31) * 8 + 4);
        const int g = qMin(255, ((cell >> 5) & 31) * 8 + 4);
        const int b = qMin(255, (cell & 31) * 8 + 4);
        palette.append(qRgb(r, g, b));
    }
    if (palette.isEmpty())
        palette.append(qRgb(0, 0, 0));

    QImage indexed(w, h, QImage::Format_Indexed8);
    indexed.setColorTable(palette);

    // Error diffusion, carried on a two-row float buffer so the strip does not band across the
    // large flat gradients these bases are full of.
    const int stride = w + 2;
    std::vector<float> curr(stride * 3, 0.f);
    std::vector<float> next(stride * 3, 0.f);
    for (int y = 0; y < h; ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(rgb.constScanLine(y));
        uchar *dest = indexed.scanLine(y);
        std::fill(next.begin(), next.end(), 0.f);
        for (int x = 0; x < w; ++x) {
            const QRgb c = line[x];
            const int at = (x + 1) * 3;
            const float wantR = qBound(0.f, float(qRed(c)) + curr[at], 255.f);
            const float wantG = qBound(0.f, float(qGreen(c)) + curr[at + 1], 255.f);
            const float wantB = qBound(0.f, float(qBlue(c)) + curr[at + 2], 255.f);

            int best = 0;
            float bestDist = std::numeric_limits<float>::max();
            for (int i = 0; i < palette.size(); ++i) {
                const QRgb p = palette.at(i);
                const float dr = wantR - float(qRed(p));
                const float dg = wantG - float(qGreen(p));
                const float db = wantB - float(qBlue(p));
                const float dist = dr * dr + dg * dg + db * db;
                if (dist < bestDist) {
                    bestDist = dist;
                    best = i;
                    if (dist == 0.f)
                        break;
                }
            }
            dest[x] = uchar(best);

            const QRgb chosen = palette.at(best);
            const float errR = wantR - float(qRed(chosen));
            const float errG = wantG - float(qGreen(chosen));
            const float errB = wantB - float(qBlue(chosen));
            const float err[3] = {errR, errG, errB};
            for (int k = 0; k < 3; ++k) {
                curr[at + 3 + k] += err[k] * (7.f / 16.f);
                next[at - 3 + k] += err[k] * (3.f / 16.f);
                next[at + k] += err[k] * (5.f / 16.f);
                next[at + 3 + k] += err[k] * (1.f / 16.f);
            }
        }
        curr.swap(next);
    }
    return indexed;
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
    bool paletted = true;
    int colors = 128;

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
        else if (a == QLatin1String("--colors") && i + 1 < args.size())
            colors = qBound(2, args.at(++i).toInt(), 256);
        else if (a == QLatin1String("--full-color"))
            paletted = false;
        else if (a == QLatin1String("--help") || a == QLatin1String("-h")) {
            err << "usage: transitionthumbs [--transitions DIR] [--base-a img] [--base-b img] "
                   "[--only id] [--size N] [--frames N] [--colors N] [--full-color]\n";
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

    const auto loadBase = [&](const QString &path) {
        QImage image;
        if (!path.isEmpty())
            image = QImage(path).convertToFormat(QImage::Format_RGBA8888);
        if (image.isNull())
            return QImage();
        return image.scaled(size, size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation)
            .copy(0, 0, size, size);
    };
    const QImage baseA = loadBase(baseAPath);
    const QImage baseB = loadBase(baseBPath);

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

        const QImage strip = renderTransitionStrip(
            QLatin1String(kTransitionCacheKeyPrefix) + def.meta.id, def.gpu, params, {size, size},
            frames, baseA, baseB);
        if (strip.isNull()) {
            err << "FAIL render " << def.meta.id << "\n";
            ++failed;
            continue;
        }

        const QString outPath =
            QDir(def.gpu.packageDir).filePath(QStringLiteral("preview_strip.png"));
        // A transition rendered over a partly transparent clip can legitimately produce alpha;
        // an indexed palette would lose it, so only quantise when there is none to lose.
        const QImage encoded = paletted && isFullyOpaque(strip) ? quantize(strip, colors) : strip;
        if (!encoded.save(outPath, "PNG")) {
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
