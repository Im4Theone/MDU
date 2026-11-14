#pragma once

#include "wallpaper/adapt.h"

#include <windows.h>
#include <wincodec.h>

#include <filesystem>
#include <optional>
#include <string>

// Image file I/O through WIC, using the BGRA buffer type of the portable core.
namespace mdu {

std::optional<wallpaper::Image> loadImage(IWICImagingFactory* factory, const std::filesystem::path& path,
                                          std::string& error);
bool savePng(IWICImagingFactory* factory, const wallpaper::Image& image, const std::filesystem::path& path,
             std::string& error);

}  // namespace mdu
