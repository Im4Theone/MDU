#pragma once

#include "ui/webview_host.h"

#include <windows.h>

#include <functional>
#include <memory>
#include <string>

namespace mdu {

// The settings window: a normal, resizable top-level window hosting the local
// settings page. It is created when opened and destroyed when closed, so it
// costs nothing while not in use. Message interpretation is up to the owner.
class SettingsWindow {
public:
    struct Callbacks {
        std::function<void(const std::string& message)> message;
        std::function<void()> closeRequested;
        std::function<void(COREWEBVIEW2_PROCESS_FAILED_KIND)> browserFailed;
    };

    static std::unique_ptr<SettingsWindow> create(HINSTANCE instance, WebViewHost::Content content,
                                                  Callbacks callbacks);
    ~SettingsWindow();

    SettingsWindow(const SettingsWindow&) = delete;
    SettingsWindow& operator=(const SettingsWindow&) = delete;

    void attachWebView(ICoreWebView2Environment* environment);
    void bringToFront();

    // The page announced itself; send it the latest state now and after reloads.
    void setPageReady();
    void postState(std::string json);

    HWND window() const { return window_; }
    bool pageReady() const { return pageReady_; }
    void executeScript(const std::wstring& script);

private:
    explicit SettingsWindow(Callbacks callbacks);

    static LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    void flushState();

    HWND window_ = nullptr;
    Callbacks callbacks_;
    std::unique_ptr<WebViewHost> webView_;
    std::string state_;
    bool pageReady_ = false;
};

}  // namespace mdu
