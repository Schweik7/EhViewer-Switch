#include "ReaderView.h"

#include "Ui.h"
#include "core/I18n.h"
#include "core/Library.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <switch.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <utility>

namespace ehviewer {
using i18n::T;
namespace {

using reader::Direction;
using reader::Orientation;

constexpr double kHeldScrollStep = 26.0;
constexpr unsigned kHintMilliseconds = 4000;
constexpr unsigned kMissingRetryMilliseconds = 1500;

struct PhysicalButton {
    std::uint64_t mask;
    Direction direction;
};

// D-pad and left stick turn pages or scroll by a screen; the right stick
// scrolls continuously.
constexpr PhysicalButton kPressDirections[] = {
    {HidNpadButton_Up | HidNpadButton_StickLUp, Direction::Up},
    {HidNpadButton_Right | HidNpadButton_StickLRight, Direction::Right},
    {HidNpadButton_Down | HidNpadButton_StickLDown, Direction::Down},
    {HidNpadButton_Left | HidNpadButton_StickLLeft, Direction::Left},
};
constexpr PhysicalButton kHeldDirections[] = {
    {HidNpadButton_StickRUp, Direction::Up},
    {HidNpadButton_StickRRight, Direction::Right},
    {HidNpadButton_StickRDown, Direction::Down},
    {HidNpadButton_StickRLeft, Direction::Left},
};

const char* OrientationLabel(Orientation orientation) {
    switch (orientation) {
        case Orientation::PortraitCounterClockwise: return T("竖屏（十字键在下）");
        case Orientation::PortraitClockwise: return T("竖屏（十字键在上）");
        default: return T("横屏");
    }
}

SDL_Surface* PrepareSurface(SDL_Surface* loaded, int max_size) {
    SDL_Surface* converted = SDL_ConvertSurfaceFormat(loaded, SDL_PIXELFORMAT_ABGR8888, 0);
    SDL_FreeSurface(loaded);
    if (converted == nullptr) return nullptr;
    const double scale = std::min({1.0, static_cast<double>(max_size) / converted->w,
                                   static_cast<double>(max_size) / converted->h});
    if (scale >= 1.0) return converted;
    // Only very large originals reach this path; GPU filtering handles the
    // common downscale at draw time.
    const int width = std::max(1, static_cast<int>(converted->w * scale));
    const int height = std::max(1, static_cast<int>(converted->h * scale));
    SDL_Surface* scaled = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_ABGR8888);
    if (scaled != nullptr) {
        SDL_SetSurfaceBlendMode(converted, SDL_BLENDMODE_NONE);
        if (SDL_BlitScaled(converted, nullptr, scaled, nullptr) != 0) {
            SDL_FreeSurface(scaled);
            scaled = nullptr;
        }
    }
    SDL_FreeSurface(converted);
    return scaled;
}

}  // namespace

ReaderView::ReaderView(Ui* ui) : ui_(ui) {}

ReaderView::~ReaderView() { Close(); }

bool ReaderView::Open(const std::string& title, std::vector<std::string> directories,
                      int page_count, int start_page, std::string* error) {
    Close();
    if (page_count <= 0 || directories.empty()) {
        if (error) *error = T("这个图库没有可阅读的页面");
        return false;
    }
    SDL_RendererInfo info{};
    if (SDL_GetRendererInfo(ui_->renderer_, &info) == 0 && info.max_texture_width > 0)
        max_texture_size_ = std::min(4096, std::min(info.max_texture_width, info.max_texture_height));

    title_ = title;
    directories_ = std::move(directories);
    page_count_ = static_cast<std::size_t>(page_count);
    state_ = defaults_;
    state_.current_page = static_cast<std::size_t>(std::max(0, std::min(start_page, page_count - 1)));
    failed_.clear();
    missing_.clear();
    hint_until_ = SDL_GetTicks() + kHintMilliseconds;
    last_hint_tick_ = 0;
    last_turn_tick_ = SDL_GetTicks();
    toast_until_ = 0;
    action_ = Action::None;
    overlay_ = options_.show_guide ? Overlay::Guide1 : Overlay::None;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = false;
        wanted_.clear();
        in_flight_.clear();
    }
    worker_ = std::thread(&ReaderView::WorkerLoop, this);
    open_ = true;
    SchedulePrefetch();
    return true;
}

