#pragma once

#include <windows.h>
#include <shellapi.h>

#include <string>

namespace mdu {

// Notification-area icon. Events arrive at the owner window as
// `callbackMessage` with NOTIFYICON_VERSION_4 semantics:
// LOWORD(lParam) is the event, wParam holds the anchor point.
class TrayIcon {
public:
    TrayIcon(HWND owner, UINT callbackMessage, HICON icon, std::wstring tooltip);
    ~TrayIcon();

    TrayIcon(const TrayIcon&) = delete;
    TrayIcon& operator=(const TrayIcon&) = delete;

    // Call again after Explorer restarts (the "TaskbarCreated" message).
    bool add();
    void remove();
    void notify(const std::wstring& title, const std::wstring& text);

private:
    NOTIFYICONDATAW baseData() const;

    HWND owner_;
    UINT callbackMessage_;
    HICON icon_;
    std::wstring tooltip_;
    bool added_ = false;
};

}  // namespace mdu
