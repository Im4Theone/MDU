#pragma once

#include "ui/webview2_sdk.h"

#include <wrl/client.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <string>

namespace mdu {

// One locked-down WebView2 controller inside a window.
//
// It shows only local application content (the web/ and themes/ folders,
// mapped to virtual hosts), cancels every other navigation, never opens
// popups, denies all permission requests, disables browser shortcuts and
// context menus, and only accepts string messages from the local page.
// Interpreting those messages is up to the owner.
class WebViewHost {
public:
    struct Content {
        std::filesystem::path webDirectory;
        std::filesystem::path themesDirectory;
    };

    struct Callbacks {
        std::function<void(const std::string& message)> message;  // UTF-8, from the local page only
        std::function<void()> pageLost;  // renderer crashed; the page reloads and must re-initialise
        std::function<void(COREWEBVIEW2_PROCESS_FAILED_KIND)> browserFailed;  // the environment is gone
    };

    struct Options {
        std::wstring page;        // relative to web/, e.g. L"index.html"
        std::string name;         // for log messages
        bool transparent = false;  // transparent background (the dock) or opaque (normal windows)
        bool startVisible = true;
    };

    WebViewHost(HWND parent, Content content, Options options, Callbacks callbacks);
    ~WebViewHost();

    WebViewHost(const WebViewHost&) = delete;
    WebViewHost& operator=(const WebViewHost&) = delete;

    void attach(ICoreWebView2Environment* environment);
    bool created() const { return controller_ != nullptr; }

    void resizeToParent();
    void parentMoved();
    void setVisible(bool visible);
    void focus();

    void postJson(const std::string& json);
    void executeScript(const std::wstring& script);
    std::wstring currentUri() const;
    int blockedNavigations() const { return blockedNavigations_; }

    static constexpr wchar_t appOrigin[] = L"https://mdu.example/";
    static constexpr wchar_t themesOrigin[] = L"https://themes.mdu.example/";

private:
    void onControllerCreated(ICoreWebView2Controller* controller);
    void configure();
    void onWebMessage(ICoreWebView2WebMessageReceivedEventArgs* args);

    HWND parent_;
    Content content_;
    Options options_;
    Callbacks callbacks_;
    Microsoft::WRL::ComPtr<ICoreWebView2Controller> controller_;
    Microsoft::WRL::ComPtr<ICoreWebView2> webView_;
    bool visible_;
    int blockedNavigations_ = 0;

    // Async WebView2 callbacks hold a weak reference so they never touch a destroyed host.
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

}  // namespace mdu
