#include "GpuPreference.h"

#include <QSettings>

#include <mutex>

#if defined(Q_OS_WIN)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dxgi.h>

#include <string>
#endif

namespace drift::gpu {
namespace {

QString settingsKey()
{
    return QStringLiteral("graphics/preferredGpu");
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

#if defined(Q_OS_WIN)
// Where Settings → System → Display → Graphics keeps its per-application choices: one REG_SZ per
// executable path. Hybrid-graphics drivers consult it for OpenGL as well as Direct3D; the
// NvOptimusEnablement / AmdPowerXpressRequestHighPerformance exports in main.cpp cover the ones
// that do not.
constexpr const wchar_t *kUserGpuPreferences = L"Software\\Microsoft\\DirectX\\UserGpuPreferences";

std::wstring executablePath()
{
    std::wstring path(MAX_PATH, L'\0');
    for (int attempt = 0; attempt < 6; ++attempt) {
        const DWORD n = GetModuleFileNameW(nullptr, path.data(), DWORD(path.size()));
        if (n == 0)
            return {};
        if (n < path.size()) {
            path.resize(n);
            return path;
        }
        path.resize(path.size() * 2);
    }
    return {};
}

void writeRegistryPreference(Preference preference)
{
    const std::wstring exe = executablePath();
    if (exe.empty())
        return;
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kUserGpuPreferences, 0, nullptr, 0, KEY_SET_VALUE,
                        nullptr, &key, nullptr)
        != ERROR_SUCCESS) {
        qWarning("GpuPreference: cannot open HKCU\\%ls", kUserGpuPreferences);
        return;
    }
    if (preference == Preference::Auto) {
        RegDeleteValueW(key, exe.c_str());
    } else {
        const std::wstring data = preference == Preference::PowerSaving ? L"GpuPreference=1;"
                                                                        : L"GpuPreference=2;";
        RegSetValueExW(key, exe.c_str(), 0, REG_SZ, reinterpret_cast<const BYTE *>(data.c_str()),
                       DWORD((data.size() + 1) * sizeof(wchar_t)));
    }
    RegCloseKey(key);
}
#endif

} // namespace

QList<Adapter> hardwareAdapters()
{
    static QList<Adapter> adapters;
    static std::once_flag once;
    std::call_once(once, [] {
#if defined(Q_OS_WIN)
        IDXGIFactory1 *factory = nullptr;
        if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void **>(&factory)))
            || !factory)
            return;
        IDXGIAdapter1 *adapter = nullptr;
        for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
            DXGI_ADAPTER_DESC1 desc{};
            if (SUCCEEDED(adapter->GetDesc1(&desc)) && !(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
                Adapter out;
                // The raw index, not the position in this list: FFmpeg enumerates the same
                // factory order including any software adapter, and takes the number as is.
                out.index = int(i);
                out.name = QString::fromWCharArray(desc.Description).trimmed();
                out.vendorId = quint16(desc.VendorId);
                out.deviceId = quint16(desc.DeviceId);
                adapters.append(out);
            }
            adapter->Release();
        }
        factory->Release();
#endif
    });
    return adapters;
}

int adapterIndexForGlVendor(const QString &glVendor)
{
    const quint16 wanted = vendorIdForGlVendor(glVendor);
    if (wanted == 0)
        return -1;
    for (const Adapter &adapter : hardwareAdapters()) {
        if (adapter.vendorId == wanted || (wanted == 0x1002 && adapter.vendorId == 0x1022))
            return adapter.index;
    }
    return -1;
}

QString preferenceId(Preference preference)
{
    switch (preference) {
    case Preference::PowerSaving:
        return QStringLiteral("integrated");
    case Preference::HighPerformance:
        return QStringLiteral("discrete");
    case Preference::Auto:
        break;
    }
    return QStringLiteral("auto");
}

Preference preferenceFromId(const QString &id)
{
    if (id == QLatin1String("integrated"))
        return Preference::PowerSaving;
    if (id == QLatin1String("discrete"))
        return Preference::HighPerformance;
    return Preference::Auto;
}

Preference storedPreference()
{
    return preferenceFromId(QSettings().value(settingsKey()).toString());
}

bool preferenceSupported()
{
#if defined(Q_OS_WIN)
    return hardwareAdapters().size() >= 2;
#else
    return false;
#endif
}

void storePreference(Preference preference)
{
    QSettings().setValue(settingsKey(), preferenceId(preference));
#if defined(Q_OS_WIN)
    writeRegistryPreference(preference);
#endif
}

Preference applyStoredPreference()
{
    const Preference preference = storedPreference();
#if defined(Q_OS_WIN)
    // Auto is never written back: that would delete a choice the user made in Windows Settings
    // for this executable on every launch.
    if (preference != Preference::Auto)
        writeRegistryPreference(preference);
#endif
    return preference;
}

} // namespace drift::gpu
