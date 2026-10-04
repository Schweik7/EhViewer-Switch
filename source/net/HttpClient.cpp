#include "HttpClient.h"
#include "NetworkPlan.h"
#include "Version.h"

#include <curl/curl.h>

#include <algorithm>
#include <cctype>
#include <cstring>

namespace ehviewer {
namespace {

constexpr const char* kDohUrl = "https://77.88.8.1/dns-query";

// Kept in sync with the Android port's EhHosts IPv4 entries. CURLOPT_RESOLVE
// preserves the TLS host name while avoiding unreliable console DNS.
constexpr const char* kBuiltInHosts[] = {
    "e-hentai.org:443:104.20.18.168,104.20.19.168,172.66.132.196,172.66.140.62,172.67.2.238",
    "repo.e-hentai.org:443:104.20.18.168,104.20.19.168,172.67.2.238",
    "forums.e-hentai.org:443:172.66.132.196,172.66.140.62",
    "upld.e-hentai.org:443:89.149.221.236,95.211.208.236",
    "ehgt.org:443:109.236.85.28,62.112.8.21,89.39.106.43",
    "exhentai.org:443:178.175.128.251,178.175.128.252,178.175.128.253,178.175.128.254,178.175.129.251,178.175.129.252,178.175.129.253,178.175.129.254,178.175.132.19,178.175.132.20,178.175.132.21,178.175.132.22",
    "upld.exhentai.org:443:178.175.132.22,178.175.129.254,178.175.128.254",
    "s.exhentai.org:443:178.175.129.253,178.175.129.254,178.175.128.253,178.175.128.254,178.175.132.21,178.175.132.22",
};

struct WriteContext {
    std::string* body;
    std::size_t limit;
    bool overflow = false;
};

std::size_t WriteBody(char* data, std::size_t size, std::size_t count, void* opaque) {
    WriteContext* context = static_cast<WriteContext*>(opaque);
    if (size != 0 && count > static_cast<std::size_t>(-1) / size) {
        context->overflow = true;
        return 0;
    }
    const std::size_t bytes = size * count;
    if (context->body->size() > context->limit ||
        bytes > context->limit - context->body->size()) {
        context->overflow = true;
        return 0;
    }
    context->body->append(data, bytes);
    return bytes;
}

struct ProgressContext {
    const std::atomic_bool* cancel = nullptr;
    std::atomic<std::uint64_t>* counter = nullptr;
    curl_off_t reported = 0;
};

int OnProgress(void* opaque, curl_off_t, curl_off_t downloaded, curl_off_t, curl_off_t) {
    auto* context = static_cast<ProgressContext*>(opaque);
    if (context->counter != nullptr && downloaded > context->reported) {
        context->counter->fetch_add(static_cast<std::uint64_t>(downloaded - context->reported));
        context->reported = downloaded;
    }
    return context->cancel != nullptr && context->cancel->load() ? 1 : 0;
}

bool StartsWith(const std::string& value, const char* prefix) {
    const std::size_t length = std::strlen(prefix);
    return value.size() >= length && value.compare(0, length, prefix) == 0;
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
        if (StartsWith(proxy, scheme) && proxy.size() > std::strlen(scheme)) return true;
    }
    return false;
}

std::string DescribeCurlError(CURLcode code, bool overflow, long timeout_seconds) {
    if (overflow) return "HTTP response exceeds size limit";
    if (code == CURLE_ABORTED_BY_CALLBACK) return "Request canceled";
    if (code == CURLE_OPERATION_TIMEDOUT) {
        return "Request timed out after " + std::to_string(timeout_seconds) + " seconds";
    }
    if (code == CURLE_COULDNT_RESOLVE_HOST) return "DNS lookup failed for the destination host";
    if (code == CURLE_COULDNT_RESOLVE_PROXY) return "DNS lookup failed for the proxy";
    if (code == CURLE_COULDNT_CONNECT) return "Could not connect to the destination or proxy";
    if (code == CURLE_SSL_CONNECT_ERROR) return "TLS connection failed";
    if (code == CURLE_PEER_FAILED_VERIFICATION) return "TLS certificate verification failed";
    if (code == CURLE_SEND_ERROR) return "Network send failed";
    if (code == CURLE_RECV_ERROR) return "Network receive failed";
    if (code == CURLE_TOO_MANY_REDIRECTS) return "Too many HTTP redirects";
    if (code == CURLE_UNSUPPORTED_PROTOCOL) return "Unsupported network protocol";
    return std::string("Network request failed: ") + curl_easy_strerror(code);
}

bool IsTransportError(CURLcode code) {
    switch (code) {
        case CURLE_COULDNT_RESOLVE_HOST:
        case CURLE_COULDNT_RESOLVE_PROXY:
        case CURLE_COULDNT_CONNECT:
        case CURLE_OPERATION_TIMEDOUT:
        case CURLE_SSL_CONNECT_ERROR:
        case CURLE_SEND_ERROR:
        case CURLE_RECV_ERROR:
        case CURLE_GOT_NOTHING:
        case CURLE_PARTIAL_FILE:
            return true;
        default:
            return false;
    }
}

bool AppendResolveEntries(curl_slist** list) {
    for (const char* entry : kBuiltInHosts) {
        curl_slist* appended = curl_slist_append(*list, entry);
        if (appended == nullptr) return false;
        *list = appended;
    }
    return true;
}

}  // namespace

