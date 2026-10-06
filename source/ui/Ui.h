#pragma once

#include "core/GalleryParser.h"
#include "core/Library.h"
#include "core/ListLayout.h"
#include "core/Subscriptions.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;
struct SDL_Surface;
struct SDL_Color;
struct _TTF_Font;
typedef struct _TTF_Font TTF_Font;

namespace ehviewer {

struct DownloadProgress;

// A modal list drawn on top of the current screen (favorite folders, rating).
struct OverlayMenu {
    std::string title;
    std::vector<std::string> items;
    std::size_t selected = 0;
    std::string hint;
};

struct SettingRow {
    std::string label;
    std::string value;
    std::string description;
    bool header = false;  // section title, not selectable
    int id = 0;           // what the row changes (App-defined)
};

// Touch targets recorded while drawing. A tap on a region selects `index`
// (when >= 0) on the current list and then acts like pressing `buttons`.
struct HitRegion {
    int x = 0, y = 0, width = 0, height = 0;
    std::uint64_t buttons = 0;
    int index = -1;
};

// Virtual buttons above every HidNpadButton bit. For these the hit region's
// index names the target instead of a list row:
//   kTouchNav    -> sidebar item (see SidebarItem)
//   kTouchTag    -> flattened tag index of the shown gallery
//   kTouchUploader -> search the gallery's uploader
constexpr std::uint64_t kTouchNav = 1ULL << 56;
constexpr std::uint64_t kTouchTag = 1ULL << 57;
constexpr std::uint64_t kTouchUploader = 1ULL << 58;
constexpr std::uint64_t kTouchSpecialMask = kTouchNav | kTouchTag | kTouchUploader;

// The EhViewer drawer, top to bottom.
enum SidebarItem {
    kSidebarHome, kSidebarWatched, kSidebarPopular, kSidebarToplist, kSidebarFavorites,
    kSidebarSubscriptions, kSidebarDownloads, kSidebarHistory, kSidebarSettings, kSidebarCount,
    kSidebarNone = -1
};

// Thumbnail cache key of a preview sprite sheet (negative, so it never
// collides with gallery ids).
inline std::int64_t PreviewImageKey(const std::string& url) {
    return -static_cast<std::int64_t>(std::hash<std::string>{}(url) & 0x3fffffffffffffffULL) - 1;
}

class Ui {
public:
    ~Ui();

    bool Initialize(std::string* error = nullptr);
    void Shutdown();
    bool PumpEvents();

    // layout: 0 rows, 1 grid (see core/ListLayout.h); scroll in pixels.
    void DrawGalleryList(const std::vector<GallerySummary>& galleries, std::size_t selected,
                         const std::string& title, const std::string& subtitle, int sidebar_item,
                         int layout, double scroll, const std::string& footer,
                         const std::string& status, const std::string& last_input,
                         unsigned long long input_events);
    // Saved searches; selected row, A opens.
    void DrawSubscriptions(const std::vector<Subscription>& subscriptions, std::size_t selected,
                           const std::string& status, const std::string& last_input,
                           unsigned long long input_events);
    void DrawSettings(const std::vector<SettingRow>& rows, std::size_t selected,
                      const std::string& status, const std::string& last_input,
                      unsigned long long input_events);
    // selected indexes the flattened tag list (all groups in order).
    void DrawTags(const GalleryDetail& detail, std::size_t selected, const std::string& status,
                  const std::string& last_input, unsigned long long input_events);
    // local_state: 0 = not local, 1 = downloading/incomplete, 2 = complete.
    void DrawGalleryDetail(const GalleryDetail& detail, int local_state,
                           const DownloadProgress& download, const std::string& status,
                           const std::string& last_input, unsigned long long input_events);
    // *scroll is a line offset; it is clamped to the available content.
    void DrawComments(const GalleryDetail& detail, int* scroll, const std::string& status,
                      const std::string& last_input, unsigned long long input_events);
    // Preview thumbnails of the shown gallery; keys from PreviewImageKey().
    void DrawPreviews(const GalleryDetail& detail, std::size_t selected, std::size_t first_row,
                      const std::string& footer, const std::string& status,
                      const std::string& last_input, unsigned long long input_events);
    static constexpr int kPreviewColumns = 6;
    static constexpr int kPreviewRows = 2;
    void DrawLibrary(const std::vector<LibraryEntry>& entries, std::size_t selected,
                     const DownloadProgress& download, const std::string& status,
                     const std::string& last_input, unsigned long long input_events);

