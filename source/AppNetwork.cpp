#include "App.h"
#include "AppShared.h"
#include "core/FileUtil.h"
#include "core/GalleryParser.h"
#include "core/HtmlUtil.h"
#include "core/I18n.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <exception>
#include <future>
#include <utility>

#ifdef __SWITCH__
#include <SDL2/SDL.h>
#endif

using ehviewer::i18n::T;
using namespace app_shared;

void App::ReloadCookieConfig()
{
    std::string error;
    m_http.ResetRoutePreference();
    if (!ehviewer::CookieConfig::LoadFromFile(kCookieFile, &m_cookies, &error))
        m_status = error + ". Create config/EhViewerSwitch/cookies.ini";
    else
        m_status = "Cookie configuration loaded: " + m_cookies.RedactedSummary();
}

bool App::CheckNetworkReady()
{
    std::string validation;
    if (!m_cookies.Validate(&validation))
    {
        m_status = "Cookie configuration invalid: " + validation;
        return false;
    }
    if (!m_socket_ready || !m_curl_ready)
    {
        m_status = "Network stack is not ready";
        return false;
    }
    if (!ehviewer::file::Exists(kCaFile))
    {
        m_status = "Missing cacert.pem in config/EhViewerSwitch";
        return false;
    }
    return true;
}

ehviewer::HttpClient::RouteCallback App::RouteReporter()
{
    return [this](const std::string& label) {
        {
            std::lock_guard<std::mutex> lock(m_route_mutex);
            m_route_label = label;
        }
        m_route_changed.store(true);
    };
}

std::string App::TakeRouteLabel()
{
    std::lock_guard<std::mutex> lock(m_route_mutex);
    return m_route_label;
}

void App::TestLogin()
{
    if (!CanStartNetworkTask() || !CheckNetworkReady())
        return;

    const std::string cookie_header = m_cookies.BuildCookieHeader();
    const ehviewer::HttpRequestOptions network_options = NetworkOptions();
    const ehviewer::HttpClient::RouteCallback on_route = RouteReporter();
    // ExHentai is reachable without a proxy (domain fronting); e-hentai.org is
    // not, so test the site that will actually be used.
    const bool exhentai = !m_cookies.igneous.empty();
    const auto cancel = BeginNetworkTask(TaskType::Login, T("正在后台测试 Cookie，B 取消"));
    m_network_task = std::async(std::launch::async,
        [this, cookie_header, network_options, on_route, exhentai, cancel]() {
        TaskResult result;
        result.type = TaskType::Login;
        const ehviewer::HttpResponse response = m_http.GetWithFallback(
            exhentai ? "https://exhentai.org/" : "https://e-hentai.org/home.php", cookie_header,
            kCaFile, network_options, 8U * 1024U * 1024U, cancel.get(), on_route);
        if (!response.error.empty()) result.status = T("登录测试失败: ") + response.error;
        else if (response.status_code != 200)
            result.status = T("登录测试 HTTP ") + std::to_string(response.status_code);
        else if (exhentai && response.body.find("/g/") == std::string::npos)
            // Without valid cookies ExHentai answers with an empty page.
            result.status = T("Cookie 无效或账号没有 ExHentai 权限");
        else if (response.body.find("act=Login") != std::string::npos ||
                 response.body.find("You are currently not logged in") != std::string::npos)
            result.status = T("Cookie 已失效：服务器返回登录页");
        else {
            result.success = true;
            result.status = T("Cookie 登录验证成功（线路：") + T(response.route) + T("）");
        }
        return result;
    });
}

namespace
{
// At most this many cancelled requests may still be unwinding (each holds a
// socket session) before a new one has to wait.
constexpr std::size_t kMaxRetiredTasks = 3;
}

bool App::CanStartNetworkTask(bool supersede)
{
    if (m_task_type == TaskType::None)
        return true;
    // Page loads are replaceable: switching menus quickly cancels the old one.
    const bool replaceable = m_task_type == TaskType::GalleryList || m_task_type == TaskType::GalleryDetail ||
                             m_task_type == TaskType::FavoriteSlots || m_task_type == TaskType::Login;
    ReapRetiredTasks();
    if (supersede && replaceable && m_retired_tasks.size() < kMaxRetiredTasks)
    {
        RetireNetworkTask();
        return true;
    }
    m_status = T("已有网络请求正在进行；按 B 可取消");
    return false;
}

