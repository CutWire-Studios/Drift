#include "app/crash/CrashReason.h"

#include <QCoreApplication>
#include <QRegularExpression>

namespace drift::crash {

namespace {

QString tr(const char *source)
{
    return QCoreApplication::translate("CrashReason", source);
}

QString normalized(const QString &path)
{
    QString out = path.toLower();
    out.replace(QLatin1Char('\\'), QLatin1Char('/'));
    return out;
}

bool isCrt(const QString &name)
{
    return name.startsWith(QLatin1String("msvcp140")) || name.startsWith(QLatin1String("vcruntime140"))
           || name.startsWith(QLatin1String("ucrtbase"));
}

bool isGpuDriver(const QString &name)
{
    static const QRegularExpression pattern(QStringLiteral(
        "^(nvoglv64|nvwgf2umx|atio6axx|atiumd.*|amdxx64|ig.*icd64|igd10.*|igc64"
        // Linux: NVIDIA proprietary, Mesa drivers and the Vulkan ICDs.
        "|libnvidia-.*|libglx_nvidia.*|libegl_nvidia.*|libgallium.*|libvulkan_.*|libdrm_.*|libglx_mesa.*"
        "|(nvidia|radeonsi|r600|iris|crocus|i965|swrast|kms_swrast|zink|nouveau|vmwgfx|virtio_gpu|d3d12)_dri"
        // macOS: Metal drivers and the OpenGL layer.
        "|agx.*|applemetal.*|amdmtl.*|amdradeonx.*|appleintel.*|glengine|libglimage|libgpusupportmercury"
        ")(\\.dll|\\.so.*|\\.dylib)?$"));
    return pattern.match(name).hasMatch();
}

} // namespace

Reason mapReason(const QString &code, const QString &module, const QString &modulePath,
                 const QString &addonsDir)
{
    Reason reason;
    const QString name = module.toLower();

    // First: an allocation failure faults inside the CRT, which would otherwise read as a stale runtime.
    if (code.compare(QLatin1String("0xC0000017"), Qt::CaseInsensitive) == 0) {
        reason.key = QStringLiteral("out-of-memory");
        reason.text = tr("Drift ran out of memory. Close other programs, or work with smaller or "
                         "lower-resolution media.");
        return reason;
    }

    if (isCrt(name)) {
        reason.key = QStringLiteral("crt-outdated");
        reason.text = tr("A Windows component Drift depends on looks out of date. Update the "
                         "Microsoft Visual C++ Redistributable.");
        reason.linkUrl = QStringLiteral("https://aka.ms/vs/17/release/vc_redist.x64.exe");
        reason.linkLabel = tr("Download the Visual C++ Redistributable");
        return reason;
    }

    if (isGpuDriver(name)) {
        reason.key = QStringLiteral("gpu-driver");
        reason.text = tr("The graphics driver stopped responding. Update your graphics driver, or "
                         "try another graphics backend in Settings.");
        return reason;
    }

    const QString addons = normalized(addonsDir);
    const QString path = normalized(modulePath);
    const bool inAddons = !addons.isEmpty() && path.startsWith(addons + QLatin1Char('/'));
    if (inAddons || name.startsWith(QLatin1String("onnxruntime"))
                         || name.startsWith(QLatin1String("libonnxruntime"))) {
        reason.key = QStringLiteral("addon");
        if (inAddons) {
            // Cut from the original-case path so the id reads the way the folder is named.
            QString original = modulePath;
            original.replace(QLatin1Char('\\'), QLatin1Char('/'));
            reason.addonId = original.mid(addons.size() + 1).section(QLatin1Char('/'), 0, 0);
        }
        reason.text = reason.addonId.isEmpty()
                          ? tr("An add-on crashed. Try disabling or reinstalling your add-ons.")
                          : tr("The add-on \"%1\" crashed. Try updating or reinstalling it.")
                                .arg(reason.addonId);
        return reason;
    }

    reason.key = QStringLiteral("unknown");
    reason.text = tr("Drift closed unexpectedly.");
    return reason;
}

} // namespace drift::crash
