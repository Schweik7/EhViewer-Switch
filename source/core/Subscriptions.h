#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace ehviewer {

// Saved searches the user can reopen ("my subscriptions"), stored as JSON.
struct Subscription {
    std::string name;   // label shown in the list
    std::string query;  // f_search value, e.g. parody:"blue archive$"
};

class Subscriptions {
public:
    static constexpr std::size_t kLimit = 100;

    // Returns false when the query is empty, already saved or the list is full.
    bool Add(const std::string& query, const std::string& name = std::string());
    bool Remove(std::size_t index);
    bool Contains(const std::string& query) const;
    const std::vector<Subscription>& Entries() const { return entries_; }

    std::string Serialize() const;
    static bool Parse(const std::string& json, Subscriptions* subscriptions, std::string* error = nullptr);
    static bool Load(const std::string& path, Subscriptions* subscriptions, std::string* error = nullptr);
    bool Save(const std::string& path, std::string* error = nullptr) const;

private:
    std::vector<Subscription> entries_;
};

}  // namespace ehviewer
