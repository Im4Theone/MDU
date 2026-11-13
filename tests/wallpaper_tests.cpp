#include "test_main.h"
#include "wallpaper/adapt.h"

using namespace mdu;
using wallpaper::Image;

namespace {

Image solid(int width, int height, ThemeColor color)
{
    Image image{width, height, std::vector<std::uint8_t>(static_cast<std::size_t>(width) * height * 4)};
    for (std::size_t i = 0; i < image.pixels.size(); i += 4) {
        image.pixels[i] = color.b;
        image.pixels[i + 1] = color.g;
        image.pixels[i + 2] = color.r;
        image.pixels[i + 3] = 255;
    }
    return image;
}

ThemeColor at(const Image& image, int x, int y)
{
    const auto* p = &image.pixels[(static_cast<std::size_t>(y) * image.width + x) * 4];
    return {p[2], p[1], p[0]};
}

void paint(Image& image, int x, int y, ThemeColor color)
{
    auto* p = &image.pixels[(static_cast<std::size_t>(y) * image.width + x) * 4];
    p[0] = color.b;
    p[1] = color.g;
    p[2] = color.r;
}

}  // namespace

TEST_CASE("wallpaper: border sampling ignores a subject touching the frame")
{
    const ThemeColor grey{168, 168, 168};
    auto image = solid(100, 125, grey);
    for (int y = 80; y < 125; ++y) {  // dark "hair" running off the right edge
        for (int x = 85; x < 100; ++x) {
            paint(image, x, y, {20, 30, 20});
        }
    }
    CHECK(wallpaper::sampleBorderColor(image) == grey);
}

TEST_CASE("wallpaper: feathering blends edges and leaves the centre untouched")
{
    const ThemeColor background{168, 168, 168};
    auto image = solid(200, 250, {0, 0, 0});
    wallpaper::featherEdges(image, background, 0.1);  // 20 px

    const auto edge = at(image, 0, 125);
    CHECK(edge.r > 160);  // almost background at the very edge
    const auto halfway = at(image, 10, 125);
    CHECK(halfway.r > 60 && halfway.r < 110);
    CHECK(at(image, 100, 125) == ThemeColor{0, 0, 0});  // centre unchanged
    CHECK(at(image, 25, 125) == ThemeColor{0, 0, 0});   // just past the feather span
    CHECK(at(image, 199, 125).r > 160);                 // right edge
    CHECK(at(image, 100, 249).r > 160);                 // bottom edge
}

TEST_CASE("wallpaper: zero feather is a no-op")
{
    auto image = solid(10, 10, {1, 2, 3});
    const auto copy = image.pixels;
    wallpaper::featherEdges(image, {200, 200, 200}, 0.0);
    CHECK(image.pixels == copy);
}
