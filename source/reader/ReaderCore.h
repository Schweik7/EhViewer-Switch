#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ehviewer::reader {

constexpr double kLandscapeWidth = 1280.0;
constexpr double kLandscapeHeight = 720.0;
constexpr double kPortraitWidth = 720.0;
constexpr double kPortraitHeight = 1280.0;

struct Point { double x = 0.0; double y = 0.0; };
struct Size {
    double width = 0.0;
    double height = 0.0;
    bool IsValid() const noexcept;
};
struct Rect {
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;
    bool IsValid() const noexcept;
    bool Contains(Point point) const noexcept;
};

enum class Orientation { PortraitCounterClockwise, PortraitClockwise, Landscape };
enum class ScaleMode { FitWidth, FitPage };
enum class PageMode { Single, Double };
enum class ReadingDirection { LeftToRight, RightToLeft };
enum class Direction { None, Up, Right, Down, Left };

struct ReaderState {
    Orientation orientation = Orientation::PortraitCounterClockwise;
    ScaleMode scale_mode = ScaleMode::FitWidth;
    PageMode requested_page_mode = PageMode::Single;
    ReadingDirection reading_direction = ReadingDirection::RightToLeft;
    std::size_t current_page = 0;
    double scroll_x = 0.0;
    double scroll_y = 0.0;
    double spread_gap = 16.0;
};

Size LogicalCanvas(Orientation orientation) noexcept;
PageMode EffectivePageMode(const ReaderState& state) noexcept;
Point LogicalToPhysical(Point logical, Orientation orientation,
                        Size physical = {kLandscapeWidth, kLandscapeHeight}) noexcept;
Point PhysicalToLogical(Point physical_point, Orientation orientation,
                        Size physical = {kLandscapeWidth, kLandscapeHeight}) noexcept;
Rect LogicalRectToPhysical(Rect logical, Orientation orientation,
                           Size physical = {kLandscapeWidth, kLandscapeHeight}) noexcept;
Direction PhysicalToLogical(Direction physical, Orientation orientation) noexcept;

// Tap zones on the logical canvas, as in Android EhViewer's reader guide:
// left third / right third turn pages, the middle top half opens the menu and
// the middle bottom half the progress bar. mirror swaps the page sides (for
// right-to-left double-page spreads).
enum class TapZone { Previous, Next, Menu, Progress };
TapZone ClassifyTap(Point logical, Size canvas, bool mirror) noexcept;

struct PageTransform {
    bool valid = false;
    Size image_size;
    Rect viewport;
    Rect destination;
    double scale = 0.0;
    double scroll_x = 0.0;
    double scroll_y = 0.0;
    double max_scroll_x = 0.0;
    double max_scroll_y = 0.0;

    Point ImageToLogical(Point image_point) const noexcept;
    Point LogicalToImage(Point logical_point) const noexcept;
};

PageTransform CalculatePageTransform(Size image_size, Rect viewport, ScaleMode mode,
                                     double scroll_x = 0.0, double scroll_y = 0.0) noexcept;

struct LaidOutPage {
    std::size_t page_index = 0;
    PageTransform transform;
};

std::vector<LaidOutPage> LayoutVisiblePages(const ReaderState& state,
                                            const std::vector<Size>& page_sizes,
                                            Rect viewport);

enum class PrefetchPriority { Visible, Ahead, Behind };
struct PrefetchRequest {
    std::size_t page_index = 0;
    PrefetchPriority priority = PrefetchPriority::Ahead;
};
struct PrefetchPolicy {
    std::size_t pages_ahead = 3;
    std::size_t pages_behind = 1;
    bool include_visible_pages = true;
};

std::vector<PrefetchRequest> BuildPrefetchPlan(std::size_t current_page,
                                               std::size_t page_count,
                                               std::size_t visible_page_count,
                                               const PrefetchPolicy& policy = {});

struct MemoryBudget {
    std::uint64_t limit_bytes = 256ULL * 1024ULL * 1024ULL;
    std::uint64_t reserved_bytes = 64ULL * 1024ULL * 1024ULL;
    std::uint64_t UsableBytes() const noexcept;
    bool CanAdmit(std::uint64_t resident, std::uint64_t candidate) const noexcept;
    std::uint64_t BytesToFree(std::uint64_t resident, std::uint64_t candidate) const noexcept;
};

std::uint64_t EstimateDecodedBytes(std::uint32_t width, std::uint32_t height,
                                   std::uint32_t bytes_per_pixel = 4) noexcept;
struct PageMemoryEstimate {
    std::size_t page_index = 0;
    std::uint64_t decoded_bytes = 0;
};
std::vector<PrefetchRequest> LimitPrefetchToBudget(
    const std::vector<PrefetchRequest>& plan,
    const std::vector<PageMemoryEstimate>& estimates,
    const MemoryBudget& budget,
    std::uint64_t resident_bytes);

}  // namespace ehviewer::reader
