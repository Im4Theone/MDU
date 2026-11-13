#pragma once

#include "utilities/geometry.h"

#include <windows.h>

#include <string>
#include <vector>

namespace mdu {

struct MonitorInfo {
    HMONITOR handle = nullptr;
    std::wstring deviceName;  // e.g. \\.\DISPLAY1; stable across display changes, unlike HMONITOR
    Rect bounds;
    Rect workArea;
    unsigned dpi = 96;
    bool primary = false;
};

std::vector<MonitorInfo> enumerateMonitors();

Rect toRect(const RECT& rect);
RECT toRECT(const Rect& rect);

}  // namespace mdu
