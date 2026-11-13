#pragma once

#include "ui/theme.h"

#include <wincodec.h>

#include <filesystem>
#include <optional>
#include <string>

namespace mdu::wallpaper {

struct Request {
    std::filesystem::path image;  // original wallpaper; never modified
    std::optional<ThemeColor> background;
    double feather = 0.04;
};

// Applies an adapted wallpaper through IDesktopWallpaper and restores the
// user's previous wallpaper when MDU's wallpaper is turned off.
class Manager {
public:
    Manager(std::filesystem::path cacheDirectory, std::filesystem::path restoreFile, IWICImagingFactory* factory);

    bool apply(const Request& request, std::string& error);
    // Restores the wallpaper that was active before MDU first changed it.
    // Does nothing if MDU has not changed the wallpaper.
    bool restore(std::string& error);

private:
    struct Prepared {
        std::filesystem::path image;
        ThemeColor background;
    };

    std::optional<Prepared> prepare(const Request& request, std::string& error);
    bool saveCurrentWallpaper(std::string& error);

    std::filesystem::path cacheDirectory_;
    std::filesystem::path restoreFile_;
    IWICImagingFactory* factory_;
};

}  // namespace mdu::wallpaper
