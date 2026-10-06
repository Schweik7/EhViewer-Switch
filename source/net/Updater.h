#pragma once

#include "core/ReleaseInfo.h"
#include "net/HttpClient.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

namespace ehviewer {

// Checks for and downloads new versions, modelled on the MusicPlayer2 Switch
// port: the VPS mirror first, the GitHub release second. The new NRO is only
// written next to the running one (<self>.nro.new) after its size and NRO
// magic are checked, then swapped in (ApplyPendingUpdate). MusicPlayer2 could
// only swap at start-up because its romfs keeps the NRO open; this app has no
// romfs, so the swap is tried right away and repeated at the next start if
// it failed.
class Updater {
public:
    enum class State { Idle, Checking, UpToDate, Available, Downloading, Ready, Failed };
    struct Status {
        State state = State::Idle;
        std::string message;
        std::string latest_version;
        std::string notes;
        std::uint64_t downloaded = 0;
        std::uint64_t total = 0;
        bool from_github = false;
    };

    Updater(HttpClient* http, std::string ca_path);
    ~Updater();

    void SetSelfPath(const std::string& path);
    const std::string& SelfPath() const { return self_path_; }
    bool Busy() const { return busy_.load(); }
    bool StartCheck(const HttpRequestOptions& options);
    // Only after a check found a newer version.
    bool StartInstall(const HttpRequestOptions& options);
    Status Poll() const;
    // Changes whenever Poll() would return something different.
    unsigned Version() const { return version_.load() + static_cast<unsigned>(downloaded_.load() >> 16); }
    void Stop();

    // Call once at start-up: moves a downloaded <self>.new over the NRO.
    // Returns a message for the user, or an empty string if nothing was done.
    std::string ApplyPendingUpdate();

private:
    void Run(void (Updater::*task)());
    void DoCheck();
    void DoInstall();
    bool Query(const std::string& url, bool github, std::string* error);
    bool Download(const std::string& url, std::uint64_t size, std::string* bytes, std::string* error);
    void Set(State state, const std::string& message);

    HttpClient* http_;
    std::string ca_path_;
    std::string self_path_ = "sdmc:/switch/EhViewerSwitch/EhViewerSwitch.nro";
    HttpRequestOptions options_;

    mutable std::mutex mutex_;
    Status status_;
    ReleaseInfo release_;
    std::thread worker_;
    std::atomic_bool busy_{false};
    std::atomic_bool cancel_{false};
    std::atomic_uint version_{0};
    std::atomic<std::uint64_t> downloaded_{0};
};

}  // namespace ehviewer
