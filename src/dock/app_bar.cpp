#include "dock/app_bar.h"

#include "windows/monitors.h"

#include <shellapi.h>

namespace mdu {

namespace {

UINT toShellEdge(DockEdge edge)
{
    switch (edge) {
    case DockEdge::Left: return ABE_LEFT;
    case DockEdge::Right: return ABE_RIGHT;
    case DockEdge::Bottom: break;
    }
    return ABE_BOTTOM;
}

void clampToThickness(RECT& rect, UINT edge, int thickness)
{
    switch (edge) {
    case ABE_LEFT: rect.right = rect.left + thickness; break;
    case ABE_RIGHT: rect.left = rect.right - thickness; break;
    default: rect.top = rect.bottom - thickness; break;
    }
}

}  // namespace

AppBar::AppBar(HWND window, UINT callbackMessage) : window_(window), callbackMessage_(callbackMessage) {}

AppBar::~AppBar()
{
    unregister();
}

bool AppBar::registerBar()
{
    if (registered_) {
        return true;
    }
    APPBARDATA data{};
    data.cbSize = sizeof(data);
    data.hWnd = window_;
    data.uCallbackMessage = callbackMessage_;
    registered_ = SHAppBarMessage(ABM_NEW, &data) != FALSE;
    return registered_;
}

void AppBar::unregister()
{
    if (registered_) {
        APPBARDATA data{};
        data.cbSize = sizeof(data);
        data.hWnd = window_;
        SHAppBarMessage(ABM_REMOVE, &data);
        registered_ = false;
        reserved_ = false;
    }
}

Rect AppBar::reserve(DockEdge edge, const Rect& monitorBounds, int thickness)
{
    if (!registered_ && !registerBar()) {
        return {};
    }

    APPBARDATA data{};
    data.cbSize = sizeof(data);
    data.hWnd = window_;
    data.uEdge = toShellEdge(edge);
    data.rc = toRECT(monitorBounds);
    clampToThickness(data.rc, data.uEdge, thickness);

    // Explorer moves the proposal away from the taskbar and other app bars,
    // then the thickness is re-applied from the adjusted outer edge.
    SHAppBarMessage(ABM_QUERYPOS, &data);
    clampToThickness(data.rc, data.uEdge, thickness);

    // ABM_SETPOS notifies other app bars on the same edge, including MDU's
    // docks on other monitors, which then lay out again. Skipping unchanged
    // reservations keeps that from turning into an endless exchange.
    if (reserved_ && reservedEdge_ == data.uEdge && EqualRect(&reservedRect_, &data.rc)) {
        return toRect(data.rc);
    }
    SHAppBarMessage(ABM_SETPOS, &data);
    reserved_ = true;
    reservedEdge_ = data.uEdge;
    reservedRect_ = data.rc;
    return toRect(data.rc);
}

void AppBar::releaseReservation()
{
    if (!reserved_) {
        return;
    }
    // Removing and re-adding the bar drops the reserved area while keeping
    // full-screen notifications.
    unregister();
    registerBar();
}

void AppBar::onActivate()
{
    if (registered_) {
        APPBARDATA data{};
        data.cbSize = sizeof(data);
        data.hWnd = window_;
        SHAppBarMessage(ABM_ACTIVATE, &data);
    }
}

void AppBar::onWindowPositionChanged()
{
    if (registered_) {
        APPBARDATA data{};
        data.cbSize = sizeof(data);
        data.hWnd = window_;
        SHAppBarMessage(ABM_WINDOWPOSCHANGED, &data);
    }
}

}  // namespace mdu
