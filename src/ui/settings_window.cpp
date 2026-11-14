#include "ui/settings_window.h"

#include "utilities/geometry.h"
#include "utilities/log.h"

#include <commctrl.h>
#include <dwmapi.h>
#include <shellscalingapi.h>

#include <algorithm>

#include <format>

namespace mdu {

namespace {

constexpr wchar_t windowClassName[] = L"MDU.Settings";
constexpr int widthDips = 760;
constexpr int heightDips = 680;
constexpr int minimumWidthDips = 520;
constexpr int minimumHeightDips = 440;
constexpr COLORREF pageColor = RGB(16, 17, 20);  // matches the settings page background

// DWMWA_USE_IMMERSIVE_DARK_MODE; spelled out because older SDK headers lack it.
constexpr DWORD darkModeAttribute = 20;

void registerWindowClass(HINSTANCE instance, WNDPROC procedure)
{
    static bool registered = false;
    if (registered) {
        return;
    }
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = procedure;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = CreateSolidBrush(pageColor);  // lives as long as the class
    windowClass.lpszClassName = windowClassName;
    LoadIconMetric(instance, MAKEINTRESOURCEW(1), LIM_LARGE, &windowClass.hIcon);
    LoadIconMetric(instance, MAKEINTRESOURCEW(1), LIM_SMALL, &windowClass.hIconSm);
    registered = RegisterClassExW(&windowClass) != 0;
}

}  // namespace

std::unique_ptr<SettingsWindow> SettingsWindow::create(HINSTANCE instance, WebViewHost::Content content,
                                                       Callbacks callbacks)
{
    registerWindowClass(instance, &SettingsWindow::windowProc);
    std::unique_ptr<SettingsWindow> settings(new SettingsWindow(std::move(callbacks)));

    // Size for the monitor the user is working on, centred in its work area.
    POINT cursor{};
    GetCursorPos(&cursor);
    const HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    GetMonitorInfoW(monitor, &info);
    UINT dpiX = 96;
    UINT dpiY = 96;
    GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);

    const RECT& work = info.rcWork;
    const int width = std::min<int>(dipsToPixels(widthDips, dpiX), work.right - work.left);
    const int height = std::min<int>(dipsToPixels(heightDips, dpiX), work.bottom - work.top);
    const int x = work.left + (work.right - work.left - width) / 2;
    const int y = work.top + (work.bottom - work.top - height) / 2;

    const HWND window = CreateWindowExW(0, windowClassName, L"MDU Settings", WS_OVERLAPPEDWINDOW, x, y, width, height,
                                        nullptr, nullptr, instance, settings.get());
    if (!window) {
        log::error(std::format("cannot create settings window: error {}", GetLastError()));
        return nullptr;
    }

    const BOOL dark = TRUE;
    DwmSetWindowAttribute(window, darkModeAttribute, &dark, sizeof(dark));

    SettingsWindow* self = settings.get();
    settings->webView_ = std::make_unique<WebViewHost>(
        window, std::move(content),
        WebViewHost::Options{.page = L"settings.html",
                             .name = "settings",
                             .background = COREWEBVIEW2_COLOR{255, GetRValue(pageColor), GetGValue(pageColor),
                                                              GetBValue(pageColor)},
                             .startVisible = true},
        WebViewHost::Callbacks{
            .message = [self](const std::string& text) { self->callbacks_.message(text); },
            .pageLost = [self] { self->pageReady_ = false; },
            .browserFailed =
                [self](COREWEBVIEW2_PROCESS_FAILED_KIND kind) {
                    if (self->callbacks_.browserFailed) {
                        self->callbacks_.browserFailed(kind);
                    }
                },
        });

    ShowWindow(window, SW_SHOWNORMAL);
    return settings;
}

SettingsWindow::SettingsWindow(Callbacks callbacks) : callbacks_(std::move(callbacks)) {}

SettingsWindow::~SettingsWindow()
{
    webView_.reset();
    if (window_) {
        SetWindowLongPtrW(window_, GWLP_USERDATA, 0);
        DestroyWindow(window_);
    }
}

LRESULT CALLBACK SettingsWindow::windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_NCCREATE) {
        auto* settings = static_cast<SettingsWindow*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        settings->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(settings));
    }
    if (auto* settings = reinterpret_cast<SettingsWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA))) {
        return settings->handleMessage(message, wParam, lParam);
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

LRESULT SettingsWindow::handleMessage(UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_SIZE:
        if (webView_) {
            webView_->resizeToParent();
        }
        return 0;

    case WM_WINDOWPOSCHANGED:
        if (webView_) {
            webView_->parentMoved();
        }
        break;

    case WM_ACTIVATE:
        if (LOWORD(wParam) != WA_INACTIVE && webView_) {
            webView_->focus();
        }
        return 0;

    case WM_GETMINMAXINFO: {
        const UINT dpi = GetDpiForWindow(window_);
        auto* limits = reinterpret_cast<MINMAXINFO*>(lParam);
        limits->ptMinTrackSize = {dipsToPixels(minimumWidthDips, dpi), dipsToPixels(minimumHeightDips, dpi)};
        return 0;
    }

    case WM_DPICHANGED: {
        const auto* suggested = reinterpret_cast<const RECT*>(lParam);
        SetWindowPos(window_, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
                     suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }

    case WM_CLOSE:
        // The owner destroys this object; doing it here would delete the
        // window inside its own message handler.
        if (callbacks_.closeRequested) {
            callbacks_.closeRequested();
        }
        return 0;

    default:
        break;
    }
    return DefWindowProcW(window_, message, wParam, lParam);
}

void SettingsWindow::attachWebView(ICoreWebView2Environment* environment)
{
    if (webView_) {
        webView_->attach(environment);
    }
}

void SettingsWindow::bringToFront()
{
    if (IsIconic(window_)) {
        ShowWindow(window_, SW_RESTORE);
    }
    SetForegroundWindow(window_);
}

void SettingsWindow::setPageReady()
{
    pageReady_ = true;
    flushState();
}

void SettingsWindow::postState(std::string json)
{
    state_ = std::move(json);
    flushState();
}

void SettingsWindow::flushState()
{
    if (pageReady_ && webView_ && !state_.empty()) {
        webView_->postJson(state_);
    }
}

void SettingsWindow::executeScript(const std::wstring& script)
{
    if (webView_) {
        webView_->executeScript(script);
    }
}

}  // namespace mdu
