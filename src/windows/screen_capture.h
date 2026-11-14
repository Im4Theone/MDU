#pragma once

#include "utilities/geometry.h"
#include "wallpaper/adapt.h"

#include <optional>

namespace mdu {

// Captures what is on screen (DWM-composed, so it includes transparent and
// DirectComposition content) within `area`, in virtual-screen coordinates.
std::optional<wallpaper::Image> captureScreen(const Rect& area);

// The bounding rectangle of all monitors.
Rect virtualScreenBounds();

// Copies a sub-rectangle (in the same coordinates as `origin`) out of a capture.
wallpaper::Image cropImage(const wallpaper::Image& image, const Rect& origin, const Rect& area);

}  // namespace mdu
