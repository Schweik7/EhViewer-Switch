#include "SpiderInfo.h"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <limits>
#include <sstream>
#include <utility>
#include <vector>

namespace ehviewer {
namespace {

constexpr int kMaxPages = 100000;
constexpr std::size_t kMaxTokenLength = 256;

bool ParseInteger(const std::string& value, int base, long long min_value,
                  long long max_value, long long* output) {
    if (value.empty() || value.size() > 32) return false;
    try {
        std::size_t used = 0;
        const long long parsed = std::stoll(value, &used, base);
        if (used != value.size() || parsed < min_value || parsed > max_value) return false;
        *output = parsed;
        return true;
    } catch (...) {
        return false;
    }
}

std::vector<std::string> Lines(const std::string& text) {
    std::istringstream input(text);
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
    }
    return lines;
}

bool Fail(std::string* error, const std::string& message) {
    if (error) *error = message;
    return false;
}

}  // namespace

bool SpiderInfo::Parse(const std::string& text, SpiderInfo* info, std::string* error) {
    if (info == nullptr) return Fail(error, "SpiderInfo output is null");
    if (text.size() > 2U * 1024U * 1024U) return Fail(error, "SpiderInfo is too large");
    const std::vector<std::string> lines = Lines(text);
    if (lines.empty()) return Fail(error, "SpiderInfo is empty");

    std::size_t cursor = 0;
    const bool version2 = lines[0] == "VERSION2";
    if (version2) ++cursor;
    const std::size_t required = 7;
    if (lines.size() < cursor + required) return Fail(error, "SpiderInfo header is incomplete");

    SpiderInfo parsed;
    long long number = 0;
    if (!ParseInteger(lines[cursor++], 16, 0, kMaxPages, &number))
        return Fail(error, "Invalid start page");
    parsed.start_page = static_cast<int>(number);
    if (!ParseInteger(lines[cursor++], 10, 1, std::numeric_limits<std::int64_t>::max(), &number))
        return Fail(error, "Invalid gallery id");
    parsed.gid = number;
    parsed.token = lines[cursor++];
    ++cursor;  // Historical reserved line.
    if (!ParseInteger(lines[cursor++], 10, 0, kMaxPages, &number))
        return Fail(error, "Invalid preview page count");
    parsed.preview_pages = static_cast<int>(number);
    const std::string previews_per_page = lines[cursor++];
    if (version2) {
        if (!ParseInteger(previews_per_page, 10, 0, kMaxPages, &number))
            return Fail(error, "Invalid previews-per-page value");
        parsed.preview_per_page = static_cast<int>(number);
    }
    if (!ParseInteger(lines[cursor++], 10, 1, kMaxPages, &number))
        return Fail(error, "Invalid page count");
    parsed.pages = static_cast<int>(number);

    for (; cursor < lines.size(); ++cursor) {
        if (lines[cursor].empty()) continue;
        std::istringstream pair(lines[cursor]);
        int index = -1;
        std::string token;
        std::string extra;
        if (!(pair >> index >> token) || (pair >> extra) || index < 0 ||
            index >= parsed.pages || token.size() > kMaxTokenLength) {
            return Fail(error, "Invalid page token entry");
        }
        parsed.page_tokens[index] = token;
    }

    if (!parsed.Validate(error)) return false;
    *info = std::move(parsed);
    return true;
}

std::string SpiderInfo::Serialize() const {
    std::ostringstream output;
    output << "VERSION2\n" << std::hex << std::nouppercase << start_page << std::dec << '\n'
           << gid << '\n' << token << "\n0\n" << preview_pages << '\n'
           << preview_per_page << '\n' << pages << '\n';
    for (const auto& [index, page_token] : page_tokens) {
        output << index << ' ' << page_token << '\n';
    }
    return output.str();
}

bool SpiderInfo::Validate(std::string* error) const {
    if (gid <= 0) return Fail(error, "Gallery id must be positive");
    if (pages <= 0 || pages > kMaxPages) return Fail(error, "Page count is out of range");
    if (start_page < 0 || start_page >= pages) return Fail(error, "Start page is out of range");
    if (token.empty() || token.size() > kMaxTokenLength)
        return Fail(error, "Gallery token is invalid");
    for (const auto& [index, page_token] : page_tokens) {
        if (index < 0 || index >= pages || page_token.empty() ||
            page_token.size() > kMaxTokenLength) {
            return Fail(error, "Page token is invalid");
        }
    }
    return true;
}

}  // namespace ehviewer
