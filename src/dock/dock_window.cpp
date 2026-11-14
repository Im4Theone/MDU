#include "dock/dock_window.h"

#include "utilities/log.h"
#include "utilities/strings.h"

#include <dwmapi.h>
#include <shellapi.h>

#include <cwchar>
#include <format>

using Microsoft::WRL::ComPtr;

namespace mdu {

namespace {

constexpr wchar_t windowClassName[] = L"MDU.Dock";
constexpr UINT appBarMessage = WM_APP + 1;

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
    windowClass.lpszClassName = windowClassName;
    registered = RegisterClassExW(&windowClass) != 0;
}

}  // namespace

std::unique_ptr<DockWindow> DockWindow::create(HINSTANCE instance, const MonitorInfo& monitor, Callbacks callbacks,
                                               Content content)
{
    registerWindowClass(instance, &DockWindow::windowProc);

    std::unique_ptr<DockWindow> dock(new DockWindow(monitor, std::move(callbacks), std::move(content)));

    // WS_EX_NOREDIRECTIONBITMAP: the window has no surface of its own, so the
    // transparent WebView2 content composes directly over the desktop.
    // WS_EX_TOOLWINDOW keeps the dock out of the taskbar and Alt+Tab.
    const HWND window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOREDIRECTIONBITMAP,
                                        windowClassName, L"MDU Dock", WS_POPUP, monitor.workArea.left,
                                        monitor.workArea.top, 1, 1, nullptr, nullptr, instance, dock.get());
    if (!window) {
        log::error(std::format("cannot create dock window: error {}", GetLastError()));
        return nullptr;
    }

    const DWM_WINDOW_CORNER_PREFERENCE corners = DWMWCP_DONOTROUND;
    DwmSetWindowAttribute(window, DWMWA_WINDOW_CORNER_PREFERENCE, &corners, sizeof(corners));
    const BOOL excludeFromPeek = TRUE;
    DwmSetWindowAttribute(window, DWMWA_EXCLUDED_FROM_PEEK, &excludeFromPeek, sizeof(excludeFromPeek));

    dock->appBar_ = std::make_unique<AppBar>(window, appBarMessage);
    dock->appBar_->registerBar();

    DockWindow* self = dock.get();
    dock->webView_ = std::make_unique<WebViewHost>(
        window, dock->content_,
        WebViewHost::Options{.page = L"index.html",
                             .name = "dock " + toUtf8(monitor.deviceName),
                             .transparent = true,
                             .startVisible = false},
        WebViewHost::Callbacks{
            .message = [self](const std::string& text) { self->onPageMessage(text); },
            .pageLost =
                [self] {
                    // The page reloads and sends "ready" again, which re-sends the state.
                    self->pageReady_ = false;
                    self->updateVisibility();
                },
            .browserFailed =
                [self](COREWEBVIEW2_PROCESS_FAILED_KIND kind) {
                    if (self->callbacks_.processFailed) {
                        self->callbacks_.processFailed(kind);
                    }
                },
        });
    return dock;
}

DockWindow::DockWindow(const MonitorInfo& monitor, Callbacks callbacks, Content content)
    : monitor_(monitor), callbacks_(std::move(callbacks)), content_(std::move(content))
{
}

DockWindow::~DockWindow()
{
    webView_.reset();
    appBar_.reset();  // removes the app bar while the window still exists
    if (window_) {
        SetWindowLongPtrW(window_, GWLP_USERDATA, 0);
        DestroyWindow(window_);
    }
}

