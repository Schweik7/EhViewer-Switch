// Reader overlays: the tap-zone guide, the reader menu, the progress bar, the
// long-press page menu and the status bar. Everything is drawn on the logical
// canvas, so it rotates with the page.
#include "ReaderView.h"

#include "Ui.h"
#include "core/FileUtil.h"
#include "core/I18n.h"
#include "core/Library.h"

#include <SDL2/SDL.h>
#include <switch.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>

namespace ehviewer {
using i18n::T;
namespace {

using reader::Direction;
using reader::Orientation;

constexpr unsigned kToastMilliseconds = 2500;
constexpr int kAutoPageChoices[] = {0, 3, 5, 8, 12, 20, 30};

enum MenuId {
    kMenuOrientation = 1, kMenuScale, kMenuDouble, kMenuDirection, kMenuAutoPage, kMenuClock,
    kMenuBattery, kMenuJump, kMenuGuide, kMenuExit,
    kPageReload, kPageRefetch, kPageSave, kPageInfo, kPageCancel
};

struct DirectionButton {
    std::uint64_t mask;
    Direction direction;
};
constexpr DirectionButton kDirections[] = {
    {HidNpadButton_Up | HidNpadButton_StickLUp, Direction::Up},
    {HidNpadButton_Right | HidNpadButton_StickLRight, Direction::Right},
    {HidNpadButton_Down | HidNpadButton_StickLDown, Direction::Down},
    {HidNpadButton_Left | HidNpadButton_StickLLeft, Direction::Left},
};

const char* OrientationName(Orientation orientation) {
    switch (orientation) {
        case Orientation::PortraitCounterClockwise: return T("竖屏 · 十字键在下");
        case Orientation::PortraitClockwise: return T("竖屏 · 十字键在上");
        default: return T("横屏");
    }
}

bool Inside(const reader::Rect& rect, reader::Point point, double slop = 0.0) {
    return point.x >= rect.x - slop && point.x < rect.x + rect.width + slop &&
           point.y >= rect.y - slop && point.y < rect.y + rect.height + slop;
}

}  // namespace

ReaderView::Action ReaderView::TakeAction(int* page) {
    const Action action = action_;
    if (page != nullptr) *page = action_page_;
    action_ = Action::None;
    return action;
}

int ReaderView::OrientationIndex() const {
    switch (state_.orientation) {
        case Orientation::PortraitClockwise: return 1;
        case Orientation::Landscape: return 2;
        default: return 0;
    }
}

std::string ReaderView::PageFile(int page) const {
    return FindPageFile(directories_, page);
}

void ReaderView::ReloadPage(int page) {
    const auto found = textures_.find(static_cast<std::size_t>(page));
    if (found != textures_.end()) {
        if (found->second.texture != nullptr) SDL_DestroyTexture(found->second.texture);
        textures_.erase(found);
    }
    failed_.erase(static_cast<std::size_t>(page));
    missing_.erase(static_cast<std::size_t>(page));
    SchedulePrefetch();
}

void ReaderView::ShowToast(const std::string& text) {
    toast_ = text;
    toast_until_ = SDL_GetTicks() + kToastMilliseconds;
}

void ReaderView::OpenOverlay(Overlay overlay, int page) {
    overlay_ = overlay;
    overlay_page_ = page < 0 ? static_cast<int>(state_.current_page) : page;
    menu_selected_ = 0;
    menu_rects_.clear();
    BuildMenu();
}

void ReaderView::BuildMenu() {
    menu_.clear();
    const auto on_off = [](bool value) { return std::string(value ? T("开") : T("关")); };
    if (overlay_ == Overlay::Menu) {
        menu_.push_back({T("屏幕方向"), OrientationName(state_.orientation), kMenuOrientation});
        menu_.push_back({T("页面缩放"), FitWidth() ? T("适应宽度") : T("整页"), kMenuScale});
        menu_.push_back({T("横屏双页"), on_off(DoublePage()), kMenuDouble});
        menu_.push_back({T("双页翻页方向"), RightToLeft() ? T("从右到左") : T("从左到右"), kMenuDirection});
        menu_.push_back({T("自动翻页"),
                         options_.auto_page_seconds > 0
                             ? std::to_string(options_.auto_page_seconds) + T(" 秒")
                             : std::string(T("关")),
                         kMenuAutoPage});
        menu_.push_back({T("显示时钟"), on_off(options_.show_clock), kMenuClock});
        menu_.push_back({T("显示电量"), on_off(options_.show_battery), kMenuBattery});
        menu_.push_back({T("跳页…"), "", kMenuJump});
        menu_.push_back({T("查看操作引导"), "", kMenuGuide});
        menu_.push_back({T("退出阅读"), "", kMenuExit});
    } else if (overlay_ == Overlay::PageMenu) {
        menu_.push_back({T("重新载入"), "", kPageReload});
        menu_.push_back({T("重新下载这一页"), "", kPageRefetch});
        menu_.push_back({T("保存到 SD 卡"), "", kPageSave});
        menu_.push_back({T("页面信息"), "", kPageInfo});
        menu_.push_back({T("取消"), "", kPageCancel});
    }
    if (menu_selected_ >= menu_.size()) menu_selected_ = 0;
}

bool ReaderView::ActivateMenuItem(int delta) {
    if (menu_selected_ >= menu_.size()) return false;
    const int forward = delta < 0 ? -1 : 1;
    const int id = menu_[menu_selected_].id;
    // Value rows change with left/right as well as A; action rows only with A.
    const bool value_row = id >= kMenuOrientation && id <= kMenuBattery;
    if (delta != 0 && !value_row) return false;
    switch (id) {
        case kMenuOrientation: {
            static const Orientation kOrder[] = {Orientation::PortraitCounterClockwise,
                                                 Orientation::PortraitClockwise, Orientation::Landscape};
            const int next = (OrientationIndex() + forward + 3) % 3;
            state_.orientation = kOrder[next];
            state_.scroll_y = 0.0;
            SchedulePrefetch();
            action_ = Action::SettingsChanged;
            break;
        }
        case kMenuScale:
            state_.scale_mode = FitWidth() ? reader::ScaleMode::FitPage : reader::ScaleMode::FitWidth;
            state_.scroll_y = 0.0;
            action_ = Action::SettingsChanged;
            break;
        case kMenuDouble:
            state_.requested_page_mode = DoublePage() ? reader::PageMode::Single : reader::PageMode::Double;
            SchedulePrefetch();
            action_ = Action::SettingsChanged;
            break;
        case kMenuDirection:
            state_.reading_direction = RightToLeft() ? reader::ReadingDirection::LeftToRight
                                                     : reader::ReadingDirection::RightToLeft;
            action_ = Action::SettingsChanged;
            break;
        case kMenuAutoPage: {
            constexpr int count = static_cast<int>(sizeof(kAutoPageChoices) / sizeof(kAutoPageChoices[0]));
            int index = 0;
            while (index < count && kAutoPageChoices[index] != options_.auto_page_seconds) ++index;
            index = ((index >= count ? 0 : index) + forward + count) % count;
            options_.auto_page_seconds = kAutoPageChoices[index];
            last_turn_tick_ = SDL_GetTicks();
            action_ = Action::SettingsChanged;
            break;
        }
        case kMenuClock:
            options_.show_clock = !options_.show_clock;
            action_ = Action::SettingsChanged;
            break;
        case kMenuBattery:
            options_.show_battery = !options_.show_battery;
            battery_tick_ = 0;
            action_ = Action::SettingsChanged;
            break;
        case kMenuJump:
            overlay_ = Overlay::None;
            action_ = Action::Jump;
            return true;
        case kMenuGuide:
            OpenOverlay(Overlay::Guide1);
            return true;
        case kMenuExit:
            overlay_ = Overlay::None;
            action_ = Action::Close;
            return true;
        case kPageReload:
            overlay_ = Overlay::None;
            ReloadPage(overlay_page_);
            ShowToast(T("已重新载入"));
            return true;
        case kPageRefetch:
        case kPageSave:
            overlay_ = Overlay::None;
            action_ = id == kPageRefetch ? Action::RefetchPage : Action::SavePage;
            action_page_ = overlay_page_;
            return true;
        case kPageInfo: {
            const std::string path = PageFile(overlay_page_);
            char line[160];
            std::snprintf(line, sizeof(line), T("第 %d / %zu 页"), overlay_page_ + 1, page_count_);
            page_info_ = line;
            if (path.empty()) {
                page_info_ += std::string("\n") + T("这一页还没有下载");
            } else {
                page_info_ += std::string("\n") + T("文件：") + path.substr(path.find_last_of('/') + 1);
                const std::uint64_t size = file::FileSize(path);
                if (size > 0) {
                    std::snprintf(line, sizeof(line), "%.1f KB", size / 1024.0);
                    page_info_ += std::string("\n") + T("大小：") + line;
                }
                const auto texture = textures_.find(static_cast<std::size_t>(overlay_page_));
                if (texture != textures_.end()) {
                    std::snprintf(line, sizeof(line), "%d × %d", texture->second.width, texture->second.height);
                    page_info_ += std::string("\n") + T("尺寸：") + line;
                }
            }
            overlay_ = Overlay::PageInfo;
            return true;
        }
        default:
            overlay_ = Overlay::None;
            return true;
    }
    BuildMenu();
    return true;
}

void ReaderView::FinishGuide() {
    overlay_ = Overlay::None;
    options_.show_guide = false;
    action_ = Action::GuideFinished;
}

bool ReaderView::HandleOverlayInput(std::uint64_t down) {
    if (down == 0) return false;
    if (overlay_ == Overlay::Guide1) {
        overlay_ = Overlay::Guide2;
        return true;
    }
    if (overlay_ == Overlay::Guide2) {
        FinishGuide();
        return true;
    }
    if (overlay_ == Overlay::PageInfo) {
        if (down & (HidNpadButton_A | HidNpadButton_B | HidNpadButton_Minus)) overlay_ = Overlay::None;
        return true;
    }
    Direction direction = Direction::None;
    for (const DirectionButton& button : kDirections)
        if (down & button.mask) direction = reader::PhysicalToLogical(button.direction, state_.orientation);

    if (overlay_ == Overlay::Progress) {
        if (direction == Direction::Right || direction == Direction::Down || (down & HidNpadButton_R)) {
            const std::size_t step = (down & HidNpadButton_R) ? 10 : 1;
            JumpTo(static_cast<int>(std::min(page_count_ - 1, state_.current_page + step)));
        }
        if (direction == Direction::Left || direction == Direction::Up || (down & HidNpadButton_L)) {
            const std::size_t step = (down & HidNpadButton_L) ? 10 : 1;
            JumpTo(static_cast<int>(state_.current_page >= step ? state_.current_page - step : 0));
        }
        if (down & (HidNpadButton_A | HidNpadButton_B | HidNpadButton_Minus)) overlay_ = Overlay::None;
        return true;
    }

    // Menus.
    if (direction == Direction::Up && menu_selected_ > 0) --menu_selected_;
    if (direction == Direction::Down && menu_selected_ + 1 < menu_.size()) ++menu_selected_;
    if (direction == Direction::Left) ActivateMenuItem(-1);
    if (direction == Direction::Right) ActivateMenuItem(1);
    if (down & HidNpadButton_A) ActivateMenuItem(0);
    if (down & (HidNpadButton_B | HidNpadButton_Minus)) overlay_ = Overlay::None;
    return true;
}

bool ReaderView::SetProgressFromPoint(reader::Point logical) {
    if (page_count_ == 0 || progress_rect_.width <= 0.0 || !Inside(progress_rect_, logical, 60.0)) return false;
    const double fraction = std::max(0.0, std::min(1.0, (logical.x - progress_rect_.x) / progress_rect_.width));
    const int page = static_cast<int>(std::lround(fraction * static_cast<double>(page_count_ - 1)));
    if (static_cast<std::size_t>(page) == state_.current_page) return false;
    JumpTo(page);
    return true;
}

bool ReaderView::TapOverlay(reader::Point logical) {
    switch (overlay_) {
        case Overlay::Guide1:
            overlay_ = Overlay::Guide2;
            return true;
        case Overlay::Guide2:
            FinishGuide();
            return true;
        case Overlay::Progress:
            if (!SetProgressFromPoint(logical) && !Inside(progress_rect_, logical, 60.0)) overlay_ = Overlay::None;
            return true;
        case Overlay::Menu:
        case Overlay::PageMenu:
            for (std::size_t index = 0; index < menu_rects_.size(); ++index) {
                if (Inside(menu_rects_[index], logical)) {
                    menu_selected_ = index;
                    ActivateMenuItem(0);
                    return true;
                }
            }
            overlay_ = Overlay::None;  // tapping outside closes the menu
            return true;
        default:
            overlay_ = Overlay::None;
            return true;
    }
}

void ReaderView::DrawStatus(int width, int height) {
    std::string text;
    if (options_.show_clock) {
        const std::time_t now = std::time(nullptr);
        const std::tm* local = std::localtime(&now);
        if (local != nullptr) {
            char clock[16];
            std::snprintf(clock, sizeof(clock), "%02d:%02d", local->tm_hour, local->tm_min);
            text += clock;
        }
    }
    if (options_.show_battery && battery_percent_ >= 0)
        text += (text.empty() ? "" : "  ·  ") + std::to_string(battery_percent_) + "%";
    char pages[32];
    std::snprintf(pages, sizeof(pages), "%zu / %zu", state_.current_page + 1, page_count_);
    text += (text.empty() ? "" : "  ·  ") + std::string(pages);
    if (options_.auto_page_seconds > 0) text += std::string("  ·  ") + T("自动");
    const int text_width = ui_->TextWidth(text, ui_->font_small_);
    ui_->FillRoundedRect(width - text_width - 48, height - 52, text_width + 32, 38, 19, 0, 0, 0, 150);
    ui_->DrawText(text, ui_->font_small_, width - text_width - 32, height - 45, 240, 240, 240);
}

void ReaderView::DrawGuide(int width, int height) {
    ui_->FillRoundedRect(0, 0, width, height, 0, 0, 0, 0, 185);
    const auto centered = [&](const std::string& text, TTF_Font* font, int center_x, int y,
                              unsigned char r, unsigned char g, unsigned char b) {
        ui_->DrawText(text, font, center_x - ui_->TextWidth(text, font) / 2, y, r, g, b);
    };
    if (overlay_ == Overlay::Guide1) {
        const int third = width / 3;
        // Zone borders, as in the Android guide.
        ui_->FillRect(third - 1, 0, 3, height, 255, 255, 255, 170);
        ui_->FillRect(third * 2 - 1, 0, 3, height, 255, 255, 255, 170);
        ui_->FillRect(third, height / 2 - 1, third, 3, 255, 255, 255, 170);
        const bool mirror = MirroredTaps();
        centered(mirror ? T("下一页") : T("上一页"), ui_->font_title_, third / 2, height / 2 - 20, 255, 255, 255);
        centered(mirror ? T("上一页") : T("下一页"), ui_->font_title_, third * 5 / 2, height / 2 - 20, 255, 255, 255);
        centered(T("菜单"), ui_->font_title_, width / 2, height / 4 - 20, 255, 255, 255);
        centered(T("进度条"), ui_->font_title_, width / 2, height * 3 / 4 - 20, 255, 255, 255);
        centered(T("点击屏幕继续（1/2）"), ui_->font_small_, width / 2, height - 60, 210, 200, 230);
        return;
    }
    const int panel = std::min(width - 64, 640);
    const int x = (width - panel) / 2;
    centered(T("长按图片：页面菜单"), ui_->font_title_, width / 2, height / 2 - 170, 255, 255, 255);
    ui_->DrawText(T("重新载入 · 重新下载这一页 · 保存到 SD 卡 · 页面信息"), ui_->font_body_, x,
                  height / 2 - 100, 220, 214, 236, panel);
    ui_->DrawText(T("按键：L/R 或十字键翻页 · − 菜单 · 按下右摇杆 页面菜单 · Y 缩放 · X 旋转 · B 返回"),
                  ui_->font_body_, x, height / 2, 220, 214, 236, panel);
    centered(T("点击屏幕完成（2/2）"), ui_->font_small_, width / 2, height - 60, 210, 200, 230);
}

void ReaderView::DrawOverlay(int width, int height) {
    menu_rects_.clear();
    if (overlay_ == Overlay::None) return;
    if (overlay_ == Overlay::Guide1 || overlay_ == Overlay::Guide2) {
        DrawGuide(width, height);
        return;
    }
    if (overlay_ == Overlay::Progress) {
        ui_->FillRoundedRect(24, height - 190, width - 48, 130, 24, 0, 0, 0, 210);
        char text[64];
        std::snprintf(text, sizeof(text), T("第 %zu / %zu 页"), state_.current_page + 1, page_count_);
        ui_->DrawText(text, ui_->font_body_, 56, height - 176, 255, 255, 255);
        const int bar_x = 64;
        const int bar_width = width - 128;
        const int bar_y = height - 112;
        progress_rect_ = {static_cast<double>(bar_x), static_cast<double>(bar_y - 14),
                          static_cast<double>(bar_width), 36.0};
        ui_->FillRoundedRect(bar_x, bar_y, bar_width, 8, 4, 120, 112, 140);
        const double fraction = page_count_ > 1
            ? static_cast<double>(state_.current_page) / static_cast<double>(page_count_ - 1) : 1.0;
        const int filled = static_cast<int>(bar_width * fraction);
        ui_->FillRoundedRect(bar_x, bar_y, std::max(8, filled), 8, 4, 208, 188, 255);
        ui_->FillRoundedRect(bar_x + filled - 14, bar_y - 10, 28, 28, 14, 255, 255, 255);
        const std::string hint = T("拖动或点按跳转 · ←→ 1 页 · L/R 10 页 · B 关闭");
        ui_->DrawFittedText(hint, ui_->font_small_, 56, height - 88, width - 112, 200, 196, 210);
        return;
    }

    ui_->FillRoundedRect(0, 0, width, height, 0, 0, 0, 0, 120);
    if (overlay_ == Overlay::PageInfo) {
        const int panel = std::min(width - 48, 620);
        const int x = (width - panel) / 2;
        const int y = height / 2 - 150;
        ui_->FillRoundedRect(x, y, panel, 300, 24, 255, 255, 255);
        ui_->DrawText(T("页面信息"), ui_->font_title_, x + 28, y + 20, 29, 27, 32);
        int line_y = y + 80;
        std::size_t begin = 0;
        while (begin <= page_info_.size()) {
            const std::size_t end = std::min(page_info_.find('\n', begin), page_info_.size());
            ui_->DrawFittedText(page_info_.substr(begin, end - begin), ui_->font_body_, x + 28, line_y,
                                panel - 56, 73, 69, 79, false);
            line_y += 40;
            begin = end + 1;
        }
        ui_->DrawText(T("A / B 关闭"), ui_->font_small_, x + 28, y + 256, 121, 116, 126);
        return;
    }

    const bool tall = height > width;
    const int item_height = tall ? 66 : 50;
    const int panel = std::min(width - 48, 620);
    const int panel_height = 76 + static_cast<int>(menu_.size()) * item_height + 16;
    const int x = (width - panel) / 2;
    const int y = std::max(16, (height - panel_height) / 2);
    ui_->FillRoundedRect(x, y, panel, panel_height, 24, 255, 255, 255);
    std::string title = T("菜单");
    if (overlay_ == Overlay::PageMenu) {
        char text[48];
        std::snprintf(text, sizeof(text), T("第 %d 页"), overlay_page_ + 1);
        title = text;
    }
    ui_->DrawText(title, ui_->font_title_, x + 28, y + 18, 29, 27, 32);
    for (std::size_t index = 0; index < menu_.size(); ++index) {
        const int item_y = y + 76 + static_cast<int>(index) * item_height;
        menu_rects_.push_back({static_cast<double>(x + 12), static_cast<double>(item_y),
                               static_cast<double>(panel - 24), static_cast<double>(item_height)});
        if (index == menu_selected_)
            ui_->FillRoundedRect(x + 12, item_y + 2, panel - 24, item_height - 4, 14, 232, 222, 248);
        const MenuItem& item = menu_[index];
        const int text_y = item_y + (item_height - 30) / 2;
        ui_->DrawText(item.label, ui_->font_body_, x + 32, text_y, 29, 27, 32);
        if (!item.value.empty()) {
            const std::string value = item.value;
            const int value_width = ui_->TextWidth(value, ui_->font_body_);
            ui_->DrawText(value, ui_->font_body_, x + panel - 32 - value_width, text_y, 103, 80, 164);
        }
    }
}

}  // namespace ehviewer
