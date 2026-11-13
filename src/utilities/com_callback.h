#pragma once

#include <windows.h>
#include <unknwn.h>
#include <wrl/client.h>

#include <atomic>
#include <functional>
#include <utility>

// Implements a single-method COM callback interface (such as the WebView2
// completion and event handlers) with a lambda. This replaces WRL's Callback()
// with a few lines and keeps the code portable across toolchains.
namespace mdu {

namespace detail {

template <typename Interface, typename... Args>
class CallbackImpl final : public Interface {
public:
    explicit CallbackImpl(std::function<HRESULT(Args...)> function) : function_(std::move(function)) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override
    {
        if (!object) {
            return E_POINTER;
        }
        if (riid == __uuidof(IUnknown) || riid == __uuidof(Interface)) {
            *object = static_cast<Interface*>(this);
            AddRef();
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }

    ULONG STDMETHODCALLTYPE Release() override
    {
        const ULONG remaining = --references_;
        if (remaining == 0) {
            delete this;  // COM reference counting owns this object
        }
        return remaining;
    }

    HRESULT STDMETHODCALLTYPE Invoke(Args... args) override { return function_(args...); }

private:
    std::atomic<ULONG> references_{1};
    std::function<HRESULT(Args...)> function_;
};

template <typename Interface, typename Method>
struct CallbackFor;

template <typename Interface, typename Class, typename... Args>
struct CallbackFor<Interface, HRESULT (STDMETHODCALLTYPE Class::*)(Args...)> {
    using Type = CallbackImpl<Interface, Args...>;
};

}  // namespace detail

template <typename Interface, typename Function>
Microsoft::WRL::ComPtr<Interface> makeCallback(Function&& function)
{
    using Impl = typename detail::CallbackFor<Interface, decltype(&Interface::Invoke)>::Type;
    Microsoft::WRL::ComPtr<Interface> callback;
    callback.Attach(new Impl(std::forward<Function>(function)));
    return callback;
}

}  // namespace mdu
