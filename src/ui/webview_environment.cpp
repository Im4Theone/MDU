#include "ui/webview_environment.h"

#include "utilities/com_callback.h"
#include "utilities/log.h"

#include <WebView2EnvironmentOptions.h>

#include <format>

using Microsoft::WRL::ComPtr;

namespace mdu {

void WebViewEnvironment::create(const std::filesystem::path& userDataFolder)
{
    if (state_ == State::Creating || state_ == State::Ready) {
        return;
    }
    state_ = State::Creating;
    const unsigned generation = ++generation_;

    auto options = Microsoft::WRL::Make<CoreWebView2EnvironmentOptions>();
    // Keep crash reports local instead of sending them to Microsoft.
    ComPtr<ICoreWebView2EnvironmentOptions3> options3;
    if (options && SUCCEEDED(options.As(&options3))) {
        options3->put_IsCustomCrashReportingEnabled(TRUE);
    }

    const HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
        nullptr, userDataFolder.c_str(), options.Get(),
        makeCallback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [this, generation](HRESULT result, ICoreWebView2Environment* environment) -> HRESULT {
                if (generation == generation_) {
                    finish(result, environment);
                }
                return S_OK;
            })
            .Get());
    if (FAILED(hr)) {
        finish(hr, nullptr);
    }
}

void WebViewEnvironment::finish(HRESULT result, ICoreWebView2Environment* environment)
{
    if (SUCCEEDED(result) && environment) {
        state_ = State::Ready;
        environment_ = environment;
        LPWSTR version = nullptr;
        if (SUCCEEDED(environment->get_BrowserVersionString(&version)) && version) {
            std::string text;
            for (const wchar_t* c = version; *c; ++c) {
                text += static_cast<char>(*c < 128 ? *c : '?');
            }
            log::info("WebView2 runtime " + text);
        }
        CoTaskMemFree(version);
    } else {
        state_ = State::Failed;
        error_ = FAILED(result) ? result : E_FAIL;
        log::error(std::format("WebView2 environment creation failed: 0x{:08X}", static_cast<unsigned>(error_)));
    }

    auto callbacks = std::move(pending_);
    pending_.clear();
    for (auto& callback : callbacks) {
        callback(environment_.Get());
    }
}

void WebViewEnvironment::whenReady(ReadyCallback callback)
{
    switch (state_) {
    case State::Ready:
        callback(environment_.Get());
        break;
    case State::Failed:
        callback(nullptr);
        break;
    case State::Idle:
    case State::Creating:
        pending_.push_back(std::move(callback));
        break;
    }
}

void WebViewEnvironment::reset()
{
    ++generation_;
    environment_.Reset();
    pending_.clear();
    state_ = State::Idle;
    error_ = S_OK;
}

}  // namespace mdu