void ReaderView::Close() {
    StopWorker();
    ReleaseTextures();
    if (canvas_ != nullptr) SDL_DestroyTexture(canvas_);
    canvas_ = nullptr;
    directories_.clear();
    page_count_ = 0;
    overlay_ = Overlay::None;
    open_ = false;
}

void ReaderView::StopWorker() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
        wanted_.clear();
    }
    wake_.notify_all();
    if (worker_.joinable()) worker_.join();
    std::lock_guard<std::mutex> lock(mutex_);
    for (Decoded& decoded : decoded_) {
        if (decoded.surface != nullptr) SDL_FreeSurface(decoded.surface);
    }
    decoded_.clear();
    in_flight_.clear();
}

void ReaderView::ReleaseTextures() {
    for (auto& entry : textures_) {
        if (entry.second.texture != nullptr) SDL_DestroyTexture(entry.second.texture);
    }
    textures_.clear();
}

void ReaderView::WorkerLoop() {
    for (;;) {
        std::size_t page = 0;
        std::vector<std::string> directories;
        int max_size = 0;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [this] { return stopping_ || !wanted_.empty(); });
            if (stopping_) return;
            page = wanted_.front();
            wanted_.erase(wanted_.begin());
            if (in_flight_.count(page) != 0) continue;
            in_flight_.insert(page);
            directories = directories_;
            max_size = max_texture_size_;
        }

        Decoded decoded;
        decoded.page = page;
        // Pages are looked up on demand so a gallery can be read while the
        // downloader is still writing (and later publishing) it.
        const std::string path = FindPageFile(directories, static_cast<int>(page));
        if (path.empty()) {
            decoded.missing = true;
        } else {
            SDL_Surface* loaded = IMG_Load(path.c_str());
            decoded.surface = loaded == nullptr ? nullptr : PrepareSurface(loaded, max_size);
            decoded.failed = decoded.surface == nullptr;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        in_flight_.erase(page);
        if (stopping_) {
            if (decoded.surface != nullptr) SDL_FreeSurface(decoded.surface);
            return;
        }
        decoded_.push_back(decoded);
    }
}

void ReaderView::SchedulePrefetch() {
    if (!open_) return;
    const std::size_t visible = reader::EffectivePageMode(state_) == reader::PageMode::Double ? 2 : 1;
    const std::vector<reader::PrefetchRequest> plan =
        reader::BuildPrefetchPlan(state_.current_page, page_count_, visible, prefetch_policy_);

    // Evict pages outside the prefetch window, then the farthest pages until
    // the resident decoded size fits the reader memory budget.
    std::set<std::size_t> keep;
    for (const auto& request : plan) keep.insert(request.page_index);
    for (auto it = textures_.begin(); it != textures_.end();) {
        if (keep.count(it->first) == 0) {
            SDL_DestroyTexture(it->second.texture);
            it = textures_.erase(it);
        } else {
            ++it;
        }
    }
    const reader::MemoryBudget budget;
    std::uint64_t resident = 0;
    for (const auto& entry : textures_) resident += entry.second.bytes;
    for (auto request = plan.rbegin(); request != plan.rend() && !budget.CanAdmit(resident, 0); ++request) {
        if (request->priority == reader::PrefetchPriority::Visible) break;
        const auto found = textures_.find(request->page_index);
        if (found == textures_.end()) continue;
        resident -= found->second.bytes;
        SDL_DestroyTexture(found->second.texture);
        textures_.erase(found);
        keep.erase(request->page_index);
    }

    std::vector<std::size_t> wanted;
    const unsigned now = SDL_GetTicks();
    for (const auto& request : plan) {
        const std::size_t page = request.page_index;
        const auto missing = missing_.find(page);
        if (missing != missing_.end() && now < missing->second) continue;
        if (keep.count(page) != 0 && textures_.count(page) == 0 && failed_.count(page) == 0)
            wanted.push_back(page);
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        wanted_ = std::move(wanted);
    }
    wake_.notify_one();
}

