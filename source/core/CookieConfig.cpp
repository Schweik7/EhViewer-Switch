#include "CookieConfig.h"

#include "FileUtil.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <sstream>
#include <utility>

namespace ehviewer {
namespace {

std::string Trim(const std::string& value) {
    std::size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin]))) ++begin;
    std::size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) --end;
    return value.substr(begin, end - begin);
}

bool IsSafeCookieValue(const std::string& value) {
    if (value.empty()) return false;
    return std::none_of(value.begin(), value.end(), [](unsigned char c) {
        return c <= 0x20 || c == 0x7f || c == ';' || c == ',';
    });
}

bool ParseBoolean(const std::string& value, bool* output) {
    std::string normalized = Trim(value);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if (normalized == "1" || normalized == "true" || normalized == "yes" ||
        normalized == "on" || normalized == "enabled") {
        *output = true;
        return true;
    }
    if (normalized == "0" || normalized == "false" || normalized == "no" ||
        normalized == "off" || normalized == "disabled") {
        *output = false;
        return true;
    }
    return false;
}

bool HasPrefix(const std::string& value, const char* prefix) {
    const std::string expected(prefix);
    return value.size() >= expected.size() &&
           std::equal(expected.begin(), expected.end(), value.begin());
}

bool IsSupportedProxy(const std::string& proxy) {
    if (proxy.empty()) return true;
    if (std::any_of(proxy.begin(), proxy.end(), [](unsigned char c) {
            return c <= 0x20 || c == 0x7f;
        })) {
        return false;
    }
    static const char* const schemes[] = {
        "http://", "https://", "socks4://", "socks4a://", "socks5://", "socks5h://"
    };
    for (const char* scheme : schemes) {
        if (HasPrefix(proxy, scheme) && proxy.size() > std::char_traits<char>::length(scheme)) {
            return true;
        }
    }
    return false;
}

void ParseAssignments(const std::string& line, std::map<std::string, std::string>* values) {
    std::size_t begin = 0;
    while (begin <= line.size()) {
        const std::size_t end = line.find(';', begin);
        std::string part = Trim(line.substr(begin, end - begin));
        const std::size_t delimiter = part.find_first_of("=:");
        if (delimiter != std::string::npos) {
            std::string key = Trim(part.substr(0, delimiter));
            std::string value = Trim(part.substr(delimiter + 1));
            std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            if (key == "cookie") ParseAssignments(value, values);
            else if (!key.empty()) (*values)[key] = value;
        }
        if (end == std::string::npos) break;
        begin = end + 1;
    }
}

}  // namespace

bool CookieConfig::Parse(const std::string& text, CookieConfig* config, std::string* error) {
    if (config == nullptr) {
        if (error) *error = "Cookie output is null";
        return false;
    }
    std::map<std::string, std::string> values;
    std::istringstream input(text);
    std::string line;
    while (std::getline(input, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        ParseAssignments(line, &values);
    }

    CookieConfig parsed;
    parsed.ipb_member_id = values["ipb_member_id"];
    parsed.ipb_pass_hash = values["ipb_pass_hash"];
    parsed.igneous = values["igneous"];
    parsed.proxy = values["proxy"];
    if (HasPrefix(parsed.proxy, "socks://")) {
        parsed.proxy.replace(0, 8, "socks5h://");
    }
    if (values.count("builtin_hosts") != 0 &&
        !ParseBoolean(values["builtin_hosts"], &parsed.builtin_hosts)) {
        if (error) *error = "builtin_hosts must be true or false";
        return false;
    }
    if (values.count("doh") != 0 && !ParseBoolean(values["doh"], &parsed.doh)) {
        if (error) *error = "doh must be true or false";
        return false;
    }
    if (values.count("domain_fronting") != 0 &&
        !ParseBoolean(values["domain_fronting"], &parsed.domain_fronting)) {
        if (error) *error = "domain_fronting must be true or false";
        return false;
    }
    if (!parsed.Validate(error)) return false;
    *config = std::move(parsed);
    return true;
}

bool CookieConfig::LoadFromFile(const std::string& path, CookieConfig* config,
                                std::string* error) {
    std::string text;
    if (!file::ReadAll(path, &text, error, 64U * 1024U)) return false;
    return Parse(text, config, error);
}

bool CookieConfig::Validate(std::string* error) const {
    if (ipb_member_id.empty() ||
        !std::all_of(ipb_member_id.begin(), ipb_member_id.end(), [](unsigned char c) {
            return std::isdigit(c) != 0;
        })) {
        if (error) *error = "ipb_member_id must contain decimal digits only";
        return false;
    }
    if (ipb_pass_hash.size() != 32 ||
        !std::all_of(ipb_pass_hash.begin(), ipb_pass_hash.end(), [](unsigned char c) {
            return std::isdigit(c) || (c >= 'a' && c <= 'z');
        })) {
        if (error) *error = "ipb_pass_hash must be 32 lowercase letters/digits";
        return false;
    }
    if (!IsSafeCookieValue(ipb_member_id) || !IsSafeCookieValue(ipb_pass_hash) ||
        (!igneous.empty() && !IsSafeCookieValue(igneous))) {
        if (error) *error = "Cookie contains unsafe characters";
        return false;
    }
    if (!IsSupportedProxy(proxy)) {
        if (error) *error = "proxy must use an HTTP or SOCKS URL";
        return false;
    }
    return true;
}

std::string CookieConfig::BuildCookieHeader() const {
    std::string header = "ipb_member_id=" + ipb_member_id +
                         "; ipb_pass_hash=" + ipb_pass_hash;
    if (!igneous.empty()) header += "; igneous=" + igneous;
    header += "; nw=1";
    return header;
}

std::string CookieConfig::RedactedSummary() const {
    return "member_id=<set>, pass_hash=<set>, igneous=" +
           std::string(igneous.empty() ? "<not set>" : "<set>") +
           ", proxy=" + (proxy.empty() ? "<not set>" : "<set>") +
           ", builtin_hosts=" + (builtin_hosts ? "on" : "off") +
           ", doh=" + (doh ? "on" : "off") +
           ", fronting=" + (domain_fronting ? "on" : "off");
}

}  // namespace ehviewer
