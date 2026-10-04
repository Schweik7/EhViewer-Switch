#include "History.h"

#include "FileUtil.h"
#include "Json.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <sstream>

namespace ehviewer {
namespace {

std::string Quote(const std::string& value) {
    std::string output = "\"";
    for (unsigned char c : value) {
        switch (c) {
            case '"': output += "\\\""; break;
            case '\\': output += "\\\\"; break;
            case '\n': output += "\\n"; break;
            case '\r': output += "\\r"; break;
            case '\t': output += "\\t"; break;
            default:
                if (c < 0x20) {
                    char escaped[8];
                    std::snprintf(escaped, sizeof(escaped), "\\u%04x", c);
                    output += escaped;
                } else {
                    output.push_back(static_cast<char>(c));
                }
        }
    }
    return output + "\"";
}

bool SafeToken(const std::string& token) {
    return !token.empty() && token.size() <= 64 &&
           std::all_of(token.begin(), token.end(), [](unsigned char c) { return std::isalnum(c) != 0; });
}

}  // namespace

void History::Add(const GallerySummary& gallery) {
    if (gallery.gid <= 0 || !SafeToken(gallery.token)) return;
    Remove(gallery.gid);
    entries_.insert(entries_.begin(), gallery);
    if (entries_.size() > kLimit) entries_.resize(kLimit);
}

void History::Remove(std::int64_t gid) {
    entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
                                  [gid](const GallerySummary& entry) { return entry.gid == gid; }),
                   entries_.end());
}

std::string History::Serialize() const {
    std::ostringstream output;
    output << "{\"version\":1,\"galleries\":[";
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        const GallerySummary& entry = entries_[i];
        output << (i == 0 ? "\n" : ",\n") << "{\"gid\":" << entry.gid << ",\"token\":" << Quote(entry.token)
               << ",\"title\":" << Quote(entry.title) << ",\"thumb\":" << Quote(entry.thumb_url)
               << ",\"category\":" << Quote(entry.category) << ",\"posted\":" << Quote(entry.posted)
               << ",\"pages\":" << entry.pages << ",\"rating10\":" << static_cast<int>(entry.rating * 2.0) << '}';
    }
    output << "\n]}\n";
    return output.str();
}

bool History::Parse(const std::string& text, History* history, std::string* error) {
    json::Value root;
    if (!json::Parse(text, &root, error)) return false;
    const json::Value* galleries = root.Find("galleries");
    if (galleries == nullptr || galleries->type != json::Value::Type::Array) {
        if (error) *error = "History has no galleries array";
        return false;
    }
    History parsed;
    for (const json::Value& item : galleries->array) {
        GallerySummary entry;
        entry.gid = item.GetInt("gid");
        entry.token = item.GetString("token");
        if (entry.gid <= 0 || !SafeToken(entry.token)) continue;
        entry.title = item.GetString("title");
        entry.thumb_url = item.GetString("thumb");
        entry.category = item.GetString("category");
        entry.posted = item.GetString("posted");
        entry.pages = static_cast<int>(item.GetInt("pages"));
        // Stored as half stars; -2 (= -1.0) means unknown.
        entry.rating = static_cast<double>(item.GetInt("rating10", -2)) / 2.0;
        entry.path = "/g/" + std::to_string(entry.gid) + "/" + entry.token + "/";
        if (parsed.entries_.size() < kLimit) parsed.entries_.push_back(std::move(entry));
    }
    *history = std::move(parsed);
    return true;
}

bool History::Load(const std::string& path, History* history, std::string* error) {
    if (!file::Exists(path)) {
        *history = History();
        return true;
    }
    std::string text;
    if (!file::ReadAll(path, &text, error, 4U * 1024U * 1024U)) return false;
    return Parse(text, history, error);
}

bool History::Save(const std::string& path, std::string* error) const {
    return file::WriteAllAtomic(path, Serialize(), error);
}

}  // namespace ehviewer