bool ReaderView::Update() {
    if (!open_) return false;
    bool dirty = false;
    std::deque<Decoded> ready;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ready.swap(decoded_);
    }
    for (Decoded& decoded : ready) {
        const std::size_t distance = decoded.page > state_.current_page
            ? decoded.page - state_.current_page : state_.current_page - decoded.page;
        if (decoded.missing) {
            missing_[decoded.page] = SDL_GetTicks() + kMissingRetryMilliseconds;
            continue;
        }
        missing_.erase(decoded.page);
        if (decoded.failed) {
            failed_.insert(decoded.page);
            dirty = dirty || distance <= 1;
            continue;
        }
        if (distance <= 4 && textures_.count(decoded.page) == 0) {
            SDL_Texture* texture = SDL_CreateTextureFromSurface(ui_->renderer_, decoded.surface);
            if (texture != nullptr) {
                textures_[decoded.page] = {texture, decoded.surface->w, decoded.surface->h,
                    reader::EstimateDecodedBytes(static_cast<std::uint32_t>(decoded.surface->w),
                                                 static_cast<std::uint32_t>(decoded.surface->h))};
                dirty = dirty || distance <= 1;
            } else {
                failed_.insert(decoded.page);
            }
        }
        SDL_FreeSurface(decoded.surface);
    }
    const unsigned now = SDL_GetTicks();
    // Pages that were not downloaded yet are looked up again periodically.
    bool retry = false;
    for (const auto& entry : missing_) {
        const std::size_t distance = entry.first > state_.current_page
            ? entry.first - state_.current_page : state_.current_page - entry.first;
        if (distance <= 4 && now >= entry.second) retry = true;
    }
    if (!ready.empty() || retry) SchedulePrefetch();
    // Keep the waiting spinner moving while the visible page is not loaded.
    if (textures_.count(state_.current_page) == 0 && failed_.count(state_.current_page) == 0 &&
        now - last_spinner_tick_ >= 120) {
        last_spinner_tick_ = now;
        dirty = true;
    }

    if (hint_until_ != 0 && now >= hint_until_ && last_hint_tick_ < hint_until_) {
        last_hint_tick_ = now;
        dirty = true;
    }
    if (toast_until_ != 0 && now >= toast_until_) {
        toast_until_ = 0;
        dirty = true;
    }
    // Auto page turning waits for the page to be on screen and pauses while
    // a menu is open.
    if (options_.auto_page_seconds > 0 && overlay_ == Overlay::None &&
        textures_.count(state_.current_page) != 0 &&
        now - last_turn_tick_ >= static_cast<unsigned>(options_.auto_page_seconds) * 1000U) {
        const reader::PageTransform transform = CurrentTransform();
        if (transform.valid && transform.scroll_y < transform.max_scroll_y - 0.5)
            Scroll(reader::LogicalCanvas(state_.orientation).height * 0.75);
        else if (!NextPage())
            ShowToast(T("已到最后一页"));
        last_turn_tick_ = now;
        dirty = true;
    }
    // Clock and battery in the status bar.
    const std::time_t clock = std::time(nullptr);
    const std::tm* local = std::localtime(&clock);
    const int minute = local != nullptr ? local->tm_hour * 60 + local->tm_min : -1;
    if (minute != last_minute_) {
        last_minute_ = minute;
        dirty = true;
    }
    if (options_.show_battery && (battery_tick_ == 0 || now - battery_tick_ > 30000)) {
        battery_tick_ = now;
        u32 percent = 0;
        const int previous = battery_percent_;
        battery_percent_ = R_SUCCEEDED(psmGetBatteryChargePercentage(&percent)) ? static_cast<int>(percent) : -1;
        dirty = dirty || previous != battery_percent_;
    }
    return dirty;
}

reader::PageTransform ReaderView::CurrentTransform() const {
    const reader::Size canvas = reader::LogicalCanvas(state_.orientation);
    std::vector<reader::Size> sizes(page_count_, canvas);
    for (const auto& entry : textures_) {
        sizes[entry.first] = {static_cast<double>(entry.second.width),
                              static_cast<double>(entry.second.height)};
    }
    const auto layout = reader::LayoutVisiblePages(state_, sizes, {0.0, 0.0, canvas.width, canvas.height});
    return layout.empty() ? reader::PageTransform() : layout.front().transform;
}

void ReaderView::GoToPage(std::size_t page, bool scroll_to_end) {
    state_.current_page = page;
    last_turn_tick_ = SDL_GetTicks();
    state_.scroll_x = 0.0;
    state_.scroll_y = scroll_to_end ? 1e9 : 0.0;
    SchedulePrefetch();
}

bool ReaderView::Scroll(double delta) {
    const reader::PageTransform transform = CurrentTransform();
    if (!transform.valid || textures_.count(state_.current_page) == 0) return false;
    const double next = std::max(0.0, std::min(transform.scroll_y + delta, transform.max_scroll_y));
    const bool changed = std::fabs(next - transform.scroll_y) > 0.01 ||
                         std::fabs(state_.scroll_y - transform.scroll_y) > 0.01;
    state_.scroll_y = next;
    return changed;
}

