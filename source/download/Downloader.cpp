#include "Downloader.h"

#include "core/FileUtil.h"
#include "core/GalleryManifest.h"
#include "core/I18n.h"
#include "core/SpiderInfo.h"
#include "net/NetworkPlan.h"

#include <algorithm>
#include <iterator>
#include <utility>
#include <vector>

namespace ehviewer {
using i18n::T;
namespace {

constexpr std::size_t kMaxImageBytes = 48U * 1024U * 1024U;
constexpr const char* kSpiderInfoName = "spider_info.ehviewer";
constexpr const char* kManifestName = "manifest.json";
const char* const kImageExtensions[] = {"jpg", "png", "gif", "webp"};

std::string Join(const std::string& directory, const std::string& name) {
    return directory + "/" + name;
}

std::string FindExistingPage(const std::string& directory, int index) {
    for (const char* extension : kImageExtensions) {
        const std::string name = StorageLayout::PageFileName(index, extension);
        if (file::FileSize(Join(directory, name)) > 0) return name;
    }
    return {};
}

std::string FindCover(const std::string& directory) {
    for (const char* extension : kImageExtensions) {
        const std::string name = std::string("cover.") + extension;
        if (file::FileSize(Join(directory, name)) > 0) return name;
    }
    return {};
}

std::string DescribeHttpFailure(const std::string& what, const HttpResponse& response) {
    if (!response.error.empty()) return what + ": " + response.error;
    return what + " HTTP " + std::to_string(response.status_code);
}

}  // namespace

Downloader::Downloader(HttpClient* http, std::string ca_path, std::string library_root)
    : http_(http), ca_path_(std::move(ca_path)), layout_(std::move(library_root)) {}

Downloader::~Downloader() { Stop(); }

void Downloader::Start() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (worker_.joinable()) return;
    stopping_ = false;
    worker_ = std::thread(&Downloader::WorkerLoop, this);
}

void Downloader::Stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
        queue_.clear();
    }
    cancel_.store(true);
    wake_.notify_all();
    if (worker_.joinable()) worker_.join();
}

bool Downloader::Enqueue(DownloadJob job) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const std::int64_t gid = job.gallery.gid;
        if (progress_.active && progress_.gid == gid) return false;
        for (const DownloadJob& queued : queue_) {
            if (queued.gallery.gid == gid) return false;
        }
        queue_.push_back(std::move(job));
        progress_.queued = queue_.size();
    }
    ++version_;
    wake_.notify_one();
    return true;
}

void Downloader::CancelAll() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.clear();
        progress_.queued = 0;
    }
    cancel_.store(true);
    ++version_;
}

bool Downloader::Pause(std::int64_t gid) {
    bool stopped = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto it = queue_.begin(); it != queue_.end(); ++it) {
            if (it->gallery.gid == gid) {
                queue_.erase(it);
                progress_.queued = queue_.size();
                stopped = true;
                break;
            }
        }
        // Under the lock, so the flag cannot leak into the next job (the worker
        // resets it under the same lock when it starts one).
        if (progress_.active && progress_.gid == gid) {
            cancel_.store(true);
            stopped = true;
        }
    }
    if (stopped) ++version_;
    return stopped;
}

bool Downloader::IsQueued(std::int64_t gid) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const DownloadJob& queued : queue_) {
        if (queued.gallery.gid == gid) return true;
    }
    return false;
}

DownloadProgress Downloader::Snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    DownloadProgress snapshot = progress_;
    if (snapshot.active) snapshot.job_bytes = bytes_.load() - job_start_bytes_;
    for (const DownloadJob& queued : queue_) snapshot.queued_gids.push_back(queued.gallery.gid);
    return snapshot;
}

void Downloader::SetPhase(const std::string& phase, int done, int total) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        progress_.phase = phase;
        progress_.done = done;
        progress_.total = total;
    }
    ++version_;
}

