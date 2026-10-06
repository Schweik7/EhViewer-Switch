#include "Updater.h"

#include "Version.h"
#include "core/FileUtil.h"
#include "core/I18n.h"

#include <cstdio>
#include <utility>

namespace ehviewer {
using i18n::T;
namespace {

constexpr std::size_t kMaxNroBytes = 64U * 1024U * 1024U;

bool FileLooksLikeNro(const std::string& path) {
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) return false;
    char head[0x20] = {};
    const std::size_t read = std::fread(head, 1, sizeof(head), file);
    std::fclose(file);
    return LooksLikeNro(std::string(head, read));
}

}  // namespace

Updater::Updater(HttpClient* http, std::string ca_path) : http_(http), ca_path_(std::move(ca_path)) {}

Updater::~Updater() { Stop(); }

void Updater::SetSelfPath(const std::string& path) {
    // hbmenu passes the NRO path as argv[0]; nxlink passes nothing useful.
    if (path.size() > 4 && path.compare(path.size() - 4, 4, ".nro") == 0) self_path_ = path;
}

void Updater::Stop() {
    cancel_.store(true);
    if (worker_.joinable()) worker_.join();
    cancel_.store(false);
}

void Updater::Set(State state, const std::string& message) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        status_.state = state;
        status_.message = message;
    }
    ++version_;
}

Updater::Status Updater::Poll() const {
    std::lock_guard<std::mutex> lock(mutex_);
    Status status = status_;
    if (status.state == State::Downloading) status.downloaded = downloaded_.load();
    return status;
}

void Updater::Run(void (Updater::*task)()) {
    if (worker_.joinable()) worker_.join();
    cancel_.store(false);
    busy_.store(true);
    worker_ = std::thread([this, task]() {
        (this->*task)();
        busy_.store(false);
        ++version_;
    });
}

bool Updater::StartCheck(const HttpRequestOptions& options) {
    if (busy_.load()) return false;
    options_ = options;
    Set(State::Checking, T("正在检查更新…"));
    Run(&Updater::DoCheck);
    return true;
}

bool Updater::StartInstall(const HttpRequestOptions& options) {
    if (busy_.load()) return false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (status_.state != State::Available) return false;
    }
    options_ = options;
    downloaded_.store(0);
    Set(State::Downloading, T("正在下载新版本…"));
    Run(&Updater::DoInstall);
    return true;
}

bool Updater::Query(const std::string& url, bool github, std::string* error) {
    HttpRequestOptions options = options_;
    options.total_timeout_seconds = 20;
    const HttpResponse response = http_->GetWithFallback(url, std::string(), ca_path_, options, 1U * 1024U * 1024U,
                                                         &cancel_);
    if (!response.ok()) {
        *error = response.error.empty() ? "HTTP " + std::to_string(response.status_code) : response.error;
        return false;
    }
    ReleaseInfo info;
    if (!ParseReleaseInfo(response.body, kUpdateAssetName, &info, error)) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    release_ = info;
    status_.latest_version = info.tag;
    status_.notes = info.notes;
    status_.total = info.asset_size;
    status_.from_github = github;
    return true;
}

void Updater::DoCheck() {
    // The VPS answers quickly in mainland China; GitHub is the fallback.
    std::string error;
    bool ok = Query(kUpdateMirrorUrl, false, &error);
    if (!ok && !cancel_.load()) {
        std::string github_error;
        ok = Query(kUpdateGithubUrl, true, &github_error);
        if (!ok) error = T("服务器: ") + error + T("；GitHub: ") + github_error;
    }
    if (cancel_.load()) return;
    if (!ok) {
        Set(State::Failed, T("检查更新失败：") + error);
        return;
    }
    Status status = Poll();
    const std::string source = status.from_github ? std::string(T("（GitHub）")) : std::string();
    if (!IsNewerVersion(status.latest_version, EHV_SWITCH_VERSION)) {
        Set(State::UpToDate, T("已是最新版本 ") + std::string(EHV_SWITCH_VERSION) + source);
    } else if (release_.asset_url.empty()) {
        Set(State::Failed, T("发现 ") + status.latest_version + T("，但没有附带 NRO"));
    } else {
        Set(State::Available, T("发现新版本 ") + status.latest_version + source);
    }
}

