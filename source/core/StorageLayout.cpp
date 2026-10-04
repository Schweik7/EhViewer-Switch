#include "StorageLayout.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <utility>

namespace ehviewer {
namespace {

std::string Join(const std::string& left, const std::string& right) {
    if (left.empty()) return right;
    if (left.back() == '/' || left.back() == '\\') return left + right;
    return left + "/" + right;
}

std::string GalleryName(std::int64_t gid) {
    return "g_" + std::to_string(gid);
}

}  // namespace

StorageLayout::StorageLayout(std::string library_root) : root_(std::move(library_root)) {
    while (root_.size() > 1 && (root_.back() == '/' || root_.back() == '\\')) root_.pop_back();
}

std::string StorageLayout::GalleriesRoot() const { return Join(root_, "galleries"); }
std::string StorageLayout::IncomingRoot() const { return Join(root_, ".incoming"); }
std::string StorageLayout::GalleryDirectory(std::int64_t gid) const {
    return Join(GalleriesRoot(), GalleryName(gid));
}
std::string StorageLayout::IncomingDirectory(std::int64_t gid) const {
    return Join(IncomingRoot(), GalleryName(gid));
}
std::string StorageLayout::ManifestPath(std::int64_t gid) const {
    return Join(GalleryDirectory(gid), "manifest.json");
}

std::string StorageLayout::PageFileName(int zero_based_page, const std::string& extension) {
    if (zero_based_page < 0) zero_based_page = 0;
    char buffer[48];
    std::snprintf(buffer, sizeof(buffer), "p_%08d.%s", zero_based_page + 1,
                  SanitizeExtension(extension).c_str());
    return buffer;
}

std::string StorageLayout::SanitizeExtension(const std::string& extension) {
    std::string value = extension;
    if (!value.empty() && value.front() == '.') value.erase(value.begin());
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if (value == "jpeg") value = "jpg";
    if (value == "jpg" || value == "png" || value == "webp" || value == "gif") return value;
    return "jpg";
}

int StorageLayout::PageIndexFromFileName(const std::string& name) {
    // p_ + 8 digits + . + extension
    if (name.size() < 12 || name.compare(0, 2, "p_") != 0 || name[10] != '.') return -1;
    int value = 0;
    for (std::size_t i = 2; i < 10; ++i) {
        if (name[i] < '0' || name[i] > '9') return -1;
        value = value * 10 + (name[i] - '0');
    }
    if (value <= 0 || SanitizeExtension(name.substr(11)) != name.substr(11)) return -1;
    return value - 1;
}

std::string StorageLayout::OriginalDirectoryName(std::int64_t gid, const std::string& title) {
    std::string clean;
    clean.reserve(title.size());
    for (unsigned char c : title) {
        if (c < 0x20 || c == 0x7f || std::strchr("\\/:*?\"<>|", c) != nullptr) clean.push_back(' ');
        else clean.push_back(static_cast<char>(c));
    }
    while (!clean.empty() && (clean.back() == ' ' || clean.back() == '.')) clean.pop_back();
    std::size_t begin = 0;
    while (begin < clean.size() && clean[begin] == ' ') ++begin;
    clean.erase(0, begin);
    // Keep the directory name well below common 255-byte limits without
    // splitting a UTF-8 sequence.
    if (clean.size() > 180) {
        std::size_t cut = 180;
        while (cut > 0 && (static_cast<unsigned char>(clean[cut]) & 0xc0) == 0x80) --cut;
        clean.resize(cut);
    }
    const std::string id = std::to_string(gid);
    return clean.empty() ? id : id + "-" + clean;
}

std::string StorageLayout::OriginalPageName(int zero_based_page, const std::string& extension) {
    if (zero_based_page < 0) zero_based_page = 0;
    char buffer[48];
    std::snprintf(buffer, sizeof(buffer), "%08d.%s", zero_based_page + 1,
                  SanitizeExtension(extension).c_str());
    return buffer;
}

std::string StorageLayout::DetectImageExtension(const std::string& bytes) {
    const auto at = [&bytes](std::size_t i) { return static_cast<unsigned char>(bytes[i]); };
    if (bytes.size() >= 3 && at(0) == 0xff && at(1) == 0xd8 && at(2) == 0xff) return "jpg";
    if (bytes.size() >= 8 && bytes.compare(0, 8, "\x89PNG\r\n\x1a\n") == 0) return "png";
    if (bytes.size() >= 6 && (bytes.compare(0, 6, "GIF87a") == 0 || bytes.compare(0, 6, "GIF89a") == 0))
        return "gif";
    if (bytes.size() >= 12 && bytes.compare(0, 4, "RIFF") == 0 && bytes.compare(8, 4, "WEBP") == 0)
        return "webp";
    return {};
}

bool StorageLayout::IsSafePhysicalComponent(const std::string& component) {
    if (component.empty() || component == "." || component == "..") return false;
    return std::all_of(component.begin(), component.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
               (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
    });
}

}  // namespace ehviewer
