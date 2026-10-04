#pragma once

// Layout, palette and text helpers shared by the Ui*.cpp files. Internal to Ui.

#include "core/I18n.h"
#include "download/Downloader.h"

#include <SDL2/SDL.h>

#include <cstddef>
#include <cstdio>
#include <string>

namespace ehviewer {
namespace ui_shared {
using i18n::T;

constexpr int kWidth = 1280;
constexpr int kHeight = 720;
constexpr int kSidebarWidth = 216;
constexpr int kHeaderHeight = 96;
constexpr int kContentLeft = 240;
constexpr int kContentRight = 1256;
constexpr int kStatusTop = 646;
constexpr std::size_t kTextCacheLimit = 320;
// Each list thumbnail is about 250x350 RGBA (~350 KiB of texture memory).
constexpr std::size_t kThumbnailLimit = 96;

// Material 3 inspired palette, adjusted for the Switch display.
constexpr SDL_Color kBackground{248, 247, 250, 255};
constexpr SDL_Color kSurface{255, 255, 255, 255};
constexpr SDL_Color kPrimary{103, 80, 164, 255};
constexpr SDL_Color kOnSurface{29, 27, 32, 255};
constexpr SDL_Color kOnSurfaceVariant{73, 69, 79, 255};
constexpr SDL_Color kOutline{202, 196, 208, 255};

// After this long without any received bytes the speed turns into a warning.
constexpr int kStallWarningSeconds = 8;

inline std::string FormatBytes(double bytes) {
    char text[32];
    if (bytes >= 1024.0 * 1024.0 * 1024.0) std::snprintf(text, sizeof(text), "%.2f GB", bytes / (1024.0 * 1024.0 * 1024.0));
    else if (bytes >= 1024.0 * 1024.0) std::snprintf(text, sizeof(text), "%.1f MB", bytes / (1024.0 * 1024.0));
    else if (bytes >= 1024.0) std::snprintf(text, sizeof(text), "%.0f KB", bytes / 1024.0);
    else std::snprintf(text, sizeof(text), "%.0f B", bytes);
    return text;
}

// Short display name of a tag namespace.
inline const char* TagNamespaceLabel(const std::string& name_space) {
    if (name_space == "language") return T("语言");
    if (name_space == "artist") return T("作者");
    if (name_space == "group") return T("社团");
    if (name_space == "parody") return T("原作");
    if (name_space == "character") return T("角色");
    if (name_space == "cosplayer") return "Coser";
    if (name_space == "female") return T("女性");
    if (name_space == "male") return T("男性");
    if (name_space == "mixed") return T("混合");
    if (name_space == "other") return T("其他");
    if (name_space == "reclass") return T("重分类");
    if (name_space == "temp") return T("临时");
    return name_space.empty() ? "misc" : name_space.c_str();
}

// Star rating as shown in lists, e.g. "★ 4.5".
inline std::string RatingText(double rating) {
    char text[16];
    std::snprintf(text, sizeof(text), "★ %.1f", rating);
    return text;
}

// Speed plus bytes received so far, or a waiting notice when nothing arrives.
inline std::string DownloadSpeedText(const DownloadProgress& download) {
    if (download.stalled_seconds >= kStallWarningSeconds) {
        char text[96];
        std::snprintf(text, sizeof(text), T("等待服务器响应 %d 秒"), download.stalled_seconds);
        return text;
    }
    return FormatBytes(download.speed_bytes_per_second) + "/s  ·  " + T("已下载 ") +
           FormatBytes(static_cast<double>(download.job_bytes));
}

}  // namespace ui_shared
}  // namespace ehviewer