bool HttpRequestOptions::Validate(std::string* error) const {
    if (!IsSupportedProxy(proxy)) {
        if (error) *error = "Proxy must use an HTTP or SOCKS URL";
        return false;
    }
    if (connect_timeout_seconds <= 0 || connect_timeout_seconds > 120) {
        if (error) *error = "Connect timeout must be between 1 and 120 seconds";
        return false;
    }
    if (total_timeout_seconds <= 0 || total_timeout_seconds > 600 ||
        total_timeout_seconds < connect_timeout_seconds) {
        if (error) *error = "Request timeout must be between the connect timeout and 600 seconds";
        return false;
    }
    return true;
}

HttpClient::HttpClient() = default;
HttpClient::~HttpClient() { Shutdown(); }

bool HttpClient::Initialize(std::string* error) {
    if (initialized_) return true;
    const CURLcode result = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (result != CURLE_OK) {
        if (error) *error = curl_easy_strerror(result);
        return false;
    }
    initialized_ = true;
    return true;
}

void HttpClient::Shutdown() {
    if (!initialized_) return;
    curl_global_cleanup();
    initialized_ = false;
}

HttpResponse HttpClient::Get(const std::string& url, const std::string& cookie_header,
                             const std::string& ca_path, const HttpRequestOptions& options,
                             std::size_t max_body_bytes, const std::atomic_bool* cancel) {
    HttpResponse response;
    if (!initialized_) {
        response.error = "HTTP client is not initialized";
        return response;
    }
    if (!options.Validate(&response.error)) return response;

    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
        response.error = "Cannot create HTTP request";
        return response;
    }

    // Domain fronting: connect to the literal address so no host name appears
    // in the TLS handshake, and name the real site in the Host header.
    const bool fronted = !options.front_address.empty();
    const std::string request_url = fronted ? FrontedUrl(url, options.front_address) : url;
    const std::string host = UrlHost(url);
    if (fronted && (request_url.empty() || host.empty())) {
        response.error = "Invalid URL for domain fronting";
        curl_easy_cleanup(curl);
        return response;
    }

    WriteContext context{&response.body, max_body_bytes, false};
    curl_slist* headers = nullptr;
    curl_slist* resolve = nullptr;
    headers = curl_slist_append(headers,
        "Accept: text/html,application/xhtml+xml,application/json;q=0.9,*/*;q=0.8");
    headers = curl_slist_append(headers, "Accept-Language: zh-CN,zh;q=0.9,en;q=0.7");
    if (headers != nullptr && !options.referer.empty())
        headers = curl_slist_append(headers, ("Referer: " + options.referer).c_str());
    if (headers != nullptr && fronted)
        headers = curl_slist_append(headers, ("Host: " + host).c_str());
    const bool post = !options.post_body.empty();
    if (headers != nullptr && post) {
        headers = curl_slist_append(headers, ("Content-Type: " + options.post_content_type).c_str());
        if (headers != nullptr)
            headers = curl_slist_append(headers, ("Origin: " + url.substr(0, url.find("://") + 3) + host).c_str());
    }
    if (headers == nullptr || (options.builtin_hosts && !AppendResolveEntries(&resolve))) {
        response.error = "Not enough memory to configure HTTP request";
        curl_slist_free_all(headers);
        curl_slist_free_all(resolve);
        curl_easy_cleanup(curl);
        return response;
    }

    curl_easy_setopt(curl, CURLOPT_URL, request_url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_USERAGENT,
                     "Mozilla/5.0 (Nintendo Switch; EhViewerSwitch/" EHV_SWITCH_VERSION
                     ") AppleWebKit/537.36");
    // A custom Host header must not leak to another redirect target, so
    // fronted requests report redirects and GetWithFallback follows them.
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, fronted ? 0L : 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, options.connect_timeout_seconds);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, options.total_timeout_seconds);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    // The chain is still verified; only the name check is skipped because the
    // URL host is an address, matching Android's domain fronting mode.
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, fronted ? 0L : 2L);
    if (post) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(options.post_body.size()));
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, options.post_body.c_str());
    }
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteBody);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &context);
    if (options.ipv4_only) curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
    if (resolve != nullptr) curl_easy_setopt(curl, CURLOPT_RESOLVE, resolve);
    if (!options.proxy.empty()) curl_easy_setopt(curl, CURLOPT_PROXY, options.proxy.c_str());
    ProgressContext progress{cancel, options.download_counter, 0};
    if (cancel != nullptr || options.download_counter != nullptr) {
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, OnProgress);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &progress);
    }
    if (!cookie_header.empty()) curl_easy_setopt(curl, CURLOPT_COOKIE, cookie_header.c_str());
    if (!ca_path.empty()) curl_easy_setopt(curl, CURLOPT_CAINFO, ca_path.c_str());

