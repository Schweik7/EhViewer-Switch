#include "GalleryManifest.h"

#include "Json.h"
#include "StorageLayout.h"

#include <iomanip>
#include <set>
#include <sstream>

namespace ehviewer {
namespace {

bool Fail(std::string* error, const std::string& message) {
    if (error) *error = message;
    return false;
}

bool SafeLogicalPath(const std::string& path) {
    if (path.empty() || path.front() == '/' || path.front() == '\\') return false;
    if (path.size() >= 2 && path[1] == ':') return false;
    std::size_t begin = 0;
    while (begin <= path.size()) {
        const std::size_t end = path.find_first_of("/\\", begin);
        const std::string component = path.substr(begin, end - begin);
        if (component.empty() || component == "." || component == "..") return false;
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    return true;
}

std::string Json(const std::string& value) {
    std::ostringstream output;
    output << '"';
    for (unsigned char c : value) {
        switch (c) {
            case '"': output << "\\\""; break;
            case '\\': output << "\\\\"; break;
            case '\b': output << "\\b"; break;
            case '\f': output << "\\f"; break;
            case '\n': output << "\\n"; break;
            case '\r': output << "\\r"; break;
            case '\t': output << "\\t"; break;
            default:
                if (c < 0x20) {
                    output << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                           << static_cast<int>(c) << std::dec;
                } else {
                    output << static_cast<char>(c);
                }
        }
    }
    output << '"';
    return output.str();
}

}  // namespace

bool GalleryManifest::Validate(std::string* error) const {
    if (format_version != 1) return Fail(error, "Unsupported manifest version");
    if (gid <= 0) return Fail(error, "Gallery id must be positive");
    if (token.empty()) return Fail(error, "Gallery token is empty");
    if (total_pages < 0 || current_page < 0 ||
        (total_pages > 0 && current_page >= total_pages)) {
        return Fail(error, "Manifest page progress is invalid");
    }
    std::set<std::string> physical_names;
    for (const ManifestFile& file : files) {
        if (!StorageLayout::IsSafePhysicalComponent(file.physical_name))
            return Fail(error, "Unsafe physical file name");
        if (!SafeLogicalPath(file.original_name)) return Fail(error, "Unsafe original file path");
        if (!physical_names.insert(file.physical_name).second)
            return Fail(error, "Duplicate physical file name");
        if (file.page_index < -1 || (total_pages > 0 && file.page_index >= total_pages))
            return Fail(error, "File page index is invalid");
    }
    return true;
}

std::string GalleryManifest::SerializeJson() const {
    std::ostringstream output;
    output << "{\n"
           << "  \"formatVersion\": " << format_version << ",\n"
           << "  \"gid\": " << gid << ",\n"
           << "  \"token\": " << Json(token) << ",\n"
           << "  \"title\": " << Json(title) << ",\n"
           << "  \"titleJpn\": " << Json(title_jpn) << ",\n"
           << "  \"originalDirectoryName\": " << Json(original_directory_name) << ",\n"
           << "  \"category\": " << Json(category) << ",\n"
           << "  \"currentPage\": " << current_page << ",\n"
           << "  \"totalPages\": " << total_pages << ",\n"
           << "  \"files\": [";
    for (std::size_t i = 0; i < files.size(); ++i) {
        const ManifestFile& file = files[i];
        output << (i == 0 ? "\n" : ",\n")
               << "    {\"physicalName\": " << Json(file.physical_name)
               << ", \"originalName\": " << Json(file.original_name)
               << ", \"role\": " << Json(file.role)
               << ", \"pageIndex\": " << file.page_index
               << ", \"size\": " << file.size
               << ", \"sha256\": " << Json(file.sha256) << '}';
    }
    if (!files.empty()) output << '\n';
    output << "  ]\n}\n";
    return output.str();
}

bool GalleryManifest::Parse(const std::string& text, GalleryManifest* manifest,
                            std::string* error) {
    if (manifest == nullptr) return Fail(error, "Manifest output is null");
    json::Value root;
    if (!json::Parse(text, &root, error)) return false;
    if (root.type != json::Value::Type::Object) return Fail(error, "Manifest is not an object");

    GalleryManifest parsed;
    parsed.format_version = static_cast<int>(root.GetInt("formatVersion", 0));
    parsed.gid = root.GetInt("gid");
    parsed.token = root.GetString("token");
    parsed.title = root.GetString("title");
    parsed.title_jpn = root.GetString("titleJpn");
    parsed.original_directory_name = root.GetString("originalDirectoryName");
    parsed.category = root.GetString("category");
    parsed.current_page = static_cast<int>(root.GetInt("currentPage"));
    parsed.total_pages = static_cast<int>(root.GetInt("totalPages"));
    const json::Value* files = root.Find("files");
    if (files != nullptr && files->type == json::Value::Type::Array) {
        for (const json::Value& item : files->array) {
            ManifestFile file;
            file.physical_name = item.GetString("physicalName");
            file.original_name = item.GetString("originalName");
            file.role = item.GetString("role");
            file.page_index = static_cast<int>(item.GetInt("pageIndex", -1));
            const std::int64_t size = item.GetInt("size");
            file.size = size > 0 ? static_cast<std::uint64_t>(size) : 0;
            file.sha256 = item.GetString("sha256");
            parsed.files.push_back(std::move(file));
        }
    }
    if (!parsed.Validate(error)) return false;
    *manifest = std::move(parsed);
    return true;
}

}  // namespace ehviewer
