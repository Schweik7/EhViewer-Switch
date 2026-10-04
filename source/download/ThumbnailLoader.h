#pragma once

#include "net/HttpClient.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace ehviewer {

struct ThumbnailRequest {
    std::int64_t key = 0;
    std::string url;
};

// Counters for the current request batch, shown on the list screen so
// failures on the console can be diagnosed without a debugger.
struct ThumbnailStats {
    unsigned requested = 0;
    unsigned fetched = 0;
    unsigned failed = 0;
    std::string last_error;
};

struct ThumbnailResult {
    std::int64_t key = 0;
    // Encoded image bytes; decoding happens on the UI thread with SDL_image.
    std::string bytes;
};

// Fetches list thumbnails on one background thread. Replace() drops pending
// work, so scrolling or reloading a list never waits for stale images.
class ThumbnailLoader {
public:
    ThumbnailLoader(HttpClient* http, std::string ca_path);
    ~ThumbnailLoader();

    void Start();
    void Stop();
    void Replace(std::vector<ThumbnailRequest> requests, std::string cookie_header,
                 HttpRequestOptions options);
    bool Pop(ThumbnailResult* result);
    ThumbnailStats Stats() const;

private:
    void WorkerLoop();

    HttpClient* http_;
    std::string ca_path_;
    mutable std::mutex mutex_;
    ThumbnailStats stats_;
    std::condition_variable wake_;
    std::deque<ThumbnailRequest> pending_;
    std::deque<ThumbnailResult> results_;
    std::string cookie_header_;
    HttpRequestOptions options_;
    unsigned generation_ = 0;
    bool stopping_ = false;
    std::atomic_bool cancel_{false};
    std::thread worker_;
};

}  // namespace ehviewer
