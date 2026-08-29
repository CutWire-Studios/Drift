#include "engine/EffectCatalog.h"
#include "engine/EffectProcessor.h"
#include "engine/EffectTemplateCatalog.h"
#include "engine/GpuEffectExecutor.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QTextStream>

namespace {

QList<drift::Effect> effectsFromLayers(const QList<EffectTemplateLayer> &layers)
{
    QList<drift::Effect> out;
    out.reserve(layers.size());
    for (const EffectTemplateLayer &layer : layers) {
        drift::Effect effect;
        effect.catalogId = layer.effectId;
        effect.parameters = layer.params;
        // Showcase the beat-peak look rather than the rest pose.
        if (layer.pulse.valid)
            effect.parameters.insert(layer.pulse.param, layer.pulse.peak);
        out.append(effect);
    }
    return out;
}

QImage applyLayers(const QImage &input, const QList<EffectTemplateLayer> &layers)
{
    const QList<drift::Effect> effects = effectsFromLayers(layers);
    if (effects.isEmpty())
        return input;
    const QImage result = EffectProcessor::applyEffects(input, effects, 500000);
    return result.isNull() ? input : result;
}

// Studio portraits sit on near-black; treat dark pixels as backdrop so multi-track templates can
// approximate segmentation without running the person-mask model.
QImage subjectMask(const QImage &src)
{
    QImage mask(src.size(), QImage::Format_Alpha8);
    for (int y = 0; y < src.height(); ++y) {
        const auto *line = reinterpret_cast<const QRgb *>(src.constScanLine(y));
        uchar *out = mask.scanLine(y);
        for (int x = 0; x < src.width(); ++x) {
            const QRgb px = line[x];
            const int luma = (qRed(px) * 54 + qGreen(px) * 183 + qBlue(px) * 19) >> 8;
            // Soft edge so hair strands don't hard-cut.
            if (luma < 18)
                out[x] = 0;
            else if (luma > 40)
                out[x] = 255;
            else
                out[x] = uchar(((luma - 18) * 255) / 22);
        }
    }
    return mask;
}

QImage compositeMasked(const QImage &background, const QImage &foreground, const QImage &mask)
{
    QImage out = background.convertToFormat(QImage::Format_RGBA8888);
    const QImage fg = foreground.convertToFormat(QImage::Format_RGBA8888);
    for (int y = 0; y < out.height(); ++y) {
        auto *dst = reinterpret_cast<QRgb *>(out.scanLine(y));
        const auto *src = reinterpret_cast<const QRgb *>(fg.constScanLine(y));
        const uchar *m = mask.constScanLine(y);
        for (int x = 0; x < out.width(); ++x) {
            const int a = m[x];
            if (a == 0)
                continue;
            if (a == 255) {
                dst[x] = src[x];
                continue;
            }
            const QRgb b = dst[x];
            const QRgb f = src[x];
            const int ia = 255 - a;
            dst[x] = qRgba((qRed(f) * a + qRed(b) * ia) / 255,
                           (qGreen(f) * a + qGreen(b) * ia) / 255,
                           (qBlue(f) * a + qBlue(b) * ia) / 255, 255);
        }
    }
    return out;
}

QImage withClones(const QImage &subject, const EffectTemplateCloneSpec &clones,
                  const QList<EffectTemplateLayer> &cloneLayers)
{
    if (clones.count <= 0)
        return subject;

    QImage processed = applyLayers(subject, cloneLayers);
    QImage canvas(subject.size(), QImage::Format_RGBA8888);
    canvas.fill(Qt::transparent);
    QPainter p(&canvas);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);

    for (int i = clones.count - 1; i >= 0; --i) {
        const double opacity =
            i < clones.opacities.size() ? clones.opacities.at(i) : (0.35 / double(i + 1));
        const double scale = i < clones.scales.size() ? clones.scales.at(i) : (1.0 - 0.03 * (i + 1));
        const int w = int(processed.width() * scale);
        const int h = int(processed.height() * scale);
        const int x = (processed.width() - w) / 2 + (i + 1) * 6;
        const int y = (processed.height() - h) / 2;
        p.setOpacity(opacity);
        p.drawImage(QRect(x, y, w, h), processed);
    }

    p.setOpacity(1.0);
    p.drawImage(0, 0, subject);
    p.end();
    return canvas;
}

const EffectTemplateTrack *trackByRole(const EffectTemplateEntry &entry, const QString &role)
{
    for (const EffectTemplateTrack &track : entry.tracks) {
        if (track.role == role)
            return &track;
    }
    return nullptr;
}

