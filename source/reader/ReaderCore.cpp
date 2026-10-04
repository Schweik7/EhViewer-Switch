#include "ReaderCore.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ehviewer::reader {
namespace {

double Clamp(double value, double minimum, double maximum) noexcept {
    return std::max(minimum, std::min(value, maximum));
}
bool Finite(double value) noexcept { return std::isfinite(value); }
Rect Bounds(Point a, Point b, Point c, Point d) noexcept {
    const double left = std::min(std::min(a.x, b.x), std::min(c.x, d.x));
    const double right = std::max(std::max(a.x, b.x), std::max(c.x, d.x));
    const double top = std::min(std::min(a.y, b.y), std::min(c.y, d.y));
    const double bottom = std::max(std::max(a.y, b.y), std::max(c.y, d.y));
    return {left, top, right - left, bottom - top};
}

}  // namespace

bool Size::IsValid() const noexcept {
    return Finite(width) && Finite(height) && width > 0.0 && height > 0.0;
}
bool Rect::IsValid() const noexcept {
    return Finite(x) && Finite(y) && Finite(width) && Finite(height) &&
           width > 0.0 && height > 0.0;
}
bool Rect::Contains(Point point) const noexcept {
    return IsValid() && Finite(point.x) && Finite(point.y) && point.x >= x && point.y >= y &&
           point.x <= x + width && point.y <= y + height;
}

Size LogicalCanvas(Orientation orientation) noexcept {
    return orientation == Orientation::Landscape
        ? Size{kLandscapeWidth, kLandscapeHeight}
        : Size{kPortraitWidth, kPortraitHeight};
}
PageMode EffectivePageMode(const ReaderState& state) noexcept {
    return state.orientation == Orientation::Landscape ? state.requested_page_mode : PageMode::Single;
}

// Orientation names describe how the user turns the console. Counter-clockwise
// puts the left Joy-Con (D-pad) at the bottom; the page is then drawn rotated
// clockwise on the 1280x720 panel, so the reader's top-left is the panel's
// top-right corner.
Point LogicalToPhysical(Point logical, Orientation orientation, Size physical) noexcept {
    const Size canvas = LogicalCanvas(orientation);
    if (!canvas.IsValid() || !physical.IsValid()) return {};
    if (orientation == Orientation::PortraitCounterClockwise)
        return {(canvas.height - logical.y) * physical.width / canvas.height,
                logical.x * physical.height / canvas.width};
    if (orientation == Orientation::PortraitClockwise)
        return {logical.y * physical.width / canvas.height,
                (canvas.width - logical.x) * physical.height / canvas.width};
    return {logical.x * physical.width / canvas.width, logical.y * physical.height / canvas.height};
}

Point PhysicalToLogical(Point point, Orientation orientation, Size physical) noexcept {
    const Size canvas = LogicalCanvas(orientation);
    if (!canvas.IsValid() || !physical.IsValid()) return {};
    if (orientation == Orientation::PortraitCounterClockwise)
        return {point.y * canvas.width / physical.height,
                canvas.height - point.x * canvas.height / physical.width};
    if (orientation == Orientation::PortraitClockwise)
        return {canvas.width - point.y * canvas.width / physical.height,
                point.x * canvas.height / physical.width};
    return {point.x * canvas.width / physical.width, point.y * canvas.height / physical.height};
}

Rect LogicalRectToPhysical(Rect logical, Orientation orientation, Size physical) noexcept {
    if (!logical.IsValid()) return {};
    return Bounds(LogicalToPhysical({logical.x, logical.y}, orientation, physical),
                  LogicalToPhysical({logical.x + logical.width, logical.y}, orientation, physical),
                  LogicalToPhysical({logical.x, logical.y + logical.height}, orientation, physical),
                  LogicalToPhysical({logical.x + logical.width, logical.y + logical.height},
                                    orientation, physical));
}

Direction PhysicalToLogical(Direction physical, Orientation orientation) noexcept {
    if (physical == Direction::None || orientation == Orientation::Landscape) return physical;
    if (orientation == Orientation::PortraitCounterClockwise) {
        // Console turned left: the D-pad's "up" now points to the user's left.
        switch (physical) {
            case Direction::Up: return Direction::Left;
            case Direction::Right: return Direction::Up;
            case Direction::Down: return Direction::Right;
            case Direction::Left: return Direction::Down;
            default: return Direction::None;
        }
    }
    switch (physical) {
        case Direction::Up: return Direction::Right;
        case Direction::Right: return Direction::Down;
        case Direction::Down: return Direction::Left;
        case Direction::Left: return Direction::Up;
        default: return Direction::None;
    }
}

Point PageTransform::ImageToLogical(Point point) const noexcept {
    return valid ? Point{destination.x + point.x * scale, destination.y + point.y * scale} : Point{};
}
Point PageTransform::LogicalToImage(Point point) const noexcept {
    return valid && scale > 0.0
        ? Point{(point.x - destination.x) / scale, (point.y - destination.y) / scale}
        : Point{};
}

