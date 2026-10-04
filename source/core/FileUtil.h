#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ehviewer::file {

struct DirectoryEntry {
    std::string name;
    bool directory = false;
};

bool Exists(const std::string& path);
bool IsDirectory(const std::string& path);
std::uint64_t FileSize(const std::string& path);
// Lists entries except "." and "..", sorted by name.
bool ListDirectory(const std::string& path, std::vector<DirectoryEntry>* entries,
                   std::string* error = nullptr);
bool Rename(const std::string& from, const std::string& to, std::string* error = nullptr);
bool RemoveTree(const std::string& path, std::string* error = nullptr);
bool CreateDirectoryRecursive(const std::string& path, std::string* error = nullptr);
bool ReadAll(const std::string& path, std::string* data, std::string* error = nullptr,
             std::size_t max_bytes = 16U * 1024U * 1024U);
bool WriteAllAtomic(const std::string& path, const std::string& data,
                    std::string* error = nullptr);
bool Remove(const std::string& path);
bool CommitDevice(std::string* error = nullptr);

}  // namespace ehviewer::file
