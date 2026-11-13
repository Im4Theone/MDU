#pragma once

namespace mdu {

// Physical-pixel rectangle in virtual-screen coordinates, same convention as
// Win32 RECT (right and bottom are exclusive).
struct Rect {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;

    constexpr int width() const { return right - left; }
    constexpr int height() const { return bottom - top; }
    constexpr bool operator==(const Rect&) const = default;
};

// Converts device-independent pixels (1/96 inch) to physical pixels.
constexpr int dipsToPixels(int dips, unsigned dpi)
{
    const long long scaled = static_cast<long long>(dips) * dpi;
    return static_cast<int>(scaled >= 0 ? (scaled + 48) / 96 : (scaled - 48) / 96);
}

}  // namespace mdu
