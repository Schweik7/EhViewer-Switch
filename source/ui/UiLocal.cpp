// Local screens: settings, subscriptions and the download library.
#include "Ui.h"
#include "UiShared.h"

#include "core/I18n.h"
#include "download/Downloader.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <switch.h>

#include <algorithm>
#include <cstdio>

namespace ehviewer {
using i18n::T;
using namespace ui_shared;

void Ui::DrawSettings(const std::vector<SettingRow>& rows, std::size_t selected,
                      const std::string& status, const std::string& last_input,
                      unsigned long long input_events) {
    Begin(T("设置"), T("上下选择 · 左右或 A 修改 · B 返回"), 0, kSidebarSettings);
    constexpr int kRowHeight = 58;
    constexpr int kVisible = 9;
    std::size_t first = selected > 4 ? selected - 4 : 0;
    if (first + kVisible > rows.size()) first = rows.size() > kVisible ? rows.size() - kVisible : 0;
    for (std::size_t index = first; index < rows.size() && index < first + kVisible; ++index) {
        const SettingRow& row = rows[index];
        const int y = 108 + static_cast<int>(index - first) * kRowHeight;
        if (row.header) {
            DrawText(row.label, font_small_, 252, y + 26, 103, 80, 164);
            continue;
        }
        const bool active = index == selected;
        AddHit(240, y, 1016, kRowHeight - 6, HidNpadButton_A, static_cast<int>(index));
        DrawCard(240, y, 1016, kRowHeight - 6, active);
        DrawFittedText(row.label, font_body_, 264, y + 9, 480,
                       active ? 33 : kOnSurface.r, active ? 0 : kOnSurface.g, active ? 93 : kOnSurface.b);
        if (!row.description.empty())
            DrawFittedText(row.description, font_small_, 640, y + 14, 330, 121, 116, 126);
        FillRoundedRect(990, y + 9, 246, 34, 17, active ? 103 : 236, active ? 80 : 230, active ? 164 : 240);
        DrawFittedText(row.value, font_small_, 1006, y + 14, 214,
                       active ? 255 : 73, active ? 255 : 50, active ? 255 : 113);
    }
    Finish(status, last_input, input_events);
}

void Ui::DrawSubscriptions(const std::vector<Subscription>& subscriptions, std::size_t selected,
                           const std::string& status, const std::string& last_input,
                           unsigned long long input_events) {
    Begin(T("我的订阅"), T("A 打开 · X 新建 · Y 删除 · ZL/ZR 切换菜单"), 0, kSidebarSubscriptions);
    if (subscriptions.empty()) {
        DrawCard(240, 124, 1016, 486);
        DrawText(T("还没有订阅"), font_title_, 560, 280, kOnSurface.r, kOnSurface.g, kOnSurface.b);
        DrawText(T("在搜索结果里按 X 保存为订阅，或在这里按 X 新建。"), font_body_, 400, 346,
                 kOnSurfaceVariant.r, kOnSurfaceVariant.g, kOnSurfaceVariant.b);
        FillRoundedRect(560, 420, 196, 48, 24, kPrimary.r, kPrimary.g, kPrimary.b);
        DrawText(T("X  新建订阅"), font_body_, 584, 429, 255, 255, 255);
        AddHit(560, 420, 196, 48, HidNpadButton_X);
        Finish(status, last_input, input_events);
        return;
    }
    constexpr int kRowHeight = 76;
    constexpr std::size_t kVisible = 7;
    std::size_t first = selected > 3 ? selected - 3 : 0;
    if (first + kVisible > subscriptions.size())
        first = subscriptions.size() > kVisible ? subscriptions.size() - kVisible : 0;
    for (std::size_t index = first; index < subscriptions.size() && index < first + kVisible; ++index) {
        const Subscription& subscription = subscriptions[index];
        const int y = 108 + static_cast<int>(index - first) * kRowHeight;
        const bool active = index == selected;
        AddHit(240, y, 1016, kRowHeight - 8, HidNpadButton_A, static_cast<int>(index));
        DrawCard(240, y, 1016, kRowHeight - 8, active);
        DrawText("★", font_body_, 262, y + 18, 196, 132, 0);
        DrawFittedText(subscription.name, font_body_, 300, y + 6, 880,
                       active ? 33 : kOnSurface.r, active ? 0 : kOnSurface.g, active ? 93 : kOnSurface.b);
        if (subscription.name != subscription.query)
            DrawFittedText(subscription.query, font_small_, 302, y + 38, 880, 121, 116, 126);
    }
    Finish(status, last_input, input_events);
}

void Ui::DrawLibrary(const std::vector<LibraryEntry>& entries, std::size_t selected,
                     const DownloadProgress& download, const std::string& status,
                     const std::string& last_input, unsigned long long input_events) {
    Begin(T("下载"), T("A 阅读 · Y 开始/暂停 · R 全部开始 · − 全部暂停 · X 删除 · ZL/ZR 切换菜单"), 0, kSidebarDownloads);

    // Download card.
    int list_top = 108;
    if (download.active || download.queued > 0 || !download.last_message.empty()) {
        DrawCard(240, 106, 1016, 76);
        if (download.active) {
            char line[96];
            std::snprintf(line, sizeof(line), T("%s  %d / %d   排队 %zu"), download.phase.c_str(),
                          download.done, download.total, download.queued);
            DrawFittedText(T("正在下载：") + download.title, font_body_, 262, 114, 970,
                           kOnSurface.r, kOnSurface.g, kOnSurface.b);
            const std::string speed = DownloadSpeedText(download);
            DrawFittedText(std::string(line) + "   ·   " + speed, font_small_, 264, 148, 600,
                           download.stalled_seconds >= kStallWarningSeconds ? 186 : 121,
                           download.stalled_seconds >= kStallWarningSeconds ? 26 : 116,
                           download.stalled_seconds >= kStallWarningSeconds ? 26 : 126, false);
            const int bar_x = 880;
            const int bar_width = 356;
            FillRoundedRect(bar_x, 154, bar_width, 10, 5, 232, 222, 248);
            if (download.total > 0) {
                const int filled = bar_width * std::min(download.done, download.total) / download.total;
                FillRoundedRect(bar_x, 154, std::max(10, filled), 10, 5, kPrimary.r, kPrimary.g, kPrimary.b);
            }
        } else {
            DrawFittedText(download.last_message.empty() ? T("下载队列空闲") : download.last_message,
                           font_body_, 262, 128, 970,
                           download.last_success ? 20 : 186, download.last_success ? 108 : 26,
                           download.last_success ? 67 : 26);
        }
        list_top = 192;
    }

    if (entries.empty()) {
        DrawCard(240, list_top, 1016, 620 - list_top);
        DrawText(T("书库还是空的"), font_title_, 520, list_top + 80,
                 kOnSurface.r, kOnSurface.g, kOnSurface.b);
        DrawText(T("在图库列表或详情页按 Y 下载，完成后会出现在这里。"), font_body_, 400,
                 list_top + 146, kOnSurfaceVariant.r, kOnSurfaceVariant.g, kOnSurfaceVariant.b);
        Finish(status, last_input, input_events);
        return;
    }

    const std::size_t visible_count = static_cast<std::size_t>((640 - list_top) / 90);
    std::size_t first = selected > 2 ? selected - 2 : 0;
    if (first + visible_count > entries.size())
        first = entries.size() > visible_count ? entries.size() - visible_count : 0;
    const std::size_t last = std::min(first + visible_count, entries.size());
    for (std::size_t index = first; index < last; ++index) {
        const LibraryEntry& entry = entries[index];
        const int y = list_top + static_cast<int>(index - first) * 90;
        const bool active = index == selected;
        AddHit(240, y, 1016, 84, HidNpadButton_A, static_cast<int>(index));
        DrawCard(240, y, 1016, 84, active);
        FillRoundedRect(256, y + 6, 54, 72, 8, 236, 230, 240);
        DrawThumbnail(entry.gid, 256, y + 6, 54, 72);
        DrawFittedText(entry.manifest.title.empty() ? T("无标题图库") : entry.manifest.title,
                       font_body_, 326, y + 12, 800,
                       active ? 33 : kOnSurface.r, active ? 0 : kOnSurface.g,
                       active ? 93 : kOnSurface.b);
        char line[128];
        if (entry.complete) {
            std::snprintf(line, sizeof(line), T("%d 页 · 读到第 %d 页 · GID %lld"),
                          entry.manifest.total_pages, entry.manifest.current_page + 1,
                          static_cast<long long>(entry.gid));
            DrawText(line, font_small_, 328, y + 48, 121, 116, 126);
        } else {
            const bool downloading = download.active && download.gid == entry.gid;
            const bool queued = std::find(download.queued_gids.begin(), download.queued_gids.end(),
                                          entry.gid) != download.queued_gids.end();
            std::snprintf(line, sizeof(line), T("%s · 已下载 %d/%d 页 · 读到第 %d 页"),
                          downloading ? T("下载中（Y 暂停）") : queued ? T("排队中（Y 暂停）")
                                                                     : T("已暂停（Y 继续）"),
                          downloading ? download.done : entry.pages_present,
                          entry.manifest.total_pages, entry.manifest.current_page + 1);
            if (downloading || queued)
                DrawText(line, font_small_, 328, y + 48, 20, 108, 67);
            else
                DrawText(line, font_small_, 328, y + 48, 122, 83, 0);
        }
        if (active) {
            FillRoundedRect(1150, y + 23, 86, 38, 19, kPrimary.r, kPrimary.g, kPrimary.b);
            DrawText(T("A 阅读"), font_small_, 1166, y + 32, 255, 255, 255);
        }
    }
    Finish(status, last_input, input_events);
}

}  // namespace ehviewer
