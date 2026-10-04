#include "FileUtil.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#else
#include <dirent.h>
#include <unistd.h>
#endif

#ifdef __SWITCH__
#include <switch.h>
#endif

namespace ehviewer::file {
namespace {

void SetError(std::string* error, const std::string& message) {
    if (error != nullptr) *error = message;
}

bool MakeDirectory(const std::string& path) {
#ifdef _WIN32
    return _mkdir(path.c_str()) == 0 || errno == EEXIST;
#else
    return mkdir(path.c_str(), 0777) == 0 || errno == EEXIST;
#endif
}

std::string ParentPath(const std::string& path) {
    const std::size_t pos = path.find_last_of("/\\");
    if (pos == std::string::npos) return {};
    if (pos == 0) return path.substr(0, 1);
    return path.substr(0, pos);
}

bool FlushFile(std::FILE* stream) {
    if (std::fflush(stream) != 0) return false;
#ifdef _WIN32
    return _commit(_fileno(stream)) == 0;
#else
    return fsync(fileno(stream)) == 0;
#endif
}

std::string JoinPath(const std::string& directory, const std::string& name) {
    if (directory.empty()) return name;
    const char last = directory.back();
    return last == '/' || last == '\\' ? directory + name : directory + "/" + name;
}

}  // namespace

bool Exists(const std::string& path) {
    struct stat info {};
    return stat(path.c_str(), &info) == 0;
}

bool IsDirectory(const std::string& path) {
    struct stat info {};
    if (stat(path.c_str(), &info) != 0) return false;
#ifdef _WIN32
    return (info.st_mode & _S_IFDIR) != 0;
#else
    return S_ISDIR(info.st_mode);
#endif
}

std::uint64_t FileSize(const std::string& path) {
    struct stat info {};
    if (stat(path.c_str(), &info) != 0 || info.st_size < 0) return 0;
    return static_cast<std::uint64_t>(info.st_size);
}

bool ListDirectory(const std::string& path, std::vector<DirectoryEntry>* entries,
                   std::string* error) {
    if (entries == nullptr) {
        SetError(error, "Directory output is null");
        return false;
    }
    entries->clear();
#ifdef _WIN32
    _finddata_t data{};
    const intptr_t handle = _findfirst(JoinPath(path, "*").c_str(), &data);
    if (handle == -1) {
        SetError(error, "Cannot open directory: " + path);
        return false;
    }
    do {
        const std::string name = data.name;
        if (name != "." && name != "..")
            entries->push_back({name, (data.attrib & _A_SUBDIR) != 0});
    } while (_findnext(handle, &data) == 0);
    _findclose(handle);
#else
    DIR* directory = opendir(path.c_str());
    if (directory == nullptr) {
        SetError(error, "Cannot open directory: " + path);
        return false;
    }
    while (const dirent* entry = readdir(directory)) {
        const std::string name = entry->d_name;
        if (name == "." || name == "..") continue;
        entries->push_back({name, IsDirectory(JoinPath(path, name))});
    }
    closedir(directory);
#endif
    std::sort(entries->begin(), entries->end(), [](const DirectoryEntry& a, const DirectoryEntry& b) {
        return a.name < b.name;
    });
    return true;
}

bool Rename(const std::string& from, const std::string& to, std::string* error) {
    if (std::rename(from.c_str(), to.c_str()) != 0) {
        SetError(error, "Cannot rename " + from + " (" + std::strerror(errno) + ")");
        return false;
    }
    return true;
}

bool RemoveTree(const std::string& path, std::string* error) {
    if (!Exists(path)) return true;
    if (!IsDirectory(path)) {
        if (std::remove(path.c_str()) == 0) return true;
        SetError(error, "Cannot remove file: " + path);
        return false;
    }
    std::vector<DirectoryEntry> entries;
    if (!ListDirectory(path, &entries, error)) return false;
    for (const DirectoryEntry& entry : entries) {
        if (!RemoveTree(JoinPath(path, entry.name), error)) return false;
    }
#ifdef _WIN32
    const bool removed = _rmdir(path.c_str()) == 0;
#else
    const bool removed = rmdir(path.c_str()) == 0;
#endif
    if (!removed) SetError(error, "Cannot remove directory: " + path);
    return removed;
}

bool CreateDirectoryRecursive(const std::string& path, std::string* error) {
    if (path.empty() || IsDirectory(path)) return true;

    std::string current;
    std::size_t index = 0;
    const std::size_t scheme = path.find(":/");
    if (scheme != std::string::npos) {
        current = path.substr(0, scheme + 2);
        index = scheme + 2;
    } else if (path.size() >= 3 && path[1] == ':' &&
               (path[2] == '/' || path[2] == '\\')) {
        current = path.substr(0, 3);
        index = 3;
    } else if (!path.empty() && (path[0] == '/' || path[0] == '\\')) {
        current = path.substr(0, 1);
        index = 1;
    }

    while (index <= path.size()) {
        const std::size_t next = path.find_first_of("/\\", index);
        const std::string component = path.substr(index, next - index);
        if (!component.empty()) {
            if (!current.empty() && current.back() != '/' && current.back() != '\\') {
                current.push_back('/');
            }
            current += component;
            if (!IsDirectory(current) && !MakeDirectory(current)) {
                SetError(error, "Cannot create directory: " + current + " (" +
                                    std::strerror(errno) + ")");
                return false;
            }
        }
        if (next == std::string::npos) break;
        index = next + 1;
    }
    return true;
}

bool ReadAll(const std::string& path, std::string* data, std::string* error,
             std::size_t max_bytes) {
    if (data == nullptr) {
        SetError(error, "Output buffer is null");
        return false;
    }
    std::FILE* stream = std::fopen(path.c_str(), "rb");
    if (stream == nullptr) {
        SetError(error, "Cannot open file: " + path);
        return false;
    }

    data->clear();
    char buffer[8192];
    bool ok = true;
    for (;;) {
        const std::size_t count = std::fread(buffer, 1, sizeof(buffer), stream);
        if (count != 0) {
            if (data->size() + count > max_bytes) {
                SetError(error, "File exceeds size limit: " + path);
                ok = false;
                break;
            }
            data->append(buffer, count);
        }
        if (count < sizeof(buffer)) {
            if (std::ferror(stream)) {
                SetError(error, "Cannot read file: " + path);
                ok = false;
            }
            break;
        }
    }
    std::fclose(stream);
    return ok;
}

bool WriteAllAtomic(const std::string& path, const std::string& data,
                    std::string* error) {
    const std::string parent = ParentPath(path);
    if (!parent.empty() && !CreateDirectoryRecursive(parent, error)) return false;

    const std::string temporary = path + ".new";
    std::FILE* stream = std::fopen(temporary.c_str(), "wb");
    if (stream == nullptr) {
        SetError(error, "Cannot create temporary file: " + temporary);
        return false;
    }
    bool ok = data.empty() || std::fwrite(data.data(), 1, data.size(), stream) == data.size();
    if (ok) ok = FlushFile(stream);
    if (std::fclose(stream) != 0) ok = false;
    if (!ok) {
        std::remove(temporary.c_str());
        SetError(error, "Cannot write file: " + path);
        return false;
    }

    std::remove(path.c_str());
    if (std::rename(temporary.c_str(), path.c_str()) != 0) {
        std::remove(temporary.c_str());
        SetError(error, "Cannot replace file: " + path);
        return false;
    }
    return CommitDevice(error);
}

bool Remove(const std::string& path) {
    return std::remove(path.c_str()) == 0 || errno == ENOENT;
}

bool CommitDevice(std::string* error) {
#ifdef __SWITCH__
    const Result result = fsdevCommitDevice("sdmc");
    if (R_FAILED(result)) {
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "fsdevCommitDevice failed: 0x%08x", result);
        SetError(error, buffer);
        return false;
    }
#else
    (void)error;
#endif
    return true;
}

}  // namespace ehviewer::file