void Downloader::WorkerLoop() {
    for (;;) {
        DownloadJob job;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            if (stopping_) return;
            job = std::move(queue_.front());
            queue_.pop_front();
            // A cancel issued while idle must not abort the next job.
            cancel_.store(false);
            progress_.active = true;
            progress_.gid = job.gallery.gid;
            progress_.title = job.gallery.title;
            progress_.phase = T("准备中");
            job_start_bytes_ = bytes_.load();
            progress_.done = 0;
            progress_.total = 0;
            progress_.queued = queue_.size();
        }
        ++version_;

        std::string message;
        bool success = false;
        try {
            success = RunJob(job, &message);
        } catch (const std::exception& error) {
            message = std::string(T("下载异常: ")) + error.what();
        } catch (...) {
            message = T("下载发生未知异常");
        }
        if (!success && cancel_.load()) message = T("下载已暂停，已完成的页面会在继续时沿用");

        {
            std::lock_guard<std::mutex> lock(mutex_);
            progress_.active = false;
            progress_.phase.clear();
            progress_.last_message = message;
            progress_.last_success = success;
            progress_.queued = queue_.size();
            if (success) ++progress_.completed_count;
        }
        ++version_;
    }
}

HttpResponse Downloader::SiteGet(const DownloadJob& job, const std::string& url,
                                 std::size_t limit) {
    return http_->GetWithFallback(url, IsSiteHost(url) ? job.cookie_header : std::string(),
                                  ca_path_, job.options, limit, &cancel_);
}

bool Downloader::RunJob(DownloadJob& job, std::string* message) {
    const std::int64_t gid = job.gallery.gid;
    const std::string incoming = layout_.IncomingDirectory(gid);
    const std::string published = layout_.GalleryDirectory(gid);
    std::string error;

    if (file::Exists(Join(published, kManifestName))) {
        *message = T("图库已在本地书库中");
        return true;
    }
    if (!file::CreateDirectoryRecursive(incoming, &error)) {
        *message = error;
        return false;
    }

    if (job.detail.gid == 0) {
        SetPhase(T("读取图库详情"), 0, 0);
        const HttpResponse response = SiteGet(job, job.site_base + job.gallery.path);
        if (!response.ok()) {
            *message = DescribeHttpFailure(T("图库详情"), response);
            return false;
        }
        if (!ParseGalleryDetail(response.body, gid, job.gallery.token, &job.detail, &error)) {
            *message = T("图库详情解析失败: ") + error;
            return false;
        }
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        progress_.title = job.detail.title;
    }

    if (!CollectTokens(job, incoming, &error)) {
        *message = error;
        return false;
    }
    const int total = job.detail.pages;
    const std::string manifest_path = Join(incoming, kManifestName);

    GalleryManifest manifest;
    manifest.gid = gid;
    manifest.token = job.gallery.token;
    manifest.title = job.detail.title;
    manifest.title_jpn = job.detail.title_jpn;
    manifest.category = job.detail.category;
    manifest.total_pages = total;
    manifest.original_directory_name = StorageLayout::OriginalDirectoryName(gid, job.detail.title);
    // Keep the reading position if the gallery was read while downloading.
    std::string saved_text;
    GalleryManifest saved;
    if (file::ReadAll(manifest_path, &saved_text) && GalleryManifest::Parse(saved_text, &saved) &&
        saved.gid == gid && saved.current_page < total) {
        manifest.current_page = saved.current_page;
    }

    // The cover first, so the library can show the gallery while it downloads.
    // It is optional; a failure here must not stop the download.
    std::string cover = FindCover(incoming);
    if (cover.empty() && !job.detail.cover_url.empty() && !cancel_.load()) {
        SetPhase(T("下载封面"), 0, total);
        std::string bytes;
        if (FetchImage(job, job.site_base + job.gallery.path, job.detail.cover_url, &bytes, &error)) {
            cover = "cover." + StorageLayout::DetectImageExtension(bytes);
            if (!file::WriteAllAtomic(Join(incoming, cover), bytes, &error)) cover.clear();
        }
    }
    std::vector<ManifestFile> files;
    if (!cover.empty()) {
        files.push_back({cover, manifest.original_directory_name + "/" + cover, "cover", -1,
                         file::FileSize(Join(incoming, cover)), ""});
    }
    // A manifest without pages marks the gallery as "downloading" for the
    // library and the reader; the final one below lists every page.
    manifest.files = files;
    if (!file::Exists(manifest_path) || saved.files.size() != files.size()) {
        if (manifest.Validate(&error)) file::WriteAllAtomic(manifest_path, manifest.SerializeJson(), &error);
    }

    // A failing page is retried, then skipped so the rest still downloads;
    // the gallery is only published once every page is present.
    int failed = 0;
    std::string first_failure;
    std::vector<char> attempted(static_cast<std::size_t>(total), 0);
    for (int count = 0; count < total; ++count) {
        if (cancel_.load()) return false;
        // Read-while-downloading: continue from the page being read, then
        // fill in whatever is left before it.
        const int focus = focus_gid_.load() == gid ? std::max(0, std::min(focus_page_.load(), total - 1)) : 0;
        int index = -1;
        for (int candidate = focus; candidate < total && index < 0; ++candidate)
            if (!attempted[candidate]) index = candidate;
        for (int candidate = 0; candidate < focus && index < 0; ++candidate)
            if (!attempted[candidate]) index = candidate;
        attempted[index] = 1;
        SetPhase(failed > 0 ? T("下载页面（") + std::to_string(failed) + T(" 页失败）") : T("下载页面"), count, total);
        std::string physical_name;
        std::uint64_t size = 0;
        bool ok = false;
        bool fatal = false;
        for (int attempt = 0; attempt < 2 && !ok && !fatal && !cancel_.load(); ++attempt)
            ok = DownloadPage(job, index, incoming, &physical_name, &size, &error, &fatal);
        if (fatal) {
            *message = error;
            return false;
        }
        if (!ok) {
            if (cancel_.load()) return false;
            if (failed++ == 0) first_failure = T("第 ") + std::to_string(index + 1) + T(" 页: ") + error;
            continue;
        }
        const std::string extension = physical_name.substr(physical_name.rfind('.') + 1);
        files.push_back({physical_name,
                         manifest.original_directory_name + "/" +
                             StorageLayout::OriginalPageName(index, extension),
                         "page", index, size, ""});
    }
    if (failed > 0) {
        *message = std::to_string(failed) + T(" 页下载失败（") + first_failure +
                   T("）。已下载的页面可以阅读，再次下载会只重试失败页");
        return false;
    }
    if (cancel_.load()) return false;

    // Re-read the progress the reader may have saved meanwhile.
    if (file::ReadAll(manifest_path, &saved_text) && GalleryManifest::Parse(saved_text, &saved) &&
        saved.gid == gid && saved.current_page < total) {
        manifest.current_page = saved.current_page;
    }
    std::stable_sort(files.begin(), files.end(), [](const ManifestFile& a, const ManifestFile& b) {
        return a.page_index < b.page_index;
    });
    manifest.files = std::move(files);
    SetPhase(T("写入清单"), total, total);
    if (!manifest.Validate(&error) ||
        !file::WriteAllAtomic(Join(incoming, kManifestName), manifest.SerializeJson(), &error)) {
        *message = T("写入 manifest 失败: ") + error;
        return false;
    }
    if (file::Exists(published) && !file::RemoveTree(published, &error)) {
        *message = error;
        return false;
    }
    if (!file::Rename(incoming, published, &error) || !file::CommitDevice(&error)) {
        *message = T("发布图库失败: ") + error;
        return false;
    }
    *message = T("下载完成: ") + job.detail.title;
    return true;
}

