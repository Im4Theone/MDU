#pragma once

#include <WebView2.h>
#include <wrl/client.h>

#include <filesystem>
#include <functional>
#include <vector>

namespace mdu {

// One WebView2 environment (one browser process) shared by every MDU window.
class WebViewEnvironment {
public:
    // Receives the environment, or nullptr if it could not be created
    // (for example when the WebView2 Runtime is missing).
    using ReadyCallback = std::function<void(ICoreWebView2Environment*)>;

    void create(const std::filesystem::path& userDataFolder);
    void whenReady(ReadyCallback callback);
    void reset();

    bool failed() const { return state_ == State::Failed; }
    HRESULT lastError() const { return error_; }

private:
    enum class State { Idle, Creating, Ready, Failed };

    void finish(HRESULT result, ICoreWebView2Environment* environment);

    State state_ = State::Idle;
    HRESULT error_ = S_OK;
    Microsoft::WRL::ComPtr<ICoreWebView2Environment> environment_;
    std::vector<ReadyCallback> pending_;
    unsigned generation_ = 0;  // ignores completions that arrive after reset()
};

}  // namespace mdu
