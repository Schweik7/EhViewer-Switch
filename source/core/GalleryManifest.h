#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ehviewer {

struct ManifestFile {
    std::string physical_name;
    std::string original_name;
    std::string role;
    int page_index = -1;
    std::uint64_t size = 0;
    std::string sha256;
};

struct GalleryManifest {
    int format_version = 1;
    std::int64_t gid = 0;
    std::string token;
    std::string title;
    std::string title_jpn;
    std::string original_directory_name;
    std::string category;
    int current_page = 0;
    int total_pages = 0;
    std::vector<ManifestFile> files;

    bool Validate(std::string* error = nullptr) const;
    std::string SerializeJson() const;
    // Parses and validates JSON produced by SerializeJson().
    static bool Parse(const std::string& json, GalleryManifest* manifest,
                      std::string* error = nullptr);
};

}  // namespace ehviewer
