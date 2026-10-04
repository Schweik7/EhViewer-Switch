#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace ehviewer::json {

// Small JSON DOM used for reading files this app wrote (manifest.json) and
// short site API responses. Numbers are kept as double.
struct Value {
    enum class Type { Null, Bool, Number, String, Array, Object };

    Type type = Type::Null;
    bool boolean = false;
    double number = 0.0;
    std::string string;
    std::vector<Value> array;
    std::map<std::string, Value> object;

    const Value* Find(const std::string& key) const;
    std::string GetString(const std::string& key, const std::string& fallback = {}) const;
    std::int64_t GetInt(const std::string& key, std::int64_t fallback = 0) const;
};

bool Parse(const std::string& text, Value* value, std::string* error = nullptr);

}  // namespace ehviewer::json
