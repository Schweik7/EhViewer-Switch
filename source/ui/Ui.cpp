#include "Ui.h"

#include "Version.h"
#include "core/I18n.h"
#include "download/Downloader.h"
#include "UiShared.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#include <switch.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace ehviewer {
using i18n::T;
using namespace ui_shared;
namespace {


TTF_Font* OpenSharedFont(const PlFontData& data, int size) {
    SDL_RWops* source = SDL_RWFromConstMem(data.address, static_cast<int>(data.size));
    return source == nullptr ? nullptr : TTF_OpenFontRW(source, 1, size);
}

bool Contains(const std::string& text, const char* token) {
    return text.find(token) != std::string::npos;
}

enum class StatusTone { Neutral, Busy, Success, Error };

StatusTone GetStatusTone(const std::string& status) {
    if (Contains(status, T("失败")) || Contains(status, T("错误")) ||
        Contains(status, T("异常")) || Contains(status, T("超时")) ||
        Contains(status, T("失效")) || Contains(status, "invalid") ||
        Contains(status, "failed") || Contains(status, "Cannot") ||
        Contains(status, "missing") || Contains(status, "Timeout") ||
        Contains(status, "timed out") || Contains(status, "not ready") ||
        Contains(status, "Create config") || Contains(status, "HTTP") ||
        Contains(status, "Cannot") || Contains(status, "error") || Contains(status, "expired") ||
        Contains(status, "Invalid") || Contains(status, "exceeded")) {
        return StatusTone::Error;
    }
    if (Contains(status, T("正在")) || Contains(status, T("请求进行")) ||
        Contains(status, T("取消网络")) || Contains(status, "Loading") || Contains(status, "Testing") ||
        Contains(status, "Cancelling") || Contains(status, "Reading") || Contains(status, "Adding") ||
        Contains(status, "Removing") || Contains(status, "Submitting") || Contains(status, "is running")) {
        return StatusTone::Busy;
    }
    if (Contains(status, T("成功")) || Contains(status, T("完成")) ||
        Contains(status, T("已加载")) || Contains(status, "loaded") || Contains(status, "Loaded") ||
        Contains(status, "OK") || Contains(status, "finished") || Contains(status, "Added") ||
        Contains(status, "Rated") || Contains(status, "Removed")) {
        return StatusTone::Success;
    }
    return StatusTone::Neutral;
}

const char* StatusLabel(StatusTone tone) {
    switch (tone) {
        case StatusTone::Busy: return T("网络请求");
        case StatusTone::Success: return T("完成");
        case StatusTone::Error: return T("需要处理");
        default: return T("状态");
    }
}

// Decodes one UTF-8 code point at *position; invalid bytes map to U+FFFD.
std::uint32_t NextCodepoint(const std::string& text, std::size_t* position) {
    const auto byte = [&text](std::size_t i) { return static_cast<unsigned char>(text[i]); };
    const unsigned char lead = byte(*position);
    int length = lead < 0x80 ? 1 : (lead >> 5) == 0x6 ? 2 : (lead >> 4) == 0xe ? 3 : (lead >> 3) == 0x1e ? 4 : 0;
    if (length == 0 || *position + length > text.size()) {
        ++*position;
        return 0xfffd;
    }
    std::uint32_t codepoint = length == 1 ? lead : lead & (0xff >> (length + 1));
    for (int i = 1; i < length; ++i) codepoint = (codepoint << 6) | (byte(*position + i) & 0x3f);
    *position += static_cast<std::size_t>(length);
    return codepoint;
}


}  // namespace

