#pragma once

#include <string>

namespace ehviewer {

struct CookieConfig {
    std::string ipb_member_id;
    std::string ipb_pass_hash;
    std::string igneous;
    // Network settings live beside the cookies for the first Switch release.
    // The proxy value is never included in diagnostic summaries.
    std::string proxy;
    bool builtin_hosts = true;
    bool doh = true;
    // SNI-less direct connection to exhentai/ehgt (Android "domain fronting").
    bool domain_fronting = true;

    static bool Parse(const std::string& text, CookieConfig* config,
                      std::string* error = nullptr);
    static bool LoadFromFile(const std::string& path, CookieConfig* config,
                             std::string* error = nullptr);

    bool Validate(std::string* error = nullptr) const;
    std::string BuildCookieHeader() const;
    std::string RedactedSummary() const;
};

}  // namespace ehviewer
