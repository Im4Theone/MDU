#include "ui/webview_host.h"

#include "utilities/com_callback.h"
#include "utilities/log.h"
#include "utilities/strings.h"

#include <cwchar>
#include <format>

using Microsoft::WRL::ComPtr;

namespace mdu {

namespace {

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

bool isTrustedUri(const wchar_t* uri)
{
    return std::wcsncmp(uri, WebViewHost::appOrigin, std::wcslen(WebViewHost::appOrigin)) == 0;
}

}  // namespace

WebViewHost::WebViewHost(HWND parent, Content content, Options options, Callbacks callbacks)
    : parent_(parent),
      content_(std::move(content)),
      options_(std::move(options)),
      callbacks_(std::move(callbacks)),
      visible_(options_.startVisible)
{
}

WebViewHost::~WebViewHost()
{
    alive_.reset();
    if (controller_) {
        controller_->Close();
    }
}

void WebViewHost::attach(ICoreWebView2Environment* environment)
{
    if (!environment || controller_) {
        return;
    }

    std::weak_ptr<bool> alive = alive_;
    const HRESULT hr = environment->CreateCoreWebView2Controller(
        parent_, makeCallback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                     [this, alive](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT {
                         if (alive.expired()) {
                             if (controller) {
                                 controller->Close();
                             }
                             return S_OK;
                         }
                         if (FAILED(result) || !controller) {
                             log::error(std::format("{}: cannot create WebView2 controller: 0x{:08X}", options_.name,
                                                    static_cast<unsigned>(result)));
                             return S_OK;
                         }
                         onControllerCreated(controller);
                         return S_OK;
                     })
                     .Get());
    if (FAILED(hr)) {
        log::error(std::format("{}: CreateCoreWebView2Controller failed: 0x{:08X}", options_.name,
                               static_cast<unsigned>(hr)));
    }
}

void WebViewHost::onControllerCreated(ICoreWebView2Controller* controller)
{
    log::info(options_.name + ": WebView2 controller created");
    controller_ = controller;
    if (FAILED(controller_->get_CoreWebView2(&webView_)) || !webView_) {
        log::error(options_.name + ": WebView2 controller has no CoreWebView2");
        controller_->Close();
        controller_.Reset();
        return;
    }

    if (options_.transparent) {
        ComPtr<ICoreWebView2Controller2> controller2;
        if (SUCCEEDED(controller_.As(&controller2))) {
            controller2->put_DefaultBackgroundColor(COREWEBVIEW2_COLOR{0, 0, 0, 0});
        }
    }

    configure();
    resizeToParent();
    controller_->put_IsVisible(visible_ ? TRUE : FALSE);

    const std::wstring url = std::wstring(appOrigin) + options_.page;
    webView_->Navigate(url.c_str());
}

void WebViewHost::configure()
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
            log::warning(options_.name + ": blocked navigation to " + toUtf8(uri.get()));
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
                    log::info(options_.name + ": page loaded: " + uri);
                } else if (status == COREWEBVIEW2_WEB_ERROR_STATUS_OPERATION_CANCELED) {
                    log::info(options_.name + ": a navigation was cancelled; the page stays at " + uri);
                } else {
                    log::error(std::format("{}: page failed to load: {} (web error status {})", options_.name, uri,
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
                log::error(std::format("{}: WebView2 process failed (kind {})", options_.name,
                                       static_cast<int>(kind)));
                if (kind == COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_EXITED ||
                    kind == COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_UNRESPONSIVE) {
                    if (callbacks_.pageLost) {
                        callbacks_.pageLost();
                    }
                    webView_->Reload();
                    return S_OK;
                }
                if (callbacks_.browserFailed) {
                    callbacks_.browserFailed(kind);
                }
                return S_OK;
            })
            .Get(),
        &token);
}

void WebViewHost::onWebMessage(ICoreWebView2WebMessageReceivedEventArgs* args)
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
    if (callbacks_.message) {
        callbacks_.message(toUtf8(text.get()));
    }
}

void WebViewHost::resizeToParent()
{
    if (controller_) {
        RECT client{};
        GetClientRect(parent_, &client);
        controller_->put_Bounds(client);
    }
}

void WebViewHost::parentMoved()
{
    if (controller_) {
        controller_->NotifyParentWindowPositionChanged();
    }
}

void WebViewHost::setVisible(bool visible)
{
    visible_ = visible;
    if (controller_) {
        controller_->put_IsVisible(visible ? TRUE : FALSE);
    }
}

void WebViewHost::focus()
{
    if (controller_) {
        controller_->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
    }
}

void WebViewHost::postJson(const std::string& json)
{
    if (webView_) {
        webView_->PostWebMessageAsJson(toWide(json).c_str());
    }
}

void WebViewHost::executeScript(const std::wstring& script)
{
    if (webView_) {
        webView_->ExecuteScript(script.c_str(), nullptr);
    }
}

std::wstring WebViewHost::currentUri() const
{
    if (!webView_) {
        return {};
    }
    CoTaskString uri;
    webView_->get_Source(uri.put());
    return uri.get();
}

}  // namespace mdu
