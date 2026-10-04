#pragma once

#include "HttpClient.h"

#include <string>
#include <vector>

namespace ehviewer {

enum class RouteKind { Proxy, Fronted, BuiltinHosts, Doh, SystemDns };

struct NetworkAttempt {
    RouteKind kind = RouteKind::SystemDns;
    HttpRequestOptions options;
};

const char* RouteLabel(RouteKind kind);

// Expands the configured network options into explicit, single-mechanism
// attempts for url: proxy -> domain fronting -> built-in hosts direct -> DoH
// -> system DNS. Each enabled mechanism is tried on its own so one broken
// route cannot hide a working one. Domain fronting is only planned for hosts
// that have fronting addresses (see FrontingAddresses).
std::vector<NetworkAttempt> BuildAttemptPlan(const HttpRequestOptions& options,
                                             const std::string& url);

// Moves the route that last succeeded to the front, keeping the rest in order.
void PreferRoute(std::vector<NetworkAttempt>* plan, RouteKind preferred);

// True for https URLs on e-hentai.org, exhentai.org, ehgt.org or their
// subdomains. Login cookies must never be sent to other hosts such as the
// third-party H@H image servers.
bool IsSiteHost(const std::string& url);

// Lower-case host of an http(s) URL without port, or "" if malformed.
std::string UrlHost(const std::string& url);

// Server addresses that accept TLS without SNI for host (the Android
// "domain fronting" mode). e-hentai.org is behind Cloudflare, which requires
// SNI, so it has none.
std::vector<std::string> FrontingAddresses(const std::string& host);

// "https://exhentai.org/g/1/" + "1.2.3.4" -> "https://1.2.3.4/g/1/".
std::string FrontedUrl(const std::string& url, const std::string& address);

}  // namespace ehviewer