#if LIBCURL_VERSION_NUM >= 0x073e00
    if (options.doh) {
        const CURLcode doh_result = curl_easy_setopt(curl, CURLOPT_DOH_URL, kDohUrl);
        if (doh_result != CURLE_OK) {
            response.error = "DNS-over-HTTPS is not supported by this libcurl build";
            curl_slist_free_all(headers);
            curl_slist_free_all(resolve);
            curl_easy_cleanup(curl);
            return response;
        }
    }
#else
    if (options.doh) {
        response.error = "DNS-over-HTTPS requires libcurl 7.62 or newer";
        curl_slist_free_all(headers);
        curl_slist_free_all(resolve);
        curl_easy_cleanup(curl);
        return response;
    }
#endif

    const CURLcode result = curl_easy_perform(curl);
    if (result != CURLE_OK) {
        response.error = DescribeCurlError(result, context.overflow, options.total_timeout_seconds);
        response.transport_error = !context.overflow && IsTransportError(result);
    } else {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status_code);
        char* effective_url = nullptr;
        curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &effective_url);
        if (effective_url != nullptr) response.effective_url = fronted ? url : effective_url;
        char* redirect_url = nullptr;
        curl_easy_getinfo(curl, CURLINFO_REDIRECT_URL, &redirect_url);
        if (redirect_url != nullptr) response.redirect_url = redirect_url;
        char* content_type = nullptr;
        curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &content_type);
        if (content_type != nullptr) response.content_type = content_type;
    }

    curl_slist_free_all(headers);
    curl_slist_free_all(resolve);
    curl_easy_cleanup(curl);
    return response;
}

HttpResponse HttpClient::Get(const std::string& url, const std::string& cookie_header,
                             const std::string& ca_path, std::size_t max_body_bytes,
                             const std::atomic_bool* cancel) {
    return Get(url, cookie_header, ca_path, HttpRequestOptions(), max_body_bytes, cancel);
}

HttpResponse HttpClient::GetWithFallback(const std::string& url, const std::string& cookie_header,
                                         const std::string& ca_path,
                                         const HttpRequestOptions& options,
                                         std::size_t max_body_bytes,
                                         const std::atomic_bool* cancel,
                                         const RouteCallback& on_route) {
    return GetWithFallback(url, cookie_header, ca_path, options, max_body_bytes, cancel, on_route, 5);
}

HttpResponse HttpClient::GetWithFallback(const std::string& url, const std::string& cookie_header,
                                         const std::string& ca_path,
                                         const HttpRequestOptions& options,
                                         std::size_t max_body_bytes,
                                         const std::atomic_bool* cancel,
                                         const RouteCallback& on_route, int redirects_left) {
    HttpResponse response;
    if (!options.Validate(&response.error)) return response;
    std::vector<NetworkAttempt> plan = BuildAttemptPlan(options, url);
    const int preferred = preferred_route_.load();
    if (preferred >= 0) PreferRoute(&plan, static_cast<RouteKind>(preferred));

    std::string failures;
    for (const NetworkAttempt& attempt : plan) {
        if (cancel != nullptr && cancel->load()) {
            response = HttpResponse();
            response.error = "Request canceled";
            return response;
        }
        if (on_route) on_route(RouteLabel(attempt.kind));
        response = Get(url, cookie_header, ca_path, attempt.options, max_body_bytes, cancel);
        response.route = RouteLabel(attempt.kind);
        if (!response.transport_error) {
            if (response.error.empty())
                preferred_route_.store(static_cast<int>(attempt.kind));
            const bool redirect = response.status_code >= 300 && response.status_code < 400 &&
                                  !response.redirect_url.empty();
            if (redirect && !attempt.options.front_address.empty()) {
                if (redirects_left <= 0) {
                    response.error = "Too many HTTP redirects";
                    return response;
                }
                // Cookies only follow to site hosts, as with automatic redirects
                // that stay on the same domain family.
                const std::string next = response.redirect_url;
                // Like a browser, a redirected POST continues as a GET.
                HttpRequestOptions next_options = options;
                next_options.post_body.clear();
                return GetWithFallback(next, IsSiteHost(next) ? cookie_header : std::string(),
                                       ca_path, next_options, max_body_bytes, cancel, on_route,
                                       redirects_left - 1);
            }
            return response;
        }
        if (!failures.empty()) failures += "; ";
        failures += response.route + ": " + response.error;
    }
    if (plan.size() > 1) response.error = "All network routes failed (" + failures + ")";
    return response;
}

void HttpClient::ResetRoutePreference() { preferred_route_.store(-1); }

}  // namespace ehviewer
