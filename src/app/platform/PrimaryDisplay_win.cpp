#include "app/platform/PrimaryDisplay.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellscalingapi.h>

namespace drift::display {

std::optional<PrimaryDisplay> primaryDisplay()
{
    // Before QApplication the process may still be DPI-unaware, and then GetDpiForMonitor
    // answers 96 whatever the setting is. Thread-scoped, so Qt's own process-wide choice later
    // is unaffected.
    const DPI_AWARENESS_CONTEXT previous =
        SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    std::optional<PrimaryDisplay> result;
    const HMONITOR monitor = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    UINT dpiX = 0;
    UINT dpiY = 0;
    if (monitor && GetMonitorInfoW(monitor, &info)
        && SUCCEEDED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY)) && dpiX > 0) {
        // The display mode is in physical pixels however the caller is DPI-aware.
        DEVMODEW mode{};
        mode.dmSize = sizeof(mode);
        if (EnumDisplaySettingsW(info.szDevice, ENUM_CURRENT_SETTINGS, &mode) && mode.dmPelsWidth > 0
            && mode.dmPelsHeight > 0) {
            result = PrimaryDisplay{int(mode.dmPelsWidth), int(mode.dmPelsHeight), dpiX / 96.0};
        }
    }

    if (previous)
        SetThreadDpiAwarenessContext(previous);
    return result;
}

} // namespace drift::display
