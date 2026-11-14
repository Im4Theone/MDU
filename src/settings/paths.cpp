#include "settings/paths.h"

#include <windows.h>
#include <knownfolders.h>
#include <shlobj.h>

#include <string>

namespace mdu {

std::filesystem::path executablePath()
{
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            return {};
        }
        if (length < buffer.size()) {
            buffer.resize(length);
            return buffer;
        }
        buffer.resize(buffer.size() * 2);
    }
}

namespace {

std::filesystem::path localAppDataFolder()
{
    PWSTR localAppData = nullptr;
    const HRESULT hr = SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &localAppData);
    std::filesystem::path result;
    if (SUCCEEDED(hr)) {
        result = std::filesystem::path(localAppData) / L"MDU";
    }
    CoTaskMemFree(localAppData);
    return result;
}

}  // namespace

std::optional<AppPaths> resolveAppPaths(const std::filesystem::path& dataOverride)
{
    const std::filesystem::path base =
        dataOverride.empty() ? localAppDataFolder() : std::filesystem::absolute(dataOverride);
    if (base.empty()) {
        return std::nullopt;
    }

    const auto exe = executablePath();
    if (exe.empty()) {
        return std::nullopt;
    }

    AppPaths paths;
    paths.installDirectory = exe.parent_path();
    paths.webDirectory = paths.installDirectory / L"web";
    paths.themesDirectory = paths.installDirectory / L"themes";
    paths.dataDirectory = base;
    paths.configFile = base / L"config.json";
    paths.crashStateFile = base / L"state" / L"session.json";
    paths.wallpaperRestoreFile = base / L"state" / L"wallpaper-restore.json";
    paths.cacheDirectory = base / L"cache";
    paths.webViewDataDirectory = base / L"WebView2";
    paths.logFile = base / L"logs" / L"mdu.log";

    std::error_code ec;
    std::filesystem::create_directories(paths.dataDirectory, ec);
    if (ec) {
        return std::nullopt;
    }
    return paths;
}

}  // namespace mdu