bool Updater::Download(const std::string& url, std::uint64_t size, std::string* bytes, std::string* error) {
    HttpRequestOptions options = options_;
    options.total_timeout_seconds = 600;
    options.download_counter = &downloaded_;
    downloaded_.store(0);
    const HttpResponse response = http_->GetWithFallback(url, std::string(), ca_path_, options, kMaxNroBytes,
                                                         &cancel_);
    if (!response.ok()) {
        *error = response.error.empty() ? "HTTP " + std::to_string(response.status_code) : response.error;
        return false;
    }
    if (!LooksLikeNro(response.body)) {
        *error = T("下载到的文件不是 NRO");
        return false;
    }
    // A truncated download must never replace the app.
    if (size > 0 && response.body.size() != size) {
        *error = T("文件大小不对 ") + std::to_string(response.body.size()) + " / " + std::to_string(size);
        return false;
    }
    *bytes = response.body;
    return true;
}

void Updater::DoInstall() {
    ReleaseInfo release;
    bool from_github = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        release = release_;
        from_github = status_.from_github;
    }
    std::string bytes;
    std::string error;
    bool ok = Download(release.asset_url, release.asset_size, &bytes, &error);
    // The other source may still work (GitHub's asset CDN often drops halfway).
    if (!ok && !cancel_.load()) {
        std::string other_error;
        if (Query(from_github ? kUpdateMirrorUrl : kUpdateGithubUrl, !from_github, &other_error)) {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                release = release_;
                status_.state = State::Downloading;
                status_.message = T("改用备用源下载…");
            }
            ++version_;
            if (!release.asset_url.empty() && IsNewerVersion(release.tag, EHV_SWITCH_VERSION))
                ok = Download(release.asset_url, release.asset_size, &bytes, &other_error);
        }
        if (!ok) error += T("；备用源: ") + other_error;
    }
    if (cancel_.load()) return;
    if (!ok) {
        Set(State::Failed, T("下载失败：") + error);
        return;
    }
    std::string write_error;
    if (!file::WriteAllAtomic(self_path_ + ".new", bytes, &write_error) || !file::CommitDevice(&write_error)) {
        Set(State::Failed, T("保存更新失败：") + write_error);
        return;
    }
    // This app has no romfs mounted from its own NRO, so the file can usually
    // be swapped right away; if not, the next start does it.
    ApplyPendingUpdate();
    if (!file::Exists(self_path_ + ".new"))
        Set(State::Ready, T("已更新到 ") + release.tag + T("，退出后重新打开即可使用"));
    else
        Set(State::Ready, T("已下载 ") + release.tag + T("，下次启动时安装"));
}

std::string Updater::ApplyPendingUpdate() {
    const std::string pending = self_path_ + ".new";
    if (!file::Exists(pending)) return {};
    const std::uint64_t size = file::FileSize(pending);
    if (size < 1024U * 1024U || !FileLooksLikeNro(pending)) {
        file::Remove(pending);
        return T("更新文件不完整，已丢弃");
    }
    // The running image is already in memory, so the file can be replaced.
    const std::string backup = self_path_ + ".bak";
    std::string error;
    file::Remove(backup);
    const bool had_old = file::Exists(self_path_);
    if (had_old && !file::Rename(self_path_, backup, &error))
        return T("无法替换旧版本，请手动把 ") + pending + T(" 改名为 ") + self_path_;
    if (!file::Rename(pending, self_path_, &error) || file::FileSize(self_path_) != size) {
        if (had_old) {
            file::Remove(self_path_);
            file::Rename(backup, self_path_, &error);
        }
        file::CommitDevice(&error);
        return T("更新写入失败，已还原原有版本");
    }
    file::Remove(backup);
    file::CommitDevice(&error);
    return T("已安装新版本，下次启动生效");
}

}  // namespace ehviewer
