#include "Library.h"

#include "FileUtil.h"

#include <algorithm>
#include <set>
#include <utility>

namespace ehviewer {
namespace {

const char* const kExtensions[] = {"jpg", "png", "gif", "webp"};

bool LoadManifest(const std::string& directory, GalleryManifest* manifest) {
    std::string text;
    return file::ReadAll(directory + "/manifest.json", &text, nullptr, 4U * 1024U * 1024U) &&
           GalleryManifest::Parse(text, manifest);
}

void ScanRoot(const std::string& root, bool complete, std::set<std::int64_t>* seen,
              std::vector<LibraryEntry>* entries) {
    std::vector<file::DirectoryEntry> directories;
    if (!file::ListDirectory(root, &directories)) return;
    for (const file::DirectoryEntry& directory : directories) {
        if (!directory.directory || directory.name.compare(0, 2, "g_") != 0) continue;
        LibraryEntry entry;
        entry.directory = root + "/" + directory.name;
        entry.complete = complete;
        if (!LoadManifest(entry.directory, &entry.manifest)) continue;
        if ("g_" + std::to_string(entry.manifest.gid) != directory.name) continue;
        entry.gid = entry.manifest.gid;
        if (!seen->insert(entry.gid).second) continue;
        for (const ManifestFile& file : entry.manifest.files) {
            if (file.role == "cover") entry.cover_path = entry.directory + "/" + file.physical_name;
        }
        if (!complete) {
            std::vector<file::DirectoryEntry> files;
            file::ListDirectory(entry.directory, &files);
            for (const file::DirectoryEntry& page : files) {
                if (!page.directory && StorageLayout::PageIndexFromFileName(page.name) >= 0)
                    ++entry.pages_present;
            }
        }
        entries->push_back(std::move(entry));
    }
}

}  // namespace

bool ScanLibrary(const StorageLayout& layout, std::vector<LibraryEntry>* entries,
                 std::string* error) {
    if (entries == nullptr) {
        if (error) *error = "Library output is null";
        return false;
    }
    entries->clear();
    if (!file::IsDirectory(layout.GalleriesRoot())) {
        if (error) *error = "Cannot open directory: " + layout.GalleriesRoot();
        return false;
    }
    std::set<std::int64_t> seen;
    ScanRoot(layout.GalleriesRoot(), true, &seen, entries);
    ScanRoot(layout.IncomingRoot(), false, &seen, entries);
    std::sort(entries->begin(), entries->end(), [](const LibraryEntry& a, const LibraryEntry& b) {
        return a.gid > b.gid;
    });
    return true;
}

std::vector<std::string> PagePaths(const LibraryEntry& entry) {
    std::vector<const ManifestFile*> pages;
    for (const ManifestFile& file : entry.manifest.files) {
        if (file.role == "page" && file.page_index >= 0) pages.push_back(&file);
    }
    std::sort(pages.begin(), pages.end(), [](const ManifestFile* a, const ManifestFile* b) {
        return a->page_index < b->page_index;
    });
    std::vector<std::string> paths;
    paths.reserve(pages.size());
    for (const ManifestFile* page : pages) paths.push_back(entry.directory + "/" + page->physical_name);
    return paths;
}

std::vector<std::string> PageDirectories(const StorageLayout& layout, std::int64_t gid) {
    return {layout.GalleryDirectory(gid), layout.IncomingDirectory(gid)};
}

std::string FindPageFile(const std::vector<std::string>& directories, int index) {
    for (const std::string& directory : directories) {
        for (const char* extension : kExtensions) {
            const std::string path = directory + "/" + StorageLayout::PageFileName(index, extension);
            if (file::FileSize(path) > 0) return path;
        }
    }
    return {};
}

bool SaveReadingProgress(LibraryEntry* entry, int current_page, std::string* error) {
    if (entry == nullptr) return false;
    GalleryManifest updated = entry->manifest;
    updated.current_page = std::max(0, std::min(current_page, std::max(0, updated.total_pages - 1)));
    if (updated.current_page == entry->manifest.current_page) return true;
    if (!updated.Validate(error) ||
        !file::WriteAllAtomic(entry->directory + "/manifest.json", updated.SerializeJson(), error)) {
        return false;
    }
    entry->manifest = std::move(updated);
    return true;
}

bool SaveReadingProgress(const StorageLayout& layout, std::int64_t gid, int current_page,
                         std::string* error) {
    for (const std::string& directory : PageDirectories(layout, gid)) {
        LibraryEntry entry;
        entry.directory = directory;
        if (!LoadManifest(directory, &entry.manifest) || entry.manifest.gid != gid) continue;
        return SaveReadingProgress(&entry, current_page, error);
    }
    if (error) *error = "No manifest for this gallery";
    return false;
}

}  // namespace ehviewer
