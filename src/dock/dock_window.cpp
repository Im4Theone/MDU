#include "dock/dock_window.h"

#include "utilities/com_callback.h"
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

// Owns a string allocated by WebView2 with CoTaskMemAlloc.
class CoTaskString {
public:
    CoTaskString() = default;
    ~CoTaskString() { CoTaskMemFree(value_); }
    CoTaskString(const CoTaskString&) = delete;
    CoTaskString& operator=(const CoTaskString&) = delete;

    LPWSTR* put() { return &value_; }
    const wchar_t* get() const { return value_ ? value_ : L""; }

private:
    LPWSTR value_ = nullptr;
};

bool startsWith(const wchar_t* text, const wchar_t* prefix)
{
    return std::wcsncmp(text, prefix, std::wcslen(prefix)) == 0;
}

bool isTrustedUri(const wchar_t* uri)
{
    return startsWith(uri, DockWindow::appOrigin);
}

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
    return dock;
}

DockWindow::DockWindow(const MonitorInfo& monitor, Callbacks callbacks, Content content)
    : monitor_(monitor), callbacks_(std::move(callbacks)), content_(std::move(content))
{
}

DockWindow::~DockWindow()
{
    alive_.reset();
    if (controller_) {
        controller_->Close();
    }
    webView_.Reset();
    controller_.Reset();
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
        resizeWebView();
        return 0;

    case WM_WINDOWPOSCHANGED:
        if (appBar_) {
            appBar_->onWindowPositionChanged();
        }
        if (controller_) {
            controller_->NotifyParentWindowPositionChanged();
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
    if (!environment || controller_) {
        return;
    }

    std::weak_ptr<bool> alive = alive_;
    const HRESULT hr = environment->CreateCoreWebView2Controller(
        window_, makeCallback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                     [this, alive](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT {
                         if (alive.expired()) {
                             if (controller) {
                                 controller->Close();
                             }
                             return S_OK;
                         }
                         if (FAILED(result) || !controller) {
                             log::error(std::format("cannot create WebView2 controller: 0x{:08X}",
                                                    static_cast<unsigned>(result)));
                             return S_OK;
                         }
                         onControllerCreated(controller);
                         return S_OK;
                     })
                     .Get());
    if (FAILED(hr)) {
        log::error(std::format("CreateCoreWebView2Controller failed: 0x{:08X}", static_cast<unsigned>(hr)));
    }
}

void DockWindow::onControllerCreated(ICoreWebView2Controller* controller)
{
    log::info("WebView2 controller created for " + toUtf8(monitor_.deviceName));
    controller_ = controller;
    if (FAILED(controller_->get_CoreWebView2(&webView_)) || !webView_) {
        log::error("WebView2 controller has no CoreWebView2");
        controller_->Close();
        controller_.Reset();
        return;
    }

    ComPtr<ICoreWebView2Controller2> controller2;
    if (SUCCEEDED(controller_.As(&controller2))) {
        controller2->put_DefaultBackgroundColor(COREWEBVIEW2_COLOR{0, 0, 0, 0});
    }

    configureWebView();
    resizeWebView();
    controller_->put_IsVisible(FALSE);

    std::wstring url = appOrigin;
    url += L"index.html";
    webView_->Navigate(url.c_str());
}

void DockWindow::configureWebView()
{
    ComPtr<ICoreWebView2Settings> settings;
    if (SUCCEEDED(webView_->get_Settings(&settings))) {
#ifdef NDEBUG
        settings->put_AreDevToolsEnabled(FALSE);
#endif
        settings->put_AreDefaultContextMenusEnabled(FALSE);
        settings->put_AreDefaultScriptDialogsEnabled(FALSE);
        settings->put_IsStatusBarEnabled(FALSE);
        settings->put_IsZoomControlEnabled(FALSE);

        ComPtr<ICoreWebView2Settings3> settings3;
        if (SUCCEEDED(settings.As(&settings3))) {
            settings3->put_AreBrowserAcceleratorKeysEnabled(FALSE);  // no reload, print, find, ...
        }
        ComPtr<ICoreWebView2Settings4> settings4;
        if (SUCCEEDED(settings.As(&settings4))) {
            settings4->put_IsPasswordAutosaveEnabled(FALSE);
            settings4->put_IsGeneralAutofillEnabled(FALSE);
        }
        ComPtr<ICoreWebView2Settings5> settings5;
        if (SUCCEEDED(settings.As(&settings5))) {
            settings5->put_IsPinchZoomEnabled(FALSE);
        }
        ComPtr<ICoreWebView2Settings6> settings6;
        if (SUCCEEDED(settings.As(&settings6))) {
            settings6->put_IsSwipeNavigationEnabled(FALSE);
        }
    }

    // Serve the bundled frontend and themes from local folders. Nothing is
    // fetched from the network.
    ComPtr<ICoreWebView2_3> webView3;
    if (SUCCEEDED(webView_.As(&webView3))) {
        log::info("serving " + toUtf8(content_.webDirectory.wstring()) + " as " + toUtf8(appOrigin));
        webView3->SetVirtualHostNameToFolderMapping(L"mdu.example", content_.webDirectory.c_str(),
                                                    COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS);
        webView3->SetVirtualHostNameToFolderMapping(L"themes.mdu.example", content_.themesDirectory.c_str(),
                                                    COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS);
    } else {
        log::error("WebView2 runtime is too old for virtual host mapping");
    }

    EventRegistrationToken token{};
    const auto blockUntrustedNavigation = [this](ICoreWebView2*,
                                                 ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
        CoTaskString uri;
        args->get_Uri(uri.put());
        if (!isTrustedUri(uri.get())) {
            log::warning("blocked navigation to " + toUtf8(uri.get()));
            args->put_Cancel(TRUE);
            ++blockedNavigations_;
        }
        return S_OK;
    };
    webView_->add_NavigationStarting(
        makeCallback<ICoreWebView2NavigationStartingEventHandler>(blockUntrustedNavigation).Get(), &token);
    webView_->add_FrameNavigationStarting(
        makeCallback<ICoreWebView2NavigationStartingEventHandler>(blockUntrustedNavigation).Get(), &token);

    webView_->add_NewWindowRequested(
        makeCallback<ICoreWebView2NewWindowRequestedEventHandler>(
            [](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
                args->put_Handled(TRUE);  // never open popups
                return S_OK;
            })
            .Get(),
        &token);

    webView_->add_PermissionRequested(
        makeCallback<ICoreWebView2PermissionRequestedEventHandler>(
            [](ICoreWebView2*, ICoreWebView2PermissionRequestedEventArgs* args) -> HRESULT {
                args->put_State(COREWEBVIEW2_PERMISSION_STATE_DENY);
                return S_OK;
            })
            .Get(),
        &token);

    webView_->add_WebMessageReceived(
        makeCallback<ICoreWebView2WebMessageReceivedEventHandler>(
            [this](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                onWebMessage(args);
                return S_OK;
            })
            .Get(),
        &token);

    webView_->add_NavigationCompleted(
        makeCallback<ICoreWebView2NavigationCompletedEventHandler>(
            [this](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
                BOOL success = FALSE;
                COREWEBVIEW2_WEB_ERROR_STATUS status{};
                args->get_IsSuccess(&success);
                args->get_WebErrorStatus(&status);
                const auto uri = toUtf8(currentUri());
                if (success) {
                    log::info("dock page loaded: " + uri);
                } else if (status == COREWEBVIEW2_WEB_ERROR_STATUS_OPERATION_CANCELED) {
                    log::info("a navigation was cancelled; the dock page stays at " + uri);
                } else {
                    log::error(std::format("dock page failed to load: {} (web error status {})", uri,
                                           static_cast<int>(status)));
                }
                return S_OK;
            })
            .Get(),
        &token);

    std::weak_ptr<bool> alive = alive_;
    webView_->add_ProcessFailed(
        makeCallback<ICoreWebView2ProcessFailedEventHandler>(
            [this, alive](ICoreWebView2*, ICoreWebView2ProcessFailedEventArgs* args) -> HRESULT {
                if (alive.expired()) {
                    return S_OK;
                }
                COREWEBVIEW2_PROCESS_FAILED_KIND kind{};
                args->get_ProcessFailedKind(&kind);
                log::error(std::format("WebView2 process failed (kind {})", static_cast<int>(kind)));
                if (kind == COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_EXITED ||
                    kind == COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_UNRESPONSIVE) {
                    // The page reloads and sends "ready" again, which re-sends the state.
                    pageReady_ = false;
                    updateVisibility();
                    webView_->Reload();
                    return S_OK;
                }
                if (callbacks_.processFailed) {
                    callbacks_.processFailed(kind);
                }
                return S_OK;
            })
            .Get(),
        &token);
}

void DockWindow::onWebMessage(ICoreWebView2WebMessageReceivedEventArgs* args)
{
    CoTaskString source;
    args->get_Source(source.put());
    if (!isTrustedUri(source.get())) {
        return;
    }

    CoTaskString text;
    if (FAILED(args->TryGetWebMessageAsString(text.put()))) {
        return;  // only JSON strings are part of the protocol
    }

    const auto message = bridge::parseIncoming(toUtf8(text.get()));
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

std::wstring DockWindow::currentUri() const
{
    if (!webView_) {
        return {};
    }
    CoTaskString uri;
    webView_->get_Source(uri.put());
    return uri.get();
}

void DockWindow::executeScript(const std::wstring& script)
{
    if (webView_) {
        webView_->ExecuteScript(script.c_str(), nullptr);
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
        webView_->PostWebMessageAsJson(toWide(state_).c_str());
    }
}

void DockWindow::setBounds(const Rect& bounds)
{
    SetWindowPos(window_, HWND_TOPMOST, bounds.left, bounds.top, bounds.width(), bounds.height(), SWP_NOACTIVATE);
    hasBounds_ = true;
    updateVisibility();
}

void DockWindow::resizeWebView()
{
    if (controller_) {
        RECT client{};
        GetClientRect(window_, &client);
        controller_->put_Bounds(client);
    }
}

void DockWindow::updateVisibility()
{
    const bool visible = pageReady_ && hasBounds_ && !fullscreenHidden_;
    if (controller_) {
        // A hidden WebView2 stops rendering, so a dock behind a full-screen
        // game costs nothing.
        controller_->put_IsVisible(visible ? TRUE : FALSE);
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