LRESULT CALLBACK DockWindow::windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_NCCREATE) {
        auto* dock = static_cast<DockWindow*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        dock->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(dock));
    }
    if (auto* dock = reinterpret_cast<DockWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA))) {
        return dock->handleMessage(message, wParam, lParam);
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

LRESULT DockWindow::handleMessage(UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case appBarMessage:
        if (wParam == ABN_FULLSCREENAPP) {
            fullscreenHidden_ = lParam != 0 && fullscreenAppOnMonitor();
            updateVisibility();
        } else if (wParam == ABN_POSCHANGED && callbacks_.layoutInvalidated) {
            callbacks_.layoutInvalidated();
        }
        return 0;

    case WM_DPICHANGED:
        // Positions are computed by the owner for each monitor, so the
        // suggested rectangle is ignored.
        if (callbacks_.layoutInvalidated) {
            callbacks_.layoutInvalidated();
        }
        return 0;

    case WM_SIZE:
        if (webView_) {
            webView_->resizeToParent();
        }
        return 0;

    case WM_WINDOWPOSCHANGED:
        if (appBar_) {
            appBar_->onWindowPositionChanged();
        }
        if (webView_) {
            webView_->parentMoved();
        }
        break;  // DefWindowProc generates WM_SIZE / WM_MOVE

    case WM_ACTIVATE:
        if (appBar_) {
            appBar_->onActivate();
        }
        break;

    case WM_CLOSE:
        return 0;  // the dock's lifetime is controlled by MDU, not by Alt+F4

    default:
        break;
    }
    return DefWindowProcW(window_, message, wParam, lParam);
}

void DockWindow::attachWebView(ICoreWebView2Environment* environment)
{
    if (webView_) {
        webView_->attach(environment);
    }
}

void DockWindow::onPageMessage(const std::string& text)
{
    const auto message = bridge::parseIncoming(text);
    if (!message) {
        log::warning("ignored invalid message from the dock page");
        ++rejectedMessages_;
        return;
    }

    if (std::holds_alternative<bridge::ReadyMessage>(*message)) {
        log::info("dock page ready on " + toUtf8(monitor_.deviceName));
        pageReady_ = true;
        flushState();
        updateVisibility();
    }
    if (callbacks_.message) {
        callbacks_.message(*this, *message);
    }
}

void DockWindow::executeScript(const std::wstring& script)
{
    if (webView_) {
        webView_->executeScript(script);
    }
}

void DockWindow::postState(std::string json)
{
    state_ = std::move(json);
    flushState();
}

void DockWindow::flushState()
{
    if (pageReady_ && webView_ && !state_.empty()) {
        webView_->postJson(state_);
    }
}

void DockWindow::setBounds(const Rect& bounds)
{
    SetWindowPos(window_, HWND_TOPMOST, bounds.left, bounds.top, bounds.width(), bounds.height(), SWP_NOACTIVATE);
    hasBounds_ = true;
    updateVisibility();
}

void DockWindow::updateVisibility()
{
    const bool visible = pageReady_ && hasBounds_ && !fullscreenHidden_;
    // A hidden WebView2 stops rendering, so a dock behind a full-screen game
    // costs nothing.
    if (webView_) {
        webView_->setVisible(visible);
    }
    if (visible) {
        SetWindowPos(window_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    } else {
        ShowWindow(window_, SW_HIDE);
    }
}

bool DockWindow::fullscreenAppOnMonitor() const
{
    const HWND foreground = GetForegroundWindow();
    if (!foreground || foreground == window_ || foreground == GetShellWindow() ||
        foreground == GetDesktopWindow()) {
        return false;
    }

    wchar_t className[32] = {};
    GetClassNameW(foreground, className, static_cast<int>(std::size(className)));
    if (std::wcscmp(className, L"WorkerW") == 0 || std::wcscmp(className, L"Progman") == 0) {
        return false;  // the desktop itself
    }

    if (MonitorFromWindow(foreground, MONITOR_DEFAULTTONULL) != monitor_.handle) {
        return false;
    }
    RECT rect{};
    if (!GetWindowRect(foreground, &rect)) {
        return false;
    }
    const auto& b = monitor_.bounds;
    return rect.left <= b.left && rect.top <= b.top && rect.right >= b.right && rect.bottom >= b.bottom;
}

}  // namespace mdu
