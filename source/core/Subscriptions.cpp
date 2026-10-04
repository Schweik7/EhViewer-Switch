#include "Subscriptions.h"

#include "FileUtil.h"
#include "Json.h"

#include <algorithm>
#include <cstdio>
#include <sstream>

namespace ehviewer {
namespace {

std::string Trim(const std::string& value) {
    const std::size_t begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return {};
    const std::size_t end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

std::string Quote(const std::string& value) {
    std::string output = "\"";
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') {
            output.push_back('\\');
            output.push_back(static_cast<char>(c));
        } else if (c < 0x20) {
            char escaped[8];
            std::snprintf(escaped, sizeof(escaped), "\\u%04x", c);
            output += escaped;
        } else {
            output.push_back(static_cast<char>(c));
        }
    }
    return output + "\"";
}

}  // namespace

bool Subscriptions::Add(const std::string& raw_query, const std::string& raw_name) {
    const std::string query = Trim(raw_query);
    if (query.empty() || query.size() > 1024 || Contains(query) || entries_.size() >= kLimit) return false;
    const std::string name = Trim(raw_name);
    entries_.push_back({name.empty() ? query : name, query});
    return true;
}

bool Subscriptions::Remove(std::size_t index) {
    if (index >= entries_.size()) return false;
    entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(index));
    return true;
}

bool Subscriptions::Contains(const std::string& query) const {
    const std::string trimmed = Trim(query);
    return std::any_of(entries_.begin(), entries_.end(),
                       [&trimmed](const Subscription& entry) { return entry.query == trimmed; });
}

std::string Subscriptions::Serialize() const {
    std::ostringstream output;
    output << "{\"version\":1,\"subscriptions\":[";
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        output << (i == 0 ? "\n" : ",\n") << "{\"name\":" << Quote(entries_[i].name)
               << ",\"query\":" << Quote(entries_[i].query) << '}';
    }
    output << "\n]}\n";
    return output.str();
}

bool Subscriptions::Parse(const std::string& text, Subscriptions* subscriptions, std::string* error) {
    json::Value root;
    if (!json::Parse(text, &root, error)) return false;
    const json::Value* list = root.Find("subscriptions");
    if (list == nullptr || list->type != json::Value::Type::Array) {
        if (error) *error = "No subscriptions array";
        return false;
    }
    Subscriptions parsed;
    for (const json::Value& item : list->array) parsed.Add(item.GetString("query"), item.GetString("name"));
    *subscriptions = std::move(parsed);
    return true;
}

bool Subscriptions::Load(const std::string& path, Subscriptions* subscriptions, std::string* error) {
    if (!file::Exists(path)) {
        *subscriptions = Subscriptions();
        return true;
    }
    std::string text;
    if (!file::ReadAll(path, &text, error, 1U * 1024U * 1024U)) return false;
    return Parse(text, subscriptions, error);
}

bool Subscriptions::Save(const std::string& path, std::string* error) const {
    return file::WriteAllAtomic(path, Serialize(), error);
}

}  // namespace ehviewer