void ReaderView::Configure(Orientation orientation, bool fit_width, bool double_page,
                           bool right_to_left, int prefetch_pages) {
    defaults_ = reader::ReaderState();
    defaults_.orientation = orientation;
    defaults_.scale_mode = fit_width ? reader::ScaleMode::FitWidth : reader::ScaleMode::FitPage;
    defaults_.requested_page_mode = double_page ? reader::PageMode::Double : reader::PageMode::Single;
    defaults_.reading_direction = right_to_left ? reader::ReadingDirection::RightToLeft
                                                : reader::ReadingDirection::LeftToRight;
    prefetch_policy_.pages_ahead = static_cast<std::size_t>(std::max(1, std::min(prefetch_pages, 8)));
}

void ReaderView::JumpTo(int page) {
    if (!open_ || page_count_ == 0) return;
    GoToPage(static_cast<std::size_t>(std::max(0, std::min(page, static_cast<int>(page_count_) - 1))), false);
}

bool ReaderView::NextPage() {
    const std::size_t step = reader::EffectivePageMode(state_) == reader::PageMode::Double ? 2 : 1;
    if (state_.current_page + step >= page_count_) return false;
    GoToPage(state_.current_page + step, false);
    return true;
}

bool ReaderView::PreviousPage() {
    const std::size_t step = reader::EffectivePageMode(state_) == reader::PageMode::Double ? 2 : 1;
    if (state_.current_page == 0) return false;
    GoToPage(state_.current_page >= step ? state_.current_page - step : 0, false);
    return true;
}

bool ReaderView::MirroredTaps() const {
    // In right-to-left spreads the left side is the next page.
    return state_.orientation == Orientation::Landscape &&
           state_.reading_direction == reader::ReadingDirection::RightToLeft &&
           reader::EffectivePageMode(state_) == reader::PageMode::Double;
}

bool ReaderView::Tap(int x, int y) {
    if (!open_) return false;
    const reader::Point logical = reader::PhysicalToLogical(
        {static_cast<double>(x), static_cast<double>(y)}, state_.orientation);
    if (overlay_ != Overlay::None) return TapOverlay(logical);
    switch (reader::ClassifyTap(logical, reader::LogicalCanvas(state_.orientation), MirroredTaps())) {
        case reader::TapZone::Previous: return PreviousPage();
        case reader::TapZone::Next: return NextPage();
        case reader::TapZone::Menu: OpenOverlay(Overlay::Menu); return true;
        case reader::TapZone::Progress: OpenOverlay(Overlay::Progress); return true;
    }
    return false;
}

bool ReaderView::LongPress(int x, int y) {
    if (!open_ || overlay_ != Overlay::None) return false;
    const reader::Point logical = reader::PhysicalToLogical(
        {static_cast<double>(x), static_cast<double>(y)}, state_.orientation);
    // In a double-page spread the half that was pressed picks the page.
    std::size_t page = state_.current_page;
    if (reader::EffectivePageMode(state_) == reader::PageMode::Double && page + 1 < page_count_) {
        const bool right_half = logical.x > reader::LogicalCanvas(state_.orientation).width / 2.0;
        const bool rtl = state_.reading_direction == reader::ReadingDirection::RightToLeft;
        if (right_half != rtl) ++page;
    }
    OpenOverlay(Overlay::PageMenu, static_cast<int>(page));
    return true;
}

