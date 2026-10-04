// Online screens: gallery list, detail, comments and tags.
#include "Ui.h"
#include "UiShared.h"

#include "core/I18n.h"
#include "download/Downloader.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <switch.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace ehviewer {
using i18n::T;
using namespace ui_shared;

void Ui::DrawGalleryList(const std::vector<GallerySummary>& galleries, std::size_t selected,
                         const std::string& title, const std::string& subtitle, int sidebar_item,
                         int layout, double scroll, const std::string& footer,
                         const std::string& status, const std::string& last_input,
                         unsigned long long input_events) {
    Begin(title, subtitle, 1, sidebar_item);

    if (galleries.empty() && !footer.empty()) {
        // Just switched here: the request for this page is running.
        DrawCard(240, 124, 1016, 486);
        // Eight-dot spinner, same style as the reader.
        const int phase = static_cast<int>((SDL_GetTicks() / 110) % 8);
        for (int index = 0; index < 8; ++index) {
            const double angle = index * 3.14159265358979 / 4.0;
            const int x = 630 + static_cast<int>(std::lround(std::cos(angle) * 26.0));
            const int y = 298 + static_cast<int>(std::lround(std::sin(angle) * 26.0));
            FillRoundedRect(x - 5, y - 5, 10, 10, 5, 103, 80, 164,
                            static_cast<unsigned char>(index == phase ? 255 : 70));
        }
        const int width = TextWidth(footer, font_body_);
        DrawText(footer, font_body_, 630 - width / 2, 372, kOnSurfaceVariant.r, kOnSurfaceVariant.g,
                 kOnSurfaceVariant.b);
        Finish(status, last_input, input_events);
        return;
    }
    if (galleries.empty()) {
        DrawCard(240, 124, 1016, 486);
        FillRoundedRect(582, 210, 96, 96, 48, 232, 222, 248);
        DrawText("…", font_title_, 604, 225, 103, 80, 164);
        DrawText(T("还没有图库内容"), font_title_, 477, 332,
                 kOnSurface.r, kOnSurface.g, kOnSurface.b);
        DrawText(T("按 X 从站点加载，网络请求期间可以继续操作或按 B 取消。"),
                 font_body_, 342, 397, kOnSurfaceVariant.r,
                 kOnSurfaceVariant.g, kOnSurfaceVariant.b);
        FillRoundedRect(532, 470, 196, 48, 24, kPrimary.r, kPrimary.g, kPrimary.b);
        DrawText(T("X  加载图库"), font_body_, 556, 479, 255, 255, 255);
        AddHit(532, 470, 196, 48, HidNpadButton_X);
        Finish(status, last_input, input_events);
        return;
    }

    const ListGeometry geometry = GalleryListGeometry(layout);
    const int top = geometry.viewport_top;
    const int bottom = top + geometry.viewport_height;
    const SDL_Rect clip{kContentLeft - 4, top, kContentRight - kContentLeft + 8, geometry.viewport_height};
    SDL_RenderSetClipRect(renderer_, &clip);
    const int offset = static_cast<int>(scroll);
    for (std::size_t index = geometry.FirstVisible(scroll, galleries.size()); index < galleries.size(); ++index) {
        const ItemRect item = geometry.Item(index);
        const int x = item.x;
        const int y = top + item.y - offset;
        if (y >= bottom) break;
        if (y + item.height <= top) continue;
        const GallerySummary& gallery = galleries[index];
        const bool active = index == selected;
        // Only the visible part of a card reacts to touch.
        const int hit_top = std::max(y, top);
        AddHit(x, hit_top, item.width, std::min(y + item.height, bottom) - hit_top, HidNpadButton_A,
               static_cast<int>(index));
        DrawCard(x, y, item.width, item.height, active);
        const std::string title = gallery.title.empty() ? std::string(T("无标题图库")) : gallery.title;
        const std::string rating = gallery.rating >= 0.0 ? RatingText(gallery.rating) : std::string();

        if (layout == 1) {
            // Thumbnail grid card.
            FillRoundedRect(x + 8, y + 8, item.width - 16, 232, 10, 236, 230, 240);
            if (!DrawThumbnail(gallery.gid, x + 8, y + 8, item.width - 16, 232))
                DrawText("…", font_title_, x + item.width / 2 - 14, y + 100, 150, 140, 160);
            const std::vector<std::string> lines = WrapText(title, font_small_, item.width - 20);
            for (std::size_t line = 0; line < 2 && line < lines.size(); ++line) {
                const bool last = line == 1 && lines.size() > 2;
                DrawFittedText(last ? lines[line] + " " + lines[line + 1] : lines[line], font_small_, x + 10,
                               y + 246 + static_cast<int>(line) * 24, item.width - 20,
                               active ? 33 : kOnSurface.r, active ? 0 : kOnSurface.g, active ? 93 : kOnSurface.b);
            }
            std::string meta = gallery.category;
            if (gallery.pages > 0) meta += (meta.empty() ? "" : " · ") + std::to_string(gallery.pages) + "P";
            DrawFittedText(meta, font_small_, x + 10, y + 296, item.width - 20, 121, 116, 126);
            if (!rating.empty()) DrawText(rating, font_small_, x + 10, y + 318, 196, 132, 0);
            continue;
        }

        // Row.
        if (active) FillRoundedRect(x, y + 18, 6, 48, 3, 103, 80, 164);
        FillRoundedRect(x + 16, y + 6, 54, 72, 8, 236, 230, 240);
        if (!DrawThumbnail(gallery.gid, x + 16, y + 6, 54, 72))
            DrawText("…", font_small_, x + 34, y + 28, 150, 140, 160);
        DrawFittedText(title, font_body_, x + 86, y + 12, 820,
                       active ? 33 : kOnSurface.r, active ? 0 : kOnSurface.g, active ? 93 : kOnSurface.b);
        std::string meta = gallery.category.empty() ? "" : gallery.category + "  ·  ";
        if (gallery.pages > 0) meta += std::to_string(gallery.pages) + T(" 页  ·  ");
        meta += gallery.posted.empty() ? "GID " + std::to_string(gallery.gid) : gallery.posted;
        int meta_x = x + 88;
        if (!rating.empty()) {
            DrawText(rating, font_small_, meta_x, y + 48, 196, 132, 0);
            meta_x += 96;
        }
        DrawFittedText(meta, font_small_, meta_x, y + 48, 760,
                       active ? 93 : 121, active ? 64 : 116, active ? 145 : 126);
        if (active) {
            FillRoundedRect(x + 924, y + 23, 72, 38, 19, kPrimary.r, kPrimary.g, kPrimary.b);
            DrawText("A  >", font_small_, x + 941, y + 32, 255, 255, 255);
        } else {
            DrawText(">", font_body_, x + 961, y + 24, 121, 116, 126);
        }
    }
    // Footer below the last item: loading more / end of list.
    if (!footer.empty()) {
        const int footer_y = top + geometry.ContentHeight(galleries.size()) - offset + 14;
        if (footer_y < bottom) DrawText(footer, font_small_, 600, footer_y, 121, 116, 126);
    }
    // Scroll bar.
    const double max_scroll = geometry.MaxScroll(galleries.size());
    if (max_scroll > 0.0) {
        const int track = geometry.viewport_height;
        const int thumb = std::max(30, track * track / (geometry.ContentHeight(galleries.size()) + track / 4));
        const int position = static_cast<int>((track - thumb) * std::min(1.0, scroll / max_scroll));
        FillRoundedRect(kContentRight + 6, top + position, 5, thumb, 2, 103, 80, 164, 160);
    }
    SDL_RenderSetClipRect(renderer_, nullptr);
    Finish(status, last_input, input_events);
}

