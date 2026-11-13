#pragma once

#include "settings/config.h"
#include "utilities/geometry.h"

namespace mdu {

// Sizes of the dock in DIPs. The window is slightly larger than the visible
// bar so the frontend has room for a soft shadow and a subtle hover lift.
struct DockMetrics {
    int iconSize;
    int padding;
    int gap;
    int shadowInset;

    int barThickness() const { return iconSize + 2 * padding; }
    int windowThickness() const { return barThickness() + 2 * shadowInset; }
    int windowLength(int itemCount) const;
};

DockMetrics dockMetricsFor(int iconSize);

struct DockLayoutInput {
    Rect area;        // work area (overlay) or the negotiated app bar rectangle (reserve)
    unsigned dpi = 96;
    DockEdge edge = DockEdge::Bottom;
    int iconSize = 48;  // DIPs, as configured
    int margin = 4;     // DIPs
    int itemCount = 0;
};

struct DockLayout {
    Rect window;       // physical pixels
    int iconSize = 0;  // DIPs actually used; smaller than configured if the items do not fit
    bool overflow = false;  // true if even the minimum icon size does not fit
};

DockLayout computeDockLayout(const DockLayoutInput& input);

// Thickness in physical pixels that the dock occupies at an edge, including the margin.
// Used to size the app bar reservation.
int dockReservedThickness(unsigned dpi, int iconSize, int margin);

}  // namespace mdu