std::vector<Ui::TextRun> Ui::SplitRuns(const std::string& text, TTF_Font* font) const {
    std::vector<TextRun> runs;
    const auto fallbacks = fallbacks_.find(font);
    for (std::size_t position = 0; position < text.size();) {
        const std::size_t begin = position;
        const std::uint32_t codepoint = NextCodepoint(text, &position);
        TTF_Font* chosen = font;
        // Whitespace and controls stay with the current run's font.
        if (codepoint > 0x20 && !TTF_GlyphIsProvided32(font, codepoint) &&
            fallbacks != fallbacks_.end()) {
            for (TTF_Font* fallback : fallbacks->second) {
                if (TTF_GlyphIsProvided32(fallback, codepoint)) {
                    chosen = fallback;
                    break;
                }
            }
        } else if (codepoint <= 0x20 && !runs.empty()) {
            chosen = runs.back().font;
        }
        if (runs.empty() || runs.back().font != chosen) runs.push_back({chosen, {}});
        runs.back().text.append(text, begin, position - begin);
    }
    return runs;
}

int Ui::TextWidth(const std::string& text, TTF_Font* font) const {
    int total = 0;
    for (const TextRun& run : SplitRuns(text, font)) {
        int width = 0;
        int height = 0;
        if (TTF_SizeUTF8(run.font, run.text.c_str(), &width, &height) == 0) total += width;
    }
    return total;
}

SDL_Surface* Ui::RenderTextSurface(const std::string& text, TTF_Font* font, SDL_Color color,
                                   int wrap_width) const {
    const std::vector<TextRun> runs = SplitRuns(text, font);
    if (runs.size() == 1 && runs.front().font == font) {
        return wrap_width > 0
            ? TTF_RenderUTF8_Blended_Wrapped(font, text.c_str(), color, static_cast<Uint32>(wrap_width))
            : TTF_RenderUTF8_Blended(font, text.c_str(), color);
    }
    // Mixed scripts (e.g. Korean titles): render each run with the font that
    // has its glyphs and join them on a common baseline.
    std::vector<SDL_Surface*> parts;
    int width = 0;
    int ascent = 0;
    int descent = 0;
    for (const TextRun& run : runs) {
        SDL_Surface* part = TTF_RenderUTF8_Blended(run.font, run.text.c_str(), color);
        parts.push_back(part);
        if (part == nullptr) continue;
        width += part->w;
        ascent = std::max(ascent, TTF_FontAscent(run.font));
        descent = std::max(descent, part->h - TTF_FontAscent(run.font));
    }
    SDL_Surface* joined = width > 0
        ? SDL_CreateRGBSurfaceWithFormat(0, width, ascent + descent, 32, SDL_PIXELFORMAT_ARGB8888)
        : nullptr;
    int x = 0;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (parts[i] == nullptr) continue;
        if (joined != nullptr) {
            SDL_SetSurfaceBlendMode(parts[i], SDL_BLENDMODE_NONE);
            SDL_Rect destination{x, ascent - TTF_FontAscent(runs[i].font), parts[i]->w, parts[i]->h};
            SDL_BlitSurface(parts[i], nullptr, joined, &destination);
        }
        x += parts[i]->w;
        SDL_FreeSurface(parts[i]);
    }
    return joined;
}

std::string Ui::FitText(const std::string& text, TTF_Font* font, int max_width) {
    if (text.empty() || font == nullptr || max_width <= 0) return {};
    std::string key = std::to_string(reinterpret_cast<std::uintptr_t>(font)) + ":" +
                      std::to_string(max_width) + ":" + text;
    const auto cached = fit_cache_.find(key);
    if (cached != fit_cache_.end()) return cached->second;
    if (fit_cache_.size() >= 512) fit_cache_.clear();

    std::string result = text;
    if (TextWidth(text, font) > max_width) {
        constexpr const char* ellipsis = "…";
        // Binary search on code point count keeps long titles cheap.
        std::vector<std::size_t> boundaries;
        for (std::size_t position = 0; position < text.size();) {
            NextCodepoint(text, &position);
            boundaries.push_back(position);
        }
        std::size_t low = 0;
        std::size_t high = boundaries.size();
        while (low < high) {
            const std::size_t middle = (low + high + 1) / 2;
            if (TextWidth(text.substr(0, boundaries[middle - 1]) + ellipsis, font) <= max_width)
                low = middle;
            else
                high = middle - 1;
        }
        result = (low == 0 ? std::string() : text.substr(0, boundaries[low - 1])) + ellipsis;
    }
    fit_cache_.emplace(std::move(key), result);
    return result;
}

