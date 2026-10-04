#pragma once

#include <cstdint>
#include <map>
#include <string>

namespace ehviewer {

struct SpiderInfo {
    int start_page = 0;
    std::int64_t gid = 0;
    std::string token;
    int preview_pages = 0;
    int preview_per_page = 0;
    int pages = 0;
    std::map<int, std::string> page_tokens;

    static bool Parse(const std::string& text, SpiderInfo* info,
                      std::string* error = nullptr);
    std::string Serialize() const;
    bool Validate(std::string* error = nullptr) const;
};

}  // namespace ehviewer
