#include "ThumbnailLoader.h"

#include "core/StorageLayout.h"
#include "net/NetworkPlan.h"

#include <iterator>
#include <utility>

namespace ehviewer {

ThumbnailLoader::ThumbnailLoader(HttpClient* http, std::string ca_path)
    : http_(http), ca_path_(std::move(ca_path)) {}

ThumbnailLoader::~ThumbnailLoader() { Stop(); }

void ThumbnailLoader::Start() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (worker_.joinable()) return;
    stopping_ = false;
    worker_ = std::thread(&ThumbnailLoader::WorkerLoop, this);
}

void ThumbnailLoader::Stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
        pending_.clear();
    }
    cancel_.store(true);
    wake_.notify_all();
    if (worker_.joinable()) worker_.join();
}

void ThumbnailLoader::Replace(std::vector<ThumbnailRequest> requests, std::string cookie_header,
                              HttpRequestOptions options) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_.assign(std::make_move_iterator(requests.begin()),
                        std::make_move_iterator(requests.end()));
        results_.clear();
        stats_ = ThumbnailStats();
        stats_.requested = static_cast<unsigned>(pending_.size());
        cookie_header_ = std::move(cookie_header);
        options_ = std::move(options);
        ++generation_;
    }
    cancel_.store(true);
    wake_.notify_one();
}

bool ThumbnailLoader::Pop(ThumbnailResult* result) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (results_.empty()) return false;
    *result = std::move(results_.front());
    results_.pop_front();
    return true;
}

void ThumbnailLoader::WorkerLoop() {
    for (;;) {
        ThumbnailRequest request;
        std::string cookie_header;
        HttpRequestOptions options;
        unsigned generation = 0;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [this] { return stopping_ || !pending_.empty(); });
            if (stopping_) return;
            request = std::move(pending_.front());
            pending_.pop_front();
            cookie_header = cookie_header_;
            options = options_;
            generation = generation_;
            cancel_.store(false);
        }

        options.connect_timeout_seconds = 10;
        options.total_timeout_seconds = 30;
        const bool site = IsSiteHost(request.url);
        HttpResponse response = http_->GetWithFallback(
            request.url, site ? cookie_header : std::string(), ca_path_, options,
            4U * 1024U * 1024U, &cancel_);
        std::string error;
        if (!response.error.empty())
            error = response.error;
        else if (!response.ok())
            error = "HTTP " + std::to_string(response.status_code);
        else if (StorageLayout::DetectImageExtension(response.body).empty())
            error = "不是图片 (" + response.content_type + ", " +
                    std::to_string(response.body.size()) + " B)";

        std::lock_guard<std::mutex> lock(mutex_);
        if (generation != generation_ || stopping_) continue;
        if (!error.empty()) {
            ++stats_.failed;
            stats_.last_error = error + " [" + response.route + "]";
            continue;
        }
        ++stats_.fetched;
        results_.push_back({request.key, std::move(response.body)});
    }
}

ThumbnailStats ThumbnailLoader::Stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}

}  // namespace ehviewer
