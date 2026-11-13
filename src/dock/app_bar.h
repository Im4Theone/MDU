#pragma once

#include "settings/config.h"
#include "utilities/geometry.h"

#include <windows.h>

namespace mdu {

// Thin wrapper around the shell's app bar protocol (SHAppBarMessage).
//
// Registering gives the dock full-screen notifications from Explorer. In
// "reserve" placement it also claims a strip of the screen so maximized
// windows stop above the dock, the same way the taskbar does.
class AppBar {
public:
    AppBar(HWND window, UINT callbackMessage);
    ~AppBar();

    AppBar(const AppBar&) = delete;
    AppBar& operator=(const AppBar&) = delete;

    bool registerBar();
    void unregister();

    // Claims `thickness` pixels along `edge` of `monitorBounds`, after Explorer
    // has moved the request clear of the taskbar and other app bars. Returns
    // the granted rectangle.
    Rect reserve(DockEdge edge, const Rect& monitorBounds, int thickness);

    // Releases a reservation but stays registered for notifications.
    void releaseReservation();

    // Must be forwarded from the window procedure (part of the protocol).
    void onActivate();
    void onWindowPositionChanged();

private:
    HWND window_;
    UINT callbackMessage_;
    bool registered_ = false;
    bool reserved_ = false;
    UINT reservedEdge_ = 0;
    RECT reservedRect_{};
};

}  // namespace mdu
