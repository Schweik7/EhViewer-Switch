#pragma once

#include "core/CookieConfig.h"
#include "core/GalleryParser.h"
#include "core/History.h"
#include "core/Library.h"
#include "core/ListLayout.h"
#include "core/Settings.h"
#include "core/Subscriptions.h"
#include "download/Downloader.h"
#include "download/ThumbnailLoader.h"
#include "net/HttpClient.h"
#include "net/Updater.h"
#include "ui/ReaderView.h"
#include "ui/Ui.h"
#include <string>
#include <vector>
#include <atomic>
#include <future>
#include <memory>
#include <mutex>

class App
{
public:
    App();
    // The NRO's own path (argv[0]), used by the updater.
    void SetSelfPath(const std::string& path) { m_updater.SetSelfPath(path); }
    bool Init();
    void Run();
    void Uninit();

private:
    enum class Screen { GalleryList, GalleryDetail, Comments, Tags, Previews, Library, Reader, Settings, Subscriptions };
    enum class TaskType { None, Login, GalleryList, GalleryDetail, FavoriteSlots, SetFavorite, Rate, Previews };
    // Order matters: ZL/ZR cycle through the online sources; Search joins
    // once used. History is local and opened from the sidebar or Home (L).
    enum class ListSource { Latest, Popular, Watched, Favorites, Toplist, Search, History };
    enum class Picker { None, Favorite, FavoriteFolder, Rating, Categories, ConfirmDelete, ConfirmDeleteSubscription };

    struct TaskResult {
        TaskType type{TaskType::None};
        bool success{};
        std::string status;
        std::vector<ehviewer::GallerySummary> galleries;
        ehviewer::ListNavigation navigation;
        std::string list_url;
        int page_number{1};
        bool append{};  // infinite scrolling: add to the current list
        ehviewer::GalleryDetail detail;
        ehviewer::FavoriteSlots favorite_slots;
        ehviewer::FavoriteFolders favorite_folders;
        bool has_favorite_folders{};
        std::string favorite_name;
        ehviewer::RatingResult rating;
        std::int64_t gid{};
        std::vector<ehviewer::GalleryPreview> previews;
    };

    void ReloadCookieConfig();
    void TestLogin();
    // An empty url loads the first page of the current source.
    void LoadGalleryList(const std::string& url = {}, int page_number = 1, bool append = false);
    // Smooth list scrolling and loading the next page near the end.
    ehviewer::ListGeometry Geometry() const;
    double ListMaxScroll() const;
    void ScrollListToSelection();
    bool AnimateListScroll();
    void MaybeLoadMore();
    void SaveSubscriptions();
    void PromptNewSubscription();
    void SaveCurrentSearchAsSubscription();
    static const char* SourceName(ListSource source);
    std::string SourceUrl() const;
    // Shows source right away (empty list, loading footer) and requests it.
    void OpenListSource(ListSource source);
    std::string FavoriteFolderName(int folder) const;
    void PromptSearch();
    std::string ToplistUrl(int page_number) const;

    // Favorites and rating run as network tasks; pickers are modal overlays.
    template <typename Work>
    void StartTask(TaskType type, const std::string& status, Work work);
    void LoadFavoriteSlots();
    void SetFavorite(int folder);  // -1 removes
    void RateGallery(int rating);  // 1..10 half stars
    void OpenPicker(Picker picker);
    bool HandlePickerInput(std::uint64_t down);

    // Settings, history and other local features.
    void LoadSettings();
    void SaveSettings();
    void ApplySettings();
    std::vector<ehviewer::SettingRow> BuildSettingRows() const;
    void ChangeSetting(std::size_t row, int delta);
    void OpenHistory();
    void RecordHistory(const ehviewer::GalleryDetail& detail);
    void DeleteSelectedLibraryEntry();
    // Queues an unfinished library gallery again; false if it was already queued.
    bool ResumeLibraryDownload(const ehviewer::LibraryEntry& entry, bool quiet = false);
    void PromptReaderJump();
    void UpdateKeepAwake();
    void Navigate(int sidebar_item);
    int CurrentSidebarItem() const;
    void SearchFor(const std::string& query);
    int m_pending_nav{-1};
    void HandleTouch(std::uint64_t* down);
    void LoadSelectedGallery();
    void PollNetworkTask();
    // supersede: a running list/detail request is cancelled and replaced
    // (quick ZL/ZR switching); favorites/rating writes are never interrupted.
    bool CanStartNetworkTask(bool supersede = false);
    // Marks a new task as running and returns its cancel flag.
    std::shared_ptr<std::atomic_bool> BeginNetworkTask(TaskType type, const std::string& status);
    void CancelNetworkTask();
    // Cancels a replaceable task and forgets it (its result is dropped).
    void RetireNetworkTask();
    void ReapRetiredTasks();
    void HandleScreenInput(std::uint64_t down);
    void HandleListInput(std::uint64_t down);
    void HandleSubscriptionsInput(std::uint64_t down);
    void HandleDetailInput(std::uint64_t down);
    void HandleCommentsInput(std::uint64_t down);
    void HandleTagsInput(std::uint64_t down);
    void HandleSettingsInput(std::uint64_t down);
    void HandleLibraryInput(std::uint64_t down);
    void HandlePreviewsInput(std::uint64_t down);
    // Preview screen: loads the next ?p=N page of previews near the end.
    void OpenPreviews();
    void LoadMorePreviews();
    void RequestPreviewThumbnails();
    ehviewer::GallerySummary DetailSummary() const;
    std::size_t m_preview_selected{};
    std::size_t m_preview_first_row{};
    int m_preview_pages_loaded{1};
    bool CheckNetworkReady();
    void Draw();
    std::string SiteBase() const;
    ehviewer::HttpRequestOptions NetworkOptions() const;
    ehviewer::HttpClient::RouteCallback RouteReporter();
    std::string TakeRouteLabel();

