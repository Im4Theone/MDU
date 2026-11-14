#include "windows/screen_capture.h"

#include <windows.h>

#include <algorithm>
#include <cstring>

namespace mdu {

Rect virtualScreenBounds()
{
    const int left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    return {left, top, left + GetSystemMetrics(SM_CXVIRTUALSCREEN), top + GetSystemMetrics(SM_CYVIRTUALSCREEN)};
}

std::optional<wallpaper::Image> captureScreen(const Rect& area)
{
    if (area.width() <= 0 || area.height() <= 0) {
        return std::nullopt;
    }

    const HDC screen = GetDC(nullptr);
    if (!screen) {
        return std::nullopt;
    }
    const HDC memory = CreateCompatibleDC(screen);

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = area.width();
    info.bmiHeader.biHeight = -area.height();  // top-down rows
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    const HBITMAP bitmap = memory ? CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0) : nullptr;

    std::optional<wallpaper::Image> result;
    if (bitmap && bits) {
        const HGDIOBJ previous = SelectObject(memory, bitmap);
        if (BitBlt(memory, 0, 0, area.width(), area.height(), screen, area.left, area.top, SRCCOPY | CAPTUREBLT)) {
            GdiFlush();
            wallpaper::Image image{area.width(), area.height(),
                                   std::vector<std::uint8_t>(static_cast<std::size_t>(area.width()) * area.height() * 4)};
            std::memcpy(image.pixels.data(), bits, image.pixels.size());
            for (std::size_t i = 3; i < image.pixels.size(); i += 4) {
                image.pixels[i] = 255;  // GDI leaves the alpha byte undefined
            }
            result = std::move(image);
        }
        SelectObject(memory, previous);
    }

    if (bitmap) {
        DeleteObject(bitmap);
    }
    if (memory) {
        DeleteDC(memory);
    }
    ReleaseDC(nullptr, screen);
    return result;
}

wallpaper::Image cropImage(const wallpaper::Image& image, const Rect& origin, const Rect& area)
{
    const int left = std::clamp(area.left - origin.left, 0, image.width);
    const int top = std::clamp(area.top - origin.top, 0, image.height);
    const int right = std::clamp(area.right - origin.left, left, image.width);
    const int bottom = std::clamp(area.bottom - origin.top, top, image.height);

    wallpaper::Image out{right - left, bottom - top,
                         std::vector<std::uint8_t>(static_cast<std::size_t>(right - left) * (bottom - top) * 4)};
    for (int y = 0; y < out.height; ++y) {
        const auto* source = &image.pixels[(static_cast<std::size_t>(top + y) * image.width + left) * 4];
        std::memcpy(&out.pixels[static_cast<std::size_t>(y) * out.width * 4], source,
                    static_cast<std::size_t>(out.width) * 4);
    }
    return out;
}

}  // namespace mdu
