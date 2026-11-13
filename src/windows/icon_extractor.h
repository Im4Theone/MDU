#pragma once

#include <wincodec.h>

#include <string>

namespace mdu {

// Returns the shell icon for a file, folder or shortcut as a PNG data: URL, or
// an empty string if the shell has no icon for it (the frontend then draws a
// fallback).
std::string extractIconDataUrl(IWICImagingFactory* factory, const std::wstring& target, int sizePixels);

}  // namespace mdu
