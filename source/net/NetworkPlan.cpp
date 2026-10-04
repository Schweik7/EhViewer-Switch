#include "NetworkPlan.h"

#include <algorithm>
#include <cctype>

namespace ehviewer {
namespace {

// At most this many fronting addresses are tried per request so a blocked
// network fails within a bounded time.
constexpr std::size_t kMaxFrontingAttempts = 2;

struct FrontingEntry {
    const char* host;
    const char* addresses[4];
};

// Verified from a mainland China network on 2026-10-04: these servers answer
// without SNI, while TLS with the real SNI is reset. Keep in sync with the
// IPv4 entries of the Android EhHosts table.
constexpr FrontingEntry kFrontingHosts[] = {
    {"exhentai.org", {"178.175.128.252", "178.175.129.252", "178.175.132.20", "178.175.128.254"}},
    {"s.exhentai.org", {"178.175.129.253", "178.175.128.253", "178.175.132.21", "178.175.129.254"}},
    {"upld.exhentai.org", {"178.175.132.22", "178.175.129.254", "178.175.128.254", nullptr}},
    {"ehgt.org", {"109.236.85.28", "62.112.8.21", "89.39.106.43", nullptr}},
};

}  // namespace

const char* RouteLabel(RouteKind kind) {
    switch (kind) {
        case RouteKind::Proxy: return "代理";
        case RouteKind::Fronted: return "域前置直连";
        case RouteKind::BuiltinHosts: return "内置 hosts";
        case RouteKind::Doh: return "DoH";
        default: return "系统 DNS";
    }
}

std::vector<NetworkAttempt> BuildAttemptPlan(const HttpRequestOptions& options,
                                             const std::string& url) {
    std::vector<NetworkAttempt> plan;
    HttpRequestOptions direct = options;
    direct.proxy.clear();
    direct.builtin_hosts = false;
    direct.doh = false;
    direct.front_address.clear();

    if (!options.proxy.empty()) {
        HttpRequestOptions proxied = direct;
        proxied.proxy = options.proxy;
        plan.push_back({RouteKind::Proxy, proxied});
    }
    if (options.domain_fronting && url.compare(0, 8, "https://") == 0) {
        const std::vector<std::string> addresses = FrontingAddresses(UrlHost(url));
        for (std::size_t i = 0; i < addresses.size() && i < kMaxFrontingAttempts; ++i) {
            HttpRequestOptions fronted = direct;
            fronted.front_address = addresses[i];
            // Two addresses must fail within a bounded time.
            fronted.connect_timeout_seconds = std::min<long>(fronted.connect_timeout_seconds, 6);
            plan.push_back({RouteKind::Fronted, fronted});
        }
    }
    if (options.builtin_hosts) {
        HttpRequestOptions hosts = direct;
        hosts.builtin_hosts = true;
        plan.push_back({RouteKind::BuiltinHosts, hosts});
    }
    if (options.doh) {
        HttpRequestOptions doh = direct;
        doh.doh = true;
        plan.push_back({RouteKind::Doh, doh});
    }
    plan.push_back({RouteKind::SystemDns, direct});
    return plan;
}

void PreferRoute(std::vector<NetworkAttempt>* plan, RouteKind preferred) {
    if (plan == nullptr) return;
    // Move every attempt of the preferred kind (e.g. both fronting addresses)
    // to the front, keeping their relative order.
    std::stable_partition(plan->begin(), plan->end(), [preferred](const NetworkAttempt& a) {
        return a.kind == preferred;
    });
}

std::string UrlHost(const std::string& url) {
    const std::size_t scheme = url.find("://");
    if (scheme == std::string::npos) return {};
    const std::size_t begin = scheme + 3;
    const std::size_t end = url.find_first_of("/?#", begin);
    std::string host = url.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
    if (host.empty() || host.find('@') != std::string::npos) return {};
    const std::size_t port = host.find(':');
    if (port != std::string::npos) host.resize(port);
    std::transform(host.begin(), host.end(), host.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return host;
}

bool IsSiteHost(const std::string& url) {
    if (url.compare(0, 8, "https://") != 0) return false;
    const std::string host = UrlHost(url);
    if (host.empty()) return false;
    for (const char* domain : {"e-hentai.org", "exhentai.org", "ehgt.org"}) {
        const std::string suffix = std::string(".") + domain;
        if (host == domain ||
            (host.size() > suffix.size() &&
             host.compare(host.size() - suffix.size(), suffix.size(), suffix) == 0)) {
            return true;
        }
    }
    return false;
}

std::vector<std::string> FrontingAddresses(const std::string& host) {
    std::vector<std::string> result;
    for (const FrontingEntry& entry : kFrontingHosts) {
        if (host != entry.host) continue;
        for (const char* address : entry.addresses) {
            if (address != nullptr) result.emplace_back(address);
        }
    }
    return result;
}

std::string FrontedUrl(const std::string& url, const std::string& address) {
    const std::size_t scheme = url.find("://");
    if (scheme == std::string::npos || address.empty()) return {};
    const std::size_t begin = scheme + 3;
    const std::size_t end = url.find_first_of("/?#", begin);
    std::string result = url.substr(0, begin) + address;
    result += end == std::string::npos ? "/" : url.substr(end);
    return result;
}

}  // namespace ehviewer