bool Downloader::CollectTokens(DownloadJob& job, const std::string& incoming,
                               std::string* error) {
    GalleryDetail& detail = job.detail;
    const std::string spider_path = Join(incoming, kSpiderInfoName);
    // The first detail page holds exactly one preview page worth of tokens.
    const int per_page = static_cast<int>(std::max<std::size_t>(1, detail.page_tokens.size()));

    // Resume: merge tokens collected by an earlier, interrupted run.
    std::string saved;
    SpiderInfo spider;
    if (file::ReadAll(spider_path, &saved) && SpiderInfo::Parse(saved, &spider) &&
        spider.gid == detail.gid) {
        for (const auto& entry : spider.page_tokens) detail.page_tokens.insert(entry);
        if (detail.pages <= 0) detail.pages = spider.pages;
    }

    for (int preview = 1; preview < detail.preview_page_count; ++preview) {
        // Skip preview pages whose tokens are all known already.
        const int first = preview * per_page;
        bool known = detail.pages > 0;
        for (int index = first; known && index < std::min(first + per_page, detail.pages); ++index)
            known = detail.page_tokens.count(index) != 0;
        if (known) continue;
        if (cancel_.load()) {
            *error = T("已取消");
            return false;
        }
        SetPhase(T("收集页面链接"), preview, detail.preview_page_count);
        const HttpResponse response = SiteGet(
            job, job.site_base + job.gallery.path + "?p=" + std::to_string(preview));
        if (!response.ok()) {
            *error = DescribeHttpFailure(T("预览页 ") + std::to_string(preview + 1), response);
            return false;
        }
        ParsePreviewTokens(response.body, detail.gid, &detail.page_tokens);
    }

    if (detail.pages <= 0) detail.pages = static_cast<int>(detail.page_tokens.size());
    if (detail.pages <= 0) {
        *error = T("没有找到任何页面");
        return false;
    }
    for (int index = 0; index < detail.pages; ++index) {
        if (detail.page_tokens.count(index) == 0) {
            *error = T("缺少第 ") + std::to_string(index + 1) + T(" 页的链接");
            return false;
        }
    }

    spider.gid = detail.gid;
    spider.token = job.gallery.token;
    spider.start_page = 0;
    spider.pages = detail.pages;
    spider.preview_pages = detail.preview_page_count;
    spider.preview_per_page = per_page;
    spider.page_tokens = detail.page_tokens;
    for (auto it = spider.page_tokens.begin(); it != spider.page_tokens.end();) {
        it = it->first >= spider.pages ? spider.page_tokens.erase(it) : std::next(it);
    }
    std::string write_error;
    file::WriteAllAtomic(spider_path, spider.Serialize(), &write_error);
    return true;
}

