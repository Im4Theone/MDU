#pragma once

#include <filesystem>
#include <optional>

namespace mdu {

struct AppPaths {
    std::filesystem::path installDirectory;  // folder containing mdu.exe
    std::filesystem::path webDirectory;      // frontend files
    std::filesystem::path themesDirectory;   // bundled themes

    std::filesystem::path dataDirectory;  // %LOCALAPPDATA%\MDU
    std::filesystem::path configFile;
    std::filesystem::path crashStateFile;
    std::filesystem::path wallpaperRestoreFile;
    std::filesystem::path cacheDirectory;
    std::filesystem::path webViewDataDirectory;
    std::filesystem::path logFile;
};

// Everything lives under the per-user local application data folder, so normal
// operation never needs administrator rights. `dataOverride`, if not empty,
// replaces that folder (used for isolated test runs).
std::optional<AppPaths> resolveAppPaths(const std::filesystem::path& dataOverride = {});

std::filesystem::path executablePath();

}  // namespace mdu
