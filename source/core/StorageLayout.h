#pragma once

#include <cstdint>
#include <string>

namespace ehviewer {

class StorageLayout {
public:
    explicit StorageLayout(std::string library_root);

    const std::string& root() const { return root_; }
    std::string GalleriesRoot() const;
    std::string IncomingRoot() const;
    std::string GalleryDirectory(std::int64_t gid) const;
    std::string IncomingDirectory(std::int64_t gid) const;
    std::string ManifestPath(std::int64_t gid) const;

    static std::string PageFileName(int zero_based_page, const std::string& extension);
    static std::string SanitizeExtension(const std::string& extension);
    static bool IsSafePhysicalComponent(const std::string& component);
    // Parses "p_00000012.jpg" back to page index 11; returns -1 otherwise.
    static int PageIndexFromFileName(const std::string& name);

    // Names used only inside manifest.json for the PC export. They follow the
    // Android download layout: "<gid>-<title>/00000001.jpg".
    static std::string OriginalDirectoryName(std::int64_t gid, const std::string& title);
    static std::string OriginalPageName(int zero_based_page, const std::string& extension);

    // Returns "jpg", "png", "gif" or "webp" from the file magic, or "" if the
    // bytes are not a supported image (e.g. an HTML error page).
    static std::string DetectImageExtension(const std::string& bytes);

private:
    std::string root_;
};

}  // namespace ehviewer