    // Thumbnail textures keyed by gallery id, decoded from encoded bytes.
    bool SetThumbnail(std::int64_t key, const std::string& encoded, std::string* error = nullptr);
    bool LoadThumbnailFile(std::int64_t key, const std::string& path);
    bool HasThumbnail(std::int64_t key) const;
    void ClearThumbnails();

    // Short text shown under the "下载" sidebar item, e.g. "12/40".
    void SetDownloadBadge(const std::string& badge) { download_badge_ = badge; }
    // Shown above every screen until cleared (nullptr clears).
    void SetOverlay(const OverlayMenu* menu) { overlay_ = menu; }
    void SetShowInputDebug(bool show) { show_input_debug_ = show; }
    // Touch targets of the last drawn frame, topmost last.
    const std::vector<HitRegion>& HitRegions() const { return hits_; }
    // Lines that fit on the comments screen; touch scrolling uses it.
    static constexpr int kCommentLineHeight = 29;

private:
    friend class ReaderView;

    struct CachedText {
        SDL_Texture* texture{};
        int width{};
        int height{};
    };
    struct TextRun {
        TTF_Font* font{};
        std::string text;
    };
    struct Thumbnail {
        SDL_Texture* texture{};
        int width{};
        int height{};
        unsigned long long last_used{};
    };

    void Begin(const std::string& title, const std::string& subtitle, int active_tab,
               int sidebar_item = kSidebarNone);
    void Finish(const std::string& status, const std::string& last_input,
                unsigned long long input_events);
    void DrawSidebar(int active_item);
    void DrawTopTabs(int active_tab);
    void FillRect(int x, int y, int width, int height, unsigned char r, unsigned char g,
                  unsigned char b, unsigned char a = 255);
    void FillRoundedRect(int x, int y, int width, int height, int radius,
                         unsigned char r, unsigned char g, unsigned char b,
                         unsigned char a = 255);
    void DrawCard(int x, int y, int width, int height, bool selected = false);
    void DrawText(const std::string& text, TTF_Font* font, int x, int y,
                  unsigned char r, unsigned char g, unsigned char b,
                  int wrap_width = 0, bool cache = true);
    void DrawFittedText(const std::string& text, TTF_Font* font, int x, int y,
                        int max_width, unsigned char r, unsigned char g,
                        unsigned char b, bool cache = true);
    // Draws the thumbnail for key scaled to fit inside the box; returns false
    // when it is not loaded yet.
    bool DrawThumbnail(std::int64_t key, int x, int y, int width, int height);
    // Draws part of a sprite sheet (preview thumbnails); source_width 0 = all.
    bool DrawThumbnailRegion(std::int64_t key, int source_x, int source_width, int source_height,
                             int x, int y, int width, int height);
    bool StoreThumbnail(std::int64_t key, SDL_Texture* texture);
    void ClearTextCache();
    // Text helpers that fall back to other system fonts for missing glyphs.
    std::vector<TextRun> SplitRuns(const std::string& text, TTF_Font* font) const;
    int TextWidth(const std::string& text, TTF_Font* font) const;
    SDL_Surface* RenderTextSurface(const std::string& text, TTF_Font* font, SDL_Color color,
                                   int wrap_width) const;
    std::string FitText(const std::string& text, TTF_Font* font, int max_width);
    std::vector<std::string> WrapText(const std::string& text, TTF_Font* font, int max_width);

    SDL_Window* window_{};
    SDL_Renderer* renderer_{};
    TTF_Font* font_small_{};
    TTF_Font* font_body_{};
    TTF_Font* font_title_{};
    bool pl_initialized_{};
    std::unordered_map<std::string, CachedText> text_cache_;
    std::unordered_map<TTF_Font*, std::vector<TTF_Font*>> fallbacks_;
    std::vector<TTF_Font*> fallback_fonts_;
    std::unordered_map<std::string, std::string> fit_cache_;
    std::unordered_map<std::string, std::vector<std::string>> wrap_cache_;
    std::unordered_map<std::int64_t, Thumbnail> thumbnails_;
    unsigned long long thumbnail_clock_{};
    std::string download_badge_;
    const OverlayMenu* overlay_{};
    void DrawOverlay(const OverlayMenu& menu);
    bool show_input_debug_{};
    std::vector<HitRegion> hits_;
    void AddHit(int x, int y, int width, int height, std::uint64_t buttons, int index = -1) {
        hits_.push_back({x, y, width, height, buttons, index});
    }
};

}  // namespace ehviewer