PageTransform CalculatePageTransform(Size image, Rect viewport, ScaleMode mode,
                                     double scroll_x, double scroll_y) noexcept {
    PageTransform result;
    result.image_size = image;
    result.viewport = viewport;
    if (!image.IsValid() || !viewport.IsValid()) return result;
    const double width_scale = viewport.width / image.width;
    const double height_scale = viewport.height / image.height;
    result.scale = mode == ScaleMode::FitWidth ? width_scale : std::min(width_scale, height_scale);
    if (!Finite(result.scale) || result.scale <= 0.0) return result;
    const double width = image.width * result.scale;
    const double height = image.height * result.scale;
    result.max_scroll_x = std::max(0.0, width - viewport.width);
    result.max_scroll_y = std::max(0.0, height - viewport.height);
    result.scroll_x = Clamp(Finite(scroll_x) ? scroll_x : 0.0, 0.0, result.max_scroll_x);
    result.scroll_y = Clamp(Finite(scroll_y) ? scroll_y : 0.0, 0.0, result.max_scroll_y);
    const double x = width <= viewport.width ? viewport.x + (viewport.width - width) * 0.5
                                             : viewport.x - result.scroll_x;
    const double y = height <= viewport.height ? viewport.y + (viewport.height - height) * 0.5
                                               : viewport.y - result.scroll_y;
    result.destination = {x, y, width, height};
    result.valid = true;
    return result;
}

std::vector<LaidOutPage> LayoutVisiblePages(const ReaderState& state,
                                            const std::vector<Size>& sizes, Rect viewport) {
    std::vector<LaidOutPage> result;
    if (sizes.empty() || !viewport.IsValid() || state.current_page >= sizes.size()) return result;
    if (EffectivePageMode(state) == PageMode::Single) {
        result.push_back({state.current_page, CalculatePageTransform(sizes[state.current_page],
            viewport, state.scale_mode, state.scroll_x, state.scroll_y)});
        return result;
    }
    const double gap = Clamp(Finite(state.spread_gap) ? state.spread_gap : 0.0, 0.0, viewport.width);
    const double slot_width = (viewport.width - gap) * 0.5;
    if (slot_width <= 0.0) return result;
    const bool has_second = state.current_page + 1 < sizes.size();
    const double right_x = viewport.x + slot_width + gap;
    if (!has_second) {
        const Rect slot{viewport.x + (viewport.width - slot_width) * 0.5,
                        viewport.y, slot_width, viewport.height};
        result.push_back({state.current_page, CalculatePageTransform(sizes[state.current_page],
            slot, state.scale_mode, state.scroll_x, state.scroll_y)});
        return result;
    }
    const bool rtl = state.reading_direction == ReadingDirection::RightToLeft;
    const Rect first{rtl ? right_x : viewport.x, viewport.y, slot_width, viewport.height};
    const Rect second{rtl ? viewport.x : right_x, viewport.y, slot_width, viewport.height};
    result.push_back({state.current_page, CalculatePageTransform(sizes[state.current_page],
        first, state.scale_mode, state.scroll_x, state.scroll_y)});
    result.push_back({state.current_page + 1, CalculatePageTransform(sizes[state.current_page + 1],
        second, state.scale_mode, state.scroll_x, state.scroll_y)});
    return result;
}

std::vector<PrefetchRequest> BuildPrefetchPlan(std::size_t current, std::size_t count,
                                               std::size_t visible_count,
                                               const PrefetchPolicy& policy) {
    std::vector<PrefetchRequest> result;
    if (count == 0 || current >= count) return result;
    const std::size_t visible = std::min(std::max<std::size_t>(1, visible_count), count - current);
    if (policy.include_visible_pages)
        for (std::size_t i = 0; i < visible; ++i) result.push_back({current + i, PrefetchPriority::Visible});
    const std::size_t ahead = current + visible;
    for (std::size_t i = 0; i < policy.pages_ahead && ahead + i < count; ++i)
        result.push_back({ahead + i, PrefetchPriority::Ahead});
    for (std::size_t i = 1; i <= policy.pages_behind && i <= current; ++i)
        result.push_back({current - i, PrefetchPriority::Behind});
    return result;
}

std::uint64_t MemoryBudget::UsableBytes() const noexcept {
    return reserved_bytes >= limit_bytes ? 0 : limit_bytes - reserved_bytes;
}
bool MemoryBudget::CanAdmit(std::uint64_t resident, std::uint64_t candidate) const noexcept {
    const std::uint64_t usable = UsableBytes();
    return resident <= usable && candidate <= usable - resident;
}
std::uint64_t MemoryBudget::BytesToFree(std::uint64_t resident,
                                        std::uint64_t candidate) const noexcept {
    const std::uint64_t usable = UsableBytes();
    if (resident <= usable && candidate <= usable - resident) return 0;
    if (candidate > std::numeric_limits<std::uint64_t>::max() - resident)
        return std::numeric_limits<std::uint64_t>::max();
    const std::uint64_t wanted = resident + candidate;
    return wanted > usable ? wanted - usable : 0;
}

std::uint64_t EstimateDecodedBytes(std::uint32_t width, std::uint32_t height,
                                   std::uint32_t bpp) noexcept {
    if (width == 0 || height == 0 || bpp == 0) return 0;
    const std::uint64_t row = static_cast<std::uint64_t>(width) * bpp;
    if (row > std::numeric_limits<std::uint64_t>::max() / height)
        return std::numeric_limits<std::uint64_t>::max();
    return row * height;
}

std::vector<PrefetchRequest> LimitPrefetchToBudget(
    const std::vector<PrefetchRequest>& plan,
    const std::vector<PageMemoryEstimate>& estimates,
    const MemoryBudget& budget,
    std::uint64_t resident) {
    std::vector<PrefetchRequest> result;
    for (const auto& request : plan) {
        const auto found = std::find_if(estimates.begin(), estimates.end(), [&](const auto& item) {
            return item.page_index == request.page_index;
        });
        if (found == estimates.end() || !budget.CanAdmit(resident, found->decoded_bytes)) continue;
        result.push_back(request);
        resident += found->decoded_bytes;
    }
    return result;
}

}  // namespace ehviewer::reader