std::vector<std::string> Ui::WrapText(const std::string& text, TTF_Font* font, int max_width) {
    std::string key = std::to_string(reinterpret_cast<std::uintptr_t>(font)) + ":" +
                      std::to_string(max_width) + ":" + text;
    const auto cached = wrap_cache_.find(key);
    if (cached != wrap_cache_.end()) return cached->second;
    if (wrap_cache_.size() >= 256) wrap_cache_.clear();
    std::vector<std::string>& lines = wrap_cache_[key];
    std::size_t paragraph_begin = 0;
    while (paragraph_begin <= text.size()) {
        const std::size_t newline = text.find('\n', paragraph_begin);
        const std::string paragraph = text.substr(
            paragraph_begin, newline == std::string::npos ? std::string::npos : newline - paragraph_begin);
        std::string line;
        std::size_t last_space = std::string::npos;
        for (std::size_t position = 0; position < paragraph.size();) {
            const std::size_t begin = position;
            const std::uint32_t codepoint = NextCodepoint(paragraph, &position);
            const std::string candidate = line + paragraph.substr(begin, position - begin);
            if (!line.empty() && TextWidth(candidate, font) > max_width) {
                // Prefer breaking Latin text at the last space.
                if (codepoint != ' ' && last_space != std::string::npos && last_space > 0) {
                    lines.push_back(line.substr(0, last_space));
                    line = line.substr(last_space + 1) + paragraph.substr(begin, position - begin);
                } else {
                    lines.push_back(line);
                    line = codepoint == ' ' ? std::string() : paragraph.substr(begin, position - begin);
                }
                last_space = std::string::npos;
                continue;
            }
            if (codepoint == ' ') last_space = line.size();
            line = candidate;
        }
        lines.push_back(line);
        if (newline == std::string::npos) break;
        paragraph_begin = newline + 1;
    }
    return lines;
}

Ui::~Ui() { Shutdown(); }

bool Ui::Initialize(std::string* error) {
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
        if (error) *error = std::string("SDL_Init: ") + SDL_GetError();
        return false;
    }
    window_ = SDL_CreateWindow("EhViewer Switch", SDL_WINDOWPOS_CENTERED,
                               SDL_WINDOWPOS_CENTERED, kWidth, kHeight, 0);
    if (window_ == nullptr) {
        if (error) *error = std::string("SDL_CreateWindow: ") + SDL_GetError();
        return false;
    }
    renderer_ = SDL_CreateRenderer(window_, -1,
                                   SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (renderer_ == nullptr) {
        if (error) *error = std::string("SDL_CreateRenderer: ") + SDL_GetError();
        return false;
    }
    SDL_RenderSetLogicalSize(renderer_, kWidth, kHeight);
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    if (TTF_Init() != 0) {
        if (error) *error = std::string("TTF_Init: ") + TTF_GetError();
        return false;
    }
    IMG_Init(IMG_INIT_JPG | IMG_INIT_PNG | IMG_INIT_WEBP);

    Result result = plInitialize(PlServiceType_User);
    if (R_FAILED(result)) {
        if (error) *error = "Cannot initialize Switch shared fonts";
        return false;
    }
    pl_initialized_ = true;

    // Use Nintendo's Simplified Chinese shared font. It contains the CJK and Latin
    // glyphs needed by this UI and avoids shipping or loading fonts every frame.
    PlFontData font_data{};
    result = plGetSharedFontByType(&font_data, PlSharedFontType_ChineseSimplified);
    if (R_FAILED(result)) result = plGetSharedFontByType(&font_data, PlSharedFontType_Standard);
    if (R_FAILED(result)) {
        if (error) *error = "Cannot load a Switch shared font";
        return false;
    }
    font_small_ = OpenSharedFont(font_data, 19);
    font_body_ = OpenSharedFont(font_data, 26);
    font_title_ = OpenSharedFont(font_data, 38);
    if (font_small_ == nullptr || font_body_ == nullptr || font_title_ == nullptr) {
        if (error) *error = std::string("Cannot open Switch shared font: ") + TTF_GetError();
        return false;
    }

    // The Simplified Chinese font has no Hangul; other system fonts fill in
    // glyphs it lacks (Korean titles, Japanese-only kanji, Nintendo symbols).
    const PlSharedFontType fallback_types[] = {
        PlSharedFontType_KO, PlSharedFontType_Standard, PlSharedFontType_ChineseTraditional,
        PlSharedFontType_NintendoExt};
    for (PlSharedFontType type : fallback_types) {
        PlFontData data{};
        if (R_FAILED(plGetSharedFontByType(&data, type))) continue;
        const std::pair<TTF_Font*, int> sizes[] = {{font_small_, 19}, {font_body_, 26}, {font_title_, 38}};
        for (const auto& size : sizes) {
            TTF_Font* fallback = OpenSharedFont(data, size.second);
            if (fallback == nullptr) continue;
            fallbacks_[size.first].push_back(fallback);
            fallback_fonts_.push_back(fallback);
        }
    }
    return true;
}

