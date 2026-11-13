#include "wallpaper/adapt.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace mdu::wallpaper {

namespace {

std::uint8_t medianOfHistogram(const std::array<std::uint32_t, 256>& histogram, std::uint32_t count)
{
    const std::uint32_t middle = count / 2;
    std::uint32_t seen = 0;
    for (int value = 0; value < 256; ++value) {
        seen += histogram[static_cast<std::size_t>(value)];
        if (seen > middle) {
            return static_cast<std::uint8_t>(value);
        }
    }
    return 0;
}

double smoothstep(double x)
{
    x = std::clamp(x, 0.0, 1.0);
    return x * x * (3.0 - 2.0 * x);
}

}  // namespace

ThemeColor sampleBorderColor(const Image& image, int borderThickness)
{
    if (image.width <= 0 || image.height <= 0) {
        return {};
    }

    const int border = std::clamp(borderThickness, 1, std::min(image.width, image.height));
    std::array<std::uint32_t, 256> blue{}, green{}, red{};
    std::uint32_t count = 0;

    for (int y = 0; y < image.height; ++y) {
        const bool edgeRow = y < border || y >= image.height - border;
        for (int x = 0; x < image.width; ++x) {
            if (!edgeRow && x >= border && x < image.width - border) {
                x = image.width - border - 1;  // skip the interior of the row
                continue;
            }
            const auto* p = &image.pixels[(static_cast<std::size_t>(y) * image.width + x) * 4];
            ++blue[p[0]];
            ++green[p[1]];
            ++red[p[2]];
            ++count;
        }
    }

    return {medianOfHistogram(red, count), medianOfHistogram(green, count), medianOfHistogram(blue, count)};
}

void featherEdges(Image& image, ThemeColor background, double fraction)
{
    const int span = static_cast<int>(std::lround(std::min(image.width, image.height) * fraction));
    if (span <= 0) {
        return;
    }

    // Weight of the original pixel per distance from an edge; 0 at the edge, 1 at `span`.
    std::vector<double> ramp(static_cast<std::size_t>(span));
    for (int d = 0; d < span; ++d) {
        ramp[static_cast<std::size_t>(d)] = smoothstep((d + 0.5) / span);
    }
    const auto weightAt = [&](int distance) {
        return distance >= span ? 1.0 : ramp[static_cast<std::size_t>(distance)];
    };

    const double bg[3] = {static_cast<double>(background.b), static_cast<double>(background.g),
                          static_cast<double>(background.r)};

    for (int y = 0; y < image.height; ++y) {
        const double wy = weightAt(std::min(y, image.height - 1 - y));
        for (int x = 0; x < image.width; ++x) {
            const double w = wy * weightAt(std::min(x, image.width - 1 - x));
            if (w >= 1.0) {
                // Skip straight to the right-hand border region of this row.
                if (x >= span && x < image.width - span) {
                    x = image.width - span - 1;
                }
                continue;
            }
            auto* p = &image.pixels[(static_cast<std::size_t>(y) * image.width + x) * 4];
            for (int c = 0; c < 3; ++c) {
                const double blended = bg[c] + (p[c] - bg[c]) * w;
                p[c] = static_cast<std::uint8_t>(std::lround(std::clamp(blended, 0.0, 255.0)));
            }
        }
    }
}

}  // namespace mdu::wallpaper
