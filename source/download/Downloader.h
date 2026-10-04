#pragma once

#include "core/GalleryParser.h"
#include "core/StorageLayout.h"
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

struct DownloadJob {
    GallerySummary gallery;
    // Optional; the worker fetches the detail page when detail.gid == 0.
    GalleryDetail detail;
    std::string site_base;
    std::string cookie_header;
    HttpRequestOptions options;
};

struct DownloadProgress {
    bool active = false;
    std::int64_t gid = 0;
    std::string title;
    std::string phase;
    int done = 0;
    int total = 0;
    std::size_t queued = 0;
    // Galleries waiting in the queue, in order.
    std::vector<std::int64_t> queued_gids;
    // Result of the most recently finished job.
    std::string last_message;
    bool last_success = false;
    // Increases on every published gallery so the library can refresh.
    unsigned completed_count = 0;
    // Image bytes received by the running job.
    std::uint64_t job_bytes = 0;
    // Filled in by the UI side from BytesDownloaded() samples.
    double speed_bytes_per_second = 0.0;
    int stalled_seconds = 0;
};

// One background worker downloads queued galleries page by page into
// .incoming/g_<gid>, writes manifest.json, then publishes the directory to
// galleries/g_<gid>. Pages already present in .incoming are kept, so a
// canceled or interrupted download resumes where it stopped.
class Downloader {
public:
    Downloader(HttpClient* http, std::string ca_path, std::string library_root);
    ~Downloader();

    void Start();
    void Stop();
    // Returns false if the gallery is already queued or downloading.
    bool Enqueue(DownloadJob job);
    // Cancels the running job and clears the queue.
    void CancelAll();
    // Stops one gallery: drops it from the queue, or cancels it if it is the
    // running job (the worker then moves on). Returns false if it was neither.
    bool Pause(std::int64_t gid);
    bool IsQueued(std::int64_t gid) const;
    // The reader reports the page it shows; the job for gid downloads from
    // there onwards first.
    void SetFocus(std::int64_t gid, int page) {
        focus_page_.store(page);
        focus_gid_.store(gid);
    }

    DownloadProgress Snapshot() const;
    // Total image bytes received since start, updated during transfers.
    std::uint64_t BytesDownloaded() const { return bytes_.load(); }
    // Changes whenever Snapshot() would return something different.
    unsigned Version() const { return version_.load(); }

private:
    void WorkerLoop();
    bool RunJob(DownloadJob& job, std::string* message);
    bool CollectTokens(DownloadJob& job, const std::string& incoming, std::string* error);
    bool DownloadPage(const DownloadJob& job, int index, const std::string& incoming,
                      std::string* physical_name, std::uint64_t* size, std::string* error,
                      bool* fatal);
    bool FetchImage(const DownloadJob& job, const std::string& page_url,
                    const std::string& image_url, std::string* bytes, std::string* error);
    HttpResponse SiteGet(const DownloadJob& job, const std::string& url,
                         std::size_t limit = 8U * 1024U * 1024U);
    void SetPhase(const std::string& phase, int done, int total);

    HttpClient* http_;
    std::string ca_path_;
    StorageLayout layout_;

    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<DownloadJob> queue_;
    DownloadProgress progress_;
    std::thread worker_;
    bool stopping_ = false;
    std::atomic_bool cancel_{false};
    std::atomic_uint version_{0};
    std::atomic<std::int64_t> focus_gid_{0};
    std::atomic_int focus_page_{0};
    std::atomic<std::uint64_t> bytes_{0};
    std::uint64_t job_start_bytes_ = 0;  // guarded by mutex_
};

}  // namespace ehviewer
