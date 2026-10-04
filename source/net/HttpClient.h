#pragma once

#include <cstddef>
#include <cstdint>
#include <atomic>
#include <functional>
#include <string>

namespace ehviewer {

struct HttpRequestOptions {
    // Empty means a direct connection. Supported schemes are http://, https://,
    // socks4://, socks4a://, socks5:// and socks5h://.
    std::string proxy;
    bool builtin_hosts = true;
    bool doh = true;
    bool ipv4_only = true;
    long connect_timeout_seconds = 10;
    long total_timeout_seconds = 20;
    // Optional Referer header. Image servers expect the gallery page here.
    std::string referer;
    // Allow the SNI-less "domain fronting" route (see NetworkPlan.h).
    bool domain_fronting = true;
    // Set by the attempt plan only: connect to this IPv4 address with no SNI
    // and send the real host name in the Host header. The certificate chain
    // is still verified against cacert.pem; only the host name check is off.
    std::string front_address;
    // Non-empty turns the request into a POST with this body and type
    // (e.g. "application/x-www-form-urlencoded"). An Origin header for the
    // request's own site is added automatically.
    std::string post_body;
    std::string post_content_type;
    // Optional: received body bytes are added here while the transfer runs
    // (used for the download speed display).
    std::atomic<std::uint64_t>* download_counter = nullptr;

    bool Validate(std::string* error = nullptr) const;
};

struct HttpResponse {
    long status_code = 0;
    std::string body;
    std::string effective_url;
    std::string content_type;
    // Location target of a 3xx response that was not followed automatically.
    std::string redirect_url;
    std::string error;
    // True when no HTTP response was received because of DNS/connect/TLS/
    // timeout failures, so another network route may succeed.
    bool transport_error = false;
    // Human readable route that produced this response, e.g. "代理".
    std::string route;

    bool ok() const { return error.empty() && status_code >= 200 && status_code < 300; }
};

class HttpClient {
public:
    using RouteCallback = std::function<void(const std::string& route_label)>;

    HttpClient();
    ~HttpClient();

    bool Initialize(std::string* error = nullptr);
    void Shutdown();

    // Performs exactly one request using every mechanism enabled in options.
    HttpResponse Get(const std::string& url, const std::string& cookie_header,
                     const std::string& ca_path, const HttpRequestOptions& options,
                     std::size_t max_body_bytes = 8U * 1024U * 1024U,
                     const std::atomic_bool* cancel = nullptr);

    // Compatibility overload. It uses direct networking, built-in hosts plus
    // DoH, IPv4, and the default 10/20 second timeouts.
    HttpResponse Get(const std::string& url, const std::string& cookie_header,
                     const std::string& ca_path, std::size_t max_body_bytes = 8U * 1024U * 1024U,
                     const std::atomic_bool* cancel = nullptr);

    // Tries the enabled routes one by one (see NetworkPlan.h) and only falls
    // back on transport failures. The last successful route is tried first on
    // later requests. on_route is invoked from the calling worker thread.
    HttpResponse GetWithFallback(const std::string& url, const std::string& cookie_header,
                                 const std::string& ca_path, const HttpRequestOptions& options,
                                 std::size_t max_body_bytes, const std::atomic_bool* cancel,
                                 const RouteCallback& on_route = {});

    // Forget the remembered route, e.g. after the user edits network settings.
    void ResetRoutePreference();

private:
    HttpResponse GetWithFallback(const std::string& url, const std::string& cookie_header,
                                 const std::string& ca_path, const HttpRequestOptions& options,
                                 std::size_t max_body_bytes, const std::atomic_bool* cancel,
                                 const RouteCallback& on_route, int redirects_left);

    bool initialized_ = false;
    std::atomic_int preferred_route_{-1};
};

}  // namespace ehviewer
