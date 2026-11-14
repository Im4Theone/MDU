#include "windows/file_picker.h"

#include <shobjidl.h>
#include <wrl/client.h>

#include <algorithm>
#include <cwctype>
#include <span>

using Microsoft::WRL::ComPtr;

namespace mdu {

namespace {

std::wstring displayNameFor(IShellItem* item, const std::wstring& path)
{
    std::wstring name;
    LPWSTR text = nullptr;
    if (SUCCEEDED(item->GetDisplayName(SIGDN_NORMALDISPLAY, &text)) && text) {
        name = text;
    }
    CoTaskMemFree(text);
    if (name.empty()) {
        name = path.substr(path.find_last_of(L"\\/") + 1);
    }

    // Explorer may show extensions; a dock label should not.
    for (const std::wstring_view extension : {L".exe", L".lnk", L".url"}) {
        if (name.size() > extension.size()) {
            auto tail = name.substr(name.size() - extension.size());
            std::ranges::transform(tail, tail.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
            if (tail == extension) {
                name.resize(name.size() - extension.size());
                break;
            }
        }
    }
    return name;
}

std::optional<PickedFile> pick(HWND owner, const wchar_t* title, std::span<const COMDLG_FILTERSPEC> filters,
                               FILEOPENDIALOGOPTIONS extraOptions)
{
    ComPtr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
        return std::nullopt;
    }

    FILEOPENDIALOGOPTIONS options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST | extraOptions);
    dialog->SetTitle(title);
    dialog->SetFileTypes(static_cast<UINT>(filters.size()), filters.data());

    if (FAILED(dialog->Show(owner))) {
        return std::nullopt;  // cancelled or failed
    }

    ComPtr<IShellItem> item;
    LPWSTR path = nullptr;
    if (FAILED(dialog->GetResult(&item)) || FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) || !path) {
        CoTaskMemFree(path);
        return std::nullopt;
    }
    PickedFile result{path, {}};
    CoTaskMemFree(path);
    result.displayName = displayNameFor(item.Get(), result.path);
    return result;
}

}  // namespace

std::optional<PickedFile> pickAppOrFile(HWND owner)
{
    static constexpr COMDLG_FILTERSPEC filters[] = {
        {L"Apps and shortcuts", L"*.exe;*.lnk;*.url"},
        {L"All files", L"*.*"},
    };
    // Keep shortcuts as shortcuts instead of resolving them to their target.
    return pick(owner, L"Add to the dock", filters, FOS_NODEREFERENCELINKS);
}

std::optional<PickedFile> pickImage(HWND owner)
{
    static constexpr COMDLG_FILTERSPEC filters[] = {
        {L"Images", L"*.jpg;*.jpeg;*.png;*.bmp"},
    };
    return pick(owner, L"Choose a wallpaper image", filters, 0);
}

}  // namespace mdu
