#pragma once

#include "dock/app_bar.h"
#include "ui/bridge.h"
#include "windows/monitors.h"

#include "ui/webview_host.h"

#include <windows.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <string>

namespace mdu {

// One dock on one monitor: a borderless, transparent, topmost tool window that
// hosts a WebView2 controller showing the local dock page.
class DockWindow {
public:
    struct Callbacks {
        std::function<void(DockWindow&, const bridge::IncomingMessage&)> message;
        // DPI or app bar position changed; the owner should lay out again.
        std::function<void()> layoutInvalidated;
        std::function<void(COREWEBVIEW2_PROCESS_FAILED_KIND)> processFailed;
    };

    using Content = WebViewHost::Content;

    static std::unique_ptr<DockWindow> create(HINSTANCE instance, const MonitorInfo& monitor, Callbacks callbacks,
                                              Content content);
    ~DockWindow();

    DockWindow(const DockWindow&) = delete;
    DockWindow& operator=(const DockWindow&) = delete;

    const MonitorInfo& monitor() const { return monitor_; }
    void updateMonitor(const MonitorInfo& monitor) { monitor_ = monitor; }
    AppBar& appBar() { return *appBar_; }

    void attachWebView(ICoreWebView2Environment* environment);
    void setBounds(const Rect& bounds);

    // Sends the latest state to the page; kept and re-sent after page reloads.
    void postState(std::string json);

    // Diagnostics, used by the self-test.
    HWND window() const { return window_; }
    bool pageReady() const { return pageReady_; }
    int rejectedMessages() const { return rejectedMessages_; }
    int blockedNavigations() const { return webView_ ? webView_->blockedNavigations() : 0; }
    std::wstring currentUri() const { return webView_ ? webView_->currentUri() : std::wstring(); }
    void executeScript(const std::wstring& script);

private:
    DockWindow(const MonitorInfo& monitor, Callbacks callbacks, Content content);

    static LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    void onPageMessage(const std::string& text);
    void flushState();
    void updateVisibility();
    bool fullscreenAppOnMonitor() const;

    HWND window_ = nullptr;
    MonitorInfo monitor_;
    Callbacks callbacks_;
    Content content_;
    std::unique_ptr<AppBar> appBar_;

    std::unique_ptr<WebViewHost> webView_;
    std::string state_;

    bool pageReady_ = false;
    bool hasBounds_ = false;
    bool fullscreenHidden_ = false;
    int rejectedMessages_ = 0;
};

}  // namespace mdu
