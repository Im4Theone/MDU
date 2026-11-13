#pragma once

#include "ui/theme.h"

#include <cstdint>
#include <vector>

// Turns a portrait (phone) wallpaper into one that Windows can letterbox
// cleanly on any monitor shape.
//
// The image is never stretched or cropped. Instead its outer edges are blended
// into a flat background colour, and Windows displays it with "Fit" placement
// and the same colour as the desktop background. The result fills a landscape
// monitor with the full, unmodified subject in the centre and seamless flat
// colour on both sides, and works for every aspect ratio and every monitor
// without generating per-monitor images.
namespace mdu::wallpaper {

// Tightly packed 32-bit BGRA (the WIC GUID_WICPixelFormat32bppBGRA layout). Alpha is ignored.
struct Image {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels;
};

// Median colour of the outermost rows and columns. A median is used so parts of
// the subject that touch the frame do not shift the colour.
ThemeColor sampleBorderColor(const Image& image, int borderThickness = 4);

// Blends pixels within `fraction * min(width, height)` of each edge towards
// `background` with a smooth falloff. A fraction of 0 leaves the image untouched.
void featherEdges(Image& image, ThemeColor background, double fraction);

}  // namespace mdu::wallpaper