bool Downloader::DownloadPage(const DownloadJob& job, int index, const std::string& incoming,
                              std::string* physical_name, std::uint64_t* size,
                              std::string* error, bool* fatal) {
    const std::string existing = FindExistingPage(incoming, index);
    if (!existing.empty()) {
        *physical_name = existing;
        *size = file::FileSize(Join(incoming, existing));
        return true;
    }

    const std::string page_url = job.site_base + "/s/" + job.detail.page_tokens.at(index) + "/" +
                                 std::to_string(job.detail.gid) + "-" + std::to_string(index + 1);
    std::string bytes;
    std::string skip_key;
    for (int attempt = 0; attempt < 2; ++attempt) {
        const std::string url = attempt == 0 ? page_url : page_url + "?nl=" + skip_key;
        const HttpResponse response = SiteGet(job, url);
        if (!response.ok()) {
            *error = DescribeHttpFailure(T("图片页"), response);
            return false;
        }
        GalleryPage page;
        if (!ParseGalleryPage(response.body, &page, error)) return false;
        if (page.image_url.find("509.gif") != std::string::npos) {
            *error = T("图片配额已用尽 (509)，请稍后再试");
            *fatal = true;
            return false;
        }
        if (FetchImage(job, page_url, page.image_url, &bytes, error)) break;
        // The image server failed; ask the site for another one once.
        if (attempt == 1 || page.skip_hath_key.empty() || cancel_.load()) return false;
        skip_key = page.skip_hath_key;
    }

    *physical_name = StorageLayout::PageFileName(index, StorageLayout::DetectImageExtension(bytes));
    *size = bytes.size();
    return file::WriteAllAtomic(Join(incoming, *physical_name), bytes, error);
}

bool Downloader::FetchImage(const DownloadJob& job, const std::string& page_url,
                            const std::string& image_url, std::string* bytes,
                            std::string* error) {
    HttpRequestOptions options = job.options;
    options.referer = page_url;
    options.download_counter = &bytes_;
    options.total_timeout_seconds = 120;
    const bool site = IsSiteHost(image_url);
    if (!site) {
        // Built-in hosts and DoH only cover site domains; for H@H servers they
        // would just repeat the direct attempt.
        options.builtin_hosts = false;
        options.doh = false;
    }
    HttpResponse response = http_->GetWithFallback(image_url, site ? job.cookie_header : std::string(),
                                                   ca_path_, options, kMaxImageBytes, &cancel_);
    if (!response.ok()) {
        *error = DescribeHttpFailure(T("图片"), response);
        return false;
    }
    if (StorageLayout::DetectImageExtension(response.body).empty()) {
        *error = T("服务器返回的不是图片");
        return false;
    }
    *bytes = std::move(response.body);
    return true;
}

}  // namespace ehviewer