void Ui::DrawGalleryDetail(const GalleryDetail& detail, int local_state,
                           const DownloadProgress& download, const std::string& status,
                           const std::string& last_input, unsigned long long input_events) {
    Begin(T("图库详情"), T("A 阅读 · Y 下载 · X 评论 · ZL 全部标签 · − 搜索上传者 · L 评分 · R 收藏 · B 返回"), 2);
    DrawCard(240, 108, 1016, 530);
    const auto value_or_dash = [](const std::string& value) { return value.empty() ? std::string("-") : value; };

    // Left column: cover and secondary facts.
    FillRoundedRect(256, 122, 228, 304, 14, 236, 230, 240);
    if (!DrawThumbnail(detail.gid, 260, 126, 220, 296))
        DrawText("COVER", font_title_, 312, 250, 121, 105, 134);
    FillRoundedRect(256, 436, 228, 30, 15, 232, 222, 248);
    DrawFittedText(detail.category.empty() ? "Gallery" : detail.category, font_small_, 272, 440, 196, 75, 50, 120);
    AddHit(256, 472, 228, 28, HidNpadButton_R);
    DrawFittedText(detail.favorite_name.empty() ? std::string(T("♡ 未收藏")) : T("♥ ") + detail.favorite_name,
                   font_small_, 258, 476, 226, detail.favorite_name.empty() ? 121 : 186,
                   detail.favorite_name.empty() ? 116 : 26, detail.favorite_name.empty() ? 126 : 80);
    DrawFittedText(T("被收藏 ") + value_or_dash(detail.favorited), font_small_, 258, 504, 226, 121, 116, 126);
    DrawFittedText(value_or_dash(detail.posted), font_small_, 258, 532, 226, 121, 116, 126);
    DrawFittedText("GID " + std::to_string(detail.gid), font_small_, 258, 560, 226, 150, 145, 155);

    // Right column: title, facts, uploader, core tags, actions.
    constexpr int kLeft = 504;
    constexpr int kWidth = 736;
    const std::vector<std::string> title_lines =
        WrapText(detail.title.empty() ? std::string(T("无标题图库")) : detail.title, font_body_, kWidth);
    for (std::size_t line = 0; line < 2 && line < title_lines.size(); ++line) {
        const bool truncated = line == 1 && title_lines.size() > 2;
        DrawFittedText(truncated ? title_lines[1] + " " + title_lines[2] : title_lines[line], font_body_, kLeft,
                       116 + static_cast<int>(line) * 32, kWidth, kOnSurface.r, kOnSurface.g, kOnSurface.b);
    }
    if (!detail.title_jpn.empty())
        DrawFittedText(detail.title_jpn, font_small_, kLeft, 184, kWidth,
                       kOnSurfaceVariant.r, kOnSurfaceVariant.g, kOnSurfaceVariant.b);

    // Fact chips: rating (tap = rate), pages, size, language.
    int chip_x = kLeft;
    const auto chip = [&](const std::string& text, std::uint64_t buttons, bool accent) {
        const int width = TextWidth(text, font_small_) + 24;
        if (buttons != 0) AddHit(chip_x, 214, width, 30, buttons);
        FillRoundedRect(chip_x, 214, width, 30, 15, accent ? 255 : 246, accent ? 244 : 242, accent ? 214 : 248);
        DrawText(text, font_small_, chip_x + 12, 217, accent ? 122 : 73, accent ? 83 : 69, accent ? 0 : 79);
        chip_x += width + 8;
    };
    chip("★ " + value_or_dash(detail.rating), HidNpadButton_L, true);
    if (detail.pages > 0) chip(std::to_string(detail.pages) + T(" 页"), 0, false);
    if (!detail.file_size.empty()) chip(detail.file_size, 0, false);
    if (!detail.language.empty()) chip(detail.language, 0, false);

    // Uploader: tap (or "−") searches their galleries.
    if (!detail.uploader.empty()) {
        const std::string label = T("上传者  ") + detail.uploader + "  ›";
        const int width = TextWidth(label, font_small_) + 28;
        AddHit(kLeft, 252, width, 30, kTouchUploader);
        FillRoundedRect(kLeft, 252, width, 30, 15, 232, 222, 248);
        DrawText(label, font_small_, kLeft + 14, 255, 73, 50, 113);
    }

    // Core tags, most informative namespaces first.
    static const char* const kOrder[] = {"language", "artist", "group", "parody", "character", "cosplayer",
                                          "female", "male", "mixed", "other", "reclass", "temp", ""};
    std::vector<std::size_t> offsets;  // flat index of each group's first tag
    std::size_t flat_total = 0;
    for (const TagGroup& group : detail.tags) {
        offsets.push_back(flat_total);
        flat_total += group.tags.size();
    }
    int y = 292;
    bool clipped = false;
    for (const char* name_space : kOrder) {
        for (std::size_t group_index = 0; group_index < detail.tags.size(); ++group_index) {
            const TagGroup& group = detail.tags[group_index];
            if (group.name_space != name_space) continue;
            if (y > 418) {
                clipped = true;
                continue;
            }
            DrawFittedText(TagNamespaceLabel(group.name_space), font_small_, kLeft, y + 4, 80, 103, 80, 164);
            int x = kLeft + 86;
            for (std::size_t tag_index = 0; tag_index < group.tags.size(); ++tag_index) {
                const std::string label = FitText(group.tags[tag_index], font_small_, 300);
                const int width = TextWidth(label, font_small_) + 22;
                if (x + width > kLeft + kWidth) {
                    x = kLeft + 86;
                    y += 34;
                    if (y > 418) {
                        clipped = true;
                        break;
                    }
                }
                AddHit(x, y, width, 30, kTouchTag, static_cast<int>(offsets[group_index] + tag_index));
                FillRoundedRect(x, y, width, 30, 15, 246, 242, 248);
                DrawText(label, font_small_, x + 11, y + 3, kOnSurface.r, kOnSurface.g, kOnSurface.b);
                x += width + 6;
            }
            y += 38;
        }
    }
    if (clipped || detail.tags.empty()) {
        // Remaining tags live on the full tag screen.
        const std::string more = detail.tags.empty() ? std::string(T("没有标签"))
                                                     : T("ZL 查看全部 ") + std::to_string(flat_total) + T(" 个标签");
        if (!detail.tags.empty()) AddHit(kLeft, 428, 300, 28, HidNpadButton_ZL);
        DrawText(more, font_small_, kLeft, 430, 103, 80, 164);
    }

    // Action buttons.
    const bool downloading = download.active && download.gid == detail.gid;
    struct Button {
        std::uint64_t key;
        std::string label;
        bool primary;
    };
    const Button buttons[] = {
        {HidNpadButton_A, local_state == 2 ? T("A 阅读") : T("A 边下边读"), true},
        {HidNpadButton_Y, local_state == 2 ? T("已下载") : downloading ? T("下载中") : local_state == 1 ? T("Y 继续下载") : T("Y 下载"), false},
        {HidNpadButton_X, T("X 评论 ") + std::to_string(detail.comments.size()), false},
        {HidNpadButton_L, T("L 评分"), false},
        {HidNpadButton_R, detail.favorite_name.empty() ? T("R 收藏") : T("R 改收藏"), false},
    };
    for (int index = 0; index < 5; ++index) {
        const int x = kLeft + index * 150;
        const Button& button = buttons[index];
        AddHit(x, 470, 140, 50, button.key);
        FillRoundedRect(x, 470, 140, 50, 25, button.primary ? kPrimary.r : 232, button.primary ? kPrimary.g : 222,
                        button.primary ? kPrimary.b : 248);
        const std::string label = FitText(button.label, font_small_, 120);
        DrawText(label, font_small_, x + (140 - TextWidth(label, font_small_)) / 2, 483,
                 button.primary ? 255 : 73, button.primary ? 255 : 50, button.primary ? 255 : 113);
    }
    if (downloading) {
        char line[48];
        std::snprintf(line, sizeof(line), T("下载中 %d/%d"), download.done, download.total);
        DrawFittedText(std::string(line) + "   ·   " + DownloadSpeedText(download), font_small_, kLeft, 534, kWidth,
                       download.stalled_seconds >= kStallWarningSeconds ? 186 : 122,
                       download.stalled_seconds >= kStallWarningSeconds ? 26 : 83, 0, false);
        const int bar = download.total > 0 ? kWidth * std::min(download.done, download.total) / download.total : 0;
        FillRoundedRect(kLeft, 566, kWidth, 8, 4, 232, 222, 248);
        if (bar > 0) FillRoundedRect(kLeft, 566, std::max(8, bar), 8, 4, kPrimary.r, kPrimary.g, kPrimary.b);
    } else if (local_state == 1) {
        DrawText(T("部分页面已下载，可以直接阅读"), font_small_, kLeft, 534, 122, 83, 0);
    }
    Finish(status, last_input, input_events);
}

