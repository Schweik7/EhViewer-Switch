#include "ReleaseInfo.h"

#include "Json.h"

#include <vector>

namespace ehviewer {
namespace {

std::vector<long> VersionParts(const std::string& text) {
    std::vector<long> parts;
    std::size_t index = !text.empty() && (text[0] == 'v' || text[0] == 'V') ? 1 : 0;
    long current = 0;
    bool digit = false;
    for (; index <= text.size(); ++index) {
        const char c = index < text.size() ? text[index] : '.';
        if (c >= '0' && c <= '9') {
            current = current * 10 + (c - '0');
            digit = true;
        } else if (c == '.' && digit) {
            parts.push_back(current);
            current = 0;
            digit = false;
        } else {
            if (digit) parts.push_back(current);
            break;
        }
    }
    return parts;
}

}  // namespace

bool ParseReleaseInfo(const std::string& json_text, const std::string& asset_name, ReleaseInfo* out,
                      std::string* error) {
    if (out == nullptr) return false;
    *out = ReleaseInfo();
    json::Value root;
    if (!json::Parse(json_text, &root, error) || root.type != json::Value::Type::Object) {
        if (error != nullptr && error->empty()) *error = "invalid JSON";
        return false;
    }
    if (root.Find("tag_name") == nullptr && root.Find("message") != nullptr) {
        if (error != nullptr) *error = root.GetString("message");
        return false;
    }
    out->tag = root.GetString("tag_name");
    if (out->tag.empty()) {
        if (error != nullptr) *error = "no tag_name";
        return false;
    }
    out->notes = root.GetString("body");
    const json::Value* assets = root.Find("assets");
    if (assets != nullptr && assets->type == json::Value::Type::Array) {
        for (const json::Value& asset : assets->array) {
            if (asset.GetString("name") != asset_name) continue;
            out->asset_url = asset.GetString("browser_download_url");
            const std::int64_t size = asset.GetInt("size");
            out->asset_size = size > 0 ? static_cast<std::uint64_t>(size) : 0;
            break;
        }
    }
    return true;
}

bool IsNewerVersion(const std::string& remote, const std::string& local) {
    const std::vector<long> a = VersionParts(remote);
    const std::vector<long> b = VersionParts(local);
    if (a.empty()) return false;
    const std::size_t count = a.size() > b.size() ? a.size() : b.size();
    for (std::size_t i = 0; i < count; ++i) {
        const long x = i < a.size() ? a[i] : 0;
        const long y = i < b.size() ? b[i] : 0;
        if (x != y) return x > y;
    }
    return false;
}

bool LooksLikeNro(const std::string& bytes) {
    return bytes.size() > 0x14 && bytes.compare(0x10, 4, "NRO0") == 0;
}

}  // namespace ehviewer
