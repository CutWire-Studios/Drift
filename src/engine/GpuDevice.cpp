#include "GpuDevice.h"

#include "GpuPreference.h"
#include "HwAccel.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QSet>

#if defined(Q_OS_LINUX)
#include <dlfcn.h>
#include <cstdint>
#endif

namespace drift::gpu {
namespace {

QMutex g_mutex;
QString g_sysfsRoot;      // empty means "/sys"
QString g_renderDrmNode;  // set by probeRenderDrmNode()
QList<Adapter> g_adapters;
bool g_adaptersCached = false;

QString sysfsRoot()
{
    return g_sysfsRoot.isEmpty() ? QStringLiteral("/sys") : g_sysfsRoot;
}

QString readTrimmedFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromUtf8(file.readAll().trimmed());
}

quint32 readSysfsHex(const QString &path)
{
    QString text = readTrimmedFile(path);
    if (text.startsWith(QLatin1String("0x"), Qt::CaseInsensitive))
        text = text.mid(2);
    bool ok = false;
    const quint32 value = text.toUInt(&ok, 16);
    return ok ? value : 0;
}

QString driverNameAt(const QString &deviceDir)
{
    const QFileInfo link(deviceDir + QStringLiteral("/driver"));
    if (!link.exists())
        return {};
    return QFileInfo(link.symLinkTarget()).fileName();
}

QString nvidiaModelForSlot(const QString &slot)
{
    QFile file(QStringLiteral("/proc/driver/nvidia/gpus/%1/information").arg(slot));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    while (!file.atEnd()) {
        const QByteArray line = file.readLine();
        if (!line.startsWith("Model:"))
            continue;
        const int colon = line.indexOf(':');
        if (colon < 0)
            continue;
        return QString::fromUtf8(line.mid(colon + 1).trimmed());
    }
    return {};
}

Adapter adapterFromSysfsDevice(const QString &deviceDir, const QString &slot)
{
    Adapter gpu;
    gpu.slot = slot;
    gpu.vendorId = static_cast<quint16>(readSysfsHex(deviceDir + QStringLiteral("/vendor")));
    gpu.deviceId = static_cast<quint16>(readSysfsHex(deviceDir + QStringLiteral("/device")));
    gpu.vendor = pciVendorName(gpu.vendorId);
    if (gpu.vendor.isEmpty() && gpu.vendorId)
        gpu.vendor = QStringLiteral("PCI %1").arg(gpu.vendorId, 4, 16, QLatin1Char('0')).toUpper();
    gpu.driver = driverNameAt(deviceDir);
    if (const QString label = readTrimmedFile(deviceDir + QStringLiteral("/label")); !label.isEmpty())
        gpu.name = label;
    else if (const QString product = readTrimmedFile(deviceDir + QStringLiteral("/product_name"));
             !product.isEmpty())
        gpu.name = product;
    if (gpu.name.isEmpty() && !slot.isEmpty())
        gpu.name = nvidiaModelForSlot(slot);
    return gpu;
}

// The sysfs directory a DRM node's PCI device lives in. Accepts "/dev/dri/renderD129",
// "renderD129" or "card0" — only the last path component matters.
QString drmDeviceDir(const QString &node)
{
    const QString name = QFileInfo(node).fileName();
    if (name.isEmpty())
        return {};
    return QStringLiteral("%1/class/drm/%2/device").arg(sysfsRoot(), name);
}

QList<Adapter> enumerateLocked()
{
    QList<Adapter> gpus;
    QSet<QString> seen;

    const auto addGpu = [&](Adapter gpu) {
        const QString key = !gpu.slot.isEmpty()
            ? gpu.slot
            : QStringLiteral("%1:%2").arg(gpu.vendorId).arg(gpu.deviceId);
        if (gpu.slot.isEmpty() && gpu.vendorId == 0 && gpu.deviceId == 0)
            return;
        if (seen.contains(key))
            return;
        if (gpu.vendorId == 0 && gpu.driver.isEmpty())
            return;
        seen.insert(key);
        gpus.append(std::move(gpu));
    };

#if defined(Q_OS_LINUX)
    const QDir drmDir(QStringLiteral("%1/class/drm").arg(sysfsRoot()));
    static const QRegularExpression cardRe(QStringLiteral("^card\\d+$"));
    const QFileInfoList cards = drmDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo &card : cards) {
        if (!cardRe.match(card.fileName()).hasMatch())
            continue;
        const QString deviceDir = card.absoluteFilePath() + QStringLiteral("/device");
        QString slot = QFileInfo(QFileInfo(deviceDir).canonicalFilePath()).fileName();
        if (!slot.contains(QLatin1Char(':')))
            slot.clear();
        addGpu(adapterFromSysfsDevice(deviceDir, slot));
    }

