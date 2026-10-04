#pragma once

#include "GalleryManifest.h"
#include "StorageLayout.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ehviewer {

struct LibraryEntry {
    std::int64_t gid = 0;
    std::string directory;
    std::string cover_path;
    GalleryManifest manifest;
    // False for galleries still in .incoming (downloading or with failed pages).
    bool complete = true;
    // Page files present on disk; only counted for incomplete galleries.
    int pages_present = 0;
};

// Lists published galleries (galleries/g_<gid>/manifest.json) and galleries
// being downloaded (.incoming/g_<gid>/manifest.json), newest gid first.
// Directories with a missing or invalid manifest are skipped.
bool ScanLibrary(const StorageLayout& layout, std::vector<LibraryEntry>* entries,
                 std::string* error = nullptr);

// Absolute page paths in reading order, taken only from the manifest.
std::vector<std::string> PagePaths(const LibraryEntry& entry);

// Directories that may hold pages of gid, published first. The reader probes
// them so it can show pages while the gallery is still downloading.
std::vector<std::string> PageDirectories(const StorageLayout& layout, std::int64_t gid);

// Finds page index (zero-based) in directories, or returns "".
std::string FindPageFile(const std::vector<std::string>& directories, int index);

// Persists the reading position in manifest.json.
bool SaveReadingProgress(LibraryEntry* entry, int current_page, std::string* error = nullptr);
// Same, for whichever copy (published or downloading) of gid exists.
bool SaveReadingProgress(const StorageLayout& layout, std::int64_t gid, int current_page,
                         std::string* error = nullptr);

}  // namespace ehviewer
