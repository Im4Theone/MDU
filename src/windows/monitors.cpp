#include "windows/monitors.h"

#include <shellscalingapi.h>

namespace mdu {

Rect toRect(const RECT& rect)
{
    return {rect.left, rect.top, rect.right, rect.bottom};
}

RECT toRECT(const Rect& rect)
{
    return {rect.left, rect.top, rect.right, rect.bottom};
}

std::vector<MonitorInfo> enumerateMonitors()
{
    std::vector<MonitorInfo> monitors;
    EnumDisplayMonitors(
        nullptr, nullptr,
        [](HMONITOR handle, HDC, LPRECT, LPARAM context) -> BOOL {
            MONITORINFOEXW info{};
            info.cbSize = sizeof(info);
            if (!GetMonitorInfoW(handle, &info)) {
                return TRUE;
            }

            UINT dpiX = 96;
            UINT dpiY = 96;
            if (FAILED(GetDpiForMonitor(handle, MDT_EFFECTIVE_DPI, &dpiX, &dpiY))) {
                dpiX = 96;
            }

            auto& list = *reinterpret_cast<std::vector<MonitorInfo>*>(context);
            list.push_back({
                .handle = handle,
                .deviceName = info.szDevice,
                .bounds = toRect(info.rcMonitor),
                .workArea = toRect(info.rcWork),
                .dpi = dpiX,
                .primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0,
            });
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&monitors));
    return monitors;
}

}  // namespace mdu
