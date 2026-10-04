#include "HtmlUtil.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace ehviewer::html {
namespace {

void AppendUtf8(unsigned long codepoint, std::string* output) {
    if (codepoint <= 0x7f) {
        output->push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7ff) {
        output->push_back(static_cast<char>(0xc0 | (codepoint >> 6)));
        output->push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    } else if (codepoint <= 0xffff) {
        output->push_back(static_cast<char>(0xe0 | (codepoint >> 12)));
        output->push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
        output->push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    } else if (codepoint <= 0x10ffff) {
        output->push_back(static_cast<char>(0xf0 | (codepoint >> 18)));
        output->push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f)));
        output->push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
        output->push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    }
}

bool AttributeContainsId(const std::string& tag, const std::string& id) {
    const std::string double_quoted = "id=\"" + id + "\"";
    const std::string single_quoted = "id='" + id + "'";
    return tag.find(double_quoted) != std::string::npos ||
           tag.find(single_quoted) != std::string::npos;
}

}  // namespace

std::string DecodeEntities(const std::string& value) {
    std::string output;
    output.reserve(value.size());
    for (std::size_t i = 0; i < value.size();) {
        if (value[i] != '&') {
            output.push_back(value[i++]);
            continue;
        }
        const std::size_t semicolon = value.find(';', i + 1);
        if (semicolon == std::string::npos || semicolon - i > 12) {
            output.push_back(value[i++]);
            continue;
        }
        const std::string entity = value.substr(i + 1, semicolon - i - 1);
        if (entity == "amp") output.push_back('&');
        else if (entity == "lt") output.push_back('<');
        else if (entity == "gt") output.push_back('>');
        else if (entity == "quot") output.push_back('"');
        else if (entity == "apos" || entity == "#039" || entity == "#39") output.push_back('\'');
        else if (entity == "nbsp") output.push_back(' ');
        else if (entity.size() >= 2 && entity[0] == '#') {
            const bool hexadecimal = entity[1] == 'x' || entity[1] == 'X';
            const char* begin = entity.c_str() + (hexadecimal ? 2 : 1);
            char* end = nullptr;
            const unsigned long codepoint = std::strtoul(begin, &end, hexadecimal ? 16 : 10);
            if (end != begin && *end == '\0' && codepoint <= 0x10ffff) AppendUtf8(codepoint, &output);
            else output.append(value, i, semicolon - i + 1);
        } else {
            output.append(value, i, semicolon - i + 1);
        }
        i = semicolon + 1;
    }
    return output;
}

std::string PlainText(const std::string& source) {
    std::string output;
    output.reserve(source.size());
    bool in_tag = false;
    bool pending_space = false;
    for (unsigned char c : source) {
        if (c == '<') {
            in_tag = true;
            pending_space = !output.empty();
        } else if (c == '>') {
            in_tag = false;
        } else if (!in_tag) {
            if (std::isspace(c)) {
                pending_space = !output.empty();
            } else {
                if (pending_space && output.back() != ' ') output.push_back(' ');
                output.push_back(static_cast<char>(c));
                pending_space = false;
            }
        }
    }
    while (!output.empty() && output.back() == ' ') output.pop_back();
    return DecodeEntities(output);
}

std::string ElementTextById(const std::string& document, const std::string& id) {
    std::size_t cursor = 0;
    while ((cursor = document.find("id=", cursor)) != std::string::npos) {
        const std::size_t tag_begin = document.rfind('<', cursor);
        const std::size_t tag_end = document.find('>', cursor);
        if (tag_begin == std::string::npos || tag_end == std::string::npos) return {};
        if (AttributeContainsId(document.substr(tag_begin, tag_end - tag_begin + 1), id)) {
            const std::size_t close = document.find("</", tag_end + 1);
            if (close == std::string::npos) return {};
            return PlainText(document.substr(tag_end + 1, close - tag_end - 1));
        }
        cursor = tag_end + 1;
    }
    return {};
}

std::string UrlEncode(const std::string& value) {
    static const char* const hex = "0123456789ABCDEF";
    std::string output;
    for (unsigned char c : value) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            output.push_back(static_cast<char>(c));
        } else if (c == ' ') {
            output.push_back('+');
        } else {
            output.push_back('%');
            output.push_back(hex[c >> 4]);
            output.push_back(hex[c & 0x0f]);
        }
    }
    return output;
}

}  // namespace ehviewer::html