    const QDir pciDir(QStringLiteral("%1/bus/pci/devices").arg(sysfsRoot()));
    const QFileInfoList devices = pciDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo &device : devices) {
        const quint32 pciClass = readSysfsHex(device.absoluteFilePath() + QStringLiteral("/class"));
        if ((pciClass >> 16) != 0x03)
            continue;
        addGpu(adapterFromSysfsDevice(device.absoluteFilePath(), device.fileName()));
    }
#elif defined(Q_OS_WIN)
    for (const Adapter &dxgi : hardwareAdapters()) {
        Adapter gpu = dxgi;
        // Slot rather than PCI ids as the key, so two identical cards still list twice.
        gpu.slot = QStringLiteral("dxgi:%1").arg(dxgi.index);
        gpu.vendor = pciVendorName(dxgi.vendorId);
        addGpu(std::move(gpu));
    }
#endif
    return gpus;
}

#if defined(Q_OS_LINUX)

// Resolved from libEGL at runtime rather than compiled against EGL/eglext.h, for the same
// reason GlRuntime resolves its dma-buf import that way: Drift has to build and run on hosts
// with no EGL headers and no EGL at all.
constexpr int kEglDeviceExt = 0x322C;
constexpr int kEglDrmDeviceFileExt = 0x3233;
constexpr int kEglDrmRenderNodeFileExt = 0x3377;

QString queryEglDrmNode()
{
    void *egl = dlopen("libEGL.so.1", RTLD_LAZY | RTLD_LOCAL);
    if (!egl)
        return {};
    auto *getCurrentDisplay = reinterpret_cast<void *(*)()>(dlsym(egl, "eglGetCurrentDisplay"));
    auto *getProc = reinterpret_cast<void *(*)(const char *)>(dlsym(egl, "eglGetProcAddress"));
    if (!getCurrentDisplay || !getProc)
        return {};

    void *display = getCurrentDisplay();
    if (!display)
        return {}; // GLX, or no context current on this thread

    auto queryDisplayAttrib =
        reinterpret_cast<unsigned int (*)(void *, int, intptr_t *)>(getProc("eglQueryDisplayAttribEXT"));
    auto queryDeviceString =
        reinterpret_cast<const char *(*)(void *, int)>(getProc("eglQueryDeviceStringEXT"));
    if (!queryDisplayAttrib || !queryDeviceString)
        return {};

    intptr_t device = 0;
    if (!queryDisplayAttrib(display, kEglDeviceExt, &device) || !device)
        return {};

    // The render node is the one a decoder would open, so prefer it; the card node still
    // identifies the same PCI device when EGL_EXT_device_drm_render_node is missing.
    for (const int attribute : {kEglDrmRenderNodeFileExt, kEglDrmDeviceFileExt}) {
        if (const char *path = queryDeviceString(reinterpret_cast<void *>(device), attribute))
            if (*path)
                return QString::fromUtf8(path);
    }
    return {};
}

#endif // Q_OS_LINUX

} // namespace

QString pciVendorName(quint16 id)
{
    switch (id) {
    case 0x10de:
        return QStringLiteral("NVIDIA");
    case 0x8086:
        return QStringLiteral("Intel");
    case 0x1002:
    case 0x1022:
        return QStringLiteral("AMD");
    case 0x14e4:
        return QStringLiteral("Broadcom");
    case 0x1af4:
        return QStringLiteral("Virtio");
    case 0x15ad:
        return QStringLiteral("VMware");
    case 0x1234:
    case 0x1b36:
        return QStringLiteral("QEMU");
    case 0x13b5:
        return QStringLiteral("ARM");
    case 0x106b:
        return QStringLiteral("Apple");
    case 0x17cb:
        return QStringLiteral("Qualcomm");
    case 0x1414:
        return QStringLiteral("Microsoft");
    default:
        return {};
    }
}