void Ui::ClearTextCache() {
    for (auto& entry : text_cache_) {
        if (entry.second.texture != nullptr) SDL_DestroyTexture(entry.second.texture);
    }
    text_cache_.clear();
}

bool Ui::StoreThumbnail(std::int64_t key, SDL_Texture* texture) {
    if (texture == nullptr) return false;
    if (thumbnails_.size() >= kThumbnailLimit && thumbnails_.count(key) == 0) {
        auto oldest = thumbnails_.begin();
        for (auto it = thumbnails_.begin(); it != thumbnails_.end(); ++it) {
            if (it->second.last_used < oldest->second.last_used) oldest = it;
        }
        SDL_DestroyTexture(oldest->second.texture);
        thumbnails_.erase(oldest);
    }
    Thumbnail& slot = thumbnails_[key];
    if (slot.texture != nullptr) SDL_DestroyTexture(slot.texture);
    slot.texture = texture;
    SDL_QueryTexture(texture, nullptr, nullptr, &slot.width, &slot.height);
    slot.last_used = ++thumbnail_clock_;
    return true;
}

bool Ui::SetThumbnail(std::int64_t key, const std::string& encoded, std::string* error) {
    if (renderer_ == nullptr || encoded.empty()) return false;
    SDL_RWops* source = SDL_RWFromConstMem(encoded.data(), static_cast<int>(encoded.size()));
    if (source == nullptr) {
        if (error) *error = SDL_GetError();
        return false;
    }
    SDL_Texture* texture = IMG_LoadTexture_RW(renderer_, source, 1);
    if (texture == nullptr && error) *error = IMG_GetError();
    return StoreThumbnail(key, texture);
}

bool Ui::LoadThumbnailFile(std::int64_t key, const std::string& path) {
    if (renderer_ == nullptr || path.empty()) return false;
    return StoreThumbnail(key, IMG_LoadTexture(renderer_, path.c_str()));
}

bool Ui::HasThumbnail(std::int64_t key) const {
    return thumbnails_.count(key) != 0;
}

void Ui::ClearThumbnails() {
    for (auto& entry : thumbnails_) {
        if (entry.second.texture != nullptr) SDL_DestroyTexture(entry.second.texture);
    }
    thumbnails_.clear();
}

