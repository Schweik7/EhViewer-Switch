#pragma once

#include <cstddef>

namespace ehviewer {

struct ItemRect {
    int x = 0;
    int y = 0;  // relative to the top of the scrolled content
    int width = 0;
    int height = 0;
};

// Geometry of the gallery list: one column of rows or a thumbnail grid. The
// list scrolls by pixels inside a fixed viewport on screen.
struct ListGeometry {
    int columns = 1;
    int item_width = 1016;
    int item_height = 84;
    int gap = 6;
    int origin_x = 240;
    int viewport_top = 104;
    int viewport_height = 538;

    ItemRect Item(std::size_t index) const;
    int ContentHeight(std::size_t count) const;
    double MaxScroll(std::size_t count) const;
    // Smallest scroll change that shows the whole item (with a small margin).
    double ScrollToShow(std::size_t index, double scroll, std::size_t count) const;
    // Index of the first item whose row is at least partly visible.
    std::size_t FirstVisible(double scroll, std::size_t count) const;
    // Index moved by the D-pad: +/-1 sideways, +/-columns vertically.
    std::size_t Move(std::size_t index, std::size_t count, int dx, int dy) const;
};

// layout: 0 = rows, 1 = thumbnail grid.
ListGeometry GalleryListGeometry(int layout);

}  // namespace ehviewer
