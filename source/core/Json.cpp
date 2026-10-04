#include "Json.h"

#include <cctype>
#include <cmath>
#include <cstdlib>

namespace ehviewer::json {
namespace {

constexpr int kMaxDepth = 32;

class Parser {
public:
    explicit Parser(const std::string& text) : text_(text) {}

    bool ParseDocument(Value* value, std::string* error) {
        SkipSpace();
        if (!ParseValue(value, 0)) return Fail(error);
        SkipSpace();
        if (position_ != text_.size()) {
            message_ = "Unexpected trailing data";
            return Fail(error);
        }
        return true;
    }

private:
    bool Fail(std::string* error) const {
        if (error) *error = message_ + " at offset " + std::to_string(position_);
        return false;
    }

    void SkipSpace() {
        while (position_ < text_.size() &&
               std::isspace(static_cast<unsigned char>(text_[position_]))) ++position_;
    }

    bool Consume(char expected) {
        SkipSpace();
        if (position_ < text_.size() && text_[position_] == expected) {
            ++position_;
            return true;
        }
        message_ = std::string("Expected '") + expected + "'";
        return false;
    }

    bool Literal(const char* word) {
        std::size_t length = 0;
        while (word[length] != '\0') ++length;
        if (text_.compare(position_, length, word) != 0) {
            message_ = "Invalid literal";
            return false;
        }
        position_ += length;
        return true;
    }

    bool ParseValue(Value* value, int depth) {
        if (depth > kMaxDepth) {
            message_ = "JSON nesting is too deep";
            return false;
        }
        SkipSpace();
        if (position_ >= text_.size()) {
            message_ = "Unexpected end of JSON";
            return false;
        }
        const char c = text_[position_];
        if (c == '{') return ParseObject(value, depth);
        if (c == '[') return ParseArray(value, depth);
        if (c == '"') {
            value->type = Value::Type::String;
            return ParseString(&value->string);
        }
        if (c == 't') { value->type = Value::Type::Bool; value->boolean = true; return Literal("true"); }
        if (c == 'f') { value->type = Value::Type::Bool; value->boolean = false; return Literal("false"); }
        if (c == 'n') { value->type = Value::Type::Null; return Literal("null"); }
        return ParseNumber(value);
    }

    bool ParseObject(Value* value, int depth) {
        value->type = Value::Type::Object;
        ++position_;
        SkipSpace();
        if (position_ < text_.size() && text_[position_] == '}') {
            ++position_;
            return true;
        }
        for (;;) {
            SkipSpace();
            std::string key;
            if (position_ >= text_.size() || text_[position_] != '"') {
                message_ = "Expected object key";
                return false;
            }
            if (!ParseString(&key) || !Consume(':')) return false;
            Value child;
            if (!ParseValue(&child, depth + 1)) return false;
            value->object[key] = std::move(child);
            SkipSpace();
            if (position_ < text_.size() && text_[position_] == ',') { ++position_; continue; }
            return Consume('}');
        }
    }

    bool ParseArray(Value* value, int depth) {
        value->type = Value::Type::Array;
        ++position_;
        SkipSpace();
        if (position_ < text_.size() && text_[position_] == ']') {
            ++position_;
            return true;
        }
        for (;;) {
            Value child;
            if (!ParseValue(&child, depth + 1)) return false;
            value->array.push_back(std::move(child));
            SkipSpace();
            if (position_ < text_.size() && text_[position_] == ',') { ++position_; continue; }
            return Consume(']');
        }
    }