bool Ui::DrawThumbnail(std::int64_t key, int x, int y, int width, int height) {
    const auto found = thumbnails_.find(key);
    if (found == thumbnails_.end() || found->second.width <= 0 || found->second.height <= 0)
        return false;
    found->second.last_used = ++thumbnail_clock_;
    const double scale = std::min(static_cast<double>(width) / found->second.width,
                                  static_cast<double>(height) / found->second.height);
    const int drawn_width = std::max(1, static_cast<int>(found->second.width * scale));
    const int drawn_height = std::max(1, static_cast<int>(found->second.height * scale));
    const SDL_Rect destination{x + (width - drawn_width) / 2, y + (height - drawn_height) / 2,
                               drawn_width, drawn_height};
    SDL_RenderCopy(renderer_, found->second.texture, nullptr, &destination);
    return true;
}

bool Ui::DrawThumbnailRegion(std::int64_t key, int source_x, int source_width, int source_height,
                             int x, int y, int width, int height) {
    const auto found = thumbnails_.find(key);
    if (found == thumbnails_.end() || found->second.width <= 0) return false;
    if (source_width <= 0) return DrawThumbnail(key, x, y, width, height);
    found->second.last_used = ++thumbnail_clock_;
    const int clipped_width = std::min(source_width, found->second.width - source_x);
    const int clipped_height = std::min(source_height, found->second.height);
    if (clipped_width <= 0 || clipped_height <= 0) return false;
    const SDL_Rect source{source_x, 0, clipped_width, clipped_height};
    const double scale = std::min(static_cast<double>(width) / clipped_width,
                                  static_cast<double>(height) / clipped_height);
    const int drawn_width = std::max(1, static_cast<int>(clipped_width * scale));
    const int drawn_height = std::max(1, static_cast<int>(clipped_height * scale));
    const SDL_Rect destination{x + (width - drawn_width) / 2, y + (height - drawn_height) / 2,
                               drawn_width, drawn_height};
    SDL_RenderCopy(renderer_, found->second.texture, &source, &destination);
    return true;
}

void Ui::Shutdown() {
    ClearTextCache();
    ClearThumbnails();
    for (TTF_Font* fallback : fallback_fonts_) TTF_CloseFont(fallback);
    fallback_fonts_.clear();
    fallbacks_.clear();
    fit_cache_.clear();
    wrap_cache_.clear();
    if (font_title_) TTF_CloseFont(font_title_);
    if (font_body_) TTF_CloseFont(font_body_);
    if (font_small_) TTF_CloseFont(font_small_);
    font_title_ = font_body_ = font_small_ = nullptr;
    if (pl_initialized_) plExit();
    pl_initialized_ = false;
    if (TTF_WasInit()) TTF_Quit();
    IMG_Quit();
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_) SDL_DestroyWindow(window_);
    renderer_ = nullptr;
    window_ = nullptr;
    SDL_Quit();
}

bool Ui::PumpEvents() {
    SDL_Event event{};
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) return false;
    }
    return true;
}

void Ui::FillRect(int x, int y, int width, int height, unsigned char r, unsigned char g,
                  unsigned char b, unsigned char a) {
    if (renderer_ == nullptr || width <= 0 || height <= 0) return;
    SDL_SetRenderDrawColor(renderer_, r, g, b, a);
    const SDL_Rect rectangle{x, y, width, height};
    SDL_RenderFillRect(renderer_, &rectangle);
}

void Ui::FillRoundedRect(int x, int y, int width, int height, int radius,
                         unsigned char r, unsigned char g, unsigned char b,
                         unsigned char a) {
    if (width <= 0 || height <= 0) return;
    radius = std::max(0, std::min(radius, std::min(width, height) / 2));
    if (radius == 0) {
        FillRect(x, y, width, height, r, g, b, a);
        return;
    }

    FillRect(x + radius, y, width - radius * 2, height, r, g, b, a);
    FillRect(x, y + radius, width, height - radius * 2, r, g, b, a);
    for (int row = 0; row < radius; ++row) {
        const double dy = static_cast<double>(radius - row) - 0.5;
        const int span = static_cast<int>(std::sqrt(radius * radius - dy * dy));
        const int inset = radius - span;
        FillRect(x + inset, y + row, width - inset * 2, 1, r, g, b, a);
        FillRect(x + inset, y + height - row - 1, width - inset * 2, 1,
                 r, g, b, a);
    }
}

