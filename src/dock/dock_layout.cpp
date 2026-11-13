#include "dock/dock_layout.h"

#include <algorithm>

namespace mdu {

namespace {

constexpr int minimumIconSize = 24;

}  // namespace

int DockMetrics::windowLength(int itemCount) const
{
    const int slots = std::max(itemCount, 1);
    return slots * iconSize + (slots - 1) * gap + 2 * padding + 2 * shadowInset;
}

DockMetrics dockMetricsFor(int iconSize)
{
    return DockMetrics{
        .iconSize = iconSize,
        .padding = std::max(6, iconSize / 5),
        .gap = std::max(4, iconSize / 6),
        .shadowInset = 8,
    };
}

int dockReservedThickness(unsigned dpi, int iconSize, int margin)
{
    return dipsToPixels(dockMetricsFor(iconSize).windowThickness() + margin, dpi);
}

DockLayout computeDockLayout(const DockLayoutInput& input)
{
    const bool horizontal = input.edge == DockEdge::Bottom;
    const int margin = dipsToPixels(input.margin, input.dpi);
    const int availableLength =
        (horizontal ? input.area.width() : input.area.height()) - 2 * margin;

    // Shrink icons until the dock fits along its edge.
    int iconSize = input.iconSize;
    while (iconSize > minimumIconSize &&
           dipsToPixels(dockMetricsFor(iconSize).windowLength(input.itemCount), input.dpi) >
               availableLength) {
        --iconSize;
    }

    // Thickness follows the configured size so the app bar reservation does not
    // change when items are added or removed.
    const auto metrics = dockMetricsFor(iconSize);
    const int thickness = dipsToPixels(dockMetricsFor(input.iconSize).windowThickness(), input.dpi);
    int length = dipsToPixels(metrics.windowLength(input.itemCount), input.dpi);

    DockLayout layout;
    layout.iconSize = iconSize;
    layout.overflow = length > availableLength;
    length = std::max(1, std::min(length, availableLength));

    const Rect& a = input.area;
    Rect& w = layout.window;
    switch (input.edge) {
    case DockEdge::Bottom:
        w.left = a.left + (a.width() - length) / 2;
        w.right = w.left + length;
        w.bottom = a.bottom - margin;
        w.top = w.bottom - thickness;
        break;
    case DockEdge::Left:
        w.left = a.left + margin;
        w.right = w.left + thickness;
        w.top = a.top + (a.height() - length) / 2;
        w.bottom = w.top + length;
        break;
    case DockEdge::Right:
        w.right = a.right - margin;
        w.left = w.right - thickness;
        w.top = a.top + (a.height() - length) / 2;
        w.bottom = w.top + length;
        break;
    }
    return layout;
}

}  // namespace mdu