void Ui::DrawComments(const GalleryDetail& detail, int* scroll, const std::string& status,
                      const std::string& last_input, unsigned long long input_events) {
    Begin(T("评论 ") + std::to_string(detail.comments.size()),
          T("上下滚动 · L/R 快速翻页 · B 返回详情"), 2);
    DrawCard(240, 108, 1016, 528);

    // Flatten every comment into lines so scrolling works per line.
    struct Line {
        std::string text;
        bool header = false;
        bool uploader = false;
    };
    std::vector<Line> lines;
    constexpr int kTextWidth = 960;
    for (const GalleryComment& comment : detail.comments) {
        std::string header = comment.uploader ? T("上传者  ") : "";
        header += comment.author.empty() ? T("匿名") : comment.author;
        if (!comment.posted.empty()) header += "  ·  " + comment.posted;
        if (!comment.score.empty()) header += "  ·  " + comment.score;
        lines.push_back({header, true, comment.uploader});
        for (const std::string& line : WrapText(comment.text, font_small_, kTextWidth))
            lines.push_back({line, false, false});
        lines.push_back({"", false, false});
    }
    if (lines.empty()) {
        DrawText(T("这个图库还没有评论"), font_title_, 560, 330, kOnSurface.r, kOnSurface.g, kOnSurface.b);
        Finish(status, last_input, input_events);
        return;
    }

    constexpr int kLineHeight = 29;
    constexpr int kVisibleLines = 17;
    const int max_scroll = std::max(0, static_cast<int>(lines.size()) - kVisibleLines);
    *scroll = std::max(0, std::min(*scroll, max_scroll));
    for (int row = 0; row < kVisibleLines; ++row) {
        const std::size_t index = static_cast<std::size_t>(*scroll + row);
        if (index >= lines.size()) break;
        const Line& line = lines[index];
        const int y = 122 + row * kLineHeight;
        if (line.header) {
            FillRoundedRect(260, y - 2, 6, 26, 3, line.uploader ? 20 : 103, line.uploader ? 108 : 80,
                            line.uploader ? 67 : 164);
            DrawFittedText(line.text, font_small_, 276, y, kTextWidth, 103, 80, 164);
        } else if (!line.text.empty()) {
            DrawText(line.text, font_small_, 276, y, kOnSurface.r, kOnSurface.g, kOnSurface.b);
        }
    }
    if (max_scroll > 0) {
        // Scroll bar.
        const int track = kVisibleLines * kLineHeight;
        const int thumb = std::max(24, track * kVisibleLines / static_cast<int>(lines.size()));
        const int offset = (track - thumb) * *scroll / max_scroll;
        FillRoundedRect(1238, 122, 6, track, 3, 236, 230, 240);
        FillRoundedRect(1238, 122 + offset, 6, thumb, 3, kPrimary.r, kPrimary.g, kPrimary.b);
    }
    Finish(status, last_input, input_events);
}