void Ui::DrawCard(int x, int y, int width, int height, bool selected) {
    FillRoundedRect(x, y + 3, width, height, 16, 35, 30, 42, 22);
    if (selected) {
        FillRoundedRect(x, y, width, height, 16, 103, 80, 164);
        FillRoundedRect(x + 2, y + 2, width - 4, height - 4, 14, 245, 238, 255);
    } else {
        FillRoundedRect(x, y, width, height, 16, kOutline.r, kOutline.g, kOutline.b);
        FillRoundedRect(x + 1, y + 1, width - 2, height - 2, 15,
                        kSurface.r, kSurface.g, kSurface.b);
    }
}

void Ui::DrawText(const std::string& source_text, TTF_Font* font, int x, int y,
                  unsigned char r, unsigned char g, unsigned char b, int wrap_width,
                  bool cache) {
    // Literal labels are translated here as a whole; composed messages are
    // translated piecewise where they are built.
    const std::string text = i18n::T(source_text);
    if (text.empty() || font == nullptr || renderer_ == nullptr) return;

    std::string key;
    if (cache) {
        key.reserve(text.size() + 64);
        key = std::to_string(reinterpret_cast<std::uintptr_t>(font)) + ":" +
              std::to_string(static_cast<unsigned int>(r)) + ":" +
              std::to_string(static_cast<unsigned int>(g)) + ":" +
              std::to_string(static_cast<unsigned int>(b)) + ":" +
              std::to_string(wrap_width) + ":" + text;
        const auto found = text_cache_.find(key);
        if (found != text_cache_.end()) {
            const SDL_Rect destination{x, y, found->second.width, found->second.height};
            SDL_RenderCopy(renderer_, found->second.texture, nullptr, &destination);
            return;
        }
    }

    const SDL_Color color{r, g, b, 255};
    SDL_Surface* surface = RenderTextSurface(text, font, color, wrap_width);
    if (surface == nullptr) return;
    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer_, surface);
    if (texture != nullptr) {
        const SDL_Rect destination{x, y, surface->w, surface->h};
        SDL_RenderCopy(renderer_, texture, nullptr, &destination);
        if (cache) {
            if (text_cache_.size() >= kTextCacheLimit) ClearTextCache();
            text_cache_.emplace(std::move(key), CachedText{texture, surface->w, surface->h});
        } else {
            SDL_DestroyTexture(texture);
        }
    }
    SDL_FreeSurface(surface);
}

void Ui::DrawFittedText(const std::string& text, TTF_Font* font, int x, int y,
                        int max_width, unsigned char r, unsigned char g,
                        unsigned char b, bool cache) {
    DrawText(FitText(i18n::T(text), font, max_width), font, x, y, r, g, b, 0, cache);
}