bool ReaderView::Drag(int dx, int dy, int x, int y) {
    if (!open_) return false;
    if (overlay_ == Overlay::Progress)
        return SetProgressFromPoint(reader::PhysicalToLogical(
            {static_cast<double>(x), static_cast<double>(y)}, state_.orientation));
    if (overlay_ != Overlay::None) return false;
    const reader::Point origin = reader::PhysicalToLogical({640.0, 360.0}, state_.orientation);
    const reader::Point moved = reader::PhysicalToLogical(
        {640.0 + dx, 360.0 + dy}, state_.orientation);
    // Content follows the finger, so scrolling goes the opposite way.
    return Scroll(-(moved.y - origin.y));
}
bool ReaderView::HandleInput(std::uint64_t down, std::uint64_t held) {
    if (!open_ || page_count_ == 0) return false;
    if (overlay_ != Overlay::None) return HandleOverlayInput(down);
    bool changed = false;
    const std::size_t step = reader::EffectivePageMode(state_) == reader::PageMode::Double ? 2 : 1;
    const auto next = [&]() {
        if (state_.current_page + step < page_count_) {
            GoToPage(state_.current_page + step, false);
            changed = true;
        }
    };
    const auto previous = [&](bool scroll_to_end) {
        if (state_.current_page > 0) {
            GoToPage(state_.current_page >= step ? state_.current_page - step : 0, scroll_to_end);
            changed = true;
        }
    };
    const double screen = reader::LogicalCanvas(state_.orientation).height * 0.75;

    for (const PhysicalButton& button : kPressDirections) {
        if ((down & button.mask) == 0) continue;
        const reader::PageTransform transform = CurrentTransform();
        switch (reader::PhysicalToLogical(button.direction, state_.orientation)) {
            case Direction::Down:
                if (transform.valid && transform.scroll_y < transform.max_scroll_y - 0.5)
                    changed = Scroll(screen) || changed;
                else
                    next();
                break;
            case Direction::Up:
                if (transform.valid && transform.scroll_y > 0.5)
                    changed = Scroll(-screen) || changed;
                else
                    previous(true);
                break;
            case Direction::Right: next(); break;
            case Direction::Left: previous(false); break;
            default: break;
        }
    }
    for (const PhysicalButton& button : kHeldDirections) {
        if ((held & button.mask) == 0) continue;
        const Direction logical = reader::PhysicalToLogical(button.direction, state_.orientation);
        if (logical == Direction::Down) changed = Scroll(kHeldScrollStep) || changed;
        if (logical == Direction::Up) changed = Scroll(-kHeldScrollStep) || changed;
    }

    if (down & (HidNpadButton_R | HidNpadButton_ZR | HidNpadButton_A)) next();
    if (down & (HidNpadButton_L | HidNpadButton_ZL)) previous(false);
    if (down & HidNpadButton_Y) {
        state_.scale_mode = state_.scale_mode == reader::ScaleMode::FitWidth
            ? reader::ScaleMode::FitPage : reader::ScaleMode::FitWidth;
        state_.scroll_y = 0.0;
        hint_until_ = SDL_GetTicks() + kHintMilliseconds;
        changed = true;
    }
    if (down & HidNpadButton_X) {
        switch (state_.orientation) {
            case Orientation::PortraitCounterClockwise: state_.orientation = Orientation::PortraitClockwise; break;
            case Orientation::PortraitClockwise: state_.orientation = Orientation::Landscape; break;
            default: state_.orientation = Orientation::PortraitCounterClockwise; break;
        }
        state_.scroll_y = 0.0;
        hint_until_ = SDL_GetTicks() + kHintMilliseconds;
        SchedulePrefetch();
        changed = true;
    }
    if (down & HidNpadButton_Minus) {
        OpenOverlay(Overlay::Menu);
        changed = true;
    }
    if (down & HidNpadButton_StickR) {
        OpenOverlay(Overlay::PageMenu, static_cast<int>(state_.current_page));
        changed = true;
    }
    if (down & HidNpadButton_B) action_ = Action::Close;
    return changed;
}

void ReaderView::EnsureCanvas() {
    if (canvas_ != nullptr && canvas_orientation_ == state_.orientation) return;
    if (canvas_ != nullptr) SDL_DestroyTexture(canvas_);
    const reader::Size size = reader::LogicalCanvas(state_.orientation);
    canvas_ = SDL_CreateTexture(ui_->renderer_, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_TARGET,
                                static_cast<int>(size.width), static_cast<int>(size.height));
    canvas_orientation_ = state_.orientation;
}