    static void AppendUtf8(unsigned long codepoint, std::string* output) {
        if (codepoint <= 0x7f) {
            output->push_back(static_cast<char>(codepoint));
        } else if (codepoint <= 0x7ff) {
            output->push_back(static_cast<char>(0xc0 | (codepoint >> 6)));
            output->push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
        } else if (codepoint <= 0xffff) {
            output->push_back(static_cast<char>(0xe0 | (codepoint >> 12)));
            output->push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
            output->push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
        } else {
            output->push_back(static_cast<char>(0xf0 | (codepoint >> 18)));
            output->push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f)));
            output->push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
            output->push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
        }
    }

    bool ParseHex4(unsigned long* result) {
        if (position_ + 4 > text_.size()) return false;
        unsigned long value = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = text_[position_++];
            value <<= 4;
            if (c >= '0' && c <= '9') value |= static_cast<unsigned long>(c - '0');
            else if (c >= 'a' && c <= 'f') value |= static_cast<unsigned long>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') value |= static_cast<unsigned long>(c - 'A' + 10);
            else return false;
        }
        *result = value;
        return true;
    }

    bool ParseString(std::string* output) {
        ++position_;
        output->clear();
        while (position_ < text_.size()) {
            const char c = text_[position_++];
            if (c == '"') return true;
            if (static_cast<unsigned char>(c) < 0x20) break;
            if (c != '\\') {
                output->push_back(c);
                continue;
            }
            if (position_ >= text_.size()) break;
            const char escape = text_[position_++];
            switch (escape) {
                case '"': output->push_back('"'); break;
                case '\\': output->push_back('\\'); break;
                case '/': output->push_back('/'); break;
                case 'b': output->push_back('\b'); break;
                case 'f': output->push_back('\f'); break;
                case 'n': output->push_back('\n'); break;
                case 'r': output->push_back('\r'); break;
                case 't': output->push_back('\t'); break;
                case 'u': {
                    unsigned long codepoint = 0;
                    if (!ParseHex4(&codepoint)) {
                        message_ = "Invalid unicode escape";
                        return false;
                    }
                    if (codepoint >= 0xd800 && codepoint <= 0xdbff &&
                        text_.compare(position_, 2, "\\u") == 0) {
                        position_ += 2;
                        unsigned long low = 0;
                        if (!ParseHex4(&low) || low < 0xdc00 || low > 0xdfff) {
                            message_ = "Invalid surrogate pair";
                            return false;
                        }
                        codepoint = 0x10000 + ((codepoint - 0xd800) << 10) + (low - 0xdc00);
                    }
                    AppendUtf8(codepoint, output);
                    break;
                }
                default:
                    message_ = "Invalid string escape";
                    return false;
            }
        }
        message_ = "Unterminated string";
        return false;
    }

    bool ParseNumber(Value* value) {
        const char* begin = text_.c_str() + position_;
        char* end = nullptr;
        const double number = std::strtod(begin, &end);
        if (end == begin || !std::isfinite(number)) {
            message_ = "Invalid JSON value";
            return false;
        }
        position_ += static_cast<std::size_t>(end - begin);
        value->type = Value::Type::Number;
        value->number = number;
        return true;
    }

    const std::string& text_;
    std::size_t position_ = 0;
    std::string message_ = "Invalid JSON";
};

}  // namespace

const Value* Value::Find(const std::string& key) const {
    if (type != Type::Object) return nullptr;
    const auto found = object.find(key);
    return found == object.end() ? nullptr : &found->second;
}

std::string Value::GetString(const std::string& key, const std::string& fallback) const {
    const Value* value = Find(key);
    return value != nullptr && value->type == Type::String ? value->string : fallback;
}

std::int64_t Value::GetInt(const std::string& key, std::int64_t fallback) const {
    const Value* value = Find(key);
    if (value == nullptr) return fallback;
    if (value->type == Type::Number) return static_cast<std::int64_t>(value->number);
    if (value->type == Type::String && !value->string.empty()) {
        char* end = nullptr;
        const long long parsed = std::strtoll(value->string.c_str(), &end, 10);
        if (end != nullptr && *end == '\0') return parsed;
    }
    return fallback;
}

bool Parse(const std::string& text, Value* value, std::string* error) {
    if (value == nullptr) {
        if (error) *error = "JSON output is null";
        return false;
    }
    Value parsed;
    Parser parser(text);
    if (!parser.ParseDocument(&parsed, error)) return false;
    *value = std::move(parsed);
    return true;
}

}  // namespace ehviewer::json