    void EnqueueDownload(const ehviewer::GallerySummary& gallery,
                         const ehviewer::GalleryDetail* detail, bool quiet = false);
    void RequestListThumbnails();
    bool PumpThumbnails();
    bool PumpDownloadProgress();
    void RefreshLibrary();
    void LoadLibraryCovers();
    // start_page < 0 resumes from the saved position.
    void OpenReader(std::int64_t gid, const std::string& title, int page_count,
                    Screen return_screen, int start_page = -1);
    void CloseReader();
    // Carries out what the reader asked for (close, jump, save, ...).
    bool HandleReaderAction();
    void RefetchReaderPage(int page);
    void SaveReaderPage(int page);
    void SaveReaderSettings();
    // Resumed once the cancelled job of this gallery has stopped.
    std::int64_t m_resume_after_cancel_gid{};
    bool m_psm_ready{};
    // 0 = not local, 1 = downloading/incomplete, 2 = complete.
    int LocalState(std::int64_t gid) const;

    ehviewer::CookieConfig m_cookies;
    ehviewer::HttpClient m_http;
    ehviewer::Ui m_ui;
    ehviewer::Downloader m_downloader;
    ehviewer::ThumbnailLoader m_thumbnails;
    ehviewer::ReaderView m_reader;
    ehviewer::Updater m_updater;
    unsigned m_updater_version{~0U};
    // The start-up check only reports a newer version, never failures.
    bool m_update_check_quiet{};
    std::string m_update_notice;
    bool PumpUpdater();
    void StartUpdateCheck(bool quiet);
    std::vector<ehviewer::GallerySummary> m_galleries;
    ehviewer::GalleryDetail m_detail;
    std::vector<ehviewer::LibraryEntry> m_library;
    std::size_t m_library_selected{};
    std::int64_t m_reading_gid{};
    ListSource m_list_source{ListSource::Latest};
    std::string m_search_keyword;
    std::string m_list_url;
    ehviewer::ListNavigation m_list_nav;
    int m_list_page_number{1};
    int m_comment_scroll{};
    int m_toplist_period{};  // index into the toplist periods (yesterday first)
    Picker m_picker{Picker::None};
    ehviewer::OverlayMenu m_overlay;
    ehviewer::FavoriteSlots m_favorite_slots;
    // Folder bar of the favorites page; m_favorite_folder -1 shows all.
    ehviewer::FavoriteFolders m_favorite_folders;
    int m_favorite_folder{-1};
    ehviewer::Settings m_settings;
    ehviewer::History m_history;
    ehviewer::Subscriptions m_subscriptions;
    std::size_t m_subscription_selected{};
    double m_list_scroll{};
    double m_list_scroll_target{};
    double m_list_velocity{};
    bool m_list_user_scrolled{};
    bool m_loading_more{};
    unsigned m_load_more_retry_tick{};
    std::size_t m_settings_selected{};
    std::size_t m_tag_selected{};
    bool m_keep_awake_active{};
    // Download speed, sampled once per second from the downloader's counter.
    std::uint64_t m_speed_last_bytes{};
    unsigned m_speed_last_tick{};
    unsigned m_last_byte_tick{};
    double m_speed{};
    bool UpdateDownloadSpeed();
    ehviewer::DownloadProgress DownloadSnapshot() const;
    void PromptProxyUrl();
    // Touch tracking (panel coordinates).
    bool m_touch_down{};
    int m_touch_start_x{};
    int m_touch_start_y{};
    int m_touch_last_x{};
    int m_touch_last_y{};
    bool m_touch_dragging{};
    bool m_touch_long_fired{};
    unsigned m_touch_start_tick{};
    int m_touch_drag_accumulator{};
    Screen m_reader_return{Screen::Library};
    std::string m_status;
    std::string m_last_input{"waiting"};
    Screen m_screen{Screen::Library};
    std::size_t m_selected{};
    unsigned long long m_input_events{};
    TaskType m_task_type{TaskType::None};
    std::future<TaskResult> m_network_task;
    std::shared_ptr<std::atomic_bool> m_cancel_network{std::make_shared<std::atomic_bool>(false)};
    // Cancelled tasks still unwinding; their results are discarded.
    std::vector<std::future<TaskResult>> m_retired_tasks;
    std::string m_task_status;
    std::mutex m_route_mutex;
    std::string m_route_label;
    std::atomic_bool m_route_changed{false};
    unsigned m_download_version{~0U};
    unsigned m_completed_seen{};
    unsigned m_thumb_decoded{};
    unsigned m_thumb_decode_failed{};
    std::string m_thumb_decode_error;
    std::string m_thumb_info;
    bool m_download_active{};
    bool m_socket_ready{};
    bool m_curl_ready{};
};
