#pragma once

#include <string>

namespace ehviewer {

// User preferences, stored as key=value lines in settings.ini. Cookies and
// the proxy stay in cookies.ini; the network switches here override the
// cookies.ini defaults only once the user changes them (-1 = not set).
struct Settings {
    enum class Language { Auto, Chinese, English };
    enum class Site { Auto, EHentai };  // Auto = ExHentai when igneous is set

    Language language = Language::Auto;
    Site site = Site::Auto;

    // Reader defaults.
    int reader_orientation = 0;  // 0 D-pad at bottom, 1 D-pad on top, 2 landscape
    bool reader_fit_width = true;
    bool reader_double_page = false;  // landscape only
    bool reader_right_to_left = true;
    int prefetch_pages = 3;  // 1..8 pages ahead
    bool keep_awake = true;  // no auto sleep while reading or downloading
    bool reader_guide_shown = false;  // tap-zone guide seen once (like Android)
    bool reader_show_clock = true;
    bool reader_show_battery = true;
    int reader_auto_page_seconds = 0;  // 0 = off, else 3..30

    // Browsing.
    int list_layout = 1;  // 0 = rows, 1 = thumbnail grid (Android-like)
    bool show_thumbnails = true;
    bool history_enabled = true;
    // E-Hentai f_cats value: bit set = category hidden (Misc=1 ... Western=512).
    int excluded_categories = 0;

    // Interface.
    bool show_input_debug = false;
    bool auto_check_update = true;  // VPS first, then GitHub

    // Proxy: off by default. Direct access (domain fronting) works for
    // ExHentai; the proxy is only needed for e-hentai.org and toplists.
    bool proxy_enabled = false;
    // Empty uses the proxy= value from cookies.ini, if any.
    std::string proxy_url;

    // Network overrides; -1 keeps the cookies.ini value.
    int domain_fronting = -1;
    int builtin_hosts = -1;
    int doh = -1;

    static bool Parse(const std::string& text, Settings* settings, std::string* error = nullptr);
    std::string Serialize() const;
    static bool Load(const std::string& path, Settings* settings, std::string* error = nullptr);
    bool Save(const std::string& path, std::string* error = nullptr) const;
};

// True for http(s)/socks proxy URLs without whitespace or control characters.
bool IsValidProxyUrl(const std::string& url);
// "http://user:secret@host:1080" -> "http://***@host:1080" for display.
std::string RedactProxyUrl(const std::string& url);

// Gallery categories in E-Hentai's f_cats bit order.
struct CategoryInfo {
    int bit;
    const char* name;
};
extern const CategoryInfo kCategories[10];

}  // namespace ehviewer
