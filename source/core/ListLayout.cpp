#include "ListLayout.h"

#include <algorithm>

namespace ehviewer {

ItemRect ListGeometry::Item(std::size_t index) const {
    const int column = static_cast<int>(index % static_cast<std::size_t>(columns));
    const int row = static_cast<int>(index / static_cast<std::size_t>(columns));
    return {origin_x + column * (item_width + gap), row * (item_height + gap), item_width, item_height};
}

int ListGeometry::ContentHeight(std::size_t count) const {
    if (count == 0) return 0;
    const int rows = static_cast<int>((count + static_cast<std::size_t>(columns) - 1) / static_cast<std::size_t>(columns));
    return rows * (item_height + gap) - gap;
}

double ListGeometry::MaxScroll(std::size_t count) const {
    return std::max(0, ContentHeight(count) - viewport_height);
}

double ListGeometry::ScrollToShow(std::size_t index, double scroll, std::size_t count) const {
    if (count == 0) return 0.0;
    const ItemRect item = Item(std::min(index, count - 1));
    const double margin = std::min(24, gap * 3);
    double target = scroll;
    if (item.y - margin < target) target = item.y - margin;
    if (item.y + item.height + margin > target + viewport_height)
        target = item.y + item.height + margin - viewport_height;
    return std::max(0.0, std::min(target, MaxScroll(count)));
}

std::size_t ListGeometry::FirstVisible(double scroll, std::size_t count) const {
    if (count == 0) return 0;
    const int row = std::max(0, static_cast<int>(scroll) / (item_height + gap));
    return std::min(count - 1, static_cast<std::size_t>(row) * static_cast<std::size_t>(columns));
}

std::size_t ListGeometry::Move(std::size_t index, std::size_t count, int dx, int dy) const {
    if (count == 0) return 0;
    long next = static_cast<long>(index) + dx + static_cast<long>(dy) * columns;
    // Moving down from a partial last row lands on the last item.
    if (dy > 0 && next >= static_cast<long>(count) &&
        static_cast<long>(index) / columns < static_cast<long>(count - 1) / columns) {
        next = static_cast<long>(count) - 1;
    }
    if (next < 0 || next >= static_cast<long>(count)) return index;
    return static_cast<std::size_t>(next);
}

ListGeometry GalleryListGeometry(int layout) {
    ListGeometry geometry;
    if (layout == 1) {
        geometry.columns = 5;
        geometry.gap = 14;
        geometry.item_width = (1016 - 4 * geometry.gap) / 5;  // 192
        geometry.item_height = 346;
    }
    return geometry;
}

}  // namespace ehviewer