void Ui::DrawSidebar(int active_item) {
    FillRect(0, 0, kSidebarWidth, kHeight, 246, 242, 248);
    FillRect(kSidebarWidth - 1, 0, 1, kHeight, 231, 225, 229);

    FillRoundedRect(22, 20, 48, 48, 16, kPrimary.r, kPrimary.g, kPrimary.b);
    DrawText("E", font_body_, 38, 28, 255, 255, 255);
    DrawText("EhViewer", font_body_, 82, 23, kOnSurface.r, kOnSurface.g, kOnSurface.b);
    DrawText("Switch  v" EHV_SWITCH_VERSION, font_small_, 82, 51, 121, 116, 126);

    // Same entries and order as the EhViewer drawer.
    const char* labels[kSidebarCount] = {T("首页"), T("关注"), T("热门"), T("排行榜"), T("收藏"),
                                         T("我的订阅"), T("下载"), T("历史"), T("设置")};
    const char* glyphs[kSidebarCount] = {"⌂", "◉", "♨", "▲", "♥", "★", "↓", "◷", "⚙"};
    for (int index = 0; index < kSidebarCount; ++index) {
        const int y = 88 + index * 58;
        const bool active = index == active_item;
        AddHit(10, y, 196, 52, kTouchNav, index);
        if (active) FillRoundedRect(12, y + 2, 192, 48, 24, 232, 222, 248);
        DrawText(glyphs[index], font_small_, 32, y + 14,
                 active ? 75 : 95, active ? 50 : 89, active ? 120 : 99);
        DrawText(labels[index], font_small_, 66, y + 13,
                 active ? 33 : 73, active ? 0 : 69, active ? 93 : 79);
        if (index == kSidebarDownloads && !download_badge_.empty()) {
            FillRoundedRect(130, y + 12, 66, 26, 13, kPrimary.r, kPrimary.g, kPrimary.b);
            DrawFittedText(download_badge_, font_small_, 138, y + 14, 54, 255, 255, 255, false);
        }
    }

    FillRoundedRect(20, 624, 176, 66, 14, 237, 232, 240);
    DrawText(T("控制提示"), font_small_, 34, 633, 94, 88, 98);
    DrawText(T("A 确认   B 返回"), font_small_, 34, 660, 73, 69, 79);
    // Tapping the hint card acts as B, so every screen can be left by touch.
    AddHit(20, 624, 176, 66, HidNpadButton_B);
}

void Ui::DrawTopTabs(int active_tab) {
    const char* labels[] = {T("首页"), T("列表"), T("详情")};
    for (int index = 0; index < 3; ++index) {
        const int x = 992 + index * 84;
        const bool active = index == active_tab;
        if (active) FillRoundedRect(x, 27, 76, 40, 20, 232, 222, 248);
        DrawText(labels[index], font_small_, x + 19, 36,
                 active ? 63 : 99, active ? 39 : 93, active ? 103 : 103);
    }
}

void Ui::Begin(const std::string& title, const std::string& subtitle, int active_tab,
               int sidebar_item) {
    SDL_SetRenderDrawColor(renderer_, kBackground.r, kBackground.g, kBackground.b, 255);
    SDL_RenderClear(renderer_);
    hits_.clear();
    DrawSidebar(sidebar_item);
    FillRect(kSidebarWidth, 0, kWidth - kSidebarWidth, kHeaderHeight,
             kSurface.r, kSurface.g, kSurface.b);
    DrawFittedText(title, font_title_, 244, 9, 736, kOnSurface.r, kOnSurface.g, kOnSurface.b);
    DrawFittedText(subtitle, font_small_, 246, 57, 736,
                   kOnSurfaceVariant.r, kOnSurfaceVariant.g, kOnSurfaceVariant.b);
    DrawTopTabs(active_tab);
    FillRect(kSidebarWidth, kHeaderHeight - 1, kWidth - kSidebarWidth, 1,
             231, 225, 229);
}