quint16 vendorIdForGlVendor(const QString &glVendor)
{
    // Case-sensitive on purpose for the short names: "Corporation" contains "ati".
    if (glVendor.contains(QLatin1String("NVIDIA"), Qt::CaseInsensitive))
        return 0x10de;
    if (glVendor.contains(QLatin1String("Intel"), Qt::CaseInsensitive))
        return 0x8086;
    if (glVendor.contains(QLatin1String("AMD")) || glVendor.contains(QLatin1String("ATI")))
        return 0x1002;
    return 0;
}

QList<Adapter> enumerateAdapters()
{
    QMutexLocker lock(&g_mutex);
    if (!g_adaptersCached) {
        g_adapters = enumerateLocked();
        g_adaptersCached = true;
    }
    return g_adapters;
}

void probeRenderDrmNode()
{
#if defined(Q_OS_LINUX)
    const QString node = queryEglDrmNode();
    QMutexLocker lock(&g_mutex);
    if (!node.isEmpty())
        g_renderDrmNode = node;
#endif
}

QString renderDrmNode()
{
    QMutexLocker lock(&g_mutex);
    return g_renderDrmNode;
}

PciId pciIdForDrmNode(const QString &node)
{
#if defined(Q_OS_LINUX)
    QString dir;
    {
        QMutexLocker lock(&g_mutex);
        dir = drmDeviceDir(node);
    }
    if (dir.isEmpty())
        return {};
    PciId id;
    id.vendor = static_cast<quint16>(readSysfsHex(dir + QStringLiteral("/vendor")));
    id.device = static_cast<quint16>(readSysfsHex(dir + QStringLiteral("/device")));
    return id;
#else
    Q_UNUSED(node);
    return {};
#endif
}

QList<QPair<QString, PciId>> drmRenderNodes()
{
    QList<QPair<QString, PciId>> nodes;
#if defined(Q_OS_LINUX)
    QString root;
    {
        QMutexLocker lock(&g_mutex);
        root = sysfsRoot();
    }
    const QDir drmDir(QStringLiteral("%1/class/drm").arg(root));
    static const QRegularExpression renderRe(QStringLiteral("^renderD\\d+$"));
    for (const QFileInfo &entry : drmDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (!renderRe.match(entry.fileName()).hasMatch())
            continue;
        const PciId id = pciIdForDrmNode(entry.fileName());
        if (id.isValid())
            nodes.append({QStringLiteral("/dev/dri/%1").arg(entry.fileName()), id});
    }
#endif
    return nodes;
}

PciId renderPciId(const QString &glVendor)
{
    const QString vendor = glVendor.isEmpty() ? drift::hwaccel::renderVendor() : glVendor;
    // The probed node describes the live renderer, so it cannot answer for anyone else.
    if (glVendor.isEmpty() || glVendor == drift::hwaccel::renderVendor()) {
        if (const QString node = renderDrmNode(); !node.isEmpty()) {
            if (const PciId id = pciIdForDrmNode(node); id.isValid())
                return id;
        }
    }

    // No node to ask, so fall back to the vendor. One adapter of that vendor is an answer;
    // two are not, and saying so is better than picking the first and warning about the
    // wrong GPU.
    const quint16 wanted = vendorIdForGlVendor(vendor);
    if (wanted == 0)
        return {};
    PciId found;
    int matches = 0;
    for (const Adapter &adapter : enumerateAdapters()) {
        const bool sameVendor =
            adapter.vendorId == wanted || (wanted == 0x1002 && adapter.vendorId == 0x1022);
        if (!sameVendor)
            continue;
        ++matches;
        found = adapter.pci();
    }
    return matches == 1 ? found : PciId{};
}

QString adapterName(PciId id)
{
    if (!id.isValid())
        return {};
    for (const Adapter &adapter : enumerateAdapters()) {
        if (adapter.pci() == id)
            return adapter.name.isEmpty() ? adapter.vendor : adapter.name;
    }
    return {};
}

void setSysfsRootForTesting(const QString &root)
{
    QMutexLocker lock(&g_mutex);
    g_sysfsRoot = root;
    g_adapters.clear();
    g_adaptersCached = false;
}

} // namespace drift::gpu
