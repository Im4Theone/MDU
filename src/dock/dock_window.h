#pragma once

#include "dock/app_bar.h"
#include "ui/bridge.h"
#include "windows/monitors.h"

#include <WebView2.h>
#include <windows.h>
#include <wrl/client.h>

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

    struct Content {
        std::filesystem::path webDirectory;
        std::filesystem::path themesDirectory;
    };

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

    static constexpr wchar_t appOrigin[] = L"https://mdu.example/";
    static constexpr wchar_t themesOrigin[] = L"https://themes.mdu.example/";

private:
    DockWindow(const MonitorInfo& monitor, Callbacks callbacks, Content content);

    static LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    void onControllerCreated(ICoreWebView2Controller* controller);
    void configureWebView();
    void onWebMessage(ICoreWebView2WebMessageReceivedEventArgs* args);
    void flushState();
    void updateVisibility();
    void resizeWebView();
    bool fullscreenAppOnMonitor() const;

    HWND window_ = nullptr;
    MonitorInfo monitor_;
    Callbacks callbacks_;
    Content content_;
    std::unique_ptr<AppBar> appBar_;

    Microsoft::WRL::ComPtr<ICoreWebView2Controller> controller_;
    Microsoft::WRL::ComPtr<ICoreWebView2> webView_;
    std::string state_;

    bool pageReady_ = false;
    bool hasBounds_ = false;
    bool fullscreenHidden_ = false;

    // Async WebView2 callbacks hold a weak reference so they never touch a destroyed window.
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

}  // namespace mdu
