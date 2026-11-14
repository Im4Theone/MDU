#pragma once

#include <windows.h>

#include <optional>
#include <string>

namespace mdu {

struct PickedFile {
    std::wstring path;
    std::wstring displayName;  // as Explorer shows it, without .exe / .lnk
};

// Native Windows file dialogs. They return std::nullopt when cancelled.
std::optional<PickedFile> pickAppOrFile(HWND owner);
std::optional<PickedFile> pickImage(HWND owner);

}  // namespace mdu