std::shared_ptr<std::atomic_bool> App::BeginNetworkTask(TaskType type, const std::string& status)
{
    // Every task gets its own flag so cancelling an old one never stops the next.
    m_cancel_network = std::make_shared<std::atomic_bool>(false);
    m_task_type = type;
    m_task_status = status;
    m_status = status;
    return m_cancel_network;
}

void App::CancelNetworkTask()
{
    m_cancel_network->store(true);
}

void App::RetireNetworkTask()
{
    if (m_task_type == TaskType::None || !m_network_task.valid())
        return;
    m_cancel_network->store(true);
    m_retired_tasks.push_back(std::move(m_network_task));
    m_task_type = TaskType::None;
    m_loading_more = false;
    if (m_status == m_task_status)
        m_status.clear();
}

void App::ReapRetiredTasks()
{
    for (auto it = m_retired_tasks.begin(); it != m_retired_tasks.end();)
    {
        if (it->wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
        {
            ++it;
            continue;
        }
        try { it->get(); }
        catch (...) {}
        it = m_retired_tasks.erase(it);
    }
}

void App::PollNetworkTask()
{
    if (!m_retired_tasks.empty())
        ReapRetiredTasks();
    if (!m_network_task.valid() ||
        m_network_task.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
        return;
    try
    {
        TaskResult result = m_network_task.get();
        if (result.success && result.type == TaskType::GalleryList)
        {
            if (result.append)
            {
                // Infinite scrolling: keep position, skip galleries already shown.
                for (ehviewer::GallerySummary& gallery : result.galleries)
                {
                    const bool seen = std::any_of(m_galleries.begin(), m_galleries.end(),
                        [&gallery](const ehviewer::GallerySummary& existing) { return existing.gid == gallery.gid; });
                    if (!seen)
                        m_galleries.push_back(std::move(gallery));
                }
            }
            else
            {
                m_galleries = std::move(result.galleries);
                m_selected = 0;
                m_list_scroll = m_list_scroll_target = m_list_velocity = 0.0;
            }
            m_list_url = result.list_url;
            m_list_nav = result.navigation;
            m_list_page_number = result.page_number;
            if (m_list_source == ListSource::Toplist)
            {
                // Toplists page with ?p=N (50 per page) instead of next/prev cursors.
                m_list_nav.next_url = m_galleries.size() >= 50 ? ToplistUrl(m_list_page_number + 1) : "";
                m_list_nav.prev_url = m_list_page_number > 1 ? ToplistUrl(m_list_page_number - 1) : "";
            }
            if (result.has_favorite_folders)
                m_favorite_folders = std::move(result.favorite_folders);
            RequestListThumbnails();
            if (result.append)
                result.status.clear();
        }
        else if (result.success && result.type == TaskType::GalleryDetail)
        {
            m_detail = std::move(result.detail);
            m_screen = Screen::GalleryDetail;
            RecordHistory(m_detail);
            if (m_settings.show_thumbnails && !m_ui.HasThumbnail(m_detail.gid) && !m_detail.cover_url.empty())
                m_thumbnails.Replace({{m_detail.gid, m_detail.cover_url}},
                                     m_cookies.BuildCookieHeader(), NetworkOptions());
        }
        else if (result.success && result.type == TaskType::FavoriteSlots)
        {
            m_favorite_slots = std::move(result.favorite_slots);
            OpenPicker(Picker::Favorite);
        }
        else if (result.success && result.type == TaskType::SetFavorite)
        {
            m_detail.favorite_name = result.favorite_name;
        }
        else if (result.success && result.type == TaskType::Rate)
        {
            char rating[48];
            std::snprintf(rating, sizeof(rating), "%.2f (%d)", result.rating.average, result.rating.count);
            m_detail.rating = rating;
        }
        m_status = std::move(result.status);
    }
    catch (const std::exception& error)
    {
        m_status = std::string(T("后台请求异常: ")) + error.what();
    }
    catch (...)
    {
        m_status = T("后台请求发生未知异常");
    }
    if (m_loading_more && m_task_type == TaskType::GalleryList && !m_status.empty())
    {
        // A failed "load more" waits before retrying instead of looping.
        m_load_more_retry_tick = SDL_GetTicks() + 5000;
    }
    m_loading_more = false;
    m_task_type = TaskType::None;
    if (m_screen != Screen::Reader)
        Draw();
}

std::string App::SiteBase() const
{
    if (m_settings.site == ehviewer::Settings::Site::EHentai || m_cookies.igneous.empty())
        return "https://e-hentai.org";
    return "https://exhentai.org";
}

ehviewer::HttpRequestOptions App::NetworkOptions() const
{
    ehviewer::HttpRequestOptions options;
    // The proxy is opt-in (Settings); cookies.ini only provides a default address.
    if (m_settings.proxy_enabled)
        options.proxy = m_settings.proxy_url.empty() ? m_cookies.proxy : m_settings.proxy_url;
    options.builtin_hosts = m_cookies.builtin_hosts;
    options.doh = m_cookies.doh;
    options.domain_fronting = m_cookies.domain_fronting;
    // Settings override cookies.ini once the user changed them.
    if (m_settings.builtin_hosts >= 0)
        options.builtin_hosts = m_settings.builtin_hosts == 1;
    if (m_settings.doh >= 0)
        options.doh = m_settings.doh == 1;
    if (m_settings.domain_fronting >= 0)
        options.domain_fronting = m_settings.domain_fronting == 1;
    options.ipv4_only = true;
    options.connect_timeout_seconds = 10;
    options.total_timeout_seconds = 20;
    return options;
}

const char* App::SourceName(ListSource source)
{
    switch (source)
    {
    case ListSource::Popular: return T("热门");
    case ListSource::Watched: return T("关注");
    case ListSource::Favorites: return T("收藏");
    case ListSource::Toplist: return T("排行榜");
    case ListSource::Search: return T("搜索");
    case ListSource::History: return T("历史");
    default: return T("首页");
    }
}

std::string App::SourceUrl() const
{
    switch (m_list_source)
    {
    case ListSource::Popular: return SiteBase() + "/popular";
    case ListSource::Watched: return SiteBase() + "/watched";
    case ListSource::Favorites:
        return SiteBase() + "/favorites.php" +
               (m_favorite_folder >= 0 ? "?favcat=" + std::to_string(m_favorite_folder) : std::string());
    case ListSource::Toplist: return ToplistUrl(1);
    case ListSource::Search:
    {
        std::string url = SiteBase() + "/?f_search=" + ehviewer::html::UrlEncode(m_search_keyword);
        if (m_settings.excluded_categories > 0)
            url += "&f_cats=" + std::to_string(m_settings.excluded_categories);
        return url;
    }
    default:
        // The category filter applies to the front page as well.
        return m_settings.excluded_categories > 0
            ? SiteBase() + "/?f_cats=" + std::to_string(m_settings.excluded_categories)
            : SiteBase() + "/";
    }
}

void App::OpenListSource(ListSource source)
{
    // Switch screens at once, like EhViewer: the old list is gone and the
    // footer shows that the new one is loading. Quick ZL/ZR presses step from
    // here and replace the request instead of waiting for it.
    m_list_source = source;
    m_galleries.clear();
    m_selected = 0;
    m_list_scroll = m_list_scroll_target = m_list_velocity = 0.0;
    m_list_url.clear();
    m_list_nav = ehviewer::ListNavigation();
    m_list_page_number = 1;
    m_load_more_retry_tick = 0;
    m_screen = Screen::GalleryList;
    LoadGalleryList();
}

std::string App::FavoriteFolderName(int folder) const
{
    if (folder < 0 || folder >= static_cast<int>(m_favorite_folders.folders.size()))
        return folder < 0 ? T("全部") : "Favorites " + std::to_string(folder);
    return m_favorite_folders.folders[folder].name;
}

std::string App::ToplistUrl(int page_number) const
{
    // Toplists only exist on e-hentai.org (behind Cloudflare: proxy needed).
    std::string url = "https://e-hentai.org/toplist.php?tl=" + std::to_string(kToplistIds[m_toplist_period]);
    if (page_number > 1)
        url += "&p=" + std::to_string(page_number - 1);
    return url;
}

void App::LoadGalleryList(const std::string& url, int page_number, bool append)
{
    if (m_list_source == ListSource::History)
    {
        // Local list: no network request.
        m_galleries = m_history.Entries();
        m_selected = 0;
        m_list_scroll = m_list_scroll_target = m_list_velocity = 0.0;
        m_list_url.clear();
        m_list_nav = ehviewer::ListNavigation();
        m_list_page_number = 1;
        m_screen = Screen::GalleryList;
        m_status = m_galleries.empty() ? T("还没有浏览记录") : "";
        RequestListThumbnails();
        return;
    }
    // A new page replaces a list or detail request that is still running;
    // "load more" only starts when nothing else runs.
    if (!CanStartNetworkTask(!append) || !CheckNetworkReady())
        return;
    const std::string list_url = url.empty() ? SourceUrl() : url;
    const std::string cookie_header = m_cookies.BuildCookieHeader();
    const ehviewer::HttpRequestOptions network_options = NetworkOptions();
    const ehviewer::HttpClient::RouteCallback on_route = RouteReporter();
    const bool favorites = m_list_source == ListSource::Favorites;
    const auto cancel = BeginNetworkTask(TaskType::GalleryList,
        append ? std::string(T("正在加载更多…"))
               : std::string(T("正在加载「")) + T(SourceName(m_list_source)) + T("」，B 取消"));
    m_loading_more = append;
    m_network_task = std::async(std::launch::async,
        [this, cookie_header, list_url, network_options, on_route, page_number, append, favorites, cancel]() {
        TaskResult result;
        result.type = TaskType::GalleryList;
        result.list_url = list_url;
        result.page_number = page_number;
        result.append = append;
        const ehviewer::HttpResponse response = m_http.GetWithFallback(
            list_url, cookie_header, kCaFile, network_options,
            8U * 1024U * 1024U, cancel.get(), on_route);
        if (!response.ok()) {
            result.status = response.error.empty()
                ? T("图库列表 HTTP ") + std::to_string(response.status_code)
                : T("图库列表失败: ") + response.error;
            return result;
        }
        // The folder bar is there even when the chosen folder is empty.
        if (favorites)
            result.has_favorite_folders = ehviewer::ParseFavoriteFolders(response.body, &result.favorite_folders);
        std::string error;
        if (!ehviewer::ParseGalleryList(response.body, &result.galleries, &error)) {
            result.status = response.body.find("No hits found") != std::string::npos ||
                                    response.body.find("No unfiltered results") != std::string::npos
                ? T("没有找到结果")
                : T("图库列表解析失败: ") + error;
            return result;
        }
        result.navigation = ehviewer::ParseListNavigation(response.body);
        result.success = true;
        result.status = T("已加载（线路：") + T(response.route) + T("）");
        return result;
    });
}

void App::LoadSelectedGallery()
{
    if (m_selected >= m_galleries.size())
        return;
    if (!CanStartNetworkTask(true) || !CheckNetworkReady())
        return;
    const ehviewer::GallerySummary gallery = m_galleries[m_selected];
    const std::string cookie_header = m_cookies.BuildCookieHeader();
    const std::string site_base = SiteBase();
    const ehviewer::HttpRequestOptions network_options = NetworkOptions();
    const ehviewer::HttpClient::RouteCallback on_route = RouteReporter();
    const auto cancel = BeginNetworkTask(TaskType::GalleryDetail, T("正在后台加载详情，B 取消"));
    m_network_task = std::async(std::launch::async,
        [this, cookie_header, site_base, network_options, gallery, on_route, cancel]() {
            TaskResult result;
            result.type = TaskType::GalleryDetail;
            // hc=1 includes every comment, not only the top ones.
            const ehviewer::HttpResponse response = m_http.GetWithFallback(
                site_base + gallery.path + "?hc=1", cookie_header, kCaFile, network_options,
                8U * 1024U * 1024U, cancel.get(), on_route);
            if (!response.ok()) {
                result.status = response.error.empty()
                    ? T("图库详情 HTTP ") + std::to_string(response.status_code)
                    : T("图库详情失败: ") + response.error;
                return result;
            }
            std::string error;
            if (!ehviewer::ParseGalleryDetail(response.body, gallery.gid, gallery.token,
                                              &result.detail, &error)) {
                result.status = T("图库详情解析失败: ") + error;
                return result;
            }
            if (result.detail.cover_url.empty())
                result.detail.cover_url = gallery.thumb_url;
            result.success = true;
            result.status = T("图库详情加载完成");
            return result;
        });
}

template <typename Work>
void App::StartTask(TaskType type, const std::string& status, Work work)
{
    if (!CanStartNetworkTask() || !CheckNetworkReady())
        return;
    const auto cancel = BeginNetworkTask(type, status + T("，B 取消"));
    m_network_task = std::async(std::launch::async, [work = std::move(work), cancel]() { return work(cancel.get()); });
}

void App::LoadFavoriteSlots()
{
    const std::string url = SiteBase() + "/gallerypopups.php?gid=" + std::to_string(m_detail.gid) +
                            "&t=" + m_detail.token + "&act=addfav";
    const std::string cookie_header = m_cookies.BuildCookieHeader();
    const ehviewer::HttpRequestOptions options = NetworkOptions();
    const ehviewer::HttpClient::RouteCallback on_route = RouteReporter();
    StartTask(TaskType::FavoriteSlots, T("正在读取收藏夹"), [this, url, cookie_header, options, on_route](const std::atomic_bool* cancel) {
        TaskResult result;
        result.type = TaskType::FavoriteSlots;
        const ehviewer::HttpResponse response = m_http.GetWithFallback(
            url, cookie_header, kCaFile, options, 1U * 1024U * 1024U, cancel, on_route);
        if (!response.ok())
            result.status = response.error.empty() ? T("收藏夹 HTTP ") + std::to_string(response.status_code)
                                                   : T("读取收藏夹失败: ") + response.error;
        else if (!ehviewer::ParseFavoriteSlots(response.body, &result.favorite_slots))
            result.status = T("收藏夹页面解析失败");
        else
        {
            result.success = true;
            result.status = T("选择收藏夹");
        }
        return result;
    });
}

void App::SetFavorite(int folder)
{
    const std::string url = SiteBase() + "/gallerypopups.php?gid=" + std::to_string(m_detail.gid) +
                            "&t=" + m_detail.token + "&act=addfav";
    ehviewer::HttpRequestOptions options = NetworkOptions();
    options.referer = url;
    options.post_content_type = "application/x-www-form-urlencoded";
    options.post_body = "favcat=" + (folder < 0 ? std::string("favdel") : std::to_string(folder)) +
                        "&favnote=&submit=Apply+Changes&update=1";
    const std::string name = folder >= 0 && folder < static_cast<int>(m_favorite_slots.names.size())
        ? m_favorite_slots.names[folder] : std::string();
    const std::string cookie_header = m_cookies.BuildCookieHeader();
    const ehviewer::HttpClient::RouteCallback on_route = RouteReporter();
    StartTask(TaskType::SetFavorite, folder < 0 ? T("正在移出收藏") : T("正在加入收藏"),
              [this, url, cookie_header, options, on_route, name, folder](const std::atomic_bool* cancel) {
        TaskResult result;
        result.type = TaskType::SetFavorite;
        const ehviewer::HttpResponse response = m_http.GetWithFallback(
            url, cookie_header, kCaFile, options, 1U * 1024U * 1024U, cancel, on_route);
        if (!response.ok())
        {
            result.status = response.error.empty() ? T("收藏 HTTP ") + std::to_string(response.status_code)
                                                   : T("收藏失败: ") + response.error;
            return result;
        }
        result.success = true;
        result.favorite_name = name;
        result.status = folder < 0 ? T("已从收藏中移除") : T("已加入收藏：") + name;
        return result;
    });
}

void App::RateGallery(int rating)
{
    const std::string body = ehviewer::BuildRateRequest(m_detail.api_uid, m_detail.api_key, m_detail.gid,
                                                        m_detail.token, rating);
    if (body.empty())
    {
        m_status = T("评分参数无效");
        return;
    }
    const std::string api_url = m_detail.api_url.empty() ? SiteBase() + "/api.php" : m_detail.api_url;
    ehviewer::HttpRequestOptions options = NetworkOptions();
    options.referer = SiteBase() + "/g/" + std::to_string(m_detail.gid) + "/" + m_detail.token + "/";
    options.post_content_type = "application/json";
    options.post_body = body;
    const std::string cookie_header = m_cookies.BuildCookieHeader();
    const ehviewer::HttpClient::RouteCallback on_route = RouteReporter();
    StartTask(TaskType::Rate, T("正在提交评分"), [this, api_url, cookie_header, options, on_route, rating](const std::atomic_bool* cancel) {
        TaskResult result;
        result.type = TaskType::Rate;
        const ehviewer::HttpResponse response = m_http.GetWithFallback(
            api_url, cookie_header, kCaFile, options, 256U * 1024U, cancel, on_route);
        std::string error;
        if (!response.ok())
            result.status = response.error.empty() ? T("评分 HTTP ") + std::to_string(response.status_code)
                                                   : T("评分失败: ") + response.error;
        else if (!ehviewer::ParseRatingResponse(response.body, &result.rating, &error))
            result.status = T("评分失败: ") + error;
        else
        {
            result.success = true;
            char text[64];
            std::snprintf(text, sizeof(text), T("已评分 %.1f 星"), rating / 2.0);
            result.status = text;
        }
        return result;
    });
}