void ReaderView::Draw() {
    if (!open_) return;
    SDL_Renderer* renderer = ui_->renderer_;
    EnsureCanvas();
    const reader::Size canvas = reader::LogicalCanvas(state_.orientation);
    const int canvas_width = static_cast<int>(canvas.width);
    const int canvas_height = static_cast<int>(canvas.height);

    if (canvas_ != nullptr) SDL_SetRenderTarget(renderer, canvas_);
    SDL_SetRenderDrawColor(renderer, 18, 17, 20, 255);
    SDL_RenderClear(renderer);

    std::vector<reader::Size> sizes(page_count_, canvas);
    for (const auto& entry : textures_) {
        sizes[entry.first] = {static_cast<double>(entry.second.width),
                              static_cast<double>(entry.second.height)};
    }
    const auto layout = reader::LayoutVisiblePages(state_, sizes, {0.0, 0.0, canvas.width, canvas.height});
    for (const reader::LaidOutPage& page : layout) {
        const reader::Rect& d = page.transform.destination;
        const auto found = textures_.find(page.page_index);
        if (found != textures_.end() && page.transform.valid) {
            const SDL_Rect destination{static_cast<int>(std::lround(d.x)), static_cast<int>(std::lround(d.y)),
                                       static_cast<int>(std::lround(d.width)),
                                       static_cast<int>(std::lround(d.height))};
            SDL_RenderCopy(renderer, found->second.texture, nullptr, &destination);
            if (page.page_index == state_.current_page) state_.scroll_y = page.transform.scroll_y;
        } else {
            const bool failed = failed_.count(page.page_index) != 0;
            const bool missing = missing_.count(page.page_index) != 0;
            char text[64];
            std::snprintf(text, sizeof(text),
                          failed ? T("第 %zu 页无法解码") : missing ? T("等待下载第 %zu 页…") : T("正在载入第 %zu 页…"),
                          page.page_index + 1);
            const int center_x = static_cast<int>(d.x + d.width / 2);
            const int center_y = static_cast<int>(d.y + d.height / 2);
            ui_->DrawText(text, ui_->font_body_, center_x - 120, center_y + 40,
                          failed ? 255 : 200, failed ? 160 : 196, failed ? 160 : 210);
            if (!failed) {
                // Eight-dot spinner, same style as the status bar.
                const int phase = static_cast<int>((SDL_GetTicks() / 110) % 8);
                for (int index = 0; index < 8; ++index) {
                    const double angle = index * 3.14159265358979 / 4.0;
                    const int x = center_x + static_cast<int>(std::lround(std::cos(angle) * 22.0));
                    const int y = center_y + static_cast<int>(std::lround(std::sin(angle) * 22.0));
                    ui_->FillRoundedRect(x - 4, y - 4, 8, 8, 4, 220, 210, 240,
                                         static_cast<unsigned char>(index == phase ? 255 : 80));
                }
            }
        }
    }

    DrawStatus(canvas_width, canvas_height);

    if (SDL_GetTicks() < hint_until_ && overlay_ == Overlay::None) {
        ui_->FillRoundedRect(16, 16, canvas_width - 32, 92, 18, 0, 0, 0, 170);
        ui_->DrawFittedText(title_, ui_->font_small_, 34, 26, canvas_width - 68, 255, 255, 255);
        std::string mode = OrientationLabel(state_.orientation);
        mode += state_.scale_mode == reader::ScaleMode::FitWidth ? T(" · 适应宽度") : T(" · 整页");
        if (state_.orientation == Orientation::Landscape)
            mode += state_.requested_page_mode == reader::PageMode::Double ? T(" · 双页") : T(" · 单页");
        ui_->DrawFittedText(mode + T("   − 菜单 · B 返回 · 点中间上方 菜单 · 长按 页面菜单"), ui_->font_small_, 34, 62,
                            canvas_width - 68, 210, 200, 230);
    }
    if (toast_until_ != 0 && SDL_GetTicks() < toast_until_) {
        const int width = std::min(canvas_width - 48, ui_->TextWidth(toast_, ui_->font_small_) + 48);
        ui_->FillRoundedRect((canvas_width - width) / 2, canvas_height - 130, width, 44, 22, 0, 0, 0, 200);
        ui_->DrawFittedText(toast_, ui_->font_small_, (canvas_width - width) / 2 + 24, canvas_height - 121,
                            width - 48, 255, 255, 255);
    }
    DrawOverlay(canvas_width, canvas_height);
    SDL_SetRenderTarget(renderer, nullptr);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    if (canvas_ != nullptr) {
        if (state_.orientation == Orientation::Landscape) {
            SDL_RenderCopy(renderer, canvas_, nullptr, nullptr);
        } else {
            // Rotate the 720x1280 canvas around the screen center. The default
            // (console turned counter-clockwise, D-pad at the bottom) needs the
            // picture turned clockwise; SDL angles are clockwise-positive.
            const SDL_Rect destination{(1280 - canvas_width) / 2, (720 - canvas_height) / 2,
                                       canvas_width, canvas_height};
            const double angle = state_.orientation == Orientation::PortraitCounterClockwise ? 90.0 : -90.0;
            SDL_RenderCopyEx(renderer, canvas_, nullptr, &destination, angle, nullptr, SDL_FLIP_NONE);
        }
    }
    SDL_RenderPresent(renderer);
}

}  // namespace ehviewer
