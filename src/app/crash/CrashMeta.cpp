#include "app/crash/CrashMeta.h"

#include <QJsonArray>

namespace drift::crash {

namespace {

QString capped(const QString &text)
{
    return text.left(256);
}

} // namespace

QString channelFor(const QString &version, const QString &distribution)
{
    if (distribution == QLatin1String("windows-store"))
        return QStringLiteral("msstore");
    return version.contains(QLatin1String("nightly")) ? QStringLiteral("nightly") : QStringLiteral("stable");
}

QString osNameFor(const QString &kernelType)
{
    if (kernelType == QLatin1String("winnt"))
        return QStringLiteral("windows");
    if (kernelType == QLatin1String("darwin"))
        return QStringLiteral("macos");
    return kernelType;
}

QString archFor(const QString &cpuArchitecture)
{
    if (cpuArchitecture == QLatin1String("x86_64"))
        return QStringLiteral("x64");
    return cpuArchitecture;
}

QJsonObject buildMeta(const CrashInfo &crash, const QString &reasonKey, const SystemInfo &system,
                      int uptimeSeconds)
{
    QJsonObject app{{QStringLiteral("name"), QStringLiteral("drift")},
                    {QStringLiteral("version"), capped(system.appVersion)},
                    {QStringLiteral("channel"), capped(system.channel)},
                    {QStringLiteral("arch"), capped(system.arch)}};
    QJsonObject os{{QStringLiteral("name"), capped(system.osName)},
                   {QStringLiteral("version"), capped(system.osVersion)}};

    QJsonObject crashObject{{QStringLiteral("code"), capped(crash.code)},
                            {QStringLiteral("module"), capped(crash.module)},
                            {QStringLiteral("offset"), capped(crash.offset)},
                            {QStringLiteral("reasonKey"), reasonKey}};
    if (!crash.moduleVersion.isEmpty())
        crashObject.insert(QStringLiteral("moduleVersion"), capped(crash.moduleVersion));

    QJsonArray gpus;
    for (const GpuInfo &gpu : system.gpus) {
        if (gpus.size() >= 4)
            break;
        QJsonObject entry{{QStringLiteral("vendor"), capped(gpu.vendor)}, {QStringLiteral("name"), capped(gpu.name)}};
        if (!gpu.driver.isEmpty())
            entry.insert(QStringLiteral("driver"), capped(gpu.driver));
        gpus.append(entry);
    }

    QJsonArray addons;
    for (const auto &addon : system.addons) {
        if (addons.size() >= 64)
            break;
        addons.append(QJsonObject{{QStringLiteral("id"), capped(addon.first)},
                                  {QStringLiteral("version"), capped(addon.second)}});
    }

    return {{QStringLiteral("schema"), 1},
            {QStringLiteral("reportId"), crash.id},
            {QStringLiteral("crashedAt"), crash.crashedAt.toUTC().toString(Qt::ISODate)},
            {QStringLiteral("uptimeSeconds"), uptimeSeconds},
            {QStringLiteral("app"), app},
            {QStringLiteral("os"), os},
            {QStringLiteral("crash"), crashObject},
            {QStringLiteral("gpu"), gpus},
            {QStringLiteral("graphicsApi"), capped(system.graphicsApi)},
            {QStringLiteral("addons"), addons},
            {QStringLiteral("locale"), capped(system.locale)}};
}

QString systemInfoText(const SystemInfo &system, const CrashInfo *crash, const QString &reasonKey)
{
    QString text;
    auto line = [&text](const QString &key, const QString &value) {
        text += key + QStringLiteral(": ") + value + QLatin1Char('\n');
    };
    line(QStringLiteral("Drift"), system.appVersion + QStringLiteral(" (") + system.channel + QLatin1Char(')'));
    line(QStringLiteral("Architecture"), system.arch);
    line(QStringLiteral("OS"), system.osDescription + QStringLiteral(" [") + system.osName + QLatin1Char(' ')
                                   + system.osVersion + QLatin1Char(']'));
    line(QStringLiteral("CPU cores"), QString::number(system.cpuCores));
    line(QStringLiteral("Locale"), system.locale);
    line(QStringLiteral("Graphics API"), system.graphicsApi);
    for (const GpuInfo &gpu : system.gpus) {
        line(QStringLiteral("GPU"), gpu.vendor + QLatin1Char(' ') + gpu.name
                                        + (gpu.driver.isEmpty() ? QString() : QStringLiteral(", driver ") + gpu.driver));
    }
    for (const auto &addon : system.addons)
        line(QStringLiteral("Add-on"), addon.first + QLatin1Char(' ') + addon.second);
    if (crash) {
        line(QStringLiteral("Crash"), crash->code + QStringLiteral(" in ") + crash->module + QLatin1Char(' ')
                                          + crash->moduleVersion + QStringLiteral(" at ") + crash->offset);
        line(QStringLiteral("Crash reason"), reasonKey);
        line(QStringLiteral("Crashed at"), crash->crashedAt.toString(Qt::ISODate));
    }
    return text;
}

} // namespace drift::crash