QImage renderTemplate(const EffectTemplateEntry &entry, const QImage &base)
{
    if (!entry.usesMultiTrack())
        return applyLayers(base, entry.layers);

    const EffectTemplateTrack *bg = trackByRole(entry, QStringLiteral("background"));
    const EffectTemplateTrack *fg = trackByRole(entry, QStringLiteral("foreground"));
    const EffectTemplateTrack *clone = trackByRole(entry, QStringLiteral("clone"));

    const QImage mask = subjectMask(base);
    QImage background = bg ? applyLayers(base, bg->layers) : base;
    QImage foreground = fg ? applyLayers(base, fg->layers) : base;

    if (entry.clones.count > 0) {
        const QList<EffectTemplateLayer> cloneLayers =
            clone ? clone->layers : QList<EffectTemplateLayer>{};
        // Build subject-only plate, stamp clones, then drop onto processed background.
        QImage subjectPlate(base.size(), QImage::Format_RGBA8888);
        subjectPlate.fill(Qt::transparent);
        {
            QPainter p(&subjectPlate);
            p.drawImage(0, 0, foreground);
            p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
            // Expand alpha mask to RGBA for DestinationIn.
            QImage rgbaMask(base.size(), QImage::Format_RGBA8888);
            for (int y = 0; y < base.height(); ++y) {
                auto *dst = reinterpret_cast<QRgb *>(rgbaMask.scanLine(y));
                const uchar *m = mask.constScanLine(y);
                for (int x = 0; x < base.width(); ++x)
                    dst[x] = qRgba(255, 255, 255, m[x]);
            }
            p.drawImage(0, 0, rgbaMask);
            p.end();
        }
        const QImage withGhosts = withClones(subjectPlate, entry.clones, cloneLayers);
        QImage out = background;
        QPainter p(&out);
        p.drawImage(0, 0, withGhosts);
        p.end();
        return out;
    }

    return compositeMasked(background, foreground, mask);
}

} // namespace

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QTextStream err(stderr);
    QTextStream out(stdout);

    const QStringList args = app.arguments();
    QString effectsRoot =
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("effects"));
    QString templatesRoot =
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("effect-templates"));
    QString basePath;
    QString onlyId;
    int size = 256;
    bool force = false;

    for (int i = 1; i < args.size(); ++i) {
        const QString a = args.at(i);
        if (a == QLatin1String("--effects") && i + 1 < args.size())
            effectsRoot = args.at(++i);
        else if (a == QLatin1String("--templates") && i + 1 < args.size())
            templatesRoot = args.at(++i);
        else if (a == QLatin1String("--base") && i + 1 < args.size())
            basePath = args.at(++i);
        else if (a == QLatin1String("--only") && i + 1 < args.size())
            onlyId = args.at(++i);
        else if (a == QLatin1String("--size") && i + 1 < args.size())
            size = qBound(64, args.at(++i).toInt(), 1024);
        else if (a == QLatin1String("--force"))
            force = true;
        else if (a == QLatin1String("--help") || a == QLatin1String("-h")) {
            err << "usage: templatethumbs [--effects DIR] [--templates DIR] [--base image]\n"
                   "                      [--only id] [--size N] [--force]\n";
            return 0;
        }
    }

    if (!QDir(effectsRoot).exists()) {
        err << "effects dir missing: " << effectsRoot << "\n";
        return 1;
    }
    if (!QDir(templatesRoot).exists()) {
        err << "templates dir missing: " << templatesRoot << "\n";
        return 1;
    }

    reloadEffectCatalog({effectsRoot});
    reloadEffectTemplateCatalog({templatesRoot});

    if (!GpuEffectExecutor::instance().isAvailable()) {
        err << "OpenGL offscreen context unavailable\n";
        return 1;
    }

    QImage base;
    if (!basePath.isEmpty())
        base = QImage(basePath).convertToFormat(QImage::Format_RGBA8888);
    if (base.isNull()) {
        err << "base image required (--base)\n";
        return 1;
    }
    base = base.scaled(size, size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation)
               .copy(0, 0, size, size);

    int ok = 0;
    int failed = 0;
    int skipped = 0;
    for (const EffectTemplateEntry &entry : effectTemplateCatalog()) {
        if (!onlyId.isEmpty() && entry.id != onlyId)
            continue;

        const QString outPath =
            QDir(entry.packageDir).filePath(QStringLiteral("thumbnail.png"));
        if (!force && onlyId.isEmpty() && QFile::exists(outPath)) {
            ++skipped;
            continue;
        }

        QImage result = renderTemplate(entry, base);
        if (result.isNull()) {
            err << "FAIL " << entry.id << "\n";
            ++failed;
            continue;
        }

        result = result.convertToFormat(QImage::Format_RGBA8888)
                     .scaled(size, size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        if (!result.save(outPath, "PNG")) {
            err << "FAIL write " << outPath << "\n";
            ++failed;
            continue;
        }
        out << "wrote " << outPath << "\n";
        ++ok;
    }

    out << "done: " << ok << " ok, " << failed << " failed, " << skipped << " kept\n";
    if (skipped > 0)
        out << "(pass --force to regenerate the ones that already have a thumbnail)\n";
    return failed == 0 ? 0 : 2;
}
