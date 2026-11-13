#include "windows/tray_icon.h"

#include <shellapi.h>

#include <utility>

namespace mdu {

namespace {

constexpr UINT trayIconId = 1;

}  // namespace

TrayIcon::TrayIcon(HWND owner, UINT callbackMessage, HICON icon, std::wstring tooltip)
    : owner_(owner), callbackMessage_(callbackMessage), icon_(icon), tooltip_(std::move(tooltip))
{
}

TrayIcon::~TrayIcon()
{
    remove();
}

NOTIFYICONDATAW TrayIcon::baseData() const
{
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = owner_;
    data.uID = trayIconId;
    return data;
}

bool TrayIcon::add()
{
    auto data = baseData();
    data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    data.uCallbackMessage = callbackMessage_;
    data.hIcon = icon_;
    wcsncpy_s(data.szTip, tooltip_.c_str(), _TRUNCATE);

    // After an Explorer restart the old icon is gone; NIM_ADD then succeeds again.
    Shell_NotifyIconW(NIM_DELETE, &data);
    if (!Shell_NotifyIconW(NIM_ADD, &data)) {
        added_ = false;
        return false;
    }
    data.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &data);
    added_ = true;
    return true;
}

void TrayIcon::remove()
{
    if (added_) {
        auto data = baseData();
        Shell_NotifyIconW(NIM_DELETE, &data);
        added_ = false;
    }
}

void TrayIcon::notify(const std::wstring& title, const std::wstring& text)
{
    if (!added_) {
        return;
    }
    auto data = baseData();
    data.uFlags = NIF_INFO;
    data.dwInfoFlags = NIIF_INFO | NIIF_RESPECT_QUIET_TIME;
    wcsncpy_s(data.szInfoTitle, title.c_str(), _TRUNCATE);
    wcsncpy_s(data.szInfo, text.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &data);
}

}  // namespace mdu
