#include "Settings.h"

#include "FileUtil.h"

#include <algorithm>
#include <cstdlib>
#include <sstream>

namespace ehviewer {

const CategoryInfo kCategories[10] = {
    {2, "Doujinshi"}, {4, "Manga"}, {8, "Artist CG"}, {16, "Game CG"}, {512, "Western"},
    {256, "Non-H"}, {32, "Image Set"}, {64, "Cosplay"}, {128, "Asian Porn"}, {1, "Misc"},
};

namespace {

std::string Trim(const std::string& value) {
    const std::size_t begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return {};
    const std::size_t end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

bool ParseInt(const std::string& value, int minimum, int maximum, int* output) {
    if (value.empty()) return false;
    char* end = nullptr;
    const long parsed = std::strtol(value.c_str(), &end, 10);
    if (end == value.c_str() || *end != '\0' || parsed < minimum || parsed > maximum) return false;
    *output = static_cast<int>(parsed);
    return true;
}

bool ParseBool(const std::string& value, bool* output) {
    if (value == "1" || value == "true" || value == "on") { *output = true; return true; }
    if (value == "0" || value == "false" || value == "off") { *output = false; return true; }
    return false;
}

}  // namespace

bool Settings::Parse(const std::string& text, Settings* settings, std::string* error) {
    if (settings == nullptr) return false;
    Settings parsed;
    std::istringstream input(text);
    std::string line;
    int number = 0;
    while (std::getline(input, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == '#') continue;
        const std::size_t equals = line.find('=');
        if (equals == std::string::npos) continue;
        const std::string key = Trim(line.substr(0, equals));
        const std::string value = Trim(line.substr(equals + 1));
        bool ok = true;
        // Unknown keys are ignored so newer files still load in older builds;
        // invalid values fall back to the default instead of failing.
        if (key == "language") {
            ok = ParseInt(value, 0, 2, &number);
            if (ok) parsed.language = static_cast<Language>(number);
        } else if (key == "site") {
            ok = ParseInt(value, 0, 1, &number);
            if (ok) parsed.site = static_cast<Site>(number);
        } else if (key == "reader_orientation") {
            ok = ParseInt(value, 0, 2, &parsed.reader_orientation);
        } else if (key == "reader_fit_width") {
            ok = ParseBool(value, &parsed.reader_fit_width);
        } else if (key == "reader_double_page") {
            ok = ParseBool(value, &parsed.reader_double_page);
        } else if (key == "reader_right_to_left") {
            ok = ParseBool(value, &parsed.reader_right_to_left);
        } else if (key == "prefetch_pages") {
            ok = ParseInt(value, 1, 8, &parsed.prefetch_pages);
        } else if (key == "keep_awake") {
            ok = ParseBool(value, &parsed.keep_awake);
        } else if (key == "list_layout") {
            ok = ParseInt(value, 0, 1, &parsed.list_layout);
        } else if (key == "show_thumbnails") {
            ok = ParseBool(value, &parsed.show_thumbnails);
        } else if (key == "history_enabled") {
            ok = ParseBool(value, &parsed.history_enabled);
        } else if (key == "excluded_categories") {
            ok = ParseInt(value, 0, 1023, &parsed.excluded_categories);
        } else if (key == "show_input_debug") {
            ok = ParseBool(value, &parsed.show_input_debug);
        } else if (key == "proxy_enabled") {
            ok = ParseBool(value, &parsed.proxy_enabled);
        } else if (key == "proxy_url") {
            ok = value.empty() || IsValidProxyUrl(value);
            if (ok) parsed.proxy_url = value;
        } else if (key == "domain_fronting") {
            ok = ParseInt(value, -1, 1, &parsed.domain_fronting);
        } else if (key == "builtin_hosts") {
            ok = ParseInt(value, -1, 1, &parsed.builtin_hosts);
        } else if (key == "doh") {
            ok = ParseInt(value, -1, 1, &parsed.doh);
        }
        if (!ok && error != nullptr) *error = "Ignored invalid value for " + key;
    }
    *settings = parsed;
    return true;
}

std::string Settings::Serialize() const {
    std::ostringstream output;
    output << "# EhViewer Switch settings\n"
           << "language=" << static_cast<int>(language) << '\n'
           << "site=" << static_cast<int>(site) << '\n'
           << "reader_orientation=" << reader_orientation << '\n'
           << "reader_fit_width=" << (reader_fit_width ? 1 : 0) << '\n'
           << "reader_double_page=" << (reader_double_page ? 1 : 0) << '\n'
           << "reader_right_to_left=" << (reader_right_to_left ? 1 : 0) << '\n'
           << "prefetch_pages=" << prefetch_pages << '\n'
           << "keep_awake=" << (keep_awake ? 1 : 0) << '\n'
           << "list_layout=" << list_layout << '\n'
           << "show_thumbnails=" << (show_thumbnails ? 1 : 0) << '\n'
           << "history_enabled=" << (history_enabled ? 1 : 0) << '\n'
           << "excluded_categories=" << excluded_categories << '\n'
           << "show_input_debug=" << (show_input_debug ? 1 : 0) << '\n'
           << "proxy_enabled=" << (proxy_enabled ? 1 : 0) << '\n'
           << "proxy_url=" << proxy_url << '\n'
           << "domain_fronting=" << domain_fronting << '\n'
           << "builtin_hosts=" << builtin_hosts << '\n'
           << "doh=" << doh << '\n';
    return output.str();
}

bool IsValidProxyUrl(const std::string& url) {
    if (url.size() > 512) return false;
    for (unsigned char c : url) {
        if (c <= 0x20 || c == 0x7f) return false;
    }
    for (const char* scheme : {"http://", "https://", "socks4://", "socks4a://", "socks5://", "socks5h://"}) {
        const std::string prefix = scheme;
        if (url.size() > prefix.size() && url.compare(0, prefix.size(), prefix) == 0) return true;
    }
    return false;
}

std::string RedactProxyUrl(const std::string& url) {
    const std::size_t scheme = url.find("://");
    const std::size_t at = url.find('@');
    if (scheme == std::string::npos || at == std::string::npos || at < scheme) return url;
    return url.substr(0, scheme + 3) + "***" + url.substr(at);
}

bool Settings::Load(const std::string& path, Settings* settings, std::string* error) {
    std::string text;
    if (!file::Exists(path)) {
        *settings = Settings();
        return true;
    }
    if (!file::ReadAll(path, &text, error, 64U * 1024U)) return false;
    return Parse(text, settings, error);
}

bool Settings::Save(const std::string& path, std::string* error) const {
    return file::WriteAllAtomic(path, Serialize(), error);
}

}  // namespace ehviewer
