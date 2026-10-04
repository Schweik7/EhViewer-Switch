#pragma once

#include "GalleryParser.h"

#include <cstddef>
#include <string>
#include <vector>

namespace ehviewer {

// Recently opened galleries, newest first, stored as JSON.
class History {
public:
    static constexpr std::size_t kLimit = 200;

    // Moves gid to the front (or inserts it) and trims to kLimit.
    void Add(const GallerySummary& gallery);
    void Remove(std::int64_t gid);
    void Clear() { entries_.clear(); }
    const std::vector<GallerySummary>& Entries() const { return entries_; }

    std::string Serialize() const;
    static bool Parse(const std::string& json, History* history, std::string* error = nullptr);
    static bool Load(const std::string& path, History* history, std::string* error = nullptr);
    bool Save(const std::string& path, std::string* error = nullptr) const;

private:
    std::vector<GallerySummary> entries_;
};

}  // namespace ehviewer
