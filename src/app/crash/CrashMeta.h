#pragma once

#include "app/crash/CrashStore.h"

#include <QJsonObject>
#include <QList>
#include <QPair>
#include <QString>

namespace drift::crash {

struct GpuInfo
{
    QString vendor;
    QString name;
    QString driver;
};

struct SystemInfo
{
    QString appVersion;
    QString channel;       // stable, nightly, msstore
    QString arch;          // x64, arm64
    QString osName;        // windows
    QString osVersion;     // 10.0.19042
    QString osDescription; // "Windows 11 Version 23H2"; only in the saved bundle
    QList<GpuInfo> gpus;
    QString graphicsApi;
    QList<QPair<QString, QString>> addons; // id, version
    QString locale;
    int cpuCores = 0;
};

// "stable", "nightly" or "msstore" from what the build knows about itself.
QString channelFor(const QString &version, const QString &distribution);
// "x64" or "arm64" from QSysInfo::buildCpuArchitecture(); the input verbatim when unknown.
QString archFor(const QString &cpuArchitecture);

// The `meta` part of POST /v1/report. Fields are capped the way the server will cap them.
QJsonObject buildMeta(const CrashInfo &crash, const QString &reasonKey, const SystemInfo &system,
                      int uptimeSeconds);

// Human-readable, for the saved bundle.
QString systemInfoText(const SystemInfo &system, const CrashInfo *crash, const QString &reasonKey);

} // namespace drift::crash
