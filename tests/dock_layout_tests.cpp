#include "dock/dock_layout.h"
#include "test_main.h"

#include <cstdlib>

using namespace mdu;

TEST_CASE("geometry: DIP conversion rounds to nearest pixel")
{
    CHECK_EQ(dipsToPixels(48, 96), 48);
    CHECK_EQ(dipsToPixels(48, 144), 72);   // 150 %
    CHECK_EQ(dipsToPixels(10, 120), 13);   // 125 %: 12.5 rounds up
    CHECK_EQ(dipsToPixels(48, 192), 96);   // 200 %
}

TEST_CASE("dock layout: bottom dock is centred and sits above the area's bottom edge")
{
    const DockLayoutInput input{
        .area = {0, 0, 1920, 1040}, .dpi = 96, .edge = DockEdge::Bottom, .iconSize = 48, .margin = 4, .itemCount = 5};
    const auto layout = computeDockLayout(input);
    const auto metrics = dockMetricsFor(48);

    CHECK_EQ(layout.iconSize, 48);
    CHECK(!layout.overflow);
    CHECK_EQ(layout.window.bottom, 1036);
    CHECK_EQ(layout.window.height(), metrics.windowThickness());
    CHECK_EQ(layout.window.width(), metrics.windowLength(5));
    CHECK_EQ(layout.window.left + layout.window.width() / 2, 960);
}

TEST_CASE("dock layout: secondary monitor with negative coordinates and 150% scaling")
{
    const DockLayoutInput input{.area = {-2560, -200, 0, 1240}, .dpi = 144, .edge = DockEdge::Left,
                                .iconSize = 48, .margin = 4, .itemCount = 3};
    const auto layout = computeDockLayout(input);
    const auto metrics = dockMetricsFor(48);

    CHECK_EQ(layout.window.left, -2560 + 6);
    CHECK_EQ(layout.window.width(), dipsToPixels(metrics.windowThickness(), 144));
    CHECK_EQ(layout.window.height(), dipsToPixels(metrics.windowLength(3), 144));
    CHECK(std::abs(layout.window.top + layout.window.height() / 2 - (-200 + 1240) / 2) <= 1);
}

TEST_CASE("dock layout: right edge")
{
    const DockLayoutInput input{
        .area = {1920, 0, 3840, 1080}, .dpi = 96, .edge = DockEdge::Right, .iconSize = 40, .margin = 0, .itemCount = 2};
    const auto layout = computeDockLayout(input);
    CHECK_EQ(layout.window.right, 3840);
    CHECK(layout.window.left > 1920);
}

TEST_CASE("dock layout: icons shrink when items do not fit, thickness stays constant")
{
    const DockLayoutInput input{
        .area = {0, 0, 800, 600}, .dpi = 96, .edge = DockEdge::Bottom, .iconSize = 64, .margin = 4, .itemCount = 20};
    const auto layout = computeDockLayout(input);
    CHECK(layout.iconSize < 64);
    CHECK(layout.window.width() <= 800 - 8);
    CHECK_EQ(layout.window.height(), dockMetricsFor(64).windowThickness());
}

TEST_CASE("dock layout: overflow is reported and the window is clamped")
{
    const DockLayoutInput input{
        .area = {0, 0, 300, 600}, .dpi = 96, .edge = DockEdge::Bottom, .iconSize = 48, .margin = 4, .itemCount = 40};
    const auto layout = computeDockLayout(input);
    CHECK(layout.overflow);
    CHECK_EQ(layout.iconSize, 24);
    CHECK_EQ(layout.window.width(), 300 - 8);
}

TEST_CASE("dock layout: reserved thickness matches window thickness plus margin")
{
    CHECK_EQ(dockReservedThickness(96, 48, 4), dockMetricsFor(48).windowThickness() + 4);
    CHECK_EQ(dockReservedThickness(192, 48, 4), 2 * (dockMetricsFor(48).windowThickness() + 4));
}