void Ui::DrawTags(const GalleryDetail& detail, std::size_t selected, const std::string& status,
                  const std::string& last_input, unsigned long long input_events) {
    Begin(T("标签"), T("方向键选择 · A 按标签搜索 · B 返回详情"), 2);
    DrawCard(240, 108, 1016, 528);
    // Tags flow left to right inside each namespace row, wrapping as needed.
    int y = 124;
    std::size_t flat = 0;
    for (const TagGroup& group : detail.tags) {
        if (y > 600) break;
        DrawFittedText(group.name_space.empty() ? "misc" : group.name_space, font_small_, 262, y + 6, 120,
                       103, 80, 164);
        int x = 392;
        for (const std::string& tag : group.tags) {
            const std::string label = FitText(tag, font_small_, 400);
            const int width = TextWidth(label, font_small_) + 28;
            if (x + width > 1236) {
                x = 392;
                y += 40;
            }
            const bool active = flat == selected;
            if (y <= 600) {
                AddHit(x, y, width, 34, HidNpadButton_A, static_cast<int>(flat));
                FillRoundedRect(x, y, width, 34, 17, active ? 103 : 236, active ? 80 : 230, active ? 164 : 240);
                DrawText(label, font_small_, x + 14, y + 6, active ? 255 : 73, active ? 255 : 50,
                         active ? 255 : 113);
            }
            x += width + 10;
            ++flat;
        }
        y += 48;
    }
    Finish(status, last_input, input_events);
}

}  // namespace ehviewer