void Ui::Finish(const std::string& status, const std::string& last_input,
                unsigned long long input_events) {
    const StatusTone tone = GetStatusTone(status);
    SDL_Color background{238, 232, 242, 255};
    SDL_Color accent{kPrimary};
    if (tone == StatusTone::Busy) {
        background = SDL_Color{255, 244, 214, 255};
        accent = SDL_Color{122, 83, 0, 255};
    } else if (tone == StatusTone::Success) {
        background = SDL_Color{222, 248, 231, 255};
        accent = SDL_Color{20, 108, 67, 255};
    } else if (tone == StatusTone::Error) {
        background = SDL_Color{255, 218, 214, 255};
        accent = SDL_Color{186, 26, 26, 255};
    }

    FillRoundedRect(kContentLeft, kStatusTop, kContentRight - kContentLeft, 56, 18,
                    background.r, background.g, background.b);
    FillRoundedRect(252, 658, 90, 32, 16, accent.r, accent.g, accent.b);
    DrawText(StatusLabel(tone), font_small_, 266, 664, 255, 255, 255);

    if (tone == StatusTone::Busy) {
        static constexpr std::array<int, 8> x_offset{{0, 5, 7, 5, 0, -5, -7, -5}};
        static constexpr std::array<int, 8> y_offset{{-7, -5, 0, 5, 7, 5, 0, -5}};
        const int phase = static_cast<int>((SDL_GetTicks() / 110) % 8);
        for (int index = 0; index < 8; ++index) {
            const int alpha = index == phase ? 255 : 90;
            FillRoundedRect(360 + x_offset[index], 674 + y_offset[index], 3, 3, 1,
                            accent.r, accent.g, accent.b,
                            static_cast<unsigned char>(alpha));
        }
    } else {
        FillRoundedRect(355, 669, 10, 10, 5, accent.r, accent.g, accent.b);
    }

    const std::string message = status.empty() ? T("已就绪") : status;
    DrawFittedText(message, font_small_, 376, 663, show_input_debug_ ? 610 : 860,
                   kOnSurface.r, kOnSurface.g, kOnSurface.b);

    if (show_input_debug_) {
        FillRoundedRect(1000, 657, 244, 34, 17, 255, 255, 255, 205);
        char input[128];
        std::snprintf(input, sizeof(input), T("按键  %s   #%llu"),
                      last_input.empty() ? "-" : last_input.c_str(), input_events);
        DrawFittedText(input, font_small_, 1016, 663, 212,
                       kOnSurfaceVariant.r, kOnSurfaceVariant.g, kOnSurfaceVariant.b, false);
    }
    if (overlay_ != nullptr) DrawOverlay(*overlay_);
    SDL_RenderPresent(renderer_);
}

void Ui::DrawOverlay(const OverlayMenu& menu) {
    FillRect(0, 0, kWidth, kHeight, 20, 16, 28, 150);
    // The overlay is modal: only its rows (and "tap outside = cancel") react.
    hits_.clear();
    AddHit(0, 0, kWidth, kHeight, HidNpadButton_B);
    constexpr int kRow = 44;
    const int rows = static_cast<int>(std::min<std::size_t>(menu.items.size(), 11));
    const int height = 96 + rows * kRow + 40;
    const int width = 560;
    const int x = (kWidth - width) / 2;
    const int y = (kHeight - height) / 2;
    AddHit(x, y, width, height, 0);  // taps on the card itself do nothing
    FillRoundedRect(x, y, width, height, 24, kSurface.r, kSurface.g, kSurface.b);
    DrawFittedText(menu.title, font_body_, x + 32, y + 24, width - 64,
                   kOnSurface.r, kOnSurface.g, kOnSurface.b);
    // Keep the selection visible when the list is longer than the card.
    std::size_t first = menu.selected >= static_cast<std::size_t>(rows) ? menu.selected - rows + 1 : 0;
    for (int row = 0; row < rows; ++row) {
        const std::size_t index = first + static_cast<std::size_t>(row);
        if (index >= menu.items.size()) break;
        const int row_y = y + 76 + row * kRow;
        const bool active = index == menu.selected;
        AddHit(x + 20, row_y, width - 40, kRow - 4, HidNpadButton_A, static_cast<int>(index));
        if (active) FillRoundedRect(x + 20, row_y, width - 40, kRow - 4, 20, 232, 222, 248);
        DrawFittedText(menu.items[index], font_body_, x + 44, row_y + 5, width - 88,
                       active ? 33 : kOnSurface.r, active ? 0 : kOnSurface.g, active ? 93 : kOnSurface.b);
    }
    DrawFittedText(menu.hint.empty() ? T("上下选择 · A 确定 · B 取消") : menu.hint, font_small_,
                   x + 32, y + height - 36, width - 64, 121, 116, 126);
}

}  // namespace ehviewer
