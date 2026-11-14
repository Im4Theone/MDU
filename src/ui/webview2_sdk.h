#pragma once

// Include WebView2 through this header. WebView2.h uses the COM `interface`
// keyword before including anything itself; that macro comes from objbase.h,
// which windows.h skips when WIN32_LEAN_AND_MEAN is defined.
#include <windows.h>
#include <objbase.h>

#include <WebView2.h>
